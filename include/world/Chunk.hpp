#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace LightweightMC::World
{
    enum class ChunkState
    {
        ACTIVE,
        FROZEN,
        UNLOADED
    };

    // ------------------------------------------------------------------
    // Sparse per-chunk processing record (0.0.15 phase 9).
    //
    // A chunk used to own a ChunkSection[16] copy of its voxels (4096
    // Blocks per section, ~131 KB per chunk of RAM) that duplicated -- and
    // therefore could silently drift from -- the LZ4 chunk rows the phase
    // 8b codec path made authoritative. It now owns only the blocks that
    // are actually *processing*: one TickRecord per furnace that is
    // cooking, per crop that is growing, per water/lava front that is
    // flowing, per dispenser that is feeding a container, per ground item
    // counting down to its despawn. Cost is per active process, not per
    // voxel, so a plane that generates 40 000 chunks costs no RAM until a
    // process inside them starts.
    //
    // `epoch` is the Storage::DirtyWriter chunk epoch the record was
    // validated against. Any block placement/removal or container-content
    // change bumps that epoch (DirtyWriter::touch), which marks the record
    // stale: the tick engine re-reads the authority (chunk cell + staged
    // overlay + container row) before stepping. That is what keeps a
    // dispenser/hopper from transferring items when the chest it feeds was
    // just broken or emptied, and what stops a removed furnace from
    // cooking -- the same synchronisation the real server/singleplayer has.
    // ------------------------------------------------------------------
    struct TickRecord
    {
        std::int16_t relX{0}, relY{0}, relZ{0}; // relY is absolute world Y
        std::uint16_t kind{0};                  // tick kind (see ProcessingTick.hpp)
        std::uint16_t state{0};                 // progress / stage / fuel state
        std::uint16_t aux{0};                   // secondary counter or link offset
        std::int64_t nextTickMs{0};             // absolute world ms the next step is due at
        std::uint32_t epoch{0};                 // DirtyWriter epoch it was derived at
    };

    // Chunk-local key for records; identical to Storage::DirtyWriter::packCoord
    // so a record and its staged row address the same cell.
    using TickCoordKey = std::uint32_t;

    using TickRecordMap = std::unordered_map<TickCoordKey, TickRecord>;

    class Chunk
    {
    private:
        int32_t m_x, m_z;
        ChunkState m_state{ChunkState::UNLOADED};
        uint64_t m_lastUnloadTimestamp{0};
        std::int64_t m_lastTickMs{0};
        std::uint32_t m_epoch{0};
        TickRecordMap m_tickRecords;
        // Source cells in a neighbouring chunk that currently drive progress
        // inside this chunk (lava/fire frontier, water flow, a cross-chunk
        // dispenser link). Encoded (neighbourLocalKey << 32) | localKey so the
        // frontier stays event-driven: ignitions/flows register the source
        // that fired rather than every tick rescanning 4096 cells.
        std::unordered_set<std::uint64_t> m_crossSources;

    public:
        Chunk(int32_t x, int32_t z);

        void setState(ChunkState state) { m_state = state; }
        ChunkState getState() const { return m_state; }

        int32_t getX() const { return m_x; }
        int32_t getZ() const { return m_z; }

        std::int64_t lastTickMs() const { return m_lastTickMs; }
        void setLastTickMs(std::int64_t ms) { m_lastTickMs = ms; }

        std::uint32_t epoch() const { return m_epoch; }
        void setEpoch(std::uint32_t e) { m_epoch = e; }

        TickRecord *find(TickCoordKey key);
        void insert(const TickRecord &record);
        void erase(TickCoordKey key);
        std::size_t recordCount() const { return m_tickRecords.size(); }
        TickRecordMap &records() { return m_tickRecords; }
        void clearRecords();

        // Re-entry after the chunk left the view: log/measure the world time
        // that elapsed while unloaded. Records carry absolute nextTickMs, so
        // the next step pass replays whatever was due in that window
        // (ovens/cultures catch up); catchup() itself only bookkeeps the
        // window so the replay stays bounded by the step budget.
        void catchup(uint64_t currentTimestamp);
    };

} // namespace LightweightMC::World
