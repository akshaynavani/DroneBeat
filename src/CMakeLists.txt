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
