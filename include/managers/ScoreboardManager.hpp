#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include "player/PlayerSession.hpp"

namespace LightweightMC::Managers
{
    using PacketSender = std::function<void(int clientFd, int32_t packetId, const std::vector<uint8_t> &payload)>;

    class ScoreboardManager
    {
    public:
        static ScoreboardManager &getInstance()
        {
            static ScoreboardManager instance;
            return instance;
        }

        ScoreboardManager(const ScoreboardManager &) = delete;
        ScoreboardManager &operator=(const ScoreboardManager &) = delete;

        // Sends the sidebar scoreboard to a specific client
        void sendScoreboard(int clientFd, const std::unordered_map<int, LightweightMC::Player::PlayerSession> &clients, const PacketSender &sendPacket);

        // Sends the player list header and footer (Tablist Header/Footer)
        void sendTablistHeaderFooter(int clientFd, size_t onlinePlayerCount, const PacketSender &sendPacket);

        // Clears the scoreboard cache when a client disconnects.
        void removeClientCache(int clientFd);

    private:
        ScoreboardManager() = default;
        std::unordered_map<int, std::vector<std::string>> m_clientScoreboardCache;
    };
}

