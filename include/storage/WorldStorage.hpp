#pragma once
#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace LightweightMC::Storage
{
    struct BlockCoord
    {
        int relX, relY, relZ;
        bool operator==(const BlockCoord &o) const
        {
            return relX == o.relX && relY == o.relY && relZ == o.relZ;
        }
    };

    struct BlockCoordHash
    {
        std::size_t operator()(const BlockCoord &c) const
        {
            return std::hash<int>()(c.relX) ^ (std::hash<int>()(c.relY) << 1) ^ (std::hash<int>()(c.relZ) << 2);
        }
    };

    class WorldStorage
    {
    private:
        sqlite3 *m_db{nullptr};
        sqlite3_stmt *m_getChunkStmt{nullptr};
        sqlite3_stmt *m_saveChunkStmt{nullptr};

        // Internal LZ4 helpers
        static std::vector<uint8_t> compressBuffer(const std::vector<uint8_t> &uncompressed);
        static std::vector<uint8_t> decompressBuffer(const uint8_t *compressedData, size_t compressedSize, size_t uncompressedSize);

    public:
        WorldStorage() = default;
        ~WorldStorage();

        bool init(const std::string &dbPath);

        // World Generation and Persistence
        bool isChunkGenerated(int chunkX, int chunkZ);
        void generateChunk(int chunkX, int chunkZ);
        void pregenerateWorld(int radius);

        // Block & Chunk Storage (LZ4 Compressed)
        void saveBlockChange(int chunkX, int chunkZ, int relX, int relY, int relZ, uint16_t blockId);
        std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> getChunkBlocks(int chunkX, int chunkZ);

        // Returns the combined block data (blockId << 4 | meta) at a world position, or 0 if air/unknown
        uint16_t getBlockAt(int chunkX, int chunkZ, int relX, int relY, int relZ);
    };

} // namespace LightweightMC::Storage