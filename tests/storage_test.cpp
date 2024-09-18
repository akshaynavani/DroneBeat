#include <gtest/gtest.h>

#include "storage/drone_repository.hpp"
#include "storage/schema.hpp"

using namespace dronebeat;
using namespace dronebeat::storage;

namespace {

Heartbeat MakeHeartbeat(const std::string& id, std::uint64_t seq, TimestampMs received,
                        float battery = 80.0f) {
  Heartbeat hb;
  hb.drone_id = id;
  hb.sequence = seq;
  hb.sent_at_ms = received - 10;
  hb.received_at_ms = received;
  hb.mode = FlightMode::kMission;
  hb.position = {37.7749, -122.4194, 120.0};
  hb.battery_pct = battery;
  hb.ground_speed_mps = 8.5f;
  hb.firmware_version = "1.4.2";
  return hb;
}

}  // namespace

TEST(Schema, MigratesToCurrentVersion) {
  Database db(":memory:");
  EXPECT_EQ(SchemaVersion(db), 0);
  Migrate(db);
  EXPECT_EQ(SchemaVersion(db), kSchemaVersion);
  Migrate(db);  // idempotent
  EXPECT_EQ(SchemaVersion(db), kSchemaVersion);
}

TEST(DroneRepository, FirstHeartbeatCreatesDrone) {
  DroneRepository repo(":memory:");
  EXPECT_EQ(repo.RecordHeartbeat(MakeHeartbeat("d1", 1, 1'000)), RecordResult::kAccepted);

  auto d = repo.GetDrone("d1");
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->first_seen_ms, 1'000);
  EXPECT_EQ(d->last_seen_ms, 1'000);
  EXPECT_EQ(d->heartbeat_count, 1u);
  EXPECT_EQ(d->mode, FlightMode::kMission);
  EXPECT_DOUBLE_EQ(d->position.latitude, 37.7749);
  EXPECT_EQ(d->firmware_version, "1.4.2");
}

TEST(DroneRepository, SubsequentHeartbeatsUpdateLatestState) {
  DroneRepository repo(":memory:");
  repo.RecordHeartbeat(MakeHeartbeat("d1", 1, 1'000, 90.0f));
  repo.RecordHeartbeat(MakeHeartbeat("d1", 2, 2'000, 85.0f));

  auto d = repo.GetDrone("d1");
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d->first_seen_ms, 1'000);
  EXPECT_EQ(d->last_seen_ms, 2'000);
  EXPECT_EQ(d->last_sequence, 2u);
  EXPECT_EQ(d->heartbeat_count, 2u);
  EXPECT_FLOAT_EQ(d->battery_pct, 85.0f);
}

TEST(DroneRepository, DuplicateSequenceIsIgnored) {
  DroneRepository repo(":memory:");
  repo.RecordHeartbeat(MakeHeartbeat("d1", 5, 1'000));
  EXPECT_EQ(repo.RecordHeartbeat(MakeHeartbeat("d1", 5, 1'500)), RecordResult::kDuplicate);
  EXPECT_EQ(repo.GetDrone("d1")->heartbeat_count, 1u);
  EXPECT_EQ(repo.History("d1", 0, 10).size(), 1u);
}

TEST(DroneRepository, EmptyFirmwareKeepsPreviousValue) {
  DroneRepository repo(":memory:");
  repo.RecordHeartbeat(MakeHeartbeat("d1", 1, 1'000));
  auto hb = MakeHeartbeat("d1", 2, 2'000);
  hb.firmware_version.clear();
  repo.RecordHeartbeat(hb);
  EXPECT_EQ(repo.GetDrone("d1")->firmware_version, "1.4.2");
}

TEST(DroneRepository, ListSeenBeforeFindsStaleDrones) {
  DroneRepository repo(":memory:");
  repo.RecordHeartbeat(MakeHeartbeat("old", 1, 1'000));
  repo.RecordHeartbeat(MakeHeartbeat("fresh", 1, 9'000));

  auto stale = repo.ListSeenBefore(5'000);
  ASSERT_EQ(stale.size(), 1u);
  EXPECT_EQ(stale[0].drone_id, "old");
  EXPECT_EQ(repo.ListDrones().size(), 2u);
  EXPECT_EQ(repo.DroneCount(), 2u);
}

TEST(DroneRepository, HistoryIsNewestFirstAndLimited) {
  DroneRepository repo(":memory:");
  for (std::uint64_t i = 1; i <= 5; ++i) {
    repo.RecordHeartbeat(MakeHeartbeat("d1", i, static_cast<TimestampMs>(i) * 1'000));
  }
  auto h = repo.History("d1", 0, 3);
  ASSERT_EQ(h.size(), 3u);
  EXPECT_EQ(h[0].sequence, 5u);
  EXPECT_EQ(h[2].sequence, 3u);

  auto since = repo.History("d1", 4'000, 100);
  EXPECT_EQ(since.size(), 2u);
}

TEST(DroneRepository, PruneHistoryKeepsDroneState) {
  DroneRepository repo(":memory:");
  for (std::uint64_t i = 1; i <= 4; ++i) {
    repo.RecordHeartbeat(MakeHeartbeat("d1", i, static_cast<TimestampMs>(i) * 1'000));
  }
  EXPECT_EQ(repo.PruneHistory(3'000), 2u);
  EXPECT_EQ(repo.History("d1", 0, 100).size(), 2u);
  EXPECT_TRUE(repo.GetDrone("d1").has_value());
}

TEST(DroneRepository, EventsFilterByDrone) {
  DroneRepository repo(":memory:");
  StatusEvent e;
  e.drone_id = "d1";
  e.sent_at_ms = 100;
  e.received_at_ms = 110;
  e.severity = Severity::kWarning;
  e.code = "LOW_BATTERY";
  e.message = "battery at 18%";
  const auto id1 = repo.RecordEvent(e);
  e.drone_id = "d2";
  e.received_at_ms = 200;
  const auto id2 = repo.RecordEvent(e);
  EXPECT_LT(id1, id2);

  auto all = repo.ListEvents({});
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].drone_id, "d2");  // newest first

  EventQuery q;
  q.drone_id = "d1";
  auto d1 = repo.ListEvents(q);
  ASSERT_EQ(d1.size(), 1u);
  EXPECT_EQ(d1[0].severity, Severity::kWarning);
  EXPECT_EQ(d1[0].code, "LOW_BATTERY");
}

TEST(DroneRepository, UnknownDroneReturnsNullopt) {
  DroneRepository repo(":memory:");
  EXPECT_FALSE(repo.GetDrone("ghost").has_value());
  EXPECT_TRUE(repo.History("ghost", 0, 10).empty());
}
