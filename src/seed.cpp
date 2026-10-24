#include "../include/seed.h"
#include "../include/algorithms.h"
#include "../include/password.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>

namespace {

// --- PORTABLE RANDOM NUMBERS ---
// std::mt19937 gives the same sequence with every compiler, but std::uniform_int_distribution
// does not (libc++ and libstdc++ differ), so ranges are mapped by hand to keep the data identical.
class SeedRandom {
public:
    explicit SeedRandom(std::uint32_t seed) : engine_(seed) {}

    // Whole number in [low, high]. Values past the last full multiple of the range are redrawn,
    // because a plain % would make small numbers slightly more likely.
    int between(int low, int high) {
        const std::uint64_t span = std::uint64_t(high - low) + 1;
        const std::uint64_t limit = (std::uint64_t(1) << 32) - (std::uint64_t(1) << 32) % span;
        std::uint64_t value;
        do {
            value = engine_();
        } while (value >= limit);
        return low + int(value % span);
    }

    bool percent(int chance) { return between(1, 100) <= chance; }

    template <typename T>
    const T& pick(const std::vector<T>& items) {
        return items[std::size_t(between(0, int(items.size()) - 1))];
    }

    // Index chosen in proportion to the given weights
    std::size_t pickWeighted(const std::vector<int>& weights) {
        int total = 0;
        for (int w : weights) total += w;
        int roll = between(1, total);
        for (std::size_t i = 0; i < weights.size(); ++i) {
            roll -= weights[i];
            if (roll <= 0) return i;
        }
        return weights.size() - 1;
    }

private:
    std::mt19937 engine_;
};

// --- CATALOGUES ---
struct BodyTypeSeed {
    const char* code;
    const char* name;
    const char* description;
};

const std::vector<BodyTypeSeed> kBodyTypes = {
    {"open", "Open body", "Flat deck with side walls; loads are covered with a tarpaulin"},
    {"tipper", "Tipper", "Tilting body that unloads loose material such as sand by tipping"},
    {"closed", "Closed container", "Fully enclosed box body that keeps goods clean, dry and secure"},
};

struct GoodsSeed {
    const char* code;
    const char* name;
    const char* unit;
    double kgPerUnit;
    double m3PerUnit;
    bool keepDry;
    bool isFood;
    const char* handling;
    std::vector<std::string> bodyTypes;
};

const std::vector<GoodsSeed> kGoods = {
    {"grain", "Grain (wheat, rice)", "bag", 50, 0.07, true, true,
     "Food: keep dry and clean, never in the same truck as cement or sand", {"closed", "open"}},
    {"cloth", "Cloth and textiles", "bale", 60, 0.25, true, false,
     "Keep clean and dry; light but bulky, so space runs out before weight", {"closed"}},
    {"cement", "Cement (50 kg bags)", "bag", 50, 0.035, true, false,
     "Keep dry, damp cement sets in the bag; heavy, so weight runs out before space", {"open", "closed"}},
    {"sand", "Sand", "tonne", 1000, 0.625, false, false,
     "Loose and very heavy (about 1.6 t per m3); never load above the truck's payload", {"tipper", "open"}},
    {"cartons", "General cartons", "carton", 20, 0.06, true, false,
     "Packed goods; stack the heavy cartons at the bottom", {"closed", "open"}},
};

// Clean, dry goods may share a truck; cement and sand only travel with their own kind
const std::vector<std::pair<std::string, std::string>> kCompatiblePairs = {
    {"grain", "cloth"}, {"grain", "cartons"}, {"cloth", "cartons"}};

struct ModelSeed {
    const char* name;
    const char* bodyType;
    double payloadKg;
    double volumeM3;
    double grossWeightKg;
    double costPerKm;
    int fleetShare;  // how common this model is in a fleet (relative weight)
};

// Approximate figures for common Indian goods vehicles
const std::vector<ModelSeed> kModels = {
    {"Mini truck, closed", "closed", 750, 4.5, 1850, 12, 10},
    {"14 ft container", "closed", 3000, 15.5, 7000, 18, 12},
    {"19 ft container", "closed", 7000, 26, 12000, 26, 10},
    {"24 ft container", "closed", 15000, 43, 25000, 34, 7},
    {"32 ft multi-axle container", "closed", 18000, 58, 31000, 42, 5},
    {"Mini truck, open", "open", 1000, 4, 2000, 11, 8},
    {"6-wheel open body", "open", 9000, 22, 16000, 25, 10},
    {"10-wheel open body", "open", 16000, 30, 26000, 32, 8},
    {"12-wheel open body", "open", 21000, 36, 31000, 38, 4},
    {"6-wheel tipper", "tipper", 10000, 8, 16200, 30, 6},
    {"10-wheel tipper", "tipper", 17000, 12, 28000, 38, 6},
    {"12-wheel tipper", "tipper", 24000, 16, 35000, 45, 3},
};

// --- COMPANY ---
struct WarehouseSeed {
    const char* city;
    const char* state;
    const char* plateCode;  // state and RTO code for number plates
    const char* area;
    const char* region;     // picks surnames that are common in that part of India
};

const std::vector<WarehouseSeed> kWarehouses = {
    {"Noida", "Uttar Pradesh", "UP 16", "Sector 62", "north"},
    {"Chandigarh", "Chandigarh", "CH 01", "Industrial Area Phase 1", "north"},
    {"Ludhiana", "Punjab", "PB 10", "Focal Point", "north"},
    {"Jaipur", "Rajasthan", "RJ 14", "Transport Nagar", "north"},
    {"Jodhpur", "Rajasthan", "RJ 19", "Basni Industrial Area", "north"},
    {"Lucknow", "Uttar Pradesh", "UP 32", "Transport Nagar", "north"},
    {"Ahmedabad", "Gujarat", "GJ 01", "Naroda GIDC", "west"},
    {"Mumbai", "Maharashtra", "MH 01", "Wadala Truck Terminal", "west"},
    {"Pune", "Maharashtra", "MH 12", "Hadapsar Industrial Estate", "west"},
    {"Nagpur", "Maharashtra", "MH 31", "Kalamna Market Yard", "central"},
    {"Indore", "Madhya Pradesh", "MP 09", "Transport Nagar", "central"},
    {"Bhopal", "Madhya Pradesh", "MP 04", "Govindpura Industrial Area", "central"},
    {"Patna", "Bihar", "BR 01", "Transport Nagar", "east"},
    {"Kolkata", "West Bengal", "WB 02", "Taratala Industrial Estate", "east"},
    {"Bhubaneswar", "Odisha", "OD 02", "Mancheswar Industrial Estate", "east"},
    {"Hyderabad", "Telangana", "TG 09", "Autonagar", "south"},
    {"Visakhapatnam", "Andhra Pradesh", "AP 31", "Autonagar", "south"},
    {"Bangalore", "Karnataka", "KA 01", "Peenya Industrial Area", "south"},
    {"Chennai", "Tamil Nadu", "TN 01", "Ambattur Industrial Estate", "south"},
    {"Kochi", "Kerala", "KL 07", "Kalamassery Industrial Area", "south"},
};

const std::vector<std::string> kFirstNames = {
    "Aakash", "Abhishek", "Ajay", "Akhil", "Amit", "Anand", "Anil", "Anjali", "Arjun", "Arun", "Ashok",
    "Babu", "Deepak", "Dinesh", "Ganesh", "Gopal", "Hari", "Imran", "Irfan", "Jagdish", "Kamal", "Karan",
    "Kavita", "Kishore", "Krishna", "Lakshman", "Mahesh", "Manoj", "Meena", "Mohan", "Mukesh", "Naresh",
    "Naveen", "Pankaj", "Pooja", "Prakash", "Pradeep", "Priya", "Rahul", "Rajesh", "Rakesh", "Ramesh",
    "Ravi", "Rekha", "Sachin", "Salim", "Sandeep", "Sanjay", "Santosh", "Satish", "Shankar", "Sunil",
    "Sunita", "Suresh", "Vijay", "Vikram", "Vinod", "Yogesh"};

const std::map<std::string, std::vector<std::string>> kSurnames = {
    {"north", {"Sharma", "Verma", "Singh", "Yadav", "Chauhan", "Rathore", "Gill", "Sandhu", "Mishra",
               "Tiwari", "Pandey", "Gupta", "Saini", "Khan"}},
    {"west", {"Patel", "Shah", "Mehta", "Desai", "Patil", "Joshi", "Kulkarni", "Deshmukh", "Pawar",
              "Jadhav", "Shaikh", "Bhatt"}},
    {"central", {"Verma", "Tiwari", "Dubey", "Jain", "Raghuwanshi", "Thakur", "Patel", "Sahu", "Malviya",
                 "Chouhan", "Qureshi"}},
    {"east", {"Das", "Ghosh", "Banerjee", "Mukherjee", "Mohanty", "Sahoo", "Behera", "Prasad", "Jha",
              "Singh", "Paswan", "Mondal"}},
    {"south", {"Reddy", "Naidu", "Rao", "Iyer", "Nair", "Menon", "Pillai", "Gowda", "Hegde", "Murthy",
               "Krishnan", "Thomas", "Joseph", "Varghese"}},
};

const std::map<std::string, std::vector<std::string>> kCompanySuffixes = {
    {"grain", {"Agro Foods", "Grain Traders", "Rice Mills"}},
    {"cloth", {"Textiles", "Fabrics", "Garments"}},
    {"cement", {"Cement Agency", "Building Materials"}},
    {"sand", {"Infra Projects", "Constructions", "Sand Suppliers"}},
    {"cartons", {"Distributors", "Traders", "Enterprises"}},
};

const std::vector<std::string> kCustomerAreas = {"Main Market", "Industrial Area", "Old City", "Station Road", "Ring Road"};

// Driver logins from the original truck_driver.csv, kept at the Noida warehouse
const std::vector<std::string> kOriginalDrivers = {
    "raam", "shyam", "raju", "amit", "rohit", "suresh", "ramesh", "mukesh", "vijay", "ajay",
    "vikram", "rahul", "sachin", "sanjay", "sunil", "anil", "ashok", "rajesh", "rakesh", "manish",
    "manoj", "pankaj", "praveen", "deepak", "dinesh", "mahesh", "naresh", "prakash", "pradeep", "mohan"};

const std::uint32_t kRandomSeed = 20261025;
const int kTotalTrucks = 800;
const int kNoidaTrucks = 43;  // 70% of 43 is 30: exactly the 30 original driver logins
const int kMinTrucks = 30;
const int kMaxTrucks = 50;
const int kDriverPercent = 70;  // approved drivers per 100 trucks
const int kHmvPercent = 70;     // drivers holding a heavy-vehicle licence
const int kCustomersPerWarehouse = 3;

// --- PLANNED RECORDS ---
// Everything is decided in memory first, then written in one transaction
struct PlannedUser {
    std::string role, username, fullName, email, passwordHash;
    std::size_t warehouse = 0;                       // managers and drivers
    std::string employeeCode;                        // managers
    std::string licenceNo, licenceClass, approval;   // drivers
    std::string companyName, city, address;          // customers
};

struct PlannedTruck {
    std::size_t warehouse;
    std::size_t model;
    std::string plate;
};

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

std::string capitalise(std::string text) {
    if (!text.empty()) text[0] = char(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

// "Reddy Textiles" -> "reddy_textiles"
std::string slug(const std::string& text) {
    std::string out;
    for (char ch : text) {
        unsigned char c = static_cast<unsigned char>(ch);  // <cctype> needs a non-negative value
        out += std::isalnum(c) ? char(std::tolower(c)) : '_';
    }
    return out;
}

// Adds 2, 3 ... to the name until it is not taken yet
std::string uniqueName(const std::string& base, std::set<std::string>& taken) {
    std::string name = base;
    for (int n = 2; !taken.insert(name).second; ++n) name = base + std::to_string(n);
    return name;
}

// e.g. "UP 16 KT 4821"; I, O and Q are left out because they look like 1 and 0
std::string makePlate(SeedRandom& random, const std::string& code, std::set<std::string>& taken) {
    static const std::string letters = "ABCDEFGHJKLMNPRSTUVWXYZ";
    std::string plate;
    do {
        char series[3] = {letters[std::size_t(random.between(0, int(letters.size()) - 1))],
                          letters[std::size_t(random.between(0, int(letters.size()) - 1))], '\0'};
        char number[8];
        std::snprintf(number, sizeof(number), "%04d", random.between(1, 9999));
        plate = code + " " + series + " " + number;
    } while (!taken.insert(plate).second);
    return plate;
}

// e.g. "DL-UP16-2014-04821" (deliberately not the official format, so it can't match a real licence)
std::string makeLicence(SeedRandom& random, std::string code, std::set<std::string>& taken) {
    code.erase(std::remove(code.begin(), code.end(), ' '), code.end());
    std::string licence;
    do {
        // Separate statements: C++ doesn't fix the order in which function arguments are evaluated,
        // so two random calls inside one call could swap between compilers and change the data
        int year = random.between(2005, 2023);
        int serial = random.between(1, 99999);
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "DL-%s-%d-%05d", code.c_str(), year, serial);
        licence = buffer;
    } while (!taken.insert(licence).second);
    return licence;
}

// Each hash takes ~50 ms on purpose, so the ~660 demo accounts are hashed on all CPU cores
void hashPasswords(std::vector<PlannedUser>& users) {
    unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    std::vector<std::exception_ptr> errors(workers);
    for (unsigned w = 0; w < workers; ++w) {
        threads.emplace_back([&users, &errors, w, workers] {
            try {
                for (std::size_t i = w; i < users.size(); i += workers) users[i].passwordHash = hashPassword(kDemoPassword);
            } catch (...) {
                errors[w] = std::current_exception();  // an exception must not escape a thread
            }
        });
    }
    for (std::thread& t : threads) t.join();
    for (const std::exception_ptr& error : errors) {
        if (error) std::rethrow_exception(error);
    }
}

// Returns the new truck model ids, in the order of kModels
std::vector<std::int64_t> insertCatalogues(Database& db) {
    Statement addBody = db.prepare("INSERT INTO body_types (code, name, description) VALUES (?1, ?2, ?3)");
    for (const BodyTypeSeed& body : kBodyTypes) {
        addBody.bind(1, body.code).bind(2, body.name).bind(3, body.description).run();
        addBody.reset();
    }

    Statement addGoods = db.prepare(
        "INSERT INTO goods_types (code, name, unit, kg_per_unit, m3_per_unit, keep_dry, is_food, handling) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    Statement addGoodsBody = db.prepare("INSERT INTO goods_body_types (goods_code, body_type) VALUES (?1, ?2)");
    Statement addPair = db.prepare("INSERT INTO goods_compatibility (goods_a, goods_b) VALUES (?1, ?2)");
    for (const GoodsSeed& goods : kGoods) {
        addGoods.bind(1, goods.code).bind(2, goods.name).bind(3, goods.unit).bind(4, goods.kgPerUnit)
            .bind(5, goods.m3PerUnit).bind(6, int(goods.keepDry)).bind(7, int(goods.isFood)).bind(8, goods.handling).run();
        addGoods.reset();
        for (const std::string& body : goods.bodyTypes) {
            addGoodsBody.bind(1, goods.code).bind(2, body).run();
            addGoodsBody.reset();
        }
        addPair.bind(1, goods.code).bind(2, goods.code).run();
        addPair.reset();
    }
    for (const auto& [a, b] : kCompatiblePairs) {
        addPair.bind(1, a).bind(2, b).run();
        addPair.reset();
        addPair.bind(1, b).bind(2, a).run();
        addPair.reset();
    }

    std::vector<std::int64_t> modelIds;
    Statement addModel = db.prepare(
        "INSERT INTO truck_models (name, body_type, payload_kg, volume_m3, gross_weight_kg, cost_per_km) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    for (const ModelSeed& model : kModels) {
        addModel.bind(1, model.name).bind(2, model.bodyType).bind(3, model.payloadKg).bind(4, model.volumeM3)
            .bind(5, model.grossWeightKg).bind(6, model.costPerKm).run();
        addModel.reset();
        modelIds.push_back(db.lastInsertId());
    }
    return modelIds;
}

}  // namespace

DatabaseCounts countRecords(Database& db) {
    Statement query = db.prepare(R"SQL(
        SELECT (SELECT COUNT(*) FROM warehouses),
               (SELECT COUNT(*) FROM trucks),
               (SELECT COUNT(*) FROM drivers WHERE approval = 'approved'),
               (SELECT COUNT(*) FROM drivers WHERE approval = 'pending'),
               (SELECT COUNT(*) FROM managers),
               (SELECT COUNT(*) FROM customers))SQL");
    query.step();
    DatabaseCounts counts;
    counts.warehouses = int(query.columnInt(0));
    counts.trucks = int(query.columnInt(1));
    counts.drivers = int(query.columnInt(2));
    counts.pendingDrivers = int(query.columnInt(3));
    counts.managers = int(query.columnInt(4));
    counts.customers = int(query.columnInt(5));
    return counts;
}

DatabaseCounts seedDemoData(Database& db, const std::map<std::string, std::vector<Edge>>& network) {
    for (const WarehouseSeed& warehouse : kWarehouses) {
        if (findCity(warehouse.city, network) != warehouse.city) {
            throw std::runtime_error(std::string("Warehouse city '") + warehouse.city + "' is not in network_map.csv");
        }
    }

    SeedRandom random(kRandomSeed);
    const std::size_t warehouseCount = kWarehouses.size();

    // 1. Fleet sizes: Noida is fixed, the others get 30-50 trucks, then random ones are
    //    nudged up or down (staying within 30-50) until the total is exactly 800
    std::vector<int> truckCounts(warehouseCount, kNoidaTrucks);
    int totalTrucks = kNoidaTrucks;
    for (std::size_t w = 1; w < warehouseCount; ++w) {
        truckCounts[w] = random.between(kMinTrucks, kMaxTrucks);
        totalTrucks += truckCounts[w];
    }
    while (totalTrucks != kTotalTrucks) {
        std::size_t w = std::size_t(random.between(1, int(warehouseCount) - 1));
        if (totalTrucks < kTotalTrucks && truckCounts[w] < kMaxTrucks) {
            ++truckCounts[w];
            ++totalTrucks;
        } else if (totalTrucks > kTotalTrucks && truckCounts[w] > kMinTrucks) {
            --truckCounts[w];
            --totalTrucks;
        }
    }

    std::vector<int> modelShares;
    for (const ModelSeed& model : kModels) modelShares.push_back(model.fleetShare);

    // The original logins are reserved first, so a generated name can never take one of them
    std::set<std::string> usernames = {"admin", "sarah_logistics"};
    usernames.insert(kOriginalDrivers.begin(), kOriginalDrivers.end());
    std::set<std::string> plates, licences;
    std::vector<PlannedTruck> trucks;
    std::vector<PlannedUser> users;

    for (std::size_t w = 0; w < warehouseCount; ++w) {
        const WarehouseSeed& warehouse = kWarehouses[w];
        const std::vector<std::string>& surnames = kSurnames.at(warehouse.region);

        for (int t = 0; t < truckCounts[w]; ++t) {
            trucks.push_back({w, random.pickWeighted(modelShares), makePlate(random, warehouse.plateCode, plates)});
        }

        // One manager per warehouse; the two original manager logins keep their names
        PlannedUser manager;
        manager.role = "manager";
        manager.warehouse = w;
        char code[16];
        std::snprintf(code, sizeof(code), "MGR-%03zu", w + 1);
        manager.employeeCode = code;
        if (w == 0) {
            manager.username = "admin";
            manager.fullName = "Apurv Kumar";
        } else if (std::string(warehouse.city) == "Mumbai") {
            manager.username = "sarah_logistics";
            manager.fullName = "Sarah Fernandes";
        } else {
            std::string first = random.pick(kFirstNames);  // one random call per statement (see makeLicence)
            manager.fullName = first + " " + random.pick(surnames);
            manager.username = uniqueName("mgr." + lowercase(warehouse.city), usernames);
        }
        users.push_back(manager);

        // Drivers for ~70% of the fleet, plus one applicant waiting for the manager's approval
        int approvedDrivers = (truckCounts[w] * kDriverPercent + 50) / 100;
        for (int d = 0; d <= approvedDrivers; ++d) {
            PlannedUser driver;
            driver.role = "driver";
            driver.warehouse = w;
            std::string first = random.pick(kFirstNames);
            std::string last = random.pick(surnames);
            if (w == 0 && d < int(kOriginalDrivers.size())) {
                driver.username = kOriginalDrivers[std::size_t(d)];
                first = capitalise(driver.username);
            } else {
                driver.username = uniqueName(lowercase(first) + "." + lowercase(last), usernames);
            }
            driver.fullName = first + " " + last;
            driver.licenceClass = random.percent(kHmvPercent) ? "HMV" : "LMV";
            driver.licenceNo = makeLicence(random, warehouse.plateCode, licences);
            driver.approval = d < approvedDrivers ? "approved" : "pending";
            users.push_back(driver);
        }

        // Local businesses, each mainly shipping one kind of goods
        for (int c = 0; c < kCustomersPerWarehouse; ++c) {
            const GoodsSeed& goods = kGoods[(w * kCustomersPerWarehouse + std::size_t(c)) % kGoods.size()];
            std::string surname = random.pick(surnames);
            PlannedUser customer;
            customer.role = "customer";
            customer.companyName = surname + " " + random.pick(kCompanySuffixes.at(goods.code));
            customer.fullName = random.pick(kFirstNames) + " " + surname;
            customer.username = uniqueName(slug(customer.companyName), usernames);
            customer.city = warehouse.city;
            std::string number = std::to_string(random.between(1, 250));
            customer.address = number + ", " + random.pick(kCustomerAreas) + ", " + warehouse.city;
            users.push_back(customer);
        }
    }
    for (PlannedUser& user : users) user.email = user.username + "@example.com";  // example.com is reserved for demos

    std::cout << "[DB] Hashing " << users.size() << " demo passwords..." << std::endl;
    hashPasswords(users);

    // 2. Write everything in one transaction: either the whole company is created or nothing is
    Transaction tx(db);
    std::vector<std::int64_t> modelIds = insertCatalogues(db);

    std::vector<std::int64_t> warehouseIds;
    Statement addWarehouse = db.prepare("INSERT INTO warehouses (code, name, city, state, address) VALUES (?1, ?2, ?3, ?4, ?5)");
    for (std::size_t w = 0; w < warehouseCount; ++w) {
        const WarehouseSeed& warehouse = kWarehouses[w];
        char code[8];
        std::snprintf(code, sizeof(code), "WH-%02zu", w + 1);
        addWarehouse.bind(1, std::string(code)).bind(2, std::string(warehouse.city) + " Warehouse").bind(3, warehouse.city)
            .bind(4, warehouse.state).bind(5, std::string(warehouse.area) + ", " + warehouse.city).run();
        addWarehouse.reset();
        warehouseIds.push_back(db.lastInsertId());
    }

    Statement addTruck = db.prepare("INSERT INTO trucks (warehouse_id, model_id, plate_no) VALUES (?1, ?2, ?3)");
    for (const PlannedTruck& truck : trucks) {
        addTruck.bind(1, warehouseIds[truck.warehouse]).bind(2, modelIds[truck.model]).bind(3, truck.plate).run();
        addTruck.reset();
    }

    Statement addUser = db.prepare("INSERT INTO users (username, password_hash, role, full_name, email) VALUES (?1, ?2, ?3, ?4, ?5)");
    Statement addManager = db.prepare("INSERT INTO managers (user_id, warehouse_id, employee_code) VALUES (?1, ?2, ?3)");
    Statement addDriver = db.prepare(
        "INSERT INTO drivers (user_id, warehouse_id, licence_no, licence_class, approval) VALUES (?1, ?2, ?3, ?4, ?5)");
    Statement addCustomer = db.prepare("INSERT INTO customers (user_id, company_name, city, address) VALUES (?1, ?2, ?3, ?4)");
    for (const PlannedUser& user : users) {
        addUser.bind(1, user.username).bind(2, user.passwordHash).bind(3, user.role).bind(4, user.fullName).bind(5, user.email).run();
        addUser.reset();
        std::int64_t userId = db.lastInsertId();
        if (user.role == "manager") {
            addManager.bind(1, userId).bind(2, warehouseIds[user.warehouse]).bind(3, user.employeeCode).run();
            addManager.reset();
        } else if (user.role == "driver") {
            addDriver.bind(1, userId).bind(2, warehouseIds[user.warehouse]).bind(3, user.licenceNo)
                .bind(4, user.licenceClass).bind(5, user.approval).run();
            addDriver.reset();
        } else {
            addCustomer.bind(1, userId).bind(2, user.companyName).bind(3, user.city).bind(4, user.address).run();
            addCustomer.reset();
        }
    }
    tx.commit();

    // New writes first go to logistics.db-wal; copy them into logistics.db now,
    // so database viewers that open only the main file see the data too
    db.exec("PRAGMA wal_checkpoint(TRUNCATE);");
    return countRecords(db);
}
