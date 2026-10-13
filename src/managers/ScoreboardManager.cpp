#include "managers/ScoreboardManager.hpp"
#include "core/utils.hpp"
#include "network/Packet.hpp"
#include "core/Logger.hpp"
#include <sstream>
#include <iomanip>
#include <unordered_set>

namespace LightweightMC::Managers
{
    void ScoreboardManager::sendScoreboard(int clientFd, const std::unordered_map<int, LightweightMC::Player::PlayerSession> &clients, const PacketSender &sendPacket)
    {
        Core::Logger::debug("[SCOREBOARD] sendScoreboard enter fd=" + std::to_string(clientFd) +
                            " cachedLines=" +
                            std::to_string(
                                [&]() {
                                    auto pre = m_clientScoreboardCache.find(clientFd);
                                    return pre == m_clientScoreboardCache.end() ? -1 : static_cast<int>(pre->second.size());
                                }()));

        std::string objectiveName = "server_stats";
        std::string title = SECTION "e" SECTION "lLIGHTWEIGHT MC";

        // 1. Create the objective (Packet 0x3B - Scoreboard Objective)
        std::vector<uint8_t> p1;
        LightweightMC::Network::Packet::writeString(p1, objectiveName);
        p1.push_back(0); // 0 = Create
        LightweightMC::Network::Packet::writeString(p1, title);
        LightweightMC::Network::Packet::writeString(p1, "integer");
        Core::Logger::debug("[SCOREBOARD] 0x3B CreateObjective fd=" + std::to_string(clientFd) +
                            " objective=\"" + objectiveName + "\"");
        sendPacket(clientFd, 0x3B, p1);

        // 2. Display in Sidebar (Packet 0x3D - Display Scoreboard)
        std::vector<uint8_t> p2;
        p2.push_back(1); // Position 1 = Sidebar
        LightweightMC::Network::Packet::writeString(p2, objectiveName);
        Core::Logger::debug("[SCOREBOARD] 0x3D DisplayObjective fd=" + std::to_string(clientFd) +
                            " objective=\"" + objectiveName + "\" position=1");
        sendPacket(clientFd, 0x3D, p2);

        // -------------------------------------------------------------
        // Helpers for Score Packets Management (Packet 0x3C)
        // -------------------------------------------------------------
        auto removeScoreLine = [&](const std::string &scoreName)
        {
            Core::Logger::debug("[SCOREBOARD] 0x3C REMOVE-line fd=" + std::to_string(clientFd) +
                                " name=\"" + scoreName + "\" objective=\"" + objectiveName + "\"");
            std::vector<uint8_t> p3;
            LightweightMC::Network::Packet::writeVarInt(p3, static_cast<int32_t>(scoreName.size()));
            p3.insert(p3.end(), scoreName.begin(), scoreName.end());
            p3.push_back(2); // Action 2 = REMOVE (0=set, 1=change-delta, 2=remove).
                             // The previous value here was 1 which is *change* — that never
                             // actually removed the entry, so old rows with stale RAM/CPU/chunk
                             // values stacked up on every refresh (the duplication bug).
            LightweightMC::Network::Packet::writeVarInt(p3, static_cast<int32_t>(objectiveName.size()));
            p3.insert(p3.end(), objectiveName.begin(), objectiveName.end());
            // The Java client's UpdateScore handler reads the value field regardless of mode,
            // so keep the packet shape consistent with sendScoreLine (which writes a VarInt
            // score). The value itself is ignored for mode=2, but the bytes must be present.
            LightweightMC::Network::Packet::writeVarInt(p3, 0);

            sendPacket(clientFd, 0x3C, p3);
        };

        auto sendScoreLine = [&](const std::string &scoreName, int scoreValue)
        {
            Core::Logger::debug("[SCOREBOARD] 0x3C SET-line fd=" + std::to_string(clientFd) +
                                " name=\"" + scoreName + "\" score=" + std::to_string(scoreValue));
            std::vector<uint8_t> p3;
            LightweightMC::Network::Packet::writeVarInt(p3, static_cast<int32_t>(scoreName.size()));
            p3.insert(p3.end(), scoreName.begin(), scoreName.end());
            p3.push_back(0); // Action 0 = Create / Update
            LightweightMC::Network::Packet::writeVarInt(p3, static_cast<int32_t>(objectiveName.size()));
            p3.insert(p3.end(), objectiveName.begin(), objectiveName.end());
            LightweightMC::Network::Packet::writeVarInt(p3, static_cast<int32_t>(scoreValue));

            sendPacket(clientFd, 0x3C, p3);
        };

        // -------------------------------------------------------------
        // Shared aggregates (RAM/CPU/chunk set) are hoisted into tick() so
        // they are computed once per refresh, not once per client. The
        // single-shot overload below keeps this entry point working for
        // callers that do not go through tick().
        // -------------------------------------------------------------
        double ram = LightweightMC::Core::getProcessRAM();
        double cpu = LightweightMC::Core::getCPUUsage();
        int onlinePlayers = static_cast<int>(clients.size());

        std::unordered_set<ChunkPos, ChunkPosHash> globalLoadedChunks;
        for (const auto &[fd, session] : clients)
        {
            if (session.state == ClientState::PLAY)
                globalLoadedChunks.insert(session.loadedChunks.begin(), session.loadedChunks.end());
        }
        int totalGlobalChunks = static_cast<int>(globalLoadedChunks.size());

        std::stringstream ramStream;
        ramStream << std::fixed << std::setprecision(1) << ram;

        std::stringstream cpuStream;
        cpuStream << std::fixed << std::setprecision(1) << cpu;

        // Build the new line list
        std::vector<std::string> newLines = {
            SECTION "fPlayers: " SECTION "a" + std::to_string(onlinePlayers),
            SECTION "fLoaded Chunks: " SECTION "e" + std::to_string(totalGlobalChunks),
            SECTION "fRAM: " SECTION "b" + ramStream.str() + " Mo",
            SECTION "fCPU Load: " SECTION "c" + cpuStream.str()};

        // 3. Purge OLD lines to avoid duplication
        //
        // NOTE: sendPacket can cascade into NetworkManager::flushSendBuffer
        // which, on a dead client, calls handleDisconnect() -> removeClientCache(fd)
        // -> m_clientScoreboardCache.erase(fd). That invalidates any reference
        // we held into the cache (oldLines) and re-fetch below has to look up
        // the map again -- so snapshot the previous lines into a local copy
        // first, then only write back to the map if the client is still cached.
        std::vector<std::string> previousLines;
        auto cacheIt = m_clientScoreboardCache.find(clientFd);
        if (cacheIt != m_clientScoreboardCache.end())
            previousLines = cacheIt->second;
        Core::Logger::debug("[SCOREBOARD] cache-lookup fd=" + std::to_string(clientFd) +
                            " hit=" + std::to_string(cacheIt != m_clientScoreboardCache.end() ? 1 : 0) +
                            " previous=" + std::to_string(previousLines.size()) +
                            " totalCacheEntries=" + std::to_string(m_clientScoreboardCache.size()));
        for (const auto &oldLine : previousLines)
        {
            removeScoreLine(oldLine);
        }

        // 4. Send NEW lines (descending scores to order the list)
        int score = static_cast<int>(newLines.size());
        for (const auto &line : newLines)
        {
            sendScoreLine(line, score--);
        }

        // Update local cache for the next refresh -- re-look up because removeClientCache()
        // may have erased the entry while we were mid-loop.
        cacheIt = m_clientScoreboardCache.find(clientFd);
        if (cacheIt != m_clientScoreboardCache.end())
        {
            cacheIt->second = newLines;
            Core::Logger::debug("[SCOREBOARD] cache-write fd=" + std::to_string(clientFd) +
                                " newCachedLines=" + std::to_string(newLines.size()));
        }
        else
        {
            Core::Logger::debug("[SCOREBOARD] cache-write ABORT fd=" + std::to_string(clientFd) +
                                " — entry was erased mid-loop (client disconnected during sendScoreLine)");
        }
    }

