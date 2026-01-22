cmake_minimum_required(VERSION 3.16)
project(cpp_chatroom LANGUAGES CXX)

# -----------------------------
# 0) 기본 설정
# -----------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 사용자가 "선택 빌드"할 수 있게 옵션 제공
option(CHAT_ENABLE_TCP "Build TCP server/client" ON)
option(CHAT_ENABLE_WS "Build WebSocket server" OFF)
option(CHAT_ENABLE_GATEWAY "Build WS<->TCP gateway" OFF)

# -----------------------------
# 1) 공통 include 경로
# -----------------------------
# - src 아래 헤더를 <...>로 include 하도록
set(PROJECT_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
)

# -----------------------------
# 2) 공통 라이브러리: chat_common
#    - net_platform + framing (길이 프레이밍)
# -----------------------------
add_library(chat_common
  src/net/net_platform.cpp
  src/common/framing.cpp
)

target_include_directories(chat_common PUBLIC
  ${PROJECT_INCLUDE_DIRS}
)

# (선택) MSVC에서 소스 인코딩 경고 줄이고 싶으면 주석 해제
# target_compile_options(chat_common PRIVATE /utf-8)

# -----------------------------
# 3) 코어 라이브러리: chat_core
#    - 채팅 도메인 로직(닉/방/명령 처리)
# -----------------------------
add_library(chat_core
  src/core/chat_core.cpp
)

target_include_directories(chat_core PUBLIC
  ${PROJECT_INCLUDE_DIRS}
)

target_link_libraries(chat_core PUBLIC
  chat_common
)

# -----------------------------
# 4) TCP Transport + 실행파일들
# -----------------------------
if(CHAT_ENABLE_TCP)
  add_library(chat_transport_tcp
    src/transport/tcp/tcp_server.cpp
  )

  target_include_directories(chat_transport_tcp PUBLIC
    ${PROJECT_INCLUDE_DIRS}
  )

  target_link_libraries(chat_transport_tcp PUBLIC
    chat_core
    chat_common
  )

  # TCP 서버 실행파일
  add_executable(chatd_tcp
    src/apps/chatd_tcp_main.cpp
  )
  target_link_libraries(chatd_tcp PRIVATE
    chat_transport_tcp
  )

  # 콘솔 클라이언트 실행파일
  add_executable(chat_client
    src/apps/chat_client_main.cpp
  )
  target_link_libraries(chat_client PRIVATE
    chat_common
  )
endif()

# -----------------------------
# 5) WS / Gateway는 Boost 필요
#    - Beast(WebSocket)는 헤더 위주지만 Boost::system 링크가 필요
# -----------------------------
if(CHAT_ENABLE_WS OR CHAT_ENABLE_GATEWAY)
  # Boost 설치되어 있어야 함 (vcpkg 등)
  # Windows/MSVC에서 보통 system만 있으면 됨
  find_package(Boost REQUIRED COMPONENTS system)
endif()

# -----------------------------
# 6) WS Transport + 실행파일
# -----------------------------
if(CHAT_ENABLE_WS)
  add_library(chat_transport_ws
    src/transport/ws/ws_server.cpp
  )

  target_include_directories(chat_transport_ws PUBLIC
    ${PROJECT_INCLUDE_DIRS}
  )

  target_link_libraries(chat_transport_ws PUBLIC
    chat_core
    chat_common
    Boost::system
  )

  add_executable(chatd_ws
    src/apps/chatd_ws_main.cpp
  )
  target_link_libraries(chatd_ws PRIVATE
    chat_transport_ws
  )
endif()

# -----------------------------
# 7) Gateway Transport + 실행파일
# -----------------------------
if(CHAT_ENABLE_GATEWAY)
  add_library(chat_transport_gateway
    src/transport/gateway/ws_gateway.cpp
  )

  target_include_directories(chat_transport_gateway PUBLIC
    ${PROJECT_INCLUDE_DIRS}
  )

  target_link_libraries(chat_transport_gateway PUBLIC
    chat_common
    Boost::system
  )

  add_executable(chat_gateway
    src/apps/chat_gateway_main.cpp
  )
  target_link_libraries(chat_gateway PRIVATE
    chat_transport_gateway
  )
endif()
