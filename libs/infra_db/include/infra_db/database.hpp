#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// A thin SQLite wrapper, sized to what this application stores: a handful of
// small tables written whole and read whole.
//
// The reason for a database here is not query power. It is that a visit's state
// now spans several tables that have to agree -- the patient's imaging link,
// the visit's lock position, the delivery log -- and writing those as separate
// files leaves a window where a crash makes them disagree with nothing to
// detect it. One transaction closes that window.
namespace beam::db {

class DatabaseError : public std::runtime_error {
public:
    explicit DatabaseError(const std::string& what) : std::runtime_error(what) {}
};

// One row of results, addressed by column position.
class Row {
public:
    explicit Row(void* statement) : statement_(statement) {}

    bool isNull(int column) const;
    long long integer(int column) const;
    double real(int column) const;
    std::string text(int column) const;

private:
    void* statement_;
};

// A prepared statement. Bind by 1-based index, the way SQLite counts.
class Statement {
public:
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, long long value);
    Statement& bind(int index, double value);
    Statement& bind(int index, const std::string& value);
    Statement& bindNull(int index);

    // Runs a statement that returns nothing.
    void run();
    // Steps once; false when the rows are exhausted.
    bool step();
    Row row() const { return Row(statement_); }
    void reset();

private:
    friend class Database;
    Statement(void* statement, std::string sql)
        : statement_(statement), sql_(std::move(sql)) {}

    void* statement_ = nullptr;
    std::string sql_;
};

// Commits when it goes out of scope normally; rolls back if it does not. An
// exception on the way out therefore leaves the store as it was.
class Transaction {
public:
    explicit Transaction(class Database& database);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    class Database* database_;
    bool open_ = true;
};

class Database {
public:
    // Opens or creates the file. ":memory:" is accepted, which is what the
    // tests use.
    explicit Database(const std::string& path);
    ~Database();
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void execute(const std::string& sql);
    Statement prepare(const std::string& sql);
    long long lastInsertId() const;

    // The schema version this file carries, 0 for a fresh one.
    int userVersion() const;
    void setUserVersion(int version);

    void* handle() const { return handle_; }

private:
    void* handle_ = nullptr;
};

}  // namespace beam::db
