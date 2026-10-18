#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace LightweightMC::Storage
{
    // -------------------------------------------------------------
    // DirtyWriter -- staged (deferred) write-back for every row family a
    // tick can touch: chunk cells, container contents (block_entities),
    // tick states and ground items.
    //
    // Nothing in the tick path writes SQLite directly. Every mutation is
    // staged in RAM, batched per chunk, and flushed when one of the
    // following triggers fires:
    //   * idle flush   -- the chunk has been dirty for >= idleFlushMs
    //                     (default 300 s, "after some seconds")
    //   * view exit    -- ChunkManager flushes a chunk before it leaves
    //                     the player's render distance (forced flush)
    //   * reload/entry -- bulk flush when a chunk comes back in view
    //   * shutdown     -- flushAll from the save point
    //   * budget       -- staged writes exceed max_block_modifications
    //
    // Coherence (the "no stale tick data" rule): every chunk carries a
    // monotonic epoch. Any block placement/removal or container-content
    // change bumps it (touch()). A tick record stamped with an older
    // epoch is stale, so the tick engine re-validates it against the
    // authoritative row -- chunk cell plus staged overlay -- before
    // applying a step. That is what keeps a hopper/dispenser from moving
    // items when the chest it feeds gets broken, and what stops a furnace
    // from cooking when it was just removed. peekCell/peekTick/... expose
    // the staged overlay so validation never reads pre-write SQLite data.
    //
    // Ownership of the actual SQLite writes stays with the plane owners:
    // WorldStorage registers the cell flusher (batched single read/patch/
    // write), Database-backed rows register their own. A plane that
    // registers nothing simply does not stage through this writer.
    // -------------------------------------------------------------
    class DirtyWriter
    {
    public:
        struct StagedCell
        {
            int relX, relY, relZ;      // relY is absolute world Y
            std::uint16_t value;       // combined18 (id << 4 | meta); 0 = air/broken
        };

        struct StagedContainerRow
        {
            int relX, relY, relZ;
            std::uint16_t type;
            std::vector<std::uint8_t> blob;   // container-item codec (PlayerSession.cpp)
            bool remove{false};               // row deletion (block broken / emptied)
        };

        struct StagedTickRow
        {
            int relX, relY, relZ;
            std::uint16_t kind{0};
            std::uint16_t state{0};
            std::uint16_t aux{0};
            std::int64_t nextTickMs{0};
            bool remove{false};               // tickable stopped (block gone / link broken)
        };

        struct StagedGroundRow
        {
            int relX, relY, relZ;
            std::int32_t itemId{0}, count{0}, damage{0};
            std::int64_t spawnMs{0}, lifetimeMs{0};
            bool remove{false};               // picked up / despawned
        };

        using CellFlushFn      = std::function<void(int, int, const std::vector<StagedCell> &)>;
        using ContainerFlushFn = std::function<void(int, int, const std::vector<StagedContainerRow> &)>;
        using TickFlushFn      = std::function<void(int, int, const std::vector<StagedTickRow> &)>;
        using GroundFlushFn    = std::function<void(int, int, const std::vector<StagedGroundRow> &)>;

        // Implementation bucket -- public so the staging helpers in
        // DirtyWriter.cpp can name the type (DirtyWriter::StagedChunk).
        struct StagedChunk
        {
            std::uint32_t epoch{0};
            std::int64_t firstDirtyMs{0};
            std::size_t writes{0};
            std::unordered_map<std::uint32_t, std::uint16_t> cells;
            std::unordered_map<std::uint32_t, StagedContainerRow> containers;
            std::unordered_map<std::uint32_t, StagedTickRow> ticks;
            std::unordered_map<std::uint32_t, StagedGroundRow> grounds;
        };

        // Key packing helpers (see DirtyWriter.cpp for the bit layout) and
        // their inverses; public because Chunk.hpp/Chunk.cpp address records
        // with the identical chunk-local key layout.
        static std::uint64_t packKey(int planeId, int cx, int cz);
        static std::uint32_t packCoord(int relX, int relY, int relZ);
        static void unpackKey(std::uint64_t key, int &planeId, int &cx, int &cz);
        static void unpackCoord(std::uint32_t key, int &relX, int &relY, int &relZ);


        static DirtyWriter &getInstance()
        {
            static DirtyWriter instance;
            return instance;
        }

        DirtyWriter(const DirtyWriter &) = delete;
        DirtyWriter &operator=(const DirtyWriter &) = delete;

        // A plane registers its flushers once (at WorldStorage::init / plane
        // creation). planeId is the world id the plane serves, so the
        // world_id column of the row families is filled with a value the
        // plane actually owns rather than a global guess.
        void registerCellFlush(int planeId, CellFlushFn fn);
        void registerContainerFlush(int planeId, ContainerFlushFn fn);
        void registerTickFlush(int planeId, TickFlushFn fn);
        void registerGroundFlush(int planeId, GroundFlushFn fn);

        // --- staged policy ---
        void setIdleFlushMs(std::int64_t ms);
        std::int64_t idleFlushMs() const { return m_idleFlushMs; }
        void setStagedWriteBudget(std::size_t writes);
        std::size_t stagedWriteBudget() const { return m_writeBudget; }

        // --- staging ---
        void stageCell(int planeId, int cx, int cz, int relX, int relY, int relZ,
                       std::uint16_t value, std::int64_t nowMs);
        void stageContainer(int planeId, int cx, int cz, int relX, int relY, int relZ,
                            std::uint16_t type, const std::vector<std::uint8_t> &blob,
                            std::int64_t nowMs);
        void dropContainer(int planeId, int cx, int cz, int relX, int relY, int relZ,
                           std::uint16_t type, std::int64_t nowMs);
        void stageTick(int planeId, int cx, int cz, int relX, int relY, int relZ,
                       std::uint16_t kind, std::uint16_t state, std::uint16_t aux,
                       std::int64_t nextTickMs, std::int64_t nowMs);
        void dropTick(int planeId, int cx, int cz, int relX, int relY, int relZ,
                      std::uint16_t kind, std::int64_t nowMs);
        void stageGround(int planeId, int cx, int cz, int relX, int relY, int relZ,
                         std::int32_t itemId, std::int32_t count, std::int32_t damage,
                         std::int64_t spawnMs, std::int64_t lifetimeMs, std::int64_t nowMs);
        void dropGround(int planeId, int cx, int cz, int relX, int relY, int relZ,
                        std::int32_t itemId, std::int64_t nowMs);

        // --- coherence epochs ---
        // Bumps the chunk epoch; any tick/state record derived before this
        // point is stale and must be re-validated before it is used.
        void touch(int planeId, int cx, int cz, const char *reason, std::int64_t nowMs);
        std::uint32_t epoch(int planeId, int cx, int cz) const;

        // Staged-overlay reads for tick validation. Return false when no
        // staged write covers the coordinate, i.e. the caller must fall
        // back to the authoritative SQLite row.
        bool peekCell(int planeId, int cx, int cz, int relX, int relY, int relZ,
                      std::uint16_t &outValue) const;
        bool peekContainer(int planeId, int cx, int cz, int relX, int relY, int relZ,
                           StagedContainerRow &outRow) const;
        bool peekTick(int planeId, int cx, int cz, int relX, int relY, int relZ,
                      StagedTickRow &outRow) const;
        bool peekGround(int planeId, int cx, int cz, int relX, int relY, int relZ,
                        StagedGroundRow &outRow) const;

        // --- flush ---
        bool flushChunk(int planeId, int cx, int cz, const char *reason);
        void flushDue(std::int64_t nowMs);
        void flushAll(const char *reason);

        std::size_t stagedChunks() const;
        std::size_t stagedWrites() const;

    private:
        DirtyWriter();
        ~DirtyWriter() = default;

        std::unordered_map<std::uint64_t, StagedChunk> m_buckets;

        // Flush one staged bucket. Called by the public flush entry points,
        // which hold m_mutex, so the bucket erase + callback dispatch stays
        // inside a single lock scope (callbacks must not call back into the
        // writer -- they write SQLite directly).
        void flushBucketLocked(std::uint64_t key, int planeId, int cx, int cz, const char *reason);
        std::unordered_map<int, CellFlushFn> m_cellFlushers;
        std::unordered_map<int, ContainerFlushFn> m_containerFlushers;
        std::unordered_map<int, TickFlushFn> m_tickFlushers;
        std::unordered_map<int, GroundFlushFn> m_groundFlushers;

        std::int64_t m_idleFlushMs{300000};
        std::size_t m_writeBudget{32768};   // mirrors the max_block_modifications default
        mutable std::mutex m_mutex;
    };

} // namespace LightweightMC::Storage

