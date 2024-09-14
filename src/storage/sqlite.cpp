#include "storage/sqlite.hpp"

#include <utility>

namespace dronebeat::storage {

// ----------------------------------------------------------------------------
// Database
// ----------------------------------------------------------------------------

Database::Database(const std::string& path) {
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  const int rc = sqlite3_open_v2(path.c_str(), &db_, flags, nullptr);
  if (rc != SQLITE_OK) {
    std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
    sqlite3_close(db_);
    db_ = nullptr;
    throw SqliteError("failed to open database '" + path + "': " + msg, rc);
  }
  sqlite3_busy_timeout(db_, 5'000);
  Exec("PRAGMA foreign_keys = ON;");
  if (path != ":memory:") {
    Exec("PRAGMA journal_mode = WAL;");
    Exec("PRAGMA synchronous = NORMAL;");
  }
}

Database::~Database() {
  if (db_ != nullptr) sqlite3_close_v2(db_);
}

Database::Database(Database&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

Database& Database::operator=(Database&& other) noexcept {
  if (this != &other) {
    if (db_ != nullptr) sqlite3_close_v2(db_);
    db_ = std::exchange(other.db_, nullptr);
  }
  return *this;
}

void Database::Exec(std::string_view sql) {
  char* err = nullptr;
  const std::string owned(sql);
  const int rc = sqlite3_exec(db_, owned.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::string msg = err ? err : sqlite3_errmsg(db_);
    sqlite3_free(err);
    throw SqliteError("exec failed: " + msg, rc);
  }
}

Statement Database::Prepare(std::string_view sql) { return Statement(db_, sql); }

std::int64_t Database::LastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

int Database::Changes() const { return sqlite3_changes(db_); }

// ----------------------------------------------------------------------------
// Statement
// ----------------------------------------------------------------------------

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
  const int rc =
      sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
  if (rc != SQLITE_OK) {
    throw SqliteError("prepare failed: " + std::string(sqlite3_errmsg(db_)) + " | " +
                          std::string(sql),
                      rc);
  }
}

Statement::~Statement() {
  if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept
    : db_(other.db_), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
  if (this != &other) {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
    db_ = other.db_;
    stmt_ = std::exchange(other.stmt_, nullptr);
  }
  return *this;
}

void Statement::Check(int rc, const char* what) const {
  if (rc != SQLITE_OK) {
    throw SqliteError(std::string(what) + ": " + sqlite3_errmsg(db_), rc);
  }
}

Statement& Statement::Bind(int index, std::int64_t value) {
  Check(sqlite3_bind_int64(stmt_, index, value), "bind int64");
  return *this;
}

Statement& Statement::Bind(int index, double value) {
  Check(sqlite3_bind_double(stmt_, index, value), "bind double");
  return *this;
}

Statement& Statement::Bind(int index, std::string_view value) {
  Check(sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT),
        "bind text");
  return *this;
}

bool Statement::Step() {
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  throw SqliteError("step failed: " + std::string(sqlite3_errmsg(db_)), rc);
}

void Statement::Run() {
  while (Step()) {
  }
}

void Statement::Reset() {
  sqlite3_reset(stmt_);
  sqlite3_clear_bindings(stmt_);
}

std::int64_t Statement::ColumnInt64(int col) const { return sqlite3_column_int64(stmt_, col); }

double Statement::ColumnDouble(int col) const { return sqlite3_column_double(stmt_, col); }

std::string Statement::ColumnText(int col) const {
  const auto* text = sqlite3_column_text(stmt_, col);
  if (text == nullptr) return {};
  return std::string(reinterpret_cast<const char*>(text),
                     static_cast<std::size_t>(sqlite3_column_bytes(stmt_, col)));
}

bool Statement::ColumnIsNull(int col) const {
  return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

// ----------------------------------------------------------------------------
// Transaction
// ----------------------------------------------------------------------------

Transaction::Transaction(Database& db) : db_(db) { db_.Exec("BEGIN IMMEDIATE;"); }

Transaction::~Transaction() {
  if (!done_) {
    try {
      db_.Exec("ROLLBACK;");
    } catch (...) {
      // Destructors must not throw; a failed rollback leaves SQLite to clean up.
    }
  }
}

void Transaction::Commit() {
  db_.Exec("COMMIT;");
  done_ = true;
}

}  // namespace dronebeat::storage
