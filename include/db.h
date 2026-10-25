#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <cstdint>
#include <stdexcept>
#include <string>

// --- SQLITE WRAPPER ---
// Small RAII classes around the SQLite C API: every handle is released in a destructor,
// so an exception halfway through a query can never leak a connection or leave a transaction open.

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Statement;

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Runs SQL that returns no rows (may contain several statements)
    void exec(const std::string& sql);
    Statement prepare(const std::string& sql);
    std::int64_t lastInsertId() const;
    int changes() const;  // rows changed by the last INSERT / UPDATE / DELETE

private:
    sqlite3* db_ = nullptr;
};

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();

    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Parameters are numbered from 1, matching the ?1, ?2 ... placeholders
    Statement& bind(int index, int value);
    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, double value);
    Statement& bind(int index, const std::string& value);
    Statement& bindNull(int index);

    bool step();   // true while a result row is available
    void run();    // runs an INSERT / UPDATE / DELETE to completion
    void reset();  // clears bindings so the statement can be reused

    // Columns are numbered from 0
    std::int64_t columnInt(int col) const;
    double columnDouble(int col) const;
    std::string columnText(int col) const;
    bool columnIsNull(int col) const;

private:
    void check(int rc) const;

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

// Rolls back automatically unless commit() was called, e.g. when an exception leaves the scope
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Database& db_;
    bool finished_ = false;
};

#endif
