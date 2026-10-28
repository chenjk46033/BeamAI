#include "infra_db/database.hpp"

#include <sqlite3.h>

namespace beam::db {
namespace {

sqlite3* as(void* handle) { return static_cast<sqlite3*>(handle); }
sqlite3_stmt* asStatement(void* statement) { return static_cast<sqlite3_stmt*>(statement); }

[[noreturn]] void fail(sqlite3* handle, const std::string& what) {
    throw DatabaseError(what + ": " + (handle ? sqlite3_errmsg(handle) : "no handle"));
}

}  // namespace

bool Row::isNull(int column) const {
    return sqlite3_column_type(asStatement(statement_), column) == SQLITE_NULL;
}

long long Row::integer(int column) const {
    return sqlite3_column_int64(asStatement(statement_), column);
}

double Row::real(int column) const {
    return sqlite3_column_double(asStatement(statement_), column);
}

std::string Row::text(int column) const {
    const unsigned char* value = sqlite3_column_text(asStatement(statement_), column);
    if (value == nullptr) return std::string();
    const int bytes = sqlite3_column_bytes(asStatement(statement_), column);
    return std::string(reinterpret_cast<const char*>(value), static_cast<size_t>(bytes));
}

Statement::~Statement() {
    if (statement_ != nullptr) sqlite3_finalize(asStatement(statement_));
}

Statement::Statement(Statement&& other) noexcept
    : statement_(other.statement_), sql_(std::move(other.sql_)) {
    other.statement_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (statement_ != nullptr) sqlite3_finalize(asStatement(statement_));
        statement_ = other.statement_;
        sql_ = std::move(other.sql_);
        other.statement_ = nullptr;
    }
    return *this;
}

Statement& Statement::bind(int index, long long value) {
    if (sqlite3_bind_int64(asStatement(statement_), index, value) != SQLITE_OK)
        throw DatabaseError("bind failed in: " + sql_);
    return *this;
}

Statement& Statement::bind(int index, double value) {
    if (sqlite3_bind_double(asStatement(statement_), index, value) != SQLITE_OK)
        throw DatabaseError("bind failed in: " + sql_);
    return *this;
}

Statement& Statement::bind(int index, const std::string& value) {
    // SQLITE_TRANSIENT: sqlite copies, so the caller's string may go away.
    if (sqlite3_bind_text(asStatement(statement_), index, value.c_str(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throw DatabaseError("bind failed in: " + sql_);
    return *this;
}

Statement& Statement::bindNull(int index) {
    if (sqlite3_bind_null(asStatement(statement_), index) != SQLITE_OK)
        throw DatabaseError("bind failed in: " + sql_);
    return *this;
}

void Statement::run() {
    const int status = sqlite3_step(asStatement(statement_));
    if (status != SQLITE_DONE && status != SQLITE_ROW)
        throw DatabaseError("statement failed: " + sql_);
    reset();
}

bool Statement::step() {
    const int status = sqlite3_step(asStatement(statement_));
    if (status == SQLITE_ROW) return true;
    if (status == SQLITE_DONE) return false;
    throw DatabaseError("statement failed: " + sql_);
}

void Statement::reset() {
    sqlite3_reset(asStatement(statement_));
    sqlite3_clear_bindings(asStatement(statement_));
}

Transaction::Transaction(Database& database) : database_(&database) {
    database_->execute("BEGIN");
}

Transaction::~Transaction() {
    // Unwinding, or a caller that never committed: the store is left as it was.
    if (!open_) return;
    try {
        database_->execute("ROLLBACK");
    } catch (const DatabaseError&) {
        // Already failing; a rollback that cannot run must not replace the cause.
    }
}

void Transaction::commit() {
    if (!open_) return;
    database_->execute("COMMIT");
    open_ = false;
}

Database::Database(const std::string& path) {
    sqlite3* handle = nullptr;
    if (sqlite3_open(path.c_str(), &handle) != SQLITE_OK) {
        const std::string message =
            handle ? sqlite3_errmsg(handle) : std::string("could not open");
        sqlite3_close(handle);
        throw DatabaseError("cannot open " + path + ": " + message);
    }
    handle_ = handle;
    // Foreign keys are off by default in sqlite, which would make the visit and
    // sonication relationships advisory rather than enforced.
    execute("PRAGMA foreign_keys = ON");
    // Survives a crash without the torn state separate CSV files could leave.
    execute("PRAGMA journal_mode = WAL");
    execute("PRAGMA synchronous = FULL");
}

Database::~Database() {
    if (handle_ != nullptr) sqlite3_close(as(handle_));
}

Database::Database(Database&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) sqlite3_close(as(handle_));
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

void Database::execute(const std::string& sql) {
    char* message = nullptr;
    if (sqlite3_exec(as(handle_), sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string detail = message ? message : "unknown";
        sqlite3_free(message);
        throw DatabaseError("failed: " + sql + ": " + detail);
    }
}

Statement Database::prepare(const std::string& sql) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(as(handle_), sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        fail(as(handle_), "cannot prepare: " + sql);
    return Statement(statement, sql);
}

long long Database::lastInsertId() const { return sqlite3_last_insert_rowid(as(handle_)); }

int Database::userVersion() const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(as(handle_), "PRAGMA user_version", -1, &statement, nullptr) !=
        SQLITE_OK)
        return 0;
    int version = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return version;
}

void Database::setUserVersion(int version) {
    execute("PRAGMA user_version = " + std::to_string(version));
}

}  // namespace beam::db
