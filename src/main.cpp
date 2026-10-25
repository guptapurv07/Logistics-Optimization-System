#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include "../include/algorithms.h"
#include "../include/db.h"
#include "../include/schema.h"
#include "../include/seed.h"
#include "../include/server.h"

using namespace std;

const string kDatabasePath = "data/logistics.db";

// --- GRAPH LOADER: READ NETWORK_MAP.CSV ---
RoadNetwork loadNetworkMap() {
    RoadNetwork adj;
    ifstream file("data/network_map.csv");
    string line, u, v, w, type;

    getline(file, line); // Skip header
    while (getline(file, line)) {
        stringstream ss(line);
        getline(ss, u, ',');
        getline(ss, v, ',');
        getline(ss, w, ',');
        getline(ss, type, ',');
        if(!u.empty() && !v.empty()) {
            adj[u].push_back({v, stoi(w), type});
            adj[v].push_back({u, stoi(w), type}); // Bidirectional
        }
    }
    return adj;
}

int main() {
    cout << "===========================================" << endl;
    cout << "   LOGISTICS ERP BACKEND                   " << endl;
    cout << "===========================================" << endl;

    RoadNetwork network = loadNetworkMap();
    cout << "[SUCCESS] Network Map Loaded (" << network.size() << " Cities Synced)." << endl;

    // --- DATABASE: the tables and the demo company are created on the first run ---
    // This connection only prepares the file; each server thread opens its own one later
    try {
        Database db(kDatabasePath);
        createSchema(db);
        DatabaseCounts counts = countRecords(db);
        if (counts.warehouses == 0) {
            cout << "[DB] New database: creating the demo company (takes a few seconds)..." << endl;
            counts = seedDemoData(db, network);
        }
        cout << "[SUCCESS] Database ready: " << counts.warehouses << " warehouses, " << counts.trucks << " trucks, "
             << counts.drivers << " drivers (+" << counts.pendingDrivers << " awaiting approval), "
             << counts.managers << " managers, " << counts.customers << " customers, "
             << counts.pendingOrders << " orders waiting for a truck." << endl;
    } catch (const exception& e) {
        cerr << "[FATAL] Database: " << e.what() << endl;
        return 1;
    }

    httplib::Server svr;

    // Serve all UI files in the 'web' folder; role pages are guarded in registerRoutes
    svr.set_mount_point("/", "./web");
    registerRoutes(svr, network, kDatabasePath);

    cout << "Server successfully initialized!" << endl;
    cout << "-> Open your browser and navigate to http://localhost:8080/login.html" << endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}
