#pragma once

#include <cstdint>
#include <optional>
#include <random>
#include <string>

#include "core/types.hpp"

namespace dronebeat::sim {

// Pure kinematic/battery model of a single drone; no networking.
// Each Step() advances the simulation by `dt_ms` and returns the heartbeat to send.
class DroneModel {
 public:
  DroneModel(std::string drone_id, double home_lat, double home_lon, std::uint32_t seed);

  Heartbeat Step(std::int64_t dt_ms, TimestampMs now_ms);

  // Returns a status event if something noteworthy happened during the last Step().
  std::optional<StatusEvent> TakePendingEvent();

  const std::string& id() const { return id_; }
  float battery_pct() const { return battery_pct_; }
  FlightMode mode() const { return mode_; }

 private:
  void AdvanceMode();
  void Raise(Severity severity, std::string code, std::string message, TimestampMs now_ms);

  std::string id_;
  double home_lat_;
  double home_lon_;
  std::mt19937 rng_;

  std::uint64_t sequence_ = 0;
  FlightMode mode_ = FlightMode::kIdle;
  std::int64_t mode_elapsed_ms_ = 0;
  GeoPosition pos_;
  double heading_rad_ = 0.0;
  float speed_mps_ = 0.0f;
  float battery_pct_ = 100.0f;
  bool low_battery_reported_ = false;
  std::optional<StatusEvent> pending_;
};

}  // namespace dronebeat::sim
