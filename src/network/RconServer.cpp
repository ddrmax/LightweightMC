#include "network/RconServer.hpp"
#include "core/Logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <algorithm>

namespace LightweightMC
{
    namespace Network
    {

        RconServer::RconServer(int port, const std::string &password)
            : m_port(port), m_password(password) {}

        RconServer::~RconServer()
        {
            stop();
        }

        void *RconServer::threadEntry(void *arg)
        {
            static_cast<RconServer *>(arg)->run();
            return nullptr;
        }

        void RconServer::start()
        {
            if (m_running)
                return;
            m_running = true;

            pthread_attr_t attr;
            pthread_attr_init(&attr);

            // Réduction drastique de la taille de pile du thread : 64 KB au lieu de 8 MB
            size_t stackSize = 64 * 1024; // 64 KB est largement suffisant pour RCON
            pthread_attr_setstacksize(&attr, stackSize);

            if (pthread_create(&m_thread, &attr, &RconServer::threadEntry, this) == 0)
            {
                m_threadStarted = true;
            }
            else
            {
                Core::Logger::error("RconServer: Échec de la création du thread à pile réduite");
                m_running = false;
            }

            pthread_attr_destroy(&attr);
        }

        void RconServer::stop()
        {
            if (!m_running)
                return;
            m_running = false;

            if (m_threadStarted)
            {
                pthread_join(m_thread, nullptr);
                m_threadStarted = false;
            }
        }

        void RconServer::run()
        {
            int sock = ::socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0)
                return;

            int opt = 1;
            ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            // Passer le socket en non-bloquant pour éviter le blocage sur accept()
            int flags = ::fcntl(sock, F_GETFL, 0);
            ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(m_port);

            if (::bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
            {
                ::close(sock);
                return;
            }
            if (::listen(sock, 5) < 0)
            {
                ::close(sock);
                return;
            }

            // Buffers fixes sur la stack pour éviter toute allocation sur le tas (heap)
            char bodyBuffer[4096];
            char outBuffer[4096];

            while (m_running)
            {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);

                timeval tv{0, 100000}; // Timeout de 100 ms
                int ready = ::select(sock + 1, &readfds, nullptr, nullptr, &tv);

                if (ready <= 0 || !FD_ISSET(sock, &readfds))
                    continue;

                int client = ::accept(sock, nullptr, nullptr);
                if (client < 0)
                    continue;

                // Socket client non-bloquant avec timeout sur recv
                timeval timeout{1, 0}; // 1s timeout client
                ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

                bool authed = false;

                while (m_running)
                {
                    uint8_t hdr[4];
                    ssize_t r = ::recv(client, hdr, 4, MSG_WAITALL);
                    if (r <= 0)
                        break;

                    int size = *reinterpret_cast<int *>(hdr);
                    if (size <= 0 || size > 4096)
                        break;

                    r = ::recv(client, bodyBuffer, size, MSG_WAITALL);
                    if (r <= 0)
                        break;

                    int rid = *reinterpret_cast<int *>(bodyBuffer);
                    int type = *reinterpret_cast<int *>(bodyBuffer + 4);

                    std::string_view payload(bodyBuffer + 8, std::max(0, size - 10));

                    int respType = 0;
                    std::string_view respPayload;

                    if (type == 3)
                    { // Authentification
                        if (payload == m_password)
                        {
                            authed = true;
                            respType = 2;
                        }
                        else
                        {
                            respType = 0;
                        }
                    }
                    else if (type == 2)
                    { // Commande
                        if (!authed)
                        {
                            respType = 0;
                        }
                        else
                        {
                            respType = 0;
                            respPayload = "OK";
                        }
                    }

                    // Construction de la réponse sans allocation dynamique
                    int respSize = 4 + 4 + static_cast<int>(respPayload.size()) + 2;

                    *reinterpret_cast<int *>(outBuffer) = respSize;
                    *reinterpret_cast<int *>(outBuffer + 4) = rid;
                    *reinterpret_cast<int *>(outBuffer + 8) = respType;

                    if (!respPayload.empty())
                    {
                        std::memcpy(outBuffer + 12, respPayload.data(), respPayload.size());
                    }

                    outBuffer[12 + respPayload.size()] = 0;
                    outBuffer[13 + respPayload.size()] = 0;

                    ::send(client, outBuffer, 12 + respSize, 0);
                }
                ::close(client);
            }
            ::close(sock);
        }

    }
}