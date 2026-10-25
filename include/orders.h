#ifndef ORDERS_H
#define ORDERS_H

#include <cstdint>
#include "accounts.h"

// --- CUSTOMER ORDERS ---

// Checks and saves a new order: the quantity is converted to kg and m3, and the order must fit at least one
// truck of the chosen warehouse that may carry these goods. Throws ApiError explaining what to change.
nlohmann::json placeOrder(Database& db, const SessionUser& customer, const nlohmann::json& body, const RoadNetwork& network);

// Customers may cancel their own orders, managers those of their warehouse, as long as no truck has them yet
void cancelOrder(Database& db, const SessionUser& user, std::int64_t orderId);

nlohmann::json customerDashboard(Database& db, const SessionUser& customer);

#endif
