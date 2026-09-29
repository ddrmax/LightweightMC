#pragma once
#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace LightweightMC::Storage
{
    // World height range for a 0-based world: Y = 0 .. 384, i.e. 25 sections of 16 blocks.
    // The client only renders sections 0..15 (Y 0..255); the extra headroom above is unused
    // but harmless. All generated terrain must stay within Y >= 0 to be visible.
    constexpr int MIN_WORLD_Y = 0;
    constexpr int MAX_WORLD_Y = 255;
    constexpr int NUM_SECTIONS = (MAX_WORLD_Y - MIN_WORLD_Y) / 16 + 1; // 25

    // 16x16x16 section size in 16-bit block entries = 4096 blocks = 8192 bytes
    constexpr size_t UNCOMPRESSED_SECTION_SIZE = 16 * 16 * 16 * sizeof(uint16_t);
    // Full chunk: all sections stacked = 24 * 8192 = 196,608 bytes uncompressed
    constexpr size_t UNCOMPRESSED_CHUNK_SIZE = NUM_SECTIONS * UNCOMPRESSED_SECTION_SIZE;

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

    // World generation presets. Selection is driven by the [world] gen-style
    // entry in server.properties (see NetworkManager::start).
    enum class GenStyle
    {
        OVERWORLD, // Bedrock at Y=-64, stone up to surface-3, dirt/grass at surface
        SUPERFLAT  // Superflat preset layers (modern Minecraft presets: flat / stone / water)
    };

    class WorldStorage
    {
    private:
        sqlite3 *m_db{nullptr};
        sqlite3_stmt *m_getChunkStmt{nullptr};
        sqlite3_stmt *m_saveChunkStmt{nullptr};
        GenStyle m_genStyle{GenStyle::OVERWORLD};

        // Internal LZ4 helpers
        static std::vector<uint8_t> compressBuffer(const std::vector<uint8_t> &uncompressed);
        static std::vector<uint8_t> decompressBuffer(const uint8_t *compressedData, size_t compressedSize, size_t uncompressedSize);

    public:
        WorldStorage() = default;
        ~WorldStorage();

        bool init(const std::string &dbPath);

        // Selects the generation preset used by generateChunk(). Must be called before
        // any chunk is generated (i.e. during startup, before pregenerateWorld).
        void setGenerationStyle(GenStyle style) { m_genStyle = style; }
        GenStyle getGenerationStyle() { return m_genStyle; }
        // Fills a zeroed full-chunk buffer (UNCOMPRESSED_CHUNK_SIZE bytes, 24 sections)
        // with the terrain layers for the given style.
        static void generateFlatChunk(GenStyle style, std::vector<uint8_t> &rawBuffer, int chunkX, int chunkZ);

        // World Generation and Persistence
        bool isChunkGenerated(int chunkX, int chunkZ);
        void generateChunk(int chunkX, int chunkZ);
        void pregenerateWorld(int radius);

        // Block & Chunk Storage (LZ4 Compressed)
        // relY is an absolute world Y coordinate (MIN_WORLD_Y .. MAX_WORLD_Y).
        void saveBlockChange(int chunkX, int chunkZ, int relX, int relY, int relZ, uint16_t blockId);
        // Returns all non-air blocks in the chunk as (relX, absY, relZ) -> combined (id << 4 | meta).
        std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> getChunkBlocks(int chunkX, int chunkZ);

        // Returns the combined block data (blockId << 4 | meta) at a world position, or 0 if air/unknown.
        // relY is an absolute world Y coordinate.
        uint16_t getBlockAt(int chunkX, int chunkZ, int relX, int relY, int relZ);

    private:
        // Reads the chunk blob from SQLite and expands it into a full UNCOMPRESSED_CHUNK_SIZE
        // buffer (24 sections). Legacy 8192-byte blobs are migrated in place: their content
        // is preserved as section 0 (absY 0..15) and the remaining sections are zeroed.
        // Returns false if the chunk does not exist in the database.
        bool readChunkBlob(int chunkX, int chunkZ, std::vector<uint8_t> &outFull);
    };

} // namespace LightweightMC::Storage
