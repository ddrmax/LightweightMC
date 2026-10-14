// Epoll lifecycle: owns the loop that consumes EPOLLIN/EPOLLOUT events, reads
// bytes into each session's rxBuffer, dispatches them to the appropriate
// handshake / status / login / play handler, flushes pending frames, and
// tears the client down on RDHUP/HUP/ERR.
//
// See src/network/NetworkManager.cpp for the class-level invariant notes.

#include "network/NetworkManager.hpp"
#include "core/utils.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "network/AntiScan.hpp"
#include "managers/ScoreboardManager.hpp"
#include "managers/PlayerManager.hpp"
#include "network/handlers/HandshakeHandler.hpp"
#include "network/handlers/StatusHandler.hpp"
#include "network/handlers/LoginHandler.hpp"
#include "network/handlers/play/PlayHandler.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <vector>
#include <sstream>
#include <iomanip>

namespace LightweightMC::Network
{

    // Small helpers for the wire-layer debug lines -- render a packet id in
    // hex and preview the first N bytes of a buffer so we can eyeball the
    // varint framing of every frame that crosses the socket.
    static inline std::string hexId(int32_t v)
    {
        std::ostringstream o;
        o << "0x" << std::hex << v;
        return o.str();
    }

    static inline std::string hexPreview(const uint8_t *bytes, size_t count)
    {
        std::ostringstream oss;
        const size_t n = count < 32 ? count : 32;
        for (size_t i = 0; i < n; ++i)
        {
            oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(bytes[i]);
            if (i + 1 < n)
                oss << ' ';
        }
        if (count > n)
            oss << " …(+" << (count - n) << "B)";
        return oss.str();
    }

    // Turn a freshly accepted socket into a non-blocking one so send()/recv()
    // from within the epoll thread return EAGAIN instead of stalling the
    // whole event loop. Called right after accept() in pollEvents.
    void NetworkManager::setNonBlocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
            return;
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    // Consume one read burst from clientFd and dispatch every complete frame
    // it contains. Frames are varint-length-prefixed (1.8 style) and the
    // caller in pollEvents has already verified the socket is in m_clients.
    void NetworkManager::handleClientData(int clientFd)
    {
        auto it = m_clients.find(clientFd);
        if (it == m_clients.end())
        {
            Core::Logger::debug("[NET RECV] abort fd=" + std::to_string(clientFd) +
                                " — client not in m_clients (skipped read)");
            return;
        }

        auto &session = it->second;

        // Hard cap on rxBuffer so a flooding client cannot grow our heap
        // unboundedly. A legitimate MC packet is <= 2 MiB (21-bit wire
        // length limit); anything beyond that is either spam or a
        // malformed stream, and both warrant disconnecting the client
        // rather than stalling the server.
        static constexpr size_t kMaxRxBufferBytes = 4u * 1024u * 1024u;

        uint8_t buf[8192];
        const ssize_t bytes = ::read(clientFd, buf, sizeof(buf));

        Core::Logger::debug("[NET RECV] read fd=" + std::to_string(clientFd) +
                            " bytes=" + std::to_string(bytes) +
                            " rxBefore=" + std::to_string(session.rxBuffer.size()));

        if (bytes <= 0)
        {
            // Peer half-closed or errored; treat as disconnect. The
            // destroy-entities broadcast (0x13) is 1.8-only; other eras
            // do not have that packet so we only emit it for legacy
            // sessions. Callers that care about other eras plug in via
            // the era translator in NetworkFraming.cpp.
            if (session.state == ClientState::PLAY)
            {
                std::vector<uint8_t> destroy;
                Packet::writeVarInt(destroy, 1);
                Packet::writeVarInt(destroy, clientFd);
                broadcastPacket(0x13, destroy, clientFd);
            }
            Core::Logger::info("Client " + std::to_string(clientFd) + " read()=0, treating as disconnect.");
            handleDisconnect(clientFd);
            return;
        }

        session.rxBuffer.insert(session.rxBuffer.end(), buf, buf + bytes);
        if (session.rxBuffer.size() > kMaxRxBufferBytes)
        {
            Core::Logger::warn("Client " + std::to_string(clientFd) +
                                " rxBuffer exceeded cap (" + std::to_string(session.rxBuffer.size()) +
                                " bytes), dropping client.");
            handleDisconnect(clientFd);
            return;
        }

        while (!session.rxBuffer.empty())
        {
            // Bound the frame-length varint against the buffer so a client
            // sending 0xFF x 10 cannot ask us to hop past the heap.
            size_t headerOff = 0;
            const int32_t packetLen = Packet::readVarIntBounded(
                session.rxBuffer.data(), headerOff, session.rxBuffer.size(), 1);
            if (packetLen <= 0)
                break; // Incomplete header; wait for the next read().

            const size_t totalLen = headerOff + static_cast<size_t>(packetLen);
            if (session.rxBuffer.size() < totalLen)
                break; // Body not fully buffered yet; wait for next read().

            const uint8_t *body = session.rxBuffer.data() + headerOff;
            size_t bodyOff = 0;
            const int32_t packetId = Packet::readVarIntBounded(
                body, bodyOff, static_cast<size_t>(packetLen), 1);
            if (packetId < 0)
            {
                // Skip this frame (a corrupt varint should not crash the
                // whole server); drop just this frame and continue.
                Core::Logger::debug("[NET RECV] corrupt-varint frame fd=" + std::to_string(clientFd) +
                                    " droppedLen=" + std::to_string(totalLen));
                session.rxBuffer.erase(session.rxBuffer.begin(),
                                       session.rxBuffer.begin() + totalLen);
                continue;
            }

            const size_t packetEnd = static_cast<size_t>(packetLen);
            size_t offset = bodyOff; // Handlers continue reading past the id varint.

            Core::Logger::debug("[NET RECV] frame fd=" + std::to_string(clientFd) +
                                " wireLen=" + std::to_string(packetLen) +
                                " id=" + hexId(packetId) +
                                " state=" + std::to_string(static_cast<int>(session.state)) +
                                " hex=" + hexPreview(body, packetEnd));

            switch (session.state)
            {
            case ClientState::HANDSHAKE:
                Core::Logger::debug("[NET DISPATCH] HANDSHAKE fd=" + std::to_string(clientFd) +
                                    " id=" + hexId(packetId));
                HandshakeHandler::handle(clientFd, session, body, offset, packetEnd, packetId);
                break;
            case ClientState::STATUS:
                Core::Logger::debug("[NET DISPATCH] STATUS fd=" + std::to_string(clientFd) +
                                    " id=" + hexId(packetId));
                StatusHandler::handle(clientFd, session, body, offset, packetEnd, packetId,
                                      m_clients, *this);
                break;
            case ClientState::LOGIN:
                Core::Logger::debug("[NET DISPATCH] LOGIN fd=" + std::to_string(clientFd) +
                                    " id=" + hexId(packetId));
                LoginHandler::handle(clientFd, session, body, offset, packetEnd, packetId,
                                     m_clients, m_worldManager, m_database, *this);
                break;
            case ClientState::PLAY:
                Core::Logger::debug("[NET DISPATCH] PLAY fd=" + std::to_string(clientFd) +
                                    " id=" + hexId(packetId));
                // PlayHandler always operates on the WorldStorage tied to the
                // session's current world (0=Overworld / 1=Nether / 2=End / 100+
                // custom). Multiworld fix: replacing the former single
                // m_worldStorage member with a session-aware accessor.
                PlayHandler::handle(clientFd, session, body, offset, packetEnd, packetId,
                                    m_clients, m_worldManager, getWorldStorage(session.worldId), m_database,
                                    *this);
                break;
            }

            session.rxBuffer.erase(session.rxBuffer.begin(),
                                   session.rxBuffer.begin() + totalLen);
        }
    }

