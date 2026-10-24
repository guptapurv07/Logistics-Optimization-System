#include "../include/algorithms.h"
#include <map>
#include <vector>
#include <queue>
#include <string>
#include <iostream>
#include <algorithm>
#include <cctype>

const int INF = 1e9;

// Lowercase copy without surrounding spaces, so " mumbai " matches "Mumbai"
static std::string normalizeCityName(const std::string& name) {
    size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = name.find_last_not_of(" \t\r\n");
    std::string out = name.substr(first, last - first + 1);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string findCity(const std::string& name, const std::map<std::string, std::vector<Edge>>& graph) {
    std::string wanted = normalizeCityName(name);
    if (wanted.empty()) return "";
    for (auto const& [city, _] : graph) {
        if (normalizeCityName(city) == wanted) return city;
    }
    return "";
}

RouteResult runDijkstraRouting(const std::string& start, const std::string& end, const std::map<std::string, std::vector<Edge>>& graph) {
    RouteResult result;

    // Cities missing from the map have no route (never treat them as 0 km away)
    if (graph.find(start) == graph.end() || graph.find(end) == graph.end()) return result;

    std::map<std::string, int> dist;
    std::map<std::string, std::string> parent;
    std::map<std::string, Edge> parentRoad; // Road used to reach each city

    for (auto const& [city, _] : graph) dist[city] = INF;
    dist[start] = 0;

    // Min-Priority Queue for Dijkstra Efficiency
    std::priority_queue<std::pair<int, std::string>, std::vector<std::pair<int, std::string>>, std::greater<std::pair<int, std::string>>> pq;
    pq.push({0, start});

    while (!pq.empty()) {
        std::string u = pq.top().second;
        int d = pq.top().first;
        pq.pop();

        if (d > dist[u]) continue;

        // find() instead of graph[u]: the shared map is read-only here
        auto roads = graph.find(u);
        if (roads == graph.end()) continue;

        for (const auto& edge : roads->second) {
            if (dist[u] + edge.weight < dist[edge.to]) {
                dist[edge.to] = dist[u] + edge.weight;
                parent[edge.to] = u;
                parentRoad[edge.to] = edge;
                pq.push({dist[edge.to], edge.to});
            }
        }
    }

    // Path Reconstruction
    if (dist[end] == INF) return result;

    for (std::string v = end; v != start; v = parent[v]) {
        result.path.push_back(v);
        result.legs.push_back(parentRoad[v]);
    }
    result.path.push_back(start);
    std::reverse(result.path.begin(), result.path.end());
    std::reverse(result.legs.begin(), result.legs.end());

    result.reachable = true;
    result.distance = dist[end];
    return result;
}
