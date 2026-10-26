#include "../include/dispatch.h"
#include "../include/api.h"
#include "../include/pricing.h"
#include <algorithm>
#include <cstdio>

using json = nlohmann::json;

namespace {

bool canDrive(const FleetDriver& driver, const FleetTruck& truck) {
    return driver.licenceClass == "HMV" || truck.licenceNeeded == "LMV";
}

// On a profit tie: the cheaper truck per km, then the smaller one (keeping big trucks free), then the lower id
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

// Everything one truck would carry on a trip
struct Load {
    std::vector<const PendingOrder*> orders;
    std::string lastCity;  // the drop-off furthest from the warehouse
    double kg = 0, m3 = 0, revenue = 0;
    double saved = 0;      // what the orders taken along would have cost on trucks of their own

    void add(const PendingOrder* order) {
        orders.push_back(order);
        kg += order->weightKg;
        m3 += order->volumeM3;
        revenue += order->price;
    }
};

}  // namespace

json readRoute(const std::string& text) {
    json route = json::parse(text, nullptr, false);
    if (route.is_discarded() || !route.is_object() || !route.contains("path") || !route["path"].is_array() ||
        !route.contains("legs") || !route["legs"].is_array()) {
        return {{"path", json::array()}, {"legs", json::array()}};
    }
    return route;
}

