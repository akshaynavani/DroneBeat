cmake_minimum_required(VERSION 3.20)
project(DroneBeat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DRONEBEAT_BUILD_TESTS "Build unit tests" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dependencies)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Protobuf / gRPC generated code
# ---------------------------------------------------------------------------
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/dronebeat.proto)
set(PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_OUT})

set(PROTO_SRCS ${PROTO_OUT}/dronebeat.pb.cc)
set(PROTO_HDRS ${PROTO_OUT}/dronebeat.pb.h)
set(GRPC_SRCS ${PROTO_OUT}/dronebeat.grpc.pb.cc)
set(GRPC_HDRS ${PROTO_OUT}/dronebeat.grpc.pb.h)

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND ${DRONEBEAT_PROTOC}
  ARGS --grpc_out=${PROTO_OUT}
       --cpp_out=${PROTO_OUT}
       -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
       --plugin=protoc-gen-grpc=${DRONEBEAT_GRPC_CPP_PLUGIN}
       ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
  COMMENT "Generating protobuf + gRPC sources for dronebeat.proto")

add_library(dronebeat_proto STATIC ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(dronebeat_proto PUBLIC ${PROTO_OUT})
target_link_libraries(dronebeat_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(DRONEBEAT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
