#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "service/fleet_monitor.hpp"

namespace dronebeat {

struct LivenessTransition {
  std::string drone_id;
  Liveness from;
  Liveness to;
  TimestampMs last_seen_ms;
};

// Periodically re-classifies every drone and records an event whenever a drone
// changes liveness state (ALIVE -> STALE -> LOST, or recovers back to ALIVE).
// Also prunes heartbeat history beyond the retention window.
class StalenessSweeper {
 public:
  using Listener = std::function<void(const LivenessTransition&)>;

  StalenessSweeper(FleetMonitor& monitor, std::chrono::milliseconds interval,
                   TimestampMs history_retention_ms);
  ~StalenessSweeper();

  StalenessSweeper(const StalenessSweeper&) = delete;
  StalenessSweeper& operator=(const StalenessSweeper&) = delete;

  void SetListener(Listener listener);
  void Start();
  void Stop();

  // Performs a single sweep synchronously; returns detected transitions.
  std::vector<LivenessTransition> RunOnce();

 private:
  void Loop();

  FleetMonitor& monitor_;
  std::chrono::milliseconds interval_;
  TimestampMs history_retention_ms_;
  Listener listener_;

  std::map<std::string, Liveness> last_known_;  // guarded by sweep_mu_
  std::mutex sweep_mu_;
  TimestampMs last_prune_ms_ = 0;

  std::mutex mu_;
  std::condition_variable cv_;
  bool running_ = false;
  std::thread thread_;
};

}  // namespace dronebeat