    // Epoll tick: consume every event surfaced in the last epoll_wait and
    // dispatch it to accept / read / flush / disconnect handling.
    void NetworkManager::pollEvents(int timeoutMs)
    {
        if (m_epollFd == -1)
            return;

        // Keep the on-stack event array bounded. m_events is a fixed 64-slot
        // array in the class header (see include/network/NetworkManager.hpp),
        // and epoll_wait's third argument caps the batch at 64. If we ever
        // need more we should switch to a heap vector, but capping prevents
        // the array-overflow bug that previously occurred when epoll_wait
        // was handed a stale 3rd arg.
        constexpr int kMaxEvents = 64;
        const int nfds = epoll_wait(m_epollFd, m_events, kMaxEvents, timeoutMs);
        if (nfds < 0)
        {
            if (errno != EINTR)
                Core::Logger::error("epoll_wait failed: " + std::string(std::strerror(errno)));
            return;
        }

        auto packetSender = [this](int f, int32_t p, const std::vector<uint8_t> &b)
        { sendPacket(f, p, b); };

        for (int i = 0; i < nfds; ++i)
        {
            const int clientFd = m_events[i].data.fd;
            const uint32_t events = m_events[i].events;

            if (clientFd == m_serverFd)
            {
                sockaddr_in clientAddr{};
                socklen_t clientLen = sizeof(clientAddr);
                const int newFd = ::accept(m_serverFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientLen);
                if (newFd < 0)
                    continue;

                char ipStr[INET_ADDRSTRLEN];
                ::inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
                const std::string clientIp(ipStr);
                const uint16_t clientPort = ::ntohs(clientAddr.sin_port);

                if (antiscan && AntiScan::isBlocked(clientIp, clientPort))
                {
                    AntiScan::handleBlockedClient(newFd, clientIp, clientPort);
                    continue;
                }

                setNonBlocking(newFd);

                int flag = 1;
                ::setsockopt(newFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                int pmTu = IP_PMTUDISC_DONT;
                ::setsockopt(newFd, IPPROTO_IP, IP_MTU_DISCOVER, &pmTu, sizeof(pmTu));

                epoll_event ev{};
                ev.events = EPOLLIN | EPOLLRDHUP;
                ev.data.fd = newFd;
                ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, newFd, &ev);

                LightweightMC::Player::PlayerSession ses{};
                ses.fd = newFd;
                ses.state = ClientState::HANDSHAKE;
                m_clients[newFd] = ses;
                continue;
            }

            auto it = m_clients.find(clientFd);
            if (it == m_clients.end())
                continue;

            // Half-close (RDHUP), full close (HUP) or error (ERR) all funnel
            // into the same disconnect path.
            if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                handleDisconnect(clientFd);
                continue;
            }

            if (events & EPOLLOUT)
                flushSendBuffer(it->second);

            // flushSendBuffer may itself disconnect the client (the send()==0
            // path we added below), which erases m_clients[clientFd]. Bail
            // out before touching the erased iterator.
            if (m_clients.find(clientFd) == m_clients.end())
                continue;

            if (events & EPOLLIN)
                handleClientData(clientFd);
        }

