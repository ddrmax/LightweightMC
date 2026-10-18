#pragma once
#include "world/Chunk.hpp"
#include "storage/WorldStorage.hpp"
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LightweightMC::World
{
    struct ChunkHash
    {
        std::size_t operator()(const std::pair<int32_t, int32_t> &p) const
        {
            return std::hash<int32_t>()(p.first) ^ (std::hash<int32_t>()(p.second) << 1);
        }
    };

    // Plane-scoped chunk key: the same chunk coordinate exists in every plane,
    // so the view registry is keyed by (worldId, cx, cz) like the sparse row
    // families (tick_states / chunk_ticks / ground_items) it feeds.
    struct PlaneChunkKey
    {
        std::int32_t worldId{0}, cx{0}, cz{0};
        bool operator==(const PlaneChunkKey &o) const
        {
            return worldId == o.worldId && cx == o.cx && cz == o.cz;
        }
    };

    struct PlaneChunkHash
    {
        std::size_t operator()(const PlaneChunkKey &k) const
        {
            // Same 42/21/21 signed-range packing as Storage::DirtyWriter::packKey
            // so the two registries agree on chunk identity.
            const std::int64_t w = static_cast<std::int64_t>(k.worldId) + 32768;
            const std::int64_t x = static_cast<std::int64_t>(k.cx) + 524288;
            const std::int64_t z = static_cast<std::int64_t>(k.cz) + 524288;
            return std::hash<std::uint64_t>()(
                (static_cast<std::uint64_t>(w & 0xFFFF) << 42) |
                (static_cast<std::uint64_t>(x & 0xFFFFF) << 21) |
                static_cast<std::uint64_t>(z & 0xFFFFF));
        }
    };

    class ChunkManager
    {
    private:
        std::unordered_map<PlaneChunkKey, Chunk, PlaneChunkHash> m_loadedChunks;
        std::vector<std::pair<std::int32_t, Storage::WorldStorage *>> m_planes;
        static constexpr int RENDER_DISTANCE = 12; // 12 chunks of processing view

    public:
        ChunkManager() = default;

        // Registers a live plane (its SQLite authority) so the sparse
        // registries can read/write cells and seed records from chunk rows.
        void registerPlane(std::int32_t worldId, Storage::WorldStorage *plane);
        Storage::WorldStorage *findPlane(std::int32_t worldId) const;

        // View lifecycle for one plane around a player position (chunk units).
        // Brings the render box into the view (seeding sparse registries on
        // first entry from the persisted rows) and evicts chunks outside it,
        // forcing their staged rows out to SQLite on exit (the "forced-on-evict"
        // half of the staged write-back policy).
        void updatePlayerPosition(std::int32_t worldId, std::int32_t pX, std::int32_t pZ,
                                  std::int64_t nowMs);

        // The world-processing tick pass over every in-view chunk of every
        // registered plane (20 Hz, driven by Server::start's fixed accumulator)
        // plus the idle/budget flush of staged rows.
        void tickActiveChunks(std::int64_t nowMs);
    };

} // namespace LightweightMC::World
