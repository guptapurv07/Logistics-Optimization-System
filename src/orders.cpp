#include "../include/orders.h"
#include "../include/api.h"
#include <cmath>
#include <sstream>

using json = nlohmann::json;

namespace {

// "360 bags", "12.5 tonnes": whole units for countable goods, one decimal for tonnes
std::string describeQuantity(double quantity, const std::string& unit) {
    std::ostringstream text;
    if (unit == "tonne") {
        text << std::floor(quantity * 10) / 10;
    } else {
        text << static_cast<long long>(std::floor(quantity));
    }
    text << " " << unit << (quantity == 1 ? "" : "s");
    return text.str();
}

}  // namespace

json placeOrder(Database& db, const SessionUser& customer, const json& body, const RoadNetwork& network) {
    Fields fields(body);
    std::int64_t warehouseId = fields.id("warehouse_id", "Warehouse");
    std::string goodsCode = fields.text("goods_code", "Goods type", 1, 20);
    double quantity = fields.number("quantity", "Quantity");
    std::string typedCity = fields.text("destination_city", "Destination city", 1, 60);
    std::string address = fields.text("delivery_address", "Delivery address", 5, 200);
    std::string receiver = fields.text("receiver_name", "Receiver name", 2, 80);
    std::string phone = checkPhone(fields.optionalText("receiver_phone", "Receiver phone", 20), "Receiver phone");
    std::string notes = fields.optionalText("notes", "Notes", 300);

    std::string warehouseCode, warehouseCity;
    {
        Statement warehouse = db.prepare("SELECT code, city FROM warehouses WHERE id = ?1");
        warehouse.bind(1, warehouseId);
        if (!warehouse.step()) throw ApiError(400, "Choose a warehouse from the list");
        warehouseCode = warehouse.columnText(0);
        warehouseCity = warehouse.columnText(1);
    }

    std::string goodsName, unit;
    double kgPerUnit, m3PerUnit;
    {
        Statement goods = db.prepare("SELECT name, unit, kg_per_unit, m3_per_unit FROM goods_types WHERE code = ?1");
        goods.bind(1, goodsCode);
        if (!goods.step()) throw ApiError(400, "Choose a goods type from the list");
        goodsName = goods.columnText(0);
        unit = goods.columnText(1);
        kgPerUnit = goods.columnDouble(2);
        m3PerUnit = goods.columnDouble(3);
    }

    if (quantity <= 0 || quantity > 100000) throw ApiError(400, "Quantity must be more than 0 and at most 100,000");
    if (unit != "tonne" && std::floor(quantity) != quantity) throw ApiError(400, "Enter a whole number of " + unit + "s");

    std::string destination = findCity(typedCity, network);
    if (destination.empty()) throw ApiError(400, "We don't deliver to '" + typedCity + "' yet. Choose a city from the list");
    if (destination == warehouseCity) {
        throw ApiError(400, warehouseCode + " is in " + destination + " itself. Choose a destination in another city");
    }
    RouteResult route = runDijkstraRouting(warehouseCity, destination, network);
    if (!route.reachable) throw ApiError(400, "There is no road from " + warehouseCity + " to " + destination);

    double weightKg = quantity * kgPerUnit;
    double volumeM3 = quantity * m3PerUnit;

    // The largest order of these goods that one truck of this warehouse can take, in the goods' own unit:
    // limited by weight for heavy goods (cement, sand) and by space for bulky ones (cloth)
    {
        Statement capacity = db.prepare(R"SQL(
            SELECT MAX(MIN(m.payload_kg / ?2, m.volume_m3 / ?3))
            FROM trucks t
            JOIN truck_models m ON m.id = t.model_id
            JOIN goods_body_types g ON g.body_type = m.body_type AND g.goods_code = ?4
            WHERE t.warehouse_id = ?1 AND t.status <> 'maintenance')SQL");
        capacity.bind(1, warehouseId).bind(2, kgPerUnit).bind(3, m3PerUnit).bind(4, goodsCode);
        capacity.step();
        if (capacity.columnIsNull(0)) {
            throw ApiError(400, warehouseCode + " has no truck that may carry " + goodsName + " yet. Choose another warehouse");
        }
        double maxUnits = capacity.columnDouble(0);
        if (quantity > maxUnits + 1e-9) {
            throw ApiError(400, "The biggest suitable truck at " + warehouseCode + " carries up to " + describeQuantity(maxUnits, unit) +
                                    " of " + goodsName + ". Please split this into smaller orders.");
        }
    }

    Statement add = db.prepare(R"SQL(
        INSERT INTO orders (customer_id, warehouse_id, goods_code, quantity, weight_kg, volume_m3,
                            destination_city, delivery_address, receiver_name, receiver_phone, notes)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11))SQL");
    add.bind(1, customer.id).bind(2, warehouseId).bind(3, goodsCode).bind(4, quantity).bind(5, weightKg).bind(6, volumeM3)
        .bind(7, destination).bind(8, address).bind(9, receiver);
    if (phone.empty()) add.bindNull(10);
    else add.bind(10, phone);
    if (notes.empty()) add.bindNull(11);
    else add.bind(11, notes);
    add.run();

    return {{"id", db.lastInsertId()}, {"status", "pending"}, {"weight_kg", weightKg}, {"volume_m3", volumeM3},
            {"destination_city", destination}, {"distance_km", route.distance}, {"path", route.path}};
}

