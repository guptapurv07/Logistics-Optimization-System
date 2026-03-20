#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <vector>
#include <string>
#include <map>

// --- Routing Data Structures ---
struct Edge {
    std::string to;
    int weight;
};

// --- Allocation Data Structures ---
struct Shipment {
    int id;
    double weight;
    std::string destination;
};

struct Truck {
    int id;
    double capacity;
    double volume;
    double fuelRate;
    bool isAvailable = true;

    // Constructor for easier loading
    Truck(int i, double c, double v, double f) 
        : id(i), capacity(c), volume(v), fuelRate(f) {}
};

// --- Function Prototypes ---
// Dijkstra Routing (routing.cpp)
int runDijkstraRouting(std::string start, std::string end, std::map<std::string, std::vector<Edge>>& graph);

// Greedy Allocation (allocation.cpp)
void performGreedyAllocation(std::vector<Shipment>& shipments, std::vector<Truck>& trucks);

#endif