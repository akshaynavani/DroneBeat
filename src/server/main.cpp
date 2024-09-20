// dronebeat_server: gRPC ingest + REST query service for drone fleet heartbeats.

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "config/config.hpp"
#include "core/clock.hpp"
#include "rest/rest_api.hpp"
#include "rpc/telemetry_service.hpp"
#include "service/fleet_monitor.hpp"
#include "service/staleness_sweeper.hpp"
#include "storage/drone_repository.hpp"

namespace {

std::mutex g_shutdown_mu;
std::condition_variable g_shutdown_cv;
std::atomic<bool> g_shutdown{false};

void HandleSignal(int /*signum*/) {
  // Only async-signal-safe work here; the main thread polls the flag.
  g_shutdown.store(true);
}

}  // namespace

int main(int argc, char** argv) {
  using namespace dronebeat;

  const std::vector<std::string> args(argv + 1, argv + argc);
  const auto loaded = LoadServerConfig(args);
  if (loaded.show_help) {
    std::cout << ServerUsage(argv[0]);
    return 0;
  }
  if (!loaded.ok) {
    std::cerr << "config error: " << loaded.error << "\n\n" << ServerUsage(argv[0]);
    return 2;
  }
  const ServerConfig& cfg = loaded.config;

  std::shared_ptr<storage::DroneRepository> repo;
  try {
    repo = std::make_shared<storage::DroneRepository>(cfg.db_path);
  } catch (const std::exception& e) {
    std::cerr << "failed to open database: " << e.what() << "\n";
    return 1;
  }

  auto clock = std::make_shared<SystemClock>();
  FleetMonitor monitor(repo, clock, cfg.liveness);

  StalenessSweeper sweeper(monitor, std::chrono::milliseconds(cfg.sweep_interval_ms),
                           cfg.history_retention_ms);
  sweeper.Start();

  rpc::TelemetryServiceImpl telemetry(monitor, cfg.heartbeat_interval_hint_ms);
  ::grpc::EnableDefaultHealthCheckService(true);
  ::grpc::ServerBuilder builder;
  int bound_port = 0;
  builder.AddListeningPort(cfg.grpc_address, ::grpc::InsecureServerCredentials(), &bound_port);
  builder.RegisterService(&telemetry);
  std::unique_ptr<::grpc::Server> grpc_server = builder.BuildAndStart();
  if (!grpc_server || bound_port == 0) {
    std::cerr << "failed to start gRPC server on " << cfg.grpc_address << "\n";
    sweeper.Stop();
    return 1;
  }

  rest::RestApi rest_api(monitor);
  if (!rest_api.Start(cfg.rest_host, cfg.rest_port)) {
    std::cerr << "failed to bind REST server on " << cfg.rest_host << ":" << cfg.rest_port << "\n";
    grpc_server->Shutdown();
    sweeper.Stop();
    return 1;
  }

  std::cout << "DroneBeat server started\n"
            << "  gRPC  : " << cfg.grpc_address << "\n"
            << "  REST  : http://" << cfg.rest_host << ":" << cfg.rest_port << "\n"
            << "  db    : " << cfg.db_path << "\n"
            << "  stale : " << cfg.liveness.stale_after_ms << " ms, lost : "
            << cfg.liveness.lost_after_ms << " ms\n";

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  {
    std::unique_lock lock(g_shutdown_mu);
    while (!g_shutdown.load()) {
      g_shutdown_cv.wait_for(lock, std::chrono::milliseconds(200));
    }
  }

  std::cout << "shutting down...\n";
  rest_api.Stop();
  grpc_server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(3));
  sweeper.Stop();
  std::cout << "received " << telemetry.heartbeats_received() << " heartbeats; bye\n";
  return 0;
}