    // Hoist shared aggregates and dispatch the per-client sendScoreboard +
    // sendTablistHeaderFooter calls in a single O(N) pass. NetworkManager
    // invokes this once per refresh window instead of inlining the same
    // loop -- which previously rebuilt the RAM/CPU/loaded-chunk aggregate
    // inside every per-client iteration (O(N^2) in PLAY clients).
    void ScoreboardManager::tick(const std::unordered_map<int, LightweightMC::Player::PlayerSession> &clients, const PacketSender &sendPacket)
    {
        // Snapshot the (fd -> PLAY-state) list before iterating. The inner
        // sendScoreboard / sendTablistHeaderFooter calls go through NetworkManager::sendPacket
        // which can cascade flushSendBuffer -> handleDisconnect -> m_clients.erase(fd)
        // when the socket is already dead; erasing mid-iteration invalidates
        // the range-for iterator (and any reference we captured for `session`),
        // so a plain loop over the live map risks UB / segfault on a later
        // session field. Copy the fds we intend to service first, then check
        // liveness each pass before dispatching.
        std::vector<int> liveFds;
        liveFds.reserve(clients.size());
        int onlinePlayers = 0;
        std::unordered_set<ChunkPos, ChunkPosHash> globalLoadedChunks;
        for (const auto &[fd, session] : clients)
        {
            if (session.state != ClientState::PLAY)
                continue;
            liveFds.push_back(fd);
            onlinePlayers++;
            globalLoadedChunks.insert(session.loadedChunks.begin(), session.loadedChunks.end());
        }

        Core::Logger::debug("[SCOREBOARD] tick: PLAY fds=" + std::to_string(liveFds.size()) +
                            " totalClients=" + std::to_string(clients.size()));

        for (int fd : liveFds)
        {
            auto it = clients.find(fd);
            if (it == clients.end())
            {
                Core::Logger::debug("[SCOREBOARD] tick: client erased mid-refresh fd=" + std::to_string(fd));
                continue;
            }
            if (it->second.state != ClientState::PLAY)
            {
                Core::Logger::debug("[SCOREBOARD] tick: state changed since snapshot fd=" + std::to_string(fd));
                continue;
            }
            sendScoreboard(fd, clients, sendPacket);
            sendTablistHeaderFooter(fd, static_cast<size_t>(onlinePlayers), sendPacket);
        }
    }

