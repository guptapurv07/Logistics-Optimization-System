#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include <cstdint>
#include <optional>
#include <string>
#include "db.h"
#include "google.h"
#include "json.hpp"
#include "network.h"

// --- ACCOUNTS AND LOGIN SESSIONS ---

struct SessionUser {
    std::int64_t id = 0;
    std::string username, role, fullName, email;
    std::string approval;          // drivers only: pending, approved or rejected
    std::int64_t warehouseId = 0;  // managers and drivers
    std::string warehouseCode, warehouseName, warehouseCity;
};

constexpr int kSessionHours = 12;

// Checks a sign-up form for a customer, a driver or a manager with a new warehouse, and hashes its password.
// Returns the checked values as JSON, ready for createAccount; they wait in the database while the email code is out.
// With `google`, the email comes from Google (already confirmed) and the password is optional: without one, the
// account logs in with its Google button only. Throws ApiError for anything the person must change.
nlohmann::json checkSignup(Database& db, const nlohmann::json& body, const RoadMap& roadMap, const GoogleAccount* google);

// Saves a checked sign-up; a driver then waits for approval. Returns {"user_id", "role", "can_login", "message"}.
// Throws ApiError (409) if the username, email, licence or warehouse city was taken in the meantime.
nlohmann::json createAccount(Database& db, const nlohmann::json& signup, const RoadMap& roadMap);

// Checks a username (or email) and password; throws ApiError with a message the login page can show
SessionUser checkLogin(Database& db, const std::string& login, const std::string& password);

// Refuses an account of another kind than the one chosen on the login page ("" = any kind),
// and a driver whose manager hasn't approved them
void checkCanLogIn(const SessionUser& user, const std::string& role);

std::optional<SessionUser> loadUser(Database& db, std::int64_t userId);
std::optional<SessionUser> findUserByEmail(Database& db, const std::string& email);

// A free username made from an email address: "apurv1305", or "apurv13052" if that is taken
std::string suggestUsername(Database& db, const std::string& email);

// A session is a random token kept in a cookie; only its SHA-256 is stored in the database
std::string createSession(Database& db, std::int64_t userId);
std::optional<SessionUser> userForSession(Database& db, const std::string& token);
void endSession(Database& db, const std::string& token);

nlohmann::json userJson(const SessionUser& user);

#endif
