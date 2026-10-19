// postgres.h - Thin, safe C++ wrapper around PostgreSQL libpq.
//
// * Uses PQconnectdbParams (no hand-built connection strings).
// * All user-influenced values go through PQexecParams ($1, $2 ...) so
//   SQL injection is not possible through this API.
// * Error messages are translated into beginner-friendly hints.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "config.h"

// Forward declarations from libpq-fe.h so callers need not include it.
typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;

namespace memopt {

class PgResult {
public:
    PgResult() = default;
    explicit PgResult(PGresult* r, std::string error);
    ~PgResult();
    PgResult(const PgResult&) = delete;
    PgResult& operator=(const PgResult&) = delete;
    PgResult(PgResult&& o) noexcept;
    PgResult& operator=(PgResult&& o) noexcept;

    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    int rows() const;
    int cols() const;
    std::string value(int row, int col) const;
    bool isNull(int row, int col) const;
    int columnIndex(const std::string& name) const;  // -1 if missing
    std::string columnName(int col) const;
    long long affectedRows() const;

private:
    PGresult* res_ = nullptr;
    bool ok_ = false;
    std::string error_;
};

using Param = std::optional<std::string>;  // nullopt -> SQL NULL

class PgConnection {
public:
    PgConnection() = default;
    ~PgConnection();
    PgConnection(const PgConnection&) = delete;
    PgConnection& operator=(const PgConnection&) = delete;

    // Returns true on success. On failure `lastError()` holds a friendly
    // message (never containing the password).
    bool connect(const AppConfig& cfg);
    void disconnect();
    bool isConnected() const;
    // Reconnect if the connection was dropped (e.g. server restarted).
    bool ensureConnected(const AppConfig& cfg);

    const std::string& lastError() const { return last_error_; }
    bool readOnly() const { return read_only_; }

    PgResult exec(const std::string& sql);
    PgResult execParams(const std::string& sql, const std::vector<Param>& params);

    int backendPid() const;
    std::string serverVersion() const;
    std::string databaseName() const;

private:
    PGconn* conn_ = nullptr;
    std::string last_error_;
    bool read_only_ = false;
};

// Tables that schema.sql must create.
const std::vector<std::string>& requiredTables();
const std::vector<std::string>& dataTables();  // users, products, orders, transactions

struct TableInfo {
    std::string name;
    bool exists = false;
    long long rows = -1;  // -1 = unknown
};

// Checks every required table (parameterized query against
// information_schema) and counts rows of the existing ones.
std::vector<TableInfo> inspectTables(PgConnection& db, std::string& error);

// Converts raw libpq error text into a message with a hint.
std::string friendlyPgError(const std::string& raw, const AppConfig& cfg);

}  // namespace memopt
