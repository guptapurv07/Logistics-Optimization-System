#include "../include/accounts.h"
#include "../include/api.h"
#include "../include/password.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

using json = nlohmann::json;

namespace {

// How the login page names each kind of account ("Continue as truck driver")
const std::map<std::string, std::string> kRoleNames = {
    {"manager", "warehouse manager"}, {"driver", "truck driver"}, {"customer", "customer"}};

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

// Letters, digits and the usual signs before the @, then a domain like gmail.com. Spaces, quotes, brackets, line
// breaks and non-English letters are refused: the address goes into email headers and the mail server's commands.
bool isValidEmail(const std::string& email) {
    std::size_t at = email.find('@');
    if (at == std::string::npos || at == 0 || at > 64 || email.size() > 254) return false;
    const std::string local = email.substr(0, at), domain = email.substr(at + 1);
    static const std::string kLocalSigns = "!#$%&'*+/=?^_`{|}~.-";
    bool localOk = std::all_of(local.begin(), local.end(), [](unsigned char c) {
        return std::isalnum(c) || kLocalSigns.find(char(c)) != std::string::npos;
    });
    bool domainOk = std::all_of(domain.begin(), domain.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '.'; });
    auto dotsOk = [](const std::string& part) {
        return !part.empty() && part.front() != '.' && part.back() != '.' && part.find("..") == std::string::npos;
    };
    return localOk && domainOk && dotsOk(local) && dotsOk(domain) && domain.find('.') != std::string::npos &&
           domain.front() != '-' && domain.back() != '-';
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

// Checked when a username doesn't exist, so a wrong username takes as long to reject as a wrong password
// and the timing doesn't reveal which usernames are real. It is computed when the program starts, so even
// the very first unknown username isn't slower (password.cpp's tables are compile-time constants, safe here).
const std::string kUnknownUserHash = hashPassword("no-such-user-1");

// The parts of a sign-up that can be taken by someone else between the form and the email code are checked
// twice: when the form is sent, so the person hears at once, and again when the account is saved
void checkStillFree(Database& db, const json& signup) {
    if (rowExists(db, "SELECT 1 FROM users WHERE username = ?1", signup.at("username").get<std::string>())) {
        throw ApiError(409, "That username is already taken");
    }
    if (rowExists(db, "SELECT 1 FROM users WHERE email = ?1", signup.at("email").get<std::string>())) {
        throw ApiError(409, "An account with that email already exists");
    }
    const std::string role = signup.at("role").get<std::string>();
    if (role == "driver") {
        Statement warehouse = db.prepare("SELECT 1 FROM warehouses WHERE id = ?1");
        warehouse.bind(1, signup.at("warehouse_id").get<std::int64_t>());
        if (!warehouse.step()) throw ApiError(400, "Choose a warehouse from the list");
        if (rowExists(db, "SELECT 1 FROM drivers WHERE licence_no = ?1", signup.at("licence_no").get<std::string>())) {
            throw ApiError(409, "A driver with that licence number is already registered");
        }
    }
    if (role == "manager") {
        const std::string city = signup.at("city").get<std::string>();
        Statement existing = db.prepare("SELECT code, name FROM warehouses WHERE city = ?1");
        existing.bind(1, city);
        if (existing.step()) {
            throw ApiError(409, city + " already has a warehouse (" + existing.columnText(0) + " " + existing.columnText(1) +
                                    "). For now each city has one warehouse.");
        }
    }
}

}  // namespace

