#ifndef DATA_MODELS_H
#define DATA_MODELS_H

#include <string>

// One road between two cities, as read from network_map.csv
struct Edge {
    std::string to;
    int weight;        // distance in km
    std::string type;  // road name, e.g. "NH-48 Direct"
};

#endif
