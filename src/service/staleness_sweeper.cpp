#include "service/staleness_sweeper.hpp"

#include <iostream>

namespace dronebeat {
namespace {

constexpr TimestampMs kPruneEveryMs = 60'000;

std::string TransitionCode(Liveness to) {
  switch (to) {
    case Liveness::kAlive: return "LINK_RECOVERED";
    case Liveness::kStale: return "LINK_STALE";
    case Liveness::kLost: return "LINK_LOST";
  }
  return "LINK_UNKNOWN";
}

Severity TransitionSeverity(Liveness to) {
  switch (to) {
    case Liveness::kAlive: return Severity::kInfo;
    case Liveness::kStale: return Severity::kWarning;
    case Liveness::kLost: return Severity::kCritical;
  }
  return Severity::kInfo;
}

}  // namespace

StalenessSweeper::StalenessSweeper(FleetMonitor& monitor, std::chrono::milliseconds interval,
                                   TimestampMs history_retention_ms)
    : monitor_(monitor), interval_(interval), history_retention_ms_(history_retention_ms) {}

StalenessSweeper::~StalenessSweeper() { Stop(); }

void StalenessSweeper::SetListener(Listener listener) {
  std::lock_guard lock(sweep_mu_);
  listener_ = std::move(listener);
}

void StalenessSweeper::Start() {
  std::lock_guard lock(mu_);
  if (running_) return;
  running_ = true;
  thread_ = std::thread([this] { Loop(); });
}

void StalenessSweeper::Stop() {
  {
    std::lock_guard lock(mu_);
    if (!running_) return;
    running_ = false;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void StalenessSweeper::Loop() {
  std::unique_lock lock(mu_);
  while (running_) {
    lock.unlock();
    try {
      RunOnce();
    } catch (const std::exception& e) {
      std::cerr << "[sweeper] sweep failed: " << e.what() << "\n";
    }
    lock.lock();
    cv_.wait_for(lock, interval_, [this] { return !running_; });
  }
}

std::vector<LivenessTransition> StalenessSweeper::RunOnce() {
  std::lock_guard lock(sweep_mu_);
  std::vector<LivenessTransition> transitions;
  const TimestampMs now = monitor_.clock().NowMs();

  for (const auto& drone : monitor_.ListDrones()) {
    auto [it, inserted] = last_known_.try_emplace(drone.drone_id, drone.liveness);
    // A drone discovered for the first time while already unhealthy (e.g. after a
    // server restart) is still worth reporting; a healthy newcomer is not.
    const Liveness previous = inserted ? Liveness::kAlive : it->second;
    if (previous == drone.liveness) continue;
    it->second = drone.liveness;
    transitions.push_back({drone.drone_id, previous, drone.liveness, drone.last_seen_ms});
  }

  for (const auto& t : transitions) {
    StatusEvent event;
    event.drone_id = t.drone_id;
    event.sent_at_ms = now;
    event.severity = TransitionSeverity(t.to);
    event.code = TransitionCode(t.to);
    event.message = std::string("liveness ") + std::string(ToString(t.from)) + " -> " +
                    std::string(ToString(t.to)) + ", last heartbeat " +
                    std::to_string(now - t.last_seen_ms) + " ms ago";
    monitor_.IngestStatus(event);
    std::cout << "[sweeper] " << t.drone_id << ": " << event.message << "\n";
    if (listener_) listener_(t);
  }

  if (now - last_prune_ms_ >= kPruneEveryMs) {
    last_prune_ms_ = now;
    const auto removed = monitor_.repository().PruneHistory(now - history_retention_ms_);
    if (removed > 0) {
      std::cout << "[sweeper] pruned " << removed << " heartbeat history rows\n";
    }
  }
  return transitions;
}

}  // namespace dronebeat
