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

add_library(dronebeat_rest STATIC
  rest/json.cpp
  rest/rest_api.cpp)
target_link_libraries(dronebeat_rest PUBLIC dronebeat_service httplib::httplib
                                            nlohmann_json::nlohmann_json)

add_executable(dronebeat_server server/main.cpp)
target_link_libraries(dronebeat_server PRIVATE dronebeat_rpc dronebeat_rest)

add_library(dronebeat_sim STATIC
  sim/sim_config.cpp
  sim/drone_model.cpp)
target_link_libraries(dronebeat_sim PUBLIC dronebeat_core)

add_executable(drone_sim sim/main.cpp)
target_link_libraries(drone_sim PRIVATE dronebeat_sim dronebeat_rpc Threads::Threads)