    void ScoreboardManager::sendTablistHeaderFooter(int clientFd, size_t onlinePlayerCount, const PacketSender &sendPacket)
    {
        std::string headerJson = R"({"text": ")" SECTION "e" SECTION R"(lLightweightMC Server\n)" SECTION R"(7Real Time Status"})";

        double ram = LightweightMC::Core::getProcessRAM();
        std::stringstream ramStream;
        ramStream << std::fixed << std::setprecision(1) << ram;

        std::string footerJson = R"({"text": ")" SECTION "fRAM Proc: " SECTION "b" + ramStream.str() + " Mo " SECTION "7| " SECTION "fPlayers: " SECTION "a" + std::to_string(onlinePlayerCount) + R"("})";

        // Packet 0x47 - Player List Header And Footer
        std::vector<uint8_t> payload;
        LightweightMC::Network::Packet::writeString(payload, headerJson);
        LightweightMC::Network::Packet::writeString(payload, footerJson);

        Core::Logger::debug("[SCOREBOARD] 0x47 TablistHeaderFooter fd=" + std::to_string(clientFd) +
                            " players=" + std::to_string(onlinePlayerCount));
        sendPacket(clientFd, 0x47, payload);
    }

    void ScoreboardManager::removeClientCache(int clientFd)
    {
        const size_t before = m_clientScoreboardCache.size();
        const size_t erased = m_clientScoreboardCache.erase(clientFd);
        Core::Logger::debug("[SCOREBOARD] removeClientCache fd=" + std::to_string(clientFd) +
                            " erased=" + std::to_string(erased) +
                            " (0=cache miss for this fd — cache was already cleared earlier)" +
                            " cacheBefore=" + std::to_string(before) +
                            " cacheAfter=" + std::to_string(m_clientScoreboardCache.size()));
    }
}

