#ifndef SERVER_H
#define SERVER_H

#include <string>
#include "algorithms.h"
#include "httplib.h"

// --- WEB API ---
// Adds every /api route plus the page guards that send visitors without the right login to login.html.
// The road network is only read, so all request threads can share it.
void registerRoutes(httplib::Server& server, const RoadNetwork& network, const std::string& databasePath);

#endif
