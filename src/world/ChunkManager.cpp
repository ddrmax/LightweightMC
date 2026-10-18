// src/world/ChunkManager.cpp -- multiworld view ring keyed by PlaneChunkKey
// (worldId, cx, cz) so the same chunk coordinate on different planes (Overworld
// id 0, Nether id -1, End id 1, custom >=101) stays separately resident and
// separately tickable. Every active Chunk owns a TickRecordMap that
// ProcessingTick seeds from persisted SQLite rows on first view entry and
// advances at every tickActiveChunks(nowMs) pass.
//
// Server::start's fixed 20 Hz accumulator is the sole driver of tickActiveChunks;
// ChunkManager::updatePlayerPosition re-centres one plane's view box on chunk
// crossings (WorldManager::updateChunksAroundPlayer invokes it per session) and
// flushes evicted chunks' staged rows out to SQLite before dropping them.
#include "world/ChunkManager.hpp"
#include "world/Chunk.hpp"
#include "world/ProcessingTick.hpp"
#include "storage/DirtyWriter.hpp"
#include "storage/WorldStorage.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace LightweightMC::World
{
    void ChunkManager::registerPlane(std::int32_t worldId, Storage::WorldStorage *plane)
    {
        if (!plane)
            return;
        for (auto &p : m_planes)
            if (p.first == worldId)
            {
                p.second = plane;
                return;
            }
        m_planes.emplace_back(worldId, plane);
    }

    Storage::WorldStorage *ChunkManager::findPlane(std::int32_t worldId) const
    {
        for (auto &p : m_planes)
            if (p.first == worldId)
                return p.second;
        return nullptr;
    }

    // ------------------------------------------------------------------
    // Update one plane's view box around a player position (chunk units)
    // and force evicted chunks' staged rows out to SQLite before dropping
    // them. Chunks on other planes are left untouched so a /gotor teleport
    // into Nether/End/custom immediately sees that plane's terrain without
    // clobbering the Overworld view other sessions may be holding.
    // ------------------------------------------------------------------
    void ChunkManager::updatePlayerPosition(std::int32_t worldId,
                                            std::int32_t pX,
                                            std::int32_t pZ,
                                            std::int64_t nowMs)
    {
        Storage::WorldStorage *plane = findPlane(worldId);
        if (!plane)
            return;

        const std::int64_t farR = RENDER_DISTANCE + 3;
        const std::int64_t farR2 = farR * farR;
        for (auto it = m_loadedChunks.begin(); it != m_loadedChunks.end();)
        {
            if (it->first.worldId != worldId)
            {
                ++it;
                continue;
            }
            const std::int64_t dx = static_cast<std::int64_t>(it->first.cx) - pX;
            const std::int64_t dz = static_cast<std::int64_t>(it->first.cz) - pZ;
            if (dx * dx + dz * dz > farR2)
            {
                Storage::DirtyWriter::getInstance().flushChunk(
                    it->first.worldId, it->first.cx, it->first.cz, "view_exit");
                it = m_loadedChunks.erase(it);
            }
            else
            {
                ++it;
            }
        }

        const std::int64_t r2 = static_cast<std::int64_t>(RENDER_DISTANCE) * RENDER_DISTANCE;
        Storage::Database *db = ProcessingTick::getInstance().database();
        for (int dz = -RENDER_DISTANCE; dz <= RENDER_DISTANCE; ++dz)
        {
            for (int dx = -RENDER_DISTANCE; dx <= RENDER_DISTANCE; ++dx)
            {
                if (static_cast<std::int64_t>(dx) * dx +
                    static_cast<std::int64_t>(dz) * dz > r2)
                    continue;
                const int cx = pX + dx;
                const int cz = pZ + dz;
                PlaneChunkKey key{worldId, cx, cz};
                if (m_loadedChunks.find(key) != m_loadedChunks.end())
                    continue;
                Chunk chunk(cx, cz);
                ProcessingTick::seedChunk(*plane, db, worldId, cx, cz, chunk, nowMs);
                m_loadedChunks.emplace(std::move(key), std::move(chunk));
            }
        }
    }

    // ------------------------------------------------------------------
    // The world-processing tick pass. Server::start's fixed 20 Hz
    // accumulator calls this every 50 ms with absolute unix-ms; we
    // iterate only the Chunks in every live plane's m_loadedChunks so
    // per-tick work stays proportional to render distance^2 summed
    // across live planes rather than total world volume.
    //
    // After the processChunk sweep, DirtyWriter::flushDue sweeps staged
    // buckets older than the idle threshold (300 s default) so chunks
    // that stayed in view for hours still persist their staged rows on
    // a schedule rather than waiting for a view exit.
    // ------------------------------------------------------------------
    void ChunkManager::tickActiveChunks(std::int64_t nowMs)
    {
        for (auto &kv : m_loadedChunks)
        {
            Storage::WorldStorage *plane = findPlane(kv.first.worldId);
            if (!plane)
                continue;
            ProcessingTick::processChunk(*plane,
                                         ProcessingTick::getInstance().database(),
                                         kv.first.worldId, kv.first.cx, kv.first.cz,
                                         kv.second, nowMs);
        }
        Storage::DirtyWriter::getInstance().flushDue(nowMs);
    }

} // namespace LightweightMC::World
