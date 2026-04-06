#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <vector>
#include <string>
#include <map>
#include "data_models.h"

// --- Function Prototypes ---
// Dijkstra Routing (routing.cpp)
std::pair<int, std::vector<std::string>> runDijkstraRouting(std::string start, std::string end, std::map<std::string, std::vector<Edge>>& graph);

// Greedy Allocation (allocation.cpp)
std::vector<std::pair<int, int>> performGreedyAllocation(std::vector<Shipment>& shipments, std::vector<Truck>& trucks);

#endif