#include "common/framing.h"
#include <cstring>
#include <vector>

namespace framing {

static uint32_t to_be32(uint32_t x) {
  return htonl(x);
}

static uint32_t from_be32(uint32_t x) {
  return ntohl(x);
}

bool send_message(net::socket_t s, const std::string& msg) {
  if (msg.size() > 10 * 1024 * 1024) return false;
  uint32_t len = static_cast<uint32_t>(msg.size());
  uint32_t be_len = to_be32(len);

  std::vector<uint8_t> buf(sizeof(uint32_t) + msg.size());
  std::memcpy(buf.data(), &be_len, sizeof(uint32_t));
  if (!msg.empty()) {
    std::memcpy(buf.data() + sizeof(uint32_t), msg.data(), msg.size());
  }
  return net::send_all(s, buf.data(), buf.size());
}

bool recv_message(net::socket_t s, std::string& out) {
  uint32_t be_len = 0;
  if (!net::recv_exact(s, reinterpret_cast<uint8_t*>(&be_len), sizeof(uint32_t)))
    return false;

  uint32_t len = from_be32(be_len);
  if (len > 10 * 1024 * 1024) return false;

  std::vector<uint8_t> payload(len);
  if (len > 0) {
    if (!net::recv_exact(s, payload.data(), payload.size())) return false;
  }
  out.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
  return true;
}

}