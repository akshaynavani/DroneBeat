#include "rest/json.hpp"

#include <string>

namespace dronebeat {

using nlohmann::json;

void to_json(json& j, const GeoPosition& p) {
  j = json{{"latitude", p.latitude}, {"longitude", p.longitude}, {"altitude_m", p.altitude_m}};
}

void to_json(json& j, const DroneState& d) {
  j = json{
      {"drone_id", d.drone_id},
      {"liveness", std::string(ToString(d.liveness))},
      {"first_seen_ms", d.first_seen_ms},
      {"last_seen_ms", d.last_seen_ms},
      {"last_sequence", d.last_sequence},
      {"heartbeat_count", d.heartbeat_count},
      {"mode", std::string(ToString(d.mode))},
      {"position", d.position},
      {"battery_pct", d.battery_pct},
      {"ground_speed_mps", d.ground_speed_mps},
      {"firmware_version", d.firmware_version},
  };
}

void to_json(json& j, const Heartbeat& hb) {
  j = json{
      {"sequence", hb.sequence},
      {"sent_at_ms", hb.sent_at_ms},
      {"received_at_ms", hb.received_at_ms},
      {"mode", std::string(ToString(hb.mode))},
      {"position", hb.position},
      {"battery_pct", hb.battery_pct},
      {"ground_speed_mps", hb.ground_speed_mps},
  };
}

void to_json(json& j, const StatusEvent& e) {
  j = json{
      {"id", e.id},
      {"drone_id", e.drone_id},
      {"sent_at_ms", e.sent_at_ms},
      {"received_at_ms", e.received_at_ms},
      {"severity", std::string(ToString(e.severity))},
      {"code", e.code},
      {"message", e.message},
  };
}

void to_json(json& j, const FleetSummary& s) {
  j = json{
      {"total", s.total},     {"alive", s.alive},
      {"stale", s.stale},     {"lost", s.lost},
      {"low_battery", s.low_battery}, {"generated_at_ms", s.generated_at_ms},
  };
}

void to_json(json& j, const LivenessPolicy& p) {
  j = json{
      {"stale_after_ms", p.stale_after_ms},
      {"lost_after_ms", p.lost_after_ms},
      {"low_battery_pct", p.low_battery_pct},
  };
}

}  // namespace dronebeat
