#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dronebeat::sim {

struct SimConfig {
  std::string target = "localhost:50051";
  int drones = 10;
  std::string id_prefix = "drone";
  std::int64_t interval_ms = 1'000;
  std::int64_t duration_s = 0;        // 0 = run until Ctrl+C
  double dropout_prob = 0.01;         // per-tick chance a drone goes silent for a while
  std::int64_t silence_min_ms = 6'000;
  std::int64_t silence_max_ms = 40'000;
  double crash_fraction = 0.1;        // fraction of drones that eventually go silent forever
  bool stream = false;                // use client-streaming RPC instead of unary
  double home_lat = 37.4275;
  double home_lon = -122.1697;
  std::uint32_t seed = 0;             // 0 = random
};

struct SimConfigResult {
  SimConfig config;
  bool ok = true;
  bool show_help = false;
  std::string error;
};

SimConfigResult ParseSimArgs(const std::vector<std::string>& args);
std::string SimUsage(const std::string& program);

}  // namespace dronebeat::sim
