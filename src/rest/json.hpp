#pragma once

#include <nlohmann/json.hpp>

#include "core/liveness.hpp"
#include "core/types.hpp"

namespace dronebeat {

// ADL hooks so nlohmann::json can serialise domain types directly.
void to_json(nlohmann::json& j, const GeoPosition& p);
void to_json(nlohmann::json& j, const DroneState& d);
void to_json(nlohmann::json& j, const Heartbeat& hb);
void to_json(nlohmann::json& j, const StatusEvent& e);
void to_json(nlohmann::json& j, const FleetSummary& s);
void to_json(nlohmann::json& j, const LivenessPolicy& p);

}  // namespace dronebeat
