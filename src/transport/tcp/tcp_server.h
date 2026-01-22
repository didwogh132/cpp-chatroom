#pragma once
#include <memory>
#include <thread>
#include <atomic>
#include "core/chat_core.h"

namespace transport::tcp {

class TcpServer {
public:
  explicit TcpServer(std::shared_ptr<core::ChatCore> core);
  ~TcpServer();

  bool start(int port);
  void stop();

private:
  std::shared_ptr<core::ChatCore> core_;
  std::atomic<bool> running_{false};
  net::socket_t listen_sock_{net::INVALID_SOCKET_FD};

  void accept_loop_();
};

} // namespace transport::tcp
