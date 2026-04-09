#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <queue>
#include <algorithm>
#include <iomanip>
#include "../include/algorithms.h"
#include "../include/httplib.h"
#include "../include/json.hpp"

using namespace std;
using json = nlohmann::json;

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

int main() {
    cout << "===========================================" << endl;
    cout << "   LOGISTICS ERP BACKEND - NOIDA HUB       " << endl;
    cout << "===========================================" << endl;

    map<string, vector<Edge>> network = loadNetworkMap();
    cout << "[SUCCESS] Network Map Loaded (" << network.size() << " Cities Synced)." << endl;

    httplib::Server svr;

    // Serve all UI files in the 'web' folder dynamically
    svr.set_mount_point("/", "./web");

    svr.Post("/api/optimize", [&network](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            vector<Shipment> shipments;
            
            for (const auto& item : j["shipments"]) {
                double w = item["weight"].is_string()
                    ? stod(item["weight"].get<string>())
                    : item["weight"].get<double>();
                double v = item["volume"].is_string()
                    ? stod(item["volume"].get<string>())
                    : item["volume"].get<double>();
                shipments.push_back({
                    item["id"].get<int>(),
                    w, v,
                    item["dest"].get<string>()
                });
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
            bool foundValidRoute = false;

            for(const auto& targetCity : uniqueDestinations) {
                auto result = runDijkstraRouting("Noida", targetCity, network);
                if (result.first != 1e9) {
                    foundValidRoute = true;
                    totalDist += result.first;
                    double manualDist = result.first * 1.25;
                    totalManualDist += manualDist;
                    double routeSavings = (manualDist - result.first) * 15.50;
                    totalSavings += routeSavings; 
                    
                    string pathStr;
                    for (size_t i = 0; i < result.second.size(); ++i) {
                        pathStr += result.second[i];
                        if (i != result.second.size() - 1) pathStr += " -> ";
                    }
                    verdict += "To <strong>" + targetCity + "</strong>: [" + pathStr + "]<br>";
                    
                    response["route_details"].push_back({
                        {"destination", targetCity},
                        {"optimized_km", result.first},
                        {"manual_km", manualDist},
                        {"savings", routeSavings},
                        {"path", pathStr}
                    });
                } else {
                    verdict += "<span style='color:red;'>Unreachable: " + targetCity + "</span><br>";
                }
            }
            
            if(!foundValidRoute && !uniqueDestinations.empty()) {
                verdict = "No valid route sequences found from Noida for given destinations.";
            } else if (uniqueDestinations.empty()) {
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
            json err = {{"error", e.what()}};
            res.status = 500;
            res.set_content(err.dump(), "application/json");
        }
    });

    cout << "Server successfully initialized!" << endl;
    cout << "-> Open your browser and navigate to http://localhost:8080/login.html" << endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}