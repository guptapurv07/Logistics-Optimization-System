#ifndef SEED_H
#define SEED_H

#include <map>
#include <string>
#include <vector>
#include "data_models.h"
#include "db.h"

// --- DEMO DATA ---
// Fills a new database with the goods and truck catalogues and a realistic company: 20 warehouses with
// 30-50 trucks each (800 in total), drivers for about 70% of each fleet, a manager per warehouse and
// demo customers. A fixed random seed makes the data identical on every run and every machine.

// Every demo account (managers, drivers, customers) logs in with this password
inline constexpr char kDemoPassword[] = "Demo@123";

struct DatabaseCounts {
    int warehouses = 0;
    int trucks = 0;
    int drivers = 0;
    int pendingDrivers = 0;
    int managers = 0;
    int customers = 0;
};

DatabaseCounts countRecords(Database& db);

// Throws if a warehouse city is missing from the road network
DatabaseCounts seedDemoData(Database& db, const std::map<std::string, std::vector<Edge>>& network);

#endif
