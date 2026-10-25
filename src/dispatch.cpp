#include "../include/dispatch.h"
#include "../include/api.h"
#include <algorithm>
#include <cstdio>

using json = nlohmann::json;

namespace {

bool canDrive(const FleetDriver& driver, const FleetTruck& truck) {
    return driver.licenceClass == "HMV" || truck.licenceNeeded == "LMV";
}

// The cheaper truck per km wins; on a tie the smaller one (keeping big trucks free), then the lower id
bool betterTruck(const FleetTruck& a, const FleetTruck& b) {
    if (a.costPerKm != b.costPerKm) return a.costPerKm < b.costPerKm;
    if (a.payloadKg != b.payloadKg) return a.payloadKg < b.payloadKg;
    return a.id < b.id;
}

std::string formatNumber(double value, int decimals) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
}

// {"closed", "open"} -> "closed or open"
std::string joinBodies(const std::set<std::string>& bodies) {
    std::string text;
    for (const std::string& body : bodies) text += (text.empty() ? "" : " or ") + body;
    return text.empty() ? "suitable" : text;
}

json routeJson(const RouteResult& route) {
    json legs = json::array();
    for (std::size_t i = 0; i < route.legs.size(); ++i) {
        legs.push_back({{"from", route.path[i]}, {"to", route.legs[i].to}, {"road", route.legs[i].type}, {"km", route.legs[i].weight}});
    }
    return {{"path", route.path}, {"legs", legs}};
}

// Stored routes are written by this program, but a damaged one shouldn't break a whole page
json readRoute(const std::string& text) {
    json route = json::parse(text, nullptr, false);
    if (route.is_discarded() || !route.is_object()) return {{"path", json::array()}, {"legs", json::array()}};
    return route;
}

}  // namespace

DispatchPlan planDispatch(const std::string& warehouseCity, const std::vector<PendingOrder>& orders,
                          const std::vector<FleetTruck>& freeTrucks, const std::vector<FleetDriver>& freeDrivers,
                          const std::map<std::string, std::set<std::string>>& goodsBodies, const RoadNetwork& network) {
    DispatchPlan plan;
    std::vector<bool> truckTaken(freeTrucks.size(), false);
    std::vector<bool> driverTaken(freeDrivers.size(), false);
    std::map<std::string, RouteResult> routes;  // one Dijkstra run per destination city
    static const std::set<std::string> kNoBodies;

    // Oldest first (ids grow with time). The caller's list is left as it is: only pointers are sorted.
    std::vector<const PendingOrder*> queue;
    for (const PendingOrder& order : orders) queue.push_back(&order);
    std::sort(queue.begin(), queue.end(), [](const PendingOrder* a, const PendingOrder* b) { return a->id < b->id; });

    for (const PendingOrder* order : queue) {
        auto known = routes.find(order->destination);
        if (known == routes.end()) {
            known = routes.emplace(order->destination, runDijkstraRouting(warehouseCity, order->destination, network)).first;
        }
        const RouteResult& route = known->second;
        if (!route.reachable) {
            plan.waiting.push_back({order->id, "No road from " + warehouseCity + " to " + order->destination});
            continue;
        }

        auto allowed = goodsBodies.find(order->goodsCode);
        const std::set<std::string>& bodies = allowed != goodsBodies.end() ? allowed->second : kNoBodies;

        std::size_t best = freeTrucks.size();
        bool fitsButNoDriver = false;
        for (std::size_t t = 0; t < freeTrucks.size(); ++t) {
            const FleetTruck& truck = freeTrucks[t];
            if (truckTaken[t] || !bodies.count(truck.bodyType) || truck.payloadKg < order->weightKg ||
                truck.volumeM3 < order->volumeM3) {
                continue;
            }
            bool hasDriver = false;
            for (std::size_t d = 0; d < freeDrivers.size() && !hasDriver; ++d) {
                hasDriver = !driverTaken[d] && canDrive(freeDrivers[d], truck);
            }
            if (!hasDriver) {
                fitsButNoDriver = true;
                continue;
            }
            if (best == freeTrucks.size() || betterTruck(truck, freeTrucks[best])) best = t;
        }

        if (best == freeTrucks.size()) {
            bool anyDriverFree = std::find(driverTaken.begin(), driverTaken.end(), false) != driverTaken.end();
            std::string reason;
            if (fitsButNoDriver) {
                reason = anyDriverFree ? "Waiting for a free driver with an HMV licence" : "Waiting for a free driver";
            } else {
                reason = "Waiting for a free " + joinBodies(bodies) + " truck with room for " + formatNumber(order->weightKg, 0) +
                         " kg and " + formatNumber(order->volumeM3, 1) + " m3";
            }
            plan.waiting.push_back({order->id, reason});
            continue;
        }

        // A light truck goes to an LMV driver if one is free, keeping HMV drivers for the heavy trucks only they may drive
        const FleetTruck& truck = freeTrucks[best];
        std::size_t chosen = freeDrivers.size();
        for (int pass = 0; pass < 2 && chosen == freeDrivers.size(); ++pass) {
            for (std::size_t d = 0; d < freeDrivers.size(); ++d) {
                bool preferred = pass == 1 || truck.licenceNeeded == "HMV" || freeDrivers[d].licenceClass == "LMV";
                if (!driverTaken[d] && canDrive(freeDrivers[d], truck) && preferred) {
                    chosen = d;
                    break;
                }
            }
        }

        truckTaken[best] = true;
        driverTaken[chosen] = true;
        plan.trips.push_back({order->id, truck.id, freeDrivers[chosen].id, route, truck.costPerKm * 2.0 * route.distance});
    }
    return plan;
}

