#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "net/net_platform.h"

namespace framing {

    bool send_message(net::socket_t s, const std::string& msg);
    bool recv_message(net::socket_t s, std::string& out);
}