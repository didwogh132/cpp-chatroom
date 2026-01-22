#include <iostream>
#include <thread>
#include <string>
#include "net/net_platform.h"
#include "common/json_io.h"

using net::socket_t;
using jsonio::json;

static std::string prompt_line(const char* label, const std::string& def = "") {
  std::cout << label;
  if (!def.empty()) std::cout << " (default: " << def << ")";
  std::cout << ": " << std::flush;

  std::string line;
  std::getline(std::cin, line);
  if (line.empty()) return def;
  return line;
}

static int prompt_int(const char* label, int def) {
  while (true) {
    std::string s = prompt_line(label, std::to_string(def));
    try {
      int v = std::stoi(s);
      if (v > 0 && v <= 65535) return v;
    } catch (...) {}
    std::cout << "Invalid number. Try again.\n";
  }
}

static void print_incoming(const json& j) {
  if (!j.contains("type") || !j["type"].is_string()) return;
  std::string type = j["type"].get<std::string>();

  if (type == "chat") {
    std::string room = j.value("room", "");
    std::string from = j.value("from", "");
    std::string text = j.value("text", "");
    if (!room.empty()) std::cout << "[" << room << "] ";
    std::cout << from << ": " << text << "\n";
    return;
  }
  if (type == "system") {
    std::cout << "* " << j.value("text", "") << "\n";
    return;
  }
  if (type == "error") {
    std::cout << "! error: " << j.value("text", "") << "\n";
    return;
  }
  if (type == "who") {
    std::cout << "* users in [" << j.value("room", "") << "]: ";
    if (j.contains("users") && j["users"].is_array()) {
      bool first = true;
      for (auto& u : j["users"]) {
        if (!u.is_string()) continue;
        if (!first) std::cout << ", ";
        std::cout << u.get<std::string>();
        first = false;
      }
    }
    std::cout << "\n";
    return;
  }

  // 기타 타입은 raw 출력
  std::cout << j.dump() << "\n";
}

static void recv_loop(socket_t s) {
  json j;
  while (jsonio::recv_json(s, j)) {
    std::cout << "\n";
    print_incoming(j);
    std::cout << "> " << std::flush;
  }
  std::cout << "\n[disconnected]\n";
}

int main() {
  std::string ip = prompt_line("Server IP", "127.0.0.1");
  int port = prompt_int("Port", 9000);
  std::string nick = prompt_line("Nickname");
  if (nick.empty()) {
    std::cout << "Nickname is required.\n";
    return 1;
  }

  if (!net::init()) {
    std::cerr << "net init failed: " << net::last_error_string() << "\n";
    return 1;
  }

  socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s == net::INVALID_SOCKET_FD) {
    std::cerr << "socket() failed: " << net::last_error_string() << "\n";
    net::cleanup();
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
#ifdef _WIN32
  addr.sin_addr.S_un.S_addr = inet_addr(ip.c_str());
#else
  inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
#endif

  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "connect() failed: " << net::last_error_string() << "\n";
    net::close_socket(s);
    net::cleanup();
    return 1;
  }

  // hello 전송
  jsonio::send_json(s, json{{"type","hello"},{"nick",nick}});

  std::thread t(recv_loop, s);

  std::cout << "Connected.\n"
            << "Commands: /who, /join <room>, /nick <new>, /quit\n> ";

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "/quit") break;

    if (!line.empty() && line[0] == '/') {
      // 명령 파싱
      if (line == "/who") {
        jsonio::send_json(s, json{{"type","who"}});
      } else if (line.rfind("/join ", 0) == 0) {
        std::string room = line.substr(6);
        jsonio::send_json(s, json{{"type","join"},{"room",room}});
      } else if (line.rfind("/nick ", 0) == 0) {
        std::string nn = line.substr(6);
        jsonio::send_json(s, json{{"type","nick"},{"nick",nn}});
      } else {
        std::cout << "! unknown command\n> ";
      }
      continue;
    }

    // 일반 채팅
    if (!line.empty()) {
      jsonio::send_json(s, json{{"type","chat"},{"text",line}});
    }
    std::cout << "> ";
  }

  net::close_socket(s);
  t.join();
  net::cleanup();
  return 0;
}