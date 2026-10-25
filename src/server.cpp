#include "../include/server.h"
#include "../include/accounts.h"
#include "../include/api.h"
#include "../include/dispatch.h"
#include "../include/manager.h"
#include "../include/orders.h"
#include <iostream>
#include <map>
#include <memory>
#include <optional>

using json = nlohmann::json;
using httplib::Request;
using httplib::Response;

namespace {

std::string gDatabasePath;

// Each server thread keeps its own SQLite connection. With one shared connection, a transaction started
// by one request would also contain the statements of any other request running at the same moment.
Database& threadDatabase() {
    thread_local std::unique_ptr<Database> db;
    if (!db) db = std::make_unique<Database>(gDatabasePath);
    return *db;
}

const char kSessionCookie[] = "session";

const std::map<std::string, std::string> kPageRoles = {
    {"/index.html", "manager"}, {"/driver.html", "driver"}, {"/customer.html", "customer"}};
const std::map<std::string, std::string> kHomePages = {
    {"manager", "/index.html"}, {"driver", "/driver.html"}, {"customer", "/customer.html"}};

std::string cookieValue(const Request& req, const std::string& name) {
    std::string header = req.get_header_value("Cookie");
    for (std::size_t start = 0; start < header.size();) {
        std::size_t end = header.find(';', start);
        if (end == std::string::npos) end = header.size();
        std::string part = trim(header.substr(start, end - start));
        std::size_t equals = part.find('=');
        if (equals != std::string::npos && part.substr(0, equals) == name) return part.substr(equals + 1);
        start = end + 1;
    }
    return "";
}

std::optional<SessionUser> currentUser(const Request& req) {
    std::string token = cookieValue(req, kSessionCookie);
    if (token.empty()) return std::nullopt;
    return userForSession(threadDatabase(), token);
}

bool hasRole(const SessionUser& user, const std::string& role) {
    return user.role == role && (role != "driver" || user.approval == "approved");
}

SessionUser requireRole(const Request& req, const std::string& role) {
    std::optional<SessionUser> user = currentUser(req);
    if (!user) throw ApiError(401, "Your login has ended. Please log in again.");
    if (!hasRole(*user, role)) throw ApiError(403, "This is only for " + role + " accounts");
    return *user;
}

json parseBody(const Request& req) {
    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) throw ApiError(400, "The request must be a JSON object");
    return body;
}

void sendJson(Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body.dump(), "application/json");
}