        // Scoreboard / tab-list cadence: refresh at most once per second so
        // clients that have not moved in the last second do not pay the
        // tablist cost every pollEvents tick.
        static auto lastUpdate = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count() >= 1000)
        {
            // Delegate to ScoreboardManager::tick so we snapshot (fd -> PLAY)
            // into a local vector before dispatching. The per-client sendScoreboard
            // / sendTablistHeaderFooter calls go through NetworkManager::sendPacket
            // which can cascade flushSendBuffer -> handleDisconnect -> m_clients.erase(fd)
            // when the socket is already dead; erasing mid-iteration invalidates
            // the range-for iterator and any reference we captured, so the old
            // inline loop over m_clients was UB once a client disconnected during
            // the refresh window. tick() copies the fd list first and re-checks
            // liveness each pass, which fixes the use-after-free.
            Managers::ScoreboardManager::getInstance().tick(m_clients, packetSender);
            lastUpdate = now;
        }
    }

    // Tear the client down after a socket error, RDHUP, or an explicit
    // hangup during flushSendBuffer. Persists player state so a re-login
    // resumes with the same inventory / position.
    void NetworkManager::handleDisconnect(int fd)
    {
        std::string username = "Unauthenticated Client";
        const std::string ipPort = Core::getIpAndPortFromFd(fd);

        auto it = m_clients.find(fd);
        if (it == m_clients.end())
            return;

        if (!it->second.username.empty())
        {
            username = it->second.username;
            Player::PlayerManager::getInstance().savePlayerState(it->second, m_database);
        }

        m_clients.erase(it);

        Core::Logger::debug("[NET DISCONNECT] before-scoreboard-cache erase fd=" + std::to_string(fd) +
                            " (ScoreboardManager::removeClientCache)");
        Managers::ScoreboardManager::getInstance().removeClientCache(fd);
        ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);

        Core::Logger::info("Client " + std::to_string(fd) + " (" + username +
                            ") disconnected. [IP: " + ipPort + "]");
    }

    // Push the pending bytes in session.sendBuffer out of the socket. Loops
    // until drained or until send() reports EAGAIN (in which case we set
    // EPOLLOUT back on the client so the next pollEvents flushes the
    // remainder).
    void NetworkManager::flushSendBuffer(LightweightMC::Player::PlayerSession &session)
    {
        while (!session.sendBuffer.empty())
        {
            const ssize_t sent = ::send(session.fd,
                                        session.sendBuffer.data(),
                                        session.sendBuffer.size(),
                                        MSG_NOSIGNAL);
            Core::Logger::debug("[NET FLUSH] send fd=" + std::to_string(session.fd) +
                                " pending=" + std::to_string(session.sendBuffer.size()) +
                                "B -> sent=" + std::to_string(sent));
            if (sent > 0)
            {
                session.sendBuffer.erase(session.sendBuffer.begin(),
                                         session.sendBuffer.begin() + sent);
                continue;
            }
            if (sent == 0)
            {
                // Linux send() returning 0 signals a hangup; treat as a
                // disconnect rather than looping forever.
                Core::Logger::warn("send() returned 0 for fd=" + std::to_string(session.fd) +
                                    ", treating as disconnect.");
                handleDisconnect(session.fd);
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                epoll_event ev{};
                ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
                ev.data.fd = session.fd;
                ::epoll_ctl(m_epollFd, EPOLL_CTL_MOD, session.fd, &ev);
                Core::Logger::debug("[NET FLUSH] EAGAIN fd=" + std::to_string(session.fd) +
                                    " remainingBytes=" + std::to_string(session.sendBuffer.size()));
                return;
            }
            Core::Logger::warn("send() failed for fd=" + std::to_string(session.fd) +
                                ": " + std::string(std::strerror(errno)) + ", disconnecting.");
            handleDisconnect(session.fd);
            return;
        }

        // Buffer drained: turn EPOLLOUT back off so pollEvents stops
        // spinning on this client.
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = session.fd;
        ::epoll_ctl(m_epollFd, EPOLL_CTL_MOD, session.fd, &ev);
    }

} // namespace LightweightMC::Network
