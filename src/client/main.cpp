#include <iostream>
#include <thread>
#include "net/net_platform.h"
#include "common/framing.h"

using net::socket_t;

static void recv_loop(socket_t s) {
    std::string msg;
    while(framing::recv_message(s, msg)) {
        std::cout << "\n" << msg << "\n> " << std::flush;
    }
    std::cout << "\n[disconnected]\n";
}

int main(int argc, char** argv) {
    if(argc < 4) {
        std::cerr << "Usage: chat_client <ip> <port> <nickname>\n";
        return 1;
    }
    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    std::string nick = argv[3];

    if(!net::init()) {
        std::cerr << "net init failed: " << net::last_error_string() << "\n";
        return 1;
    }

    socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if(s == net::INVALID_SOCKET_FD) {
        std::cerr << "socket() failed: " << net::last_error_string() << "\n";
        net::cleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    #ifdef _WIN32
        addr.sin_addr.S_un.S_addr = inet_addr(ip.c_str());
    #else
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    #endif

    if(::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "connect() failed: " << net::last_error_string() << "\n";
        net::close_socket(s);
        net::cleanup();
        return 1;
    }

    std::thread t(recv_loop, s);

    std::cout << "Connected. Type messages. /quit to exit.\n";
    std::string line;
    while(std::getline(std::cin, line)) {
        if(line == "/quit") break;
        std::string payload = "[" + nick + "]" + line;
        if(!framing::send_message(s, payload)) break;
        std::cout << "> ";
    }

    net::close_socket(s);
    t.join();
    net::cleanup();
    return 0;
}