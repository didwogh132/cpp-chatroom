#include <iostream>
#include <string>

#include "transport/gateway/ws_gateway.h"

int main(int argc, char** argv) {
  // usage: chat_gateway <ws_port> <tcp_host> <tcp_port>
  int ws_port = 9001;
  std::string tcp_host = "127.0.0.1";
  int tcp_port = 9000;

  if (argc >= 2) ws_port = std::stoi(argv[1]);
  if (argc >= 3) tcp_host = argv[2];
  if (argc >= 4) tcp_port = std::stoi(argv[3]);

  transport::gateway::WsGateway gw(tcp_host, tcp_port);
  if (!gw.start(ws_port)) {
    std::cerr << "failed to start gateway\n";
    return 1;
  }

  std::cout << "Press ENTER to stop...\n";
  std::string tmp;
  std::getline(std::cin, tmp);

  gw.stop();
  return 0;
}
