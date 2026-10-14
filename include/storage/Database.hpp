#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <sqlite3.h>

namespace LightweightMC::Storage
{
    struct PlayerData
    {
        std::string uuid;
        std::string username;
        double spawnX{0.5}, spawnY{7.0}, spawnZ{0.5};
        float spawnYaw{0.0f}, spawnPitch{0.0f};
        double lastX{0.5}, lastY{7.0}, lastZ{0.5};
        float lastYaw{0.0f}, lastPitch{0.0f};
        // -------------------------------------------------------------
        // Inventory slot -- the exact blob the player currently holds
        // depends on the plane-scoped inventory policy stored on the
        // world_registry row (see `kInventoryPolicy*` constants below):
        //   * SYNC (kInventoryPolicySync, default for Overworld / Nether
        //     / End / Aether / mining worlds): maps to the flat
        //     `players.inventory_blob` column so /gotor or a portal
        //     carries items across planes transparently (standard
        //     Minecraft behaviour).
        //   * SEPARATE (kInventoryPolicySeparate) and RESTRICTED
        //     (kInventoryPolicyRestricted) planes map this slot to the
        //     plane-scoped blob on the `player_inventories(uuid, world_id)`
        //     SQLite table so switching planes swaps the whole inventory
        //     rather than carrying items across. SEPARATE is intended
        //     for flat / creative sandbox planes (flat world, no gen,
        //     usually paired with force-gamemode=creative); RESTRICTED
        //     is for PVP / PVE training worlds whose loot must never
        //     leak back into SYNC / SEPARATE inventories.
        // `Database::loadPlayerData` picks the correct slot on the
        // login path; `PlayerManager::savePlayerState` routes the
        // session's inventory to `savePlayerInventory` for non-SYNC
        // planes and preserves the flat `players.inventory_blob` so
        // the shared SYNC slot is never accidentally cleared.
        // -------------------------------------------------------------
        std::vector<uint8_t> inventoryData{};

        // -------------------------------------------------------------
        // player.dat-mirrored fields (see PlayerSession for NBT names).
        // Nullable/optional semantics: hasSpawn, hasBedSpawn and bedX/Y/Z
        // represent "not present" state when false / -1.
        // -------------------------------------------------------------
        float health{20.0f};
        int8_t foodLevel{20};
        float foodSaturationLevel{5.0f};
        float foodExhaustionLevel{0.0f};
        uint8_t gamemode{1};

        bool hasSpawn{false};
        int32_t bedX{-1}, bedY{-1}, bedZ{-1}; // -1 = no bed spawn (not present)
        bool hasBedSpawn{false};

        float fallDistance{0.0f};
        int32_t air{30};
        bool onFire{false};
        int16_t fire{0};

        bool sleeping{false};
        uint8_t sleepTimer{0};
        bool flying{false};
        bool sprinting{false};

        int16_t experienceLevel{0};
        float experienceProgress{0.0f};
        int32_t totalExperience{0};

        uint8_t opLevel{0}; // Operator permission level (0 = not an operator, 1-4)

        uint8_t deathCount{0};
        int64_t lastSeen{0}; // Unix timestamp, 0 = never recorded

        // Multiworld / dimension-plane selector (mirrors PlayerSession::worldId).
        // Persisted in the `players` SQLite table as `world_id INTEGER DEFAULT 0`
        // so a `/gotor <plane>` survives a relogin: `Database::loadPlayerData`
        // reads it back and PlayerManager's login handler writes it onto the
        // fresh PlayerSession before the first
        // `getWorldStorageFor(session.worldId)` call in `sendPlayPackets`.
        // Standard planes use pre-1.12 vanilla IDs (0 Overworld, -1 Nether, 1
        // End) plus a reserved 100-series slot for the Minecraft Legends-style
        // Aether special dimension; `/world create` custom planes allocate
        // IDs 101+ starting at `kFirstCustomWorldId` in `Database.cpp`.
        int32_t worldId{0};

        // Players hold independent coordinates per plane (standard Minecraft
        // behaviour for a multi-world server). `lastX/lastY/lastZ` on the
        // `players` row always describe the plane `world_id` names, so a plain
        // save/restore pair round-trips correctly. `hasPositionData` marks
        // whether the current PlayerData payload carries real coordinates;
        // partial-write callers (setPlayerOpLevel, setPlayerGameMode, ...)
        // build a PlayerData with default 0.5/7.0/0.5 which must NOT clobber
        // the per-plane mirror in the SQLite `player_positions` table. Player
        // Session's own `applyTo` sets this flag; the login/telemetry paths
        // leave it false.
        bool hasPositionData{false};
    };

    // -------------------------------------------------------------
    // Game rule types (matches Mojang's RuleType enum).
    // -------------------------------------------------------------
    enum class GameRuleType : uint8_t
    {
        Bool = 0,
        Int = 1
    };

    struct GameRuleData
    {
        std::string name;
        GameRuleType type{GameRuleType::Bool};
        bool boolValue{false};
        int64_t intValue{0};
    };

