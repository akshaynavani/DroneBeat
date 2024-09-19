#include "rpc/telemetry_service.hpp"

#include <iostream>

#include "rpc/proto_convert.hpp"

namespace dronebeat::rpc {

namespace pb = dronebeat::v1;

TelemetryServiceImpl::TelemetryServiceImpl(FleetMonitor& monitor, std::uint32_t next_interval_ms)
    : monitor_(monitor), next_interval_ms_(next_interval_ms) {}

::grpc::Status TelemetryServiceImpl::SendHeartbeat(::grpc::ServerContext* /*context*/,
                                                   const pb::Heartbeat* request,
                                                   pb::HeartbeatAck* response) {
  try {
    heartbeats_received_.fetch_add(1, std::memory_order_relaxed);
    const auto result = monitor_.IngestHeartbeat(FromProto(*request));
    response->set_server_time_ms(result.server_time_ms);
    response->set_next_interval_ms(next_interval_ms_);
    switch (result.status) {
      case IngestOutcome::kInvalid:
        return {::grpc::StatusCode::INVALID_ARGUMENT, result.error};
      case IngestOutcome::kDuplicate:
        // Retries of an already-stored heartbeat are harmless; acknowledge but flag them.
        response->set_accepted(false);
        return ::grpc::Status::OK;
      case IngestOutcome::kAccepted:
        response->set_accepted(true);
        return ::grpc::Status::OK;
    }
    return ::grpc::Status::OK;
  } catch (const std::exception& e) {
    std::cerr << "[grpc] SendHeartbeat failed: " << e.what() << "\n";
    return {::grpc::StatusCode::INTERNAL, "failed to store heartbeat"};
  }
}

::grpc::Status TelemetryServiceImpl::StreamHeartbeats(::grpc::ServerContext* context,
                                                      ::grpc::ServerReader<pb::Heartbeat>* reader,
                                                      pb::HeartbeatStreamSummary* response) {
  pb::Heartbeat msg;
  std::uint64_t received = 0;
  std::uint64_t rejected = 0;
  try {
    while (reader->Read(&msg)) {
      if (context->IsCancelled()) {
        return {::grpc::StatusCode::CANCELLED, "client cancelled stream"};
      }
      ++received;
      heartbeats_received_.fetch_add(1, std::memory_order_relaxed);
      const auto result = monitor_.IngestHeartbeat(FromProto(msg));
      if (result.status != IngestOutcome::kAccepted) ++rejected;
    }
  } catch (const std::exception& e) {
    std::cerr << "[grpc] StreamHeartbeats failed: " << e.what() << "\n";
    return {::grpc::StatusCode::INTERNAL, "failed to store heartbeat stream"};
  }
  response->set_received(received);
  response->set_rejected(rejected);
  response->set_server_time_ms(monitor_.clock().NowMs());
  return ::grpc::Status::OK;
}

::grpc::Status TelemetryServiceImpl::ReportStatus(::grpc::ServerContext* /*context*/,
                                                  const pb::StatusUpdate* request,
                                                  pb::StatusAck* response) {
  try {
    const auto result = monitor_.IngestStatus(FromProto(*request));
    if (!result.ok) return {::grpc::StatusCode::INVALID_ARGUMENT, result.error};
    response->set_accepted(true);
    response->set_event_id(result.event_id);
    return ::grpc::Status::OK;
  } catch (const std::exception& e) {
    std::cerr << "[grpc] ReportStatus failed: " << e.what() << "\n";
    return {::grpc::StatusCode::INTERNAL, "failed to store status update"};
  }
}

}  // namespace dronebeat::rpc
