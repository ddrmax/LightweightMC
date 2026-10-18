#pragma once
#include "world/Chunk.hpp"
#include "storage/Database.hpp"
#include "storage/WorldStorage.hpp"
#include <cstdint>

namespace LightweightMC::World
{
    // ------------------------------------------------------------------
    // ProcessingTick -- the world-processing tick over sparse registries.
    //
    // Steps only the blocks that are actually progressing (the per-chunk
    // TickRecord maps Chunk owns), never all 4096 cells of a chunk: cost is
    // per active process. Each step re-validates its inputs against the
    // authority -- the chunk cell (staged overlay first, then the SQLite row)
    // and the linked container row -- so a record never operates on
    // synchronisation-drifted data:
    //   * block placed/removed  -> DirtyWriter::touch bumps the chunk epoch,
    //                               the record is stale, the cell is re-read
    //                               and a vanished tickable drops its record;
    //   * container emptied     -> a dispenser/hopper stops moving items, and
    //                               refilling it resumes the transfer on the
    //                               next step;
    //   * linked chest broken   -> the container row is gone, the transfer
    //                               stops immediately (the user-visible
    //                               singleplayer behaviour).
    //
    // Every mutation is staged through Storage::DirtyWriter (batched flush per
    // chunk), never written inline, and bounded by the max_block_modifications
    // game rule. Steps are gated by the GameRules registry (doFireTick,
    // command_blocks_work, random_tick_speed, ...).
    // ------------------------------------------------------------------
    class ProcessingTick
    {
    public:
        static ProcessingTick &getInstance()
        {
            static ProcessingTick instance;
            return instance;
        }

        ProcessingTick(const ProcessingTick &) = delete;
        ProcessingTick &operator=(const ProcessingTick &) = delete;

        // Persisted row family for processes with no block of their own.
        static constexpr std::uint16_t KIND_GROUND = 0x200;

        void init(Storage::Database *db);
        Storage::Database *database() const { return m_db; }

        // Bring a chunk into the view: seed its record map from the persisted
        // tick_states / ground_items / chunk_ticks rows. A chunk that has never
        // been seen before (no rows at all) gets one bounded discovery scan
        // over its non-air cells so furnaces/crops/lava/etc. that were placed
        // by terrain generation start processing without waiting for an event.
        static void seedChunk(Storage::WorldStorage &plane, Storage::Database *db,
                              std::int32_t worldId, std::int32_t cx, std::int32_t cz,
                              Chunk &chunk, std::int64_t nowMs);

        // Apply every step that became due since the chunk's last processed
        // tick (catch-up replay), bounded by the per-chunk step budget, and
        // stage the resulting cells/rows/timestamps.
        static void processChunk(Storage::WorldStorage &plane, Storage::Database *db,
                                 std::int32_t worldId, std::int32_t cx, std::int32_t cz,
                                 Chunk &chunk, std::int64_t nowMs);

        // Neighbour-state memoisation: adjacency tests (redstone-style wiring
        // of burnables/flow sources, hopper links) ask for the same cells over
        // and over inside one pass. Entries carry the chunk epoch they were
        // read at, so a placement/removal (which bumps that epoch) invalidates
        // them -- memoised neighbour state can never outlive a block change.
        static uint16_t memoCell(std::int32_t worldId, Storage::WorldStorage &plane,
                                 int cx, int cz, int relX, int relY, int relZ);
        static void clearNeighbourMemo();

    private:
        ProcessingTick();
        ~ProcessingTick() = default;

        Storage::Database *m_db{nullptr};
    };

} // namespace LightweightMC::World
