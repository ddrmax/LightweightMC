#include "storage/Database.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::Storage
{

    Database::Database(std::string path) : m_path(std::move(path)) {}

    Database::~Database()
    {
        if (m_db)
            sqlite3_close(m_db);
    }

    bool Database::init()
    {
        if (sqlite3_open(m_path.c_str(), &m_db) != SQLITE_OK)
        {
            Core::Logger::error("Failed to open SQLite database: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        // Enable WAL mode and NORMAL synchronous setting for performance
        sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

        const char *sql = R"(
        CREATE TABLE IF NOT EXISTS books (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_uuid TEXT UNIQUE,
            title TEXT,
            author TEXT,
            pages_json TEXT
        );
        CREATE TABLE IF NOT EXISTS block_entities (
            chunk_x INTEGER, chunk_z INTEGER,
            rel_x INTEGER, y INTEGER, rel_z INTEGER,
            type INTEGER, data_blob BLOB,
            PRIMARY KEY (chunk_x, chunk_z, rel_x, y, rel_z)
        );
        CREATE TABLE IF NOT EXISTS players (
            uuid TEXT PRIMARY KEY,
            username TEXT,
            spawn_x DOUBLE, spawn_y DOUBLE, spawn_z DOUBLE, spawn_yaw FLOAT, spawn_pitch FLOAT,
            last_x DOUBLE, last_y DOUBLE, last_z DOUBLE, last_yaw FLOAT, last_pitch FLOAT,
            inventory_blob BLOB,
            last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS gamerules (
            name TEXT PRIMARY KEY,
            type INTEGER NOT NULL DEFAULT 0,
            bool_value INTEGER NOT NULL DEFAULT 0,
            int_value INTEGER NOT NULL DEFAULT 0
        );
    )";

        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            Core::Logger::error("Failed to execute DB initialization SQL: " + std::string(errMsg));
            sqlite3_free(errMsg);
            return false;
        }

        // -------------------------------------------------------------
        // Idempotent migration for pre-existing databases: add the
        // player.dat-mirrored columns if they are not present yet.
        // New databases already get them via the CREATE TABLE above only
        // when listed there; both paths converge here (ALTER is skipped
        // per-column when the column already exists).
        // -------------------------------------------------------------
        const char *newColumns[] = {
            "health FLOAT DEFAULT 20.0",
            "food_level INTEGER DEFAULT 20",
            "food_saturation FLOAT DEFAULT 5.0",
            "food_exhaustion FLOAT DEFAULT 0.0",
            "gamemode INTEGER DEFAULT 1",
            "has_spawn INTEGER DEFAULT 0",
            "bed_x INTEGER DEFAULT -1",
            "bed_y INTEGER DEFAULT -1",
            "bed_z INTEGER DEFAULT -1",
            "fall_distance FLOAT DEFAULT 0.0",
            "air INTEGER DEFAULT 30",
            "on_fire INTEGER DEFAULT 0",
            "fire INTEGER DEFAULT 0",
            "sleeping INTEGER DEFAULT 0",
            "sleep_timer INTEGER DEFAULT 0",
            "flying INTEGER DEFAULT 0",
            "sprinting INTEGER DEFAULT 0",
            "xp_level INTEGER DEFAULT 0",
            "xp_progress FLOAT DEFAULT 0.0",
            "xp_total INTEGER DEFAULT 0",
            "death_count INTEGER DEFAULT 0",
            "op_level INTEGER DEFAULT 0"
        };

        for (const char *col : newColumns)
        {
            // Column name is the token before the first space.
            std::string colName(col);
            size_t sp = colName.find(' ');
            if (sp != std::string::npos)
                colName.resize(sp);

            std::string checkSql = "SELECT COUNT(*) FROM pragma_table_info('players') WHERE name = '" + colName + "';";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(m_db, checkSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
            {
                int exists = 0;
                if (sqlite3_step(stmt) == SQLITE_ROW)
                    exists = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);

                if (!exists)
                {
                    std::string alterSql = "ALTER TABLE players ADD COLUMN " + std::string(col) + ";";
                    char *alterErr = nullptr;
                    if (sqlite3_exec(m_db, alterSql.c_str(), nullptr, nullptr, &alterErr) != SQLITE_OK)
                    {
                        Core::Logger::error("Failed to migrate players table column " + colName + ": " + std::string(alterErr));
                        sqlite3_free(alterErr);
                        return false;
                    }
                }
            }
        }

        return true;
    }

    bool Database::savePlayerData(const PlayerData &data)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            INSERT OR REPLACE INTO players 
            (uuid, username, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch, last_x, last_y, last_z, last_yaw, last_pitch, inventory_blob, last_seen,
             health, food_level, food_saturation, food_exhaustion, gamemode, has_spawn, bed_x, bed_y, bed_z,
             fall_distance, air, on_fire, fire, sleeping, sleep_timer, flying, sprinting,
             xp_level, xp_progress, xp_total, death_count, op_level)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP,
                    ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare savePlayerData SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_text(stmt, 1, data.uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, data.username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, data.spawnX);
        sqlite3_bind_double(stmt, 4, data.spawnY);
        sqlite3_bind_double(stmt, 5, data.spawnZ);
        sqlite3_bind_double(stmt, 6, data.spawnYaw);
        sqlite3_bind_double(stmt, 7, data.spawnPitch);
        sqlite3_bind_double(stmt, 8, data.lastX);
        sqlite3_bind_double(stmt, 9, data.lastY);
        sqlite3_bind_double(stmt, 10, data.lastZ);
        sqlite3_bind_double(stmt, 11, data.lastYaw);
        sqlite3_bind_double(stmt, 12, data.lastPitch);

        if (!data.inventoryData.empty())
        {
            sqlite3_bind_blob(stmt, 13, data.inventoryData.data(), static_cast<int>(data.inventoryData.size()), SQLITE_TRANSIENT);
        }
        else
        {
            sqlite3_bind_null(stmt, 13);
        }

        // player.dat-mirrored columns (14..35)
        sqlite3_bind_double(stmt, 14, data.health);
        sqlite3_bind_int(stmt, 15, data.foodLevel);
        sqlite3_bind_double(stmt, 16, data.foodSaturationLevel);
        sqlite3_bind_double(stmt, 17, data.foodExhaustionLevel);
        sqlite3_bind_int(stmt, 18, data.gamemode);
        sqlite3_bind_int(stmt, 19, data.hasSpawn ? 1 : 0);
        sqlite3_bind_int64(stmt, 20, data.bedX);
        sqlite3_bind_int64(stmt, 21, data.bedY);
        sqlite3_bind_int64(stmt, 22, data.bedZ);
        sqlite3_bind_double(stmt, 23, data.fallDistance);
        sqlite3_bind_int(stmt, 24, data.air);
        sqlite3_bind_int(stmt, 25, data.onFire ? 1 : 0);
        sqlite3_bind_int(stmt, 26, data.fire);
        sqlite3_bind_int(stmt, 27, data.sleeping ? 1 : 0);
        sqlite3_bind_int(stmt, 28, data.sleepTimer);
        sqlite3_bind_int(stmt, 29, data.flying ? 1 : 0);
        sqlite3_bind_int(stmt, 30, data.sprinting ? 1 : 0);
        sqlite3_bind_int(stmt, 31, data.experienceLevel);
        sqlite3_bind_double(stmt, 32, data.experienceProgress);
        sqlite3_bind_int64(stmt, 33, data.totalExperience);
        sqlite3_bind_int(stmt, 34, data.deathCount);
        sqlite3_bind_int(stmt, 35, data.opLevel);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!success)
        {
            Core::Logger::error("Failed to execute savePlayerData for " + data.username + ": " + std::string(sqlite3_errmsg(m_db)));
        }
        sqlite3_finalize(stmt);
        return success;
    }

    bool Database::loadPlayerData(const std::string &uuid, PlayerData &outData)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            SELECT username, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch, 
                   last_x, last_y, last_z, last_yaw, last_pitch, inventory_blob,
                   health, food_level, food_saturation, food_exhaustion, gamemode, has_spawn, bed_x, bed_y, bed_z,
                   fall_distance, air, on_fire, fire, sleeping, sleep_timer, flying, sprinting,
                   xp_level, xp_progress, xp_total, death_count, op_level
            FROM players WHERE uuid = ?;
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare loadPlayerData SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            found = true;
            outData.uuid = uuid;
            outData.username = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            outData.spawnX = sqlite3_column_double(stmt, 1);
            outData.spawnY = sqlite3_column_double(stmt, 2);
            outData.spawnZ = sqlite3_column_double(stmt, 3);
            outData.spawnYaw = static_cast<float>(sqlite3_column_double(stmt, 4));
            outData.spawnPitch = static_cast<float>(sqlite3_column_double(stmt, 5));
            outData.lastX = sqlite3_column_double(stmt, 6);
            outData.lastY = sqlite3_column_double(stmt, 7);
            outData.lastZ = sqlite3_column_double(stmt, 8);
            outData.lastYaw = static_cast<float>(sqlite3_column_double(stmt, 9));
            outData.lastPitch = static_cast<float>(sqlite3_column_double(stmt, 10));

            const void *blobData = sqlite3_column_blob(stmt, 11);
            int blobSize = sqlite3_column_bytes(stmt, 11);
            if (blobData && blobSize > 0)
            {
                const uint8_t *byteData = static_cast<const uint8_t *>(blobData);
                outData.inventoryData.assign(byteData, byteData + blobSize);
            }

            // player.dat-mirrored columns (12..33). These are nullable on rows
            // written before the schema migration; NULL falls back to defaults.
            auto dbl = [](sqlite3_stmt *s, int i) -> double { return sqlite3_column_type(s, i) == SQLITE_NULL ? 0.0 : sqlite3_column_double(s, i); };
            auto num = [](sqlite3_stmt *s, int i) -> int64_t { return sqlite3_column_type(s, i) == SQLITE_NULL ? 0 : sqlite3_column_int64(s, i); };

            outData.health = static_cast<float>(dbl(stmt, 12));
            outData.foodLevel = static_cast<int8_t>(num(stmt, 13));
            outData.foodSaturationLevel = static_cast<float>(dbl(stmt, 14));
            outData.foodExhaustionLevel = static_cast<float>(dbl(stmt, 15));
            outData.gamemode = static_cast<uint8_t>(num(stmt, 16));
            outData.hasSpawn = num(stmt, 17) != 0;
            outData.bedX = static_cast<int32_t>(num(stmt, 18));
            outData.bedY = static_cast<int32_t>(num(stmt, 19));
            outData.bedZ = static_cast<int32_t>(num(stmt, 20));
            outData.fallDistance = static_cast<float>(dbl(stmt, 21));
            outData.air = static_cast<int32_t>(num(stmt, 22));
            outData.onFire = num(stmt, 23) != 0;
            outData.fire = static_cast<int16_t>(num(stmt, 24));
            outData.sleeping = num(stmt, 25) != 0;
            outData.sleepTimer = static_cast<uint8_t>(num(stmt, 26));
            outData.flying = num(stmt, 27) != 0;
            outData.sprinting = num(stmt, 28) != 0;
            outData.experienceLevel = static_cast<int16_t>(num(stmt, 29));
            outData.experienceProgress = static_cast<float>(dbl(stmt, 30));
            outData.totalExperience = static_cast<int32_t>(num(stmt, 31));
            outData.deathCount = static_cast<uint8_t>(num(stmt, 32));
            outData.opLevel = static_cast<uint8_t>(num(stmt, 33));

            // Bed spawn is "not present" unless flagged and coordinates are valid.
            outData.hasBedSpawn = outData.hasSpawn && outData.bedX >= 0 && outData.bedY >= 0 && outData.bedZ >= 0;
        }

        sqlite3_finalize(stmt);
        return found;
    }


    bool Database::saveGameRule(const GameRuleData &rule)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            INSERT OR REPLACE INTO gamerules (name, type, bool_value, int_value)
            VALUES (?, ?, ?, ?);
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare saveGameRule SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_text(stmt, 1, rule.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(rule.type));
        sqlite3_bind_int(stmt, 3, rule.boolValue ? 1 : 0);
        sqlite3_bind_int64(stmt, 4, rule.intValue);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!success)
        {
            Core::Logger::error("Failed to save gamerule " + rule.name + ": " + std::string(sqlite3_errmsg(m_db)));
        }
        sqlite3_finalize(stmt);
        return success;
    }

    bool Database::loadAllGameRules(std::unordered_map<std::string, GameRuleData> &outRules)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            SELECT name, type, bool_value, int_value FROM gamerules;
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare loadAllGameRules SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            GameRuleData rule;
            const char *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            if (name)
                rule.name = name;
            rule.type = static_cast<GameRuleType>(sqlite3_column_int(stmt, 1));
            rule.boolValue = sqlite3_column_int(stmt, 2) != 0;
            rule.intValue = sqlite3_column_int64(stmt, 3);

            outRules[rule.name] = std::move(rule);
        }

        sqlite3_finalize(stmt);
        return true;
    }

} // namespace LightweightMC::Storage