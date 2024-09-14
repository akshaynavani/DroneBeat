add_library(dronebeat_core STATIC
  core/types.cpp
  core/liveness.cpp
  config/config.cpp)
target_include_directories(dronebeat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
