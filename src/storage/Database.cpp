#include "storage/Database.hpp"
#include "core/Logger.hpp"
#include "storage/RegionFile.hpp"
#include <ctime>
#include <cstring>
#include <fstream>
#include <sstream>

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
        -- 0.0.15 (phase 9): world-processing state kept as SPARSE rows on
        -- top of the chunk rows rather than as a full in-RAM voxel copy of
        -- every loaded chunk (the ChunkSection[16] holder in world/Chunk.hpp
        -- would cost ~131 KB per chunk and would duplicate -- i.e. risk
        -- drifting from -- the SQLite authority the phase 8b codec path
        -- established):
        --   * tick_states: one row per tick-capable block (furnace,
        --     brewing stand, crop, water/lava source, fire, dispenser/hopper
        --     link, torch, ...) carrying its progress state, an aux counter
        --     and the next processing timestamp (ms). Rows exist ONLY for
        --     blocks that are actually progressing, so a populated plane
        --     pays per active process rather than per voxel.
        --   * chunk_ticks: last processed world-tick timestamp per chunk so
        --     a chunk that was outside the player's view catches up on
        --     ovens/crops/etc. on the next tick without replaying ticks it
        --     already resolved (Chunk::catchup() semantics).
        --   * ground_items: dropped items (chest/container drops, mob loot,
        --     broken-block fragments) keyed by the chunk holding them with an
        --     absolute despawn deadline, i.e. the phase 5 drop ledger
        --     persists instead of living in RAM for the 300 s lifetime only.
        CREATE TABLE IF NOT EXISTS tick_states (
            world_id INTEGER, chunk_x INTEGER, chunk_z INTEGER,
            rel_x INTEGER, y INTEGER, rel_z INTEGER,
            tick_kind INTEGER, state INTEGER, aux INTEGER,
            next_tick_ms INTEGER,
            PRIMARY KEY (world_id, chunk_x, chunk_z, rel_x, y, rel_z)
        );
        CREATE TABLE IF NOT EXISTS chunk_ticks (
            world_id INTEGER, chunk_x INTEGER, chunk_z INTEGER,
            last_tick_ms INTEGER,
            PRIMARY KEY (world_id, chunk_x, chunk_z)
        );
        CREATE TABLE IF NOT EXISTS ground_items (
            world_id INTEGER, chunk_x INTEGER, chunk_z INTEGER,
            rel_x INTEGER, y INTEGER, rel_z INTEGER,
            item_id INTEGER, item_count INTEGER, damage INTEGER,
            spawn_ms INTEGER, lifetime_ms INTEGER,
            PRIMARY KEY (world_id, chunk_x, chunk_z, rel_x, y, rel_z)
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

        // Plane-scoped inventory: SEPARATE / RESTRICTED planes keep their
        // own blob on the player_inventories(uuid, world_id) row rather
        // than on the flat players.inventory_blob column. The SELECT above
        // always filled outData.inventoryData with the flat SYNC blob; if
        // the world_registry row for the plane the player is currently on
        // declares a non-SYNC policy, replace the blob with the plane row
        // so PlayerManager's login handler restores the plane-scoped
        // inventory instead of the shared SYNC chain. Missing plane row
        // (first visit) means empty inventory -- that is exactly the
        // intended behaviour for a fresh creative sandbox or PVP training
        // map: the player starts with nothing on that plane.
        if (found)
        {
            const int32_t planePolicy = getWorldInventoryPolicy(outData.worldId);
            if (planePolicy == kInventoryPolicySeparate || planePolicy == kInventoryPolicyRestricted)
            {
                std::vector<uint8_t> planeBlob;
                bool hasRow = false;
                loadPlayerInventory(uuid, outData.worldId, planeBlob, hasRow);
                outData.inventoryData.swap(planeBlob);
                // (hasRow is intentionally ignored: an empty blob restores
                // an empty inventory, which is the desired first-visit
                // state for SEPARATE / RESTRICTED planes.)
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
                style TEXT,
                inventory_policy INTEGER DEFAULT 0,
                force_gamemode INTEGER DEFAULT -1,
                spawn_x DOUBLE DEFAULT 0.5,
                spawn_y DOUBLE DEFAULT 64.0,
                spawn_z DOUBLE DEFAULT 0.5,
                world_seed INTEGER DEFAULT 0
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
        // Legacy 0.0.14 / 0.0.15 server_data.db rows keep the earlier CREATE
        // signature so the new columns (inventory_policy / force_gamemode
        // for 0.0.15 and spawn_x / spawn_y / spawn_z / world_seed for
        // 0.0.16) are added via ALTER TABLE migrations (same pragma_table_info
        // probe + best-effort ALTER pattern used by Database::initPlayers for
        // the flat player rows). Fresh CREATE already carries all of them,
        // so the ALTER only runs on legacy DBs and is otherwise a no-op.
        const char *newColumns[] = {
            "inventory_policy INTEGER DEFAULT 0",
            "force_gamemode INTEGER DEFAULT -1",
            "spawn_x DOUBLE DEFAULT 0.5",
            "spawn_y DOUBLE DEFAULT 64.0",
            "spawn_z DOUBLE DEFAULT 0.5",
            "world_seed INTEGER DEFAULT 0",
        };
        for (const char *col : newColumns)
        {
            std::string colName(col);
            const size_t sp = colName.find(' ');
            if (sp != std::string::npos)
                colName.resize(sp);
            std::string checkSql = "SELECT COUNT(*) FROM pragma_table_info('world_registry') WHERE name = '" + colName + "';";
            sqlite3_stmt *probe = nullptr;
            if (sqlite3_prepare_v2(m_db, checkSql.c_str(), -1, &probe, nullptr) == SQLITE_OK)
            {
                int exists = 0;
                if (sqlite3_step(probe) == SQLITE_ROW)
                    exists = sqlite3_column_int(probe, 0);
                sqlite3_finalize(probe);
                if (!exists)
                {
                    std::string alterSql = std::string("ALTER TABLE world_registry ADD COLUMN ") + col + ";";
                    char *alterErr = nullptr;
                    if (sqlite3_exec(m_db, alterSql.c_str(), nullptr, nullptr, &alterErr) != SQLITE_OK)
                    {
                        Core::Logger::error("Failed to migrate world_registry column " + colName +
                                            ": " + std::string(alterErr ? alterErr : "unknown"));
                        if (alterErr) sqlite3_free(alterErr);
                        return false;
                    }
                }
            }
            else
            {
                sqlite3_finalize(probe);
                Core::Logger::error("Failed to probe world_registry column " + colName +
                                    ": " + std::string(sqlite3_errmsg(m_db)));
                return false;
            }
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

    // ---------------------------------------------------------------------
    // Multiworld plane inventory policy -- world_registry extension.
    //
    // Two new columns (inventory_policy, force_gamemode) let a plane pick
    // how it handles inventory across /gotor and (future) portal
    // transitions and, independently, whether to force a game mode on
    // entry. Defaults are 0 SYNC / -1 INHERIT so vanilla planes and
    // planes registered before 0.0.15 keep standard Minecraft behaviour
    // without further edits. See Database.hpp's kInventoryPolicy* and
    // kForceGameMode* constants for the encoding.
    // ---------------------------------------------------------------------
    bool Database::setWorldInventoryPolicy(int32_t id, int32_t policy)
    {
        if (!m_db)
            return false;
        if (policy < kInventoryPolicySync || policy > kInventoryPolicyRestricted)
        {
            Core::Logger::warn("setWorldInventoryPolicy: policy=" + std::to_string(policy) +
                               " out of range [0..2]; clamping to SYNC so legacy planes keep standard behaviour");
            policy = kInventoryPolicySync;
        }
        const char *sql = "UPDATE world_registry SET inventory_policy = ? WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare setWorldInventoryPolicy: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_int(stmt, 1, policy);
        sqlite3_bind_int(stmt, 2, id);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    bool Database::setWorldForceGameMode(int32_t id, int32_t forceGm)
    {
        if (!m_db)
            return false;
        if (forceGm != kForceGameModeInherit &&
            (forceGm < kForceGameModeSurvival || forceGm > kForceGameModeSpectator))
        {
            Core::Logger::warn("setWorldForceGameMode: forceGm=" + std::to_string(forceGm) +
                               " invalid; falling back to INHERIT (-1)");
            forceGm = kForceGameModeInherit;
        }
        const char *sql = "UPDATE world_registry SET force_gamemode = ? WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare setWorldForceGameMode: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_int(stmt, 1, forceGm);
        sqlite3_bind_int(stmt, 2, id);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    int32_t Database::getWorldInventoryPolicy(int32_t id) const
    {
        if (!m_db)
            return kInventoryPolicySync;
        const char *sql = "SELECT inventory_policy FROM world_registry WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return kInventoryPolicySync;
        sqlite3_bind_int(stmt, 1, id);
        int32_t out = kInventoryPolicySync;
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
            out = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return out;
    }

    int32_t Database::getWorldForceGameMode(int32_t id) const
    {
        if (!m_db)
            return kForceGameModeInherit;
        const char *sql = "SELECT force_gamemode FROM world_registry WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return kForceGameModeInherit;
        sqlite3_bind_int(stmt, 1, id);
        int32_t out = kForceGameModeInherit;
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
            out = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return out;
    }

    // ---------------------------------------------------------------------
    // Per-plane world spawn + world seed (0.0.16 world-control commands).
    //
    //   /setworldspawn <x> <y> <z> writes this triple to the plane's
    //   world_registry row (spawn_x / spawn_y / spawn_z DOUBLE columns)
    //   and mirrors it in WorldManager::m_worldSpawn so the login path
    //   (PlayerManager::sendPlayPackets falling back to plane default
    //   spawn, NetworkManager::setPlayerWorldAt's "leaving-plane
    //   coordinates not stored" branch, and future portal authors)
    //   can consult the cache without a fresh SQLite lookup.
    //
    //   /seed <plane> reads the world_seed INTEGER column; when the
    //   plane's WorldStorage runtime seed differs (a server boot from
    //   server.properties' `[world] world-seed` may have seeded the
    //   in-memory WorldStorage before the SQLite column was created),
    //   NetworkManager::getWorldSeedByName prefers the WorldStorage
    //   value so the command reports the seed that was actually used
    //   for terrain generation. /seed <plane> <value> writes both the
    //   SQLite column and WorldStorage::setWorldSeed so subsequent
    //   chunk regenerations on that plane use the new value.
    //
    //   /spawnpoint writes only the player row's spawn_x/y/z columns
    //   (see updatePlayerSpawn); the update bypasses savePlayerData's
    //   INSERT-OR-REPLACE so it never clobbers inventory, coordinates
    //   or the world_id column.
    // ---------------------------------------------------------------------
    bool Database::setWorldSpawn(int32_t id, double x, double y, double z)
    {
        if (!m_db)
            return false;
        const char *sql =
            "UPDATE world_registry SET spawn_x = ?, spawn_y = ?, spawn_z = ? WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare setWorldSpawn: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_double(stmt, 1, x);
        sqlite3_bind_double(stmt, 2, y);
        sqlite3_bind_double(stmt, 3, z);
        sqlite3_bind_int(stmt, 4, id);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (!ok)
        {
            Core::Logger::warn("setWorldSpawn: no world_registry row for plane id " + std::to_string(id));
        }
        return ok;
    }

    bool Database::getWorldSpawn(int32_t id, double &outX, double &outY, double &outZ) const
    {
        outX = kDefaultWorldSpawnX;
        outY = kDefaultWorldSpawnY;
        outZ = kDefaultWorldSpawnZ;
        if (!m_db)
            return false;
        const char *sql = "SELECT spawn_x, spawn_y, spawn_z FROM world_registry WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(stmt, 1, id);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            found = true;
            if (sqlite3_column_type(stmt, 0) != SQLITE_NULL)
                outX = sqlite3_column_double(stmt, 0);
            if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
                outY = sqlite3_column_double(stmt, 1);
            if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
                outZ = sqlite3_column_double(stmt, 2);
        }
        sqlite3_finalize(stmt);
        return found;
    }

    bool Database::setWorldSeed(int32_t id, int64_t seed)
    {
        if (!m_db)
            return false;
        const char *sql = "UPDATE world_registry SET world_seed = ? WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare setWorldSeed: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_int64(stmt, 1, seed);
        sqlite3_bind_int(stmt, 2, id);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (!ok)
        {
            Core::Logger::warn("setWorldSeed: no world_registry row for plane id " + std::to_string(id));
        }
        return ok;
    }

    int64_t Database::getWorldSeed(int32_t id) const
    {
        if (!m_db)
            return kDefaultWorldSeed;
        const char *sql = "SELECT world_seed FROM world_registry WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return kDefaultWorldSeed;
        sqlite3_bind_int(stmt, 1, id);
        int64_t out = kDefaultWorldSeed;
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
            out = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return out;
    }

    // Targeted UPDATE of only the spawn_x / spawn_y / spawn_z columns on a
    // player row (never touches last_x/y/z, world_id, inventory_blob,
    // gamemode, op level, etc.). Probes first so /spawnpoint on an
    // unknown player returns false rather than silently no-op succeeding.
    bool Database::updatePlayerSpawn(const std::string &uuid, double x, double y, double z)
    {
        if (!m_db || uuid.empty())
            return false;
        {
            const char *probeSql = "SELECT COUNT(*) FROM players WHERE uuid = ?;";
            sqlite3_stmt *probe = nullptr;
            if (sqlite3_prepare_v2(m_db, probeSql, -1, &probe, nullptr) != SQLITE_OK)
            {
                Core::Logger::error("Failed to prepare updatePlayerSpawn probe: " + std::string(sqlite3_errmsg(m_db)));
                return false;
            }
            sqlite3_bind_text(probe, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
            int exists = 0;
            if (sqlite3_step(probe) == SQLITE_ROW)
                exists = sqlite3_column_int(probe, 0);
            sqlite3_finalize(probe);
            if (exists == 0)
                return false;
        }
        const char *sql = "UPDATE players SET spawn_x = ?, spawn_y = ?, spawn_z = ? WHERE uuid = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare updatePlayerSpawn: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_double(stmt, 1, x);
        sqlite3_bind_double(stmt, 2, y);
        sqlite3_bind_double(stmt, 3, z);
        sqlite3_bind_text(stmt, 4, uuid.c_str(), -1, SQLITE_TRANSIENT);
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    // ---------------------------------------------------------------------
    // Per-plane inventory SQLite table (SEPARATE / RESTRICTED policies).
    // Fresh CREATE TABLE keeps 0.0.14 DBs unaffected -- SYNC planes never
    // touch this table so their flat players.inventory_blob column stays
    // authoritative for the shared inventory chain.
    // ---------------------------------------------------------------------
    bool Database::initPlayerInventoriesTable()
    {
        if (!m_db)
            return false;
        const char *sql = R"(
            CREATE TABLE IF NOT EXISTS player_inventories (
                uuid TEXT NOT NULL,
                world_id INTEGER NOT NULL,
                inventory_data BLOB NOT NULL,
                updated_at INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (uuid, world_id)
            );
        )";
        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            Core::Logger::error("Failed to initialise player_inventories: " +
                                std::string(errMsg ? errMsg : "unknown"));
            if (errMsg) sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    bool Database::savePlayerInventory(const std::string &uuid, int32_t worldId,
                                       const std::vector<uint8_t> &blob)
    {
        if (!m_db || uuid.empty())
            return false;
        const char *sql =
            "INSERT OR REPLACE INTO player_inventories "
            "(uuid, world_id, inventory_data, updated_at) VALUES (?, ?, ?, ?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare savePlayerInventory: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, worldId);
        sqlite3_bind_blob(stmt, 3, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(std::time(nullptr)));
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    bool Database::loadPlayerInventory(const std::string &uuid, int32_t worldId,
                                       std::vector<uint8_t> &blobOut, bool &hasRowOut) const
    {
        blobOut.clear();
        hasRowOut = false;
        if (!m_db || uuid.empty())
            return false;
        const char *sql =
            "SELECT inventory_data FROM player_inventories WHERE uuid = ? AND world_id = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, worldId);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            found = true;
            hasRowOut = true;
            const int n = sqlite3_column_bytes(stmt, 0);
            if (n > 0)
            {
                const uint8_t *bytes = static_cast<const uint8_t *>(sqlite3_column_blob(stmt, 0));
                blobOut.assign(bytes, bytes + n);
            }
        }
        sqlite3_finalize(stmt);
        return found;
    }

    // Reads the flat players.inventory_blob column only, so PlayerManager::savePlayerState
    // can preserve the shared SYNC slot when the player currently sits on a
    // SEPARATE / RESTRICTED plane. Database::savePlayerData's INSERT OR
    // REPLACE semantics would otherwise write NULL to inventory_blob.
    bool Database::loadSyncInventoryBlob(const std::string &uuid,
                                          std::vector<uint8_t> &blobOut) const
    {
        blobOut.clear();
        if (!m_db || uuid.empty())
            return false;
        const char *sql = "SELECT inventory_blob FROM players WHERE uuid = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            found = true;
            const int n = sqlite3_column_bytes(stmt, 0);
            if (n > 0)
            {
                const uint8_t *bytes = static_cast<const uint8_t *>(sqlite3_column_blob(stmt, 0));
                blobOut.assign(bytes, bytes + n);
            }
        }
        sqlite3_finalize(stmt);
        return found;
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

// Restore deletePlayerPosition and add the level.dat NBT reader/writer
// below it. The block below is intentionally compact: the NBT helpers
// only cover what vanilla-1.8's level.dat actually needs, so reusing
// them for arbitrary NBT trees is out of scope today.

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
    } // deletePlayerPosition

    // -------------------------------------------------------------------
    // 0.0.15 phase 8c: NBT helpers for vanilla-1.8 level.dat.
    //
    // Vanilla tag IDs (see Minecraft TAG_* and https://en.wikipedia.org/
    // wiki/NBT): 0 End, 1 Byte, 2 ByteArray, 3 Short, 4 Int, 5 Long, 6
    // Float, 7 Double, 8 String, 9 List, 10 Compound. Helpers live in an
    // anonymous namespace so the Database class header stays clean and
    // other consumers don't get NBT tag names in their scope.
    // -------------------------------------------------------------------
    namespace
    {
        enum NbtTagId : uint8_t
        {
            TAG_End = 0, TAG_Byte = 1, TAG_ByteArray = 2, TAG_Short = 3,
            TAG_Int = 4, TAG_Long = 5, TAG_Float = 6, TAG_Double = 7,
            TAG_String = 8, TAG_List = 9, TAG_Compound = 10,
        };
        inline void pushBe16(std::vector<uint8_t> &o, uint16_t v)
        { o.push_back(static_cast<uint8_t>((v >> 8) & 0xFF)); o.push_back(static_cast<uint8_t>( v & 0xFF)); }
        inline void pushBe32(std::vector<uint8_t> &o, uint32_t v)
        { for (int i = 3; i >= 0; --i) o.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF)); }
        inline void pushBe64(std::vector<uint8_t> &o, uint64_t v)
        { for (int i = 7; i >= 0; --i) o.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF)); }
        inline uint16_t rdBe16(const uint8_t *p) { return (static_cast<uint16_t>(p[0]) << 8) | p[1]; }
        inline int32_t  rdBe32(const uint8_t *p) { return (static_cast<int32_t>(p[0]) << 24) | (static_cast<int32_t>(p[1]) << 16) | (static_cast<int32_t>(p[2]) << 8) | static_cast<int32_t>(p[3]); }
        inline int64_t  rdBe64(const uint8_t *p) { int64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<int64_t>(p[i]); return v; }
        inline void wTag(std::vector<uint8_t> &o, uint8_t id, const std::string &name)
        { o.push_back(id); pushBe16(o, static_cast<uint16_t>(name.size())); o.insert(o.end(), name.begin(), name.end()); }
        inline void wByte (std::vector<uint8_t> &o, const std::string &n, int8_t  v) { wTag(o, TAG_Byte, n); o.push_back(static_cast<uint8_t>(v)); }
        inline void wInt  (std::vector<uint8_t> &o, const std::string &n, int32_t v) { wTag(o, TAG_Int,  n); pushBe32(o, static_cast<uint32_t>(v)); }
        inline void wLong (std::vector<uint8_t> &o, const std::string &n, int64_t v) { wTag(o, TAG_Long, n); pushBe64(o, static_cast<uint64_t>(v)); }
        inline void wStr  (std::vector<uint8_t> &o, const std::string &n, const std::string &v)
        { wTag(o, TAG_String, n); pushBe16(o, static_cast<uint16_t>(v.size())); o.insert(o.end(), v.begin(), v.end()); }
    } // anonymous NBT namespace

    // -------------------------------------------------------------------
    // writeLevelDat: build a vanilla-1.8 TAG_Compound payload using the
    // helpers above, then gzip it with the bundled minigzip codec and
    // spill to `path`. Order of tags mirrors vanilla's Java client so a
    // vanilla tool can parse the result.
    // -------------------------------------------------------------------
    bool Database::writeLevelDat(const std::string &path,
                                 const LevelDatSnapshot &snap) const
    {
        if (path.empty())
            return false;

        std::vector<uint8_t> payload;
        payload.reserve(512);
        wTag(payload, TAG_Compound, std::string());
        wInt (payload, "format",         snap.format);
        wLong(payload, "seed",           snap.seed);
        wLong(payload, "time",           snap.time);
        wLong(payload, "modified",       snap.modified);
        wInt (payload, "used_blocks",    snap.usedBlocks);
        // StartPos: TAG_Byte[3] = TAG_ByteArray header + int32 length + 3 bytes.
        wTag(payload, TAG_ByteArray, "StartPos");
        pushBe32(payload, 3);
        auto clampByte = [](int32_t v) -> uint8_t
        {
            if (v < -128) v = -128;
            if (v >  127) v =  127;
            return static_cast<uint8_t>(static_cast<int8_t>(v));
        };
        payload.push_back(clampByte(snap.spawnX));
        payload.push_back(clampByte(snap.spawnY));
        payload.push_back(clampByte(snap.spawnZ));
        wInt (payload, "MapStatChunk",      snap.mapStatChunk);
        wInt (payload, "MapStatTrackChunks", snap.mapStatTrack);
        wByte(payload, "mode",              snap.mode);
        wStr (payload, "GameMode",          snap.gameMode);
        payload.push_back(TAG_End);

        const std::vector<uint8_t> gzipped =
            RegionFile::compressGzip(payload.data(),
                                     static_cast<int>(payload.size()));
        if (gzipped.empty())
        {
            Core::Logger::error("Database::writeLevelDat: gzip compress failed for " + path);
            return false;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            Core::Logger::error("Database::writeLevelDat: cannot open " + path);
            return false;
        }
        out.write(reinterpret_cast<const char *>(gzipped.data()),
                  static_cast<std::streamsize>(gzipped.size()));
        out.flush();
        return static_cast<bool>(out);
    }

    bool Database::readLevelDat(const std::string &path,
                                LevelDatSnapshot &out) const
    {
        if (path.empty())
            return false;
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            Core::Logger::warn("Database::readLevelDat: cannot open " + path);
            return false;
        }
        const std::vector<uint8_t> gz((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
        if (gz.empty())
            return false;
        const std::vector<uint8_t> nbt =
            RegionFile::decompressGzip(gz.data(), static_cast<int>(gz.size()));
        if (nbt.empty())
        {
            Core::Logger::warn("Database::readLevelDat: gzip decompress failed for " + path);
            return false;
        }
        std::size_t pos = 0;
        // Skip walker for tags we don't model (unknown additions, MapStats
        // compound, next_start_seed long, ServerType byte, etc.). Kept as
        // a self-passing lambda so no separate symbol is exposed to the
        // linker.
        auto skipTag = [&](auto self, std::size_t &p, uint8_t tagId) -> bool
        {
            const std::size_t n = nbt.size();
            if (tagId == TAG_Byte)   { if (p + 1 > n) return false; p += 1; return true; }
            if (tagId == TAG_Short)  { if (p + 2 > n) return false; p += 2; return true; }
            if (tagId == TAG_Int || tagId == TAG_Float) { if (p + 4 > n) return false; p += 4; return true; }
            if (tagId == TAG_Long || tagId == TAG_Double) { if (p + 8 > n) return false; p += 8; return true; }
            if (tagId == TAG_String) {
                if (p + 2 > n) return false;
                const uint16_t len = rdBe16(&nbt[p]); p += 2 + len; return p <= n;
            }
            if (tagId == TAG_ByteArray) {
                if (p + 4 > n) return false;
                const int32_t len = rdBe32(&nbt[p]); p += 4 + (len < 0 ? 0 : len); return p <= n;
            }
            if (tagId == TAG_List) {
                if (p + 5 > n) return false;
                const uint8_t inner = nbt[p];
                const int32_t len = rdBe32(&nbt[p + 1]);
                p += 5;
                for (int i = 0; i < len; ++i) if (!self(self, p, inner)) return false;
                return true;
            }
            if (tagId == TAG_Compound) {
                while (true)
                {
                    if (p >= n) return false;
                    const uint8_t child = nbt[p++];
                    if (child == TAG_End) return true;
                    if (p + 2 > n) return false;
                    const uint16_t nl = rdBe16(&nbt[p]); p += 2 + nl;
                    if (!self(self, p, child)) return false;
                }
            }
            return false;
        };

        if (pos >= nbt.size() || nbt[pos] != TAG_Compound)
            return false;
        ++pos;
        if (pos + 2 > nbt.size())
            return false;
        const uint16_t rootNameLen = rdBe16(&nbt[pos]); pos += 2 + rootNameLen;

        while (pos < nbt.size())
        {
            const uint8_t tagId = nbt[pos];
            if (tagId == TAG_End)
                return true;
            ++pos;
            if (pos + 2 > nbt.size())
                return false;
            const uint16_t nameLen = rdBe16(&nbt[pos]); pos += 2;
            if (pos + nameLen > nbt.size())
                return false;
            const std::string name(reinterpret_cast<const char *>(&nbt[pos]),
                                   static_cast<std::size_t>(nameLen));
            pos += nameLen;

            if (tagId == TAG_Int && name == "format")
            {
                if (pos + 4 > nbt.size()) return false;
                out.format = rdBe32(&nbt[pos]); pos += 4;
            }
            else if (tagId == TAG_Long && (name == "seed" || name == "time" || name == "modified"))
            {
                if (pos + 8 > nbt.size()) return false;
                const int64_t v = rdBe64(&nbt[pos]); pos += 8;
                if (name == "seed") out.seed = v;
                else if (name == "time") out.time = v;
                else out.modified = v;
            }
            else if (tagId == TAG_Int && name == "used_blocks")
            {
                if (pos + 4 > nbt.size()) return false;
                out.usedBlocks = rdBe32(&nbt[pos]); pos += 4;
            }
            else if (tagId == TAG_Int && (name == "MapStatChunk" || name == "MapStatTrackChunks"))
            {
                if (pos + 4 > nbt.size()) return false;
                const int32_t v = rdBe32(&nbt[pos]); pos += 4;
                if (name == "MapStatChunk") out.mapStatChunk = v;
                else out.mapStatTrack = v;
            }
            else if (tagId == TAG_Byte && name == "mode")
            {
                if (pos + 1 > nbt.size()) return false;
                out.mode = static_cast<int8_t>(nbt[pos]); pos += 1;
            }
            else if (tagId == TAG_String && name == "GameMode")
            {
                if (pos + 2 > nbt.size()) return false;
                const uint16_t len = rdBe16(&nbt[pos]); pos += 2;
                if (pos + len > nbt.size()) return false;
                out.gameMode = std::string(reinterpret_cast<const char *>(&nbt[pos]),
                                            static_cast<std::size_t>(len));
                pos += len;
            }
            else if (tagId == TAG_ByteArray && name == "StartPos")
            {
                if (pos + 4 > nbt.size()) return false;
                const int32_t len = rdBe32(&nbt[pos]); pos += 4;
                if (len >= 3 && pos + static_cast<std::size_t>(len) <= nbt.size())
                {
                    out.spawnX = static_cast<int8_t>(nbt[pos + 0]);
                    out.spawnY = static_cast<int8_t>(nbt[pos + 1]);
                    out.spawnZ = static_cast<int8_t>(nbt[pos + 2]);
                }
                pos += static_cast<std::size_t>(len < 0 ? 0 : len);
            }
            else
            {
                if (!skipTag(skipTag, pos, tagId))
                    return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------
    // Phase 9 world-processing rows.
    //
    // All three families carry a world_id column so a plane's tick states,
    // catch-up timestamps and ground drops never leak across planes (the
    // chunks table is per-SQLite-file already; block_entities predates this
    // and stays chunk-keyed only, see its own comment above).
    //
    // Timestamps are absolute epoch milliseconds bound with sqlite3_bind_
    // int64: an ms epoch is ~1.7e12, past the 32-bit range that bind_int
    // would silently truncate (SQLite INTEGER columns are 64-bit).
    // -------------------------------------------------------------------
    bool Database::saveTickState(const TickStateRow &row)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            INSERT OR REPLACE INTO tick_states
            (world_id, chunk_x, chunk_z, rel_x, y, rel_z, tick_kind, state, aux, next_tick_ms)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare saveTickState SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, row.worldId);
        sqlite3_bind_int(stmt, 2, row.cx);
        sqlite3_bind_int(stmt, 3, row.cz);
        sqlite3_bind_int(stmt, 4, row.relX);
        sqlite3_bind_int(stmt, 5, row.y);
        sqlite3_bind_int(stmt, 6, row.relZ);
        sqlite3_bind_int(stmt, 7, row.tickKind);
        sqlite3_bind_int(stmt, 8, row.state);
        sqlite3_bind_int(stmt, 9, row.aux);
        sqlite3_bind_int64(stmt, 10, row.nextTickMs);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            Core::Logger::error("Failed to save tick state at (" + std::to_string(row.cx) + ", " +
                                std::to_string(row.cz) + ", " + std::to_string(row.relX) + ", " +
                                std::to_string(row.y) + ", " + std::to_string(row.relZ) + ")");
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        return true;
    }

    bool Database::deleteTickState(int32_t worldId, int cx, int cz, int rx, int y, int rz)
    {
        if (!m_db)
            return false;

        const char *delSql =
            "DELETE FROM tick_states WHERE world_id = ? AND chunk_x = ? AND chunk_z = ? "
            "AND rel_x = ? AND y = ? AND rel_z = ?;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, delSql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare deleteTickState SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, worldId);
        sqlite3_bind_int(stmt, 2, cx);
        sqlite3_bind_int(stmt, 3, cz);
        sqlite3_bind_int(stmt, 4, rx);
        sqlite3_bind_int(stmt, 5, y);
        sqlite3_bind_int(stmt, 6, rz);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            Core::Logger::error("Failed to delete tick state at (" + std::to_string(cx) + ", " +
                                std::to_string(cz) + ", " + std::to_string(rx) + ", " +
                                std::to_string(y) + ", " + std::to_string(rz) + ")");
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        return true;
    }

    bool Database::loadTickStates(int32_t worldId, int cx, int cz, std::vector<TickStateRow> &out) const
    {
        if (!m_db)
            return false;

        const char *sql =
            "SELECT world_id, chunk_x, chunk_z, rel_x, y, rel_z, tick_kind, state, aux, next_tick_ms "
            "FROM tick_states WHERE world_id = ? AND chunk_x = ? AND chunk_z = ?;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare loadTickStates SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, worldId);
        sqlite3_bind_int(stmt, 2, cx);
        sqlite3_bind_int(stmt, 3, cz);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            TickStateRow row;
            row.worldId = sqlite3_column_int(stmt, 0);
            row.cx = sqlite3_column_int(stmt, 1);
            row.cz = sqlite3_column_int(stmt, 2);
            row.relX = sqlite3_column_int(stmt, 3);
            row.y = sqlite3_column_int(stmt, 4);
            row.relZ = sqlite3_column_int(stmt, 5);
            row.tickKind = sqlite3_column_int(stmt, 6);
            row.state = sqlite3_column_int(stmt, 7);
            row.aux = sqlite3_column_int(stmt, 8);
            row.nextTickMs = sqlite3_column_int64(stmt, 9);
            out.push_back(row);
        }
        sqlite3_finalize(stmt);
        return true;
    }

    bool Database::saveChunkTick(const ChunkTickRow &row)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            INSERT OR REPLACE INTO chunk_ticks
            (world_id, chunk_x, chunk_z, last_tick_ms)
            VALUES (?, ?, ?, ?);
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare saveChunkTick SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, row.worldId);
        sqlite3_bind_int(stmt, 2, row.cx);
        sqlite3_bind_int(stmt, 3, row.cz);
        sqlite3_bind_int64(stmt, 4, row.lastTickMs);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool Database::loadChunkTick(int32_t worldId, int cx, int cz, int64_t &lastTickMs) const
    {
        lastTickMs = 0;
        if (!m_db)
            return false;

        const char *sql =
            "SELECT last_tick_ms FROM chunk_ticks WHERE world_id = ? AND chunk_x = ? AND chunk_z = ?;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare loadChunkTick SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, worldId);
        sqlite3_bind_int(stmt, 2, cx);
        sqlite3_bind_int(stmt, 3, cz);

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            lastTickMs = sqlite3_column_int64(stmt, 0);
            found = true;
        }
        sqlite3_finalize(stmt);
        return found;
    }
    bool Database::saveGroundItem(const GroundItemRow &row)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            INSERT OR REPLACE INTO ground_items
            (world_id, chunk_x, chunk_z, rel_x, y, rel_z, item_id, item_count, damage, spawn_ms, lifetime_ms)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare saveGroundItem SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, row.worldId);
        sqlite3_bind_int(stmt, 2, row.cx);
        sqlite3_bind_int(stmt, 3, row.cz);
        sqlite3_bind_int(stmt, 4, row.relX);
        sqlite3_bind_int(stmt, 5, row.y);
        sqlite3_bind_int(stmt, 6, row.relZ);
        sqlite3_bind_int(stmt, 7, row.itemId);
        sqlite3_bind_int(stmt, 8, row.itemCount);
        sqlite3_bind_int(stmt, 9, row.damage);
        sqlite3_bind_int64(stmt, 10, row.spawnMs);
        sqlite3_bind_int64(stmt, 11, row.lifetimeMs);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool Database::deleteGroundItem(int32_t worldId, int cx, int cz, int rx, int y, int rz)
    {
        if (!m_db)
            return false;

        const char *sql =
            "DELETE FROM ground_items WHERE world_id = ? AND chunk_x = ? AND chunk_z = ? "
            "AND rel_x = ? AND y = ? AND rel_z = ?;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare deleteGroundItem SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, worldId);
        sqlite3_bind_int(stmt, 2, cx);
        sqlite3_bind_int(stmt, 3, cz);
        sqlite3_bind_int(stmt, 4, rx);
        sqlite3_bind_int(stmt, 5, y);
        sqlite3_bind_int(stmt, 6, rz);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool Database::loadGroundItems(int32_t worldId, int cx, int cz, std::vector<GroundItemRow> &out) const
    {
        if (!m_db)
            return false;

        const char *sql =
            "SELECT world_id, chunk_x, chunk_z, rel_x, y, rel_z, item_id, item_count, damage, spawn_ms, lifetime_ms "
            "FROM ground_items WHERE world_id = ? AND chunk_x = ? AND chunk_z = ?;";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Failed to prepare loadGroundItems SQL: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        sqlite3_bind_int(stmt, 1, worldId);
        sqlite3_bind_int(stmt, 2, cx);
        sqlite3_bind_int(stmt, 3, cz);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            GroundItemRow row;
            row.worldId = sqlite3_column_int(stmt, 0);
            row.cx = sqlite3_column_int(stmt, 1);
            row.cz = sqlite3_column_int(stmt, 2);
            row.relX = sqlite3_column_int(stmt, 3);
            row.y = sqlite3_column_int(stmt, 4);
            row.relZ = sqlite3_column_int(stmt, 5);
            row.itemId = sqlite3_column_int(stmt, 6);
            row.itemCount = sqlite3_column_int(stmt, 7);
            row.damage = sqlite3_column_int(stmt, 8);
            row.spawnMs = sqlite3_column_int64(stmt, 9);
            row.lifetimeMs = sqlite3_column_int64(stmt, 10);
            out.push_back(row);
        }
        sqlite3_finalize(stmt);
        return true;
    }



} // namespace LightweightMC::Storage
