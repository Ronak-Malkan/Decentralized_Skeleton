cmake_minimum_required(VERSION 3.15)
project(mini3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Threading
find_package(Threads REQUIRED)

# gRPC & Protobuf via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC     REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

# nlohmann_json (header-only) — install with `brew install nlohmann-json`
find_package(nlohmann_json 3.2.0 REQUIRED)

# Include paths
include_directories(
  ${GRPC_INCLUDE_DIRS}
  ${PROTOBUF_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/generated     # generated stubs
  ${CMAKE_SOURCE_DIR}/include       # your headers
)

# Library search paths for pkg-config
link_directories(
  ${GRPC_LIBRARY_DIRS}
  ${PROTOBUF_LIBRARY_DIRS}
)

# Gather all generated .cc files
file(GLOB PROTO_SRCS
  ${CMAKE_SOURCE_DIR}/generated/*.cc
)

# Our server executable
add_executable(server
  src/main.cpp
  src/server.cpp
  ${PROTO_SRCS}
)

# Link libraries
target_link_libraries(server
  PRIVATE
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    Threads::Threads
    nlohmann_json::nlohmann_json   # ← brings in the JSON include path
)
