#ifndef SERVER_H
#define SERVER_H

#include <string>
#include "httplib.h"
#include "network.h"
#include "settings.h"

// --- WEB API ---
// Adds every /api route plus the page guards that send visitors without the right login to login.html.
// The road map and the settings are only read, so all request threads can share them.
void registerRoutes(httplib::Server& server, const RoadMap& roadMap, const std::string& databasePath, const Settings& settings);

#endif
