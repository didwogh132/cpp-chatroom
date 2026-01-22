#pragma once
#include <string>
#include "net/net_platform.h"
#include "common/framing.h"
#include "nlohmann/json.hpp"

namespace jsonio {
using json = nlohmann::json;

// JSON 객체를 framing에 실어 전송
inline bool send_json(net::socket_t s, const json& j) {
  return framing::send_message(s, j.dump());
}

// framing으로 받은 문자열을 JSON으로 파싱
inline bool recv_json(net::socket_t s, json& out) {
  std::string payload;
  if (!framing::recv_message(s, payload)) return false;

  try {
    out = json::parse(payload);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace jsonio
