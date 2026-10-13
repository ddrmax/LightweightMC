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
    };

} // namespace LightweightMC::Storage