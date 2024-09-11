# System-provided dependencies ------------------------------------------------
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()
find_package(gRPC CONFIG REQUIRED)

if(TARGET protobuf::protoc)
  set(DRONEBEAT_PROTOC $<TARGET_FILE:protobuf::protoc>)
else()
  find_program(DRONEBEAT_PROTOC protoc REQUIRED)
endif()

if(TARGET gRPC::grpc_cpp_plugin)
  set(DRONEBEAT_GRPC_CPP_PLUGIN $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
else()
  find_program(DRONEBEAT_GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
endif()

# Fetched header-only / test dependencies ------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
  GIT_SHALLOW TRUE)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)

set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib nlohmann_json)

if(DRONEBEAT_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