// HttpOnly: page scripts can't read the cookie. SameSite=Strict: other websites can't make the browser send it.
void setSessionCookie(Response& res, const std::string& token, int maxAgeSeconds) {
    res.set_header("Set-Cookie", std::string(kSessionCookie) + "=" + token +
                                     "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(maxAgeSeconds));
}

// Turns an ApiError into {"error": message} with its status; anything unexpected is logged and becomes a 500
// without internal details
template <typename Handler>
httplib::Server::Handler api(Handler handler) {
    return [handler](const Request& req, Response& res) {
        try {
            handler(req, res);
        } catch (const ApiError& e) {
            sendJson(res, {{"error", e.what()}}, e.status());
        } catch (const std::exception& e) {
            std::cerr << "[API ERROR] " << req.method << " " << req.path << ": " << e.what() << std::endl;
            sendJson(res, {{"error", "Something went wrong on the server. Please try again."}}, 500);
        }
    };
}

json catalogJson(Database& db, const RoadNetwork& network) {
    json cities = json::array();
    for (const auto& [city, roads] : network) cities.push_back(city);  // std::map keeps them sorted

    json warehouses = json::array();
    {
        Statement query = db.prepare("SELECT id, code, name, city, state FROM warehouses ORDER BY id");
        while (query.step()) {
            warehouses.push_back({{"id", query.columnInt(0)}, {"code", query.columnText(1)}, {"name", query.columnText(2)},
                                  {"city", query.columnText(3)}, {"state", query.columnText(4)}});
        }
    }
    std::map<std::string, json> goodsBodies;
    {
        Statement query = db.prepare("SELECT goods_code, body_type FROM goods_body_types ORDER BY body_type");
        while (query.step()) goodsBodies[query.columnText(0)].push_back(query.columnText(1));
    }
    json goods = json::array();
    {
        Statement query = db.prepare(
            "SELECT code, name, unit, kg_per_unit, m3_per_unit, keep_dry, is_food, handling FROM goods_types ORDER BY rowid");
        while (query.step()) {
            std::string code = query.columnText(0);
            goods.push_back({{"code", code}, {"name", query.columnText(1)}, {"unit", query.columnText(2)},
                             {"kg_per_unit", query.columnDouble(3)}, {"m3_per_unit", query.columnDouble(4)},
                             {"keep_dry", query.columnInt(5) == 1}, {"is_food", query.columnInt(6) == 1},
                             {"handling", query.columnText(7)}, {"body_types", goodsBodies[code]}});
        }
    }
    json models = json::array();
    {
        Statement query = db.prepare(R"SQL(
            SELECT id, name, body_type, payload_kg, volume_m3, gross_weight_kg, licence_needed, cost_per_km
            FROM truck_models ORDER BY body_type, payload_kg)SQL");
        while (query.step()) {
            models.push_back({{"id", query.columnInt(0)}, {"name", query.columnText(1)}, {"body_type", query.columnText(2)},
                              {"payload_kg", query.columnDouble(3)}, {"volume_m3", query.columnDouble(4)},
                              {"gross_weight_kg", query.columnDouble(5)}, {"licence_needed", query.columnText(6)},
                              {"cost_per_km", query.columnDouble(7)}});
        }
    }
    json bodies = json::array();
    {
        Statement query = db.prepare("SELECT code, name, description FROM body_types ORDER BY rowid");
        while (query.step()) {
            bodies.push_back({{"code", query.columnText(0)}, {"name", query.columnText(1)}, {"description", query.columnText(2)}});
        }
    }
    return {{"cities", cities}, {"warehouses", warehouses}, {"goods", goods}, {"truck_models", models}, {"body_types", bodies}};
}

// Every pair of cities once, with its shortest road, for drawing the map
json networkJson(const RoadNetwork& network) {
    std::map<std::pair<std::string, std::string>, Edge> shortest;
    for (const auto& [city, roads] : network) {
        for (const Edge& road : roads) {
            if (city >= road.to) continue;
            auto key = std::make_pair(city, road.to);
            auto known = shortest.find(key);
            if (known == shortest.end() || road.weight < known->second.weight) shortest[key] = road;
        }
    }
    json cities = json::array(), roads = json::array();
    for (const auto& [city, unused] : network) cities.push_back(city);
    for (const auto& [pair, road] : shortest) {
        roads.push_back({{"from", pair.first}, {"to", pair.second}, {"km", road.weight}, {"road", road.type}});
    }
    return {{"cities", cities}, {"roads", roads}};
}

}  // namespace

