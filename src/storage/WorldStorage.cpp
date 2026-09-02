#include "storage/WorldStorage.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <iostream>
namespace LightweightMC::Storage
{

    WorldStorage::~WorldStorage()
    {
        // 1. Release the pre-compiled statement before closing the database
        if (m_getChunkStmt)
            sqlite3_finalize(m_getChunkStmt);
        if (m_db)
            sqlite3_close(m_db);
    }

    bool WorldStorage::init(const std::string &dbPath)
    {
        if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK)
        {
            Core::Logger::error("Impossible d'ouvrir la base SQLite: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        // Enable WAL mode and NORMAL synchronization for performance
        sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

        const char *sqlTables = R"(
        CREATE TABLE IF NOT EXISTS chunk_blocks (
            chunk_x INT, chunk_z INT, rel_x INT, rel_y INT, rel_z INT, block_id INT,
            PRIMARY KEY (chunk_x, chunk_z, rel_x, rel_y, rel_z)
        );
        CREATE TABLE IF NOT EXISTS generated_chunks (
            chunk_x INT, chunk_z INT, 
            PRIMARY KEY (chunk_x, chunk_z)
        );
        CREATE INDEX IF NOT EXISTS idx_chunk_coords ON chunk_blocks(chunk_x, chunk_z);
    )";

        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sqlTables, nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            Core::Logger::error("Error creating tables: " + std::string(errMsg));
            sqlite3_free(errMsg);
            return false;
        }

        // 2. One-time preparation of the read request
        const char *sqlSelect = "SELECT rel_x, rel_y, rel_z, block_id FROM chunk_blocks WHERE chunk_x = ? AND chunk_z = ?;";
        if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &m_getChunkStmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Error preparing SQL statement : " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }
        return true;
    }

    bool WorldStorage::isChunkGenerated(int chunkX, int chunkZ)
    {
        const char *sql = "SELECT 1 FROM generated_chunks WHERE chunk_x = ? AND chunk_z = ? LIMIT 1;";
        sqlite3_stmt *stmt;
        bool exists = false;

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, chunkX);
            sqlite3_bind_int(stmt, 2, chunkZ);
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                exists = true;
            }
        }
        sqlite3_finalize(stmt);
        return exists;
    }

    void WorldStorage::generateChunk(int chunkX, int chunkZ)
    {
        if (isChunkGenerated(chunkX, chunkZ))
            return;

        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        const char *sqlBlock = "INSERT INTO chunk_blocks (chunk_x, chunk_z, rel_x, rel_y, rel_z, block_id) VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(m_db, sqlBlock, -1, &stmt, nullptr) == SQLITE_OK)
        {
            for (int x = 0; x < 16; ++x)
            {
                for (int z = 0; z < 16; ++z)
                {
                    for (int y = 0; y <= 5; ++y)
                    {
                        uint16_t blockType = 0;
                        if (y == 0)
                            blockType = (7 << 4); // Bedrock
                        else if (y <= 3)
                            blockType = (1 << 4); // Stone
                        else if (y == 4)
                            blockType = (3 << 4); // Dirt
                        else if (y == 5)
                            blockType = (2 << 4); // Grass

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

        const char *sqlMark = "INSERT INTO generated_chunks (chunk_x, chunk_z) VALUES (?, ?);";
        if (sqlite3_prepare_v2(m_db, sqlMark, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, chunkX);
            sqlite3_bind_int(stmt, 2, chunkZ);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);

        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);

        Core::Logger::info("generateChunk completed for [" + std::to_string(chunkX) + ", " + std::to_string(chunkZ) + "]");
    }

    void WorldStorage::pregenerateWorld(int radius)
    {
        Core::Logger::info("World verification and pre-generation in progress...");

        // Removed BEGIN/COMMIT here to avoid the "cannot start a transaction within a transaction" SQLite crash
        // (generateChunk already handles its own transaction efficiently).

        int count = 0;
        for (int cx = -radius; cx < radius; ++cx)
        {
            for (int cz = -radius; cz < radius; ++cz)
            {
                if (!isChunkGenerated(cx, cz))
                {
                    auto start = std::chrono::high_resolution_clock::now();

                    // Chunk generation
                    generateChunk(cx, cz);

                    auto end = std::chrono::high_resolution_clock::now();
                    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

                    std::cout << "[BENCH] Chunk (" << cx << ", " << cz << ") generated in : " << duration_us << " us" << std::endl;
                    count++;
                }
            }
        }

        Core::Logger::info("Pre-generation complete: " + std::to_string(count) + " new chunks created.");
    }

    void WorldStorage::saveBlockChange(int chunkX, int chunkZ, int relX, int relY, int relZ, uint16_t blockId)
    {
        if (!isChunkGenerated(chunkX, chunkZ))
        {
            auto start = std::chrono::high_resolution_clock::now();

            // Chunk generation
            generateChunk(chunkX, chunkZ);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

            std::cout << "[BENCH] Chunk (" << chunkX << ", " << chunkZ << ") generated in : " << duration_us << " us" << std::endl;
        }

        const char *sql = "INSERT OR REPLACE INTO chunk_blocks (chunk_x, chunk_z, rel_x, rel_y, rel_z, block_id) "
                          "VALUES (?, ?, ?, ?, ?, ?);";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, chunkX);
            sqlite3_bind_int(stmt, 2, chunkZ);
            sqlite3_bind_int(stmt, 3, relX);
            sqlite3_bind_int(stmt, 4, relY);
            sqlite3_bind_int(stmt, 5, relZ);
            sqlite3_bind_int(stmt, 6, blockId);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }

    std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> WorldStorage::getChunkBlocks(int chunkX, int chunkZ)
    {
        if (!isChunkGenerated(chunkX, chunkZ))
        {
            auto start = std::chrono::high_resolution_clock::now();

            // Chunk generation
            generateChunk(chunkX, chunkZ);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

            std::cout << "[BENCH] Chunk (" << chunkX << ", " << chunkZ << ") generated in : " << duration_us << " us" << std::endl;
        }

        std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> blocks;
        // 3. Pre-allocation to avoid thread saturation
        blocks.reserve(1536);

        // 4. Using the compiled statement
        sqlite3_reset(m_getChunkStmt);
        sqlite3_clear_bindings(m_getChunkStmt);

        sqlite3_bind_int(m_getChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_getChunkStmt, 2, chunkZ);

        while (sqlite3_step(m_getChunkStmt) == SQLITE_ROW)
        {
            int rx = sqlite3_column_int(m_getChunkStmt, 0);
            int ry = sqlite3_column_int(m_getChunkStmt, 1);
            int rz = sqlite3_column_int(m_getChunkStmt, 2);
            uint16_t blockId = static_cast<uint16_t>(sqlite3_column_int(m_getChunkStmt, 3));

            blocks[{rx, ry, rz}] = blockId;
        }

        return blocks;
    }

} // namespace LightweightMC::Storage