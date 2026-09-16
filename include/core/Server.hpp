#pragma once
#include "network/NetworkManager.hpp"
#include "world/ChunkManager.hpp"
#include "storage/Database.hpp"
#include <iostream>
#include <string>
#include <thread>
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
        void initServerCommands();
        void processConsoleInput(const std::string &inputLine);
        void startConsoleThread(const std::atomic<bool> &running);

    public:
        Server();
        ~Server();

        void start();
        void stop();
    };

} // namespace LightweightMC::Core