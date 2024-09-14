#include "config/config.hpp"

#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

namespace dronebeat {
namespace {

using Setter = std::function<void(ServerConfig&, const std::string&)>;

struct Option {
  std::string flag;     // --grpc-address
  std::string env;      // DRONEBEAT_GRPC_ADDRESS
  std::string help;
  Setter set;
};

std::int64_t ParseInt(const std::string& value, const std::string& name) {
  try {
    std::size_t pos = 0;
    const long long v = std::stoll(value, &pos);
    if (pos != value.size()) throw std::invalid_argument("trailing characters");
    return v;
  } catch (const std::exception&) {
    throw std::invalid_argument("invalid integer for " + name + ": '" + value + "'");
  }
}

const std::vector<Option>& Options() {
  static const std::vector<Option> kOptions = {
      {"--grpc-address", "DRONEBEAT_GRPC_ADDRESS", "gRPC listen address (host:port)",
       [](ServerConfig& c, const std::string& v) { c.grpc_address = v; }},
      {"--rest-host", "DRONEBEAT_REST_HOST", "REST bind host",
       [](ServerConfig& c, const std::string& v) { c.rest_host = v; }},
      {"--rest-port", "DRONEBEAT_REST_PORT", "REST listen port",
       [](ServerConfig& c, const std::string& v) {
         c.rest_port = static_cast<int>(ParseInt(v, "rest port"));
       }},
      {"--db", "DRONEBEAT_DB_PATH", "SQLite database path (':memory:' for ephemeral)",
       [](ServerConfig& c, const std::string& v) { c.db_path = v; }},
      {"--stale-after-ms", "DRONEBEAT_STALE_AFTER_MS", "age after which a drone is STALE",
       [](ServerConfig& c, const std::string& v) {
         c.liveness.stale_after_ms = ParseInt(v, "stale-after-ms");
       }},
      {"--lost-after-ms", "DRONEBEAT_LOST_AFTER_MS", "age after which a drone is LOST",
       [](ServerConfig& c, const std::string& v) {
         c.liveness.lost_after_ms = ParseInt(v, "lost-after-ms");
       }},
      {"--low-battery-pct", "DRONEBEAT_LOW_BATTERY_PCT", "battery threshold for warnings",
       [](ServerConfig& c, const std::string& v) {
         c.liveness.low_battery_pct = static_cast<float>(ParseInt(v, "low-battery-pct"));
       }},
      {"--sweep-interval-ms", "DRONEBEAT_SWEEP_INTERVAL_MS", "staleness sweeper period",
       [](ServerConfig& c, const std::string& v) {
         c.sweep_interval_ms = ParseInt(v, "sweep-interval-ms");
       }},
      {"--history-retention-ms", "DRONEBEAT_HISTORY_RETENTION_MS",
       "how long heartbeat history rows are kept",
       [](ServerConfig& c, const std::string& v) {
         c.history_retention_ms = ParseInt(v, "history-retention-ms");
       }},
  };
  return kOptions;
}

std::string Validate(const ServerConfig& c) {
  if (c.rest_port <= 0 || c.rest_port > 65535) return "rest port must be in 1..65535";
  if (!c.liveness.IsValid()) return "require 0 < stale-after-ms < lost-after-ms";
  if (c.sweep_interval_ms <= 0) return "sweep-interval-ms must be positive";
  if (c.history_retention_ms <= 0) return "history-retention-ms must be positive";
  if (c.grpc_address.empty()) return "grpc address must not be empty";
  return {};
}

}  // namespace

ConfigResult LoadServerConfig(const std::vector<std::string>& args) {
  ConfigResult result;
  try {
    for (const auto& opt : Options()) {
      if (const char* env = std::getenv(opt.env.c_str()); env != nullptr && *env != '\0') {
        opt.set(result.config, env);
      }
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
      std::string arg = args[i];
      if (arg == "-h" || arg == "--help") {
        result.show_help = true;
        return result;
      }
      std::string value;
      bool has_inline_value = false;
      if (auto eq = arg.find('='); eq != std::string::npos) {
        value = arg.substr(eq + 1);
        arg = arg.substr(0, eq);
        has_inline_value = true;
      }
      const Option* match = nullptr;
      for (const auto& opt : Options()) {
        if (opt.flag == arg) match = &opt;
      }
      if (match == nullptr) throw std::invalid_argument("unknown option: " + arg);
      if (!has_inline_value) {
        if (i + 1 >= args.size()) throw std::invalid_argument("missing value for " + arg);
        value = args[++i];
      }
      match->set(result.config, value);
    }
  } catch (const std::exception& e) {
    result.ok = false;
    result.error = e.what();
    return result;
  }

  if (auto err = Validate(result.config); !err.empty()) {
    result.ok = false;
    result.error = err;
  }
  return result;
}

std::string ServerUsage(const std::string& program) {
  std::ostringstream out;
  out << "Usage: " << program << " [options]\n\nOptions (env var in brackets):\n";
  for (const auto& opt : Options()) {
    out << "  " << opt.flag << " <value>  [" << opt.env << "]\n      " << opt.help << "\n";
  }
  out << "  -h, --help\n";
  return out.str();
}

}  // namespace dronebeat
