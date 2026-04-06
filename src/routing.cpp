#include "../include/algorithms.h"
#include <map>
#include <vector>
#include <queue>
#include <string>
#include <iostream>
#include <algorithm>

const int INF = 1e9;

std::pair<int, std::vector<std::string>> runDijkstraRouting(std::string start, std::string end, std::map<std::string, std::vector<Edge>>& graph) {
    std::map<std::string, int> dist;
    std::map<std::string, std::string> parent;
    
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

        for (auto& edge : graph[u]) {
            if (dist[u] + edge.weight < dist[edge.to]) {
                dist[edge.to] = dist[u] + edge.weight;
                parent[edge.to] = u;
                pq.push({dist[edge.to], edge.to});
            }
        }
    }

    // Path Reconstruction
    std::vector<std::string> path;
    if (dist[end] == INF) return {INF, path};

    for (std::string v = end; v != ""; v = parent[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return {dist[end], path};
}