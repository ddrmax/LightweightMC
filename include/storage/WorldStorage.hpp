#pragma once
#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <utility>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>

namespace LightweightMC::Storage
{
    // World height range for a 0-based world: Y = 0 .. 255, i.e. 16 sections of 16 blocks.
    // The 1.8 Map Chunk Bulk / Chunk wire formats carry a 16-bit section bitmask, so
    // sections 16+ cannot be represented on the wire. The 16-section ceiling keeps
    // generated terrain inside the encodable range; if MAX_WORLD_Y ever grows above
    // 255, callers must split tall chunks across separate packets (see the s < 16
    // guards in sendFlatChunk / sendMapChunkBulk).
    constexpr int MIN_WORLD_Y = 0;
    constexpr int MAX_WORLD_Y = 255;
    constexpr int NUM_SECTIONS = (MAX_WORLD_Y - MIN_WORLD_Y) / 16 + 1; // 16
    static_assert(NUM_SECTIONS <= 16, "Wire sectionBitmask is 16-bit: NUM_SECTIONS must stay <= 16");

    // 16x16x16 section size in 16-bit block entries = 4096 blocks = 8192 bytes
    constexpr size_t UNCOMPRESSED_SECTION_SIZE = 16 * 16 * 16 * sizeof(uint16_t);
    // Full chunk: all 16 sections stacked = 16 * 8192 = 131,072 bytes uncompressed
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
        OVERWORLD,  // Real Minecraft 1.7 style terrain (64-bit seed, Perlin
                    // heightmap, caves, oak/pine forests, sea level 62).
        SUPERFLAT,  // Superflat preset layers (modern Minecraft presets: flat / stone / water)
        TEST_GRID,  // Deterministic block grid for protocol validation (see /testpackets)
        NETHER,     // Underworld: solid stone ceiling + carved caverns + lava.
        AETHER,     // Sky world: floating islands, slime-trunk trees, void gap.
        END,        // "The End" plane (pre-1.12 id = 1): obsidian monoliths
                    // over a bedrock floor, redstone-brick pillars, and a
                    // void ceiling -- the palette the client shows when the
                    // player crosses into the deepest vanilla dimension.
        CREATIVE,   // Flat infinite canvas: solid dirt/grass column from y=0
                    // up to y=127 with no ore, trees, or structures. Backs the
                    // "creative" embedded world (id 99 in the reserved special-
                    // dimension band).
        MINING,     // Overworld-shaped heightmap pinned to a PLAINS surface
                    // (grass over dirt over stone over bedrock) with 20× the
                    // vanilla ore density and 10× cluster size; no trees, no
                    // structures, no caves. Backs the "mining" embedded world
                    // (id 98 in the reserved band).
        TWILIGHT    // Twilight-forrest plane: forest-floor surface + 5×
                    // plantGiantTree per column (10..50 tall, 4x4 trunks,
                    // mixed WOOD/LEAVES/LEAVES2 species) with no ore,
                    // no structures. Backs the "twilight" embedded world
                    // (id 97 in the reserved band).
    };

    // ---------------------------------------------------------------------
    // Phase 8 world-model trigger. Selects the file-format backend that
    // NetworkManager::importWorldFromPath / exportWorldToPath speak when
    // the /import-world and /export-world commands run. The trigger lives
    // in server.properties under `[world] world-model = sqlite|anvil|legacy`
    // (default sqlite); per-invocation format-hint from the command can
    // override it. Unknown values fall back to SQLITE at boot and log a
    // warn so admins see the trigger actually reached the parser.
    // ---------------------------------------------------------------------
    enum class WorldModel
    {
        SQLITE,   // default -- the server's own per-plane SQLite .db files
                  // (world.db, world-nether.db, world-<custom>.db). Import
                  // copies an .db file over the plane's SQLite database;
                  // export writes the plane's database to the requested
                  // path. This is the only mode that round-trips every
                  // subsystem (terrain, containers, entities) unchanged.
        ANVIL,    // Anvil region format (.mca). A file-level container of
                  // region tiles, each tile holding 16x16 chunk blobs
                  // LZ4-compressed with the same codec as
                  // RegionFile::compressLZ4. Import reads the region tiles
                  // and seeds the plane's chunk blobs; export writes them
                  // back out. Container/entity state is not carried across
                  // the boundary because the Anvil format does not model
                  // it -- terrain only.
        LEGACY    // Pre-Anvil vanilla region format (.mcc / legacy region
                  // file). Same shape as ANVIL in this build -- gzip framing
                  // would require the header-only tiny-gzip we do not yet
                  // depend on, so we reuse the LZ4 codec as a placeholder
                  // and document the wire-format divergence in the log.
    };

    // Parses a config-string / command-string into a WorldModel. Returns
    // SQLITE on empty or unknown values; callers log a warn when the
    // fallback triggers.
    inline WorldModel worldModelFromName(const std::string &name)
    {
        if (name == "anvil")  return WorldModel::ANVIL;
        if (name == "legacy") return WorldModel::LEGACY;
        return WorldModel::SQLITE;
    }
    // Reverse of worldModelFromName -- used by NetworkManager::worldModelName
    // so the /import-world and /export-world commands can print the trigger
    // value in their confirmation replies.
    inline const char *worldModelToName(WorldModel m)
    {
        switch (m)
        {
            case WorldModel::ANVIL:  return "anvil";
            case WorldModel::LEGACY: return "legacy";
            case WorldModel::SQLITE:
            default:                  return "sqlite";
        }
    }


    class WorldStorage
    {
    private:
        sqlite3 *m_db{nullptr};
        sqlite3_stmt *m_getChunkStmt{nullptr};
        sqlite3_stmt *m_saveChunkStmt{nullptr};
        GenStyle m_genStyle{GenStyle::OVERWORLD};
        std::uint64_t m_worldSeed{0};

        // SQLite shared-stmt locks. m_getChunkStmt and m_saveChunkStmt are per-instance
        // prepared statements, so only ONE thread at a time may drive each of them.
        // Two separate mutexes let the network thread's reads (getChunkStmt) overlap
        // with the generation worker's writes (saveChunkStmt).
        std::mutex m_getStmtMutex;
        std::recursive_mutex m_saveStmtMutex;

        // -------------------------------------------------------------------
        // Off-thread chunk generation
        // -------------------------------------------------------------------
        // The network send path used to trigger synchronous generateChunk() on
        // the first readChunkBlob miss, which made clients hitch / drop chunks
        // at high render distances (each missing chunk blocked the send loop by
        // ~1-3ms of terrain generation + LZ4 compression + SQLite write, times
        // hundreds of chunks on the first tick and every crossing). Moving gen
        // to a dedicated worker per world decouples wire emission (which can
        // only emit chunks that ARE already generated) from terrain creation
        // (which runs in the background and prefetches a small margin ahead of
        // each player's view).
        //
        // One worker per WorldStorage (not per client) is the correct
        // granularity: it matches the SQLite handle topology (one sqlite3*
        // per world, one sqlite3_stmt* for writes per world) and keeps thread
        // count proportional to the number of *planes* (~3-4 with Nether/End/
        // Aether) instead of the number of live players (potentially 50+).
        std::mutex m_genQueueMutex;
        std::condition_variable m_genCv;
        std::deque<std::pair<int, int>> m_genQueue;
        std::unordered_set<std::uint64_t> m_genQueued;
        std::thread m_genWorker;
        std::atomic<bool> m_genWorkerStarted{false};
        std::atomic<bool> m_genStopFlag{false};

        static std::vector<uint8_t> compressBuffer(const std::vector<uint8_t> &uncompressed);
        static std::vector<uint8_t> decompressBuffer(const uint8_t *compressedData, size_t compressedSize, size_t uncompressedSize);

        // Worker body for the off-thread generation thread. Called via a lambda
        // capture from requestChunk so it can touch the private m_genQueue /
        // m_genQueued / m_genCv / m_genStopFlag members. Terminates when the
        // WorldStorage dtor flips m_genStopFlag AND the queue drains; that's
        // what enables the ~WorldStorage join() without risk of a stuck worker.
        void generationWorkerLoop();

    public:
        WorldStorage() = default;
        ~WorldStorage();

        // Enqueue a chunk for asynchronous generation. Idempotent: returns
        // immediately if the chunk is already generated (present in the DB)
        // or already sitting in the pending queue. Spawns the worker thread
        // on the first call so a plane that is never looked at costs no
        // threads. Safe to call from the network send loop.
        void requestChunk(int chunkX, int chunkZ);

        // Cheap, thread-safe check: is the chunk's compressed blob already
        // written to the DB? Same as isChunkGenerated(); separated by name
        // so callers in the send path don't confuse the "ready for wire"
        // check with the "was requested to be generated" bookkeeping.
        bool isChunkReady(int chunkX, int chunkZ);

        // Read-only block lookup for the streaming send path. Unlike getChunkBlocks(),
        // which lazy-generates on a DB miss (fine for the login burst but not for
        // movement crossings, where the caller wants the network thread to stay
        // responsive), this variant returns an empty map when the chunk hasn't
        // been generated yet. Callers pair it with requestChunk() to submit
        // generation to the worker and re-send on the next tick.
        std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> getChunkBlocksIfReady(int chunkX, int chunkZ);

        bool init(const std::string &dbPath);

        // Selects the generation preset used by generateChunk(). Must be called before
        // any chunk is generated (i.e. during startup, before pregenerateWorld).
        void setGenerationStyle(GenStyle style) { m_genStyle = style; }
        GenStyle getGenerationStyle() { return m_genStyle; }
        // 64-bit world seed. Every preset (overworld, nether, aether and the
        // superflat variants) mixes this value into its LCG/Perlin inputs so
        // a player replays the same world by writing the seed into
        // [world] world-seed in server.properties. Default is 0 (mirrors
        // Java Minecraft's "no seed given" behaviour).
        void setWorldSeed(std::uint64_t seed) { m_worldSeed = seed; }
        std::uint64_t getWorldSeed() const { return m_worldSeed; }
        // Fills a zeroed full-chunk buffer (UNCOMPRESSED_CHUNK_SIZE bytes, 24 sections)
        // with the terrain layers for the given style.
        static void generateFlatChunk(GenStyle style, std::vector<uint8_t> &rawBuffer, int chunkX, int chunkZ,
                                      std::uint64_t worldSeed);


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

        // ---- Spawn-eligibility helpers (world coordinates) -----------------
        // The spawn rules below are the vanilla Java Edition predicate for
        // "a 1-wide / 2-tall standing entity fits here": solid at feetY-1
        // (so the entity is grounded), air at feetY (feet block) AND air at
        // feetY+1 (head block). Both EntityManager (per-login mob seeding)
        // and PlayerManager::sendPlayPackets (first login) call these so a
        // player or a mob never appears buried inside generated terrain or
        // inside a player-placed block -- the exact 2-air-above rule the
        // user reported the client was showing.
        //
        // Not marked const: getBlockAt itself is not const (it reads from a
        // mutable SQLite handle under the hood), so the wrappers can't be
        // const-correct without also making getBlockAt const, which would
        // ripple through the readChunkBlob/stmt-reuse code path.
        uint16_t blockAtWorld(int wx, int wy, int wz);
        bool canStandAtWorld(int wx, int wy, int wz);
        // Search column (wx, wz) for the nearest feet-Y to hintY satisfying
        // canStandAtWorld(); returns -1 when no legal feet-Y exists in the
        // world Y range. Scans outward from hintY so spawns end up close to
        // the caller's current altitude when the terrain has a surface.
        int findStandingFeetY(int wx, int wz, int hintY);

    private:
        // Reads the chunk blob from SQLite and expands it into a full UNCOMPRESSED_CHUNK_SIZE
        // buffer (24 sections). Legacy 8192-byte blobs are migrated in place: their content
        // is preserved as section 0 (absY 0..15) and the remaining sections are zeroed.
        // Returns false if the chunk does not exist in the database.
        bool readChunkBlob(int chunkX, int chunkZ, std::vector<uint8_t> &outFull);
    };

} // namespace LightweightMC::Storage
