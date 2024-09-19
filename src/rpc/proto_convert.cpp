#include "rpc/proto_convert.hpp"

namespace dronebeat::rpc {

namespace pb = dronebeat::v1;

FlightMode FromProto(pb::FlightMode mode) {
  switch (mode) {
    case pb::FLIGHT_MODE_IDLE: return FlightMode::kIdle;
    case pb::FLIGHT_MODE_TAKEOFF: return FlightMode::kTakeoff;
    case pb::FLIGHT_MODE_MISSION: return FlightMode::kMission;
    case pb::FLIGHT_MODE_HOVER: return FlightMode::kHover;
    case pb::FLIGHT_MODE_RETURN_TO_HOME: return FlightMode::kReturnToHome;
    case pb::FLIGHT_MODE_LANDING: return FlightMode::kLanding;
    case pb::FLIGHT_MODE_EMERGENCY: return FlightMode::kEmergency;
    default: return FlightMode::kUnspecified;
  }
}

pb::FlightMode ToProto(FlightMode mode) {
  switch (mode) {
    case FlightMode::kIdle: return pb::FLIGHT_MODE_IDLE;
    case FlightMode::kTakeoff: return pb::FLIGHT_MODE_TAKEOFF;
    case FlightMode::kMission: return pb::FLIGHT_MODE_MISSION;
    case FlightMode::kHover: return pb::FLIGHT_MODE_HOVER;
    case FlightMode::kReturnToHome: return pb::FLIGHT_MODE_RETURN_TO_HOME;
    case FlightMode::kLanding: return pb::FLIGHT_MODE_LANDING;
    case FlightMode::kEmergency: return pb::FLIGHT_MODE_EMERGENCY;
    case FlightMode::kUnspecified: break;
  }
  return pb::FLIGHT_MODE_UNSPECIFIED;
}

Severity FromProto(pb::Severity severity) {
  switch (severity) {
    case pb::SEVERITY_INFO: return Severity::kInfo;
    case pb::SEVERITY_WARNING: return Severity::kWarning;
    case pb::SEVERITY_CRITICAL: return Severity::kCritical;
    default: return Severity::kUnspecified;
  }
}

pb::Severity ToProto(Severity severity) {
  switch (severity) {
    case Severity::kInfo: return pb::SEVERITY_INFO;
    case Severity::kWarning: return pb::SEVERITY_WARNING;
    case Severity::kCritical: return pb::SEVERITY_CRITICAL;
    case Severity::kUnspecified: break;
  }
  return pb::SEVERITY_UNSPECIFIED;
}

Heartbeat FromProto(const pb::Heartbeat& msg) {
  Heartbeat hb;
  hb.drone_id = msg.drone_id();
  hb.sequence = msg.sequence();
  hb.sent_at_ms = msg.sent_at_ms();
  hb.mode = FromProto(msg.mode());
  if (msg.has_position()) {
    hb.position.latitude = msg.position().latitude();
    hb.position.longitude = msg.position().longitude();
    hb.position.altitude_m = msg.position().altitude_m();
  }
  hb.battery_pct = msg.battery_pct();
  hb.ground_speed_mps = msg.ground_speed_mps();
  hb.firmware_version = msg.firmware_version();
  return hb;
}

void ToProto(const Heartbeat& hb, pb::Heartbeat* out) {
  out->set_drone_id(hb.drone_id);
  out->set_sequence(hb.sequence);
  out->set_sent_at_ms(hb.sent_at_ms);
  out->set_mode(ToProto(hb.mode));
  auto* pos = out->mutable_position();
  pos->set_latitude(hb.position.latitude);
  pos->set_longitude(hb.position.longitude);
  pos->set_altitude_m(hb.position.altitude_m);
  out->set_battery_pct(hb.battery_pct);
  out->set_ground_speed_mps(hb.ground_speed_mps);
  out->set_firmware_version(hb.firmware_version);
}

StatusEvent FromProto(const pb::StatusUpdate& msg) {
  StatusEvent e;
  e.drone_id = msg.drone_id();
  e.sent_at_ms = msg.sent_at_ms();
  e.severity = FromProto(msg.severity());
  e.code = msg.code();
  e.message = msg.message();
  return e;
}

}  // namespace dronebeat::rpc
