# DroneBeat — Implementation Plan

**gRPC Heartbeat Monitor for Drone Fleets**
Tech: C++17, gRPC, Protobuf, REST (cpp-httplib), SQLite, CMake, GoogleTest

## Goal

A C++17 service that:

1. Accepts heartbeats and status updates from drones over gRPC (unary + client-streaming).
2. Persists the latest state of every drone plus a bounded telemetry history in SQLite.
3. Exposes a REST/JSON API for querying fleet state, individual drones, history, and
   "stale" drones whose last heartbeat is older than a configurable threshold.
4. Ships a mock drone client that simulates a fleet (including drones that drop out)
   so the whole system can be exercised locally.

## Key design decisions

- **Build:** CMake ≥ 3.20. gRPC, Protobuf and SQLite3 come from the system
  (`find_package`); header-only `cpp-httplib` and `nlohmann/json` plus GoogleTest are pulled
  in with `FetchContent` so there are no vendored sources.
- **Layering:** `proto` → `core` (domain types, clock, staleness logic) → `storage`
  (SQLite repository) → `grpc` service + `rest` API → `server` binary. Each layer is a
  static library so tests link only what they need.
- **Time:** all timestamps are Unix epoch milliseconds. Server-side receive time is the
  source of truth for staleness (drone clocks are untrusted); the drone-reported time is
  stored alongside for diagnostics. A `Clock` interface lets tests inject fake time.
- **Concurrency:** one SQLite connection guarded by a mutex, WAL mode enabled. Fine for
  the expected load (hundreds of drones at ~1 Hz); documented as the scaling limit.
- **Staleness:** a drone is `STALE` when `now - last_seen > stale_after_ms` and `LOST` when
  `now - last_seen > lost_after_ms`. Status is computed at query time, and a background
  sweeper logs transitions and records them as events.
- **Config:** CLI flags with environment-variable fallbacks (`DRONEBEAT_*`).

## Phases

### Phase 1 — Skeleton, protocol and core domain
Deliverables:
- CMake project, dependency wiring, `.clang-format`.
- `proto/dronebeat.proto`: `DroneTelemetry` service with `SendHeartbeat` (unary),
  `StreamHeartbeats` (client-streaming), `ReportStatus` (unary status/event update).
- Core types (`DroneState`, `Heartbeat`, `FleetSummary`), `Clock` abstraction,
  liveness classifier, config parsing.

Files: `CMakeLists.txt`, `cmake/Dependencies.cmake`, `proto/dronebeat.proto`,
`src/core/*.hpp|cpp`, `src/config/*`.

### Phase 2 — SQLite storage layer
Deliverables:
- RAII SQLite wrappers (`Database`, `Statement`).
- Schema migrations (`drones`, `heartbeats`, `events` tables + indexes).
- `DroneRepository`: upsert heartbeat (transactional: latest state + history row),
  record status events, list drones, get drone, history query, stale query,
  history pruning.
- Unit tests for the repository using an in-memory DB.

Files: `src/storage/*`, `tests/storage_test.cpp`.

### Phase 3 — gRPC service
Deliverables:
- `TelemetryServiceImpl` implementing all RPCs with input validation and
  proper gRPC status codes.
- `FleetMonitor` façade combining repository + clock + liveness rules, shared by gRPC
  and REST layers.
- Background `StalenessSweeper` thread emitting transition events.
- Tests for the monitor and sweeper with a fake clock.

Files: `src/service/*`, `src/rpc/*` (named `rpc` to avoid shadowing `<grpc/...>` headers), `tests/monitor_test.cpp`.

### Phase 4 — REST API and server binary
Deliverables:
- JSON serialization for domain types.
- REST routes: `GET /healthz`, `GET /api/v1/fleet`, `GET /api/v1/drones`,
  `GET /api/v1/drones/{id}`, `GET /api/v1/drones/{id}/history`,
  `GET /api/v1/drones/stale`, `GET /api/v1/events`.
- `dronebeat_server` main: starts gRPC + REST, graceful shutdown on SIGINT/SIGTERM.
- REST handler tests (in-process httplib client).

Files: `src/rest/*`, `src/server/main.cpp`, `tests/rest_test.cpp`.

### Phase 5 — Mock drone fleet client and packaging
Deliverables:
- `drone_sim`: spawns N simulated drones (thread each), random-walk GPS, battery
  drain, modes, configurable drop-out probability, unary or streaming mode.
- Dockerfile + docker-compose (server + simulator), `.env.example`.
- Helper script for curl queries.

Files: `src/sim/*`, `Dockerfile`, `docker-compose.yml`, `.env.example`, `scripts/*`.

After phases: `.gitignore`, `README.md` with architecture diagram, run and test docs.
