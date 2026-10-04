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

        // --- Game rules persistence (server_data.db) ---
        bool saveGameRule(const GameRuleData &rule);
        bool loadAllGameRules(std::unordered_map<std::string, GameRuleData> &outRules);
    };

} // namespace LightweightMC::Storage