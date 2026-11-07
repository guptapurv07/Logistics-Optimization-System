#include "../include/server.h"
#include "../include/accounts.h"
#include "../include/api.h"
#include "../include/codes.h"
#include "../include/dispatch.h"
#include "../include/google.h"
#include "../include/manager.h"
#include "../include/orders.h"
#include "../include/pricing.h"
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

// Logs the user in: a new session in an HttpOnly cookie, and where the page should go next
void startSession(Response& res, Database& db, const SessionUser& user) {
    setSessionCookie(res, createSession(db, user.id), kSessionHours * 3600);
    sendJson(res, {{"user", userJson(user)}, {"home", kHomePages.at(user.role)}});
}

// After a new account is saved: customers and new managers are logged in straight away; drivers wait for their manager
void answerNewAccount(Response& res, Database& db, json result) {
    if (result["can_login"].get<bool>()) {
        setSessionCookie(res, createSession(db, result["user_id"].get<std::int64_t>()), kSessionHours * 3600);
        result["home"] = kHomePages.at(result["role"].get<std::string>());
    }
    sendJson(res, result, 201);
}

// The kind of account chosen on the login page: "" (any), customer, driver or manager
std::string chosenRole(const Fields& fields) {
    std::string role = fields.optionalText("role", "Account type", 20);
    if (!role.empty() && role != "customer" && role != "driver" && role != "manager") {
        throw ApiError(400, "Account type must be customer, driver or manager");
    }
    return role;
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
    json pricing = {{"rate_per_tonne_km", kRatePerTonneKm}, {"chargeable_kg_per_m3", kChargeableKgPerM3},
                    {"minimum_order_price", kMinimumOrderPrice}};
    return {{"cities", cities}, {"warehouses", warehouses}, {"goods", goods}, {"truck_models", models}, {"body_types", bodies},
            {"pricing", pricing}};
}

// For drawing the map: every city with its position, and every pair of neighbouring cities once,
// with its shortest road and that road's path
json networkJson(const RoadMap& roadMap) {
    std::map<std::pair<std::string, std::string>, Edge> shortest;
    for (const auto& [city, roads] : roadMap.network) {
        for (const Edge& road : roads) {
            if (city >= road.to) continue;
            auto key = std::make_pair(city, road.to);
            auto known = shortest.find(key);
            if (known == shortest.end() || road.weight < known->second.weight) shortest[key] = road;
        }
    }
    json cities = json::array(), roads = json::array();
    for (const auto& [name, unused] : roadMap.network) {
        const CityInfo& city = roadMap.cities.at(name);  // the loader only keeps roads between known cities
        cities.push_back({{"name", name}, {"state", city.state}, {"lat", city.latitude}, {"lng", city.longitude}});
    }
    for (const auto& [pair, road] : shortest) {
        json entry = {{"from", pair.first}, {"to", pair.second}, {"km", road.weight}, {"road", road.type}};
        auto shape = roadMap.shapes.find(pair);
        if (shape != roadMap.shapes.end()) entry["shape"] = shape->second;
        roads.push_back(entry);
    }
    return {{"cities", cities}, {"roads", roads}};
}

}  // namespace

