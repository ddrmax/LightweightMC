
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
    // Force glibc to return freed memory to the OS if freed block >= 64 KB
    mallopt(M_TRIM_THRESHOLD, 64 * 1024);
    // Do not keep empty memory arenas around
    mallopt(M_MMAP_THRESHOLD, 64 * 1024);
    // Forces std::cout to flush immediately after every write.
    std::cout << std::unitbuf;
    std::ios_base::sync_with_stdio(true);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    g_server.start();
    return 0;
}