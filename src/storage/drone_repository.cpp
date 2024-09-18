#include "storage/drone_repository.hpp"

#include "storage/schema.hpp"

namespace dronebeat::storage {
namespace {

constexpr const char* kDroneColumns =
    "drone_id, first_seen_ms, last_seen_ms, last_sequence, heartbeat_count, mode, "
    "latitude, longitude, altitude_m, battery_pct, ground_speed_mps, firmware_version";

DroneState ReadDrone(const Statement& s) {
  DroneState d;
  d.drone_id = s.ColumnText(0);
  d.first_seen_ms = s.ColumnInt64(1);
  d.last_seen_ms = s.ColumnInt64(2);
  d.last_sequence = static_cast<std::uint64_t>(s.ColumnInt64(3));
  d.heartbeat_count = static_cast<std::uint64_t>(s.ColumnInt64(4));
  d.mode = FlightModeFromString(s.ColumnText(5)).value_or(FlightMode::kUnspecified);
  d.position.latitude = s.ColumnDouble(6);
  d.position.longitude = s.ColumnDouble(7);
  d.position.altitude_m = s.ColumnDouble(8);
  d.battery_pct = static_cast<float>(s.ColumnDouble(9));
  d.ground_speed_mps = static_cast<float>(s.ColumnDouble(10));
  d.firmware_version = s.ColumnText(11);
  return d;
}

StatusEvent ReadEvent(const Statement& s) {
  StatusEvent e;
  e.id = s.ColumnInt64(0);
  e.drone_id = s.ColumnText(1);
  e.sent_at_ms = s.ColumnInt64(2);
  e.received_at_ms = s.ColumnInt64(3);
  e.severity = SeverityFromString(s.ColumnText(4)).value_or(Severity::kUnspecified);
  e.code = s.ColumnText(5);
  e.message = s.ColumnText(6);
  return e;
}

}  // namespace

DroneRepository::DroneRepository(const std::string& db_path) : db_(db_path) { Migrate(db_); }

RecordResult DroneRepository::RecordHeartbeat(const Heartbeat& hb) {
  std::lock_guard lock(mu_);
  Transaction tx(db_);

  {
    auto check = db_.Prepare("SELECT last_sequence FROM drones WHERE drone_id = ?1;");
    check.Bind(1, hb.drone_id);
    if (check.Step() && static_cast<std::uint64_t>(check.ColumnInt64(0)) == hb.sequence) {
      return RecordResult::kDuplicate;  // tx rolls back (nothing written)
    }
  }

  const std::string mode(ToString(hb.mode));

  auto upsert = db_.Prepare(R"sql(
    INSERT INTO drones (drone_id, first_seen_ms, last_seen_ms, last_sequence, heartbeat_count,
                        mode, latitude, longitude, altitude_m, battery_pct, ground_speed_mps,
                        firmware_version)
    VALUES (?1, ?2, ?2, ?3, 1, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
    ON CONFLICT(drone_id) DO UPDATE SET
      last_seen_ms     = MAX(drones.last_seen_ms, excluded.last_seen_ms),
      last_sequence    = excluded.last_sequence,
      heartbeat_count  = drones.heartbeat_count + 1,
      mode             = excluded.mode,
      latitude         = excluded.latitude,
      longitude        = excluded.longitude,
      altitude_m       = excluded.altitude_m,
      battery_pct      = excluded.battery_pct,
      ground_speed_mps = excluded.ground_speed_mps,
      firmware_version = CASE WHEN excluded.firmware_version = ''
                              THEN drones.firmware_version
                              ELSE excluded.firmware_version END;
  )sql");
  upsert.Bind(1, hb.drone_id)
      .Bind(2, hb.received_at_ms)
      .Bind(3, hb.sequence)
      .Bind(4, mode)
      .Bind(5, hb.position.latitude)
      .Bind(6, hb.position.longitude)
      .Bind(7, hb.position.altitude_m)
      .Bind(8, static_cast<double>(hb.battery_pct))
      .Bind(9, static_cast<double>(hb.ground_speed_mps))
      .Bind(10, hb.firmware_version);
  upsert.Run();

  auto insert = db_.Prepare(R"sql(
    INSERT INTO heartbeats (drone_id, sequence, sent_at_ms, received_at_ms, mode,
                            latitude, longitude, altitude_m, battery_pct, ground_speed_mps)
    VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);
  )sql");
  insert.Bind(1, hb.drone_id)
      .Bind(2, hb.sequence)
      .Bind(3, hb.sent_at_ms)
      .Bind(4, hb.received_at_ms)
      .Bind(5, mode)
      .Bind(6, hb.position.latitude)
      .Bind(7, hb.position.longitude)
      .Bind(8, hb.position.altitude_m)
      .Bind(9, static_cast<double>(hb.battery_pct))
      .Bind(10, static_cast<double>(hb.ground_speed_mps));
  insert.Run();

  tx.Commit();
  return RecordResult::kAccepted;
}

std::int64_t DroneRepository::RecordEvent(const StatusEvent& event) {
  std::lock_guard lock(mu_);
  auto stmt = db_.Prepare(R"sql(
    INSERT INTO events (drone_id, sent_at_ms, received_at_ms, severity, code, message)
    VALUES (?1, ?2, ?3, ?4, ?5, ?6);
  )sql");
  stmt.Bind(1, event.drone_id)
      .Bind(2, event.sent_at_ms)
      .Bind(3, event.received_at_ms)
      .Bind(4, ToString(event.severity))
      .Bind(5, event.code)
      .Bind(6, event.message);
  stmt.Run();
  return db_.LastInsertRowId();
}

std::vector<DroneState> DroneRepository::QueryDrones(const std::string& where,
                                                     std::optional<TimestampMs> arg) {
  std::lock_guard lock(mu_);
  std::string sql = std::string("SELECT ") + kDroneColumns + " FROM drones";
  if (!where.empty()) sql += " WHERE " + where;
  sql += " ORDER BY drone_id;";
  auto stmt = db_.Prepare(sql);
  if (arg) stmt.Bind(1, *arg);
  std::vector<DroneState> out;
  while (stmt.Step()) out.push_back(ReadDrone(stmt));
  return out;
}

std::optional<DroneState> DroneRepository::GetDrone(const std::string& drone_id) {
  std::lock_guard lock(mu_);
  auto stmt =
      db_.Prepare(std::string("SELECT ") + kDroneColumns + " FROM drones WHERE drone_id = ?1;");
  stmt.Bind(1, drone_id);
  if (!stmt.Step()) return std::nullopt;
  return ReadDrone(stmt);
}

std::vector<DroneState> DroneRepository::ListDrones() { return QueryDrones("", std::nullopt); }

std::vector<DroneState> DroneRepository::ListSeenBefore(TimestampMs cutoff_ms) {
  return QueryDrones("last_seen_ms < ?1", cutoff_ms);
}

std::vector<Heartbeat> DroneRepository::History(const std::string& drone_id,
                                                TimestampMs since_ms, std::size_t limit) {
  std::lock_guard lock(mu_);
  auto stmt = db_.Prepare(R"sql(
    SELECT sequence, sent_at_ms, received_at_ms, mode, latitude, longitude, altitude_m,
           battery_pct, ground_speed_mps
    FROM heartbeats
    WHERE drone_id = ?1 AND received_at_ms >= ?2
    ORDER BY received_at_ms DESC, id DESC
    LIMIT ?3;
  )sql");
  stmt.Bind(1, drone_id).Bind(2, since_ms).Bind(3, static_cast<std::int64_t>(limit));
  std::vector<Heartbeat> out;
  while (stmt.Step()) {
    Heartbeat hb;
    hb.drone_id = drone_id;
    hb.sequence = static_cast<std::uint64_t>(stmt.ColumnInt64(0));
    hb.sent_at_ms = stmt.ColumnInt64(1);
    hb.received_at_ms = stmt.ColumnInt64(2);
    hb.mode = FlightModeFromString(stmt.ColumnText(3)).value_or(FlightMode::kUnspecified);
    hb.position.latitude = stmt.ColumnDouble(4);
    hb.position.longitude = stmt.ColumnDouble(5);
    hb.position.altitude_m = stmt.ColumnDouble(6);
    hb.battery_pct = static_cast<float>(stmt.ColumnDouble(7));
    hb.ground_speed_mps = static_cast<float>(stmt.ColumnDouble(8));
    out.push_back(std::move(hb));
  }
  return out;
}

std::vector<StatusEvent> DroneRepository::ListEvents(const EventQuery& query) {
  std::lock_guard lock(mu_);
  std::string sql =
      "SELECT id, drone_id, sent_at_ms, received_at_ms, severity, code, message "
      "FROM events WHERE received_at_ms >= ?1";
  if (query.drone_id) sql += " AND drone_id = ?3";
  sql += " ORDER BY received_at_ms DESC, id DESC LIMIT ?2;";
  auto stmt = db_.Prepare(sql);
  stmt.Bind(1, query.since_ms).Bind(2, static_cast<std::int64_t>(query.limit));
  if (query.drone_id) stmt.Bind(3, *query.drone_id);
  std::vector<StatusEvent> out;
  while (stmt.Step()) out.push_back(ReadEvent(stmt));
  return out;
}

std::size_t DroneRepository::PruneHistory(TimestampMs before_ms) {
  std::lock_guard lock(mu_);
  auto stmt = db_.Prepare("DELETE FROM heartbeats WHERE received_at_ms < ?1;");
  stmt.Bind(1, before_ms);
  stmt.Run();
  return static_cast<std::size_t>(db_.Changes());
}

std::size_t DroneRepository::DroneCount() {
  std::lock_guard lock(mu_);
  auto stmt = db_.Prepare("SELECT COUNT(*) FROM drones;");
  stmt.Step();
  return static_cast<std::size_t>(stmt.ColumnInt64(0));
}

}  // namespace dronebeat::storage
