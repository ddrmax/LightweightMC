#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <limits>

namespace LightweightMC::Network::Protocol
{
    class EraAdapter;   // Forward declaration (defined in network/protocol/EraAdapter.hpp)
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

    // -------------------------------------------------------------
    // Container-window state model (chest, furnace, brewing-stand, …).
    // Keyed by windowId for the currently-open session; persisted to the
    // block_entities SQLite table keyed by the container's world coord
    // (chunk_x, chunk_z, rel_x, y, rel_z).
    // -------------------------------------------------------------
    struct WindowInfo
    {
        int16_t chunkX{0};
        int16_t chunkZ{0};
        int16_t relX{0};
        int16_t y{0};
        int16_t relZ{0};
        uint16_t blockType{0};   // existing combined block data (id<<4 | meta)
        std::vector<ItemStack> items;
        bool dirty{false};
    };

    // Free helpers used by the container handlers. These encode a compact
    // varint-count followed by count × (slot:short, id:short, count:byte,
    // damage:short). Slot count is stored separately by the caller.
    void serializeContainerItems(const std::vector<ItemStack> &items,
                                 std::vector<uint8_t> &out);
    void deserializeContainerItems(const std::vector<uint8_t> &blob,
                                   int expectedSlotCount,
                                   std::vector<ItemStack> &out);

    struct PlayerSession
    {
        int fd{-1};
        ClientState state{ClientState::HANDSHAKE};
        std::string username{""};
        std::string uuid{""}; // Offline-mode UUID (deterministic from username), set at login.

        double x{0.0}, y{7.0}, z{0.0};
        float yaw{0.0f}, pitch{0.0f};
        bool onGround{false};

        int currentChunkX{0};
        int currentChunkZ{0};
        // Previous chunk used to derive the player's velocity vector on each
        // updateChunksAroundPlayer call. Initialized to a sentinel so the first
        // call after login is detected as the "login burst" (no velocity) and
        // uses the legacy sync-generation path; subsequent calls (movement
        // crossings) use the async prefetch path and bias the generate box in
        // this direction so high-speed travel doesn't outrun the worker.
        std::int32_t prevChunkX{std::numeric_limits<std::int32_t>::min()};
        std::int32_t prevChunkZ{std::numeric_limits<std::int32_t>::min()};
        std::unordered_set<ChunkPos, ChunkPosHash> loadedChunks{};

        // -------------------------------------------------------------
        // Multiworld / dimension-plane selector (matches the pre-1.12 vanilla
        // Java Edition dimension ids used by NetworkManager::start in
        // NetworkSocket.cpp and the world_registry SQLite table):
        //   worldId =  0  -> Overworld (the fallback plane every accessor
        //                     falls back to when worldId is unknown)
        //   worldId = -1  -> Nether (the underworld plane, id is signed)
        //   worldId =  1  -> End (the deepest plane)
        //   worldId = 100 -> Aether (reserved Minecraft-Legends-style special
        //                     dimension; see [world] aether-plane-id in
        //                     server.properties, default 100)
        //   worldId >= 101 -> custom planes auto-allocated at runtime by
        //                     Database::registerWorld via
        //                     kFirstCustomWorldId = 101 (so the reserved 100
        //                     slot never collides with a `/world create` id
        //                     and future Legends-style planes can claim
        //                     another 100-series id without touching the
        //                     vanilla 0 / -1 / 1 slots or custom planes).
        //
        // Session state (position, loadedChunks, rx/send buffers) is per-world:
        // when worldId changes we clear loadedChunks so the new plane's chunks
        // stream to the client fresh. Database::setPlayerWorldIdByName persists
        // the change on the row for the given username, and Database::loadPlayerData
        // reads it back on login so a reconnect lands on the same plane.
        // -------------------------------------------------------------
        int32_t worldId{0};

        int16_t selectedSlot{0};
        uint8_t openWindowId{0}; // next window ID to assign when opening a container
        std::unordered_map<int, ItemStack> inventory{};

        // Currently-open container windows for this session (windowId -> state).
        // Contents are flushed to SQLite (block_entities) on every click;
        // this map is cleared on login (see resetVolatileState) so a reconnect
        // reopens a fresh window that is re-populated from the SQLite row.
        std::unordered_map<uint8_t, WindowInfo> openWindows;

        // -------------------------------------------------------------
        // Persistent player state (mirrors the fields of the vanilla
        // player.dat format, https://minecraft.wiki/w/Player.dat_format,
        // stored in the dedicated SQLite database instead of NBT).
        // -------------------------------------------------------------
        float health{20.0f};             // Health (NBT: Health)
        int8_t foodLevel{20};            // Food level (NBT: foodLevel)
        float foodSaturationLevel{5.0f}; // Food saturation (NBT: foodSaturationLevel)
        float foodExhaustionLevel{0.0f}; // Food exhaustion (NBT: foodExhaustionLevel)
        uint8_t gamemode{1};             // Game mode (NBT: playerGameType) 0=survival,1=creative,2=adventure,3=spectator

        int32_t spawnX{0}, spawnY{64}, spawnZ{0}; // World/bed spawn (NBT: SpawnX/Y/Z)
        bool hasSpawn{false};                     // True once a spawn point has been set (bed or /spawnpoint)

        float fallDistance{0.0f}; // Fall distance (NBT: fallDistance)
        int32_t air{30};          // Air ticks remaining (NBT: air)
        bool onFire{false};       // On fire (NBT: OnFire)
        int16_t fire{0};          // Fire ticks remaining (NBT: fire)

        bool sleeping{false};                 // Sleeping in bed (NBT: sleeping)
        uint8_t sleepTimer{0};                // Sleep counter (NBT: sleepTimer)
        bool hasBedSpawn{false};              // Has a bed spawn point (NBT: SpawnForced)
        int32_t bedX{-1}, bedY{-1}, bedZ{-1}; // Bed spawn coordinates (NBT: Bed)

        bool flying{false};             // Creative/spectator flight (NBT: EnderPearls / derived from gamemode + flags)
        bool sprinting{false};          // Sprinting (NBT: playerSprinting)
        int16_t experienceLevel{0};     // Experience level (NBT: XpLevel)
        float experienceProgress{0.0f}; // XP progress to next level (NBT: XpP)
        int32_t totalExperience{0};     // Total XP (NBT: XpTotal)

        uint8_t opLevel{0}; // Operator permission level (0 = not an operator, 1-4; console is 4)
        bool isOperator() const { return opLevel > 0; }
        void setOperator(bool isOp, int level = 3) { opLevel = isOp ? static_cast<uint8_t>(level) : 0; }

        uint8_t deathCount{0}; // Death count (NBT: DeathCount)
        int64_t lastSeen{0};   // Unix timestamp of last activity (not in player.dat, DB only)

        std::vector<uint8_t> rxBuffer{};
        std::vector<uint8_t> sendBuffer{};

        // Multi-version protocol support: the era adapter chosen during handshake.
        // Non-null once the client has sent a Handshake packet. Callers do not own
        // the pointer (adapters are process-lifetime singletons from EraAdapter::create).
        Network::Protocol::EraAdapter *adapter{nullptr};
        int32_t clientProtocol{47}; // Protocol number reported by the client in the handshake.

        // Set once the first send to an unsupported-era client has been logged;
        // prevents NetworkManager::sendPacket from spamming the console for
        // every subsequent packet from that same client.
        bool unsupportedEraWarned{false};

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