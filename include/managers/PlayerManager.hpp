#pragma once
#include "player/PlayerSession.hpp"
#include "storage/Database.hpp"
#include "managers/ScoreboardManager.hpp"
#include "managers/WorldManager.hpp"
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>

// Generates a deterministic offline-mode UUID string for a given username.
// Free function so it can be shared by the login handler and the player manager.
std::string generatePlayerUuid(const std::string &username);

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

        // Builds and broadcasts an Entity Equipment packet (0x04) for the item currently
        // in one of the player's slots, encoding the item with the client's own protocol.
        void broadcastEquipmentForSession(PlayerSession &session, int16_t slot, const BroadcastSender &broadcastPacket);

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