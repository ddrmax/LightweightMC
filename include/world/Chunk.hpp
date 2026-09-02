#pragma once
#include "world/Block.hpp"
#include <cstdint>
#include <vector>

namespace LightweightMC::World
{

    enum class ChunkState
    {
        ACTIVE,
        FROZEN,
        UNLOADED
    };

    struct ChunkSection
    {
        Block blocks[4096];
    };

    class Chunk
    {
    private:
        int32_t m_x, m_z;
        ChunkState m_state{ChunkState::UNLOADED};
        uint64_t m_lastUnloadTimestamp{0};
        ChunkSection m_sections[16];

    public:
        Chunk(int32_t x, int32_t z);

        void setState(ChunkState state) { m_state = state; }
        ChunkState getState() const { return m_state; }

        int32_t getX() const { return m_x; }
        int32_t getZ() const { return m_z; }

        void catchup(uint64_t currentTimestamp);
    };

} // namespace LightweightMC::World