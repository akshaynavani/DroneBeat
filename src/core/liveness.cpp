#include "core/liveness.hpp"

namespace dronebeat {

Liveness Classify(const LivenessPolicy& policy, TimestampMs last_seen_ms, TimestampMs now_ms) {
  const TimestampMs age = now_ms - last_seen_ms;
  if (age > policy.lost_after_ms) return Liveness::kLost;
  if (age > policy.stale_after_ms) return Liveness::kStale;
  return Liveness::kAlive;
}

}  // namespace dronebeat
