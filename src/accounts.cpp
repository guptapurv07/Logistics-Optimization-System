#include "../include/accounts.h"
#include "../include/api.h"
#include "../include/password.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <random>

using json = nlohmann::json;

namespace {

// The state of every city in network_map.csv, filled in when a manager opens a warehouse there
const std::map<std::string, std::string> kCityStates = {
    {"Agra", "Uttar Pradesh"}, {"Ahmedabad", "Gujarat"}, {"Amritsar", "Punjab"}, {"Bangalore", "Karnataka"},
    {"Bhopal", "Madhya Pradesh"}, {"Bhubaneswar", "Odisha"}, {"Chandigarh", "Chandigarh"}, {"Chennai", "Tamil Nadu"},
    {"Coimbatore", "Tamil Nadu"}, {"Dehradun", "Uttarakhand"}, {"Delhi", "Delhi"}, {"Goa", "Goa"},
    {"Gwalior", "Madhya Pradesh"}, {"Hyderabad", "Telangana"}, {"Indore", "Madhya Pradesh"}, {"Jaipur", "Rajasthan"},
    {"Jodhpur", "Rajasthan"}, {"Kanpur", "Uttar Pradesh"}, {"Kochi", "Kerala"}, {"Kolkata", "West Bengal"},
    {"Lucknow", "Uttar Pradesh"}, {"Ludhiana", "Punjab"}, {"Madurai", "Tamil Nadu"}, {"Mumbai", "Maharashtra"},
    {"Mysore", "Karnataka"}, {"Nagpur", "Maharashtra"}, {"Nashik", "Maharashtra"}, {"Noida", "Uttar Pradesh"},
    {"Patna", "Bihar"}, {"Prayagraj", "Uttar Pradesh"}, {"Pune", "Maharashtra"}, {"Surat", "Gujarat"},
    {"Trivandrum", "Kerala"}, {"Varanasi", "Uttar Pradesh"}, {"Vijayawada", "Andhra Pradesh"},
    {"Visakhapatnam", "Andhra Pradesh"}};

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

std::string uppercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return text;
}

// 3-30 characters: letters, digits, dots, dashes and underscores, starting with a letter or digit
bool isValidUsername(const std::string& name) {
    if (name.size() < 3 || name.size() > 30 || !std::isalnum(static_cast<unsigned char>(name[0]))) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_' || c == '-'; });
}

bool isValidEmail(const std::string& email) {
    std::size_t at = email.find('@');
    if (at == std::string::npos || at == 0 || email.find('@', at + 1) != std::string::npos) return false;
    std::size_t dot = email.find('.', at + 2);
    return dot != std::string::npos && dot + 1 < email.size() && email.find_first_of(" \t") == std::string::npos;
}

// Passwords are taken exactly as typed (no trimming), since spaces may be part of them
std::string readPassword(const json& body) {
    if (!body.contains("password") || !body.at("password").is_string()) throw ApiError(400, "Password is required");
    return body.at("password").get<std::string>();
}

void checkPasswordStrength(const std::string& password) {
    if (password.size() < 8 || password.size() > 128) throw ApiError(400, "Password must be 8 to 128 characters long");
    bool letter = std::any_of(password.begin(), password.end(), [](unsigned char c) { return std::isalpha(c); });
    bool digit = std::any_of(password.begin(), password.end(), [](unsigned char c) { return std::isdigit(c); });
    if (!letter || !digit) throw ApiError(400, "Password must contain at least one letter and one number");
}

bool rowExists(Database& db, const char* sql, const std::string& value) {
    Statement query = db.prepare(sql);
    query.bind(1, value);
    return query.step();
}

// 32 random bytes from the operating system's secure source (std::random_device on macOS and Linux)
std::string randomToken() {
    std::random_device random;
    std::uint8_t bytes[32];
    for (std::size_t i = 0; i < sizeof(bytes); i += 4) {
        std::uint32_t bits = random();
        for (std::size_t j = 0; j < 4; ++j) bytes[i + j] = std::uint8_t(bits >> (8 * j));
    }
    return toHex(bytes, sizeof(bytes));
}

std::string tokenHash(const std::string& token) {
    std::array<std::uint8_t, 32> digest = sha256(token);
    return toHex(digest.data(), digest.size());
}

// Checked when a username doesn't exist, so a wrong username takes as long to reject as a wrong password
// and the timing doesn't reveal which usernames are real. It is computed when the program starts, so even
// the very first unknown username isn't slower (password.cpp's tables are compile-time constants, safe here).
const std::string kUnknownUserHash = hashPassword("no-such-user-1");

}  // namespace

