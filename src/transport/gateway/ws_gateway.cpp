#include "transport/gateway/ws_gateway.h"

#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>

#include "net/net_platform.h"
#include "common/framing.h"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace transport::gateway {

struct WsGateway::Impl {
  asio::io_context ioc;
  tcp::acceptor acceptor{ioc};
  std::thread th;
  int ws_port = 0;

  std::string tcp_host;
  int tcp_port = 0;

  std::atomic<bool>* running = nullptr;

  explicit Impl(std::string host, int port, std::atomic<bool>* r)
      : tcp_host(std::move(host)), tcp_port(port), running(r) {}

  static net::socket_t connect_tcp_raw(const std::string& host, int port) {
    if (!net::init()) return net::INVALID_SOCKET_FD;

    net::socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == net::INVALID_SOCKET_FD) return net::INVALID_SOCKET_FD;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
#ifdef _WIN32
    addr.sin_addr.S_un.S_addr = inet_addr(host.c_str());
#else
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
#endif

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      net::close_socket(s);
      return net::INVALID_SOCKET_FD;
    }
    return s;
  }

  void run_accept_loop() {
    while (running->load()) {
      beast::error_code ec;
      tcp::socket sock{ioc};
      acceptor.accept(sock, ec);
      if (!running->load()) break;
      if (ec) continue;

      std::thread([this](tcp::socket client_sock) {
        try {
          auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(client_sock));
          ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
          ws->accept();

          // 내부 TCP 서버 연결(클라 1명당 TCP 1연결)
          net::socket_t tcp_s = connect_tcp_raw(tcp_host, tcp_port);
          if (tcp_s == net::INVALID_SOCKET_FD) {
            std::string err = R"({"v":1,"type":"error","code":"TCP_CONNECT_FAIL","text":"failed to connect tcp backend"})";
            ws->text(true);
            ws->write(asio::buffer(err));
            beast::error_code ec2;
            ws->close(websocket::close_code::normal, ec2);
            return;
          }

          std::mutex ws_write_mx;
          std::atomic<bool> alive{true};

          // TCP -> WS thread
          std::thread t_tcp_to_ws([&]() {
            try {
              while (alive.load() && running->load()) {
                std::string payload;
                if (!framing::recv_message(tcp_s, payload)) break;
                std::lock_guard<std::mutex> lk(ws_write_mx);
                if (!ws->is_open()) break;
                ws->text(true);
                ws->write(asio::buffer(payload));
              }
            } catch (...) {}
            alive = false;
          });

          // WS -> TCP loop (this thread)
          beast::flat_buffer buffer;
          while (alive.load() && running->load() && ws->is_open()) {
            buffer.clear();
            ws->read(buffer);
            std::string payload = beast::buffers_to_string(buffer.data());
            if (!framing::send_message(tcp_s, payload)) break;
          }

          alive = false;
          net::close_socket(tcp_s);
          if (t_tcp_to_ws.joinable()) t_tcp_to_ws.join();

          beast::error_code ec3;
          if (ws->is_open()) ws->close(websocket::close_code::normal, ec3);
        } catch (...) {
          // ignore
        }
      }, std::move(sock)).detach();
    }
  }
};

WsGateway::WsGateway(std::string tcp_host, int tcp_port)
    : tcp_host_(std::move(tcp_host)), tcp_port_(tcp_port) {}

WsGateway::~WsGateway() { stop(); }

bool WsGateway::start(int ws_port) {
  if (running_) return false;

  impl_ = std::make_unique<Impl>(tcp_host_, tcp_port_, &running_);
  impl_->ws_port = ws_port;

  beast::error_code ec;
  tcp::endpoint ep{tcp::v4(), static_cast<unsigned short>(ws_port)};
  impl_->acceptor.open(ep.protocol(), ec);
  if (ec) return false;

  impl_->acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec) return false;

  impl_->acceptor.bind(ep, ec);
  if (ec) return false;

  impl_->acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) return false;

  running_ = true;
  impl_->th = std::thread([this]() { impl_->run_accept_loop(); });

  std::cout << "WS gateway listening on " << ws_port
            << " (tcp backend " << tcp_host_ << ":" << tcp_port_ << ")\n";
  return true;
}

void WsGateway::stop() {
  if (!running_) return;
  running_ = false;

  if (impl_) {
    beast::error_code ec;
    impl_->acceptor.close(ec);
    if (impl_->th.joinable()) impl_->th.join();
    impl_.reset();
  }
}

} // namespace transport::gateway