void registerRoutes(httplib::Server& server, const RoadNetwork& network, const std::string& databasePath) {
    gDatabasePath = databasePath;

    // --- PAGE GUARDS ---
    // A role's page is only served to a logged-in user with that role; anyone else lands on the login page
    server.set_pre_routing_handler([](const Request& req, Response& res) {
        if (req.path == "/") {
            res.set_redirect("/login.html");
            return httplib::Server::HandlerResponse::Handled;
        }
        auto page = kPageRoles.find(req.path);
        if (page == kPageRoles.end()) return httplib::Server::HandlerResponse::Unhandled;

        bool allowed = false;
        try {
            std::optional<SessionUser> user = currentUser(req);
            allowed = user && hasRole(*user, page->second);
        } catch (const std::exception& e) {
            std::cerr << "[PAGE ERROR] " << req.path << ": " << e.what() << std::endl;
        }
        if (!allowed) {
            res.set_redirect("/login.html");
            return httplib::Server::HandlerResponse::Handled;
        }
        res.set_header("Cache-Control", "no-store");  // after logging out, Back must not show the old page
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Unknown /api paths answer in JSON like every other API error
    server.set_error_handler([](const Request& req, Response& res) {
        if (!res.body.empty() || req.path.rfind("/api/", 0) != 0) return httplib::Server::HandlerResponse::Unhandled;
        sendJson(res, {{"error", "Unknown API address"}}, res.status);
        return httplib::Server::HandlerResponse::Handled;
    });

    // --- PUBLIC ---
    server.Get("/api/catalog", api([&network](const Request&, Response& res) {
        sendJson(res, catalogJson(threadDatabase(), network));
    }));

    server.Get("/api/network", api([&network](const Request&, Response& res) {
        sendJson(res, networkJson(network));
    }));

    server.Post("/api/register", api([&network](const Request& req, Response& res) {
        Database& db = threadDatabase();
        json result = registerAccount(db, parseBody(req), network);
        // Customers and new managers are logged in straight away; drivers wait for their manager
        if (result["can_login"].get<bool>()) {
            setSessionCookie(res, createSession(db, result["user_id"].get<std::int64_t>()), kSessionHours * 3600);
            result["home"] = kHomePages.at(result["role"].get<std::string>());
        }
        sendJson(res, result, 201);
    }));

    server.Post("/api/login", api([](const Request& req, Response& res) {
        Database& db = threadDatabase();
        json body = parseBody(req);
        std::string username = Fields(body).text("username", "Username", 1, 60);
        if (!body.contains("password") || !body.at("password").is_string()) throw ApiError(400, "Password is required");
        SessionUser user = checkLogin(db, username, body.at("password").get<std::string>());
        setSessionCookie(res, createSession(db, user.id), kSessionHours * 3600);
        sendJson(res, {{"user", userJson(user)}, {"home", kHomePages.at(user.role)}});
    }));

    server.Post("/api/logout", api([](const Request& req, Response& res) {
        std::string token = cookieValue(req, kSessionCookie);
        if (!token.empty()) endSession(threadDatabase(), token);
        setSessionCookie(res, "", 0);
        sendJson(res, {{"ok", true}});
    }));

    // "Who is logged in?" Nobody is a normal answer here (user: null), not an error
    server.Get("/api/me", api([](const Request& req, Response& res) {
        std::optional<SessionUser> user = currentUser(req);
        if (!user || !hasRole(*user, user->role)) {
            sendJson(res, {{"user", nullptr}});
            return;
        }
        sendJson(res, {{"user", userJson(*user)}, {"home", kHomePages.at(user->role)}});
    }));

    // --- CUSTOMER ---
    server.Get("/api/customer/dashboard", api([](const Request& req, Response& res) {
        sendJson(res, customerDashboard(threadDatabase(), requireRole(req, "customer")));
    }));

    server.Post("/api/customer/orders", api([&network](const Request& req, Response& res) {
        SessionUser customer = requireRole(req, "customer");
        sendJson(res, placeOrder(threadDatabase(), customer, parseBody(req), network), 201);
    }));

    server.Post("/api/customer/orders/cancel", api([](const Request& req, Response& res) {
        SessionUser customer = requireRole(req, "customer");
        json body = parseBody(req);
        cancelOrder(threadDatabase(), customer, Fields(body).id("order_id", "Order"));
        sendJson(res, {{"ok", true}});
    }));

    // --- MANAGER ---
    server.Get("/api/manager/dashboard", api([](const Request& req, Response& res) {
        sendJson(res, managerDashboard(threadDatabase(), requireRole(req, "manager")));
    }));

    server.Post("/api/manager/dispatch", api([&network](const Request& req, Response& res) {
        SessionUser manager = requireRole(req, "manager");
        sendJson(res, dispatchWarehouse(threadDatabase(), manager, network));
    }));

    server.Post("/api/manager/drivers/decide", api([](const Request& req, Response& res) {
        SessionUser manager = requireRole(req, "manager");
        json body = parseBody(req);
        Fields fields(body);
        decideDriver(threadDatabase(), manager, fields.id("driver_id", "Driver"), fields.flag("approve", "Decision"));
        sendJson(res, {{"ok", true}});
    }));

    server.Post("/api/manager/trucks", api([](const Request& req, Response& res) {
        SessionUser manager = requireRole(req, "manager");
        sendJson(res, addTruck(threadDatabase(), manager, parseBody(req)), 201);
    }));

    server.Post("/api/manager/orders/cancel", api([](const Request& req, Response& res) {
        SessionUser manager = requireRole(req, "manager");
        json body = parseBody(req);
        cancelOrder(threadDatabase(), manager, Fields(body).id("order_id", "Order"));
        sendJson(res, {{"ok", true}});
    }));

    // --- DRIVER ---
    server.Get("/api/driver/dashboard", api([](const Request& req, Response& res) {
        sendJson(res, driverDashboard(threadDatabase(), requireRole(req, "driver")));
    }));

    server.Post("/api/driver/trips/delivered", api([](const Request& req, Response& res) {
        SessionUser driver = requireRole(req, "driver");
        json body = parseBody(req);
        markTripDelivered(threadDatabase(), driver, Fields(body).id("trip_id", "Trip"));
        sendJson(res, {{"ok", true}});
    }));

    server.Post("/api/driver/trips/returned", api([](const Request& req, Response& res) {
        SessionUser driver = requireRole(req, "driver");
        json body = parseBody(req);
        markTripReturned(threadDatabase(), driver, Fields(body).id("trip_id", "Trip"));
        sendJson(res, {{"ok", true}});
    }));
}