json registerAccount(Database& db, const json& body, const RoadNetwork& network) {
    Fields fields(body);
    std::string role = fields.text("role", "Account type", 1, 20);
    if (role != "customer" && role != "driver" && role != "manager") {
        throw ApiError(400, "Choose an account type: customer, driver or warehouse manager");
    }

    std::string fullName = fields.text("full_name", "Full name", 2, 80);
    std::string email = lowercase(fields.text("email", "Email", 5, 120));
    if (!isValidEmail(email)) throw ApiError(400, "That email address doesn't look right");
    std::string phone = checkPhone(fields.optionalText("phone", "Phone", 20), "Phone");
    std::string username = lowercase(fields.text("username", "Username", 3, 30));
    if (!isValidUsername(username)) {
        throw ApiError(400, "Username can use letters, numbers, dots, dashes and underscores, and must start with a letter or number");
    }
    std::string password = readPassword(body);
    checkPasswordStrength(password);

    // Everything role-specific is checked before anything is written
    std::string companyName, city, address, licenceNo, licenceClass, warehouseName;
    std::int64_t warehouseId = 0;
    if (role == "customer") {
        companyName = fields.text("company_name", "Company name", 2, 100);
        city = findCity(fields.text("city", "City", 1, 60), network);
        if (city.empty()) throw ApiError(400, "Choose your city from the list");
        address = fields.text("address", "Address", 5, 200);
    } else if (role == "driver") {
        warehouseId = fields.id("warehouse_id", "Warehouse");
        licenceNo = uppercase(fields.text("licence_no", "Licence number", 5, 25));
        if (licenceNo.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -/") != std::string::npos) {
            throw ApiError(400, "Licence number can only contain letters, digits, spaces, dashes and slashes");
        }
        licenceClass = uppercase(fields.text("licence_class", "Licence class", 3, 3));
        if (licenceClass != "LMV" && licenceClass != "HMV") throw ApiError(400, "Licence class must be LMV or HMV");
    } else {
        warehouseName = fields.text("warehouse_name", "Warehouse name", 3, 60);
        city = findCity(fields.text("city", "City", 1, 60), network);
        if (city.empty()) throw ApiError(400, "Choose the warehouse city from the list");
        address = fields.text("address", "Warehouse address", 5, 200);
    }

    // Hashing takes ~50 ms on purpose, so it happens before the write lock is taken
    std::string passwordHash = hashPassword(password);

    Transaction tx(db);
    if (rowExists(db, "SELECT 1 FROM users WHERE username = ?1", username)) throw ApiError(409, "That username is already taken");
    if (rowExists(db, "SELECT 1 FROM users WHERE email = ?1", email)) throw ApiError(409, "An account with that email already exists");

    std::string warehouseLabel;
    if (role == "driver") {
        Statement warehouse = db.prepare("SELECT code, name FROM warehouses WHERE id = ?1");
        warehouse.bind(1, warehouseId);
        if (!warehouse.step()) throw ApiError(400, "Choose a warehouse from the list");
        warehouseLabel = warehouse.columnText(0) + " " + warehouse.columnText(1);
        if (rowExists(db, "SELECT 1 FROM drivers WHERE licence_no = ?1", licenceNo)) {
            throw ApiError(409, "A driver with that licence number is already registered");
        }
    }
    if (role == "manager") {
        Statement existing = db.prepare("SELECT code, name FROM warehouses WHERE city = ?1");
        existing.bind(1, city);
        if (existing.step()) {
            throw ApiError(409, city + " already has a warehouse (" + existing.columnText(0) + " " + existing.columnText(1) +
                                    "). For now each city has one warehouse.");
        }
    }

    Statement addUser = db.prepare("INSERT INTO users (username, password_hash, role, full_name, email, phone) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    addUser.bind(1, username).bind(2, passwordHash).bind(3, role).bind(4, fullName).bind(5, email);
    if (phone.empty()) addUser.bindNull(6);
    else addUser.bind(6, phone);
    addUser.run();
    std::int64_t userId = db.lastInsertId();

    std::string message;
    if (role == "customer") {
        Statement add = db.prepare("INSERT INTO customers (user_id, company_name, city, address) VALUES (?1, ?2, ?3, ?4)");
        add.bind(1, userId).bind(2, companyName).bind(3, city).bind(4, address).run();
        message = "Welcome, " + fullName + "! Your customer account is ready.";
    } else if (role == "driver") {
        Statement add = db.prepare(
            "INSERT INTO drivers (user_id, warehouse_id, licence_no, licence_class, approval) VALUES (?1, ?2, ?3, ?4, 'pending')");
        add.bind(1, userId).bind(2, warehouseId).bind(3, licenceNo).bind(4, licenceClass).run();
        message = "Request sent to " + warehouseLabel + ". You can log in as soon as its manager approves you.";
    } else {
        // The write lock is held, so the next number can't be taken by someone signing up at the same moment
        Statement next = db.prepare("SELECT COALESCE(MAX(id), 0) + 1, (SELECT COUNT(*) + 1 FROM managers) FROM warehouses");
        next.step();
        char code[16], employeeCode[16];
        std::snprintf(code, sizeof(code), "WH-%02lld", static_cast<long long>(next.columnInt(0)));
        std::snprintf(employeeCode, sizeof(employeeCode), "MGR-%03lld", static_cast<long long>(next.columnInt(1)));
        auto state = kCityStates.find(city);

        Statement addWarehouse = db.prepare("INSERT INTO warehouses (code, name, city, state, address) VALUES (?1, ?2, ?3, ?4, ?5)");
        addWarehouse.bind(1, std::string(code)).bind(2, warehouseName).bind(3, city)
            .bind(4, state != kCityStates.end() ? state->second : std::string("India")).bind(5, address).run();
        std::int64_t newWarehouseId = db.lastInsertId();
        Statement addManager = db.prepare("INSERT INTO managers (user_id, warehouse_id, employee_code) VALUES (?1, ?2, ?3)");
        addManager.bind(1, userId).bind(2, newWarehouseId).bind(3, std::string(employeeCode)).run();
        message = std::string(code) + " " + warehouseName + " is open. Add your trucks under Fleet to start taking orders.";
    }
    tx.commit();

    return {{"user_id", userId}, {"role", role}, {"can_login", role != "driver"}, {"message", message}};
}

SessionUser checkLogin(Database& db, const std::string& username, const std::string& password) {
    std::int64_t userId = 0;
    std::string storedHash;
    {
        // Usernames are matched without caring about case (the column is COLLATE NOCASE)
        Statement find = db.prepare("SELECT id, password_hash FROM users WHERE username = ?1");
        find.bind(1, trim(username));
        if (find.step()) {
            userId = find.columnInt(0);
            storedHash = find.columnText(1);
        }
    }  // the statement is finished before the slow password check below

    bool passwordOk = verifyPassword(password, userId ? storedHash : kUnknownUserHash);
    if (!userId || !passwordOk) throw ApiError(401, "Wrong username or password");

    std::optional<SessionUser> user = loadUser(db, userId);
    if (!user) throw ApiError(401, "Wrong username or password");
    if (user->role == "driver" && user->approval == "pending") {
        throw ApiError(403, "Your account is waiting for approval by the manager of " + user->warehouseCode + " " +
                                user->warehouseName + ".");
    }
    if (user->role == "driver" && user->approval == "rejected") {
        throw ApiError(403, "The manager of " + user->warehouseCode + " " + user->warehouseName + " declined your application.");
    }
    return *user;
}

std::optional<SessionUser> loadUser(Database& db, std::int64_t userId) {
    Statement query = db.prepare(R"SQL(
        SELECT u.id, u.username, u.role, u.full_name, u.email, COALESCE(d.approval, ''),
               COALESCE(m.warehouse_id, d.warehouse_id, 0), COALESCE(w.code, ''), COALESCE(w.name, ''), COALESCE(w.city, '')
        FROM users u
        LEFT JOIN managers m ON m.user_id = u.id
        LEFT JOIN drivers d ON d.user_id = u.id
        LEFT JOIN warehouses w ON w.id = COALESCE(m.warehouse_id, d.warehouse_id)
        WHERE u.id = ?1)SQL");
    query.bind(1, userId);
    if (!query.step()) return std::nullopt;
    SessionUser user;
    user.id = query.columnInt(0);
    user.username = query.columnText(1);
    user.role = query.columnText(2);
    user.fullName = query.columnText(3);
    user.email = query.columnText(4);
    user.approval = query.columnText(5);
    user.warehouseId = query.columnInt(6);
    user.warehouseCode = query.columnText(7);
    user.warehouseName = query.columnText(8);
    user.warehouseCity = query.columnText(9);
    return user;
}

std::string createSession(Database& db, std::int64_t userId) {
    db.exec("DELETE FROM sessions WHERE expires_at <= datetime('now')");  // tidy up logins that ran out
    std::string token = randomToken();
    Statement add = db.prepare("INSERT INTO sessions (token_hash, user_id, expires_at) VALUES (?1, ?2, datetime('now', ?3))");
    add.bind(1, tokenHash(token)).bind(2, userId).bind(3, "+" + std::to_string(kSessionHours) + " hours").run();
    return token;
}

std::optional<SessionUser> userForSession(Database& db, const std::string& token) {
    if (token.size() != 64 || token.find_first_not_of("0123456789abcdef") != std::string::npos) return std::nullopt;
    std::int64_t userId;
    {
        Statement find = db.prepare("SELECT user_id FROM sessions WHERE token_hash = ?1 AND expires_at > datetime('now')");
        find.bind(1, tokenHash(token));
        if (!find.step()) return std::nullopt;
        userId = find.columnInt(0);
    }
    return loadUser(db, userId);
}

void endSession(Database& db, const std::string& token) {
    Statement remove = db.prepare("DELETE FROM sessions WHERE token_hash = ?1");
    remove.bind(1, tokenHash(token)).run();
}

json userJson(const SessionUser& user) {
    json result = {{"id", user.id}, {"username", user.username}, {"role", user.role},
                   {"full_name", user.fullName}, {"email", user.email}, {"warehouse", nullptr}};
    if (user.warehouseId) {
        result["warehouse"] = {{"id", user.warehouseId}, {"code", user.warehouseCode},
                               {"name", user.warehouseName}, {"city", user.warehouseCity}};
    }
    return result;
}