json dispatchWarehouse(Database& db, const SessionUser& manager, const RoadNetwork& network) {
    // BEGIN IMMEDIATE: a second dispatch at the same moment waits here, then sees these trucks as busy
    Transaction tx(db);

    std::vector<PendingOrder> orders;
    {
        Statement query = db.prepare(
            "SELECT id, goods_code, weight_kg, volume_m3, destination_city FROM orders WHERE warehouse_id = ?1 AND status = 'pending' ORDER BY id");
        query.bind(1, manager.warehouseId);
        while (query.step()) {
            orders.push_back({query.columnInt(0), query.columnText(1), query.columnDouble(2), query.columnDouble(3), query.columnText(4)});
        }
    }
    std::vector<FleetTruck> trucks;
    {
        Statement query = db.prepare(R"SQL(
            SELECT t.id, t.plate_no, m.body_type, m.licence_needed, m.payload_kg, m.volume_m3, m.cost_per_km
            FROM trucks t JOIN truck_models m ON m.id = t.model_id
            WHERE t.warehouse_id = ?1 AND t.status = 'available' ORDER BY t.id)SQL");
        query.bind(1, manager.warehouseId);
        while (query.step()) {
            trucks.push_back({query.columnInt(0), query.columnText(1), query.columnText(2), query.columnText(3),
                              query.columnDouble(4), query.columnDouble(5), query.columnDouble(6)});
        }
    }
    std::vector<FleetDriver> drivers;
    {
        Statement query = db.prepare(R"SQL(
            SELECT d.user_id, u.full_name, d.licence_class FROM drivers d JOIN users u ON u.id = d.user_id
            WHERE d.warehouse_id = ?1 AND d.approval = 'approved' AND d.status = 'available' ORDER BY d.user_id)SQL");
        query.bind(1, manager.warehouseId);
        while (query.step()) drivers.push_back({query.columnInt(0), query.columnText(1), query.columnText(2)});
    }
    std::map<std::string, std::set<std::string>> goodsBodies;
    {
        Statement query = db.prepare("SELECT goods_code, body_type FROM goods_body_types");
        while (query.step()) goodsBodies[query.columnText(0)].insert(query.columnText(1));
    }

    DispatchPlan plan = planDispatch(manager.warehouseCity, orders, trucks, drivers, goodsBodies, network);

    std::map<std::int64_t, std::string> plates, driverNames;
    for (const FleetTruck& truck : trucks) plates[truck.id] = truck.plate;
    for (const FleetDriver& driver : drivers) driverNames[driver.id] = driver.name;

    Statement addTrip = db.prepare(
        "INSERT INTO trips (warehouse_id, truck_id, driver_id, route_json, distance_km, cost) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    Statement assignOrder = db.prepare("UPDATE orders SET status = 'assigned', trip_id = ?1 WHERE id = ?2 AND status = 'pending'");
    Statement busyTruck = db.prepare("UPDATE trucks SET status = 'on_trip' WHERE id = ?1");
    Statement busyDriver = db.prepare("UPDATE drivers SET status = 'on_trip' WHERE user_id = ?1");

    json assigned = json::array();
    for (const PlannedTrip& trip : plan.trips) {
        addTrip.bind(1, manager.warehouseId).bind(2, trip.truckId).bind(3, trip.driverId).bind(4, routeJson(trip.route).dump())
            .bind(5, trip.route.distance).bind(6, trip.cost).run();
        addTrip.reset();
        std::int64_t tripId = db.lastInsertId();
        assignOrder.bind(1, tripId).bind(2, trip.orderId).run();
        assignOrder.reset();
        busyTruck.bind(1, trip.truckId).run();
        busyTruck.reset();
        busyDriver.bind(1, trip.driverId).run();
        busyDriver.reset();
        assigned.push_back({{"order_id", trip.orderId}, {"trip_id", tripId}, {"truck_plate", plates[trip.truckId]},
                            {"driver_name", driverNames[trip.driverId]}, {"path", trip.route.path},
                            {"distance_km", trip.route.distance}, {"cost", trip.cost}});
    }
    tx.commit();

    json waiting = json::array();
    for (const WaitingOrder& order : plan.waiting) waiting.push_back({{"order_id", order.orderId}, {"reason", order.reason}});
    return {{"assigned", assigned}, {"waiting", waiting}};
}

json driverDashboard(Database& db, const SessionUser& driver) {
    json profile;
    {
        Statement query = db.prepare("SELECT licence_no, licence_class, status FROM drivers WHERE user_id = ?1");
        query.bind(1, driver.id);
        if (!query.step()) throw ApiError(404, "Driver profile not found");
        profile = {{"licence_no", query.columnText(0)}, {"licence_class", query.columnText(1)}, {"status", query.columnText(2)}};
    }

    json trip = nullptr;
    {
        Statement query = db.prepare(R"SQL(
            SELECT t.id, t.status, t.route_json, t.distance_km, t.dispatched_at, t.delivered_at,
                   tr.plate_no, tm.name, tm.body_type, tm.payload_kg, tm.volume_m3
            FROM trips t JOIN trucks tr ON tr.id = t.truck_id JOIN truck_models tm ON tm.id = tr.model_id
            WHERE t.driver_id = ?1 AND t.status <> 'completed'
            ORDER BY t.id DESC LIMIT 1)SQL");
        query.bind(1, driver.id);
        if (query.step()) {
            trip = {{"id", query.columnInt(0)}, {"status", query.columnText(1)}, {"route", readRoute(query.columnText(2))},
                    {"distance_km", query.columnInt(3)}, {"dispatched_at", query.columnText(4)}, {"delivered_at", textOrNull(query, 5)},
                    {"truck", {{"plate", query.columnText(6)}, {"model", query.columnText(7)}, {"body_type", query.columnText(8)},
                               {"payload_kg", query.columnDouble(9)}, {"volume_m3", query.columnDouble(10)}}},
                    {"orders", json::array()}};
        }
    }
    if (!trip.is_null()) {
        Statement query = db.prepare(R"SQL(
            SELECT o.id, g.name, g.unit, g.handling, o.quantity, o.weight_kg, o.volume_m3, o.destination_city,
                   o.delivery_address, o.receiver_name, o.receiver_phone, o.notes, c.company_name
            FROM orders o JOIN goods_types g ON g.code = o.goods_code JOIN customers c ON c.user_id = o.customer_id
            WHERE o.trip_id = ?1 ORDER BY o.id)SQL");
        query.bind(1, trip["id"].get<std::int64_t>());
        while (query.step()) {
            trip["orders"].push_back({{"id", query.columnInt(0)}, {"goods_name", query.columnText(1)}, {"unit", query.columnText(2)},
                                      {"handling", query.columnText(3)}, {"quantity", query.columnDouble(4)},
                                      {"weight_kg", query.columnDouble(5)}, {"volume_m3", query.columnDouble(6)},
                                      {"destination_city", query.columnText(7)}, {"delivery_address", query.columnText(8)},
                                      {"receiver_name", query.columnText(9)}, {"receiver_phone", textOrNull(query, 10)},
                                      {"notes", textOrNull(query, 11)}, {"customer", query.columnText(12)}});
        }
    }

    json history = json::array();
    {
        Statement query = db.prepare(R"SQL(
            SELECT t.id, t.route_json, t.distance_km, t.dispatched_at, t.delivered_at, t.completed_at, tr.plate_no
            FROM trips t JOIN trucks tr ON tr.id = t.truck_id
            WHERE t.driver_id = ?1 AND t.status = 'completed'
            ORDER BY t.id DESC LIMIT 25)SQL");
        query.bind(1, driver.id);
        while (query.step()) {
            history.push_back({{"id", query.columnInt(0)}, {"path", readRoute(query.columnText(1))["path"]},
                               {"distance_km", query.columnInt(2)}, {"dispatched_at", query.columnText(3)},
                               {"delivered_at", textOrNull(query, 4)}, {"completed_at", textOrNull(query, 5)},
                               {"truck_plate", query.columnText(6)}});
        }
    }
    return {{"user", userJson(driver)}, {"driver", profile}, {"trip", trip}, {"history", history}};
}

void markTripDelivered(Database& db, const SessionUser& driver, std::int64_t tripId) {
    Transaction tx(db);
    Statement deliver = db.prepare(
        "UPDATE trips SET status = 'returning', delivered_at = datetime('now') WHERE id = ?1 AND driver_id = ?2 AND status = 'in_transit'");
    deliver.bind(1, tripId).bind(2, driver.id).run();
    if (db.changes() == 0) throw ApiError(409, "This trip isn't yours, or its delivery was already confirmed");
    Statement orders = db.prepare("UPDATE orders SET status = 'delivered', delivered_at = datetime('now') WHERE trip_id = ?1 AND status = 'assigned'");
    orders.bind(1, tripId).run();
    tx.commit();
}

void markTripReturned(Database& db, const SessionUser& driver, std::int64_t tripId) {
    Transaction tx(db);
    Statement finish = db.prepare(
        "UPDATE trips SET status = 'completed', completed_at = datetime('now') WHERE id = ?1 AND driver_id = ?2 AND status = 'returning'");
    finish.bind(1, tripId).bind(2, driver.id).run();
    if (db.changes() == 0) throw ApiError(409, "Confirm the delivery first; only then can the truck return");
    Statement freeTruck = db.prepare("UPDATE trucks SET status = 'available' WHERE id = (SELECT truck_id FROM trips WHERE id = ?1)");
    freeTruck.bind(1, tripId).run();
    Statement freeDriver = db.prepare("UPDATE drivers SET status = 'available' WHERE user_id = ?1");
    freeDriver.bind(1, driver.id).run();
    tx.commit();
}
