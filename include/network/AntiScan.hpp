#pragma once

#include "core/ConfigManager.hpp"
#include <string>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>

namespace AntiScan
{

    // Initializes the library and downloads the PebbleHost Hunter list on startup
    void init(const std::string &listUrl = "https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt");

    // Hot-reloads the blocklist (can be triggered via command)
    bool reload();

    // Dynamically blocks an IP address and saves it to local custom_ips.txt file
    bool addCustomIp(const std::string &ip);

    // Checks whether an IP/port is blocked. Returns true if blocked.
    bool isBlocked(const std::string &ip, uint16_t port = 0);

    // Sends a decoy HTTP 500 response and closes the socket
    void handleBlockedClient(int socketFd, const std::string &ip, uint16_t port = 0);

    // Returns the total count of currently loaded blocked IPs in memory
    size_t getBlockedCount();

} // namespace AntiScan