#include "storage/WorldStorage.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::Storage {

WorldStorage::~WorldStorage() {
    if (m_db) sqlite3_close(m_db);
}

bool WorldStorage::init(const std::string& dbPath) {
    if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK) {
        Core::Logger::error("Impossible to open SQLite Database: " + std::string(sqlite3_errmsg(m_db)));
        return false;
    }

    // Enable WAL mode for very fast writes
    sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

    const char* sql = "CREATE TABLE IF NOT EXISTS chunk_blocks ("
                      "chunk_x INT, chunk_z INT, rel_x INT, rel_y INT, rel_z INT, block_id INT, "
                      "PRIMARY KEY (chunk_x, chunk_z, rel_x, rel_y, rel_z));"
                      "CREATE TABLE IF NOT EXISTS generated_chunks ("
                      "chunk_x INT, chunk_z INT, PRIMARY KEY (chunk_x, chunk_z));";

    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Core::Logger::error("Erreur création tables : " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

bool WorldStorage::isChunkGenerated(int chunkX, int chunkZ) {
    const char* sql = "SELECT 1 FROM generated_chunks WHERE chunk_x = ? AND chunk_z = ? LIMIT 1;";
    sqlite3_stmt* stmt;
    bool exists = false;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, chunkX);
        sqlite3_bind_int(stmt, 2, chunkZ);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = true;
        }
    }
    sqlite3_finalize(stmt);
    return exists;
}

void WorldStorage::generateChunk(int chunkX, int chunkZ) {
    if (isChunkGenerated(chunkX, chunkZ)) return;

    const char* sqlBlock = "INSERT INTO chunk_blocks (chunk_x, chunk_z, rel_x, rel_y, rel_z, block_id) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sqlBlock, -1, &stmt, nullptr) == SQLITE_OK) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y <= 5; ++y) {
                    uint16_t blockType = 0;
                    if (y == 0) blockType = (7 << 4);       // Bedrock
                    else if (y <= 3) blockType = (1 << 4);  // Stone
                    else if (y == 4) blockType = (3 << 4);  // Dirt
                    else if (y == 5) blockType = (2 << 4);  // Grass

                    sqlite3_bind_int(stmt, 1, chunkX);
                    sqlite3_bind_int(stmt, 2, chunkZ);
                    sqlite3_bind_int(stmt, 3, x);
                    sqlite3_bind_int(stmt, 4, y);
                    sqlite3_bind_int(stmt, 5, z);
                    sqlite3_bind_int(stmt, 6, blockType);
                    sqlite3_step(stmt);
                    sqlite3_reset(stmt);
                }
            }
        }
    }
    sqlite3_finalize(stmt);

    // Mark the chunk as generated
    const char* sqlMark = "INSERT INTO generated_chunks (chunk_x, chunk_z) VALUES (?, ?);";
    if (sqlite3_prepare_v2(m_db, sqlMark, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, chunkX);
        sqlite3_bind_int(stmt, 2, chunkZ);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

void WorldStorage::pregenerateWorld(int radius) {
    Core::Logger::info("World verification and pre-generation in progress...");
    
    sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    
    int count = 0;
    for (int cx = -radius; cx < radius; ++cx) {
        for (int cz = -radius; cz < radius; ++cz) {
            if (!isChunkGenerated(cx, cz)) {
                generateChunk(cx, cz);
                count++;
            }
        }
    }
    
    sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    Core::Logger::info("Pre-generation complete: " + std::to_string(count) + " new chunks created.");
}

void WorldStorage::saveBlockChange(int chunkX, int chunkZ, int relX, int relY, int relZ, uint16_t blockId) {
    if (!isChunkGenerated(chunkX, chunkZ)) {
        generateChunk(chunkX, chunkZ);
    }

    const char* sql = "INSERT OR REPLACE INTO chunk_blocks (chunk_x, chunk_z, rel_x, rel_y, rel_z, block_id) "
                      "VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, chunkX);
        sqlite3_bind_int(stmt, 2, chunkZ);
        sqlite3_bind_int(stmt, 3, relX);
        sqlite3_bind_int(stmt, 4, relY);
        sqlite3_bind_int(stmt, 5, relZ);
        sqlite3_bind_int(stmt, 6, blockId); // blockId = 0 for air
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> WorldStorage::getChunkBlocks(int chunkX, int chunkZ) {    
// If the chunk doesn't exist in the database, we generate it on the fly!
    if (!isChunkGenerated(chunkX, chunkZ)) {
        generateChunk(chunkX, chunkZ);
    }

    std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> blocks;
    const char* sql = "SELECT rel_x, rel_y, rel_z, block_id FROM chunk_blocks WHERE chunk_x = ? AND chunk_z = ?;";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, chunkX);
        sqlite3_bind_int(stmt, 2, chunkZ);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int rx = sqlite3_column_int(stmt, 0);
            int ry = sqlite3_column_int(stmt, 1);
            int rz = sqlite3_column_int(stmt, 2);
            uint16_t blockId = static_cast<uint16_t>(sqlite3_column_int(stmt, 3));
            blocks[{rx, ry, rz}] = blockId;
        }
    }
    sqlite3_finalize(stmt);
    return blocks;
}

} // namespace LightweightMC::Storage