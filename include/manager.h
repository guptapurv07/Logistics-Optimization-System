#ifndef MANAGER_H
#define MANAGER_H

#include <cstdint>
#include "accounts.h"

// --- WAREHOUSE MANAGER ---
// Every function works only on the manager's own warehouse, taken from their login, never from the request

nlohmann::json managerDashboard(Database& db, const SessionUser& manager);

// Approves or declines a driver who applied to this warehouse
void decideDriver(Database& db, const SessionUser& manager, std::int64_t driverId, bool approve);

nlohmann::json addTruck(Database& db, const SessionUser& manager, const nlohmann::json& body);

// "mh01ab1234" -> "MH 01 AB 1234"; returns "" if it isn't an Indian number plate
std::string normalisePlate(const std::string& text);

#endif
