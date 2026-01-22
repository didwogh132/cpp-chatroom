#pragma once
#include <unordered_map>
#include <mutex>
#include <string>
#include "core/connection.h"
#include "core/logger.h"

namespace core {

class ChatCore {
public:
  explicit ChatCore(LogFn logger = nullptr);

  void on_connect(const ConnPtr& c);
  void on_disconnect(const ConnPtr& c);
  void on_message(const ConnPtr& c, const nlohmann::json& j);

private:
  struct Client {
    ConnPtr conn;
    std::string nick = "guest";
    std::string room = "lobby";
    bool hello = false;
  };

  mutable std::mutex mx_;
  std::unordered_map<std::string, Client> clients_; // key = conn->id()
  LogFn log_;

  void log_line(const std::string& s);

  bool nick_taken_locked(const std::string& nick) const;
  std::string make_unique_nick_locked(const std::string& base) const;

  void send_error(const ConnPtr& c, const std::string& req_id,
                  const std::string& code, const std::string& text);

  void drop_dead_clients_locked(); // optional; can be no-op

  void send_system_to_room_locked(const std::string& room, const std::string& text);
  void broadcast_chat_to_room_locked(const std::string& room,
                                     const std::string& from,
                                     const std::string& text);
  void handle_who_locked(const ConnPtr& c, const std::string& req_id);
};

} // namespace core
