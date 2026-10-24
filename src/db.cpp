#include "../include/db.h"
#include <utility>

// --- DATABASE ---
Database::Database(const std::string& path) {
    // FULLMUTEX: the HTTP server answers requests on several threads that share this connection
    int rc = sqlite3_open_v2(path.c_str(), &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
        sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("Cannot open database '" + path + "': " + message);
    }
    sqlite3_busy_timeout(db_, 5000);
    try {
        exec("PRAGMA foreign_keys = ON;");   // SQLite leaves foreign keys off unless asked
        exec("PRAGMA journal_mode = WAL;");  // readers don't block the writer
    } catch (...) {
        // The destructor doesn't run when a constructor throws, so close here
        sqlite3_close_v2(db_);
        throw;
    }
}

Database::~Database() {
    sqlite3_close_v2(db_);
}

void Database::exec(const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw DbError(message);
    }
}

Statement Database::prepare(const std::string& sql) {
    return Statement(db_, sql);
}

std::int64_t Database::lastInsertId() const {
    return sqlite3_last_insert_rowid(db_);
}

// --- STATEMENT ---
Statement::Statement(sqlite3* db, const std::string& sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
        throw DbError(std::string(sqlite3_errmsg(db_)) + " in: " + sql);
    }
}

Statement::~Statement() {
    sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        sqlite3_finalize(stmt_);
        db_ = std::exchange(other.db_, nullptr);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

void Statement::check(int rc) const {
    if (rc != SQLITE_OK) throw DbError(sqlite3_errmsg(db_));
}

Statement& Statement::bind(int index, int value) {
    check(sqlite3_bind_int(stmt_, index, value));
    return *this;
}

Statement& Statement::bind(int index, std::int64_t value) {
    check(sqlite3_bind_int64(stmt_, index, value));
    return *this;
}

Statement& Statement::bind(int index, double value) {
    check(sqlite3_bind_double(stmt_, index, value));
    return *this;
}

Statement& Statement::bind(int index, const std::string& value) {
    // SQLITE_TRANSIENT: SQLite keeps its own copy, so `value` may go away before step()
    check(sqlite3_bind_text(stmt_, index, value.c_str(), (int)value.size(), SQLITE_TRANSIENT));
    return *this;
}

Statement& Statement::bindNull(int index) {
    check(sqlite3_bind_null(stmt_, index));
    return *this;
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DbError(sqlite3_errmsg(db_));
}

void Statement::run() {
    while (step()) {}
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::int64_t Statement::columnInt(int col) const {
    return sqlite3_column_int64(stmt_, col);
}

double Statement::columnDouble(int col) const {
    return sqlite3_column_double(stmt_, col);
}

std::string Statement::columnText(int col) const {
    const unsigned char* text = sqlite3_column_text(stmt_, col);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

bool Statement::columnIsNull(int col) const {
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

// --- TRANSACTION ---
Transaction::Transaction(Database& db) : db_(db) {
    db_.exec("BEGIN IMMEDIATE;");
}

Transaction::~Transaction() {
    if (!finished_) {
        try {
            db_.exec("ROLLBACK;");
        } catch (...) {
            // A destructor must not throw; SQLite also rolls back by itself when the connection closes
        }
    }
}

void Transaction::commit() {
    db_.exec("COMMIT;");
    finished_ = true;
}
