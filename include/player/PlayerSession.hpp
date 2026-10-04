#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace LightweightMC::Network::Protocol
{
    class EraAdapter; // Forward declaration (defined in network/protocol/EraAdapter.hpp)
    class BaseProtocol; // Forward declaration (defined in network/protocol/BaseProtocol.hpp)
}

namespace LightweightMC::Storage
{
    struct PlayerData; // Forward declaration (defined in storage/Database.hpp)
}

namespace LightweightMC::Player
{
    enum class ClientState
    {
        HANDSHAKE,
        STATUS,
        LOGIN,
        PLAY
    };

    struct ChunkPos
    {
        int x;
        int z;
        bool operator==(const ChunkPos &other) const { return x == other.x && z == other.z; }
    };

    struct ChunkPosHash
    {
        std::size_t operator()(const ChunkPos &pos) const
        {
            return std::hash<int>()(pos.x) ^ (std::hash<int>()(pos.z) << 1);
        }
    };

    struct ItemStack
    {
        int16_t id = -1;
        uint8_t count = 0;
        int16_t damage = 0;
    };

    struct PlayerSession
    {
        int fd{-1};
        ClientState state{ClientState::HANDSHAKE};
        std::string username{""};
        std::string uuid{""}; // Offline-mode UUID (deterministic from username), set at login.

        double x{0.0}, y{7.0}, z{0.0};
        float yaw{0.0f}, pitch{0.0f};

        int currentChunkX{0};
        int currentChunkZ{0};
        std::unordered_set<ChunkPos, ChunkPosHash> loadedChunks{};

        int16_t selectedSlot{0};
        uint8_t openWindowId{0}; // next window ID to assign when opening a container
        std::unordered_map<int, ItemStack> inventory{};

        // -------------------------------------------------------------
        // Persistent player state (mirrors the fields of the vanilla
        // player.dat format, https://minecraft.wiki/w/Player.dat_format,
        // stored in the dedicated SQLite database instead of NBT).
        // -------------------------------------------------------------
        float health{20.0f};           // Health (NBT: Health)
        int8_t foodLevel{20};          // Food level (NBT: foodLevel)
        float foodSaturationLevel{5.0f}; // Food saturation (NBT: foodSaturationLevel)
        float foodExhaustionLevel{0.0f}; // Food exhaustion (NBT: foodExhaustionLevel)
        uint8_t gamemode{1};           // Game mode (NBT: playerGameType) 0=survival,1=creative,2=adventure,3=spectator

        int32_t spawnX{0}, spawnY{64}, spawnZ{0}; // World/bed spawn (NBT: SpawnX/Y/Z)
        bool hasSpawn{false};                      // True once a spawn point has been set (bed or /spawnpoint)

        float fallDistance{0.0f};                  // Fall distance (NBT: fallDistance)
        int32_t air{30};                           // Air ticks remaining (NBT: air)
        bool onFire{false};                        // On fire (NBT: OnFire)
        int16_t fire{0};                           // Fire ticks remaining (NBT: fire)

        bool sleeping{false};                      // Sleeping in bed (NBT: sleeping)
        uint8_t sleepTimer{0};                     // Sleep counter (NBT: sleepTimer)
        bool hasBedSpawn{false};                    // Has a bed spawn point (NBT: SpawnForced)
        int32_t bedX{-1}, bedY{-1}, bedZ{-1};      // Bed spawn coordinates (NBT: Bed)

        bool flying{false};                        // Creative/spectator flight (NBT: EnderPearls / derived from gamemode + flags)
        bool sprinting{false};                     // Sprinting (NBT: playerSprinting)
        int16_t experienceLevel{0};                // Experience level (NBT: XpLevel)
        float experienceProgress{0.0f};            // XP progress to next level (NBT: XpP)
        int32_t totalExperience{0};                // Total XP (NBT: XpTotal)

        uint8_t opLevel{0};                         // Operator permission level (0 = not an operator, 1-4; console is 4)
        bool isOperator() const { return opLevel > 0; }
        void setOperator(bool isOp, int level = 3) { opLevel = isOp ? static_cast<uint8_t>(level) : 0; }

        uint8_t deathCount{0};                      // Death count (NBT: DeathCount)
        int64_t lastSeen{0};                         // Unix timestamp of last activity (not in player.dat, DB only)

        std::vector<uint8_t> rxBuffer{};
        std::vector<uint8_t> sendBuffer{};

        // Multi-version protocol support: the era adapter chosen during handshake.
        // Non-null once the client has sent a Handshake packet. Callers do not own
        // the pointer (adapters are process-lifetime singletons from EraAdapter::create).
        Network::Protocol::EraAdapter *adapter{nullptr};
        int32_t clientProtocol{47}; // Protocol number reported by the client in the handshake.

        // Version-specific framing / packet ID / field encodings, chosen during
        // handshake (BaseProtocol::create). Non-null once the client has sent a
        // Handshake packet. Callers do not own the pointer (singletons).
        Network::Protocol::BaseProtocol *protocol{nullptr};

        // -------------------------------------------------------------
        // Persistence mapping (implemented in src/player/PlayerSession.cpp):
        // copies fields to/from the SQLite PlayerData row. The inventory is
        // handled separately via the custom varint blob format.
        // -------------------------------------------------------------
        void loadFrom(const Storage::PlayerData &data);
        void applyTo(Storage::PlayerData &data) const;

        // Resets volatile state (position, health, food, status flags) to fresh-spawn defaults.
        void resetVolatileState();
    };
}
using LightweightMC::Player::ChunkPos;
using LightweightMC::Player::ChunkPosHash;
using LightweightMC::Player::ClientState;