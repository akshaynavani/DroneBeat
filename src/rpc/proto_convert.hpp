#pragma once

#include "core/types.hpp"
#include "dronebeat.pb.h"

namespace dronebeat::rpc {

FlightMode FromProto(dronebeat::v1::FlightMode mode);
dronebeat::v1::FlightMode ToProto(FlightMode mode);

Severity FromProto(dronebeat::v1::Severity severity);
dronebeat::v1::Severity ToProto(Severity severity);

Heartbeat FromProto(const dronebeat::v1::Heartbeat& msg);
void ToProto(const Heartbeat& hb, dronebeat::v1::Heartbeat* out);

StatusEvent FromProto(const dronebeat::v1::StatusUpdate& msg);

}  // namespace dronebeat::rpc
