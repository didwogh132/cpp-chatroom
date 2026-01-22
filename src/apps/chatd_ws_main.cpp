#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "core/chat_core.h"
#include "transport/ws/ws_server.h"

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

int main(int argc, char** argv) {
  int port = 9001;
  if (argc >= 2) port = std::stoi(argv[1]);

  std::filesystem::create_directories("logs");
  std::string logpath = "logs/ws_chat_" + today_yyyymmdd() + ".txt";

  auto logger = [logpath](const std::string& line) {
    std::ofstream ofs(logpath, std::ios::app);
    if (!ofs) return;
    ofs << "[" << now_hhmmss() << "] " << line << "\n";
  };

  auto core = std::make_shared<core::ChatCore>(logger);
  transport::ws::WsServer server(core);

  if (!server.start(port)) {
    std::cerr << "failed to start ws server\n";
    return 1;
  }

  std::cout << "Press ENTER to stop...\n";
  std::string tmp;
  std::getline(std::cin, tmp);

  server.stop();
  return 0;
}
