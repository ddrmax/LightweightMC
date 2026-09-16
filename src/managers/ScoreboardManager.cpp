#include "managers/ScoreboardManager.hpp"
#include "core/utils.hpp"
#include "network/Packet.hpp"
#include <sstream>
#include <iomanip>
#include <unordered_set>

namespace LightweightMC::Managers
{
    void ScoreboardManager::sendScoreboard(int clientFd, const std::unordered_map<int, LightweightMC::Player::PlayerSession> &clients, const PacketSender &sendPacket)
    {
        std::string objectiveName = "server_stats";
        std::string title = SECTION "e" SECTION "lLIGHTWEIGHT MC";

        // 1. Create the objective (Packet 0x3B - Scoreboard Objective)
        std::vector<uint8_t> p1;
        LightweightMC::Network::Packet::writeString(p1, objectiveName);
        p1.push_back(0); // 0 = Create
        LightweightMC::Network::Packet::writeString(p1, title);
        LightweightMC::Network::Packet::writeString(p1, "integer");
        sendPacket(clientFd, 0x3B, p1);

        // 2. Display in Sidebar (Packet 0x3D - Display Scoreboard)
        std::vector<uint8_t> p2;
        p2.push_back(1); // Position 1 = Sidebar
        LightweightMC::Network::Packet::writeString(p2, objectiveName);
        sendPacket(clientFd, 0x3D, p2);

        // -------------------------------------------------------------
        // Helpers for Score Packets Management (Packet 0x3C)
        // -------------------------------------------------------------
        auto removeScoreLine = [&](const std::string &scoreName)
        {
            std::vector<uint8_t> p3;
            LightweightMC::Network::Packet::writeVarInt(p3, static_cast<int32_t>(scoreName.size()));
            p3.insert(p3.end(), scoreName.begin(), scoreName.end());
            p3.push_back(1); // Action 1 = Remove
            LightweightMC::Network::Packet::writeVarInt(p3, static_cast<int32_t>(objectiveName.size()));
            p3.insert(p3.end(), objectiveName.begin(), objectiveName.end());

            sendPacket(clientFd, 0x3C, p3);
        };

        auto sendScoreLine = [&](const std::string &scoreName, int scoreValue)
        {
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
        // Calculate Server Data
        // -------------------------------------------------------------
        double ram = LightweightMC::Core::getProcessRAM();
        double cpu = LightweightMC::Core::getCPUUsage();
        int onlinePlayers = static_cast<int>(clients.size());

        std::unordered_set<ChunkPos, ChunkPosHash> globalLoadedChunks;
        for (const auto &[fd, session] : clients)
        {
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
        auto &oldLines = m_clientScoreboardCache[clientFd];
        for (const auto &oldLine : oldLines)
        {
            removeScoreLine(oldLine);
        }

        // 4. Send NEW lines (descending scores to order the list)
        int score = static_cast<int>(newLines.size());
        for (const auto &line : newLines)
        {
            sendScoreLine(line, score--);
        }

        // Update local cache for the next refresh
        oldLines = newLines;
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

        sendPacket(clientFd, 0x47, payload);
    }

    void ScoreboardManager::removeClientCache(int clientFd)
    {
        m_clientScoreboardCache.erase(clientFd);
    }
}

