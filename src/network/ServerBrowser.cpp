// Master-server / server-list ping handler for MC 1.8 vanilla.

#include "network/ServerBrowser.hpp"
#include "core/utils.hpp"
#include <string>
#include "core/ConfigManager.hpp"
#include "core/Logger.hpp"
#include "core/utils.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <vector>

namespace LightweightMC::Network
{
    namespace ServerBrowser
    {
        namespace
        {
            std::vector<std::uint8_t> buildStatusDatagram(std::uint16_t protocol,
                                                          const std::string &serverName,
                                                          const std::string &motd,
                                                          const std::string &versionString,
                                                          std::uint16_t playerCount,
                                                          std::uint16_t maxPlayers)
            {
                std::vector<std::uint8_t> out;
                out.push_back(0xFE);
                out.push_back(0xFD);
                out.push_back(0xFF);
                out.push_back(static_cast<std::uint8_t>((protocol >> 8) & 0xFF));
                out.push_back(static_cast<std::uint8_t>(protocol & 0xFF));
                out.push_back(static_cast<std::uint8_t>('j'));
                out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x01);
                out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);
                auto appendString = [&out](const std::string &s)
                {
                    out.push_back(static_cast<std::uint8_t>((s.size() >> 8) & 0xFF));
                    out.push_back(static_cast<std::uint8_t>(s.size() & 0xFF));
                    for (char c : s) out.push_back(static_cast<std::uint8_t>(c));
                };
                appendString(serverName);
                out.push_back(0x00);
                appendString(motd);
                appendString(versionString);
                out.push_back(static_cast<std::uint8_t>((playerCount >> 8) & 0xFF));
                out.push_back(static_cast<std::uint8_t>(playerCount & 0xFF));
                out.push_back(static_cast<std::uint8_t>((maxPlayers >> 8) & 0xFF));
                out.push_back(static_cast<std::uint8_t>(maxPlayers & 0xFF));
                out.push_back(0x00);
                return out;
            }
        } // anonymous

        bool handleFromBuffer(const std::uint8_t *bytes, std::size_t n, int clientFd,
                              LightweightMC::Network::NetworkManager &net)
        {
            if (n < 8) return false;
            // Phase 13: accept case-insensitive "GET" + a '/' anywhere in the
            // first bytes — listings ping bare paths ("GET / HTTP/1.0"), and
            // this has to agree with the detector in NetworkEvents.cpp's
            // handleClientData that routes frames here.
            const bool verbMatch = (bytes[0] == 'G' || bytes[0] == 'g') &&
                                   (bytes[1] == 'E' || bytes[1] == 'e') &&
                                   (bytes[2] == 'T' || bytes[2] == 't');
            bool hasSlash = false;
            const std::size_t scanLen = n < 64 ? n : 64;
            for (std::size_t i = 3; i < scanLen; ++i)
                if (bytes[i] == '/') { hasSlash = true; break; }
            if (!(verbMatch && hasSlash))
                return false;

            LightweightMC::Core::Logger::info("Server browser ping received on fd=" + std::to_string(clientFd));

            auto &cfg = ConfigManager::getInstance();
            const std::string serverName = cfg.getString("server", "server-name",
                                                         cfg.getString("server", "motd", "LightweightMC"));
            const std::string motd = cfg.getString("server", "motd", "LightweightMC C++ Engine");
            const std::string version = std::string("LightweightMC ") + LWMC_VER;

            const std::uint16_t playerCount = static_cast<std::uint16_t>(net.countOnline());
            const std::uint16_t maxPlayers = static_cast<std::uint16_t>(cfg.template get<int>("server", "max-players", 100));
            const std::uint16_t protocol = 47;
            // Phase 13: read the configured [auth] server-icon before building datagram
            const std::string iconB64 = cfg.getString("auth", "server-icon", "");

            std::vector<std::uint8_t> datagram = buildStatusDatagram(protocol, serverName, motd, version, playerCount, maxPlayers);
            // Phase 13: append "&icon=<base64>" suffix for master-server icon field.
            if (!iconB64.empty())
            {
                const std::string suffix = "&icon=" + iconB64;
                datagram.insert(datagram.end(), suffix.begin(), suffix.end());
            }
            const ssize_t sent = ::send(clientFd, datagram.data(), datagram.size(), MSG_NOSIGNAL);
            if (sent < 0)
                LightweightMC::Core::Logger::warn("Server browser datagram send failed on fd=" + std::to_string(clientFd));
            return true;
        }
    } // namespace ServerBrowser
} // namespace LightweightMC::Network
