#include "core/Server.hpp"
#include "core/IniParser.hpp"
#include <csignal>
#include <malloc.h>

LightweightMC::Core::Server g_server;

void handleSignal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        g_server.stop();
    }
}

int main()
{
    // Limits malloc to a single memory arena.
    mallopt(M_ARENA_MAX, 1);
    // Forces std::cout to flush immediately after every write.
    std::cout << std::unitbuf;
    std::ios_base::sync_with_stdio(true);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    g_server.start();
    return 0;
}