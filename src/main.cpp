#include <curl/curl.h>
#include <iostream>
#include <string>
#include "../include/db.h"
#include "../include/network.h"
#include "../include/orders.h"
#include "../include/schema.h"
#include "../include/seed.h"
#include "../include/server.h"
#include "../include/settings.h"

using namespace std;

const string kDatabasePath = "data/logistics.db";
const string kSettingsPath = ".env";

int main() {
    cout << "===========================================" << endl;
    cout << "   LOGISTICS ERP BACKEND                   " << endl;
    cout << "===========================================" << endl;

    // --- SETTINGS: port, Google sign-in and the mail server, from .env (see .env.example) ---
    Settings settings;
    try {
        settings = loadSettings(kSettingsPath);
    } catch (const exception& e) {
        cerr << "[FATAL] Settings: " << e.what() << endl;
        return 1;
    }
    // libcurl sends the emails and asks Google about sign-ins; it must be started once, before any thread uses it
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        cerr << "[FATAL] libcurl could not start" << endl;
        return 1;
    }
    if (settings.googleOn()) cout << "[AUTH] Google sign-in is on." << endl;
    else cout << "[AUTH] Google sign-in is off: put GOOGLE_CLIENT_ID in .env to turn it on (see .env.example)." << endl;
    if (settings.emailOn()) cout << "[MAIL] Login codes are emailed from " << settings.mailFrom << "." << endl;
    else cout << "[MAIL] Email isn't set up: login codes are printed in this window (see .env.example to email them)." << endl;

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
    registerRoutes(svr, roadMap, kDatabasePath, settings);

    // The HTTP library lets several programs share a port (SO_REUSEPORT), so an old copy of the server could keep
    // answering while this one waits unnoticed. Only SO_REUSEADDR is kept: a quick restart still works, but a port
    // that another program is listening on is refused.
    svr.set_socket_options([](socket_t sock) { httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1); });
    if (!svr.bind_to_port("0.0.0.0", settings.port)) {
        cerr << "[FATAL] Port " << settings.port << " is already in use. Is another ./app still running? "
             << "Stop it with Ctrl+C, or choose another port with PORT= in .env." << endl;
        return 1;
    }
    cout << "Server successfully initialized!" << endl;
    cout << "-> Open your browser and navigate to http://localhost:" << settings.port << "/" << endl;
    svr.listen_after_bind();

    curl_global_cleanup();
    return 0;
}
