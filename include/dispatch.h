#ifndef DISPATCH_H
#define DISPATCH_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "accounts.h"
#include "algorithms.h"

// --- DISPATCH PLANNING ---
// The planning step works only on plain lists, without the database, so it can be tested on its own.
// For now every order gets its own truck; step 4 of the fix plan lets one truck drop off several orders
// along its route.

struct PendingOrder {
    std::int64_t id;
    std::string goodsCode;
    double weightKg;
    double volumeM3;
    std::string destination;
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
    std::int64_t orderId;
    std::int64_t truckId;
    std::int64_t driverId;
    RouteResult route;
    double cost;  // the truck's cost per km x the distance there and back
};

struct WaitingOrder {
    std::int64_t orderId;
    std::string reason;
};

struct DispatchPlan {
    std::vector<PlannedTrip> trips;
    std::vector<WaitingOrder> waiting;
};

// Oldest order first, each gets the cheapest free truck that may carry its goods and has room for both its
// weight and its volume, plus a free driver licensed for that truck. goodsBodies: goods code -> allowed bodies.
DispatchPlan planDispatch(const std::string& warehouseCity, const std::vector<PendingOrder>& orders,
                          const std::vector<FleetTruck>& freeTrucks, const std::vector<FleetDriver>& freeDrivers,
                          const std::map<std::string, std::set<std::string>>& goodsBodies, const RoadNetwork& network);

// --- DISPATCH AND TRIPS IN THE DATABASE ---
// Plans every pending order of the manager's warehouse and saves the trips; trucks and drivers stay busy
// until the driver reports being back at the warehouse
nlohmann::json dispatchWarehouse(Database& db, const SessionUser& manager, const RoadNetwork& network);

nlohmann::json driverDashboard(Database& db, const SessionUser& driver);
void markTripDelivered(Database& db, const SessionUser& driver, std::int64_t tripId);  // out: goods handed over
void markTripReturned(Database& db, const SessionUser& driver, std::int64_t tripId);   // back home: truck and driver free

#endif
