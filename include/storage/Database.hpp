#pragma once
#include <string>
#include <vector>
#include <cstdint>
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
    };

} // namespace LightweightMC::Storage