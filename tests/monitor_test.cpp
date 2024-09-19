#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "core/clock.hpp"
#include "service/fleet_monitor.hpp"
#include "service/staleness_sweeper.hpp"

using namespace dronebeat;

namespace {

class MonitorTest : public ::testing::Test {
 protected:
  MonitorTest()
      : clock_(std::make_shared<FakeClock>(1'000'000)),
        repo_(std::make_shared<storage::DroneRepository>(":memory:")),
        monitor_(repo_, clock_, LivenessPolicy{5'000, 30'000, 20.0f}) {}

  Heartbeat Beat(const std::string& id, std::uint64_t seq, float battery = 75.0f) {
    Heartbeat hb;
    hb.drone_id = id;
    hb.sequence = seq;
    hb.sent_at_ms = clock_->NowMs();
    hb.mode = FlightMode::kHover;
    hb.position = {47.6, -122.3, 50.0};
    hb.battery_pct = battery;
    return hb;
  }

  std::shared_ptr<FakeClock> clock_;
  std::shared_ptr<storage::DroneRepository> repo_;
  FleetMonitor monitor_;
};

}  // namespace

TEST_F(MonitorTest, StampsServerReceiveTime) {
  auto hb = Beat("d1", 1);
  hb.sent_at_ms = 42;  // wildly skewed drone clock
  ASSERT_EQ(monitor_.IngestHeartbeat(hb).status, IngestOutcome::kAccepted);
  EXPECT_EQ(monitor_.GetDrone("d1")->last_seen_ms, clock_->NowMs());
}

TEST_F(MonitorTest, RejectsInvalidHeartbeats) {
  auto bad_id = Beat("drone with spaces", 1);
  EXPECT_EQ(monitor_.IngestHeartbeat(bad_id).status, IngestOutcome::kInvalid);

  auto bad_lat = Beat("d1", 1);
  bad_lat.position.latitude = 123.0;
  EXPECT_EQ(monitor_.IngestHeartbeat(bad_lat).status, IngestOutcome::kInvalid);

  auto bad_battery = Beat("d1", 1, 140.0f);
  EXPECT_EQ(monitor_.IngestHeartbeat(bad_battery).status, IngestOutcome::kInvalid);

  auto nan_alt = Beat("d1", 1);
  nan_alt.position.altitude_m = std::nan("");
  EXPECT_EQ(monitor_.IngestHeartbeat(nan_alt).status, IngestOutcome::kInvalid);

  EXPECT_EQ(repo_->DroneCount(), 0u);
}

TEST_F(MonitorTest, ReportsDuplicates) {
  monitor_.IngestHeartbeat(Beat("d1", 7));
  EXPECT_EQ(monitor_.IngestHeartbeat(Beat("d1", 7)).status, IngestOutcome::kDuplicate);
}

TEST_F(MonitorTest, LivenessFollowsClock) {
  monitor_.IngestHeartbeat(Beat("d1", 1));
  EXPECT_EQ(monitor_.GetDrone("d1")->liveness, Liveness::kAlive);
  clock_->Advance(6'000);
  EXPECT_EQ(monitor_.GetDrone("d1")->liveness, Liveness::kStale);
  clock_->Advance(30'000);
  EXPECT_EQ(monitor_.GetDrone("d1")->liveness, Liveness::kLost);
  monitor_.IngestHeartbeat(Beat("d1", 2));
  EXPECT_EQ(monitor_.GetDrone("d1")->liveness, Liveness::kAlive);
}

TEST_F(MonitorTest, SummaryAndUnhealthyList) {
  monitor_.IngestHeartbeat(Beat("lost", 1));
  clock_->Advance(25'000);
  monitor_.IngestHeartbeat(Beat("stale", 1, 10.0f));
  clock_->Advance(8'000);
  monitor_.IngestHeartbeat(Beat("alive", 1));

  const auto s = monitor_.Summary();
  EXPECT_EQ(s.total, 3u);
  EXPECT_EQ(s.alive, 1u);
  EXPECT_EQ(s.stale, 1u);
  EXPECT_EQ(s.lost, 1u);
  EXPECT_EQ(s.low_battery, 1u);

  const auto unhealthy = monitor_.ListUnhealthy();
  ASSERT_EQ(unhealthy.size(), 2u);
  EXPECT_EQ(unhealthy[0].drone_id, "lost");  // oldest contact first
  EXPECT_EQ(unhealthy[0].liveness, Liveness::kLost);
  EXPECT_EQ(unhealthy[1].liveness, Liveness::kStale);

  EXPECT_EQ(monitor_.ListDrones(Liveness::kAlive).size(), 1u);
}

TEST_F(MonitorTest, StatusEventsDefaultToInfoAndValidateCode) {
  StatusEvent e;
  e.drone_id = "d1";
  e.code = "";
  EXPECT_FALSE(monitor_.IngestStatus(e).ok);

  e.code = "GPS_LOSS";
  const auto r = monitor_.IngestStatus(e);
  ASSERT_TRUE(r.ok);
  auto events = monitor_.Events({});
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].severity, Severity::kInfo);
  EXPECT_EQ(events[0].id, r.event_id);
}

TEST_F(MonitorTest, SweeperRecordsTransitionsOnce) {
  StalenessSweeper sweeper(monitor_, std::chrono::milliseconds(1'000), 3'600'000);
  monitor_.IngestHeartbeat(Beat("d1", 1));

  EXPECT_TRUE(sweeper.RunOnce().empty());

  clock_->Advance(6'000);
  auto t = sweeper.RunOnce();
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0].from, Liveness::kAlive);
  EXPECT_EQ(t[0].to, Liveness::kStale);
  EXPECT_TRUE(sweeper.RunOnce().empty());  // no duplicate events

  clock_->Advance(30'000);
  t = sweeper.RunOnce();
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0].to, Liveness::kLost);

  monitor_.IngestHeartbeat(Beat("d1", 2));
  t = sweeper.RunOnce();
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0].to, Liveness::kAlive);

  const auto events = monitor_.Events({});
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].code, "LINK_RECOVERED");
  EXPECT_EQ(events[1].code, "LINK_LOST");
  EXPECT_EQ(events[1].severity, Severity::kCritical);
  EXPECT_EQ(events[2].code, "LINK_STALE");
}

TEST_F(MonitorTest, SweeperPrunesOldHistory) {
  StalenessSweeper sweeper(monitor_, std::chrono::milliseconds(1'000), 10'000);
  monitor_.IngestHeartbeat(Beat("d1", 1));
  clock_->Advance(20'000);
  monitor_.IngestHeartbeat(Beat("d1", 2));
  sweeper.RunOnce();
  EXPECT_EQ(monitor_.History("d1", 0, 100).size(), 1u);
}