DispatchPlan planDispatch(const std::string& warehouseCity, const std::vector<PendingOrder>& orders,
                          const std::vector<FleetTruck>& freeTrucks, const std::vector<FleetDriver>& freeDrivers,
                          const std::map<std::string, std::set<std::string>>& goodsBodies, const GoodsPairs& mayShare,
                          const RoadNetwork& network) {
    DispatchPlan plan;
    std::vector<bool> truckTaken(freeTrucks.size(), false);
    std::vector<bool> driverTaken(freeDrivers.size(), false);
    std::set<std::int64_t> loaded;  // orders already on a planned trip
    static const std::set<std::string> kNoBodies;

    // One Dijkstra run per delivery city. Every run starts at the warehouse and builds the same tree of shortest
    // roads, so a city that appears on the route to another city lies on the way there.
    std::map<std::string, RouteResult> routes;
    auto routeTo = [&](const std::string& city) -> const RouteResult& {
        auto known = routes.find(city);
        if (known == routes.end()) known = routes.emplace(city, runDijkstraRouting(warehouseCity, city, network)).first;
        return known->second;
    };
    auto onTheWayTo = [&](const std::string& city, const std::string& destination) {
        const std::vector<std::string>& path = routeTo(destination).path;
        return std::find(path.begin(), path.end(), city) != path.end();
    };
    auto bodiesFor = [&](const std::string& goodsCode) -> const std::set<std::string>& {
        auto allowed = goodsBodies.find(goodsCode);
        return allowed != goodsBodies.end() ? allowed->second : kNoBodies;
    };
    auto hasFreeDriver = [&](const FleetTruck& truck) {
        for (std::size_t d = 0; d < freeDrivers.size(); ++d) {
            if (!driverTaken[d] && canDrive(freeDrivers[d], truck)) return true;
        }
        return false;
    };

    // Oldest first (ids grow with time). The caller's list is left as it is: only pointers are sorted.
    std::vector<const PendingOrder*> queue;
    for (const PendingOrder& order : orders) queue.push_back(&order);
    std::sort(queue.begin(), queue.end(), [](const PendingOrder* a, const PendingOrder* b) { return a->id < b->id; });

    // A trip's cost: the truck's running cost and the driver's pay, both for the km there and back
    auto tripCost = [&](const FleetTruck& truck, int km) { return truck.costPerKm * 2.0 * km + driverPay(km); };

    // What an order would cost on a truck of its own: the cheapest free truck that may carry it alone (drivers aside).
    // An order no free truck can carry alone counts as very expensive, so any trip that can take it along does.
    const double kNoTruckAlone = 1e9;
    std::map<std::int64_t, double> aloneCosts;  // worked out again after each planned trip, as trucks get taken
    auto aloneCost = [&](const PendingOrder& order) {
        auto known = aloneCosts.find(order.id);
        if (known != aloneCosts.end()) return known->second;
        double cheapest = kNoTruckAlone;
        for (std::size_t t = 0; t < freeTrucks.size(); ++t) {
            const FleetTruck& truck = freeTrucks[t];
            if (truckTaken[t] || !bodiesFor(order.goodsCode).count(truck.bodyType) || truck.payloadKg < order.weightKg ||
                truck.volumeM3 < order.volumeM3) {
                continue;
            }
            cheapest = std::min(cheapest, tripCost(truck, routeTo(order.destination).distance));
        }
        return aloneCosts[order.id] = cheapest;
    };

    // The first order, then every other waiting order (oldest first) that this truck can take along. A city on the
    // way is always worth a stop; going further down the road only when the extra km cost less than a truck of its own.
    auto fill = [&](const PendingOrder& first, const FleetTruck& truck) {
        Load load;
        load.lastCity = first.destination;
        load.add(&first);
        for (const PendingOrder* other : queue) {
            if (other == &first || loaded.count(other->id) || !routeTo(other->destination).reachable) continue;
            if (!bodiesFor(other->goodsCode).count(truck.bodyType)) continue;
            if (load.kg + other->weightKg > truck.payloadKg || load.m3 + other->volumeM3 > truck.volumeM3) continue;
            bool shares = std::all_of(load.orders.begin(), load.orders.end(), [&](const PendingOrder* onBoard) {
                return mayShare.count({onBoard->goodsCode, other->goodsCode}) > 0;
            });
            if (!shares) continue;
            if (onTheWayTo(other->destination, load.lastCity)) {
                load.add(other);  // dropped off on the way
                load.saved += aloneCost(*other);
            } else if (onTheWayTo(load.lastCity, other->destination)) {
                double extra = tripCost(truck, routeTo(other->destination).distance) - tripCost(truck, routeTo(load.lastCity).distance);
                if (extra >= aloneCost(*other)) continue;
                load.lastCity = other->destination;  // the trip goes further; every drop-off so far is on the way there
                load.add(other);
                load.saved += aloneCost(*other);
            }
        }
        return load;
    };

    for (const PendingOrder* order : queue) {
        if (loaded.count(order->id)) continue;
        if (!routeTo(order->destination).reachable) {
            plan.waiting.push_back({order->id, "No road from " + warehouseCity + " to " + order->destination});
            continue;
        }
        const std::set<std::string>& bodies = bodiesFor(order->goodsCode);

        // Each truck that may carry this order is filled; the one that saves the most wins: the costs of the orders it
        // takes along minus its own trip's cost
        std::size_t best = freeTrucks.size();
        Load bestLoad;
        double bestSaving = 0;
        bool fitsButNoDriver = false;
        for (std::size_t t = 0; t < freeTrucks.size(); ++t) {
            const FleetTruck& truck = freeTrucks[t];
            if (truckTaken[t] || !bodies.count(truck.bodyType) || truck.payloadKg < order->weightKg ||
                truck.volumeM3 < order->volumeM3) {
                continue;
            }
            if (!hasFreeDriver(truck)) {
                fitsButNoDriver = true;
                continue;
            }
            Load load = fill(*order, truck);
            double saving = load.saved - tripCost(truck, routeTo(load.lastCity).distance);
            if (best == freeTrucks.size() || saving > bestSaving || (saving == bestSaving && betterTruck(truck, freeTrucks[best]))) {
                best = t;
                bestLoad = load;
                bestSaving = saving;
            }
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
        aloneCosts.clear();

        // Drop-offs in the order the truck reaches them, oldest order first within one city
        const RouteResult& route = routeTo(bestLoad.lastCity);
        auto position = [&route](const std::string& city) { return std::find(route.path.begin(), route.path.end(), city) - route.path.begin(); };
        std::vector<const PendingOrder*> drops = bestLoad.orders;
        std::sort(drops.begin(), drops.end(), [&](const PendingOrder* a, const PendingOrder* b) {
            auto first = position(a->destination), second = position(b->destination);
            return first != second ? first < second : a->id < b->id;
        });

        PlannedTrip trip{{}, truck.id, freeDrivers[chosen].id, route, bestLoad.revenue,
                         truck.costPerKm * 2.0 * route.distance, driverPay(route.distance)};
        for (const PendingOrder* drop : drops) {
            trip.orderIds.push_back(drop->id);
            loaded.insert(drop->id);
        }
        plan.trips.push_back(trip);
    }

    // An order that found no truck on its own turn may have joined a later order's truck
    plan.waiting.erase(std::remove_if(plan.waiting.begin(), plan.waiting.end(),
                                      [&](const WaitingOrder& waiting) { return loaded.count(waiting.orderId) > 0; }),
                       plan.waiting.end());
    return plan;
}

json dispatchWarehouse(Database& db, const SessionUser& manager, const RoadNetwork& network) {
    // BEGIN IMMEDIATE: a second dispatch at the same moment waits here, then sees these trucks as busy
    Transaction tx(db);

    std::vector<PendingOrder> orders;
    {
        Statement query = db.prepare(R"SQL(
            SELECT id, goods_code, weight_kg, volume_m3, destination_city, COALESCE(price, 0)
            FROM orders WHERE warehouse_id = ?1 AND status = 'pending' ORDER BY id)SQL");
        query.bind(1, manager.warehouseId);
        while (query.step()) {
            orders.push_back({query.columnInt(0), query.columnText(1), query.columnDouble(2), query.columnDouble(3),
                              query.columnText(4), query.columnDouble(5)});
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
    GoodsPairs mayShare;
    {
        Statement query = db.prepare("SELECT goods_a, goods_b FROM goods_compatibility");
        while (query.step()) mayShare.insert({query.columnText(0), query.columnText(1)});
    }

    DispatchPlan plan = planDispatch(manager.warehouseCity, orders, trucks, drivers, goodsBodies, mayShare, network);

    std::map<std::int64_t, std::string> plates, driverNames, destinations;
    for (const FleetTruck& truck : trucks) plates[truck.id] = truck.plate;
    for (const FleetDriver& driver : drivers) driverNames[driver.id] = driver.name;
    for (const PendingOrder& order : orders) destinations[order.id] = order.destination;

    Statement addTrip = db.prepare(R"SQL(
        INSERT INTO trips (warehouse_id, truck_id, driver_id, route_json, distance_km, cost, driver_pay)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7))SQL");
    Statement assignOrder = db.prepare("UPDATE orders SET status = 'assigned', trip_id = ?1 WHERE id = ?2 AND status = 'pending'");
    Statement busyTruck = db.prepare("UPDATE trucks SET status = 'on_trip' WHERE id = ?1");
    Statement busyDriver = db.prepare("UPDATE drivers SET status = 'on_trip' WHERE user_id = ?1");

    json trips = json::array();
    for (const PlannedTrip& trip : plan.trips) {
        addTrip.bind(1, manager.warehouseId).bind(2, trip.truckId).bind(3, trip.driverId).bind(4, routeJson(trip.route).dump())
            .bind(5, trip.route.distance).bind(6, trip.cost).bind(7, trip.driverPay).run();
        addTrip.reset();
        std::int64_t tripId = db.lastInsertId();
        json stops = json::array();
        for (std::int64_t orderId : trip.orderIds) {
            assignOrder.bind(1, tripId).bind(2, orderId).run();
            assignOrder.reset();
            const std::string& city = destinations[orderId];
            if (stops.empty() || stops.back()["city"] != city) stops.push_back({{"city", city}, {"order_ids", json::array()}});
            stops.back()["order_ids"].push_back(orderId);
        }
        busyTruck.bind(1, trip.truckId).run();
        busyTruck.reset();
        busyDriver.bind(1, trip.driverId).run();
        busyDriver.reset();
        trips.push_back({{"trip_id", tripId}, {"truck_plate", plates[trip.truckId]}, {"driver_name", driverNames[trip.driverId]},
                         {"path", trip.route.path}, {"distance_km", trip.route.distance}, {"stops", stops},
                         {"revenue", trip.revenue}, {"cost", trip.cost}, {"driver_pay", trip.driverPay}, {"profit", trip.profit()}});
    }
    tx.commit();

    json waiting = json::array();
    for (const WaitingOrder& order : plan.waiting) waiting.push_back({{"order_id", order.orderId}, {"reason", order.reason}});
    return {{"trips", trips}, {"waiting", waiting}};
}

json tripStops(const json& route, const json& orders) {
    // km from the warehouse to each city on the route, adding up its legs
    std::map<std::string, int> kmTo;
    std::vector<std::string> cities;
    if (!route["path"].empty()) kmTo[route["path"][0].get<std::string>()] = 0;
    int km = 0;
    for (const json& leg : route["legs"]) {
        km += leg.value("km", 0);
        kmTo[leg.value("to", "")] = km;
    }
    for (const json& city : route["path"]) cities.push_back(city.get<std::string>());
    // A city that isn't on the stored route (a damaged route) still gets its stop, at the end
    for (const json& order : orders) {
        std::string city = order["destination_city"].get<std::string>();
        if (std::find(cities.begin(), cities.end(), city) == cities.end()) cities.push_back(city);
    }

    json stops = json::array();
    for (const std::string& city : cities) {
        json here = json::array();
        bool allDelivered = true;
        std::string lastDelivery;
        for (const json& order : orders) {
            if (order["destination_city"] != city) continue;
            here.push_back(order);
            if (order["delivered_at"].is_null()) allDelivered = false;
            else lastDelivery = std::max(lastDelivery, order["delivered_at"].get<std::string>());
        }
        if (here.empty()) continue;
        stops.push_back({{"city", city}, {"km", kmTo.count(city) ? json(kmTo[city]) : json()}, {"orders", here},
                         {"delivered_at", allDelivered ? json(lastDelivery) : json()}});
    }
    return stops;
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
                   tr.plate_no, tm.name, tm.body_type, tm.payload_kg, tm.volume_m3, COALESCE(t.driver_pay, 0)
            FROM trips t JOIN trucks tr ON tr.id = t.truck_id JOIN truck_models tm ON tm.id = tr.model_id
            WHERE t.driver_id = ?1 AND t.status <> 'completed'
            ORDER BY t.id DESC LIMIT 1)SQL");
        query.bind(1, driver.id);
        if (query.step()) {
            trip = {{"id", query.columnInt(0)}, {"status", query.columnText(1)}, {"route", readRoute(query.columnText(2))},
                    {"distance_km", query.columnInt(3)}, {"dispatched_at", query.columnText(4)}, {"delivered_at", textOrNull(query, 5)},
                    {"truck", {{"plate", query.columnText(6)}, {"model", query.columnText(7)}, {"body_type", query.columnText(8)},
                               {"payload_kg", query.columnDouble(9)}, {"volume_m3", query.columnDouble(10)}}},
                    {"driver_pay", query.columnDouble(11)}, {"orders", json::array()}};
        }
    }
    if (!trip.is_null()) {
        Statement query = db.prepare(R"SQL(
            SELECT o.id, g.name, g.unit, g.handling, o.quantity, o.weight_kg, o.volume_m3, o.destination_city,
                   o.delivery_address, o.receiver_name, o.receiver_phone, o.notes, c.company_name, o.status, o.delivered_at
            FROM orders o JOIN goods_types g ON g.code = o.goods_code JOIN customers c ON c.user_id = o.customer_id
            WHERE o.trip_id = ?1 ORDER BY o.id)SQL");
        query.bind(1, trip["id"].get<std::int64_t>());
        while (query.step()) {
            trip["orders"].push_back({{"id", query.columnInt(0)}, {"goods_name", query.columnText(1)}, {"unit", query.columnText(2)},
                                      {"handling", query.columnText(3)}, {"quantity", query.columnDouble(4)},
                                      {"weight_kg", query.columnDouble(5)}, {"volume_m3", query.columnDouble(6)},
                                      {"destination_city", query.columnText(7)}, {"delivery_address", query.columnText(8)},
                                      {"receiver_name", query.columnText(9)}, {"receiver_phone", textOrNull(query, 10)},
                                      {"notes", textOrNull(query, 11)}, {"customer", query.columnText(12)},
                                      {"status", query.columnText(13)}, {"delivered_at", textOrNull(query, 14)}});
        }
        trip["stops"] = tripStops(trip["route"], trip["orders"]);
    }

    json history = json::array();
    {
        Statement query = db.prepare(R"SQL(
            SELECT t.id, t.route_json, t.distance_km, t.dispatched_at, t.delivered_at, t.completed_at, tr.plate_no,
                   COALESCE(t.driver_pay, 0), (SELECT COUNT(*) FROM orders WHERE trip_id = t.id)
            FROM trips t JOIN trucks tr ON tr.id = t.truck_id
            WHERE t.driver_id = ?1 AND t.status = 'completed'
            ORDER BY t.id DESC LIMIT 25)SQL");
        query.bind(1, driver.id);
        while (query.step()) {
            history.push_back({{"id", query.columnInt(0)}, {"path", readRoute(query.columnText(1))["path"]},
                               {"distance_km", query.columnInt(2)}, {"dispatched_at", query.columnText(3)},
                               {"delivered_at", textOrNull(query, 4)}, {"completed_at", textOrNull(query, 5)},
                               {"truck_plate", query.columnText(6)}, {"driver_pay", query.columnDouble(7)},
                               {"order_count", query.columnInt(8)}});
        }
    }

    // Pay is earned when a trip is finished (back at the warehouse); "this month" follows this computer's clock
    json earnings;
    {
        Statement query = db.prepare(R"SQL(
            SELECT COALESCE(SUM(driver_pay), 0), COUNT(*),
                   COALESCE(SUM(CASE WHEN strftime('%Y-%m', completed_at, 'localtime') = strftime('%Y-%m', 'now', 'localtime') THEN driver_pay END), 0),
                   COUNT(CASE WHEN strftime('%Y-%m', completed_at, 'localtime') = strftime('%Y-%m', 'now', 'localtime') THEN 1 END)
            FROM trips WHERE driver_id = ?1 AND status = 'completed')SQL");
        query.bind(1, driver.id);
        query.step();
        earnings = {{"total", query.columnDouble(0)}, {"total_trips", query.columnInt(1)}, {"month", query.columnDouble(2)},
                    {"month_trips", query.columnInt(3)}, {"rate_per_km", kDriverPayPerKm}};
    }
    return {{"user", userJson(driver)}, {"driver", profile}, {"trip", trip}, {"history", history}, {"earnings", earnings}};
}

