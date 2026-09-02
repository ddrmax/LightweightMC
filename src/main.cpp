#include "core/Server.hpp"
#include <csignal>

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
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    g_server.start(25565);
    return 0;
}