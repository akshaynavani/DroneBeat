#include <gtest/gtest.h>

#include "service/fleet_monitor.hpp"
#include "sim/drone_model.hpp"
#include "sim/sim_config.hpp"

using namespace dronebeat;
using namespace dronebeat::sim;

TEST(DroneModel, SequenceIncreasesAndHeartbeatsAreValid) {
  DroneModel model("sim-1", 37.0, -122.0, 1234);
  TimestampMs now = 1'000;
  std::uint64_t last_seq = 0;
  // Ten simulated minutes at 1 Hz exercises takeoff, mission, RTH and landing.
  for (int i = 0; i < 600; ++i) {
    now += 1'000;
    const auto hb = model.Step(1'000, now);
    EXPECT_GT(hb.sequence, last_seq);
    last_seq = hb.sequence;
    EXPECT_EQ(FleetMonitor::ValidateHeartbeat(hb), "") << "tick " << i;
  }
}

TEST(DroneModel, DeterministicForSameSeed) {
  DroneModel a("x", 10.0, 10.0, 99);
  DroneModel b("x", 10.0, 10.0, 99);
  for (int i = 0; i < 50; ++i) {
    const auto ha = a.Step(1'000, i * 1'000);
    const auto hb = b.Step(1'000, i * 1'000);
    EXPECT_DOUBLE_EQ(ha.position.latitude, hb.position.latitude);
    EXPECT_FLOAT_EQ(ha.battery_pct, hb.battery_pct);
  }
}

TEST(DroneModel, LowBatteryTriggersReturnHomeAndEvent) {
  DroneModel model("lb", 0.0, 0.0, 7);
  bool saw_low_battery = false;
  for (int i = 0; i < 2'000 && !saw_low_battery; ++i) {
    model.Step(1'000, i * 1'000);
    if (auto e = model.TakePendingEvent(); e && e->code == "LOW_BATTERY") {
      saw_low_battery = true;
      EXPECT_EQ(e->severity, Severity::kWarning);
      // Usually RTH; may already be landing if the drone happened to be right above home.
      EXPECT_TRUE(model.mode() == FlightMode::kReturnToHome ||
                  model.mode() == FlightMode::kLanding);
    }
  }
  EXPECT_TRUE(saw_low_battery);
}

TEST(SimConfig, ParsesArguments) {
  auto r = ParseSimArgs({"--drones", "25", "--stream", "--home", "51.5,-0.12", "--seed", "3"});
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.config.drones, 25);
  EXPECT_TRUE(r.config.stream);
  EXPECT_DOUBLE_EQ(r.config.home_lat, 51.5);
  EXPECT_DOUBLE_EQ(r.config.home_lon, -0.12);
  EXPECT_EQ(r.config.seed, 3u);
}

TEST(SimConfig, RejectsBadValues) {
  EXPECT_FALSE(ParseSimArgs({"--drones", "0"}).ok);
  EXPECT_FALSE(ParseSimArgs({"--dropout-prob", "1.5"}).ok);
  EXPECT_FALSE(ParseSimArgs({"--silence-min-ms", "5000", "--silence-max-ms", "10"}).ok);
  EXPECT_FALSE(ParseSimArgs({"--home", "51.5"}).ok);
  EXPECT_FALSE(ParseSimArgs({"--bogus"}).ok);
}
