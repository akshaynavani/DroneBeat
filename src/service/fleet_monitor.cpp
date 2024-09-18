#include "service/fleet_monitor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace dronebeat {
namespace {

constexpr std::size_t kMaxDroneIdLength = 64;
constexpr std::size_t kMaxCodeLength = 64;
constexpr std::size_t kMaxMessageLength = 1024;

}  // namespace

FleetMonitor::FleetMonitor(std::shared_ptr<storage::DroneRepository> repo,
                           std::shared_ptr<const Clock> clock, LivenessPolicy policy)
    : repo_(std::move(repo)), clock_(std::move(clock)), policy_(policy) {}

std::string FleetMonitor::ValidateDroneId(const std::string& drone_id) {
  if (drone_id.empty()) return "drone_id is required";
  if (drone_id.size() > kMaxDroneIdLength) return "drone_id exceeds 64 characters";
  const bool ok = std::all_of(drone_id.begin(), drone_id.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
  });
  if (!ok) return "drone_id may only contain [A-Za-z0-9._-]";
  return {};
}

std::string FleetMonitor::ValidateHeartbeat(const Heartbeat& hb) {
  if (auto err = ValidateDroneId(hb.drone_id); !err.empty()) return err;
  const auto& p = hb.position;
  if (!std::isfinite(p.latitude) || p.latitude < -90.0 || p.latitude > 90.0) {
    return "latitude out of range";
  }
  if (!std::isfinite(p.longitude) || p.longitude < -180.0 || p.longitude > 180.0) {
    return "longitude out of range";
  }
  if (!std::isfinite(p.altitude_m)) return "altitude must be finite";
  if (!std::isfinite(hb.battery_pct) || hb.battery_pct < 0.0f || hb.battery_pct > 100.0f) {
    return "battery_pct must be within 0..100";
  }
  if (!std::isfinite(hb.ground_speed_mps) || hb.ground_speed_mps < 0.0f) {
    return "ground_speed_mps must be non-negative";
  }
  return {};
}

IngestResult FleetMonitor::IngestHeartbeat(Heartbeat hb) {
  IngestResult result;
  result.server_time_ms = clock_->NowMs();
  if (auto err = ValidateHeartbeat(hb); !err.empty()) {
    result.status = IngestOutcome::kInvalid;
    result.error = std::move(err);
    return result;
  }
  hb.received_at_ms = result.server_time_ms;
  result.status = repo_->RecordHeartbeat(hb) == storage::RecordResult::kAccepted
                      ? IngestOutcome::kAccepted
                      : IngestOutcome::kDuplicate;
  return result;
}

EventIngestResult FleetMonitor::IngestStatus(StatusEvent event) {
  EventIngestResult result;
  if (auto err = ValidateDroneId(event.drone_id); !err.empty()) {
    result.ok = false;
    result.error = std::move(err);
    return result;
  }
  if (event.code.empty() || event.code.size() > kMaxCodeLength) {
    result.ok = false;
    result.error = "code is required and must be at most 64 characters";
    return result;
  }
  if (event.message.size() > kMaxMessageLength) {
    event.message.resize(kMaxMessageLength);
  }
  if (event.severity == Severity::kUnspecified) event.severity = Severity::kInfo;
  event.received_at_ms = clock_->NowMs();
  result.event_id = repo_->RecordEvent(event);
  return result;
}

DroneState FleetMonitor::WithLiveness(DroneState d, TimestampMs now) const {
  d.liveness = Classify(policy_, d.last_seen_ms, now);
  return d;
}

std::optional<DroneState> FleetMonitor::GetDrone(const std::string& drone_id) const {
  auto d = repo_->GetDrone(drone_id);
  if (!d) return std::nullopt;
  return WithLiveness(std::move(*d), clock_->NowMs());
}

std::vector<DroneState> FleetMonitor::ListDrones(std::optional<Liveness> filter) const {
  const TimestampMs now = clock_->NowMs();
  std::vector<DroneState> out;
  for (auto& d : repo_->ListDrones()) {
    auto with = WithLiveness(std::move(d), now);
    if (!filter || with.liveness == *filter) out.push_back(std::move(with));
  }
  return out;
}

std::vector<DroneState> FleetMonitor::ListUnhealthy() const {
  const TimestampMs now = clock_->NowMs();
  std::vector<DroneState> out;
  for (auto& d : repo_->ListSeenBefore(now - policy_.stale_after_ms)) {
    out.push_back(WithLiveness(std::move(d), now));
  }
  std::sort(out.begin(), out.end(), [](const DroneState& a, const DroneState& b) {
    return a.last_seen_ms < b.last_seen_ms;
  });
  return out;
}

FleetSummary FleetMonitor::Summary() const {
  FleetSummary s;
  s.generated_at_ms = clock_->NowMs();
  for (const auto& d : repo_->ListDrones()) {
    ++s.total;
    switch (Classify(policy_, d.last_seen_ms, s.generated_at_ms)) {
      case Liveness::kAlive: ++s.alive; break;
      case Liveness::kStale: ++s.stale; break;
      case Liveness::kLost: ++s.lost; break;
    }
    if (d.battery_pct < policy_.low_battery_pct) ++s.low_battery;
  }
  return s;
}

std::vector<Heartbeat> FleetMonitor::History(const std::string& drone_id, TimestampMs since_ms,
                                             std::size_t limit) const {
  return repo_->History(drone_id, since_ms, limit);
}

std::vector<StatusEvent> FleetMonitor::Events(const storage::EventQuery& query) const {
  return repo_->ListEvents(query);
}

}  // namespace dronebeat
