#!/usr/bin/env bash
# Convenience wrapper around the DroneBeat REST API.
#   scripts/fleet.sh summary
#   scripts/fleet.sh drones [ALIVE|STALE|LOST]
#   scripts/fleet.sh stale
#   scripts/fleet.sh drone <id>
#   scripts/fleet.sh history <id> [limit]
#   scripts/fleet.sh events [drone_id]
#   scripts/fleet.sh watch            # refresh the stale list every 2s
set -euo pipefail

BASE_URL="${DRONEBEAT_URL:-http://localhost:8080}"

pretty() {
  if command -v jq >/dev/null 2>&1; then jq .; else cat; echo; fi
}

get() { curl -fsS "${BASE_URL}$1" | pretty; }

cmd="${1:-summary}"
shift || true

case "$cmd" in
  summary) get "/api/v1/fleet" ;;
  drones)
    if [[ $# -gt 0 ]]; then get "/api/v1/drones?liveness=$1"; else get "/api/v1/drones"; fi ;;
  stale)   get "/api/v1/drones/stale" ;;
  drone)   get "/api/v1/drones/${1:?drone id required}" ;;
  history) get "/api/v1/drones/${1:?drone id required}/history?limit=${2:-20}" ;;
  events)
    if [[ $# -gt 0 ]]; then get "/api/v1/events?drone_id=$1"; else get "/api/v1/events"; fi ;;
  watch)
    while true; do
      clear
      date
      get "/api/v1/fleet"
      get "/api/v1/drones/stale"
      sleep 2
    done ;;
  *)
    sed -n '2,10p' "$0"
    exit 1 ;;
esac
