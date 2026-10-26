#include <iostream>
#include <string>
#include "../include/db.h"
#include "../include/network.h"
#include "../include/orders.h"
#include "../include/schema.h"
#include "../include/seed.h"
#include "../include/server.h"

using namespace std;

const string kDatabasePath = "data/logistics.db";

int main() {
    cout << "===========================================" << endl;
    cout << "   LOGISTICS ERP BACKEND                   " << endl;
    cout << "===========================================" << endl;

    // --- ROAD MAP: cities, roads and road paths from data/ ---
    RoadMap roadMap;
    try {
        roadMap = loadRoadMap("data");
    } catch (const exception& e) {
        cerr << "[FATAL] Road map: " << e.what() << endl;
        return 1;
    }
    cout << "[SUCCESS] Road map loaded: " << roadMap.network.size() << " cities, " << roadMap.roadCount() << " roads." << endl;

    // --- DATABASE: the tables and the demo company are created on the first run ---
    // This connection only prepares the file; each server thread opens its own one later
    try {
        Database db(kDatabasePath);
        createSchema(db);
        DatabaseCounts counts = countRecords(db);
        if (counts.warehouses == 0) {
            cout << "[DB] New database: creating the demo company (takes a few seconds)..." << endl;
            counts = seedDemoData(db, roadMap.network);
        }
        int priced = priceOldRecords(db, roadMap.network);
        if (priced > 0) cout << "[DB] Priced " << priced << " orders placed before prices existed." << endl;
        cout << "[SUCCESS] Database ready: " << counts.warehouses << " warehouses, " << counts.trucks << " trucks, "
             << counts.drivers << " drivers (+" << counts.pendingDrivers << " awaiting approval), "
             << counts.managers << " managers, " << counts.customers << " customers, "
             << counts.pendingOrders << " orders waiting for a truck." << endl;
    } catch (const exception& e) {
        cerr << "[FATAL] Database: " << e.what() << endl;
        return 1;
    }

    httplib::Server svr;

    // Serve all UI files in the 'web' folder; role pages are guarded in registerRoutes.
    // "no-cache" makes browsers check for a newer file each time, so an updated page or script shows at once.
    svr.set_mount_point("/", "./web", {{"Cache-Control", "no-cache"}});
    registerRoutes(svr, roadMap, kDatabasePath);

    cout << "Server successfully initialized!" << endl;
    cout << "-> Open your browser and navigate to http://localhost:8080/login.html" << endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}
