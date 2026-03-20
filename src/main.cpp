#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <queue>
#include <algorithm>
#include <iomanip>

#ifdef _WIN32
    #include <windows.h> // For Sleep on Windows
#else
    #include <unistd.h>  // For sleep on Unix/Mac
#endif

using namespace std;

// --- DATA STRUCTURES ---
const int INF = 1e9;

struct Edge {
    string to;
    int weight;
    string type;
};

struct Truck {
    int id;
    double weightCap;
    double volCap;
    double fuelRate; // ₹ per km
};

// --- 1. DIJKSTRA CORE ENGINE (DAA Implementation) ---
pair<int, vector<string>> calculateShortestPath(string start, string end, map<string, vector<Edge>>& adj) {
    map<string, int> dist;
    map<string, string> parent;
    
    for (auto const& [city, _] : adj) dist[city] = INF;
    dist[start] = 0;

    // Min-Priority Queue for Dijkstra Efficiency
    priority_queue<pair<int, string>, vector<pair<int, string>>, greater<pair<int, string>>> pq;
    pq.push({0, start});

    while (!pq.empty()) {
        string u = pq.top().second;
        int d = pq.top().first;
        pq.pop();

        if (d > dist[u]) continue;

        for (auto& edge : adj[u]) {
            if (dist[u] + edge.weight < dist[edge.to]) {
                dist[edge.to] = dist[u] + edge.weight;
                parent[edge.to] = u;
                pq.push({dist[edge.to], edge.to});
            }
        }
    }

    // Path Reconstruction
    vector<string> path;
    if (dist[end] == INF) return {INF, path};

    for (string v = end; v != ""; v = parent[v]) path.push_back(v);
    reverse(path.begin(), path.end());
    return {dist[end], path};
}

// --- 2. DATA BRIDGE: UPDATE WEB/DATA.JS ---
void updateWebDashboard(int distance, double savings, double load) {
    ofstream file("web/data.js");
    file << "const logisticsData = {\n";
    file << "    savings: " << fixed << setprecision(2) << savings << ",\n";
    file << "    fuel: " << (distance > 0 ? 33.3 : 0.0) << ",\n";
    file << "    load: " << load << ",\n";
    file << "    assignments: []\n";
    file << "};";
    file.close();
}

// --- 3. GRAPH LOADER: READ NETWORK_MAP.CSV ---
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

// --- 4. MAIN EXECUTION LOOP ---
int main() {
    cout << "===========================================" << endl;
    cout << "   LOGISTICS ERP BACKEND - NOIDA HUB       " << endl;
    cout << "===========================================" << endl;

    map<string, vector<Edge>> network = loadNetworkMap();
    cout << "[SUCCESS] Network Map Loaded (" << network.size() << " Cities Synced)." << endl;

    // Launch UI
    #ifdef __APPLE__
        system("open web/login.html");
    #elif _WIN32
        system("start web/login.html");
    #else
        system("xdg-open web/login.html");
    #endif

    cout << "[SYSTEM] Waiting for Shipment Planning data..." << endl;

    // INFINITE MONITORING LOOP
    while (true) {
        // Simulating checking for "trigger" from web input
        // In a real demo, you can manually trigger a calculation here
        string targetCity;
        cout << "\n Enter Target City for Optimization (or 'exit'): ";
        cin >> targetCity;

        if (targetCity == "exit") break;

        // Run Dijkstra from Dehradun (Source) to User Input
        auto result = calculateShortestPath("Dehradun", targetCity, network);

        if (result.first == INF) {
            cout << "[!] No valid route found to " << targetCity << endl;
        } else {
            cout << "--- DIJKSTRA OPTIMIZATION VERDICT ---" << endl;
            cout << "Shortest Route: " << result.first << " km" << endl;
            cout << "Best Path: ";
            for (int i = 0; i < result.second.size(); i++) 
                cout << result.second[i] << (i == result.second.size()-1 ? "" : " -> ");
            cout << endl;

            // Calculate savings (Base logic: Manual Route is usually 20% longer)
            double manualDist = result.first * 1.25;
            double savings = (manualDist - result.first) * 15.50; // ₹15.50 per km

            updateWebDashboard(result.first, savings, 85.5);
            cout << "[SUCCESS] Web Dashboard updated with ₹" << savings << " savings." << endl;
        }

        #ifdef _WIN32
            Sleep(1000);
        #else
            sleep(1);
        #endif
    }

    return 0;
}