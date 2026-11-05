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
#include "world/EntityManager.hpp"
#include "network/handlers/play/InventoryHandler.hpp"
#include "network/handlers/HandshakeHandler.hpp"
#include "network/handlers/StatusHandler.hpp"
#include "network/handlers/LoginHandler.hpp"
#include "network/handlers/play/PlayHandler.hpp"
#include "network/ServerBrowser.hpp"
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
#include <set>   // announced-drop eid tracking (A1 ground-item spawn announcement)
#include <sstream>
#include <iomanip>
#include "network/BanList.hpp"
#include <malloc.h>

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

        // Server-browser ping detection. Master-server listings (addserver.org,
        // Bluetymaps, community relays) send a small HTTP GET to the game port
        // and expect a raw 0xFE 0xFD status datagram back -- not a Minecraft
        // varint-framed packet. The bytes reach us before any handshake has
        // been parsed, so we intercept here rather than treating them as a
        // corrupt frame that disconnects the client silently. Only check
        // while the session is still HANDSHAKE -- real Minecraft clients
        // will not have "GET " at byte 0. Phase 13 extension: listings also
        // ping bare paths (they send "GET / HTTP/1.0") and some probe with
        // lowercase verbs, so match "GET" + a '/' in the request line here
        // instead of requiring the old "GET <space>" byte pair.
        if ((session.state == ClientState::HANDSHAKE || session.state == ClientState::PLAY) &&
            session.rxBuffer.size() >= 8)
        {
            const uint8_t *p = session.rxBuffer.data();
            const bool verbMatch = (p[0] == 'G' || p[0] == 'g') &&
                                   (p[1] == 'E' || p[1] == 'e') &&
                                   (p[2] == 'T' || p[2] == 't');
            bool hasSlash = false;
            const size_t scanLen = session.rxBuffer.size() < 64 ? session.rxBuffer.size() : 64;
            for (size_t i = 3; i < scanLen; ++i)
                if (session.rxBuffer[i] == '/')
                {
                    hasSlash = true;
                    break;
                }
            if (verbMatch && hasSlash)
            {
                Core::Logger::info("[NET BROWSER] fd=" + std::to_string(clientFd) +
                                   " GET-prefix request, emitting server-info datagram");
                ServerBrowser::handleFromBuffer(session.rxBuffer.data(),
                                                session.rxBuffer.size(),
                                                clientFd, *this);
                handleDisconnect(clientFd);
                return;
            }
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
                // whole server); drop just this frame and continue. WARN so
                // malformed/framing-divergent clients are visible in logs.
                Core::Logger::warn("[NET RECV] corrupt-varint frame fd=" + std::to_string(clientFd) +
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
            case ClientState::DISCONNECTED:
                // Tombstone: handleDisconnect parked this session (epoll registration
                // already dropped, flushPendingDisconnects closes + erases it after the
                // pass). Frames that arrived before teardown must not reach handlers —
                // they are discarded so nothing else references a dead PlayerSession.
                Core::Logger::debug("[NET DISPATCH] DISCONNECTED fd=" + std::to_string(clientFd) +
                                    " id=" + hexId(packetId) + " dropped (tombstone)");
                break;
            default:
                // Defensive: an unrecognised session state must not pass frames to
                // handlers. WARN so a state-machine gap is visible in the log.
                Core::Logger::warn("[NET DROP] fd=" + std::to_string(clientFd) +
                                   " id=" + hexId(packetId) +
                                   " unmatched session state=" + std::to_string(static_cast<int>(session.state)) +
                                   " — frame dropped");
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
                // Phase 10 (ban/kick/whitelist): remember the peer IP so LoginHandler
                // and NetworkManager's ban lookups can match it against
                // banned-ips.json-style entries without probing every session.
                ses.peerIp = clientIp;
                m_clients[newFd] = ses;
                continue;
            }

            auto it = m_clients.find(clientFd);
            if (it == m_clients.end())
                continue;

            // Tombstone pass: handleDisconnect parked this session as DISCONNECTED
            // (epoll registration already dropped) and flushPendingDisconnects has
            // not erased it yet — skip re-dispatch so queued handlers do not touch a
            // dead client. The session erase itself runs at the end of pollEvents
            // (or in ~NetworkManager), closing the socket after any send that referenced
            // this pass' PlayerSession& can still be serviced.
            if (it->second.state == ClientState::DISCONNECTED)
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

        // Drain every disconnect tombstone queued by this pass: the epoll event loop
        // has already handed off each dead client's session to handlers/flush paths
        // that may still reference it in this tick (kickSessionByName queues Disconnect 0x00
        // then handleDisconnect marks a tombstone and ScoreboardManager::tick below iterates
        // over sessions again). Erase them now before any further consumers see
        // ClientState::DISCONNECTED handles.
        flushPendingDisconnects(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());

        // ---- Ground-item spawn announcement (S2C Spawn Object 0x0E) --------------
        // A1 fidelity fix: DROPPED_ITEM entities exist in the simulation and the
        // pickup sweep below collects them, but vanilla clients were never told the
        // item exists on the ground, so nothing renders it until pickup. Emit one
        // Spawn Object (legacy wire 0x0E) per entity to the nearest PLAY client once
        // (announce-then-forget), matching vanilla where a dropped item becomes visible
        // when first seen. Reuses writeSpawnEntityItem's probe-blessed body (varint entityId
        // + type byte 2=item + three fixed-point shorts scaled x*32, no trailing mob tail).
        // NOTE: the earlier SPAWN_ENTITY (0x19) variant crashed the JVM client ("Packet 0/25
        // ... found 22 bytes extra whilst reading packet 25") because that wire id decodes as
        // Spawn Entity Living and our mob-shaped tail overflowed it; 0x0E is what the client
        // actually expects for an item on the ground. Announced eids are remembered in a static
        // set so a still-on-the-ground item is not re-spammed every sweep; pruned against listIds.
        static std::set<int32_t> announcedDrops;
        {
            auto &emAnn = LightweightMC::World::EntityManager::getInstance();
            // Prune announcements for entities that no longer exist so their ids can
            // be re-announced if reused, and keep the set bounded.
            {
                const std::vector<int32_t> liveIds = emAnn.listIds();
                std::set<int32_t> liveSet(liveIds.begin(), liveIds.end());
                for (auto it = announcedDrops.begin(); it != announcedDrops.end();)
                {
                    if (!liveSet.count(*it))
                        it = announcedDrops.erase(it);
                    else
                        ++it;
                }
            }
            constexpr double kDropRenderD2 = 24.0 * 24.0; // ~render-distance announce radius
            for (const int32_t eid : emAnn.listIds())
            {
                if (announcedDrops.count(eid))
                    continue;
                const LightweightMC::World::EntityManager::EntityRecord *rec = emAnn.find(eid);
                if (!rec || rec->kind != LightweightMC::World::EntityManager::EntityKind::DROPPED_ITEM)
                    continue;
                // Announce to the nearest PLAY client within render distance so the
                // item renders on that client's ground. One announce per entity is
                // enough (items are world-global; each client learns it when close).
                int bestFd = -1;
                double bestD2 = kDropRenderD2;
                for (const auto &[fd, ses] : m_clients)
                {
                    if (ses.state != Player::ClientState::PLAY)
                        continue;
                    const double dx = ses.x - rec->x;
                    const double dy = ses.y - rec->y;
                    const double dz = ses.z - rec->z;
                    const double d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < bestD2)
                    {
                        bestD2 = d2;
                        bestFd = fd;
                    }
                }
                if (bestFd < 0)
                    continue;
                std::vector<uint8_t> spawnObj;
                LightweightMC::Network::Packet::writeSpawnEntityItem(spawnObj, eid, rec->x, rec->y, rec->z);
                packetSender(bestFd, 0x0E, spawnObj);
                announcedDrops.insert(eid);
                LightweightMC::Core::Logger::warn(std::string("[WIRE SpawnObject 0x0E] ")
                    + "player=" + m_clients[bestFd].username
                    + " eid=" + std::to_string(eid)
                    + " itemId=" + std::to_string(rec->itemId)
                    + " pos=(" + std::to_string(rec->x) + "," + std::to_string(rec->y) + "," + std::to_string(rec->z) + ")");
            }
        }

        // Ground-item pickup sweep (0x0D Collect Item + 0x13 Destroy Entities).
        // Runs ~twice a second. Any DROPPED_ITEM entity that has passed its
        // vanilla 2s pickup delay and sits within ~1.3 blocks of a PLAY client's
        // feet is offered to that client: merge into the authoritative inventory,
        // then only destroy the ground entity if at least one unit landed (a full
        // inventory keeps the item on the ground). A full Window Items (0x30) sync
        // makes the collected stack visible even if slot mapping drifts.
        static auto lastPickup = std::chrono::steady_clock::now();
        const auto pickupNow = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(pickupNow - lastPickup).count() >= 500)
        {
            lastPickup = pickupNow;
            auto &em = LightweightMC::World::EntityManager::getInstance();
            const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(pickupNow.time_since_epoch()).count();
            for (const int32_t eid : em.listIds())
            {
                const LightweightMC::World::EntityManager::EntityRecord *rec = em.find(eid);
                if (!rec || rec->kind != LightweightMC::World::EntityManager::EntityKind::DROPPED_ITEM)
                    continue;
                if (nowMs - rec->spawnMs < 2000)
                    continue;

                int bestFd = -1;
                double bestD2 = 2.25; // vanilla pickup radius ~1.5 blocks (squared)
                for (const auto &[fd, ses] : m_clients)
                {
                    if (ses.state != Player::ClientState::PLAY)
                        continue;
                    const double dx = ses.x - rec->x;
                    const double dy = (ses.y + 0.5) - rec->y; // feet-to-centre offset
                    const double dz = ses.z - rec->z;
                    const double d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < bestD2)
                    {
                        bestD2 = d2;
                        bestFd = fd;
                    }
                }
                if (bestFd < 0)
                    continue;

                auto sit = m_clients.find(bestFd);
                if (sit == m_clients.end())
                    continue;
                const int32_t itemId = rec->itemId;
                const uint8_t cnt = static_cast<uint8_t>(rec->stackCount > 0 ? rec->stackCount : 1);
                const int remaining = ::InventoryHandler::mergeItemIntoInventory(
                    sit->second, itemId, cnt, 0);
                // Vanilla keeps the item on the ground when the inventory is
                // completely full; only collect if at least one unit landed.
                if (remaining >= cnt)
                    continue;

                // [WIRE] Collect Item (0x0D) + Destroy Entities (0x13): the client
                // plays the suck-in animation and stops rendering the ground item.
                LightweightMC::Core::Logger::warn(std::string("[WIRE Pickup 0x0D+0x13] ")
                    + "player="+sit->second.username+" eid="+std::to_string(eid)+" itemId="+std::to_string(itemId));

                // Payload-only bodies: sendPacket prepends the wire-ID varint.
                std::vector<uint8_t> collect;
                LightweightMC::Network::Packet::writeVarInt(collect, eid);
                LightweightMC::Network::Packet::writeVarInt(collect, bestFd);
                packetSender(bestFd, 0x0D, collect);

                std::vector<uint8_t> destroy;
                LightweightMC::Network::Packet::writeVarInt(destroy, 1);
                LightweightMC::Network::Packet::writeVarInt(destroy, eid);
                packetSender(bestFd, 0x13, destroy);

                em.removeItemDrop(eid);
                LightweightMC::Core::Logger::info("[World] " + sit->second.username +
                    " picked up item eid=" + std::to_string(eid) +
                    " itemId=" + std::to_string(itemId));
            }
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

    // Wall-clock ms used for disconnect tombstones so queued teardowns line up
    // with the same steady_clock timeline pollEvents/tick loops read. Not exposed
    // in the header because only NetworkEvents + flushPendingDisconnects use it.
    static inline int64_t pendingDisconnectNow()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // Mark the client for teardown instead of erasing m_clients[fd] immediately.
    // The old design erased + closed in one step, which corrupted callers that still
    // hold a PlayerSession& or queued packet for fd later in the same tick (sendBuffer
    // flushes during ScoreboardManager::tick / kick chains read session state after
    // handleDisconnect already erased it -> use-after-free). Now the session is parked
    // as ClientState::DISCONNECTED with a timestamp and flushPendingDisconnects erases
    // m_clients + closes the socket once the pass' pending references have drained. The
    // fd's epoll registration is dropped right away so pollEvents stops serving dead
    // clients while close() stays behind the flush so any queued sendPacket(fd, ...)
    // for this pass still resolves to a live PlayerSession before teardown.
    void NetworkManager::handleDisconnect(int fd)
    {
        std::string username = "Unauthenticated Client";
        const std::string ipPort = Core::getIpAndPortFromFd(fd);

        auto it = m_clients.find(fd);
        if (it == m_clients.end())
            return;

        // Idempotent: an RDHUP / send()==0 / kick cascade can reach the same fd twice
        // before flushPendingDisconnects drains tombstones. The first pass saved the
        // player, marked the session and dropped the epoll registration, so later passes
        // must NOT re-save (the row is already persisted) or double-remove from epoll.

        if (it->second.state == LightweightMC::Player::ClientState::DISCONNECTED)
        {
            Core::Logger::debug("[NET DISCONNECT] tombstone fd=" + std::to_string(fd) +
                                " queuedAtMs=" + std::to_string(it->second.disconnectedAtMs) +
                                " — already queued, skipping re-save");
            return;
        }

        if (!it->second.username.empty())
        {
            username = it->second.username;
            Player::PlayerManager::getInstance().savePlayerState(it->second, m_database);
        }

        it->second.state = LightweightMC::Player::ClientState::DISCONNECTED;
        it->second.disconnectedAtMs = pendingDisconnectNow();

        Core::Logger::debug("[NET DISCONNECT] tombstone queued fd=" + std::to_string(fd) +
                            " — flushPendingDisconnects erases m_clients after this tick");
        Managers::ScoreboardManager::getInstance().removeClientCache(fd);
        ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);

        // Queueing stays quiet (DEBUG) so the single INFO "disconnected" line comes
        // from flushPendingDisconnects when the deferred close() actually runs.
        Core::Logger::debug("Client " + std::to_string(fd) + " (" + username +
                            ") queued for disconnect teardown. [IP: " + ipPort + "]");
        // Reclaim memory back to Linux kernel if no clients are connected
        if (m_clients.empty())
        {
            // sqlite3_db_release_memory(m_database.);
            getWorldStorage(0).releaseMemory();
            ::malloc_trim(0);
        }
    }

    // Erase every ClientState::DISCONNECTED session handleDisconnect parked and close
    // the deferred socket fds. pollEvents invokes this right after the epoll event loop
    // (and ~NetworkManager runs it explicitly), so callers never see a DISCONNECTED
    // handle outside the narrow window between handleDisconnect and the flush pass —
    // queued packet sites in that window read a valid PlayerSession whose send() hits
    // a dead socket instead of a dangling reference. `nowMs` documents the flush
    // window (steady-clock ms) for diagnostics only; iteration goes over a copy of
    // the pending fds because each erase mutates m_clients mid-pass.
    void NetworkManager::flushPendingDisconnects(int64_t nowMs)
    {
        std::vector<int> pending;
        for (const auto &[fd, session] : m_clients)
        {
            if (session.state == LightweightMC::Player::ClientState::DISCONNECTED)
                pending.push_back(fd);
        }

        for (int fd : pending)
        {
            auto it = m_clients.find(fd);
            if (it == m_clients.end())
                continue;

            // Capture the fields before erasing — reading them after m_clients.erase(it)
            // would read a dangling PlayerSession.
            const std::string username = it->second.username.empty()
                                             ? "Unauthenticated Client"
                                             : it->second.username;
            const int64_t queuedAtMs = it->second.disconnectedAtMs;
            // Socket stays open until this point so ipPort is still resolvable here.
            const std::string ipPort = Core::getIpAndPortFromFd(fd);

            m_clients.erase(it);
            ::close(fd);
            malloc_trim(0);

            Core::Logger::debug("[NET DISCONNECT] flushed tombstone fd=" + std::to_string(fd) +
                                " queuedAtMs=" + std::to_string(queuedAtMs) +
                                " flushWindowMs=" + std::to_string(nowMs));
            Core::Logger::info("Client " + std::to_string(fd) + " (" + username +
                               ") disconnected. [IP: " + ipPort + "]");
        }
    }
    // Phase 10 (ban/kick/whitelist). allowLoginGate answers whether
    // `username` may connect right now: explicit bans always win, and when the
    // whitelist is enabled only whitelisted names pass (vanilla semantics; see
    // src/network/BanList.cpp sidecar format).
    bool NetworkManager::allowLoginGate(const std::string &username)
    {
        if (Network::isPlayerBanned(username))
            return false;
        if (Network::getWhitelistEnabled() && !Network::isWhitelisted(username))
            return false;
        return true;
    }

    // Deny a login that passed the wire but failed the gate: CannotConnect
    // (wire 0x03, payload = plain reason string + NUL terminator like LoginHandler
    // uses for its own auth-failure path) then close the socket.
    void NetworkManager::denyLoginOnFd(int fd, const std::string &reason)
    {
        std::vector<uint8_t> payload;
        LightweightMC::Network::Packet::writeString(payload, reason);
        payload.push_back(0);
        sendPacket(fd, 0x03, payload);
        handleDisconnect(fd);
    }

    // Phase 13: InvalidSessionToken (S2C wire 0x04) — the counterpart of denyLoginOnFd
    // for the token-era auth-mode path. Wire verified against tools/protocol_seed/
    // packet_ids_1.8.json + src/network/handlers/LoginHandler.cpp (the server replies
    // 0x02 LoginSuccess / 0x03 CannotConnect / 0x04 InvalidSessionToken); the payload
    // is the plain reason string like the other auth-failure replies in this file.
    // Unlike denyLoginOnFd this keeps the socket open: a token-era client that sent a
    // stale/foreign session token still gets its LoginStart handshake answered, it
    // just does not get an authenticated identity — mirrors what community relays do.
    void NetworkManager::denySessionTokenOnFd(int fd, const std::string &reason)
    {
        std::vector<uint8_t> payload;
        LightweightMC::Network::Packet::writeString(payload, reason);
        payload.push_back(0);
        sendPacket(fd, 0x04, payload);
    }

    // Kick a connected session by username (case-insensitive like login names).
    // Wire note: the Disconnect packet is S2C wire 0x00 whose payload is a JSON
    // object carrying the reason string (PacketCodec47::encodeDisconnect contract);
    // NOT the "0x40" that phases.md's draft roadmap line guessed -- verified against
    // include/network/protocol/PacketCodec47.hpp and tools/protocol_seed/packet_ids_1.8.json.
    std::string NetworkManager::kickSessionByName(const std::string &name, const std::string &reason)
    {
        std::string lowerName = name;
        for (char &c : lowerName)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto &[fd, session] : m_clients)
        {
            if (session.state != LightweightMC::Player::ClientState::PLAY &&
                session.username.empty())
                continue;
            std::string candidate = session.username;
            for (char &c : candidate)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (candidate != lowerName)
                continue;
            const std::string resolved = session.username;
            std::vector<uint8_t> payload;
            LightweightMC::Network::Packet::writeString(payload,
                                                        R"({"reason":")" + Core::escapeChatJson(reason) + R"("})");
            payload.push_back(0);
            sendPacket(fd, 0x00, payload);
            handleDisconnect(fd);
            return resolved;
        }
        return {};
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
