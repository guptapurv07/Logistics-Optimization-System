#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include <cstdint>
#include <optional>
#include <string>
#include "algorithms.h"
#include "db.h"
#include "json.hpp"

// --- ACCOUNTS AND LOGIN SESSIONS ---

struct SessionUser {
    std::int64_t id = 0;
    std::string username, role, fullName, email;
    std::string approval;          // drivers only: pending, approved or rejected
    std::int64_t warehouseId = 0;  // managers and drivers
    std::string warehouseCode, warehouseName, warehouseCity;
};

constexpr int kSessionHours = 12;

// Creates a customer, a driver (who then waits for approval) or a manager together with their warehouse.
// Returns {"user_id", "role", "can_login", "message"}; throws ApiError for anything the user must change.
nlohmann::json registerAccount(Database& db, const nlohmann::json& body, const RoadNetwork& network);

// Checks a username and password; throws ApiError with a message the login page can show
SessionUser checkLogin(Database& db, const std::string& username, const std::string& password);

std::optional<SessionUser> loadUser(Database& db, std::int64_t userId);

// A session is a random token kept in a cookie; only its SHA-256 is stored in the database
std::string createSession(Database& db, std::int64_t userId);
std::optional<SessionUser> userForSession(Database& db, const std::string& token);
void endSession(Database& db, const std::string& token);

nlohmann::json userJson(const SessionUser& user);

#endif
