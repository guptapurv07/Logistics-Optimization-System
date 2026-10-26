#ifndef DISPATCH_H
#define DISPATCH_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "accounts.h"
#include "algorithms.h"

// --- DISPATCH PLANNING ---
// The planning step works only on plain lists, without the database, so it can be tested on its own.
// One truck can carry several orders and drop them off one after another on its road from the warehouse.

struct PendingOrder {
    std::int64_t id;
    std::string goodsCode;
    double weightKg;
    double volumeM3;
    std::string destination;
    double price;  // what the customer pays
};

struct FleetTruck {
    std::int64_t id;
    std::string plate;
    std::string bodyType;       // open, tipper or closed
    std::string licenceNeeded;  // LMV (up to 7,500 kg gross weight) or HMV
    double payloadKg;
    double volumeM3;
    double costPerKm;
};

struct FleetDriver {
    std::int64_t id;
    std::string name;
    std::string licenceClass;  // LMV or HMV; an HMV licence covers every truck
};

struct PlannedTrip {
    std::vector<std::int64_t> orderIds;  // in the order they are dropped off
    std::int64_t truckId;
    std::int64_t driverId;
    RouteResult route;                   // warehouse to the last drop-off
    double revenue;                      // what the customers pay for all its orders
    double cost;                         // the truck's running cost per km x the distance there and back
    double driverPay;                    // the driver's pay for the km there and back
    double profit() const { return revenue - cost - driverPay; }
};

struct WaitingOrder {
    std::int64_t orderId;
    std::string reason;
};

struct DispatchPlan {
    std::vector<PlannedTrip> trips;
    std::vector<WaitingOrder> waiting;
};

// Pairs of goods codes that may share a truck, stored both ways, and each code with itself
using GoodsPairs = std::set<std::pair<std::string, std::string>>;

// Oldest waiting order first. Every free truck that may carry it (allowed body, room for its weight and volume, a free
// driver licensed for it) is filled with more waiting orders, oldest first: goods that may share the truck with
// everything already on it, room left, and a delivery city on the same road from the warehouse. A city on the way is
// always worth a stop; going further down the road only when the extra km cost less than a truck of its own for that
// order. The truck that saves the most (what its extra orders would cost alone, minus its own trip's cost) takes the
// load. goodsBodies: goods code -> allowed bodies.
DispatchPlan planDispatch(const std::string& warehouseCity, const std::vector<PendingOrder>& orders,
                          const std::vector<FleetTruck>& freeTrucks, const std::vector<FleetDriver>& freeDrivers,
                          const std::map<std::string, std::set<std::string>>& goodsBodies, const GoodsPairs& mayShare,
                          const RoadNetwork& network);

// --- DISPATCH AND TRIPS IN THE DATABASE ---
// Plans every pending order of the manager's warehouse and saves the trips; trucks and drivers stay busy
// until the driver reports being back at the warehouse
nlohmann::json dispatchWarehouse(Database& db, const SessionUser& manager, const RoadNetwork& network);

// A trip's drop-offs in route order, from its stored route and its orders (each with destination_city and
// delivered_at): [{city, km from the warehouse, orders, delivered_at}]. delivered_at stays null until every
// order for that city is delivered.
nlohmann::json tripStops(const nlohmann::json& route, const nlohmann::json& orders);

// Reads a stored route; a damaged one becomes an empty route instead of breaking a page
nlohmann::json readRoute(const std::string& text);

nlohmann::json driverDashboard(Database& db, const SessionUser& driver);
// The goods for one city are handed over (city "" means the next drop-off); after the last one the truck heads home
void markStopDelivered(Database& db, const SessionUser& driver, std::int64_t tripId, const std::string& city);
void markTripReturned(Database& db, const SessionUser& driver, std::int64_t tripId);   // back home: truck and driver free

#endif
