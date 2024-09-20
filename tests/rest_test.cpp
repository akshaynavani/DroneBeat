#include <gtest/gtest.h>
#include <httplib.h>

#include <memory>
#include <nlohmann/json.hpp>

#include "core/clock.hpp"
#include "rest/rest_api.hpp"
#include "service/fleet_monitor.hpp"

using namespace dronebeat;
using nlohmann::json;

namespace {

class RestApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<FakeClock>(5'000'000);
    repo_ = std::make_shared<storage::DroneRepository>(":memory:");
    monitor_ = std::make_unique<FleetMonitor>(repo_, clock_, LivenessPolicy{5'000, 30'000, 20.0f});
    api_ = std::make_unique<rest::RestApi>(*monitor_);
    port_ = api_->StartOnAnyPort("127.0.0.1");
    ASSERT_GT(port_, 0);
    client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
    client_->set_connection_timeout(2);
  }

  void TearDown() override { api_->Stop(); }

  void Beat(const std::string& id, std::uint64_t seq, float battery = 60.0f) {
    Heartbeat hb;
    hb.drone_id = id;
    hb.sequence = seq;
    hb.mode = FlightMode::kMission;
    hb.position = {1.0, 2.0, 3.0};
    hb.battery_pct = battery;
    ASSERT_EQ(monitor_->IngestHeartbeat(hb).status, IngestOutcome::kAccepted);
  }

  json GetJson(const std::string& path, int expected_status = 200) {
    auto res = client_->Get(path);
    EXPECT_TRUE(res) << "request failed: " << path;
    if (!res) return {};
    EXPECT_EQ(res->status, expected_status) << path << " -> " << res->body;
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/json");
    return json::parse(res->body);
  }

  std::shared_ptr<FakeClock> clock_;
  std::shared_ptr<storage::DroneRepository> repo_;
  std::unique_ptr<FleetMonitor> monitor_;
  std::unique_ptr<rest::RestApi> api_;
  std::unique_ptr<httplib::Client> client_;
  int port_ = 0;
};

}  // namespace

TEST_F(RestApiTest, Health) {
  auto body = GetJson("/healthz");
  EXPECT_EQ(body["status"], "ok");
}

TEST_F(RestApiTest, FleetSummary) {
  Beat("a", 1);
  clock_->Advance(10'000);
  Beat("b", 1, 5.0f);
  auto body = GetJson("/api/v1/fleet");
  EXPECT_EQ(body["summary"]["total"], 2);
  EXPECT_EQ(body["summary"]["alive"], 1);
  EXPECT_EQ(body["summary"]["stale"], 1);
  EXPECT_EQ(body["summary"]["low_battery"], 1);
  EXPECT_EQ(body["policy"]["stale_after_ms"], 5'000);
}

TEST_F(RestApiTest, ListAndFilterDrones) {
  Beat("a", 1);
  clock_->Advance(10'000);
  Beat("b", 1);

  auto all = GetJson("/api/v1/drones");
  EXPECT_EQ(all["count"], 2);

  auto stale = GetJson("/api/v1/drones?liveness=STALE");
  ASSERT_EQ(stale["count"], 1);
  EXPECT_EQ(stale["drones"][0]["drone_id"], "a");

  auto bad = GetJson("/api/v1/drones?liveness=sleepy", 400);
  EXPECT_TRUE(bad.contains("error"));
}

TEST_F(RestApiTest, StaleEndpointReportsSilence) {
  Beat("quiet", 1);
  clock_->Advance(40'000);
  Beat("chatty", 1);

  auto body = GetJson("/api/v1/drones/stale");
  ASSERT_EQ(body["count"], 1);
  EXPECT_EQ(body["drones"][0]["drone_id"], "quiet");
  EXPECT_EQ(body["drones"][0]["liveness"], "LOST");
  EXPECT_EQ(body["drones"][0]["silent_for_ms"], 40'000);
}

TEST_F(RestApiTest, GetDroneAndNotFound) {
  Beat("d-7", 3);
  auto body = GetJson("/api/v1/drones/d-7");
  EXPECT_EQ(body["drone_id"], "d-7");
  EXPECT_EQ(body["mode"], "MISSION");
  EXPECT_EQ(body["liveness"], "ALIVE");
  EXPECT_DOUBLE_EQ(body["position"]["altitude_m"].get<double>(), 3.0);

  auto missing = GetJson("/api/v1/drones/nope", 404);
  EXPECT_TRUE(missing.contains("error"));
}

TEST_F(RestApiTest, HistoryRespectsLimitAndValidation) {
  for (std::uint64_t i = 1; i <= 5; ++i) {
    Beat("h1", i);
    clock_->Advance(1'000);
  }
  auto body = GetJson("/api/v1/drones/h1/history?limit=2");
  ASSERT_EQ(body["count"], 2);
  EXPECT_EQ(body["heartbeats"][0]["sequence"], 5);

  GetJson("/api/v1/drones/h1/history?limit=abc", 400);
  GetJson("/api/v1/drones/h1/history?limit=0", 400);
  GetJson("/api/v1/drones/ghost/history", 404);
}

TEST_F(RestApiTest, EventsEndpoint) {
  StatusEvent e;
  e.drone_id = "e1";
  e.code = "GPS_LOSS";
  e.severity = Severity::kCritical;
  ASSERT_TRUE(monitor_->IngestStatus(e).ok);
  e.drone_id = "e2";
  e.code = "LOW_BATTERY";
  ASSERT_TRUE(monitor_->IngestStatus(e).ok);

  auto all = GetJson("/api/v1/events");
  EXPECT_EQ(all["count"], 2);

  auto filtered = GetJson("/api/v1/events?drone_id=e1");
  ASSERT_EQ(filtered["count"], 1);
  EXPECT_EQ(filtered["events"][0]["code"], "GPS_LOSS");
  EXPECT_EQ(filtered["events"][0]["severity"], "CRITICAL");
}

TEST_F(RestApiTest, UnknownRouteReturnsJson404) {
  auto body = GetJson("/api/v1/unknown", 404);
  EXPECT_EQ(body["error"], "not found");
}
