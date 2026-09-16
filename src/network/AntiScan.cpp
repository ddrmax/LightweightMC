#include "network/AntiScan.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <sys/socket.h>
#include <unistd.h>
#include <core/Logger.hpp>

namespace AntiScan
{

    namespace
    {
        std::unordered_set<std::string> g_blockedIps;
        std::shared_mutex g_mutex;
        std::string g_targetUrl = "https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt";
        const std::string g_cacheFilename = "antiscan_ips.txt";
        const std::string g_customFilename = "custom_ips.txt"; // Local custom list file

        const std::string g_http500Response =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Server: nginx/1.18.0\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 177\r\n"
            "Connection: close\r\n\r\n"
            "<html>\r\n"
            "<head><title>500 Internal Server Error</title></head>\r\n"
            "<body>\r\n"
            "<center><h1>500 Internal Server Error</h1></center>\r\n"
            "<hr><center>nginx/1.18.0</center>\r\n"
            "</body>\r\n"
            "</html>\r\n";

        void loadFileToSet(const std::string &filename, std::unordered_set<std::string> &set)
        {
            std::ifstream file(filename);
            if (!file.is_open())
                return;

            std::string line;
            while (std::getline(file, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
                {
                    line.pop_back();
                }
                if (!line.empty() && line[0] != '#')
                {
                    set.insert(line);
                }
            }
        }
    }

    void init(const std::string &listUrl)
    {
        g_targetUrl = listUrl;
        LightweightMC::Core::Logger::info("[AntiScan] Initializing AntiScan module...");
        reload();
    }

    bool reload()
    {
        LightweightMC::Core::Logger::info("[AntiScan] Downloading latest blocklist...");
        std::string command = "curl -s -f --connect-timeout 5 " + g_targetUrl + " -o " + g_cacheFilename;
        int res = std::system(command.c_str());

        if (res != 0)
        {
            LightweightMC::Core::Logger::error("[AntiScan] Warning: Failed to fetch online list. Using local files.");
        }

        std::unordered_set<std::string> tempSet;

        // 1. Load PebbleHost Hunter list
        loadFileToSet(g_cacheFilename, tempSet);

        // 2. Load custom local IP list
        loadFileToSet(g_customFilename, tempSet);

        {
            std::unique_lock<std::shared_mutex> lock{g_mutex};
            g_blockedIps = std::move(tempSet);
        }

        LightweightMC::Core::Logger::info("[AntiScan] Blocklist loaded. Total blocked IPs: " + std::to_string(getBlockedCount()));
        return true;
    }

    bool addCustomIp(const std::string &ip)
    {
        if (ip.empty())
            return false;

        {
            // Add to memory
            std::unique_lock<std::shared_mutex> lock{g_mutex};
            g_blockedIps.insert(ip);
        }

        // Append to custom_ips.txt file
        std::ofstream file(g_customFilename, std::ios::app);
        if (file.is_open())
        {
            file << ip << "\n";
            file.close();
            LightweightMC::Core::Logger::info("[AntiScan] Added custom IP to " + g_customFilename + ": " + ip);
            return true;
        }

        LightweightMC::Core::Logger::error("[AntiScan] Error: Could not write to " + g_customFilename);
        return false;
    }

    bool isBlocked(const std::string &ip, uint16_t port)
    {
        (void)port;
        std::shared_lock<std::shared_mutex> lock{g_mutex};
        return g_blockedIps.find(ip) != g_blockedIps.end();
    }

    void handleBlockedClient(int socketFd, const std::string &ip, uint16_t port)
    {
        LightweightMC::Core::Logger::info("[AntiScan] Blocked scanner/bot attempt from " + ip + ":" + std::to_string(port) + " (Fake HTTP 500 sent)");
        send(socketFd, g_http500Response.c_str(), g_http500Response.size(), MSG_NOSIGNAL);
        close(socketFd);
    }

    size_t getBlockedCount()
    {
        std::shared_lock<std::shared_mutex> lock{g_mutex};
        return g_blockedIps.size();
    }

} // namespace AntiScan