json checkSignup(Database& db, const json& body, const RoadMap& roadMap, const GoogleAccount* google) {
    const RoadNetwork& network = roadMap.network;
    Fields fields(body);
    std::string role = fields.text("role", "Account type", 1, 20);
    if (!kRoleNames.count(role)) throw ApiError(400, "Choose an account type: customer, driver or warehouse manager");

    json signup = {{"role", role}};
    signup["full_name"] = fields.text("full_name", "Full name", 2, 80);
    // An email from Google is already confirmed; a typed one gets a code before the account is made
    std::string email = google ? google->email : lowercase(fields.text("email", "Email", 5, 120));
    if (!isValidEmail(email)) throw ApiError(400, "That email address doesn't look right");
    signup["email"] = email;
    signup["phone"] = checkPhone(fields.optionalText("phone", "Phone", 20), "Phone");
    std::string username = lowercase(fields.text("username", "Username", 3, 30));
    if (!isValidUsername(username)) {
        throw ApiError(400, "Username can use letters, numbers, dots, dashes and underscores, and must start with a letter or number");
    }
    signup["username"] = username;

    // Passwords are taken exactly as typed (no trimming), since spaces may be part of them
    const json* password = body.contains("password") ? &body.at("password") : nullptr;
    if (password && !password->is_null() && !password->is_string()) throw ApiError(400, "Password must be text");
    std::string typed = password && password->is_string() ? password->get<std::string>() : "";
    if (typed.empty() && !google) throw ApiError(400, "Password is required");
    if (!typed.empty()) checkPasswordStrength(typed);

    if (role == "customer") {
        signup["company_name"] = fields.text("company_name", "Company name", 2, 100);
        std::string city = findCity(fields.text("city", "City", 1, 60), network);
        if (city.empty()) throw ApiError(400, "Choose your city from the list");
        signup["city"] = city;
        signup["address"] = fields.text("address", "Address", 5, 200);
    } else if (role == "driver") {
        signup["warehouse_id"] = fields.id("warehouse_id", "Warehouse");
        std::string licenceNo = uppercase(fields.text("licence_no", "Licence number", 5, 25));
        if (licenceNo.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -/") != std::string::npos) {
            throw ApiError(400, "Licence number can only contain letters, digits, spaces, dashes and slashes");
        }
        signup["licence_no"] = licenceNo;
        std::string licenceClass = uppercase(fields.text("licence_class", "Licence class", 3, 3));
        if (licenceClass != "LMV" && licenceClass != "HMV") throw ApiError(400, "Licence class must be LMV or HMV");
        signup["licence_class"] = licenceClass;
    } else {
        signup["warehouse_name"] = fields.text("warehouse_name", "Warehouse name", 3, 60);
        std::string city = findCity(fields.text("city", "City", 1, 60), network);
        if (city.empty()) throw ApiError(400, "Choose the warehouse city from the list");
        signup["city"] = city;
        signup["address"] = fields.text("address", "Warehouse address", 5, 200);
    }
    checkStillFree(db, signup);

    // Hashing takes ~50 ms on purpose. An empty hash means "no password": verifyPassword never accepts it.
    signup["password_hash"] = typed.empty() ? "" : hashPassword(typed);
    return signup;
}