void cancelOrder(Database& db, const SessionUser& user, std::int64_t orderId) {
    bool customer = user.role == "customer";
    Statement cancel = db.prepare(customer
        ? "UPDATE orders SET status = 'cancelled' WHERE id = ?1 AND customer_id = ?2 AND status = 'pending'"
        : "UPDATE orders SET status = 'cancelled' WHERE id = ?1 AND warehouse_id = ?2 AND status = 'pending'");
    cancel.bind(1, orderId).bind(2, customer ? user.id : user.warehouseId).run();
    if (db.changes() == 0) {
        throw ApiError(409, customer ? "Only your own orders that are still waiting for a truck can be cancelled"
                                     : "Only orders of your warehouse that are still waiting for a truck can be cancelled");
    }
}

json customerDashboard(Database& db, const SessionUser& customer) {
    json company = nullptr;
    {
        Statement profile = db.prepare("SELECT company_name, city, address FROM customers WHERE user_id = ?1");
        profile.bind(1, customer.id);
        if (profile.step()) {
            company = {{"name", profile.columnText(0)}, {"city", profile.columnText(1)}, {"address", profile.columnText(2)}};
        }
    }

    json orders = json::array();
    Statement query = db.prepare(R"SQL(
        SELECT o.id, o.status, o.goods_code, g.name, g.unit, o.quantity, o.weight_kg, o.volume_m3,
               o.destination_city, o.delivery_address, o.receiver_name, o.receiver_phone, o.notes, o.created_at, o.delivered_at,
               w.code, w.name, w.city,
               t.id, t.status, t.dispatched_at, t.distance_km, tr.plate_no, tm.name, u.full_name
        FROM orders o
        JOIN goods_types g ON g.code = o.goods_code
        JOIN warehouses w ON w.id = o.warehouse_id
        LEFT JOIN trips t ON t.id = o.trip_id
        LEFT JOIN trucks tr ON tr.id = t.truck_id
        LEFT JOIN truck_models tm ON tm.id = tr.model_id
        LEFT JOIN users u ON u.id = t.driver_id
        WHERE o.customer_id = ?1
        ORDER BY o.id DESC)SQL");
    query.bind(1, customer.id);
    while (query.step()) {
        json order = {
            {"id", query.columnInt(0)}, {"status", query.columnText(1)},
            {"goods", {{"code", query.columnText(2)}, {"name", query.columnText(3)}, {"unit", query.columnText(4)}}},
            {"quantity", query.columnDouble(5)}, {"weight_kg", query.columnDouble(6)}, {"volume_m3", query.columnDouble(7)},
            {"destination_city", query.columnText(8)}, {"delivery_address", query.columnText(9)},
            {"receiver_name", query.columnText(10)}, {"receiver_phone", textOrNull(query, 11)}, {"notes", textOrNull(query, 12)},
            {"created_at", query.columnText(13)}, {"delivered_at", textOrNull(query, 14)},
            {"warehouse", {{"code", query.columnText(15)}, {"name", query.columnText(16)}, {"city", query.columnText(17)}}},
            {"trip", nullptr}};
        if (!query.columnIsNull(18)) {
            order["trip"] = {{"id", query.columnInt(18)}, {"status", query.columnText(19)}, {"dispatched_at", query.columnText(20)},
                             {"distance_km", query.columnInt(21)}, {"truck_plate", query.columnText(22)},
                             {"truck_model", query.columnText(23)}, {"driver_name", query.columnText(24)}};
        }
        orders.push_back(order);
    }
    return {{"user", userJson(customer)}, {"company", company}, {"orders", orders}};
}
