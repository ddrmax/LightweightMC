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
    )";

        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            Core::Logger::error("Failed to execute DB initialization SQL: " + std::string(errMsg));
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    bool Database::savePlayerData(const PlayerData &data)
    {
        if (!m_db)
            return false;

        const char *sql = R"(
            INSERT OR REPLACE INTO players 
            (uuid, username, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch, last_x, last_y, last_z, last_yaw, last_pitch, inventory_blob, last_seen)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);
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
                   last_x, last_y, last_z, last_yaw, last_pitch, inventory_blob 
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
        }

        sqlite3_finalize(stmt);
        return found;
    }

} // namespace LightweightMC::Storage