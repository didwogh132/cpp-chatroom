#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>

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

// 로그용 뮤텍스(별도)
static std::mutex g_log_mx;

static std::string today_yyyymmdd() {
  using namespace std::chrono;
  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  tm = *std::localtime(&t);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%d");
  return oss.str();
}

static std::string now_hhmmss() {
  using namespace std::chrono;
  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  tm = *std::localtime(&t);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%H:%M:%S");
  return oss.str();
}

static void append_log(const std::string& line) {
  std::lock_guard<std::mutex> lk(g_log_mx);
  try {
    std::filesystem::create_directories("logs");
  } catch (...) {
    // ignore
  }
  std::string filename = "logs/chat_" + today_yyyymmdd() + ".txt";
  std::ofstream ofs(filename, std::ios::app);
  if (!ofs) return;
  ofs << "[" << now_hhmmss() << "] " << line << "\n";
}

static bool nick_taken_locked(const std::string& nick) {
  for (auto& [_, c] : g_clients) {
    if (c.nick == nick) return true;
  }
  return false;
}

// (1) 닉 중복이면 자동 suffix 붙여서 유니크하게 만들기
static std::string make_unique_nick_locked(const std::string& base) {
  if (!nick_taken_locked(base)) return base;
  for (int i = 2; i <= 9999; i++) {
    std::string cand = base + "_" + std::to_string(i);
    if (!nick_taken_locked(cand)) return cand;
  }
  // 여기까지 오면 거의 비정상(안전 fallback)
  return base + "_" + std::to_string(std::rand() % 100000);
}

static void send_error(socket_t s, const std::string& text) {
  json j = {{"type","error"}, {"text", text}};
  jsonio::send_json(s, j);
}

static bool send_json_or_fail(socket_t s, const json& j) {
  return jsonio::send_json(s, j);
}

// (3) 전송 실패(끊긴 클라) 자동 제거 유틸
static void drop_client_locked(socket_t s) {
  auto it = g_clients.find(s);
  if (it == g_clients.end()) return;
  net::close_socket(it->second.sock);
  g_clients.erase(it);
}

static void send_system_to_room(const std::string& room, const std::string& text) {
  std::vector<socket_t> to_drop;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    for (auto& [_, c] : g_clients) {
      if (c.room != room) continue;
      json j = {{"type","system"}, {"text", text}};
      if (!send_json_or_fail(c.sock, j)) {
        to_drop.push_back(c.sock);
      }
    }
    for (auto s : to_drop) drop_client_locked(s);
  }
  append_log("[system][" + room + "] " + text);
}

static void broadcast_chat_to_room(const std::string& room,
                                  const std::string& from,
                                  const std::string& text) {
  std::vector<socket_t> to_drop;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    json msg = {{"type","chat"}, {"room", room}, {"from", from}, {"text", text}};
    for (auto& [_, c] : g_clients) {
      if (c.room != room) continue;
      if (!send_json_or_fail(c.sock, msg)) {
        to_drop.push_back(c.sock);
      }
    }
    for (auto s : to_drop) drop_client_locked(s);
  }
  append_log("[chat][" + room + "][" + from + "] " + text);
}

static void handle_who(socket_t s) {
  std::vector<std::string> users;
  std::string room;

  {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_clients.find(s);
    if (it == g_clients.end()) return;
    room = it->second.room;

    for (auto& [_, c] : g_clients) {
      if (c.room == room) users.push_back(c.nick);
    }
  }

  json resp = {{"type","who"}, {"room", room}, {"users", users}};
  if (!send_json_or_fail(s, resp)) {
    std::lock_guard<std::mutex> lk(g_mx);
    drop_client_locked(s);
  }
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

    // hello 전에는 hello만 허용
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
      std::string requested = msg["nick"].get<std::string>();
      if (requested.empty() || requested.size() > 20) {
        send_error(s, "invalid nick length");
        continue;
      }

      std::string assigned;
      std::string room;

      {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_clients.find(s);
        if (it == g_clients.end()) break;

        assigned = make_unique_nick_locked(requested);
        it->second.nick = assigned;
        it->second.hello_done = true;
        room = it->second.room;
      }

      // hello 응답(클라가 실제 닉네임 확인 가능)
      json ack = {{"type","hello"}, {"nick", assigned}, {"room", room}};
      if (!send_json_or_fail(s, ack)) {
        std::lock_guard<std::mutex> lk(g_mx);
        drop_client_locked(s);
        break;
      }

      send_system_to_room(room, assigned + " joined " + room);
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
      std::string requested = msg["nick"].get<std::string>();
      if (requested.empty() || requested.size() > 20) {
        send_error(s, "invalid nick length");
        continue;
      }

      std::string oldnick, newnick, room;
      {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_clients.find(s);
        if (it == g_clients.end()) break;
        room = it->second.room;
        oldnick = it->second.nick;

        newnick = make_unique_nick_locked(requested);
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

  // 연결 종료 처리(정리 + 시스템 메시지)
  std::string nick, room;
  {
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_clients.find(s);
    if (it != g_clients.end()) {
      nick = it->second.nick;
      room = it->second.room;
      g_clients.erase(it);
    }
  }

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
  append_log("[server] started on port " + std::to_string(port));

  while (true) {
    sockaddr_in caddr{};
#ifdef _WIN32
    int clen = sizeof(caddr);
#else
    socklen_t clen = sizeof(caddr);
#endif
    socket_t c = ::accept(srv, reinterpret_cast<sockaddr*>(&caddr), &clen);
    if (c == net::INVALID_SOCKET_FD) continue;

    append_log("[server] client accepted");
    std::thread(client_thread, c).detach();
  }
}
