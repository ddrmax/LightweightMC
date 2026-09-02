#include "world/Chunk.hpp"
#include "core/Logger.hpp"

namespace LightweightMC::World
{

    Chunk::Chunk(int32_t x, int32_t z) : m_x(x), m_z(z) {}

    void Chunk::catchup(uint64_t currentTimestamp)
    {
        if (m_lastUnloadTimestamp == 0)
            return;

        uint64_t deltaSec = currentTimestamp - m_lastUnloadTimestamp;
        if (deltaSec > 0)
        {
            // Application of Catch-up (ovens, cultures, etc.)
            Core::Logger::info("Catch-up executed on Chunk!");
        }
    }

} // namespace LightweightMC::World