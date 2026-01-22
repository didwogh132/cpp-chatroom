#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <string>

namespace transport::gateway {

class WsGateway {
public:
  WsGateway(std::string tcp_host, int tcp_port);
  ~WsGateway();

  bool start(int ws_port);
  void stop();

private:
  std::string tcp_host_;
  int tcp_port_;

  std::atomic<bool> running_{false};

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace transport::gateway
