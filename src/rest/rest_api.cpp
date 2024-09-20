#include "rest/rest_api.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>

#include "rest/json.hpp"

namespace dronebeat::rest {
namespace {

using nlohmann::json;

constexpr std::size_t kDefaultLimit = 100;
constexpr std::size_t kMaxLimit = 1'000;
constexpr const char* kIdPattern = "([A-Za-z0-9._-]+)";

// Thrown by parameter helpers and converted into HTTP 400 responses.
struct BadRequest : std::runtime_error {
  using std::runtime_error::runtime_error;
};

void SendJson(httplib::Response& res, int status, const json& body) {
  res.status = status;
  res.set_content(body.dump(), "application/json");
}

void SendError(httplib::Response& res, int status, const std::string& message) {
  SendJson(res, status, json{{"error", message}, {"status", status}});
}

std::int64_t IntParam(const httplib::Request& req, const char* name, std::int64_t fallback) {
  if (!req.has_param(name)) return fallback;
  const std::string raw = req.get_param_value(name);
  try {
    std::size_t pos = 0;
    const long long v = std::stoll(raw, &pos);
    if (pos != raw.size()) throw std::invalid_argument("trailing");
    return v;
  } catch (const std::exception&) {
    throw BadRequest(std::string("query parameter '") + name + "' must be an integer");
  }
}

std::size_t LimitParam(const httplib::Request& req) {
  const auto v = IntParam(req, "limit", static_cast<std::int64_t>(kDefaultLimit));
  if (v <= 0) throw BadRequest("limit must be positive");
  return std::min(static_cast<std::size_t>(v), kMaxLimit);
}

// Wraps a handler so BadRequest -> 400 and any other exception -> 500.
template <typename Fn>
httplib::Server::Handler Guard(Fn fn) {
  return [fn](const httplib::Request& req, httplib::Response& res) {
    try {
      fn(req, res);
    } catch (const BadRequest& e) {
      SendError(res, 400, e.what());
    } catch (const std::exception& e) {
      std::cerr << "[rest] " << req.method << " " << req.path << " failed: " << e.what() << "\n";
      SendError(res, 500, "internal error");
    }
  };
}

}  // namespace

RestApi::RestApi(FleetMonitor& monitor) : monitor_(monitor) { RegisterRoutes(server_); }

RestApi::~RestApi() { Stop(); }

void RestApi::RegisterRoutes(httplib::Server& server) {
  auto bind = [this](void (RestApi::*method)(const httplib::Request&, httplib::Response&)) {
    return Guard([this, method](const httplib::Request& req, httplib::Response& res) {
      (this->*method)(req, res);
    });
  };

  const std::string id = kIdPattern;
  server.Get("/healthz", bind(&RestApi::HandleHealth));
  server.Get("/api/v1/fleet", bind(&RestApi::HandleFleet));
  server.Get("/api/v1/drones", bind(&RestApi::HandleDrones));
  // Must be registered before the {id} route: httplib matches in registration order.
  server.Get("/api/v1/drones/stale", bind(&RestApi::HandleStale));
  server.Get("/api/v1/drones/" + id + "/history", bind(&RestApi::HandleHistory));
  server.Get("/api/v1/drones/" + id, bind(&RestApi::HandleDrone));
  server.Get("/api/v1/events", bind(&RestApi::HandleEvents));

  server.set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
    if (res.body.empty()) {
      SendError(res, res.status, res.status == 404 ? "not found" : "request failed");
    }
  });
}

void RestApi::HandleHealth(const httplib::Request& /*req*/, httplib::Response& res) {
  SendJson(res, 200, json{{"status", "ok"}, {"time_ms", monitor_.clock().NowMs()}});
}

void RestApi::HandleFleet(const httplib::Request& /*req*/, httplib::Response& res) {
  SendJson(res, 200, json{{"summary", monitor_.Summary()}, {"policy", monitor_.policy()}});
}

void RestApi::HandleDrones(const httplib::Request& req, httplib::Response& res) {
  std::optional<Liveness> filter;
  if (req.has_param("liveness")) {
    filter = LivenessFromString(req.get_param_value("liveness"));
    if (!filter) throw BadRequest("liveness must be one of ALIVE, STALE, LOST");
  }
  const auto drones = monitor_.ListDrones(filter);
  SendJson(res, 200, json{{"count", drones.size()}, {"drones", drones}});
}

void RestApi::HandleStale(const httplib::Request& /*req*/, httplib::Response& res) {
  const auto drones = monitor_.ListUnhealthy();
  const TimestampMs now = monitor_.clock().NowMs();
  json items = json::array();
  for (const auto& d : drones) {
    json item = d;
    item["silent_for_ms"] = now - d.last_seen_ms;
    items.push_back(std::move(item));
  }
  SendJson(res, 200,
           json{{"count", drones.size()},
                {"stale_after_ms", monitor_.policy().stale_after_ms},
                {"drones", items}});
}

void RestApi::HandleDrone(const httplib::Request& req, httplib::Response& res) {
  const std::string drone_id = req.matches[1];
  auto drone = monitor_.GetDrone(drone_id);
  if (!drone) {
    SendError(res, 404, "drone '" + drone_id + "' not found");
    return;
  }
  SendJson(res, 200, *drone);
}

void RestApi::HandleHistory(const httplib::Request& req, httplib::Response& res) {
  const std::string drone_id = req.matches[1];
  if (!monitor_.GetDrone(drone_id)) {
    SendError(res, 404, "drone '" + drone_id + "' not found");
    return;
  }
  const auto since = IntParam(req, "since_ms", 0);
  const auto history = monitor_.History(drone_id, since, LimitParam(req));
  SendJson(res, 200,
           json{{"drone_id", drone_id}, {"count", history.size()}, {"heartbeats", history}});
}

void RestApi::HandleEvents(const httplib::Request& req, httplib::Response& res) {
  storage::EventQuery query;
  if (req.has_param("drone_id")) {
    query.drone_id = req.get_param_value("drone_id");
    if (auto err = FleetMonitor::ValidateDroneId(*query.drone_id); !err.empty()) {
      throw BadRequest(err);
    }
  }
  query.since_ms = IntParam(req, "since_ms", 0);
  query.limit = LimitParam(req);
  const auto events = monitor_.Events(query);
  SendJson(res, 200, json{{"count", events.size()}, {"events", events}});
}

bool RestApi::Start(const std::string& host, int port) {
  if (started_.exchange(true)) return false;
  if (!server_.bind_to_port(host, port)) {
    started_ = false;
    return false;
  }
  thread_ = std::thread([this] { server_.listen_after_bind(); });
  return true;
}

int RestApi::StartOnAnyPort(const std::string& host) {
  if (started_.exchange(true)) return -1;
  const int port = server_.bind_to_any_port(host);
  if (port <= 0) {
    started_ = false;
    return -1;
  }
  thread_ = std::thread([this] { server_.listen_after_bind(); });
  return port;
}

void RestApi::Stop() {
  if (!started_.exchange(false)) return;
  server_.stop();
  if (thread_.joinable()) thread_.join();
}

}  // namespace dronebeat::rest
