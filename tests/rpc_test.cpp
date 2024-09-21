#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <memory>

#include "core/clock.hpp"
#include "rpc/proto_convert.hpp"
#include "rpc/telemetry_service.hpp"

using namespace dronebeat;
namespace pb = dronebeat::v1;

namespace {

class RpcTest : public ::testing::Test {
 protected:
  RpcTest()
      : clock_(std::make_shared<FakeClock>(2'000'000)),
        repo_(std::make_shared<storage::DroneRepository>(":memory:")),
        monitor_(repo_, clock_, LivenessPolicy{}),
        service_(monitor_, 1'000) {}

  pb::Heartbeat MakeBeat(const std::string& id, std::uint64_t seq) {
    pb::Heartbeat msg;
    msg.set_drone_id(id);
    msg.set_sequence(seq);
    msg.set_sent_at_ms(clock_->NowMs());
    msg.set_mode(pb::FLIGHT_MODE_RETURN_TO_HOME);
    msg.mutable_position()->set_latitude(51.5);
    msg.mutable_position()->set_longitude(-0.12);
    msg.mutable_position()->set_altitude_m(80.0);
    msg.set_battery_pct(33.0f);
    msg.set_firmware_version("2.0.0");
    return msg;
  }

  std::shared_ptr<FakeClock> clock_;
  std::shared_ptr<storage::DroneRepository> repo_;
  FleetMonitor monitor_;
  rpc::TelemetryServiceImpl service_;
  ::grpc::ServerContext ctx_;
};

}  // namespace

TEST(ProtoConvert, RoundTripsHeartbeat) {
  Heartbeat hb;
  hb.drone_id = "x";
  hb.sequence = 9;
  hb.mode = FlightMode::kLanding;
  hb.position = {1.5, 2.5, 3.5};
  hb.battery_pct = 50.0f;
  pb::Heartbeat msg;
  rpc::ToProto(hb, &msg);
  const auto back = rpc::FromProto(msg);
  EXPECT_EQ(back.drone_id, "x");
  EXPECT_EQ(back.sequence, 9u);
  EXPECT_EQ(back.mode, FlightMode::kLanding);
  EXPECT_DOUBLE_EQ(back.position.altitude_m, 3.5);
}

TEST_F(RpcTest, SendHeartbeatAcceptsAndStores) {
  pb::HeartbeatAck ack;
  const auto msg = MakeBeat("uav-1", 1);
  const auto status = service_.SendHeartbeat(&ctx_, &msg, &ack);
  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_TRUE(ack.accepted());
  EXPECT_EQ(ack.server_time_ms(), clock_->NowMs());
  EXPECT_EQ(ack.next_interval_ms(), 1'000u);

  auto drone = monitor_.GetDrone("uav-1");
  ASSERT_TRUE(drone.has_value());
  EXPECT_EQ(drone->mode, FlightMode::kReturnToHome);
  EXPECT_EQ(drone->firmware_version, "2.0.0");
}

TEST_F(RpcTest, DuplicateHeartbeatIsAcknowledgedButNotAccepted) {
  pb::HeartbeatAck ack;
  const auto msg = MakeBeat("uav-1", 4);
  ASSERT_TRUE(service_.SendHeartbeat(&ctx_, &msg, &ack).ok());
  ASSERT_TRUE(service_.SendHeartbeat(&ctx_, &msg, &ack).ok());
  EXPECT_FALSE(ack.accepted());
  EXPECT_EQ(service_.heartbeats_received(), 2u);
}

TEST_F(RpcTest, InvalidHeartbeatReturnsInvalidArgument) {
  pb::HeartbeatAck ack;
  auto msg = MakeBeat("", 1);
  const auto status = service_.SendHeartbeat(&ctx_, &msg, &ack);
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(RpcTest, ReportStatusStoresEvent) {
  pb::StatusUpdate update;
  update.set_drone_id("uav-2");
  update.set_severity(pb::SEVERITY_WARNING);
  update.set_code("WIND_GUST");
  update.set_message("gust 14 m/s");
  pb::StatusAck ack;
  ASSERT_TRUE(service_.ReportStatus(&ctx_, &update, &ack).ok());
  EXPECT_TRUE(ack.accepted());
  EXPECT_GT(ack.event_id(), 0);

  update.set_code("");
  EXPECT_EQ(service_.ReportStatus(&ctx_, &update, &ack).error_code(),
            ::grpc::StatusCode::INVALID_ARGUMENT);
}