    // ---------------------------------------------------------------------
    // Multiworld plane inventory policy + forced game mode (0.0.15
    // plane-switch inventory fix). Encoded as world_registry's
    // inventory_policy / force_gamemode INTEGER columns so a single
    // SQLite row describes how each plane treats inventory across
    // /gotor and (future) portal transitions and whether it forces a
    // specific game mode on entry. Defaults SYNC / INHERIT match
    // standard Minecraft behaviour so vanilla 0/-1/1 planes, the
    // reserved Aether special dimension, and pre-0.0.15 custom
    // planes keep working without further edits.
    //
    // Callers: NetworkManager::setPlayerWorldAt (hot path, via
    // WorldManager's cached maps), PlayerManager::savePlayerState
    // (per-plane save mirror), and Database::loadPlayerData (login
    // path) all use these constants so the encoding stays in one
    // place.
    constexpr int32_t kInventoryPolicySync = 0;      // standard Minecraft behaviour
    constexpr int32_t kInventoryPolicySeparate = 1;  // per-plane creative-sandbox blob
    constexpr int32_t kInventoryPolicyRestricted = 2; // per-plane PVP/PVE blob, one-way
    constexpr int32_t kForceGameModeInherit = -1;    // no forced mode on entry
    constexpr int32_t kForceGameModeSurvival = 0;    // matches PlayerSession::gamemode
    constexpr int32_t kForceGameModeCreative = 1;
    constexpr int32_t kForceGameModeAdventure = 2;
    constexpr int32_t kForceGameModeSpectator = 3;

    class Database
    {
    private:
        sqlite3 *m_db{nullptr};
        std::string m_path;

    public:
        explicit Database(std::string path);
        ~Database();

        bool init();

        bool savePlayerData(const PlayerData &data);
        bool loadPlayerData(const std::string &uuid, PlayerData &outData);
        // Patch the world_id column on a player row selected by username. Used
        // by NetworkManager::setPlayerWorldId so a live-plane switch persists
        // across relogins. Returns false when the username has no row.
        bool setPlayerWorldIdByName(const std::string &username, int32_t worldId);

        // ---- Per-plane player coordinates (0.0.14 multiworld fix) ------------
        // Standard Minecraft keeps a *separate* coordinate space per dimension
        // plane. Walking far in the Overworld then transitioning to End must not
        // reappear at the Overworld's (x, z) in the End (which is often void
        // terrain). Coordinates are stored per (uuid, world_id) in the SQLite
        // `player_positions` table so:
        //   * savePlayerData also mirrors the current-plane coords into it
        //     (only when PlayerData::hasPositionData is true);
        //   * loadPlayerData consults it for the plane the row says the player
        //     is on, overriding `last_x/last_y/last_z` when an entry exists;
        //   * NetworkManager::setPlayerWorldAt / setPlayerWorldId save the
        //     leaving plane's coords before the switch and load the destination
        //     plane's coords on arrival (or fall back to that plane's default
        //     spawn), which is exactly the standard Minecraft teleport semantic
        //     the /gotor command already assumes but the previous code did not
        //     implement.
        // Future portal code should call NetworkManager::setPlayerWorldAt with
        // hasExplicitPosition=true so the portal author chooses the arrival
        // point on the destination plane; the same player_positions rows back
        // the standard /gotor path.
        bool savePlayerPosition(const std::string &uuid, int32_t worldId,
                                double x, double y, double z,
                                float yaw, float pitch);
        bool loadPlayerPosition(const std::string &uuid, int32_t worldId,
                                double &x, double &y, double &z,
                                float &yaw, float &pitch) const;
        bool deletePlayerPosition(const std::string &uuid, int32_t worldId);

        // --- Game rules persistence (server_data.db) ---
        bool saveGameRule(const GameRuleData &rule);
        bool loadAllGameRules(std::unordered_map<std::string, GameRuleData> &outRules);

        // --- Container (chest / furnace / brewing-stand etc.) persistence ---
        // Keyed by the container's block location (chunk_x, chunk_z, rel_x, y, rel_z).
        // The data_blob uses the compact container-item format defined in
        // PlayerSession.cpp (varint count + count × (slot:short, id:short,
        // count:byte, damage:short)). Empty containers may be either absent
        // (never written) or present with count=0.
        bool loadContainer(int cx, int cz, int rx, int y, int rz,
                           uint16_t &typeOut, std::vector<uint8_t> &blobOut);
        bool saveContainer(int cx, int cz, int rx, int y, int rz,
                           uint16_t type, const std::vector<uint8_t> &blob);
        bool deleteContainer(int cx, int cz, int rx, int y, int rz);

