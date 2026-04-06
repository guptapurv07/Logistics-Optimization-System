#include "../include/algorithms.h"
#include <vector>
#include <algorithm>
#include <iostream>

// GREEDY BEST-FIT ALLOCATION
std::vector<std::pair<int, int>> performGreedyAllocation(std::vector<Shipment>& shipments, std::vector<Truck>& trucks) {
    std::vector<std::pair<int, int>> assignments;

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
                assignments.push_back({s.id, t.id});
                assigned = true;
                break;
            }
        }
        if (!assigned) {
            std::cout << "[ALERT] No truck available for Shipment #" << s.id << "!" << std::endl;
            assignments.push_back({s.id, -1});
        }
    }
    
    return assignments;
}