#include "core/Server.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::Core {

Server::Server() : m_database("world_data.db") {}
Server::~Server() { stop(); }

void Server::printBanner() {
    std::cout << "\033[1;32m"
            << R"(M""MMMMMMMM oo          dP         dP                       oo          dP         dP   M"""""`'"""`YM MM'""""'YMM)" << "\n" 
            << R"(M  MMMMMMMM             88         88                                   88         88   M  mm.  mm.  M M' .mmm. `M)" << "\n" 
            << R"(M  MMMMMMMM dP .d8888b. 88d888b. d8888P dP  dP  dP .d8888b. dP .d8888b. 88d888b. d8888P M  MMM  MMM  M M  MMMMMooM)" << "\n" 
            << R"(M  MMMMMMMM 88 88'  `88 88'  `88   88   88  88  88 88ooood8 88 88'  `88 88'  `88   88   M  MMM  MMM  M M  MMMMMMMM)" << "\n"   
            << R"(M  MMMMMMMM 88 88.  .88 88    88   88   88.88b.88' 88.  ... 88 88.  .88 88    88   88   M  MMM  MMM  M M. `MMM' .M)" << "\n"     
            << R"(M         M dP `8888P88 dP    dP   dP   8888P Y8P  `88888P' dP `8888P88 dP    dP   dP   M  MMM  MMM  M MM.     .dM)" << "\n"   
            << R"(MMMMMMMMMMM         .88                                             .88                 MMMMMMMMMMMMMM MMMMMMMMMMM)" << "\n"     
            << R"(                d8888P                                          d8888P                                            )" << "\n"                            
            << "\033[0m"
              << "\033[1;36m       -- Native C++ Engine | Event-Driven & Zero-Tick --\033[0m\n\n";
}
void Server::start(uint16_t port) {
    printBanner();
    m_running = true;
    Logger::info("SQLite Database Init...");
    m_database.init();

    Logger::info("Starting Network Manager (epoll)...");
    m_network.start(port);

    Logger::info("Server is ready and listening");
    
    // Boucle maître
    while (m_running) {
        m_network.pollEvents(50); // 50ms = 1 tick (20 TPS)
        m_chunkManager.tickActiveChunks();
    }
}

void Server::stop() {
    if (m_running) {
        m_running = false;
        m_network.stop();
        Logger::info("Server shutdown with success.");
    }
}

} // namespace LightweightMC::Core