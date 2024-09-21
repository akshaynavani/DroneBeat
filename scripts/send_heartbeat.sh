#!/usr/bin/env bash
# Sends a single heartbeat with grpcurl (https://github.com/fullstorydev/grpcurl).
#   scripts/send_heartbeat.sh <drone_id> [sequence]
set -euo pipefail

TARGET="${DRONEBEAT_TARGET:-localhost:50051}"
ID="${1:?drone id required}"
SEQ="${2:-$(date +%s)}"
NOW_MS="$(($(date +%s) * 1000))"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

grpcurl -plaintext -import-path "$ROOT/proto" -proto dronebeat.proto -d @ "$TARGET" \
  dronebeat.v1.DroneTelemetry/SendHeartbeat <<JSON
{
  "drone_id": "$ID",
  "sequence": "$SEQ",
  "sent_at_ms": "$NOW_MS",
  "mode": "FLIGHT_MODE_HOVER",
  "position": {"latitude": 37.4275, "longitude": -122.1697, "altitude_m": 30},
  "battery_pct": 88.5,
  "ground_speed_mps": 0,
  "firmware_version": "manual-1.0"
}
JSON
