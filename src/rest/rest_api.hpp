#pragma once

#include <httplib.h>

#include <atomic>
#include <string>
#include <thread>

#include "service/fleet_monitor.hpp"

namespace dronebeat::rest {

// Read-only REST/JSON API over the fleet state.
//
//   GET /healthz
//   GET /api/v1/fleet                       summary counts + policy
//   GET /api/v1/drones[?liveness=STALE]     all drones (optionally filtered)
//   GET /api/v1/drones/stale                STALE + LOST drones, oldest contact first
//   GET /api/v1/drones/{id}                 one drone
//   GET /api/v1/drones/{id}/history[?since_ms=&limit=]
//   GET /api/v1/events[?drone_id=&since_ms=&limit=]
class RestApi {
 public:
  explicit RestApi(FleetMonitor& monitor);
  ~RestApi();

  RestApi(const RestApi&) = delete;
  RestApi& operator=(const RestApi&) = delete;

  // Installs all routes on the given server (exposed separately for tests).
  void RegisterRoutes(httplib::Server& server);

  // Binds and serves on a background thread. Returns false if binding fails.
  bool Start(const std::string& host, int port);
  // Binds to an OS-chosen port; returns the port or -1.
  int StartOnAnyPort(const std::string& host);
  void Stop();

 private:
  void HandleHealth(const httplib::Request& req, httplib::Response& res);
  void HandleFleet(const httplib::Request& req, httplib::Response& res);
  void HandleDrones(const httplib::Request& req, httplib::Response& res);
  void HandleStale(const httplib::Request& req, httplib::Response& res);
  void HandleDrone(const httplib::Request& req, httplib::Response& res);
  void HandleHistory(const httplib::Request& req, httplib::Response& res);
  void HandleEvents(const httplib::Request& req, httplib::Response& res);

  FleetMonitor& monitor_;
  httplib::Server server_;
  std::thread thread_;
  std::atomic<bool> started_{false};
};

}  // namespace dronebeat::rest
