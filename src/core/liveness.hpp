#pragma once

#include "core/types.hpp"

namespace dronebeat {

struct LivenessPolicy {
  TimestampMs stale_after_ms = 5'000;   // missed ~5 heartbeats at 1 Hz
  TimestampMs lost_after_ms = 30'000;   // considered gone
  float low_battery_pct = 20.0f;

  bool IsValid() const { return stale_after_ms > 0 && lost_after_ms > stale_after_ms; }
};

// Classifies a drone based on how long ago the server last heard from it.
// Future timestamps (clock skew between threads) count as alive.
Liveness Classify(const LivenessPolicy& policy, TimestampMs last_seen_ms, TimestampMs now_ms);

}  // namespace dronebeat
