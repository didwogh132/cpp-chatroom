#include "net/net_platform.h"
#include <sstream>

namespace net {
bool init() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

int last_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

std::string last_error_string() {
    std::ostringstream oss;
    oss << "err=" << last_error();
    return oss.str();
}

void close_socket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

bool send_all(socket_t s, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while(sent < len) {
        #ifdef WIN32
            int n = ::send(s, reinterpret_cast<const char*>(data + sent),
                            static_cast<int>(len - sent), 0);
        #else
            ssize_t n = ::send(s, data + sent, len - sent, 0);
        #endif
            if(n <= 0) return false;
            sent += static_cast<size_t> (n);
    }
    return true;
}

bool recv_exact(socket_t s, uint8_t* data, size_t len) {
    size_t got = 0;
    while(got < len) {
        #ifdef WIN32
            int n = ::recv(s, reinterpret_cast<char*>(data + got),
                            static_cast<int>(len - got), 0);
        #else
            ssize_t n = ::recv(s, data + got, len - got, 0);
        #endif
            if(n <= 0) return false;
            got += static_cast<size_t>(n);
    }
    return true;
}

}