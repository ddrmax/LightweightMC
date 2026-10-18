#include "world/Chunk.hpp"
#include "storage/DirtyWriter.hpp"
#include "core/Logger.hpp"

#include <string>

namespace LightweightMC::World
{
    Chunk::Chunk(int32_t x, int32_t z) : m_x(x), m_z(z) {}

    TickRecord *Chunk::find(TickCoordKey key)
    {
        auto it = m_tickRecords.find(key);
        return it == m_tickRecords.end() ? nullptr : &it->second;
    }

    void Chunk::insert(const TickRecord &record)
    {
        // Records and staged rows share the chunk-local key layout
        // (Storage::DirtyWriter::packCoord) so a record and the staged cell it
        // writes address the same coordinate.
        const TickCoordKey key = Storage::DirtyWriter::packCoord(record.relX, record.relY, record.relZ);
        m_tickRecords[key] = record;
    }

    void Chunk::erase(TickCoordKey key)
    {
        m_tickRecords.erase(key);
    }

    void Chunk::clearRecords()
    {
        m_tickRecords.clear();
        m_crossSources.clear();
    }

    // Re-entry after the chunk left the player's view. Records carry absolute
    // nextTickMs deadlines, so the next ProcessingTick pass replays every step
    // that became due while the chunk was unloaded (ovens finish, crops
    // advance, ground items expire) -- catch-up needs no per-voxel replay and
    // no re-scan, it is a bounded per-record budget inside processChunk().
    void Chunk::catchup(uint64_t currentTimestamp)
    {
        if (m_lastUnloadTimestamp == 0)
            return;

        const uint64_t deltaSec = currentTimestamp > m_lastUnloadTimestamp
                                      ? currentTimestamp - m_lastUnloadTimestamp
                                      : 0;
        if (deltaSec == 0)
            return;

        Core::Logger::debug("Catch-up executed on Chunk [" + std::to_string(m_x) + "," + std::to_string(m_z) +
                            "] after " + std::to_string(deltaSec) + "s idle (" +
                            std::to_string(m_tickRecords.size()) + " sparse record(s) eligible, " +
                            std::to_string(m_crossSources.size()) + " cross-chunk source(s) pending)");
    }

} // namespace LightweightMC::World
