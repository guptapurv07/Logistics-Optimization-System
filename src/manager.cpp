#include "../include/manager.h"
#include "../include/api.h"
#include <cctype>
#include <regex>

using json = nlohmann::json;

std::string normalisePlate(const std::string& text) {
    std::string compact;
    for (char c : text) {
        if (c == ' ' || c == '-') continue;
        compact += char(std::toupper(static_cast<unsigned char>(c)));
    }
    // State (2 letters), district (1-2 digits), series (0-3 letters), number (1-4 digits)
    static const std::regex plate("^([A-Z]{2})([0-9]{1,2})([A-Z]{0,3})([0-9]{1,4})$");
    std::smatch parts;
    if (!std::regex_match(compact, parts, plate)) return "";
    std::string district = parts[2].str();
    std::string number = parts[4].str();
    if (district.size() == 1) district = "0" + district;
    number = std::string(4 - number.size(), '0') + number;
    return parts[1].str() + " " + district + (parts[3].length() ? " " + parts[3].str() : "") + " " + number;
}

json managerDashboard(Database& db, const SessionUser& manager) {
    const std::int64_t warehouseId = manager.warehouseId;

    json warehouse;
    {
        Statement query = db.prepare("SELECT code, name, city, state, address FROM warehouses WHERE id = ?1");
        query.bind(1, warehouseId);
        if (!query.step()) throw ApiError(404, "Warehouse not found");
        warehouse = {{"id", warehouseId}, {"code", query.columnText(0)}, {"name", query.columnText(1)},
                     {"city", query.columnText(2)}, {"state", query.columnText(3)}, {"address", query.columnText(4)}};
    }

    json counts;
    {
        Statement query = db.prepare(R"SQL(
            SELECT (SELECT COUNT(*) FROM orders WHERE warehouse_id = ?1 AND status = 'pending'),
                   (SELECT COUNT(*) FROM orders WHERE warehouse_id = ?1 AND status = 'delivered'),
                   (SELECT COUNT(*) FROM trips WHERE warehouse_id = ?1 AND status <> 'completed'),
                   (SELECT COUNT(*) FROM trucks WHERE warehouse_id = ?1),
                   (SELECT COUNT(*) FROM trucks WHERE warehouse_id = ?1 AND status = 'available'),
                   (SELECT COUNT(*) FROM drivers WHERE warehouse_id = ?1 AND approval = 'approved'),
                   (SELECT COUNT(*) FROM drivers WHERE warehouse_id = ?1 AND approval = 'approved' AND status = 'available'),
                   (SELECT COUNT(*) FROM drivers WHERE warehouse_id = ?1 AND approval = 'pending'))SQL");
        query.bind(1, warehouseId);
        query.step();
        counts = {{"orders_pending", query.columnInt(0)}, {"orders_delivered", query.columnInt(1)},
                  {"trips_active", query.columnInt(2)}, {"trucks_total", query.columnInt(3)}, {"trucks_free", query.columnInt(4)},
                  {"drivers_total", query.columnInt(5)}, {"drivers_free", query.columnInt(6)}, {"drivers_pending", query.columnInt(7)}};
    }

    // Waiting orders first, then those on the road, then finished ones; newest first within each group
    json orders = json::array();
    {
        Statement query = db.prepare(R"SQL(
            SELECT o.id, o.status, g.name, g.unit, o.quantity, o.weight_kg, o.volume_m3, o.destination_city,
                   o.created_at, o.delivered_at, c.company_name, o.trip_id, tr.plate_no, u.full_name
            FROM orders o
            JOIN goods_types g ON g.code = o.goods_code
            JOIN customers c ON c.user_id = o.customer_id
            LEFT JOIN trips t ON t.id = o.trip_id
            LEFT JOIN trucks tr ON tr.id = t.truck_id
            LEFT JOIN users u ON u.id = t.driver_id
            WHERE o.warehouse_id = ?1
            ORDER BY CASE o.status WHEN 'pending' THEN 0 WHEN 'assigned' THEN 1 ELSE 2 END, o.id DESC
            LIMIT 500)SQL");
        query.bind(1, warehouseId);
        while (query.step()) {
            orders.push_back({{"id", query.columnInt(0)}, {"status", query.columnText(1)}, {"goods_name", query.columnText(2)},
                              {"unit", query.columnText(3)}, {"quantity", query.columnDouble(4)}, {"weight_kg", query.columnDouble(5)},
                              {"volume_m3", query.columnDouble(6)}, {"destination_city", query.columnText(7)},
                              {"created_at", query.columnText(8)}, {"delivered_at", textOrNull(query, 9)},
                              {"customer", query.columnText(10)},
                              {"trip_id", query.columnIsNull(11) ? json() : json(query.columnInt(11))},
                              {"truck_plate", textOrNull(query, 12)}, {"driver_name", textOrNull(query, 13)}});
        }
    }

    json activeTrips = json::array(), ledger = json::array();
    {
        Statement query = db.prepare(R"SQL(
            SELECT t.id, t.status, t.route_json, t.distance_km, t.cost, t.dispatched_at, t.delivered_at, t.completed_at,
                   tr.plate_no, tm.name, u.full_name,
                   (SELECT COUNT(*) FROM orders WHERE trip_id = t.id),
                   (SELECT COALESCE(SUM(weight_kg), 0) FROM orders WHERE trip_id = t.id)
            FROM trips t
            JOIN trucks tr ON tr.id = t.truck_id
            JOIN truck_models tm ON tm.id = tr.model_id
            JOIN users u ON u.id = t.driver_id
            WHERE t.warehouse_id = ?1
            ORDER BY t.id DESC LIMIT 300)SQL");
        query.bind(1, warehouseId);
        while (query.step()) {
            json route = json::parse(query.columnText(2), nullptr, false);
            if (route.is_discarded()) route = {{"path", json::array()}, {"legs", json::array()}};
            json trip = {{"id", query.columnInt(0)}, {"status", query.columnText(1)}, {"route", route},
                         {"distance_km", query.columnInt(3)}, {"cost", query.columnDouble(4)},
                         {"dispatched_at", query.columnText(5)}, {"delivered_at", textOrNull(query, 6)},
                         {"completed_at", textOrNull(query, 7)}, {"truck_plate", query.columnText(8)},
                         {"truck_model", query.columnText(9)}, {"driver_name", query.columnText(10)},
                         {"order_count", query.columnInt(11)}, {"weight_kg", query.columnDouble(12)}};
            (trip["status"] == "completed" ? ledger : activeTrips).push_back(trip);
        }
    }

    json trucks = json::array();
    {
        Statement query = db.prepare(R"SQL(
            SELECT t.id, t.plate_no, t.status, m.name, m.body_type, m.payload_kg, m.volume_m3, m.licence_needed, m.cost_per_km
            FROM trucks t JOIN truck_models m ON m.id = t.model_id
            WHERE t.warehouse_id = ?1 ORDER BY t.id)SQL");
        query.bind(1, warehouseId);
        while (query.step()) {
            trucks.push_back({{"id", query.columnInt(0)}, {"plate", query.columnText(1)}, {"status", query.columnText(2)},
                              {"model", query.columnText(3)}, {"body_type", query.columnText(4)},
                              {"payload_kg", query.columnDouble(5)}, {"volume_m3", query.columnDouble(6)},
                              {"licence_needed", query.columnText(7)}, {"cost_per_km", query.columnDouble(8)}});
        }
    }

    json drivers = json::array();
    {
        Statement query = db.prepare(R"SQL(
            SELECT d.user_id, u.full_name, u.username, d.licence_no, d.licence_class, d.approval, d.status, u.created_at
            FROM drivers d JOIN users u ON u.id = d.user_id
            WHERE d.warehouse_id = ?1
            ORDER BY CASE d.approval WHEN 'pending' THEN 0 WHEN 'approved' THEN 1 ELSE 2 END, u.full_name)SQL");
        query.bind(1, warehouseId);
        while (query.step()) {
            drivers.push_back({{"id", query.columnInt(0)}, {"name", query.columnText(1)}, {"username", query.columnText(2)},
                               {"licence_no", query.columnText(3)}, {"licence_class", query.columnText(4)},
                               {"approval", query.columnText(5)}, {"status", query.columnText(6)}, {"applied_at", query.columnText(7)}});
        }
    }

    return {{"user", userJson(manager)}, {"warehouse", warehouse}, {"counts", counts}, {"orders", orders},
            {"active_trips", activeTrips}, {"ledger", ledger}, {"trucks", trucks}, {"drivers", drivers}};
}

