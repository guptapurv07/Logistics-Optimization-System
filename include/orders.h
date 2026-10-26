#ifndef ORDERS_H
#define ORDERS_H

#include <cstdint>
#include "accounts.h"

// --- CUSTOMER ORDERS ---

// What an order would weigh, take up, travel and cost, without placing it: the quantity is converted to kg and m3,
// the road is found, and it must fit at least one truck of the chosen warehouse that may carry these goods.
// Throws ApiError explaining what to change.
nlohmann::json quoteOrder(Database& db, const nlohmann::json& body, const RoadNetwork& network);

// Checks and saves a new order with the same checks as quoteOrder, at the quoted price
nlohmann::json placeOrder(Database& db, const SessionUser& customer, const nlohmann::json& body, const RoadNetwork& network);

// Customers may cancel their own orders, managers those of their warehouse, as long as no truck has them yet
void cancelOrder(Database& db, const SessionUser& user, std::int64_t orderId);

nlohmann::json customerDashboard(Database& db, const SessionUser& customer);

// Orders and trips saved before prices existed (schema 2) get a price, road km and driver pay at today's rates.
// Returns how many orders were priced.
int priceOldRecords(Database& db, const RoadNetwork& network);

#endif
