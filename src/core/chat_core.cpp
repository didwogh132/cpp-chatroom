#include "core/chat_core.h"
#include "core/protocol.h"
#include <vector>

using nlohmann::json;

namespace core {

ChatCore::ChatCore(LogFn logger) : log_(std::move(logger)) {}

void ChatCore::log_line(const std::string& s) {
  if (log_) log_(s);
}

bool ChatCore::nick_taken_locked(const std::string& nick) const {
  for (const auto& [_, c] : clients_) {
    if (c.nick == nick) return true;
  }
  return false;
}

std::string ChatCore::make_unique_nick_locked(const std::string& base) const {
  if (!nick_taken_locked(base)) return base;
  for (int i = 2; i <= 9999; i++) {
    std::string cand = base + "_" + std::to_string(i);
    if (!nick_taken_locked(cand)) return cand;
  }
  return base + "_" + std::to_string(std::rand() % 100000);
}

void ChatCore::send_error(const ConnPtr& c, const std::string& req_id,
                          const std::string& code, const std::string& text) {
  if (!c) return;
  (void)c->send(proto::make_error(req_id, code, text));
}

void ChatCore::on_connect(const ConnPtr& c) {
  if (!c) return;
  std::lock_guard<std::mutex> lk(mx_);

  Client cl;
  cl.conn = c;
  cl.nick = "guest";
  cl.room = "lobby";
  cl.hello = false;
  clients_[c->id()] = cl;

  log_line("[connect] " + c->id());
}

void ChatCore::on_disconnect(const ConnPtr& c) {
  if (!c) return;

  std::string nick;
  std::string room;

  {
    std::lock_guard<std::mutex> lk(mx_);
    auto it = clients_.find(c->id());
    if (it != clients_.end()) {
      nick = it->second.nick;
      room = it->second.room;
      clients_.erase(it);
    }
  }

  if (!nick.empty()) {
    std::lock_guard<std::mutex> lk(mx_);
    send_system_to_room_locked(room, nick + " disconnected");
  }

  log_line("[disconnect] " + c->id());
}

void ChatCore::send_system_to_room_locked(const std::string& room, const std::string& text) {
  std::vector<std::string> dead;
  for (auto& [id, cl] : clients_) {
    if (cl.room != room) continue;
    if (!cl.conn || !cl.conn->send(proto::make_system(text))) {
      dead.push_back(id);
    }
  }
  for (auto& id : dead) {
    auto it = clients_.find(id);
    if (it != clients_.end()) {
      if (it->second.conn) it->second.conn->close();
      clients_.erase(it);
    }
  }
  log_line("[system][" + room + "] " + text);
}

void ChatCore::broadcast_chat_to_room_locked(const std::string& room,
                                             const std::string& from,
                                             const std::string& text) {
  json msg = proto::make_chat(room, from, text);

  std::vector<std::string> dead;
  for (auto& [id, cl] : clients_) {
    if (cl.room != room) continue;
    if (!cl.conn || !cl.conn->send(msg)) {
      dead.push_back(id);
    }
  }
  for (auto& id : dead) {
    auto it = clients_.find(id);
    if (it != clients_.end()) {
      if (it->second.conn) it->second.conn->close();
      clients_.erase(it);
    }
  }
  log_line("[chat][" + room + "][" + from + "] " + text);
}

void ChatCore::handle_who_locked(const ConnPtr& c, const std::string& req_id) {
  if (!c) return;
  auto it = clients_.find(c->id());
  if (it == clients_.end()) return;

  const std::string room = it->second.room;
  json users = json::array();
  for (auto& [_, cl] : clients_) {
    if (cl.room == room) users.push_back(cl.nick);
  }

  if (!c->send(proto::make_who_ok(req_id, room, users))) {
    // 연결이 죽었으면 제거
    auto it2 = clients_.find(c->id());
    if (it2 != clients_.end()) {
      if (it2->second.conn) it2->second.conn->close();
      clients_.erase(it2);
    }
  }
}

void ChatCore::on_message(const ConnPtr& c, const json& j) {
  if (!c) return;

  const std::string t = proto::type(j);
  const std::string rid = proto::req_id(j);

  std::lock_guard<std::mutex> lk(mx_);
  auto it = clients_.find(c->id());
  if (it == clients_.end()) return;

  Client& me = it->second;

  if (t.empty()) {
    send_error(c, rid, "BAD_REQ", "missing type");
    return;
  }

  // hello before anything
  if (!me.hello && t != "hello") {
    send_error(c, rid, "BAD_STATE", "send hello first");
    return;
  }

  if (t == "hello") {
    if (!j.contains("nick") || !j["nick"].is_string()) {
      send_error(c, rid, "BAD_REQ", "hello requires nick");
      return;
    }
    std::string requested = j["nick"].get<std::string>();
    if (requested.empty() || requested.size() > 20) {
      send_error(c, rid, "BAD_REQ", "invalid nick");
      return;
    }
    std::string assigned = make_unique_nick_locked(requested);
    me.nick = assigned;
    me.hello = true;

    (void)c->send(proto::make_hello_ok(rid, assigned, me.room));
    send_system_to_room_locked(me.room, assigned + " joined " + me.room);
    return;
  }

  if (t == "chat") {
    if (!j.contains("text") || !j["text"].is_string()) {
      send_error(c, rid, "BAD_REQ", "chat requires text");
      return;
    }
    std::string text = j["text"].get<std::string>();
    if (text.empty()) return;
    broadcast_chat_to_room_locked(me.room, me.nick, text);
    return;
  }

  if (t == "join") {
    if (!j.contains("room") || !j["room"].is_string()) {
      send_error(c, rid, "BAD_REQ", "join requires room");
      return;
    }
    std::string new_room = j["room"].get<std::string>();
    if (new_room.empty() || new_room.size() > 30) {
      send_error(c, rid, "BAD_REQ", "invalid room");
      return;
    }

    std::string old = me.room;
    me.room = new_room;
    send_system_to_room_locked(old, me.nick + " left " + old);
    send_system_to_room_locked(new_room, me.nick + " joined " + new_room);
    return;
  }

  if (t == "nick") {
    if (!j.contains("nick") || !j["nick"].is_string()) {
      send_error(c, rid, "BAD_REQ", "nick requires nick");
      return;
    }
    std::string requested = j["nick"].get<std::string>();
    if (requested.empty() || requested.size() > 20) {
      send_error(c, rid, "BAD_REQ", "invalid nick");
      return;
    }
    std::string old = me.nick;
    std::string nn = make_unique_nick_locked(requested);
    me.nick = nn;
    send_system_to_room_locked(me.room, old + " is now " + nn);
    return;
  }

  if (t == "who") {
    handle_who_locked(c, rid);
    return;
  }

  send_error(c, rid, "BAD_REQ", "unknown type: " + t);
}

} // namespace core
