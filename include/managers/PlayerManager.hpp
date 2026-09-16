#pragma once
#include "player/PlayerSession.hpp"
#include "storage/Database.hpp"
#include "managers/ScoreboardManager.hpp"
#include "managers/WorldManager.hpp"
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>

namespace LightweightMC::Player
{
    using PacketSender = Managers::PacketSender;
    using BroadcastSender = std::function<void(int32_t packetId, const std::vector<uint8_t> &payload, int ignoreFd)>;

    class PlayerManager
    {
    public:
        static PlayerManager &getInstance()
        {
            static PlayerManager instance;
            return instance;
        }

        PlayerManager(const PlayerManager &) = delete;
        PlayerManager &operator=(const PlayerManager &) = delete;

        // Generates a deterministic offline UUID string for a given username
        static std::string getPlayerUuid(const std::string &username);

        // Teleports a player to exact target coordinates
        void teleportPlayer(int fd, PlayerSession &session, double x, double y, double z, float yaw, float pitch, const PacketSender &sendPacket);

        // Initializes play session (Play Packets, Tablist, Spawning, and restoring PlayerData from SQLite)
        void sendPlayPackets(int clientFd, PlayerSession &session, const std::string &username,
                             std::unordered_map<int, PlayerSession> &clients, World::WorldManager &worldManager,
                             Storage::Database &db,
                             const PacketSender &sendPacket, const BroadcastSender &broadcastPacket);

        // Broadcasts entity equipment to other players
        void broadcastEquipment(int entityId, int16_t itemSlot, int16_t itemId, const BroadcastSender &broadcastPacket);

        // Saves current player state (coordinates, spawn, inventory) to SQLite database
        void savePlayerState(const PlayerSession &session, Storage::Database &db);

    private:
        PlayerManager() = default;

        static std::vector<uint8_t> serializeInventory(const std::unordered_map<int, ItemStack> &inventory);
        static void deserializeInventory(const std::vector<uint8_t> &data, std::unordered_map<int, ItemStack> &outInventory);
    };
}