#include "transport/ws/ws_server.h"

#include <iostream>
#include <mutex>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>

#include "core/connection.h"
#include "core/protocol.h"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using nlohmann::json;

namespace transport::ws {

class WsConnection : public core::Connection {
public:
  explicit WsConnection(std::shared_ptr<websocket::stream<tcp::socket>> ws, std::string id)
      : ws_(std::move(ws)), id_(std::move(id)) {}

  bool send(const json& j) override {
    try {
      std::lock_guard<std::mutex> lk(write_mx_);
      if (!ws_ || !ws_->is_open()) return false;
      ws_->text(true);
      ws_->write(asio::buffer(j.dump()));
      return true;
    } catch (...) {
      return false;
    }
  }

  void close() override {
    try {
      std::lock_guard<std::mutex> lk(write_mx_);
      if (ws_ && ws_->is_open()) {
        beast::error_code ec;
        ws_->close(websocket::close_code::normal, ec);
      }
    } catch (...) {}
  }

  std::string id() const override { return id_; }

private:
  std::shared_ptr<websocket::stream<tcp::socket>> ws_;
  std::string id_;
  std::mutex write_mx_;
};

struct WsServer::Impl {
  asio::io_context ioc;
  tcp::acceptor acceptor{ioc};
  std::thread th;
  int port = 0;

  std::shared_ptr<core::ChatCore> core;
  std::atomic<bool>* running = nullptr;

  explicit Impl(std::shared_ptr<core::ChatCore> c, std::atomic<bool>* r)
      : core(std::move(c)), running(r) {}

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
          ws->accept(); // handshake

          // 연결 ID
          std::ostringstream oss;
          try {
            auto ep = ws->next_layer().socket().remote_endpoint();
            oss << "ws:" << ep.address().to_string() << ":" << ep.port();
          } catch (...) {
            oss << "ws:unknown";
          }

          auto conn = std::make_shared<WsConnection>(ws, oss.str());
          core->on_connect(conn);

          beast::flat_buffer buffer;
          while (running->load() && ws->is_open()) {
            buffer.clear();
            ws->read(buffer); // blocking

            std::string payload = beast::buffers_to_string(buffer.data());
            json j;
            try {
              j = json::parse(payload);
            } catch (...) {
              conn->send(core::proto::make_error("", "BAD_JSON", "invalid json"));
              continue;
            }

            core->on_message(conn, j);
          }

          core->on_disconnect(conn);
          conn->close();
        } catch (...) {
          // handshake/read 예외면 그냥 종료
        }
      }, std::move(sock)).detach();
    }
  }
};

WsServer::WsServer(std::shared_ptr<core::ChatCore> core)
    : core_(std::move(core)) {}

WsServer::~WsServer() { stop(); }

bool WsServer::start(int port) {
  if (running_) return false;
  if (!core_) return false;

  impl_ = std::make_unique<Impl>(core_, &running_);
  impl_->port = port;

  beast::error_code ec;
  tcp::endpoint ep{tcp::v4(), static_cast<unsigned short>(port)};
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

  std::cout << "WS server listening on " << port << "\n";
  return true;
}

void WsServer::stop() {
  if (!running_) return;
  running_ = false;

  if (impl_) {
    beast::error_code ec;
    impl_->acceptor.close(ec);
    if (impl_->th.joinable()) impl_->th.join();
    impl_.reset();
  }
}

} // namespace transport::ws
