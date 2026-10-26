#ifndef RCON_SERVER_HPP
#define RCON_SERVER_HPP

#include <string>
#include <atomic>
#include <pthread.h>

namespace LightweightMC
{
    namespace Core
    {
        class Server;
    }
}

namespace LightweightMC
{
    namespace Network
    {

        class RconServer
        {
        public:
            RconServer(int port, const std::string &password);
            ~RconServer();
            void start();
            void stop();

        private:
            void run();
            static void *threadEntry(void *arg);

            int m_port;
            std::string m_password;
            std::atomic<bool> m_running{false};
            pthread_t m_thread{};
            bool m_threadStarted{false};
        };

    }
}

#endif // RCON_SERVER_HPP