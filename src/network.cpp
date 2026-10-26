#include "../include/network.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

// The map files never quote their values, so a line is just split on commas
std::vector<std::string> splitLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(field);
    return fields;
}

// Calls handle(fields, lineNumber) for every line after the header
template <typename Handler>
void readCsv(const std::string& path, Handler handle) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path + " (start the server from the project folder)");
    std::string line;
    std::getline(file, line);
    for (int number = 2; std::getline(file, line); ++number) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // a file saved on Windows
        if (!line.empty()) handle(splitLine(line), number);
    }
}

void skipLine(const std::string& path, int line, const std::string& problem) {
    std::cerr << "[WARN] " << path << " line " << line << ": " << problem << "; line skipped" << std::endl;
}

// Whole-text number parsing: "250km" or "" is refused instead of being half read
bool parseInt(const std::string& text, int& value) {
    try {
        std::size_t used = 0;
        value = std::stoi(text, &used);
        return used == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

bool parseDouble(const std::string& text, double& value) {
    try {
        std::size_t used = 0;
        value = std::stod(text, &used);
        return used == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

int RoadMap::roadCount() const {
    std::size_t ends = 0;
    for (const auto& [city, roads] : network) ends += roads.size();
    return static_cast<int>(ends / 2);  // every road is stored at both of its ends
}

RoadMap loadRoadMap(const std::string& folder) {
    RoadMap map;
    const std::string citiesPath = folder + "/cities.csv";
    const std::string roadsPath = folder + "/network_map.csv";
    const std::string shapesPath = folder + "/road_shapes.csv";

    readCsv(citiesPath, [&](const std::vector<std::string>& fields, int line) {
        CityInfo city;
        if (fields.size() != 4 || fields[0].empty() || !parseDouble(fields[2], city.latitude) || !parseDouble(fields[3], city.longitude)) {
            return skipLine(citiesPath, line, "expected city,state,latitude,longitude");
        }
        city.state = fields[1];
        map.cities[fields[0]] = city;
    });

    readCsv(roadsPath, [&](const std::vector<std::string>& fields, int line) {
        int km = 0;
        if (fields.size() != 4 || fields[0].empty() || fields[1].empty() || !parseInt(fields[2], km) || km <= 0) {
            return skipLine(roadsPath, line, "expected source,destination,distance_km,route_type with a whole number of km");
        }
        if (!map.cities.count(fields[0]) || !map.cities.count(fields[1])) {
            return skipLine(roadsPath, line, "a city that is not in cities.csv");
        }
        map.network[fields[0]].push_back({fields[1], km, fields[3]});
        map.network[fields[1]].push_back({fields[0], km, fields[3]});  // roads go both ways
    });

    if (std::ifstream(shapesPath)) {
        readCsv(shapesPath, [&](const std::vector<std::string>& fields, int line) {
            if (fields.size() != 3 || fields[2].empty()) return skipLine(shapesPath, line, "expected source,destination,polyline");
            map.shapes[std::minmax(fields[0], fields[1])] = fields[2];
        });
    } else {
        std::cerr << "[WARN] " << shapesPath << " not found; the map draws roads as straight lines" << std::endl;
    }

    for (const auto& [name, city] : map.cities) {
        if (!map.network.count(name)) std::cerr << "[WARN] " << name << " is in cities.csv but has no roads" << std::endl;
    }
    return map;
}
