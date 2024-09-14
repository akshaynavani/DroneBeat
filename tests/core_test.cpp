#include <gtest/gtest.h>

#include "config/config.hpp"
#include "core/clock.hpp"
#include "core/liveness.hpp"
#include "core/types.hpp"

using namespace dronebeat;

TEST(Liveness, ClassifiesByAge) {
  LivenessPolicy policy{5'000, 30'000, 20.0f};
  const TimestampMs now = 100'000;
  EXPECT_EQ(Classify(policy, now, now), Liveness::kAlive);
  EXPECT_EQ(Classify(policy, now - 5'000, now), Liveness::kAlive);
  EXPECT_EQ(Classify(policy, now - 5'001, now), Liveness::kStale);
  EXPECT_EQ(Classify(policy, now - 30'000, now), Liveness::kStale);
  EXPECT_EQ(Classify(policy, now - 30'001, now), Liveness::kLost);
}

TEST(Liveness, FutureTimestampIsAlive) {
  LivenessPolicy policy;
  EXPECT_EQ(Classify(policy, 2'000, 1'000), Liveness::kAlive);
}

TEST(Liveness, PolicyValidation) {
  EXPECT_TRUE(LivenessPolicy{}.IsValid());
  EXPECT_FALSE((LivenessPolicy{10, 5, 20.0f}.IsValid()));
  EXPECT_FALSE((LivenessPolicy{0, 5, 20.0f}.IsValid()));
}

TEST(Types, EnumStringRoundTrip) {
  for (auto mode : {FlightMode::kIdle, FlightMode::kMission, FlightMode::kEmergency}) {
    EXPECT_EQ(FlightModeFromString(ToString(mode)), mode);
  }
  EXPECT_EQ(SeverityFromString("CRITICAL"), Severity::kCritical);
  EXPECT_EQ(LivenessFromString("STALE"), Liveness::kStale);
  EXPECT_FALSE(LivenessFromString("bogus").has_value());
}

TEST(FakeClock, Advances) {
  FakeClock clock(1'000);
  clock.Advance(500);
  EXPECT_EQ(clock.NowMs(), 1'500);
  clock.Set(42);
  EXPECT_EQ(clock.NowMs(), 42);
}

TEST(Config, ParsesFlags) {
  auto r = LoadServerConfig({"--rest-port", "9090", "--db=:memory:", "--stale-after-ms", "2000"});
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.config.rest_port, 9090);
  EXPECT_EQ(r.config.db_path, ":memory:");
  EXPECT_EQ(r.config.liveness.stale_after_ms, 2000);
}

TEST(Config, RejectsInvalidThresholds) {
  auto r = LoadServerConfig({"--stale-after-ms", "60000", "--lost-after-ms", "1000"});
  EXPECT_FALSE(r.ok);
}

TEST(Config, RejectsUnknownFlag) {
  auto r = LoadServerConfig({"--nope", "1"});
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("unknown option"), std::string::npos);
}

TEST(Config, RejectsBadInteger) {
  auto r = LoadServerConfig({"--rest-port", "80x"});
  EXPECT_FALSE(r.ok);
}

TEST(Config, HelpFlag) {
  auto r = LoadServerConfig({"--help"});
  EXPECT_TRUE(r.show_help);
}
