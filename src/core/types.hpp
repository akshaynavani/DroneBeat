#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dronebeat {

using TimestampMs = std::int64_t;

enum class FlightMode {
  kUnspecified = 0,
  kIdle,
  kTakeoff,
  kMission,
  kHover,
  kReturnToHome,
  kLanding,
  kEmergency,
};

enum class Severity { kUnspecified = 0, kInfo, kWarning, kCritical };

// Liveness is derived from server receive time, never stored.
enum class Liveness { kAlive, kStale, kLost };

struct GeoPosition {
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_m = 0.0;
};

struct Heartbeat {
  std::string drone_id;
  std::uint64_t sequence = 0;
  TimestampMs sent_at_ms = 0;      // drone clock
  TimestampMs received_at_ms = 0;  // server clock (filled by the server)
  FlightMode mode = FlightMode::kUnspecified;
  GeoPosition position;
  float battery_pct = 0.0f;
  float ground_speed_mps = 0.0f;
  std::string firmware_version;
};

// Latest known state of a single drone.
struct DroneState {
  std::string drone_id;
  TimestampMs first_seen_ms = 0;
  TimestampMs last_seen_ms = 0;
  std::uint64_t last_sequence = 0;
  std::uint64_t heartbeat_count = 0;
  FlightMode mode = FlightMode::kUnspecified;
  GeoPosition position;
  float battery_pct = 0.0f;
  float ground_speed_mps = 0.0f;
  std::string firmware_version;
  Liveness liveness = Liveness::kAlive;  // populated at query time
};

struct StatusEvent {
  std::int64_t id = 0;
  std::string drone_id;
  TimestampMs sent_at_ms = 0;
  TimestampMs received_at_ms = 0;
  Severity severity = Severity::kUnspecified;
  std::string code;
  std::string message;
};

struct FleetSummary {
  std::size_t total = 0;
  std::size_t alive = 0;
  std::size_t stale = 0;
  std::size_t lost = 0;
  std::size_t low_battery = 0;
  TimestampMs generated_at_ms = 0;
};

std::string_view ToString(FlightMode mode);
std::string_view ToString(Severity severity);
std::string_view ToString(Liveness liveness);

std::optional<FlightMode> FlightModeFromString(std::string_view s);
std::optional<Severity> SeverityFromString(std::string_view s);
std::optional<Liveness> LivenessFromString(std::string_view s);

}  // namespace dronebeat