json createAccount(Database& db, const json& signup, const RoadMap& roadMap) {
    const std::string role = signup.at("role").get<std::string>();
    const std::string fullName = signup.at("full_name").get<std::string>();
    const std::string phone = signup.value("phone", std::string());

    Transaction tx(db);
    checkStillFree(db, signup);

    Statement addUser = db.prepare("INSERT INTO users (username, password_hash, role, full_name, email, phone) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    addUser.bind(1, signup.at("username").get<std::string>()).bind(2, signup.at("password_hash").get<std::string>()).bind(3, role)
        .bind(4, fullName).bind(5, signup.at("email").get<std::string>());
    if (phone.empty()) addUser.bindNull(6);
    else addUser.bind(6, phone);
    addUser.run();
    std::int64_t userId = db.lastInsertId();

    std::string message;
    if (role == "customer") {
        Statement add = db.prepare("INSERT INTO customers (user_id, company_name, city, address) VALUES (?1, ?2, ?3, ?4)");
        add.bind(1, userId).bind(2, signup.at("company_name").get<std::string>()).bind(3, signup.at("city").get<std::string>())
            .bind(4, signup.at("address").get<std::string>()).run();
        message = "Welcome, " + fullName + "! Your customer account is ready.";
    } else if (role == "driver") {
        std::int64_t warehouseId = signup.at("warehouse_id").get<std::int64_t>();
        Statement warehouse = db.prepare("SELECT code, name FROM warehouses WHERE id = ?1");
        warehouse.bind(1, warehouseId);
        warehouse.step();  // checkStillFree made sure it exists
        std::string warehouseLabel = warehouse.columnText(0) + " " + warehouse.columnText(1);
        Statement add = db.prepare(
            "INSERT INTO drivers (user_id, warehouse_id, licence_no, licence_class, approval) VALUES (?1, ?2, ?3, ?4, 'pending')");
        add.bind(1, userId).bind(2, warehouseId).bind(3, signup.at("licence_no").get<std::string>())
            .bind(4, signup.at("licence_class").get<std::string>()).run();
        message = "Request sent to " + warehouseLabel + ". You can log in as soon as its manager approves you.";
    } else {
        // The write lock is held, so the next number can't be taken by someone signing up at the same moment
        const std::string city = signup.at("city").get<std::string>(), warehouseName = signup.at("warehouse_name").get<std::string>();
        Statement next = db.prepare("SELECT COALESCE(MAX(id), 0) + 1, (SELECT COUNT(*) + 1 FROM managers) FROM warehouses");
        next.step();
        char code[16], employeeCode[16];
        std::snprintf(code, sizeof(code), "WH-%02lld", static_cast<long long>(next.columnInt(0)));
        std::snprintf(employeeCode, sizeof(employeeCode), "MGR-%03lld", static_cast<long long>(next.columnInt(1)));
        auto info = roadMap.cities.find(city);   // the city was checked against the road map in checkSignup

        Statement addWarehouse = db.prepare("INSERT INTO warehouses (code, name, city, state, address) VALUES (?1, ?2, ?3, ?4, ?5)");
        addWarehouse.bind(1, std::string(code)).bind(2, warehouseName).bind(3, city)
            .bind(4, info != roadMap.cities.end() ? info->second.state : std::string("India")).bind(5, signup.at("address").get<std::string>()).run();
        std::int64_t newWarehouseId = db.lastInsertId();
        Statement addManager = db.prepare("INSERT INTO managers (user_id, warehouse_id, employee_code) VALUES (?1, ?2, ?3)");
        addManager.bind(1, userId).bind(2, newWarehouseId).bind(3, std::string(employeeCode)).run();
        message = std::string(code) + " " + warehouseName + " is open. Add your trucks under Fleet to start taking orders.";
    }
    tx.commit();

    return {{"user_id", userId}, {"role", role}, {"can_login", role != "driver"}, {"message", message}};
}

SessionUser checkLogin(Database& db, const std::string& login, const std::string& password) {
    std::int64_t userId = 0;
    std::string storedHash;
    {
        // A username or an email; both columns are COLLATE NOCASE, and usernames can't contain "@", so they never mix up
        Statement find = db.prepare("SELECT id, password_hash FROM users WHERE username = ?1 OR email = ?1");
        find.bind(1, trim(login));
        if (find.step()) {
            userId = find.columnInt(0);
            storedHash = find.columnText(1);
        }
    }  // the statement is finished before the slow password check below

    if (userId && storedHash.empty()) {
        throw ApiError(401, "This account logs in with Google. Use the \"Continue with Google\" button.");
    }
    bool passwordOk = verifyPassword(password, userId ? storedHash : kUnknownUserHash);
    if (!userId || !passwordOk) throw ApiError(401, "Wrong username or password");

    std::optional<SessionUser> user = loadUser(db, userId);
    if (!user) throw ApiError(401, "Wrong username or password");
    return *user;
}

void checkCanLogIn(const SessionUser& user, const std::string& role) {
    if (!role.empty() && role != user.role) {
        const std::string& name = kRoleNames.at(user.role);
        throw ApiError(403, "This is a " + name + " account. Go back and choose \"Continue as " + name + "\".");
    }
    if (user.role == "driver" && user.approval == "pending") {
        throw ApiError(403, "Your account is waiting for approval by the manager of " + user.warehouseCode + " " +
                                user.warehouseName + ".");
    }
    if (user.role == "driver" && user.approval == "rejected") {
        throw ApiError(403, "The manager of " + user.warehouseCode + " " + user.warehouseName + " declined your application.");
    }
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

std::optional<SessionUser> findUserByEmail(Database& db, const std::string& email) {
    std::int64_t userId = 0;
    {
        Statement find = db.prepare("SELECT id FROM users WHERE email = ?1");
        find.bind(1, email);
        if (!find.step()) return std::nullopt;
        userId = find.columnInt(0);
    }
    return loadUser(db, userId);
}

std::string suggestUsername(Database& db, const std::string& email) {
    std::string base;
    for (unsigned char c : lowercase(email.substr(0, email.find('@')))) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') base += char(c);
    }
    while (!base.empty() && !std::isalnum(static_cast<unsigned char>(base[0]))) base.erase(0, 1);
    if (base.size() > 26) base.resize(26);
    for (int n = 1; n < 1000; ++n) {
        std::string candidate = n == 1 ? base : base + std::to_string(n);   // apurv, apurv2, apurv3...
        if (isValidUsername(candidate) && !rowExists(db, "SELECT 1 FROM users WHERE username = ?1", candidate)) return candidate;
    }
    return "";
}

std::string createSession(Database& db, std::int64_t userId) {
    db.exec("DELETE FROM sessions WHERE expires_at <= datetime('now')");  // tidy up logins that ran out
    std::string token = randomToken();
    Statement add = db.prepare("INSERT INTO sessions (token_hash, user_id, expires_at) VALUES (?1, ?2, datetime('now', ?3))");
    add.bind(1, sha256Hex(token)).bind(2, userId).bind(3, "+" + std::to_string(kSessionHours) + " hours").run();
    return token;
}

std::optional<SessionUser> userForSession(Database& db, const std::string& token) {
    if (token.size() != 64 || token.find_first_not_of("0123456789abcdef") != std::string::npos) return std::nullopt;
    std::int64_t userId;
    {
        Statement find = db.prepare("SELECT user_id FROM sessions WHERE token_hash = ?1 AND expires_at > datetime('now')");
        find.bind(1, sha256Hex(token));
        if (!find.step()) return std::nullopt;
        userId = find.columnInt(0);
    }
    return loadUser(db, userId);
}

void endSession(Database& db, const std::string& token) {
    Statement remove = db.prepare("DELETE FROM sessions WHERE token_hash = ?1");
    remove.bind(1, sha256Hex(token)).run();
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
