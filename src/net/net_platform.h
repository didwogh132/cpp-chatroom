#pragma once
#include <cstdint>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
#endif

namespace net {

    #ifdef _WIN32
        using socket_t = SOCKET;
        constexpr socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
    #else
        using socket_t = int;
        constexpr socket_t INVALID_SOCKET_FD = -1;
    #endif
    bool init();
    void cleanup();

    int last_error();
    std::string last_error_string();

    void close_socket(socket_t sock);

    // 전송/수신 유틸
    bool send_all(socket_t sock, const uint8_t* data, size_t len);
    bool recv_exact(socket_t s, uint8_t* data, size_t len);
}