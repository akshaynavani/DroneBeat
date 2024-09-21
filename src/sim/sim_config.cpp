#include "sim/sim_config.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace dronebeat::sim {
namespace {

std::int64_t ToInt(const std::string& v, const std::string& name) {
  try {
    std::size_t pos = 0;
    const long long out = std::stoll(v, &pos);
    if (pos != v.size()) throw std::invalid_argument("trailing");
    return out;
  } catch (const std::exception&) {
    throw std::invalid_argument("invalid integer for " + name + ": '" + v + "'");
  }
}

double ToDouble(const std::string& v, const std::string& name) {
  try {
    std::size_t pos = 0;
    const double out = std::stod(v, &pos);
    if (pos != v.size()) throw std::invalid_argument("trailing");
    return out;
  } catch (const std::exception&) {
    throw std::invalid_argument("invalid number for " + name + ": '" + v + "'");
  }
}

}  // namespace

SimConfigResult ParseSimArgs(const std::vector<std::string>& args) {
  SimConfigResult r;
  SimConfig& c = r.config;
  if (const char* env = std::getenv("DRONEBEAT_TARGET"); env != nullptr && *env != '\0') {
    c.target = env;
  }
  try {
    for (std::size_t i = 0; i < args.size(); ++i) {
      const std::string& a = args[i];
      auto next = [&]() -> std::string {
        if (i + 1 >= args.size()) throw std::invalid_argument("missing value for " + a);
        return args[++i];
      };
      if (a == "-h" || a == "--help") {
        r.show_help = true;
        return r;
      } else if (a == "--target") {
        c.target = next();
      } else if (a == "--drones") {
        c.drones = static_cast<int>(ToInt(next(), a));
      } else if (a == "--prefix") {
        c.id_prefix = next();
      } else if (a == "--interval-ms") {
        c.interval_ms = ToInt(next(), a);
      } else if (a == "--duration-s") {
        c.duration_s = ToInt(next(), a);
      } else if (a == "--dropout-prob") {
        c.dropout_prob = ToDouble(next(), a);
      } else if (a == "--silence-min-ms") {
        c.silence_min_ms = ToInt(next(), a);
      } else if (a == "--silence-max-ms") {
        c.silence_max_ms = ToInt(next(), a);
      } else if (a == "--crash-fraction") {
        c.crash_fraction = ToDouble(next(), a);
      } else if (a == "--stream") {
        c.stream = true;
      } else if (a == "--home") {
        const std::string v = next();
        const auto comma = v.find(',');
        if (comma == std::string::npos) throw std::invalid_argument("--home expects lat,lon");
        c.home_lat = ToDouble(v.substr(0, comma), a);
        c.home_lon = ToDouble(v.substr(comma + 1), a);
      } else if (a == "--seed") {
        c.seed = static_cast<std::uint32_t>(ToInt(next(), a));
      } else {
        throw std::invalid_argument("unknown option: " + a);
      }
    }
  } catch (const std::exception& e) {
    r.ok = false;
    r.error = e.what();
    return r;
  }

  if (c.drones <= 0 || c.drones > 10'000) {
    r.ok = false;
    r.error = "--drones must be in 1..10000";
  } else if (c.interval_ms < 50) {
    r.ok = false;
    r.error = "--interval-ms must be >= 50";
  } else if (c.dropout_prob < 0.0 || c.dropout_prob > 1.0 || c.crash_fraction < 0.0 ||
             c.crash_fraction > 1.0) {
    r.ok = false;
    r.error = "probabilities must be within 0..1";
  } else if (c.silence_min_ms <= 0 || c.silence_max_ms < c.silence_min_ms) {
    r.ok = false;
    r.error = "require 0 < silence-min-ms <= silence-max-ms";
  }
  return r;
}

std::string SimUsage(const std::string& program) {
  std::ostringstream o;
  o << "Usage: " << program << " [options]\n\n"
    << "  --target host:port      gRPC server address (env DRONEBEAT_TARGET)\n"
    << "  --drones N              number of simulated drones (default 10)\n"
    << "  --prefix NAME           drone id prefix (default 'drone')\n"
    << "  --interval-ms MS        heartbeat period (default 1000)\n"
    << "  --duration-s S          stop after S seconds (default: run forever)\n"
    << "  --dropout-prob P        per-tick chance of a temporary link drop (default 0.01)\n"
    << "  --silence-min-ms MS     minimum silence on drop (default 6000)\n"
    << "  --silence-max-ms MS     maximum silence on drop (default 40000)\n"
    << "  --crash-fraction F      fraction of drones that eventually go silent forever (0.1)\n"
    << "  --stream                use the client-streaming RPC\n"
    << "  --home LAT,LON          fleet home position\n"
    << "  --seed N                RNG seed for reproducible runs\n";
  return o.str();
}

}  // namespace dronebeat::sim
