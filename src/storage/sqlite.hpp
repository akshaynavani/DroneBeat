#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dronebeat::storage {

class SqliteError : public std::runtime_error {
 public:
  SqliteError(const std::string& what, int code)
      : std::runtime_error(what), code_(code) {}
  int code() const { return code_; }

 private:
  int code_;
};

class Statement;

// Owning wrapper around a sqlite3 connection.
class Database {
 public:
  explicit Database(const std::string& path);
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&& other) noexcept;
  Database& operator=(Database&& other) noexcept;

  void Exec(std::string_view sql);
  Statement Prepare(std::string_view sql);
  std::int64_t LastInsertRowId() const;
  int Changes() const;
  sqlite3* handle() const { return db_; }

 private:
  sqlite3* db_ = nullptr;
};

// Owning wrapper around a prepared statement. Bind indices are 1-based like SQLite.
class Statement {
 public:
  Statement(sqlite3* db, std::string_view sql);
  ~Statement();

  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&& other) noexcept;
  Statement& operator=(Statement&& other) noexcept;

  Statement& Bind(int index, std::int64_t value);
  Statement& Bind(int index, int value) { return Bind(index, static_cast<std::int64_t>(value)); }
  Statement& Bind(int index, std::uint64_t value) {
    return Bind(index, static_cast<std::int64_t>(value));
  }
  Statement& Bind(int index, double value);
  Statement& Bind(int index, std::string_view value);
  Statement& Bind(int index, const char* value) { return Bind(index, std::string_view(value)); }
  Statement& Bind(int index, const std::string& value) {
    return Bind(index, std::string_view(value));
  }

  // Returns true while a row is available; false once the statement is done.
  bool Step();
  // Executes a statement that is not expected to return rows.
  void Run();
  void Reset();

  std::int64_t ColumnInt64(int col) const;
  double ColumnDouble(int col) const;
  std::string ColumnText(int col) const;
  bool ColumnIsNull(int col) const;

 private:
  void Check(int rc, const char* what) const;

  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

// RAII transaction: rolls back unless Commit() is called.
class Transaction {
 public:
  explicit Transaction(Database& db);
  ~Transaction();
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  void Commit();

 private:
  Database& db_;
  bool done_ = false;
};

}  // namespace dronebeat::storage
