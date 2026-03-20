
#ifndef DATA_MODELS_H
#define DATA_MODELS_H

#include <string>
#include <vector>

class Shipment {
private:
    int id;
    double weight;
    double volume; // To handle shipment size constraints [cite: 22]
    std::string destination;

public:
    Shipment(int id, double w, double v, std::string dest) 
        : id(id), weight(w), volume(v), destination(dest) {}

    // Getters
    int getId() const { return id; }
    double getWeight() const { return weight; }
    double getVolume() const { return volume; }
    std::string getDestination() const { return destination; }
};

class Truck {
    private:
        int id;
        double weightCapacity;
        double volumeCapacity;
        double costPerKm;
        double currentWeightLoad;
        double currentVolumeLoad;

    public:
        // Constructor
        Truck(int id, double weightCap, double volumeCap, double cost) 
            : id(id), weightCapacity(weightCap), volumeCapacity(volumeCap), 
            costPerKm(cost), currentWeightLoad(0.0), currentVolumeLoad(0.0) {}

        // Validation Logic: Checks if a shipment fits both weight and volume
        bool canFit(double weight, double volume) const {
            return (currentWeightLoad + weight <= weightCapacity) && 
                (currentVolumeLoad + volume <= volumeCapacity);
        }

        // Update Logic: Adds the shipment to the current load
        void addShipment(double weight, double volume) {
            currentWeightLoad += weight;
            currentVolumeLoad += volume;
        }

        // --- GETTERS (Marked 'const' to fix the error in your screenshot) ---
        
        int getId() const { 
            return id; 
        }

        double getWeightCapacity() const { 
            return weightCapacity; 
        }

        double getWeightLoad() const { 
            return currentWeightLoad; 
        }

        double getVolumeCapacity() const { 
            return volumeCapacity; 
        }

        double getVolumeLoad() const { 
            return currentVolumeLoad; 
        }

        double getCostPerKm() const { 
            return costPerKm; 
        }
};

#endif