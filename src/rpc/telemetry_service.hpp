#pragma once

#include <atomic>
#include <cstdint>

#include "dronebeat.grpc.pb.h"
#include "service/fleet_monitor.hpp"

namespace dronebeat::rpc {

// gRPC front-end for drone telemetry. All business logic lives in FleetMonitor;
// this class only translates messages and maps outcomes to gRPC status codes.
class TelemetryServiceImpl final : public dronebeat::v1::DroneTelemetry::Service {
 public:
  TelemetryServiceImpl(FleetMonitor& monitor, std::uint32_t next_interval_ms);

  ::grpc::Status SendHeartbeat(::grpc::ServerContext* context,
                               const dronebeat::v1::Heartbeat* request,
                               dronebeat::v1::HeartbeatAck* response) override;

  ::grpc::Status StreamHeartbeats(::grpc::ServerContext* context,
                                  ::grpc::ServerReader<dronebeat::v1::Heartbeat>* reader,
                                  dronebeat::v1::HeartbeatStreamSummary* response) override;

  ::grpc::Status ReportStatus(::grpc::ServerContext* context,
                              const dronebeat::v1::StatusUpdate* request,
                              dronebeat::v1::StatusAck* response) override;

  std::uint64_t heartbeats_received() const { return heartbeats_received_.load(); }

 private:
  FleetMonitor& monitor_;
  std::uint32_t next_interval_ms_;
  std::atomic<std::uint64_t> heartbeats_received_{0};
};

}  // namespace dronebeat::rpc
