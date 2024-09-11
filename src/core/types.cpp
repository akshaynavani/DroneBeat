#include "core/types.hpp"

#include <array>
#include <utility>

namespace dronebeat {
namespace {

constexpr std::array<std::pair<FlightMode, std::string_view>, 8> kFlightModes{{
    {FlightMode::kUnspecified, "UNSPECIFIED"},
    {FlightMode::kIdle, "IDLE"},
    {FlightMode::kTakeoff, "TAKEOFF"},
    {FlightMode::kMission, "MISSION"},
    {FlightMode::kHover, "HOVER"},
    {FlightMode::kReturnToHome, "RETURN_TO_HOME"},
    {FlightMode::kLanding, "LANDING"},
    {FlightMode::kEmergency, "EMERGENCY"},
}};

constexpr std::array<std::pair<Severity, std::string_view>, 4> kSeverities{{
    {Severity::kUnspecified, "UNSPECIFIED"},
    {Severity::kInfo, "INFO"},
    {Severity::kWarning, "WARNING"},
    {Severity::kCritical, "CRITICAL"},
}};

constexpr std::array<std::pair<Liveness, std::string_view>, 3> kLiveness{{
    {Liveness::kAlive, "ALIVE"},
    {Liveness::kStale, "STALE"},
    {Liveness::kLost, "LOST"},
}};

template <typename Enum, std::size_t N>
std::string_view Lookup(const std::array<std::pair<Enum, std::string_view>, N>& table, Enum value) {
  for (const auto& [e, name] : table) {
    if (e == value) return name;
  }
  return "UNKNOWN";
}

template <typename Enum, std::size_t N>
std::optional<Enum> ReverseLookup(const std::array<std::pair<Enum, std::string_view>, N>& table,
                                  std::string_view s) {
  for (const auto& [e, name] : table) {
    if (name == s) return e;
  }
  return std::nullopt;
}

}  // namespace

std::string_view ToString(FlightMode mode) { return Lookup(kFlightModes, mode); }
std::string_view ToString(Severity severity) { return Lookup(kSeverities, severity); }
std::string_view ToString(Liveness liveness) { return Lookup(kLiveness, liveness); }

std::optional<FlightMode> FlightModeFromString(std::string_view s) {
  return ReverseLookup(kFlightModes, s);
}
std::optional<Severity> SeverityFromString(std::string_view s) {
  return ReverseLookup(kSeverities, s);
}
std::optional<Liveness> LivenessFromString(std::string_view s) {
  return ReverseLookup(kLiveness, s);
}

}  // namespace dronebeat
