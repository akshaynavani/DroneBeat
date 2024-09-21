// drone_sim: spawns a fleet of mock drones that send heartbeats to dronebeat_server.
//
// Each drone runs on its own thread, advances a DroneModel every interval and
// reports over gRPC. Drones randomly drop their link for a while (to exercise
// STALE/LOST detection) and a configurable fraction "crash" and never return.

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "dronebeat.grpc.pb.h"
#include "rpc/proto_convert.hpp"
#include "sim/drone_model.hpp"
#include "sim/sim_config.hpp"

namespace {

namespace pb = dronebeat::v1;
using Clock = std::chrono::system_clock;

std::atomic<bool> g_stop{false};
std::mutex g_log_mu;

void HandleSignal(int /*signum*/) { g_stop.store(true); }

template <typename... Args>
void Log(Args&&... args) {
  std::ostringstream out;
  (out << ... << std::forward<Args>(args));
  std::lock_guard lock(g_log_mu);
  std::cout << out.str() << std::endl;
}

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
      .count();
}

struct Stats {
  std::atomic<std::uint64_t> sent{0};
  std::atomic<std::uint64_t> failed{0};
  std::atomic<std::uint64_t> events{0};
};

// Sleeps in small slices so Ctrl+C is honoured promptly.
void InterruptibleSleep(std::int64_t ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!g_stop.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min<std::int64_t>(ms, 100)));
  }
}

class DroneRunner {
 public:
  DroneRunner(std::shared_ptr<grpc::Channel> channel, const dronebeat::sim::SimConfig& cfg,
              std::string id, std::uint32_t seed, bool will_crash, Stats& stats)
      : stub_(pb::DroneTelemetry::NewStub(channel)),
        cfg_(cfg),
        model_(std::move(id), cfg.home_lat, cfg.home_lon, seed),
        rng_(seed ^ 0x9e3779b9u),
        will_crash_(will_crash),
        stats_(stats) {}

  void Run(std::int64_t deadline_ms) {
    // Crashing drones die at a random point in the first few minutes.
    const std::int64_t crash_at =
        will_crash_ ? NowMs() + std::uniform_int_distribution<std::int64_t>(15'000, 180'000)(rng_)
                    : -1;
    std::bernoulli_distribution drop(cfg_.dropout_prob);
    std::uniform_int_distribution<std::int64_t> silence(cfg_.silence_min_ms, cfg_.silence_max_ms);

    while (!g_stop.load() && (deadline_ms <= 0 || NowMs() < deadline_ms)) {
      if (crash_at > 0 && NowMs() >= crash_at) {
        CloseStream();
        Log("[", model_.id(), "] crashed - going silent permanently");
        return;
      }
      if (drop(rng_)) {
        const auto ms = silence(rng_);
        CloseStream();
        Log("[", model_.id(), "] link drop for ", ms, " ms");
        // Model keeps flying while the radio is down; sequence numbers still advance.
        const auto until = NowMs() + ms;
        while (!g_stop.load() && NowMs() < until) {
          model_.Step(cfg_.interval_ms, NowMs());
          InterruptibleSleep(cfg_.interval_ms);
        }
        continue;
      }

      const auto hb = model_.Step(cfg_.interval_ms, NowMs());
      pb::Heartbeat msg;
      dronebeat::rpc::ToProto(hb, &msg);
      if (cfg_.stream ? SendStreaming(msg) : SendUnary(msg)) {
        stats_.sent.fetch_add(1);
      } else {
        stats_.failed.fetch_add(1);
      }
      if (auto event = model_.TakePendingEvent()) SendEvent(*event);
      InterruptibleSleep(cfg_.interval_ms);
    }
    CloseStream();
  }

 private:
  bool SendUnary(const pb::Heartbeat& msg) {
    grpc::ClientContext ctx;
    ctx.set_deadline(Clock::now() + std::chrono::seconds(2));
    pb::HeartbeatAck ack;
    const auto status = stub_->SendHeartbeat(&ctx, msg, &ack);
    if (!status.ok()) {
      Log("[", model_.id(), "] heartbeat failed: ", status.error_message());
      return false;
    }
    return true;
  }

