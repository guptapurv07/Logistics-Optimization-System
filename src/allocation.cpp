#include <vector>
#include <algorithm>
#include <iostream>

struct Shipment {
    int id;
    double weight;
    std::string destination;
};

struct Truck {
    int id;
    double capacity;
    bool isAvailable = true;
};

// GREEDY BEST-FIT ALLOCATION
void performGreedyAllocation(std::vector<Shipment>& shipments, std::vector<Truck>& trucks) {
    // Sort shipments by weight (descending) to fill larger items first
    std::sort(shipments.begin(), shipments.end(), [](const Shipment& a, const Shipment& b) {
        return a.weight > b.weight;
    });

    // Sort trucks by capacity (ascending) to find the "Tightest Fit"
    std::sort(trucks.begin(), trucks.end(), [](const Truck& a, const Truck& b) {
        return a.capacity < b.capacity;
    });

    std::cout << "\n--- Greedy Allocation Results ---" << std::endl;
    for (auto& s : shipments) {
        bool assigned = false;
        for (auto& t : trucks) {
            if (t.isAvailable && t.capacity >= s.weight) {
                std::cout << "[MATCH] Shipment #" << s.id << " (" << s.weight << "kg) -> Truck ID: " << t.id << std::endl;
                t.isAvailable = false; // Mark truck as used
                assigned = true;
                break;
            }
        }
        if (!assigned) {
            std::cout << "[ALERT] No truck available for Shipment #" << s.id << "!" << std::endl;
        }
    }
}