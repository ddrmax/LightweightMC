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
            last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            world_id INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS gamerules (
            name TEXT PRIMARY KEY,
            type INTEGER NOT NULL DEFAULT 0,
            bool_value INTEGER NOT NULL DEFAULT 0,
            int_value INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS player_positions (
            uuid TEXT NOT NULL,
            world_id INTEGER NOT NULL,
            x DOUBLE, y DOUBLE, z DOUBLE,
            yaw FLOAT, pitch FLOAT,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (uuid, world_id)
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
            "op_level INTEGER DEFAULT 0",
            "world_id INTEGER DEFAULT 0"
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
             xp_level, xp_progress, xp_total, death_count, op_level, world_id)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP,
                    ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
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
        // Plane the session is currently on. Persisted on every save so the
        // last-known-plane survives a relogin even without
        // Database::setPlayerWorldIdByName (used by the live /gotor switch).
        sqlite3_bind_int(stmt, 36, data.worldId);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!success)
        {
            Core::Logger::error("Failed to execute savePlayerData for " + data.username + ": " + std::string(sqlite3_errmsg(m_db)));
        }
        sqlite3_finalize(stmt);

        // Standard Minecraft keeps a separate coordinate space per plane. In
        // addition to the flat last_x/last_y/last_z fields on the players row
        // (which describe the plane named by world_id), mirror those
        // coordinates into the per-plane `player_positions` table so a later
        // /gotor or portal hop can find the player's last position on any
        // plane they visited -- not just the last one they were on. Skip the
        // mirror when the caller built a partial PlayerData (op-level or game
        // mode updates construct a fresh PlayerData whose last_x/y/z are
        // still default 0.5/7.0/0.5, which must not overwrite the real
        // per-plane coordinates we already have).
        if (success && data.hasPositionData && !data.uuid.empty())
        {
            if (!savePlayerPosition(data.uuid, data.worldId, data.lastX, data.lastY, data.lastZ,
                                    data.lastYaw, data.lastPitch))
            {
                Core::Logger::warn("savePlayerData: per-plane mirror (player_positions) for " +
                                   data.username + " failed -- login restore will fall back to "
                                   "the flat last_x/y/z columns on the players row");
            }
        }

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
                   xp_level, xp_progress, xp_total, death_count, op_level, world_id
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

            // Plane the last session was on (column 34). NULL on rows written
            // before the schema migration -- default 0 (Overworld) is what the
            // login handler expects.
            outData.worldId = static_cast<int32_t>(num(stmt, 34));

            // Bed spawn is "not present" unless flagged and coordinates are valid.
            outData.hasBedSpawn = outData.hasSpawn && outData.bedX >= 0 && outData.bedY >= 0 && outData.bedZ >= 0;
        }

        sqlite3_finalize(stmt);

        // Standard Minecraft maintains per-plane player coordinates. Once the
        // flat player row is loaded, consult the per-plane mirror (player_posi-
        // tions) for the plane the row says the player is on. If the mirror has
        // a fresher or authoritative entry (a legacy row where the flat last_x
        // /last_y/last_z fields were clobbered by a partial savePlayerData
        // from setPlayerOpLevel / setPlayerGameMode -- or an entry recorded by
        // a setPlayerWorldId plane switch that never went through savePlayerData
        // before disconnecting), override the flat fields with the per-plane
        // values so login lands the player at their last visited position on
        // that specific plane rather than the position they had when the last
        // full save happened. When no per-plane entry exists, fall back to the
        // flat columns and let NetworkManager::setPlayerWorldAt's spawn-fallback
        // logic (called on subsequent /gotor) handle a first-visit on that plane.
        if (found)
        {
            double px = 0.0, py = 0.0, pz = 0.0;
            float pyaw = 0.0f, ppitch = 0.0f;
            if (loadPlayerPosition(uuid, outData.worldId, px, py, pz, pyaw, ppitch))
            {
                outData.lastX = px;
                outData.lastY = py;
                outData.lastZ = pz;
                outData.lastYaw = pyaw;
                outData.lastPitch = ppitch;
                outData.hasPositionData = true;
            }
        }

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

    // ------------------------------------------------------------------
    // Container persistence (block_entities table, keyed by block coord).
    // ------------------------------------------------------------------
    bool Database::loadContainer(int cx, int cz, int rx, int y, int rz,
                                 uint16_t &typeOut, std::vector<uint8_t> &blobOut)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            SELECT type, data_blob FROM block_entities
            WHERE chunk_x = ? AND chunk_z = ? AND rel_x = ? AND y = ? AND rel_z = ?;
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare loadContainer SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, cx);
        sqlite3_bind_int(stmt, 2, cz);
        sqlite3_bind_int(stmt, 3, rx);
        sqlite3_bind_int(stmt, 4, y);
        sqlite3_bind_int(stmt, 5, rz);

        bool found = false;
        typeOut = 0;
        blobOut.clear();
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            typeOut = static_cast<uint16_t>(sqlite3_column_int(stmt, 0));
            const void *raw = sqlite3_column_blob(stmt, 1);
            if (raw)
            {
                const size_t len = sqlite3_column_bytes(stmt, 1);
                const uint8_t *bytes = reinterpret_cast<const uint8_t *>(raw);
                blobOut.assign(bytes, bytes + len);
            }
            found = true;
        }

        sqlite3_finalize(stmt);
        return found;
    }

    bool Database::saveContainer(int cx, int cz, int rx, int y, int rz,
                                 uint16_t type, const std::vector<uint8_t> &blob)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            INSERT OR REPLACE INTO block_entities
            (chunk_x, chunk_z, rel_x, y, rel_z, type, data_blob)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare saveContainer SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, cx);
        sqlite3_bind_int(stmt, 2, cz);
        sqlite3_bind_int(stmt, 3, rx);
        sqlite3_bind_int(stmt, 4, y);
        sqlite3_bind_int(stmt, 5, rz);
        sqlite3_bind_int(stmt, 6, static_cast<int>(type));
        sqlite3_bind_blob(stmt, 7, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            Core::Logger::error("Failed to save container at (" + std::to_string(cx) + ", " +
                                std::to_string(cz) + ", " + std::to_string(rx) + ", " +
                                std::to_string(y) + ", " + std::to_string(rz) + ")");
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        return true;
    }

    bool Database::deleteContainer(int cx, int cz, int rx, int y, int rz)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            DELETE FROM block_entities
            WHERE chunk_x = ? AND chunk_z = ? AND rel_x = ? AND y = ? AND rel_z = ?;
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare deleteContainer SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, cx);
        sqlite3_bind_int(stmt, 2, cz);
        sqlite3_bind_int(stmt, 3, rx);
        sqlite3_bind_int(stmt, 4, y);
        sqlite3_bind_int(stmt, 5, rz);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    // ---------------------------------------------------------------------
    // World registry (multiworld / dimension planes).
    //
    // Table layout:
    //   id INTEGER UNIQUE  -- 0=Overworld, 1=Nether, 2=End are hard-coded by
    //                        NetworkManager::start; 3..99 reserved for
    //                        future vanilla-style planes ("The Sif", "The
    //                        Shift"); 100+ auto-allocated here for custom
    //                        worlds created via /world create.
    //   name TEXT UNIQUE COLLATE NOCASE
    //   style TEXT          -- GenStyle string (overworld/superflat/etc.)
    //
    // Uniqueness is guaranteed by the UNIQUE column constraint plus the
    // monotonic allocator (max id >= 100) + 1.
    // ---------------------------------------------------------------------
    bool Database::initWorldRegistry()
    {
        if (!m_db)
            return false;
        const char *sql = R"(
            CREATE TABLE IF NOT EXISTS world_registry (
                id INTEGER UNIQUE,
                name TEXT UNIQUE COLLATE NOCASE,
                style TEXT
            );
        )";
        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            Core::Logger::error("Failed to initialise world_registry: " +
                                std::string(errMsg ? errMsg : "unknown"));
            if (errMsg) sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    bool Database::lookupWorld(const std::string &name,
                               int32_t &outId, std::string &outStyle) const
    {
        if (!m_db)
            return false;
        const char *sql = R"(
            SELECT id, style FROM world_registry WHERE name = ? COLLATE NOCASE;
        )";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare lookupWorld: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            outId = sqlite3_column_int(stmt, 0);
            const unsigned char *s = sqlite3_column_text(stmt, 1);
            outStyle = s ? std::string(reinterpret_cast<const char *>(s)) : std::string();
            found = true;
        }
        sqlite3_finalize(stmt);
        return found;
    }

    bool Database::lookupWorldById(int32_t id, std::string &outName,
                                   std::string &outStyle) const
    {
        if (!m_db)
            return false;
        const char *sql = R"(
            SELECT name, style FROM world_registry WHERE id = ?;
        )";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare lookupWorldById: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_int(stmt, 1, id);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char *n = sqlite3_column_text(stmt, 0);
            const unsigned char *s = sqlite3_column_text(stmt, 1);
            outName = n ? std::string(reinterpret_cast<const char *>(n)) : std::string();
            outStyle = s ? std::string(reinterpret_cast<const char *>(s)) : std::string();
            found = true;
        }
        sqlite3_finalize(stmt);
        return found;
    }

    // ---------------------------------------------------------------------
    // Allocate a new custom world ID starting at 101. Never returns an ID in
    // the pre-1.12 vanilla range (0, -1, 1) nor on the reserved 100 "Aether"
    // special-dimension row, so a Minecraft Legends-style plane (Aether, or a
    // future "The Sif"/"The Shift" plane that later claims another 100-series
    // id) cannot collide with a server-created custom plane.
    // ---------------------------------------------------------------------
    bool Database::registerWorld(const std::string &name, const std::string &style,
                                 int32_t &outId)
    {
        if (!m_db || name.empty())
            return false;

        // Idempotent: if the world is already registered, return its ID.
        int32_t existingId = 0;
        std::string existingStyle;
        if (lookupWorld(name, existingId, existingStyle))
        {
            outId = existingId;
            return true;
        }

        constexpr int32_t kFirstCustomWorldId = 101;

        // Find the largest custom ID we've already allocated (or fall back).
        const char *maxSql =
            "SELECT MAX(id) FROM world_registry;";
        sqlite3_stmt *stmt = nullptr;
        int32_t nextId = kFirstCustomWorldId;
        if (sqlite3_prepare_v2(m_db, maxSql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
            {
                const int32_t maxExisting = sqlite3_column_int(stmt, 0);
                if (maxExisting >= kFirstCustomWorldId)
                    nextId = maxExisting + 1;
            }
            sqlite3_finalize(stmt);
        }
        else
        {
            Core::Logger::error("Failed to prepare registerWorld-max: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        const char *sqlIns =
            "INSERT INTO world_registry (id, name, style) VALUES (?, ?, ?);";
        if (sqlite3_prepare_v2(m_db, sqlIns, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare registerWorld-insert: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_int(stmt, 1, nextId);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, style.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (!ok)
        {
            Core::Logger::error("Failed to register world \"" + name + "\": " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        outId = nextId;
        return true;
    }

    // Explicit-ID registration used by NetworkManager::start to seed the four
    // standard Minecraft dimension planes at their pre-1.12 vanilla IDs
    // (Overworld=0 / Nether=-1 / End=1) plus the reserved id 100 "Aether"
    // special-dimension row. Idempotent.
    bool Database::setWorldById(int32_t id, const std::string &name,
                                const std::string &style)
    {
        if (!m_db || name.empty())
            return false;

        // Idempotent: if a row for (id, name) already exists, we're done.
        int32_t existingId = -1;
        std::string existingStyle;
        if (lookupWorld(name, existingId, existingStyle))
            return existingId == id;

        const char *sql =
            "INSERT INTO world_registry (id, name, style) VALUES (?, ?, ?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare setWorldById: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, style.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    // Update the world_id column on the player row matching the given username.
    // Kept as a name-based lookup (rather than uuid-based) because NetworkManager
    // only tracks live sessions by username on the console side and we do not
    // want to thread PlayerData / UUID resolution through that call.
    bool Database::setPlayerWorldIdByName(const std::string &username, int32_t worldId)
    {
        if (!m_db || username.empty())
            return false;

        const char *updateSql =
            "UPDATE players SET world_id = ? WHERE username = ? COLLATE NOCASE;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, updateSql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare setPlayerWorldIdByName: " +
                                std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_int(stmt, 1, worldId);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    // ---- Per-plane coordinate storage (0.0.14 fix) ---------------------------
    // See Database.hpp for the semantics: one row per (uuid, world_id). `save-
    // PlayerPosition` inserts-or-replaces so subsequent saves on the same plane
    // overwrite the last-known coordinate; `loadPlayerPosition` returns false
    // when the plane has never been visited (so callers can distinguish "not
    // visited yet" from "visited and standing at 0,0,0"). `deletePlayerPosition`
    // is provided for future portal / plane-teardown cleanup.
    bool Database::savePlayerPosition(const std::string &uuid, int32_t worldId,
                                       double x, double y, double z,
                                       float yaw, float pitch)
    {
        if (!m_db || uuid.empty())
            return false;

        const char *sql =
            "INSERT OR REPLACE INTO player_positions "
            "(uuid, world_id, x, y, z, yaw, pitch, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare savePlayerPosition: " +
                                std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, worldId);
        sqlite3_bind_double(stmt, 3, x);
        sqlite3_bind_double(stmt, 4, y);
        sqlite3_bind_double(stmt, 5, z);
        sqlite3_bind_double(stmt, 6, yaw);
        sqlite3_bind_double(stmt, 7, pitch);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok)
        {
            Core::Logger::error("Failed to execute savePlayerPosition for " +
                                uuid + " on plane " + std::to_string(worldId) +
                                ": " + std::string(sqlite3_errmsg(m_db)));
        }
        sqlite3_finalize(stmt);
        return ok;
    }

    bool Database::loadPlayerPosition(const std::string &uuid, int32_t worldId,
                                       double &x, double &y, double &z,
                                       float &yaw, float &pitch) const
    {
        if (!m_db || uuid.empty())
            return false;

        const char *sql =
            "SELECT x, y, z, yaw, pitch FROM player_positions "
            "WHERE uuid = ? AND world_id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare loadPlayerPosition: " +
                                std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, worldId);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            found = true;
            x = sqlite3_column_double(stmt, 0);
            y = sqlite3_column_double(stmt, 1);
            z = sqlite3_column_double(stmt, 2);
            yaw = static_cast<float>(sqlite3_column_double(stmt, 3));
            pitch = static_cast<float>(sqlite3_column_double(stmt, 4));
        }
        sqlite3_finalize(stmt);
        return found;
    }

    bool Database::deletePlayerPosition(const std::string &uuid, int32_t worldId)
    {
        if (!m_db || uuid.empty())
            return false;

        const char *sql =
            "DELETE FROM player_positions WHERE uuid = ? AND world_id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare deletePlayerPosition: " +
                                std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, worldId);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

} // namespace LightweightMC::Storage