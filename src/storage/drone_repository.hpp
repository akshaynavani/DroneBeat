#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "storage/sqlite.hpp"

namespace dronebeat::storage {

enum class RecordResult {
  kAccepted,
  kDuplicate,  // same sequence number as the last stored heartbeat
};

struct EventQuery {
  std::optional<std::string> drone_id;
  TimestampMs since_ms = 0;
  std::size_t limit = 100;
};

// Thread-safe persistence for drone state, heartbeat history and status events.
// A single connection is serialised with a mutex; SQLite runs in WAL mode.
class DroneRepository {
 public:
  explicit DroneRepository(const std::string& db_path);

  // Upserts the drone's latest state and appends a history row atomically.
  // `hb.received_at_ms` must be set by the caller.
  RecordResult RecordHeartbeat(const Heartbeat& hb);

  // Stores a status event and returns its id. `received_at_ms` must be set.
  std::int64_t RecordEvent(const StatusEvent& event);

  std::optional<DroneState> GetDrone(const std::string& drone_id);
  std::vector<DroneState> ListDrones();
  // Drones whose last heartbeat was received strictly before `cutoff_ms`.
  std::vector<DroneState> ListSeenBefore(TimestampMs cutoff_ms);

  std::vector<Heartbeat> History(const std::string& drone_id, TimestampMs since_ms,
                                 std::size_t limit);
  std::vector<StatusEvent> ListEvents(const EventQuery& query);

  // Deletes heartbeat history older than `before_ms`; returns rows removed.
  std::size_t PruneHistory(TimestampMs before_ms);

  std::size_t DroneCount();

 private:
  std::vector<DroneState> QueryDrones(const std::string& where, std::optional<TimestampMs> arg);

  std::mutex mu_;
  Database db_;
};

}  // namespace dronebeat::storage
