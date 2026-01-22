#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace core::proto {

inline int version(const nlohmann::json& j) {
  if (j.contains("v") && j["v"].is_number_integer()) return j["v"].get<int>();
  return 1;
}

inline std::string type(const nlohmann::json& j) {
  if (j.contains("type") && j["type"].is_string()) return j["type"].get<std::string>();
  return "";
}

inline std::string req_id(const nlohmann::json& j) {
  if (j.contains("req_id") && j["req_id"].is_string()) return j["req_id"].get<std::string>();
  return "";
}

inline nlohmann::json make_error(const std::string& req_id,
                                 const std::string& code,
                                 const std::string& text) {
  nlohmann::json e = {{"v",1},{"type","error"},{"code",code},{"text",text}};
  if (!req_id.empty()) e["req_id"] = req_id;
  return e;
}

inline nlohmann::json make_system(const std::string& text) {
  return {{"v",1},{"type","system"},{"text",text}};
}

inline nlohmann::json make_chat(const std::string& room,
                                const std::string& from,
                                const std::string& text) {
  return {{"v",1},{"type","chat"},{"room",room},{"from",from},{"text",text}};
}

inline nlohmann::json make_who_ok(const std::string& req_id,
                                  const std::string& room,
                                  const nlohmann::json& users) {
  nlohmann::json r = {{"v",1},{"type","who_ok"},{"room",room},{"users",users}};
  if (!req_id.empty()) r["req_id"] = req_id;
  return r;
}

inline nlohmann::json make_hello_ok(const std::string& req_id,
                                    const std::string& nick,
                                    const std::string& room) {
  nlohmann::json r = {{"v",1},{"type","hello_ok"},{"nick",nick},{"room",room}};
  if (!req_id.empty()) r["req_id"] = req_id;
  return r;
}

} // namespace core::proto
