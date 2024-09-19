add_library(dronebeat_core STATIC
  core/types.cpp
  core/liveness.cpp
  config/config.cpp)
target_include_directories(dronebeat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(dronebeat_storage STATIC
  storage/sqlite.cpp
  storage/schema.cpp
  storage/drone_repository.cpp)
target_link_libraries(dronebeat_storage PUBLIC dronebeat_core SQLite::SQLite3)

add_library(dronebeat_service STATIC
  service/fleet_monitor.cpp
  service/staleness_sweeper.cpp)
target_link_libraries(dronebeat_service PUBLIC dronebeat_storage Threads::Threads)

# Note: directory is named rpc/ (not grpc/) so it never shadows <grpc/...> system headers.
add_library(dronebeat_rpc STATIC
  rpc/proto_convert.cpp
  rpc/telemetry_service.cpp)
target_link_libraries(dronebeat_rpc PUBLIC dronebeat_service dronebeat_proto)
