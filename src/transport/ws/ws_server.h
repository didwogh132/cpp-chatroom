#pragma once
#include <memory>
#include <atomic>
#include <thread>

#include "core/chat_core.h"

namespace transport::ws {

class WsServer {
public:
  explicit WsServer(std::shared_ptr<core::ChatCore> core);
  ~WsServer();

  bool start(int port);
  void stop();

private:
  std::shared_ptr<core::ChatCore> core_;
  std::atomic<bool> running_{false};

  // pimpl-ish (cpp에서만 Boost 의존)
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace transport::ws
