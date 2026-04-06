#ifndef DATA_MODELS_H
#define DATA_MODELS_H

#include <string>

struct Edge {
    std::string to;
    int weight;
    std::string type;
};

struct Shipment {
    int id;
    double weight;
    double volume; 
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
        : id(i), capacity(c), volume(v), fuelRate(f), isAvailable(true) {}
};

#endif