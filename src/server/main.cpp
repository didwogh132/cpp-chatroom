#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>

#include "net/net_platform.h"
#include "common/json_io.h"

using net::socket_t;
using jsonio::json;

struct Client {
  socket_t sock;
  std::string nick = "guest";
  std::string room = "lobby";
  bool hello_done = false;
};

static std::mutex g_mx;
static std::unordered_map<socket_t, Client> g_clients;

static bool nick_taken_locked(const std::string& nick) {
  for (auto& [_, c] : g_clients) {
    if (c.nick == nick) return true;
  }
  return false;
}

static void send_error(socket_t s, const std::string& text) {
  json j = {{"type","error"}, {"text", text}};
  jsonio::send_json(s, j);
}

static void send_system_to_room(const std::string& room, const std::string& text) {
  std::lock_guard<std::mutex> lk(g_mx);
  for (auto& [_, c] : g_clients) {
    if (c.room != room) continue;
    jsonio::send_json(c.sock, json{{"type","system"}, {"text", text}});
  }
}

static void broadcast_chat_to_room(const std::string& room,
                                  const std::string& from,
                                  const std::string& text,
                                  socket_t except = net::INVALID_SOCKET_FD) {
  std::lock_guard<std::mutex> lk(g_mx);
  json msg = {{"type","chat"}, {"room", room}, {"from", from}, {"text", text}};
  for (auto& [_, c] : g_clients) {
    if (c.sock == except) continue;
    if (c.room != room) continue;
    jsonio::send_json(c.sock, msg);
  }
}

static void remove_client(socket_t s) {
  std::lock_guard<std::mutex> lk(g_mx);
  g_clients.erase(s);
}

static void handle_who(socket_t s) {
  std::lock_guard<std::mutex> lk(g_mx);
  auto it = g_clients.find(s);
  if (it == g_clients.end()) return;

  const std::string room = it->second.room;
  std::vector<std::string> users;
  for (auto& [_, c] : g_clients) {
    if (c.room == room) users.push_back(c.nick);
  }
  json resp = {{"type","who"}, {"room", room}, {"users", users}};
  jsonio::send_json(s, resp);
}

static void client_thread(socket_t s) {
  // 기본 등록
  {
    std::lock_guard<std::mutex> lk(g_mx);
    Client c;
    c.sock = s;
    c.nick = "guest";
    c.room = "lobby";
    c.hello_done = false;
    g_clients[s] = c;
  }

  json msg;
  while (jsonio::recv_json(s, msg)) {
    if (!msg.contains("type") || !msg["type"].is_string()) {
      send_error(s, "invalid message: missing type");
      continue;
    }

    std::string type = msg["type"].get<std::string>();

    // 클라가 hello를 안 했으면, hello만 허용(서비스처럼)
    {
      std::lock_guard<std::mutex> lk(g_mx);
      auto it = g_clients.find(s);
      if (it == g_clients.end()) break;
      if (!it->second.hello_done && type != "hello") {
        send_error(s, "please send hello first");
        continue;
      }
    }

    if (type == "hello") {
      if (!msg.contains("nick") || !msg["nick"].is_string()) {
        send_error(s, "hello requires nick");
        continue;
      }
      std::string nick = msg["nick"].get<std::string>();
      if (nick.empty() || nick.size() > 20) {
        send_error(s, "invalid nick length");
        continue;
      }

      std::string old_room;
      std::string old_nick;
      {
        std::lock_guard<std::mutex> lk(g_mx);
        auto& c = g_clients[s];
        old_room = c.room;
        old_nick = c.nick;

        if (nick_taken_locked(nick)) {
          send_error(s, "nickname already in use");
          continue;
        }
        c.nick = nick;
        c.hello_done = true;
      }

      jsonio::send_json(s, json{{"type","system"},{"text","hello ok"}});

      send_system_to_room(old_room, nick + " joined " + old_room);
      continue;
    }

    if (type == "chat") {
      if (!msg.contains("text") || !msg["text"].is_string()) {
        send_error(s, "chat requires text");
        continue;
      }
      std::string text = msg["text"].get<std::string>();
      if (text.empty()) continue;

      std::string room, from;
      {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_clients.find(s);
        if (it == g_clients.end()) break;
        room = it->second.room;
        from = it->second.nick;
      }
      broadcast_chat_to_room(room, from, text);
      continue;
    }

    if (type == "join") {
      if (!msg.contains("room") || !msg["room"].is_string()) {
        send_error(s, "join requires room");
        continue;
      }
      std::string new_room = msg["room"].get<std::string>();
      if (new_room.empty() || new_room.size() > 30) {
        send_error(s, "invalid room");
        continue;
      }

      std::string old_room, nick;
      {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_clients.find(s);
        if (it == g_clients.end()) break;
        old_room = it->second.room;
        it->second.room = new_room;
        nick = it->second.nick;
      }

      send_system_to_room(old_room, nick + " left " + old_room);
      send_system_to_room(new_room, nick + " joined " + new_room);
      continue;
    }

    if (type == "nick") {
      if (!msg.contains("nick") || !msg["nick"].is_string()) {
        send_error(s, "nick requires nick");
        continue;
      }
      std::string newnick = msg["nick"].get<std::string>();
      if (newnick.empty() || newnick.size() > 20) {
        send_error(s, "invalid nick length");
        continue;
      }

      std::string room, oldnick;
      {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_clients.find(s);
        if (it == g_clients.end()) break;
        room = it->second.room;
        oldnick = it->second.nick;

        if (nick_taken_locked(newnick)) {
          send_error(s, "nickname already in use");
          continue;
        }
        it->second.nick = newnick;
      }

      send_system_to_room(room, oldnick + " is now " + newnick);
      continue;
    }

    if (type == "who") {
      handle_who(s);
      continue;
    }

    send_error(s, "unknown type: " + type);
  }

  // 연결 종료 처리
  std::string nick, room;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_clients.find(s);
    if (it != g_clients.end()) {
      nick = it->second.nick;
      room = it->second.room;
    }
  }

  remove_client(s);
  net::close_socket(s);

  if (!nick.empty()) {
    send_system_to_room(room, nick + " disconnected");
  }
}
int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: chat_server <port>\n";
    return 1;
  }
  int port = std::stoi(argv[1]);

  if (!net::init()) {
    std::cerr << "net init failed: " << net::last_error_string() << "\n";
    return 1;
  }

  socket_t srv = ::socket(AF_INET, SOCK_STREAM, 0);
  if (srv == net::INVALID_SOCKET_FD) {
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
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "bind() failed: " << net::last_error_string() << "\n";
    net::close_socket(srv);
    net::cleanup();
    return 1;
  }

  if (::listen(srv, 16) != 0) {
    std::cerr << "listen() failed: " << net::last_error_string() << "\n";
    net::close_socket(srv);
    net::cleanup();
    return 1;
  }

  std::cout << "Server listening on port " << port << "\n";

  while (true) {
    sockaddr_in caddr{};
#ifdef _WIN32
    int clen = sizeof(caddr);
#else
    socklen_t clen = sizeof(caddr);
#endif
    socket_t c = ::accept(srv, reinterpret_cast<sockaddr*>(&caddr), &clen);
    if (c == net::INVALID_SOCKET_FD) continue;

    std::thread(client_thread, c).detach();
  }
}