#include "sim/drone_model.hpp"

#include <algorithm>
#include <cmath>

namespace dronebeat::sim {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetersPerDegLat = 111'320.0;
constexpr double kCruiseAltitude = 120.0;
constexpr float kLowBattery = 20.0f;

}  // namespace

DroneModel::DroneModel(std::string drone_id, double home_lat, double home_lon, std::uint32_t seed)
    : id_(std::move(drone_id)), home_lat_(home_lat), home_lon_(home_lon), rng_(seed) {
  // Spread drones around home so the fleet isn't stacked on one point.
  std::uniform_real_distribution<double> offset(-0.01, 0.01);
  std::uniform_real_distribution<float> battery(70.0f, 100.0f);
  pos_ = {home_lat_ + offset(rng_), home_lon_ + offset(rng_), 0.0};
  battery_pct_ = battery(rng_);
  heading_rad_ = std::uniform_real_distribution<double>(0.0, 2 * kPi)(rng_);
}

void DroneModel::AdvanceMode() {
  mode_elapsed_ms_ = 0;
  switch (mode_) {
    case FlightMode::kIdle: mode_ = FlightMode::kTakeoff; break;
    case FlightMode::kTakeoff: mode_ = FlightMode::kMission; break;
    case FlightMode::kMission:
      mode_ = std::bernoulli_distribution(0.3)(rng_) ? FlightMode::kHover : FlightMode::kMission;
      break;
    case FlightMode::kHover: mode_ = FlightMode::kMission; break;
    case FlightMode::kReturnToHome: mode_ = FlightMode::kLanding; break;
    case FlightMode::kLanding:
      mode_ = FlightMode::kIdle;
      battery_pct_ = 100.0f;  // "battery swap" on the ground
      low_battery_reported_ = false;
      break;
    default: mode_ = FlightMode::kIdle; break;
  }
}

void DroneModel::Raise(Severity severity, std::string code, std::string message,
                       TimestampMs now_ms) {
  StatusEvent e;
  e.drone_id = id_;
  e.sent_at_ms = now_ms;
  e.severity = severity;
  e.code = std::move(code);
  e.message = std::move(message);
  pending_ = std::move(e);
}

Heartbeat DroneModel::Step(std::int64_t dt_ms, TimestampMs now_ms) {
  const double dt_s = static_cast<double>(dt_ms) / 1000.0;
  mode_elapsed_ms_ += dt_ms;

  // Mode transitions on simple timers.
  const std::int64_t mode_duration = [&]() -> std::int64_t {
    switch (mode_) {
      case FlightMode::kIdle: return 5'000;
      case FlightMode::kTakeoff: return 8'000;
      case FlightMode::kMission: return 30'000;
      case FlightMode::kHover: return 6'000;
      case FlightMode::kLanding: return 8'000;
      default: return 1'000'000'000;  // RTH ends when we reach home
    }
  }();
  if (mode_elapsed_ms_ >= mode_duration) AdvanceMode();

  if (battery_pct_ < kLowBattery && !low_battery_reported_) {
    low_battery_reported_ = true;
    mode_ = FlightMode::kReturnToHome;
    mode_elapsed_ms_ = 0;
    Raise(Severity::kWarning, "LOW_BATTERY",
          "battery at " + std::to_string(static_cast<int>(battery_pct_)) + "%, returning home",
          now_ms);
  }

  // Kinematics.
  std::normal_distribution<double> turn(0.0, 0.15);
  switch (mode_) {
    case FlightMode::kIdle:
      speed_mps_ = 0.0f;
      pos_.altitude_m = 0.0;
      break;
    case FlightMode::kTakeoff:
      speed_mps_ = 0.0f;
      pos_.altitude_m = std::min(kCruiseAltitude, pos_.altitude_m + 15.0 * dt_s);
      break;
    case FlightMode::kMission:
      heading_rad_ += turn(rng_);
      speed_mps_ = 12.0f;
      pos_.altitude_m = kCruiseAltitude;
      break;
    case FlightMode::kHover:
      speed_mps_ = 0.0f;
      break;
    case FlightMode::kReturnToHome: {
      const double dlat = home_lat_ - pos_.latitude;
      const double dlon = home_lon_ - pos_.longitude;
      const double dist_m = std::hypot(dlat * kMetersPerDegLat,
                                       dlon * kMetersPerDegLat * std::cos(pos_.latitude * kPi / 180));
      if (dist_m < 20.0) {
        AdvanceMode();  // -> landing
        speed_mps_ = 0.0f;
      } else {
        heading_rad_ = std::atan2(dlat, dlon * std::cos(pos_.latitude * kPi / 180));
        speed_mps_ = 15.0f;
      }
      break;
    }
    case FlightMode::kLanding:
      speed_mps_ = 0.0f;
      pos_.altitude_m = std::max(0.0, pos_.altitude_m - 15.0 * dt_s);
      break;
    default:
      break;
  }

  // heading measured from east (x) counter-clockwise; north component -> latitude.
  const double dist = static_cast<double>(speed_mps_) * dt_s;
  pos_.latitude += dist * std::sin(heading_rad_) / kMetersPerDegLat;
  pos_.longitude += dist * std::cos(heading_rad_) /
                    (kMetersPerDegLat * std::cos(pos_.latitude * kPi / 180));
  pos_.latitude = std::clamp(pos_.latitude, -89.9, 89.9);
  if (pos_.longitude > 180.0) pos_.longitude -= 360.0;
  if (pos_.longitude < -180.0) pos_.longitude += 360.0;

  // Battery: idle drains nothing, flight ~0.25%/s, faster at speed.
  if (mode_ != FlightMode::kIdle) {
    const float drain = static_cast<float>((0.15 + 0.01 * speed_mps_) * dt_s);
    battery_pct_ = std::max(0.0f, battery_pct_ - drain);
  }
  if (battery_pct_ <= 0.0f && mode_ != FlightMode::kEmergency) {
    mode_ = FlightMode::kEmergency;
    Raise(Severity::kCritical, "BATTERY_DEPLETED", "forced landing", now_ms);
  }

  Heartbeat hb;
  hb.drone_id = id_;
  hb.sequence = ++sequence_;
  hb.sent_at_ms = now_ms;
  hb.mode = mode_;
  hb.position = pos_;
  hb.battery_pct = battery_pct_;
  hb.ground_speed_mps = speed_mps_;
  hb.firmware_version = "sim-1.0.0";
  return hb;
}

std::optional<StatusEvent> DroneModel::TakePendingEvent() {
  auto out = std::move(pending_);
  pending_.reset();
  return out;
}

}  // namespace dronebeat::sim
