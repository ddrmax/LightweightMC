#include "world/ChunkManager.hpp"

namespace LightweightMC::World
{

    void ChunkManager::updatePlayerPosition(int32_t pX, int32_t pZ)
    {
        for (int x = -RENDER_DISTANCE; x <= RENDER_DISTANCE; ++x)
        {
            for (int z = -RENDER_DISTANCE; z <= RENDER_DISTANCE; ++z)
            {
                auto key = std::make_pair(pX + x, pZ + z);
                if (!m_loadedChunks.contains(key))
                {
                    Chunk newChunk(pX + x, pZ + z);
                    newChunk.setState(ChunkState::ACTIVE);
                    m_loadedChunks.emplace(key, newChunk);
                }
            }
        }
    }

    void ChunkManager::tickActiveChunks()
    {
        for (auto &[pos, chunk] : m_loadedChunks)
        {
            if (chunk.getState() == ChunkState::ACTIVE)
            {

                // Localized real-time ticks
            }
        }
    }

} // namespace LightweightMC::World