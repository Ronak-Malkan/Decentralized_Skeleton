cmake_minimum_required(VERSION 3.15)
project(mini3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find threading (for OpenMP or just std::thread)
find_package(Threads REQUIRED)

# Use pkg-config to locate gRPC and Protobuf
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC    REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

include_directories(
  ${GRPC_INCLUDE_DIRS}
  ${PROTOBUF_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/generated     # our generated code
  ${CMAKE_SOURCE_DIR}/include       # your headers
)

link_directories(
  ${GRPC_LIBRARY_DIRS}
  ${PROTOBUF_LIBRARY_DIRS}
)

# Grab all the generated sources
file(GLOB PROTO_SRCS
  ${CMAKE_SOURCE_DIR}/generated/*.cc
)

# Your server binary
add_executable(server
  src/main.cpp
  ${PROTO_SRCS}
)

target_link_libraries(server
  ${GRPC_LIBRARIES}
  ${PROTOBUF_LIBRARIES}
  Threads::Threads
)
