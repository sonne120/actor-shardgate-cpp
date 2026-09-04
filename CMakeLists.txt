cmake_minimum_required(VERSION 3.20)
project(shardgate_cpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)

add_executable(shardgate src/main.cpp)
target_link_libraries(shardgate PRIVATE Threads::Threads)

add_executable(keyhash tools/keyhash.cpp)

add_executable(packet_client src/client.cpp)
target_include_directories(packet_client PRIVATE include)
target_link_libraries(packet_client PRIVATE Threads::Threads)
