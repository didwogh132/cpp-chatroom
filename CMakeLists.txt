cmake_minimum_required(VERSION 3.16)
project(cpp_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chat_core
    src/net/net_platform.cpp
    src/common/framing.cpp
)

target_include_directories(chat_core PUBLIC src)
target_include_directories(chat_core PUBLIC src external)

if(WIN32)
    target_link_libraries(chat_core PUBLIC ws2_32)
endif()

add_executable(chat_server src/server/main.cpp)
target_link_libraries(chat_server PRIVATE chat_core)

add_executable(chat_client src/client/main.cpp)
target_link_libraries(chat_client PRIVATE chat_core)