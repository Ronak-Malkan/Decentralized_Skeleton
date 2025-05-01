cmake_minimum_required(VERSION 3.15)
project(mini3 LANGUAGES C CXX)

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Threads for std::thread
find_package(Threads REQUIRED)

# gRPC & Protobuf via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC    REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

# nlohmann_json (header-only)
# Install on macOS with: brew install nlohmann-json
find_package(nlohmann_json 3.2.0 REQUIRED)

# Include directories
include_directories(
  ${GRPC_INCLUDE_DIRS}
  ${PROTOBUF_INCLUDE_DIRS}
  ${nlohmann_json_INCLUDE_DIRS}

  ${CMAKE_SOURCE_DIR}/generated   # generated .pb.cc/.grpc.pb.cc
  ${CMAKE_SOURCE_DIR}/include     # your .h files
  ${CMAKE_SOURCE_DIR}/src         # for client.h if you keep it in src/
)

# Link directories for pkg-config libraries
link_directories(
  ${GRPC_LIBRARY_DIRS}
  ${PROTOBUF_LIBRARY_DIRS}
)

# Collect the generated protobuf sources
file(GLOB PROTO_SRCS
  ${CMAKE_SOURCE_DIR}/generated/*.cc
)

#
# Server
#
add_executable(server
  src/main.cpp
  src/server.cpp
  ${PROTO_SRCS}
)

target_link_libraries(server
  PRIVATE
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    Threads::Threads
    nlohmann_json::nlohmann_json
)

#
# Client
#
add_executable(client
  src/client.cpp
  src/client.h
  ${PROTO_SRCS}
)

target_link_libraries(client
  PRIVATE
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    Threads::Threads
    nlohmann_json::nlohmann_json
)
