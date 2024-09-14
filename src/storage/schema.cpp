#include "storage/schema.hpp"

#include <array>
#include <string>

namespace dronebeat::storage {
namespace {

// Each entry migrates from version (index) to version (index + 1).
constexpr std::array<const char*, kSchemaVersion> kMigrations = {
    R"sql(
    CREATE TABLE IF NOT EXISTS drones (
      drone_id          TEXT PRIMARY KEY,
      first_seen_ms     INTEGER NOT NULL,
      last_seen_ms      INTEGER NOT NULL,
      last_sequence     INTEGER NOT NULL DEFAULT 0,
      heartbeat_count   INTEGER NOT NULL DEFAULT 0,
      mode              TEXT    NOT NULL DEFAULT 'UNSPECIFIED',
      latitude          REAL    NOT NULL DEFAULT 0,
      longitude         REAL    NOT NULL DEFAULT 0,
      altitude_m        REAL    NOT NULL DEFAULT 0,
      battery_pct       REAL    NOT NULL DEFAULT 0,
      ground_speed_mps  REAL    NOT NULL DEFAULT 0,
      firmware_version  TEXT    NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_drones_last_seen ON drones(last_seen_ms);

    CREATE TABLE IF NOT EXISTS heartbeats (
      id                INTEGER PRIMARY KEY AUTOINCREMENT,
      drone_id          TEXT    NOT NULL REFERENCES drones(drone_id) ON DELETE CASCADE,
      sequence          INTEGER NOT NULL,
      sent_at_ms        INTEGER NOT NULL,
      received_at_ms    INTEGER NOT NULL,
      mode              TEXT    NOT NULL,
      latitude          REAL    NOT NULL,
      longitude         REAL    NOT NULL,
      altitude_m        REAL    NOT NULL,
      battery_pct       REAL    NOT NULL,
      ground_speed_mps  REAL    NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_heartbeats_drone_time
      ON heartbeats(drone_id, received_at_ms DESC);
    CREATE INDEX IF NOT EXISTS idx_heartbeats_time ON heartbeats(received_at_ms);

    CREATE TABLE IF NOT EXISTS events (
      id              INTEGER PRIMARY KEY AUTOINCREMENT,
      drone_id        TEXT    NOT NULL,
      sent_at_ms      INTEGER NOT NULL,
      received_at_ms  INTEGER NOT NULL,
      severity        TEXT    NOT NULL,
      code            TEXT    NOT NULL,
      message         TEXT    NOT NULL DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_events_drone_time ON events(drone_id, received_at_ms DESC);
    CREATE INDEX IF NOT EXISTS idx_events_time ON events(received_at_ms DESC);
    )sql",
};

}  // namespace

int SchemaVersion(Database& db) {
  auto stmt = db.Prepare("PRAGMA user_version;");
  return stmt.Step() ? static_cast<int>(stmt.ColumnInt64(0)) : 0;
}

void Migrate(Database& db) {
  int version = SchemaVersion(db);
  if (version > kSchemaVersion) {
    throw SqliteError("database schema version " + std::to_string(version) +
                          " is newer than supported " + std::to_string(kSchemaVersion),
                      SQLITE_MISMATCH);
  }
  while (version < kSchemaVersion) {
    Transaction tx(db);
    db.Exec(kMigrations[static_cast<std::size_t>(version)]);
    ++version;
    db.Exec("PRAGMA user_version = " + std::to_string(version) + ";");
    tx.Commit();
  }
}

}  // namespace dronebeat::storage