void registerRoutes(httplib::Server& server, const RoadMap& roadMap, const std::string& databasePath, const Settings& settings) {
    gDatabasePath = databasePath;
    const RoadNetwork& network = roadMap.network;

    // --- PAGE GUARDS ---
    // The site opens on the welcome page. A role's page is only served to a logged-in user with that role;
    // anyone else lands on the login page.
    server.set_pre_routing_handler([](const Request& req, Response& res) {
        if (req.path == "/") {
            res.set_redirect("/welcome.html");
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

    server.Get("/api/network", api([&roadMap](const Request&, Response& res) {
        sendJson(res, networkJson(roadMap));
    }));

    // --- LOGIN AND SIGN-UP ---
    // What the login page can offer: the Google button (with this site's client ID) and how codes are delivered
    server.Get("/api/auth/options", api([&settings](const Request&, Response& res) {
        sendJson(res, {{"google_client_id", settings.googleOn() ? json(settings.googleClientId) : json()},
                       {"email", settings.emailOn() ? "email" : "console"}});
    }));

    // A sign-up with a typed email first gets a code by email ({"verify": ...}); the account is made once the code is
    // right (/api/verify). With "google_credential" the email is already confirmed by Google, so it is made at once.
    server.Post("/api/register", api([&roadMap, &settings](const Request& req, Response& res) {
        Database& db = threadDatabase();
        json body = parseBody(req);
        if (body.contains("google_credential")) {
            std::string credential = Fields(body).text("google_credential", "Google sign-in", 1, 4096);
            GoogleAccount google = checkGoogleToken(settings, credential);
            answerNewAccount(res, db, createAccount(db, checkSignup(db, body, roadMap, &google), roadMap));
            return;
        }
        json signup = checkSignup(db, body, roadMap, nullptr);
        sendJson(res, {{"verify", sendSignupCode(db, settings, signup)}});
    }));

    // Step 1 of a password login: the password, and then a code by email ({"verify": ...}) before any session starts.
    // The login page first asks which kind of account this is; another kind is refused here.
    server.Post("/api/login", api([&settings](const Request& req, Response& res) {
        Database& db = threadDatabase();
        json body = parseBody(req);
        Fields fields(body);
        std::string login = fields.text("username", "Username or email", 1, 120);
        if (!body.contains("password") || !body.at("password").is_string()) throw ApiError(400, "Password is required");
        std::string role = chosenRole(fields);
        SessionUser user = checkLogin(db, login, body.at("password").get<std::string>());
        checkCanLogIn(user, role);
        sendJson(res, {{"verify", sendLoginCode(db, settings, user.id, user.email, user.fullName)}});
    }));

    // Step 2: the code from the email. Finishes a login, or saves the new account it confirmed.
    server.Post("/api/verify", api([&roadMap](const Request& req, Response& res) {
        Database& db = threadDatabase();
        json body = parseBody(req);
        Fields fields(body);
        ConfirmedCode confirmed = confirmCode(db, fields.text("ticket", "Ticket", 1, 64), fields.text("code", "Code", 1, 20));
        if (confirmed.purpose == "signup") {
            answerNewAccount(res, db, createAccount(db, confirmed.signup, roadMap));
            return;
        }
        std::optional<SessionUser> user = loadUser(db, confirmed.userId);
        if (!user) throw ApiError(410, "This account no longer exists");
        checkCanLogIn(*user, "");   // a driver's approval could have been withdrawn while the code was out
        startSession(res, db, *user);
    }));

    server.Post("/api/verify/resend", api([&settings](const Request& req, Response& res) {
        json body = parseBody(req);
        sendJson(res, {{"verify", resendCode(threadDatabase(), settings, Fields(body).text("ticket", "Ticket", 1, 64))}});
    }));

    // "Continue with Google": logs in the account with that Google email, no code needed (Google has checked who it is).
    // With no such account the answer is {"signup": {email, name, username}}, and the page asks for the rest.
    server.Post("/api/auth/google", api([&settings](const Request& req, Response& res) {
        Database& db = threadDatabase();
        json body = parseBody(req);
        Fields fields(body);
        GoogleAccount google = checkGoogleToken(settings, fields.text("credential", "Google sign-in", 1, 4096));
        std::string role = chosenRole(fields);
        std::optional<SessionUser> user = findUserByEmail(db, google.email);
        if (!user) {
            sendJson(res, {{"signup", {{"email", google.email}, {"name", google.name}, {"username", suggestUsername(db, google.email)}}}});
            return;
        }
        checkCanLogIn(*user, role);
        startSession(res, db, *user);
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

    // The weight, road km and price of an order before it is placed
    server.Post("/api/customer/quote", api([&network](const Request& req, Response& res) {
        requireRole(req, "customer");
        sendJson(res, quoteOrder(threadDatabase(), parseBody(req), network));
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
        Fields fields(body);
        markStopDelivered(threadDatabase(), driver, fields.id("trip_id", "Trip"), fields.optionalText("city", "City", 60));
        sendJson(res, {{"ok", true}});
    }));

    server.Post("/api/driver/trips/returned", api([](const Request& req, Response& res) {
        SessionUser driver = requireRole(req, "driver");
        json body = parseBody(req);
        markTripReturned(threadDatabase(), driver, Fields(body).id("trip_id", "Trip"));
        sendJson(res, {{"ok", true}});
    }));
}
