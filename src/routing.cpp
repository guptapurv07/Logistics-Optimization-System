#include <map>
#include <vector>
#include <queue>
#include <string>
#include <iostream>

struct Edge {
    std::string to;
    int weight;
};

// DIJKSTRA SHORTEST PATH
int runDijkstraRouting(std::string start, std::string end, std::map<std::string, std::vector<Edge>>& graph) {
    std::map<std::string, int> distances;
    for (auto const& [city, _] : graph) distances[city] = 1e9; // Initialize with Infinity
    
    distances[start] = 0;
    std::priority_queue<std::pair<int, std::string>, std::vector<std::pair<int, std::string>>, std::greater<std::pair<int, std::string>>> pq;
    
    pq.push({0, start});

    while (!pq.empty()) {
        std::string u = pq.top().second;
        int d = pq.top().first;
        pq.pop();

        if (d > distances[u]) continue;

        for (auto& edge : graph[u]) {
            if (distances[u] + edge.weight < distances[edge.to]) {
                distances[edge.to] = distances[u] + edge.weight;
                pq.push({distances[edge.to], edge.to});
            }
        }
    }
    
    return (distances[end] == 1e9) ? -1 : distances[end];
}