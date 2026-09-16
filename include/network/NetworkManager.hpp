#pragma once
#include <cstdint>
#include <sys/epoll.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "storage/WorldStorage.hpp"
#include "storage/Database.hpp"
#include "player/PlayerSession.hpp"
#include "managers/WorldManager.hpp"

namespace LightweightMC::Network
{
    class NetworkManager
    {
    private:
        int m_serverFd{-1};
        int m_epollFd{-1};
        bool antiscan{true};
        epoll_event m_events[64];
        std::unordered_map<int, LightweightMC::Player::PlayerSession> m_clients;
        Storage::WorldStorage m_worldStorage;
        Storage::Database m_database{"server_data.db"};
        World::WorldManager m_worldManager{m_worldStorage};

        void setNonBlocking(int fd);
        void handleClientData(int clientFd);
        void handleDisconnect(int fd);
        void sendPacket(int fd, int32_t packetId, const std::vector<uint8_t> &payload);
        void flushSendBuffer(LightweightMC::Player::PlayerSession &client);
        void broadcastPacket(int32_t packetId, const std::vector<uint8_t> &payload, int ignoreFd = -1);

    public:
        NetworkManager() = default;
        ~NetworkManager() = default;

        bool start(uint16_t port);
        void pollEvents(int timeoutMs);
        void stop();
    };

} // namespace LightweightMC::Network