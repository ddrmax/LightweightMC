#include "storage/WorldStorage.hpp"
#include "core/Logger.hpp"
#include <lz4.h>
#include <chrono>
#include <iostream>
#include <cstring>

namespace LightweightMC::Storage
{
    // 16x16x16 section size in 16-bit block entries = 4096 blocks = 8192 bytes
    constexpr size_t UNCOMPRESSED_SECTION_SIZE = 16 * 16 * 16 * sizeof(uint16_t);

    std::vector<uint8_t> WorldStorage::compressBuffer(const std::vector<uint8_t> &uncompressed)
    {
        int maxCompressedSize = LZ4_compressBound(static_cast<int>(uncompressed.size()));
        std::vector<uint8_t> compressed(maxCompressedSize);

        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char *>(uncompressed.data()),
            reinterpret_cast<char *>(compressed.data()),
            static_cast<int>(uncompressed.size()),
            maxCompressedSize);

        if (compressedSize <= 0)
        {
            Core::Logger::error("LZ4 Compression failed!");
            return {};
        }

        compressed.resize(compressedSize);
        return compressed;
    }

    std::vector<uint8_t> WorldStorage::decompressBuffer(const uint8_t *compressedData, size_t compressedSize, size_t uncompressedSize)
    {
        std::vector<uint8_t> decompressed(uncompressedSize);

        int decompressedBytes = LZ4_decompress_safe(
            reinterpret_cast<const char *>(compressedData),
            reinterpret_cast<char *>(decompressed.data()),
            static_cast<int>(compressedSize),
            static_cast<int>(uncompressedSize));

        if (decompressedBytes < 0 || static_cast<size_t>(decompressedBytes) != uncompressedSize)
        {
            Core::Logger::error("LZ4 Decompression failed!");
            return {};
        }

        return decompressed;
    }

    WorldStorage::~WorldStorage()
    {
        if (m_getChunkStmt)
            sqlite3_finalize(m_getChunkStmt);
        if (m_saveChunkStmt)
            sqlite3_finalize(m_saveChunkStmt);
        if (m_db)
            sqlite3_close(m_db);
    }

    bool WorldStorage::init(const std::string &dbPath)
    {
        if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK)
        {
            Core::Logger::error("Failed to open SQLite database: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        // Enable WAL mode and NORMAL synchronization for optimal I/O performance
        sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

        const char *sqlTables = R"(
        CREATE TABLE IF NOT EXISTS chunks (
            chunk_x INT, chunk_z INT, 
            data_blob BLOB,
            PRIMARY KEY (chunk_x, chunk_z)
        );
        CREATE TABLE IF NOT EXISTS generated_chunks (
            chunk_x INT, chunk_z INT, 
            PRIMARY KEY (chunk_x, chunk_z)
        );
    )";

        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sqlTables, nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            Core::Logger::error("Error creating SQLite tables: " + std::string(errMsg));
            sqlite3_free(errMsg);
            return false;
        }

        // Prepared statements for reading and writing compressed chunks
        const char *sqlSelect = "SELECT data_blob FROM chunks WHERE chunk_x = ? AND chunk_z = ?;";
        if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &m_getChunkStmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Error preparing getChunkStmt: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        const char *sqlInsert = "INSERT OR REPLACE INTO chunks (chunk_x, chunk_z, data_blob) VALUES (?, ?, ?);";
        if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &m_saveChunkStmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Error preparing saveChunkStmt: " + std::string(sqlite3_errmsg(m_db)));
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

        // Construct flat section 0 (Y=0..15, Z=0..15, X=0..15)
        std::vector<uint8_t> rawBuffer(UNCOMPRESSED_SECTION_SIZE, 0);
        uint16_t *blocksArray = reinterpret_cast<uint16_t *>(rawBuffer.data());

        for (int y = 0; y < 16; ++y)
        {
            for (int z = 0; z < 16; ++z)
            {
                for (int x = 0; x < 16; ++x)
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

                    int index = (y * 256) + (z * 16) + x;
                    blocksArray[index] = blockType;
                }
            }
        }

        // Compress chunk data using LZ4
        std::vector<uint8_t> compressed = compressBuffer(rawBuffer);

        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        sqlite3_reset(m_saveChunkStmt);
        sqlite3_clear_bindings(m_saveChunkStmt);
        sqlite3_bind_int(m_saveChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_saveChunkStmt, 2, chunkZ);
        sqlite3_bind_blob(m_saveChunkStmt, 3, compressed.data(), static_cast<int>(compressed.size()), SQLITE_TRANSIENT);
        sqlite3_step(m_saveChunkStmt);

        const char *sqlMark = "INSERT INTO generated_chunks (chunk_x, chunk_z) VALUES (?, ?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sqlMark, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, chunkX);
            sqlite3_bind_int(stmt, 2, chunkZ);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);

        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);

        Core::Logger::info("Generated and saved LZ4 compressed chunk [" + std::to_string(chunkX) + ", " + std::to_string(chunkZ) + "]");
    }

    void WorldStorage::pregenerateWorld(int radius)
    {
        Core::Logger::info("World verification and LZ4 pre-generation in progress...");

        int count = 0;
        for (int cx = -radius; cx < radius; ++cx)
        {
            for (int cz = -radius; cz < radius; ++cz)
            {
                if (!isChunkGenerated(cx, cz))
                {
                    auto start = std::chrono::high_resolution_clock::now();

                    generateChunk(cx, cz);

                    auto end = std::chrono::high_resolution_clock::now();
                    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

                    std::cout << "[BENCH] Compressed Chunk (" << cx << ", " << cz << ") generated in: " << duration_us << " us" << std::endl;
                    count++;
                }
            }
        }

        Core::Logger::info("LZ4 Pre-generation complete: " + std::to_string(count) + " new compressed chunks created.");
    }

    void WorldStorage::saveBlockChange(int chunkX, int chunkZ, int relX, int relY, int relZ, uint16_t blockId)
    {
        if (!isChunkGenerated(chunkX, chunkZ))
        {
            generateChunk(chunkX, chunkZ);
        }

        // 1. Read existing compressed chunk
        sqlite3_reset(m_getChunkStmt);
        sqlite3_clear_bindings(m_getChunkStmt);
        sqlite3_bind_int(m_getChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_getChunkStmt, 2, chunkZ);

        std::vector<uint8_t> rawBuffer(UNCOMPRESSED_SECTION_SIZE, 0);

        if (sqlite3_step(m_getChunkStmt) == SQLITE_ROW)
        {
            const void *blobData = sqlite3_column_blob(m_getChunkStmt, 0);
            int blobSize = sqlite3_column_bytes(m_getChunkStmt, 0);

            if (blobData && blobSize > 0)
            {
                rawBuffer = decompressBuffer(static_cast<const uint8_t *>(blobData), blobSize, UNCOMPRESSED_SECTION_SIZE);
            }
        }

        if (rawBuffer.size() < UNCOMPRESSED_SECTION_SIZE)
        {
            rawBuffer.resize(UNCOMPRESSED_SECTION_SIZE, 0);
        }

        // 2. Modify block in memory
        uint16_t *blocksArray = reinterpret_cast<uint16_t *>(rawBuffer.data());
        if (relY >= 0 && relY < 16 && relX >= 0 && relX < 16 && relZ >= 0 && relZ < 16)
        {
            int index = (relY * 256) + (relZ * 16) + relX;
            blocksArray[index] = blockId;
        }

        // 3. Compress and update database
        std::vector<uint8_t> compressed = compressBuffer(rawBuffer);

        sqlite3_reset(m_saveChunkStmt);
        sqlite3_clear_bindings(m_saveChunkStmt);
        sqlite3_bind_int(m_saveChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_saveChunkStmt, 2, chunkZ);
        sqlite3_bind_blob(m_saveChunkStmt, 3, compressed.data(), static_cast<int>(compressed.size()), SQLITE_TRANSIENT);
        sqlite3_step(m_saveChunkStmt);
    }

    std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> WorldStorage::getChunkBlocks(int chunkX, int chunkZ)
    {
        if (!isChunkGenerated(chunkX, chunkZ))
        {
            generateChunk(chunkX, chunkZ);
        }

        std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> blocks;
        blocks.reserve(1024);

        sqlite3_reset(m_getChunkStmt);
        sqlite3_clear_bindings(m_getChunkStmt);

        sqlite3_bind_int(m_getChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_getChunkStmt, 2, chunkZ);

        if (sqlite3_step(m_getChunkStmt) == SQLITE_ROW)
        {
            const void *blobData = sqlite3_column_blob(m_getChunkStmt, 0);
            int blobSize = sqlite3_column_bytes(m_getChunkStmt, 0);

            if (blobData && blobSize > 0)
            {
                std::vector<uint8_t> rawBuffer = decompressBuffer(static_cast<const uint8_t *>(blobData), blobSize, UNCOMPRESSED_SECTION_SIZE);

                if (rawBuffer.size() == UNCOMPRESSED_SECTION_SIZE)
                {
                    const uint16_t *blocksArray = reinterpret_cast<const uint16_t *>(rawBuffer.data());
                    for (int y = 0; y < 16; ++y)
                    {
                        for (int z = 0; z < 16; ++z)
                        {
                            for (int x = 0; x < 16; ++x)
                            {
                                int index = (y * 256) + (z * 16) + x;
                                uint16_t bId = blocksArray[index];
                                if (bId != 0)
                                {
                                    blocks[{x, y, z}] = bId;
                                }
                            }
                        }
                    }
                }
            }
        }

        return blocks;
    }

} // namespace LightweightMC::Storage