        // ---- Multiworld registry (server_data.db) ---------------------------
        // Maps custom dimension names to unique auto-allocated IDs. Standard
        // Minecraft dimension IDs use the pre-1.12 vanilla layout: 0 =
        // Overworld, -1 = Nether, 1 = End. NetworkManager::start hard-codes
        // those plus a reserved id 100 for the Minecraft Legends "Aether"
        // special dimension; the custom allocator used by registerWorld
        // starts at 101 so future Legends-adjacent planes ("The Sif", "The
        // Shift") can claim another 100-series id without colliding with
        // server-created custom planes or the fixed vanilla 0/-1/1 slots.
        bool initWorldRegistry();
        bool registerWorld(const std::string &name, const std::string &style,
                           int32_t &outId);
        bool lookupWorld(const std::string &name, int32_t &outId,
                         std::string &outStyle) const;
        bool lookupWorldById(int32_t id, std::string &outName,
                             std::string &outStyle) const;
        // Explicit-ID registration for the fixed standard planes (Overworld=0
        // / Nether=-1 / End=1) and the reserved 100 "Aether" special-dimension
        // row. Idempotent: re-registration of an already-present row is a no-
        // op success. Bypasses the >=101 custom allocator used by registerWorld
        // so NetworkManager::start can seed the standard planes without
        // touching the custom ID range.
        bool setWorldById(int32_t id, const std::string &name,
                          const std::string &style);

        // ---- Multiworld plane inventory policy (world_registry extension) --
        // Two additional INTEGER columns on the world_registry table control
        // how a plane handles inventory across /gotor and (future) portal
        // transitions:
        //
        //   inventory_policy
        //       0 SYNC      (DEFAULT) -- one shared inventory slot persisted
        //                     on the flat players.inventory_blob column.
        //                     Items transfer between SYNC planes (Overworld,
        //                     Nether, End, Aether, mining worlds, ...)
        //                     transparently; this is standard Minecraft
        //                     behaviour.
        //       1 SEPARATE            -- per-plane blob keyed on (uuid,
        //                     planeId) in the new player_inventories table.
        //                     Intended for flat "creative sandbox" planes:
        //                     flat / no-gen terrain, overworld height, and
        //                     almost always paired with force-gamemode=creative
        //                     so the plane author can hand players their own
        //                     loot that never leaks back to the SYNC chain.
        //       2 RESTRICTED          -- same storage as SEPARATE but treated
        //                     as one-way: items gained on the plane stay on
        //                     the plane, and arriving from any other plane
        //                     never inherits the source's inventory (loads
        //                     empty / the plane's own row if the player has
        //                     been training there earlier). PVP / PVE
        //                     training worlds.
        //
        //   force_gamemode
        //       -1 INHERIT  (DEFAULT) -- do not change the player's current
        //                     mode on plane entry.
        //        0 SURVIVAL, 1 CREATIVE, 2 ADVENTURE, 3 SPECTATOR --
        //                     matches PlayerSession::gamemode / the S2C
        //                     JoinGame 0x01 + Respawn 0x07 gamemode byte.
        //                     Forced onto the session when the player
        //                     switches onto this plane (used for
        //                     creative sandbox planes and PVP training
        //                     maps that pin game mode).
        //
        // Concrete values live in the enclosing Storage namespace as
        // `kInventoryPolicy*` / `kForceGameMode*` constexpr ints (declared
        // right above class Database) so callers can write
        // `Storage::kInventoryPolicySeparate` instead of a magic 1.

        // Fresh DBs get both columns via CREATE TABLE; existing 0.0.14
        // server_data.db rows pick them up via ALTER TABLE migrations run
        // inside initWorldRegistry.
        bool setWorldInventoryPolicy(int32_t id, int32_t policy);
        bool setWorldForceGameMode(int32_t id, int32_t forceGm);
        // Reads the plane's inventory_policy column; returns
        // kInventoryPolicySync when the row is missing or unset so legacy
        // planes keep standard Minecraft behaviour without further edits.
        int32_t getWorldInventoryPolicy(int32_t id) const;
        // Reads the plane's force_gamemode column; returns kForceGameModeInherit
        // when unset so vanilla planes keep the player's current mode.
        int32_t getWorldForceGameMode(int32_t id) const;

        // --- Per-plane inventory persistence (player_inventories) ----------
        // New SQLite table holding the SEPARATE / RESTRICTED plane blobs so
        // they don't fight with the flat players.inventory_blob column. SYNC
        // planes never touch this table.
        bool initPlayerInventoriesTable();
        bool savePlayerInventory(const std::string &uuid, int32_t worldId,
                                 const std::vector<uint8_t> &blob);
        bool loadPlayerInventory(const std::string &uuid, int32_t worldId,
                                 std::vector<uint8_t> &blobOut,
                                 bool &hasRowOut) const;
        // Reads ONLY the flat players.inventory_blob column so SEPARATE /
        // RESTRICTED save paths can preserve the SYNC slot without pulling
        // the whole PlayerData through a separate loadPlayerData call.
        bool loadSyncInventoryBlob(const std::string &uuid,
                                   std::vector<uint8_t> &blobOut) const;
    };

} // namespace LightweightMC::Storage