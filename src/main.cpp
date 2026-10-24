#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <queue>
#include <algorithm>
#include <iomanip>
#include <set>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <memory>
#include "../include/algorithms.h"
#include "../include/db.h"
#include "../include/schema.h"
#include "../include/seed.h"
#include "../include/httplib.h"
#include "../include/json.hpp"

using namespace std;
using json = nlohmann::json;

// Every route starts from the dispatch hub
const string HUB_CITY = "Noida";

// --- GRAPH LOADER: READ NETWORK_MAP.CSV ---
map<string, vector<Edge>> loadNetworkMap() {
    map<string, vector<Edge>> adj;
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

// --- TRUCK LOADER: READ TRUCK_DB.CSV ---
vector<Truck> loadTruckDatabase() {
    vector<Truck> trucks;
    ifstream file("data/truck_db.csv");
    if (!file.is_open()) return trucks;
    
    string line, idStr, capStr, volStr, fuelStr;
    getline(file, line); // Skip header
    while (getline(file, line)) {
        stringstream ss(line);
        if (getline(ss, idStr, ',') && getline(ss, capStr, ',') &&
            getline(ss, volStr, ',') && getline(ss, fuelStr, ',')) {
            trucks.push_back(Truck(stoi(idStr), stod(capStr), stod(volStr), stod(fuelStr)));
        }
    }
    return trucks;
}

// --- DRIVER LOADER: READ TRUCK_DRIVER.CSV ---
map<int, pair<string, string>> loadDriverDatabase() {
    map<int, pair<string, string>> drivers;
    ifstream file("data/truck_driver.csv");
    if (!file.is_open()) return drivers;
    
    string line, user, pass, role, license, tidStr;
    getline(file, line); // Skip header
    while (getline(file, line)) {
        stringstream ss(line);
        if (getline(ss, user, ',') && getline(ss, pass, ',') &&
            getline(ss, role, ',') && getline(ss, license, ',') &&
            getline(ss, tidStr, ',')) {
            drivers[stoi(tidStr)] = {user, license};
        }
    }
    return drivers;
}

// --- REQUEST VALIDATION ---
// Read a number sent as a JSON number or as text like "12.5"; false for anything else (e.g. "12abc", "nan")
bool readNumber(const json& value, double& out) {
    double number;
    if (value.is_number()) {
        number = value.get<double>();
    } else if (value.is_string()) {
        const string& text = value.get_ref<const string&>();
        char* end = nullptr;
        number = strtod(text.c_str(), &end);
        if (end == text.c_str()) return false;
        while (*end == ' ' || *end == '\t') ++end;
        if (*end != '\0') return false;
    } else {
        return false;
    }
    if (!isfinite(number)) return false;
    out = number;
    return true;
}

// Shipment IDs must be whole numbers from 1 up
bool readShipmentId(const json& value, int& out) {
    if (!value.is_number_integer()) return false;
    long long id = value.get<long long>();
    if (id < 1 || id > INT_MAX) return false;
    out = (int)id;
    return true;
}

// Returns "" and fills `out` when the shipment is usable; otherwise returns an issue code and sets `reason`.
// Every field is checked before use: reading a missing field used to crash the whole server.
string validateShipment(const json& item, const map<string, vector<Edge>>& network, set<int>& usedIds, Shipment& out, string& reason) {
    if (!item.is_object()) {
        reason = "not a shipment record";
        return "invalid_shipment";
    }
    if (!item.contains("id") || !readShipmentId(item.at("id"), out.id)) {
        reason = "ID must be a whole number from 1 up";
        return "invalid_id";
    }
    // The first shipment with an ID keeps it; a second one would get mixed up with it
    if (!usedIds.insert(out.id).second) {
        reason = "ID " + to_string(out.id) + " is already used by another shipment";
        return "duplicate_id";
    }
    if (!item.contains("weight") || !readNumber(item.at("weight"), out.weight) || out.weight <= 0) {
        reason = "weight must be a number above 0 kg";
        return "invalid_weight";
    }

    // Volume is optional: the dashboard sends 0 when the box is left empty
    out.volume = 0;
    json volume = item.contains("volume") ? item.at("volume") : json();
    if (!volume.is_null() && (!readNumber(volume, out.volume) || out.volume < 0)) {
        reason = "volume must be a number, 0 or more";
        return "invalid_volume";
    }

    json dest = item.contains("dest") ? item.at("dest") : json();
    if (!dest.is_null() && !dest.is_string()) {
        reason = "destination must be a city name";
        return "invalid_destination";
    }
    string typed = dest.is_string() ? dest.get<string>() : "";
    if (typed.find_first_not_of(" \t\r\n") == string::npos) {
        reason = "no destination given";
        return "missing_destination";
    }

    // Match the typed destination to a city in the network map (ignores case and extra spaces)
    out.destination = findCity(typed, network);
    if (out.destination.empty()) {
        reason = "unknown city '" + typed + "'";
        return "unknown_city";
    }
    if (out.destination == HUB_CITY) {
        reason = out.destination + " is the dispatch hub, not a delivery city";
        return "hub_destination";
    }
    return "";
}

int main() {
    cout << "===========================================" << endl;
    cout << "   LOGISTICS ERP BACKEND - NOIDA HUB       " << endl;
    cout << "===========================================" << endl;

    map<string, vector<Edge>> network = loadNetworkMap();
    cout << "[SUCCESS] Network Map Loaded (" << network.size() << " Cities Synced)." << endl;

    // --- DATABASE: the tables and the demo company are created on the first run ---
    unique_ptr<Database> db;
    try {
        db = make_unique<Database>("data/logistics.db");
        createSchema(*db);
        DatabaseCounts counts = countRecords(*db);
        if (counts.warehouses == 0) {
            cout << "[DB] New database: creating the demo company (takes a few seconds)..." << endl;
            counts = seedDemoData(*db, network);
        }
        cout << "[SUCCESS] Database ready: " << counts.warehouses << " warehouses, " << counts.trucks << " trucks, "
             << counts.drivers << " drivers (+" << counts.pendingDrivers << " awaiting approval), "
             << counts.managers << " managers, " << counts.customers << " customers." << endl;
    } catch (const exception& e) {
        cerr << "[FATAL] Database: " << e.what() << endl;
        return 1;
    }

    httplib::Server svr;

    // Serve all UI files in the 'web' folder dynamically
    svr.set_mount_point("/", "./web");

    svr.Post("/api/optimize", [&network](const httplib::Request& req, httplib::Response& res) {
        try {
            // A request that isn't JSON like {"shipments": [...]} can't be processed at all
            json j = json::parse(req.body, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains("shipments") || !j.at("shipments").is_array()) {
                json err = {{"error", "Send JSON like {\"shipments\": [ ... ]}"}};
                res.status = 400;
                res.set_content(err.dump(), "application/json");
                return;
            }

            vector<Shipment> shipments;
            json rejected = json::array();
            string rejectedNotes;
            set<int> usedIds;
            int position = 0;
            
            for (const auto& item : j.at("shipments")) {
                ++position;
                Shipment s{0, 0, 0, ""};
                string reason;
                string issue = validateShipment(item, network, usedIds, s, reason);

                // Bad shipments are reported back instead of being routed or given a truck
                if (!issue.empty()) {
                    // Send back what the dashboard can display: the typed city, and the weight if it is a number
                    double shownWeight = 0;
                    bool hasWeight = item.is_object() && item.contains("weight") && readNumber(item.at("weight"), shownWeight);
                    bool hasDest = item.is_object() && item.contains("dest") && item.at("dest").is_string();
                    rejected.push_back({
                        {"shipment", s.id > 0 ? json(s.id) : json()},
                        {"truck", -1},
                        {"dest", hasDest ? item.at("dest") : json("")},
                        {"weight", hasWeight ? json(shownWeight) : json()},
                        {"issue", issue}
                    });
                    string who = s.id > 0 ? "Shipment #" + to_string(s.id) : "Entry " + to_string(position);
                    rejectedNotes += "<span style='color:red;'>" + who + " skipped: " + reason + "</span><br>";
                    continue;
                }

                shipments.push_back(s);
            }

            // Load trucks representing our dynamic fleet from database
            vector<Truck> trucks = loadTruckDatabase();

            auto assignments = performGreedyAllocation(shipments, trucks);

            json response;
            response["assignments"] = json::array();
            
            response["fleet_status"] = json::array();
            
            vector<string> uniqueDestinations;
            for(auto& s: shipments) {
                if(find(uniqueDestinations.begin(), uniqueDestinations.end(), s.destination) == uniqueDestinations.end()) {
                    uniqueDestinations.push_back(s.destination);
                }
            }

            response["route_details"] = json::array();

            double totalSavings = 0;
            int totalDist = 0;
            double totalManualDist = 0;
            string verdict = "Optimization Summary:<br>";

            for(const auto& targetCity : uniqueDestinations) {
                RouteResult route = runDijkstraRouting(HUB_CITY, targetCity, network);
                if (route.reachable) {
                    totalDist += route.distance;
                    double manualDist = route.distance * 1.25;
                    totalManualDist += manualDist;
                    double routeSavings = (manualDist - route.distance) * 15.50;
                    totalSavings += routeSavings; 
                    
                    string pathStr;
                    for (size_t i = 0; i < route.path.size(); ++i) {
                        pathStr += route.path[i];
                        if (i != route.path.size() - 1) pathStr += " -> ";
                    }

                    // Road taken on each leg, e.g. Noida -> Jaipur on "NH-48 Direct"
                    json legs = json::array();
                    for (size_t i = 0; i < route.legs.size(); ++i) {
                        legs.push_back({
                            {"from", route.path[i]},
                            {"to", route.legs[i].to},
                            {"road", route.legs[i].type},
                            {"km", route.legs[i].weight}
                        });
                    }
                    verdict += "To <strong>" + targetCity + "</strong>: [" + pathStr + "]<br>";
                    
                    response["route_details"].push_back({
                        {"destination", targetCity},
                        {"optimized_km", route.distance},
                        {"manual_km", manualDist},
                        {"savings", routeSavings},
                        {"path", pathStr},
                        {"legs", legs}
                    });
                } else {
                    verdict += "<span style='color:red;'>Unreachable: " + targetCity + "</span><br>";
                }
            }
            
            // Explain skipped shipments too, so a bad destination is never silently ignored
            verdict += rejectedNotes;

            if (uniqueDestinations.empty() && rejected.empty()) {
                verdict = "No destination provided by dispatch array.";
            }

            // Load drivers for 1:1 asset tracking
            auto driverMap = loadDriverDatabase();

            // Inject Fleet constraints logic
            for (const auto& t : trucks) {
                 string d_name = "Unassigned";
                 string d_license = "N/A";
                 if (driverMap.count(t.id)) {
                     d_name = driverMap[t.id].first;
                     d_license = driverMap[t.id].second;
                 }
                 
                 response["fleet_status"].push_back({
                     {"id", t.id},
                     {"capacity", t.capacity},
                     {"isAvailable", t.isAvailable},
                     {"driver_name", d_name},
                     {"driver_license", d_license}
                 });
            }

            for (auto& pair : assignments) {
                // Look up original shipment for metadata
                auto it = find_if(shipments.begin(), shipments.end(), [pair](const Shipment& s) { return s.id == pair.first; });
                string dest = it != shipments.end() ? it->destination : "Unknown";
                double weight = it != shipments.end() ? it->weight : 0;
                
                response["assignments"].push_back({
                    {"shipment", pair.first},
                    {"truck", pair.second},
                    {"dest", dest},
                    {"weight", weight}
                });
            }

            // Skipped shipments are listed too, with the reason in "issue"
            for (const auto& r : rejected) response["assignments"].push_back(r);

            response["savings"] = totalSavings;
            response["routing_manual_km"] = totalManualDist;
            response["routing_optimized_km"] = totalDist;

            // Route Efficiency: % saved by Dijkstra vs brute manual routing
            double routeEfficiency = (totalManualDist > 0)
                ? ((totalManualDist - totalDist) / totalManualDist) * 100.0
                : 0.0;

            // Fleet Utilization: % of trucks currently dispatched on routes
            int trucksInUse = 0;
            for (const auto& t : trucks) {
                if (!t.isAvailable) trucksInUse++;
            }
            double fleetUtilization = trucks.empty() ? 0.0
                : ((double)trucksInUse / trucks.size()) * 100.0;

            response["fuel"] = routeEfficiency;
            response["load"] = fleetUtilization;
            response["verdict"] = verdict;

            res.set_content(response.dump(), "application/json");
            
            cout << "[API] Optimization computed for batch size " << uniqueDestinations.size() << endl;

        } catch (const exception& e) {
            // Details go to the server console only; the browser gets a plain message
            cerr << "[API ERROR] " << e.what() << endl;
            json err = {{"error", "Internal server error"}};
            res.status = 500;
            res.set_content(err.dump(), "application/json");
        }
    });

    cout << "Server successfully initialized!" << endl;
    cout << "-> Open your browser and navigate to http://localhost:8080/login.html" << endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}