#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/clock.hpp"
#include "core/liveness.hpp"
#include "core/types.hpp"
#include "storage/drone_repository.hpp"

namespace dronebeat {

enum class IngestOutcome {
  kAccepted,
  kDuplicate,
  kInvalid,
};

struct IngestResult {
  IngestOutcome status = IngestOutcome::kAccepted;
  std::string error;  // populated when kInvalid
  TimestampMs server_time_ms = 0;
};

struct EventIngestResult {
  bool ok = true;
  std::string error;
  std::int64_t event_id = 0;
};

// Application façade shared by the gRPC and REST layers. Stamps server receive
// time, validates input, and attaches computed liveness to query results.
class FleetMonitor {
 public:
  FleetMonitor(std::shared_ptr<storage::DroneRepository> repo, std::shared_ptr<const Clock> clock,
               LivenessPolicy policy);

  IngestResult IngestHeartbeat(Heartbeat hb);
  EventIngestResult IngestStatus(StatusEvent event);

  std::optional<DroneState> GetDrone(const std::string& drone_id) const;
  std::vector<DroneState> ListDrones(std::optional<Liveness> filter = std::nullopt) const;
  // Drones that are STALE or LOST, oldest contact first.
  std::vector<DroneState> ListUnhealthy() const;
  FleetSummary Summary() const;

  std::vector<Heartbeat> History(const std::string& drone_id, TimestampMs since_ms,
                                 std::size_t limit) const;
  std::vector<StatusEvent> Events(const storage::EventQuery& query) const;

  const LivenessPolicy& policy() const { return policy_; }
  const Clock& clock() const { return *clock_; }
  storage::DroneRepository& repository() const { return *repo_; }

  static std::string ValidateHeartbeat(const Heartbeat& hb);
  static std::string ValidateDroneId(const std::string& drone_id);

 private:
  DroneState WithLiveness(DroneState d, TimestampMs now) const;

  std::shared_ptr<storage::DroneRepository> repo_;
  std::shared_ptr<const Clock> clock_;
  LivenessPolicy policy_;
};

}  // namespace dronebeat
