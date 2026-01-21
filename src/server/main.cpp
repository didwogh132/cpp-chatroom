#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include "net/net_platform.h"
#include "common/framing.h"

using net::socket_t;

static std::mutex g_mx;
static std::vector<socket_t> g_clients;

static void broadcast(const std::string& msg, socket_t except = net::INVALID_SOCKET_FD) {
    std::lock_guard<std::mutex> lk(g_mx);
    for(auto c : g_clients) {
        if(c == except) continue;
        framing::send_message(c, msg);
    }
}

static void remove_client(socket_t c) {
    std::lock_guard<std::mutex> lk(g_mx);
    g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), c), g_clients.end());
}

static void client_thread(socket_t c) {
    std::string msg;
    while(framing::recv_message(c, msg)) {
        broadcast(msg, c);
    }
    remove_client(c);
    net::close_socket(c);
}

int main(int argc, char** argv) {
    if(argc < 2) {
        std::cerr << "Usage: chat_server <port>\n";
        return 1;
    }
    int port = std::stoi(argv[1]);

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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    int opt = 1;
    #ifdef _WIN32
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    #else
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #endif

    if(::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind() failed: " << net::last_error_string() << "\n";
        net::close_socket(s);
        net::cleanup();
        return 1;
    }

    if(::listen(s, 16) != 0) {
        std::cerr << "listen() failed: " << net::last_error_string() << "\n";
        net::close_socket(s);
        net::cleanup();
        return 1;
    }

    std::cout << "Chat server listening on port " << port << "\n";
    
    while(true) {
        sockaddr_in caddr{};
        #ifdef _WIN32
            int clen = sizeof(caddr);
        #else
            socklen_t clen = sizeof(caddr);
        #endif
        
        socket_t c = ::accept(s, reinterpret_cast<sockaddr*>(&caddr), &clen);
        if(c == net::INVALID_SOCKET_FD) continue;
        {
            std::lock_guard<std::mutex> lk(g_mx);
            g_clients.push_back(c);
        }
        std::thread(client_thread, c).detach();
        
    }
}