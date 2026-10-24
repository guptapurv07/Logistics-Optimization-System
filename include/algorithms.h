#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <vector>
#include <string>
#include <map>
#include "data_models.h"

// --- Routing Result ---
struct RouteResult {
    bool reachable = false;
    int distance = 0;
    std::vector<std::string> path;  // Cities in travel order, start first
    std::vector<Edge> legs;         // legs[i] = road taken from path[i] to path[i + 1]
};

// --- Function Prototypes ---
// Dijkstra Routing (routing.cpp)
RouteResult runDijkstraRouting(const std::string& start, const std::string& end, const std::map<std::string, std::vector<Edge>>& graph);

// City lookup that ignores case and extra spaces; returns "" if the city is not in the map (routing.cpp)
std::string findCity(const std::string& name, const std::map<std::string, std::vector<Edge>>& graph);

// Greedy Allocation (allocation.cpp)
std::vector<std::pair<int, int>> performGreedyAllocation(std::vector<Shipment>& shipments, std::vector<Truck>& trucks);

#endif
