#ifndef NETWORK_H
#define NETWORK_H

#include <map>
#include <string>
#include <utility>
#include "algorithms.h"

// --- THE ROAD MAP ---

// A city on the map, from data/cities.csv
struct CityInfo {
    std::string state;
    double latitude = 0;
    double longitude = 0;
};

// Everything the server knows about cities and roads. It is read once at startup and only read after that,
// so all request threads can share it.
struct RoadMap {
    RoadNetwork network;                                                 // city -> roads leaving it (network_map.csv)
    std::map<std::string, CityInfo> cities;                              // city -> state and position (cities.csv)
    std::map<std::pair<std::string, std::string>, std::string> shapes;  // both cities, A-Z -> the road's path as an
                                                                         // encoded polyline (road_shapes.csv)
    int roadCount() const;
};

// Reads cities.csv, network_map.csv and road_shapes.csv from the folder. Throws if the city or road file
// can't be opened; a line it can't read, or a road to a city missing from cities.csv, is skipped with a warning.
// road_shapes.csv only draws the map, so without it the map shows straight lines.
RoadMap loadRoadMap(const std::string& folder);

#endif
