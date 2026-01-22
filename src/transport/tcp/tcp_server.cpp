#include "transport/tcp/tcp_server.h"
#include <iostream>
#include <thread>
#include <sstream>

#include "net/net_platform.h"
#include "common/json_io.h"

using jsonio::json;

namespace transport::tcp {

class TcpConnection : public core::Connection {
public:
  TcpConnection(net::socket_t s, std::string id)
    : sock_(s), id_(std::move(id)) {}

  ~TcpConnection() override { close(); }

  bool send(const json& j) override {
    return jsonio::send_json(sock_, j);
  }

  void close() override {
    if (closed_) return;
    closed_ = true;
    if (sock_ != net::INVALID_SOCKET_FD) {
      net::close_socket(sock_);
      sock_ = net::INVALID_SOCKET_FD;
    }
  }

  std::string id() const override { return id_; }

  net::socket_t sock() const { return sock_; }

private:
  net::socket_t sock_{net::INVALID_SOCKET_FD};
  std::string id_;
  bool closed_{false};
};

TcpServer::TcpServer(std::shared_ptr<core::ChatCore> core)
  : core_(std::move(core)) {}

TcpServer::~TcpServer() {
  stop();
}

bool TcpServer::start(int port) {
  if (running_) return false;
  if (!core_) return false;

  if (!net::init()) {
    std::cerr << "net init failed: " << net::last_error_string() << "\n";
    return false;
  }

  listen_sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_sock_ == net::INVALID_SOCKET_FD) {
    std::cerr << "socket() failed: " << net::last_error_string() << "\n";
    net::cleanup();
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));

  int opt = 1;
#ifdef _WIN32
  setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
  setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  if (::bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "bind() failed: " << net::last_error_string() << "\n";
    net::close_socket(listen_sock_);
    listen_sock_ = net::INVALID_SOCKET_FD;
    net::cleanup();
    return false;
  }

  if (::listen(listen_sock_, 16) != 0) {
    std::cerr << "listen() failed: " << net::last_error_string() << "\n";
    net::close_socket(listen_sock_);
    listen_sock_ = net::INVALID_SOCKET_FD;
    net::cleanup();
    return false;
  }

  running_ = true;
  std::thread(&TcpServer::accept_loop_, this).detach();
  std::cout << "TCP server listening on " << port << "\n";
  return true;
}

void TcpServer::stop() {
  if (!running_) return;
  running_ = false;

  if (listen_sock_ != net::INVALID_SOCKET_FD) {
    net::close_socket(listen_sock_);
    listen_sock_ = net::INVALID_SOCKET_FD;
  }
  net::cleanup();
}

void TcpServer::accept_loop_() {
  while (running_) {
    sockaddr_in caddr{};
#ifdef _WIN32
    int clen = sizeof(caddr);
#else
    socklen_t clen = sizeof(caddr);
#endif
    net::socket_t cs = ::accept(listen_sock_, reinterpret_cast<sockaddr*>(&caddr), &clen);
    if (!running_) break;
    if (cs == net::INVALID_SOCKET_FD) continue;

    // id는 간단히 포인터값/카운터도 가능. 여기서는 주소+포트 대신 "tcp:<handle>"
    std::ostringstream oss;
    oss << "tcp:" << reinterpret_cast<uintptr_t>(cs);

    auto conn = std::make_shared<TcpConnection>(cs, oss.str());
    core_->on_connect(conn);

    std::thread([this, conn]() {
      json j;
      while (jsonio::recv_json(conn->sock(), j)) {
        core_->on_message(conn, j);
      }
      core_->on_disconnect(conn);
      conn->close();
    }).detach();
  }
}

} // namespace transport::tcp