void decideDriver(Database& db, const SessionUser& manager, std::int64_t driverId, bool approve) {
    Statement decide = db.prepare(
        "UPDATE drivers SET approval = ?1 WHERE user_id = ?2 AND warehouse_id = ?3 AND approval = 'pending'");
    decide.bind(1, std::string(approve ? "approved" : "rejected")).bind(2, driverId).bind(3, manager.warehouseId).run();
    if (db.changes() == 0) throw ApiError(404, "There is no pending application from that driver at your warehouse");
}

json addTruck(Database& db, const SessionUser& manager, const json& body) {
    Fields fields(body);
    std::int64_t modelId = fields.id("model_id", "Truck model");
    std::string plate = normalisePlate(fields.text("plate_no", "Number plate", 1, 20));
    if (plate.empty()) throw ApiError(400, "The number plate should look like MH 01 AB 1234");

    {
        Statement model = db.prepare("SELECT 1 FROM truck_models WHERE id = ?1");
        model.bind(1, modelId);
        if (!model.step()) throw ApiError(400, "Choose a truck model from the list");
    }
    {
        Statement existing = db.prepare("SELECT 1 FROM trucks WHERE plate_no = ?1");
        existing.bind(1, plate);
        if (existing.step()) throw ApiError(409, "A truck with plate " + plate + " is already registered");
    }
    Statement add = db.prepare("INSERT INTO trucks (warehouse_id, model_id, plate_no) VALUES (?1, ?2, ?3)");
    add.bind(1, manager.warehouseId).bind(2, modelId).bind(3, plate).run();
    return {{"id", db.lastInsertId()}, {"plate", plate}};
}
