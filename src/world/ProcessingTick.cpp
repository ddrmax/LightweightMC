// src/world/ProcessingTick.cpp -- LightweightMC 1.8-era tick engine.
#include "world/ProcessingTick.hpp"
#include "world/Chunk.hpp"
#include "world/GameRules.hpp"
#include "world/MC18ItemIds.hpp"
#include "world/MC18BlockIds.hpp"
#include "world/Block.hpp"
#include "storage/DirtyWriter.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace LightweightMC::World
{
    namespace
    {
        // ---- Time source ---------------------------------------------------
        // Server.cpp's master loop already computes wall-clock milliseconds
        // from std::chrono::steady_clock::now(). We mirror the same source
        // here so seedChunk / processChunk are robust when called outside
        // the loop (tests, replays, etc.). Unused right now -- kept for future
        // processing-tick consumers, hence [[maybe_unused]] to stay warning-clean.
        [[maybe_unused]] static std::int64_t steadyNowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        // ---- Kind registry -------------------------------------------------
        // Kinds map 1:1 with the `tick_kind` column of tick_states and the
        // `kind` field of DirtyWriter::StagedTickRow. Physical tickables use
        // their block id so the revalidation path can compare kind against
        // the current (blockId = combinedId >> 4) without a table lookup.
        constexpr std::uint16_t KIND_FURNACE = Blocks18::FURNACE;
        constexpr std::uint16_t KIND_LIT_FURNACE = Blocks18::LIT_FURNACE;
        constexpr std::uint16_t KIND_CROP = Blocks18::WHEAT;
        constexpr std::uint16_t KIND_CARROTS = Blocks18::CARROTS;
        constexpr std::uint16_t KIND_DISPENSER = Blocks18::DISPENSER;
        constexpr std::uint16_t KIND_HOPPER = Blocks18::HOPPER;
        constexpr std::uint16_t KIND_BREWING_STAND = Blocks18::BREWING_STAND;
        constexpr std::uint16_t KIND_LAVA = Blocks18::LAVA_FLOWING;
        constexpr std::uint16_t KIND_WATER = Blocks18::WATER_FLOWING;
        constexpr std::uint16_t KIND_FIRE = Blocks18::FIRE;
        constexpr std::uint16_t KIND_SPAWNER = Blocks18::SPAWNER;
        constexpr std::uint16_t KIND_TORCH = Blocks18::TORCH;
        constexpr std::uint16_t KIND_GROUND = ProcessingTick::KIND_GROUND;

        // Step cadences (ms). Minecraft 1.8 fires furnaces on a 2s cooldown
        // (40 ticks @ 20 Hz) and drives crops via a random per-plant grow
        // roll; we approximate with fixed intervals so a 5-min logout can
        // fast-forward the sim deterministically.
        constexpr std::int64_t INTERVAL_FURNACE_MS = 2000;
        constexpr std::int64_t INTERVAL_LIT_FURNACE_MS = 500;
        constexpr std::int64_t INTERVAL_CROP_MS = 4000;
        constexpr std::int64_t INTERVAL_DISPENSER_MS = 400; // 8-tick dispense
        constexpr std::int64_t INTERVAL_HOPPER_MS = 400;
        constexpr std::int64_t INTERVAL_BREW_MS = 12000;
        constexpr std::int64_t INTERVAL_FLOW_MS = 500;
        constexpr std::int64_t INTERVAL_FIRE_MS = 300; // ember decay
        constexpr std::int64_t INTERVAL_SPAWNER_MS = 15000;
        constexpr std::int64_t INTERVAL_TORCH_MS = 2000;
        constexpr std::int64_t GROUND_LIFETIME_MS = 5 * 60 * 1000; // 5 min
        constexpr std::int64_t GROUND_MERGE_WINDOW_MS = 60 * 1000; // merge same-kind rows

        // Per-record bounds so a 5-min logout cannot cause unbounded work.
        constexpr std::int64_t kMaxCatchupMsPerRecordPerTick = 5LL * 60LL * 1000LL;
        constexpr int kMaxStepsPerRecordPerTick = 256;
        constexpr std::size_t kMaxMutationsPerChunk = 32768;
        constexpr std::size_t kMaxRecordsPerChunk = 8192;
        constexpr std::size_t kMaxMemoEntries = 8192;

        // Unused right now (crop/flow sweeps land with later phase-9 drops):
        // [[maybe_unused]] keeps the object quiet in this batch.
        [[maybe_unused]] static bool kindIsCrop(std::uint16_t k)
        {
            return k == KIND_CROP || k == KIND_CARROTS;
        }
        // Unused right now (crop/flow sweeps land with later phase-9 drops):
        // [[maybe_unused]] keeps the object quiet in this batch.
        [[maybe_unused]] static bool kindIsFlow(std::uint16_t k)
        {
            return k == KIND_LAVA || k == KIND_WATER;
        }
        static std::int64_t stepIntervalFor(std::uint16_t k)
        {
            switch (k)
            {
            case KIND_FURNACE: return INTERVAL_FURNACE_MS;
            case KIND_LIT_FURNACE: return INTERVAL_LIT_FURNACE_MS;
            case KIND_CROP: case KIND_CARROTS: return INTERVAL_CROP_MS;
            case KIND_DISPENSER: case KIND_HOPPER: return INTERVAL_DISPENSER_MS;
            case KIND_BREWING_STAND: return INTERVAL_BREW_MS;
            case KIND_LAVA: case KIND_WATER: return INTERVAL_FLOW_MS;
            case KIND_FIRE: return INTERVAL_FIRE_MS;
            case KIND_SPAWNER: return INTERVAL_SPAWNER_MS;
            case KIND_TORCH: return INTERVAL_TORCH_MS;
            case KIND_GROUND: return GROUND_LIFETIME_MS;
            default: return 0;
            }
        }

        static bool isTickableBlock(std::uint16_t combinedId)
        {
            const std::uint16_t id = combinedId >> 4;
            switch (id)
            {
            case KIND_FURNACE: case KIND_LIT_FURNACE:
            case KIND_CROP: case KIND_CARROTS:
            case KIND_DISPENSER: case KIND_HOPPER:
            case KIND_BREWING_STAND:
            case KIND_LAVA: case KIND_WATER:
            case KIND_FIRE: case KIND_SPAWNER: case KIND_TORCH:
                return true;
            default:
                return false;
            }
        }
    }

    ProcessingTick::ProcessingTick() = default;

    void ProcessingTick::init(Storage::Database *db)
    {
        m_db = db;
    }

    // ---- Neighbour-value memo -------------------------------------------
    // A small per-pass table so adjacency tests inside one step (e.g. redstone
    // wiring feeding burnables, lava flowing to a neighbouring cell) do not
    // re-read the same cell from SQLite over and over. Cleared by
    // clearNeighbourMemo() at the start of every processChunk() wave.
    namespace
    {
        std::mutex memoMutex;
        // Key = chunk key + chunk-local coord; value = current uint16 combined
        // id at that absolute world cell.
        std::unordered_map<std::uint64_t, std::uint16_t> memoTable;

        // Pack (worldId, plane-cx, plane-cz, relX, relY, relZ) into a 64-bit
        // key. worldId + plane c* range well within the reserved bit widths
        // for typical play -- chunk coords wrap at 2^21 = 2M for MC18 maps,
        // and relY is 8-bit absolute, relX/relZ 4-bit each.
        static inline std::uint64_t memoKey(std::int32_t worldId,
                                            std::int32_t cx,
                                            std::int32_t cz,
                                            int relX,
                                            std::int16_t relY,
                                            int relZ)
        {
            const std::uint64_t w = static_cast<std::uint64_t>(worldId + 32768) & 0xFFFFull;
            const std::uint64_t a = static_cast<std::uint64_t>(cx + 2097152) & 0x3FFFFFull;
            const std::uint64_t b = static_cast<std::uint64_t>(cz + 2097152) & 0x3FFFFFull;
            const std::uint64_t x = static_cast<std::uint64_t>(relX & 0xF);
            const std::uint64_t y = static_cast<std::uint64_t>(relY & 0x1FF);
            const std::uint64_t z = static_cast<std::uint64_t>(relZ & 0xF);
            // Layout: [w:17][a:23][b:23][y:9][z:4][x:4] = 80 bits (wrap into 64).
            return (w * 131071ull) ^ (a * 8191ull) ^ (b * 4093ull) ^
                   (y << 8ull) ^ (z << 4ull) ^ x;
        }
    }

    void ProcessingTick::clearNeighbourMemo()
    {
        std::lock_guard<std::mutex> lk(memoMutex);
        memoTable.clear();
    }

    uint16_t ProcessingTick::memoCell(std::int32_t worldId,
                                      Storage::WorldStorage &plane,
                                      int cx,
                                      int cz,
                                      int relX,
                                      int relY,
                                      int relZ)
    {
        const auto key = memoKey(worldId, cx, cz, relX, static_cast<std::int16_t>(relY), relZ);
        std::lock_guard<std::mutex> lk(memoMutex);
        auto it = memoTable.find(key);
        if (it != memoTable.end())
            return it->second;
        // Cache miss: read via WorldStorage (its own staged overlay already
        // folds DirtyWriter stage/ground drops). Cap the memo so the table
        // does not grow indefinitely between waves.
        const std::uint16_t v = plane.getBlockAt(cx, cz, relX, relY, relZ);
        if (memoTable.size() < kMaxMemoEntries)
            memoTable.emplace(key, v);
        return v;
    }

    // ---- seedChunk -------------------------------------------------------
    // Populate the empty Chunk with persisted tick states, ground items, and
    // a bounded set of tickables discovered by getChunkBlocks. Called by
    // ChunkManager::getActiveChunkFromCache on first creation. Does NOT
    // stage anything to SQLite -- Chunk::lastTickMs starts at 0 so a chunk
    // that just entered the view will run catch-up at its first tickActiveChunks
    // pass with the same step-budget cap the processChunk enforces.
    void ProcessingTick::seedChunk(Storage::WorldStorage &plane,
                                    Storage::Database *db,
                                    std::int32_t worldId,
                                    std::int32_t cx,
                                    std::int32_t cz,
                                    Chunk &chunk,
                                    std::int64_t nowMs)
    {
        auto &dirty = Storage::DirtyWriter::getInstance();
        const std::uint32_t curEpoch = dirty.epoch(worldId, cx, cz);
        chunk.setLastTickMs(0);

        // 1) Restore persisted tick_states for this chunk.
        std::vector<Storage::Database::TickStateRow> tickRows;
        db->loadTickStates(worldId, cx, cz, tickRows);
        for (const auto &row : tickRows)
        {
            TickRecord rec;
            rec.relX = static_cast<std::int16_t>(row.relX);
            rec.relY = static_cast<std::int16_t>(row.y);
            rec.relZ = static_cast<std::int16_t>(row.relZ);
            rec.kind = static_cast<std::uint16_t>(row.tickKind);
            rec.state = static_cast<std::uint16_t>(row.state);
            rec.aux = static_cast<std::uint16_t>(row.aux);
            rec.nextTickMs = row.nextTickMs;
            rec.epoch = curEpoch;
            if (rec.nextTickMs < nowMs)
            {
                // Clamp catch-up window so a 5-min logout does not queue
                // millions of steps; processChunk fast-forwards at most
                // kMaxCatchupMsPerRecordPerTick from the current time.
                if (nowMs - rec.nextTickMs > kMaxCatchupMsPerRecordPerTick)
                    rec.nextTickMs = nowMs - kMaxCatchupMsPerRecordPerTick;
            }
            chunk.insert(rec);
        }

        // 2) Restore persisted ground_items as KIND_GROUND records (itemId
        // stored in `state`, count in `aux`, and the death absolute-time
        // in nextTickMs). The engine lets each such record expire naturally
        // when nextTickMs <= nowMs.
        std::vector<Storage::Database::GroundItemRow> groundRows;
        db->loadGroundItems(worldId, cx, cz, groundRows);
        for (const auto &row : groundRows)
        {
            TickRecord rec;
            rec.relX = static_cast<std::int16_t>(row.relX);
            rec.relY = static_cast<std::int16_t>(row.y);
            rec.relZ = static_cast<std::int16_t>(row.relZ);
            rec.kind = KIND_GROUND;
            rec.state = static_cast<std::uint16_t>(row.itemId);
            rec.aux = static_cast<std::uint16_t>(row.itemCount);
            rec.nextTickMs = row.spawnMs + row.lifetimeMs;
            rec.epoch = curEpoch;
            chunk.insert(rec);
        }

        // 3) Restore last processed tick per chunk so a fresh login resumes
        // the sim at the position of the last save rather than replaying.
        std::int64_t lastTickMs = 0;
        if (db->loadChunkTick(worldId, cx, cz, lastTickMs))
            chunk.setLastTickMs(lastTickMs);

        // 4) Bounded discovery scan for tickables not already covered by
        // persisted rows. getChunkBlocks reads the chunk exactly once via
        // WorldStorage::getChunkBlocks (which already folds the staged
        // overlay), and iterates only over the non-air cells in the LZ4
        // stream -- not the full 18*16*16 volume. Cap discovered count at
        // kMaxMutationsPerChunk to respect the same budget as processChunk.
        std::size_t discovered = 0;
        auto blocks = plane.getChunkBlocks(cx, cz);
        for (const auto &kv : blocks)
        {
            if (discovered >= kMaxMutationsPerChunk)
                break;
            if (!isTickableBlock(kv.second))
                continue;
            const std::uint64_t key =
                Storage::DirtyWriter::packCoord(kv.first.relX, kv.first.relY, kv.first.relZ);
            if (chunk.find(key) != nullptr)
                continue; // already restored from tick_states -- authority
            TickRecord rec;
            rec.relX = static_cast<std::int16_t>(kv.first.relX);
            rec.relY = static_cast<std::int16_t>(kv.first.relY);
            rec.relZ = static_cast<std::int16_t>(kv.first.relZ);
            rec.kind = static_cast<std::uint16_t>(kv.second >> 4);
            rec.state = 0;
            rec.aux = 0;
            rec.nextTickMs = nowMs + stepIntervalFor(rec.kind);
            rec.epoch = curEpoch;
            chunk.insert(rec);
            ++discovered;
        }
    }

    // ---- processChunk ----------------------------------------------------
    // Runs one tick pass for a Chunk. `nowMs` is the absolute unix-time
    // millisecond value Server.cpp feeds every 50 ms (20 Hz master loop).
    // The engine walks Chunk::records(), fast-forwards each record whose
    // nextTickMs has drifted behind by up to 5 real minutes, and applies
    // per-step side-effects (block flips, container rows, ground drops)
    // through DirtyWriter staging.
    void ProcessingTick::processChunk(Storage::WorldStorage &plane,
                                       Storage::Database *db,
                                       std::int32_t worldId,
                                       std::int32_t cx,
                                       std::int32_t cz,
                                       Chunk &chunk,
                                       std::int64_t nowMs)
    {
        if (chunk.recordCount() == 0)
            return;

        clearNeighbourMemo();

        auto &dirty = Storage::DirtyWriter::getInstance();
        const std::uint32_t curEpoch = dirty.epoch(worldId, cx, cz);
        std::size_t writes = 0;
        bool doCropGrow = true;
        bool doFireTick = true;
        bool enableLavaFlow = true;
        if (db)
        {
            bool tmp = true;
            if (GameRules::getInstance().getBool("growCrops", tmp))
                doCropGrow = tmp;
            tmp = true;
            if (GameRules::getInstance().getBool("doFireTick", tmp))
                doFireTick = tmp;
            tmp = true;
            if (GameRules::getInstance().getBool("enableLavaFlow", tmp))
                enableLavaFlow = tmp;
        }

        // Whether this chunk is allowed to keep stage/ground writes for
        // this pass. We bail out past kMaxMutationsPerChunk writes so a
        // chunk with a huge pending set still finishes in bounded time.
        std::vector<TickCoordKey> toDrop;
        toDrop.reserve(16);

        for (auto &kv : chunk.records())
        {
            TickRecord &rec = kv.second;

            // Revalidate authority: if DirtyWriter bumped the epoch for
            // this chunk (either by a container-row stage or a cell stage)
            // since our last read, a player/placer mutated the underlying
            // block or a linked container, so any cached interpretation
            // we are carrying is stale. Re-read the block through the
            // staged overlay and drop the record if the block changed or
            // was removed.
            if (rec.epoch != curEpoch)
            {
                std::uint16_t stagedValue = 0;
                if (!dirty.peekCell(worldId, cx, cz, rec.relX, rec.relY, rec.relZ, stagedValue))
                    stagedValue = plane.getBlockAt(cx, cz, rec.relX, rec.relY, rec.relZ);
                if (rec.kind != KIND_GROUND)
                {
                    const std::uint16_t blockId = stagedValue >> 4;
                    if (blockId == 0 || blockId != rec.kind)
                    {
                        toDrop.push_back(kv.first);
                        continue;
                    }
                }
                else
                {
                    Storage::DirtyWriter::StagedGroundRow probe;
                    if (dirty.peekGround(worldId, cx, cz, rec.relX, rec.relY, rec.relZ, probe))
                    {
                        if (probe.remove)
                        {
                            toDrop.push_back(kv.first);
                            continue;
                        }
                        rec.state = probe.itemId;
                        rec.aux = probe.count;
                        rec.nextTickMs = probe.spawnMs + probe.lifetimeMs;
                    }
                }
                rec.epoch = curEpoch;
            }

            if (rec.nextTickMs > nowMs)
                continue; // not yet due

            const std::int64_t interval = stepIntervalFor(rec.kind);
            if (interval <= 0)
                continue;

            // Fast-forward window capped to 5 real minutes of sim so a
            // long logout resumes in bounded work. Then cap step count
            // to kMaxStepsPerRecordPerTick so a chunk with a huge pending
            // backlog cannot starve other chunks in the same loop iter.
            std::int64_t effectiveStart = nowMs - kMaxCatchupMsPerRecordPerTick;
            if (rec.nextTickMs < effectiveStart)
                rec.nextTickMs = effectiveStart;
            int steps = 0;
            while (steps < kMaxStepsPerRecordPerTick && nowMs >= rec.nextTickMs)
            {
                if (writes >= kMaxMutationsPerChunk)
                    break;
                const std::int64_t stepAt = rec.nextTickMs;
                bool produced = false;
                switch (rec.kind)
                {
                case KIND_FURNACE:
                case KIND_LIT_FURNACE:
                {
                    // state 0 = idle (waiting for load), state 1 = cooking,
                    // state 2 = done -> unlit (state = 0, blockId -> FURNACE).
                    // The item queued for delivery is in aux (item id).
                    if (rec.kind == KIND_FURNACE && rec.state == 0)
                    {
                        // Nothing to do until the player stages an INSERT
                        // row for the linked container. Do not consume.
                        produced = false;
                    }
                    else if (rec.kind == KIND_FURNACE && rec.state == 1)
                    {
                        rec.kind = KIND_LIT_FURNACE;
                        produced = true;
                        // Stage the block flip so peekCell on the next
                        // revalidation returns LIT_FURNACE and the record
                        // continues to match by blockId.
                        if (writes < kMaxMutationsPerChunk)
                        {
                            dirty.stageCell(worldId, cx, cz, rec.relX, rec.relY, rec.relZ,
                                            static_cast<std::uint16_t>((KIND_LIT_FURNACE << 4) | 0xF),
                                            nowMs);
                            ++writes;
                        }
                    }
                    else if (rec.kind == KIND_LIT_FURNACE)
                    {
                        if (rec.state == 1 && stepAt >= rec.nextTickMs)
                        {
                            rec.state = 0;
                            produced = true;
                            // Emit a ground item for the cooked output via
                            // staging. Minecraft puts cooked food on the
                            // tile adjacent -- we put it on the tile above
                            // for simplicity (relY+1 in world absolute).
                            if (rec.aux != 0)
                            {
                                std::int16_t gy = static_cast<std::int16_t>(rec.relY + 1);
                                dirty.stageGround(worldId, cx, cz, rec.relX, gy, rec.relZ,
                                                 rec.aux, 1, 0, stepAt, GROUND_LIFETIME_MS, nowMs);
                                ++writes;
                            }
                        }
                        else if (rec.state == 0)
                        {
                            // Burn-out -> block reverts to FURNACE id 61.
                            produced = true;
                        }
                    }
                    break;
                }
                case KIND_CROP:
                case KIND_CARROTS:
                {
                    if (!doCropGrow) break;
                    // state = growth stage (0..7). Block id is the crop id.
                    if (rec.state < 7)
                    {
                        ++rec.state;
                        produced = true;
                        // Stage a cell write for the meta-encoded crop
                        // progress so chunk re-saves / network streams see
                        // the growth. Combined id = (cropBlockId << 4) | meta
                        // where meta = state & 0xF.
                        if (writes < kMaxMutationsPerChunk)
                        {
                            const std::uint16_t combinedId =
                                static_cast<std::uint16_t>((rec.kind << 4) | (rec.state & 0xF));
                            dirty.stageCell(worldId, cx, cz, rec.relX, rec.relY, rec.relZ,
                                            combinedId, nowMs);
                            ++writes;
                        }
                    }
                    break;
                }
                case KIND_DISPENSER:
                case KIND_HOPPER:
                {
                    // Minecraft's seed/produce dispensers hold a pending
                    // output count in `state` (from a preceding INSERT
                    // row). Each step dispenses one and decrements; the
                    // ground item that materialises on the tile below is
                    // staged as a KIND_GROUND record using MC18ItemIds so
                    // save-on-logout reload sees the item on the ground.
                    if (rec.state == 0)
                    {
                        produced = false;
                        break;
                    }
                    const std::uint16_t itemId = static_cast<std::uint16_t>(rec.aux);
                    if (rec.state > 0)
                    {
                        --rec.state;
                        if (itemId != 0 && writes < kMaxMutationsPerChunk)
                        {
                            dirty.stageGround(worldId, cx, cz, rec.relX,
                                              static_cast<std::int16_t>(rec.relY - 1),
                                              rec.relZ,
                                              itemId, 1, 0, stepAt, GROUND_LIFETIME_MS, nowMs);
                            ++writes;
                        }
                        produced = true;
                    }
                    break;
                }
                case KIND_BREWING_STAND:
                {
                    // aux = batch counter (0..4). state = 0 idle, 1 brewing.
                    if (rec.state == 1)
                    {
                        if (rec.aux < 4)
                        {
                            ++rec.aux;
                            produced = true;
                        }
                        else
                        {
                            rec.state = 0;
                            rec.aux = 0;
                            produced = true;
                        }
                    }
                    break;
                }
                case KIND_LAVA:
                case KIND_WATER:
                {
                    if (!enableLavaFlow) break;
                    // Flow propagation: try one neighbour per step and
                    // stage a cell if the neighbour is AIR. Keep it
                    // bounded to one write per record per tick to avoid
                    // flooding. Minecraft uses a random-flow pick; we
                    // cycle through 4 directions per step so the effect
                    // is deterministic (catch-up-friendly).
                    const int dirIdx = (steps + (kv.first & 0x3)) & 0x3;
                    int nRelX = rec.relX, nRelZ = rec.relZ;
                    switch (dirIdx)
                    {
                    case 0: nRelX = rec.relX + 1; break;
                    case 1: nRelX = rec.relX - 1; break;
                    case 2: nRelZ = rec.relZ + 1; break;
                    case 3: nRelZ = rec.relZ - 1; break;
                    }
                    // Neighbour may cross a chunk boundary; only process
                    // if it lands in this chunk. ChunkManager keeps only
                    // the current-plane view so cross-chunk flow is left
                    // to the neighbour's own tick when it becomes active
                    // (which the 5-min catch-up window covers).
                    if (nRelX >= 0 && nRelX < 16 && nRelZ >= 0 && nRelZ < 16)
                    {
                        const std::uint16_t n = memoCell(worldId, plane, cx, cz,
                                                        nRelX, rec.relY, nRelZ);
                        if ((n >> 4) == Blocks18::AIR && writes < kMaxMutationsPerChunk)
                        {
                            const std::uint16_t flowCombined =
                                static_cast<std::uint16_t>((rec.kind << 4) | 0x1);
                            dirty.stageCell(worldId, cx, cz, nRelX, rec.relY, nRelZ,
                                            flowCombined, nowMs);
                            ++writes;
                            produced = true;
                        }
                    }
                    break;
                }
                case KIND_FIRE:
                {
                    if (!doFireTick) break;
                    // Decay: burn state (aux) counts down 4 steps, then
                    // cell drops to AIR via dropCell.
                    if (rec.aux == 0) rec.aux = 4;
                    --rec.aux;
                    if (rec.aux == 0)
                    {
                        dirty.stageCell(worldId, cx, cz, rec.relX, rec.relY, rec.relZ, 0, nowMs);
                        ++writes;
                        toDrop.push_back(kv.first);
                    }
                    produced = true;
                    break;
                }
                case KIND_SPAWNER:
                {
                    // aux = remaining spawn budget. Server-side spawns are
                    // a stubbed no-op (no entity subsystem yet) -- the
                    // record keeps its schedule so the /gamerule entity
                    // config remains consistent across logins.
                    produced = true;
                    break;
                }
                case KIND_TORCH:
                {
                    // Torch burns out after 300 s (aux counts down seconds
                    // in 2-s steps). When exhausted, drop the cell.
                    if (rec.aux == 0) rec.aux = 150;
                    --rec.aux;
                    if (rec.aux == 0)
                    {
                        dirty.stageCell(worldId, cx, cz, rec.relX, rec.relY, rec.relZ, 0, nowMs);
                        ++writes;
                        toDrop.push_back(kv.first);
                    }
                    produced = true;
                    break;
                }
                case KIND_GROUND:
                {
                    // Merge window is handled at applyContainerBatch /
                    // applyGroundBatch (SQLite) side. The engine just
                    // expires the record when its lifetime hits zero.
                    if (stepAt >= rec.nextTickMs)
                    {
                        dirty.dropGround(worldId, cx, cz, rec.relX, rec.relY, rec.relZ,
                                         rec.state, nowMs);
                        ++writes;
                        toDrop.push_back(kv.first);
                        produced = true;
                    }
                    break;
                }
                default:
                    break;
                }
                if (!produced)
                    break;
                rec.nextTickMs += interval;
                ++steps;
            }

            // Persist the resulting tick row when the record is still
            // live and it produced observable state.
            if (rec.kind != KIND_GROUND)
            {
                dirty.stageTick(worldId, cx, cz, rec.relX, rec.relY, rec.relZ,
                                rec.kind, rec.state, rec.aux, rec.nextTickMs, nowMs);
            }
        }

        // Drop stale records from the in-memory TickRecordMap and stage
        // their corresponding tick/ground deletes to SQLite through
        // DirtyWriter. This is the single-authority step that removes
        // the "block state / container state change invalidates stale
        // records" case the user described.
        for (auto key : toDrop)
        {
            auto it = chunk.records().find(key);
            if (it == chunk.records().end())
                continue;
            int rx, ry, rz;
            Storage::DirtyWriter::unpackCoord(key, rx, ry, rz);
            if (it->second.kind == KIND_GROUND)
                dirty.dropGround(worldId, cx, cz, rx, ry, rz,
                                 it->second.state, nowMs);
            else
                dirty.dropTick(worldId, cx, cz, rx, ry, rz, it->second.kind, nowMs);
            chunk.erase(key);
        }

        chunk.setLastTickMs(nowMs);
    }

} // namespace LightweightMC::World
