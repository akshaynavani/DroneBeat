#pragma once

#include "storage/sqlite.hpp"

namespace dronebeat::storage {

// Current schema version, stored in PRAGMA user_version.
inline constexpr int kSchemaVersion = 1;

// Applies any pending migrations. Safe to call on every startup.
void Migrate(Database& db);

int SchemaVersion(Database& db);

}  // namespace dronebeat::storage