  bool SendStreaming(const pb::Heartbeat& msg) {
    if (!writer_) {
      stream_ctx_ = std::make_unique<grpc::ClientContext>();
      stream_summary_ = std::make_unique<pb::HeartbeatStreamSummary>();
      writer_ = stub_->StreamHeartbeats(stream_ctx_.get(), stream_summary_.get());
    }
    if (!writer_->Write(msg)) {
      Log("[", model_.id(), "] stream write failed; reconnecting next tick");
      CloseStream();
      return false;
    }
    return true;
  }

  void CloseStream() {
    if (!writer_) return;
    writer_->WritesDone();
    const auto status = writer_->Finish();
    if (status.ok()) {
      Log("[", model_.id(), "] stream closed: server received ", stream_summary_->received(),
          ", rejected ", stream_summary_->rejected());
    }
    writer_.reset();
    stream_ctx_.reset();
    stream_summary_.reset();
  }

  void SendEvent(const dronebeat::StatusEvent& event) {
    pb::StatusUpdate update;
    update.set_drone_id(event.drone_id);
    update.set_sent_at_ms(event.sent_at_ms);
    update.set_severity(dronebeat::rpc::ToProto(event.severity));
    update.set_code(event.code);
    update.set_message(event.message);
    grpc::ClientContext ctx;
    ctx.set_deadline(Clock::now() + std::chrono::seconds(2));
    pb::StatusAck ack;
    if (stub_->ReportStatus(&ctx, update, &ack).ok()) {
      stats_.events.fetch_add(1);
      Log("[", model_.id(), "] reported ", event.code, ": ", event.message);
    }
  }

  std::unique_ptr<pb::DroneTelemetry::Stub> stub_;
  const dronebeat::sim::SimConfig& cfg_;
  dronebeat::sim::DroneModel model_;
  std::mt19937 rng_;
  bool will_crash_;
  Stats& stats_;

  std::unique_ptr<grpc::ClientContext> stream_ctx_;
  std::unique_ptr<pb::HeartbeatStreamSummary> stream_summary_;
  std::unique_ptr<grpc::ClientWriter<pb::Heartbeat>> writer_;
};

}  // namespace

int main(int argc, char** argv) {
  using namespace dronebeat::sim;

  const auto parsed = ParseSimArgs(std::vector<std::string>(argv + 1, argv + argc));
  if (parsed.show_help) {
    std::cout << SimUsage(argv[0]);
    return 0;
  }
  if (!parsed.ok) {
    std::cerr << "error: " << parsed.error << "\n\n" << SimUsage(argv[0]);
    return 2;
  }
  const SimConfig& cfg = parsed.config;

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  const std::uint32_t base_seed = cfg.seed != 0 ? cfg.seed : std::random_device{}();
  std::mt19937 rng(base_seed);
  std::bernoulli_distribution crashes(cfg.crash_fraction);

  // One shared channel multiplexes all drones over HTTP/2.
  auto channel = grpc::CreateChannel(cfg.target, grpc::InsecureChannelCredentials());
  if (!channel->WaitForConnected(Clock::now() + std::chrono::seconds(10))) {
    std::cerr << "could not connect to " << cfg.target << " within 10s\n";
    return 1;
  }

  Log("drone_sim: ", cfg.drones, " drones -> ", cfg.target, " every ", cfg.interval_ms, " ms (",
      cfg.stream ? "streaming" : "unary", ", seed ", base_seed, ")");

  Stats stats;
  const std::int64_t deadline = cfg.duration_s > 0 ? NowMs() + cfg.duration_s * 1000 : 0;
  std::vector<std::unique_ptr<DroneRunner>> runners;
  std::vector<std::thread> threads;
  for (int i = 0; i < cfg.drones; ++i) {
    std::ostringstream id;
    id << cfg.id_prefix << "-" << std::setw(3) << std::setfill('0') << (i + 1);
    runners.push_back(std::make_unique<DroneRunner>(channel, cfg, id.str(),
                                                    static_cast<std::uint32_t>(rng()),
                                                    crashes(rng), stats));
  }
  for (auto& r : runners) {
    // Stagger start-up so heartbeats don't arrive in lock-step.
    threads.emplace_back([&r, deadline] { r->Run(deadline); });
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg.interval_ms / cfg.drones + 1));
  }
  for (auto& t : threads) t.join();

  Log("drone_sim done: sent ", stats.sent.load(), ", failed ", stats.failed.load(),
      ", status events ", stats.events.load());
  return 0;
}