void markStopDelivered(Database& db, const SessionUser& driver, std::int64_t tripId, const std::string& city) {
    Transaction tx(db);
    json route;
    {
        Statement trip = db.prepare("SELECT route_json FROM trips WHERE id = ?1 AND driver_id = ?2 AND status = 'in_transit'");
        trip.bind(1, tripId).bind(2, driver.id);
        if (!trip.step()) throw ApiError(409, "This trip isn't yours, or all its goods were already delivered");
        route = readRoute(trip.columnText(0));
    }
    std::vector<std::string> onBoard;  // cities whose goods are still on the truck
    {
        Statement query = db.prepare("SELECT DISTINCT destination_city FROM orders WHERE trip_id = ?1 AND status = 'assigned'");
        query.bind(1, tripId);
        while (query.step()) onBoard.push_back(query.columnText(0));
    }

    if (!onBoard.empty()) {
        std::string stop = city;
        if (stop.empty()) {
            // The next drop-off: the first city along the route that still has goods for it
            stop = onBoard.front();
            for (const json& place : route["path"]) {
                if (std::find(onBoard.begin(), onBoard.end(), place.get<std::string>()) != onBoard.end()) {
                    stop = place.get<std::string>();
                    break;
                }
            }
        }
        Statement deliver = db.prepare(
            "UPDATE orders SET status = 'delivered', delivered_at = datetime('now') WHERE trip_id = ?1 AND destination_city = ?2 AND status = 'assigned'");
        deliver.bind(1, tripId).bind(2, stop).run();
        if (db.changes() == 0) throw ApiError(409, "There are no goods for " + stop + " left on this trip");
        if (onBoard.size() > 1) {
            tx.commit();
            return;  // more drop-offs to go
        }
    }
    // That was the last drop-off: the truck heads back to the warehouse
    Statement returning = db.prepare("UPDATE trips SET status = 'returning', delivered_at = datetime('now') WHERE id = ?1");
    returning.bind(1, tripId).run();
    tx.commit();
}

void markTripReturned(Database& db, const SessionUser& driver, std::int64_t tripId) {
    Transaction tx(db);
    Statement finish = db.prepare(
        "UPDATE trips SET status = 'completed', completed_at = datetime('now') WHERE id = ?1 AND driver_id = ?2 AND status = 'returning'");
    finish.bind(1, tripId).bind(2, driver.id).run();
    if (db.changes() == 0) throw ApiError(409, "Confirm the deliveries first; only then can the truck return");
    Statement freeTruck = db.prepare("UPDATE trucks SET status = 'available' WHERE id = (SELECT truck_id FROM trips WHERE id = ?1)");
    freeTruck.bind(1, tripId).run();
    Statement freeDriver = db.prepare("UPDATE drivers SET status = 'available' WHERE user_id = ?1");
    freeDriver.bind(1, driver.id).run();
    tx.commit();
}
