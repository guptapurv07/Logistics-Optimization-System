#ifndef SCHEMA_H
#define SCHEMA_H

#include "db.h"

// --- DATABASE SCHEMA ---
// The schema version is stored in the database file (PRAGMA user_version), so a later version of the
// program can upgrade an old file step by step instead of guessing what it contains.
// 1: company (warehouses, users, trucks, catalogues)   2: orders, trips and login sessions
// 3: order prices and road km, driver pay per trip
constexpr int kSchemaVersion = 3;

// Creates the tables in a new database; does nothing if the database is already up to date
void createSchema(Database& db);

#endif
