#pragma once
#include "world/Chunk.hpp"
#include <unordered_map>
#include <utility>

namespace LightweightMC::World {

struct ChunkHash {
    std::size_t operator()(const std::pair<int32_t, int32_t>& p) const {
        return std::hash<int32_t>()(p.first) ^ (std::hash<int32_t>()(p.second) << 1);
    }
};

class ChunkManager {
private:
    std::unordered_map<std::pair<int32_t, int32_t>, Chunk, ChunkHash> m_loadedChunks;
    static constexpr int RENDER_DISTANCE = 12; // 12 Active Chunks

public:
    ChunkManager() = default;

    void updatePlayerPosition(int32_t playerChunkX, int32_t playerChunkZ);
    void tickActiveChunks();
};

} // namespace LightweightMC::World