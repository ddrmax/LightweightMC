#pragma once
#include "network/NetworkManager.hpp"
#include "world/ChunkManager.hpp"
#include "storage/Database.hpp"
#include <atomic>

namespace LightweightMC::Core
{

    class Server
    {
    private:
        std::atomic<bool> m_running{false};
        Network::NetworkManager m_network;
        World::ChunkManager m_chunkManager;
        Storage::Database m_database;

        void printBanner();

    public:
        Server();
        ~Server();

        void start(uint16_t port);
        void stop();
    };

} // namespace LightweightMC::Core