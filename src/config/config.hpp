#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/liveness.hpp"

namespace dronebeat {

struct ServerConfig {
  std::string grpc_address = "0.0.0.0:50051";
  std::string rest_host = "0.0.0.0";
  int rest_port = 8080;
  std::string db_path = "dronebeat.db";
  LivenessPolicy liveness;
  std::int64_t sweep_interval_ms = 1'000;
  std::int64_t history_retention_ms = 24LL * 60 * 60 * 1000;  // 24h of heartbeat history
  std::uint32_t heartbeat_interval_hint_ms = 1'000;
};

struct ConfigResult {
  ServerConfig config;
  bool ok = true;
  bool show_help = false;
  std::string error;
};

// Resolution order: built-in defaults < DRONEBEAT_* environment variables < CLI flags.
ConfigResult LoadServerConfig(const std::vector<std::string>& args);

std::string ServerUsage(const std::string& program);

}  // namespace dronebeat
