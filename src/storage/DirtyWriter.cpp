#include "storage/DirtyWriter.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <tuple>

namespace LightweightMC::Storage
{
    // -------------------------------------------------------------
    // Key packing. Signed fields are offset into unsigned ranges so the
    // OR-composition never aliases: plane id 17 bits (0..65 535 after a
    // +32 768 bias), chunk axes 21 bits each (+524 288 bias, i.e. a
    // +/-524 288-chunk window = ~8.4 M blocks per axis). Planes beyond
    // that range fold into the same bucket, which is logged rather than
    // silently merged.
    // -------------------------------------------------------------
    std::uint64_t DirtyWriter::packKey(int planeId, int cx, int cz)
    {
        const std::int64_t p = static_cast<std::int64_t>(planeId) + 32768;
        const std::int64_t x = static_cast<std::int64_t>(cx) + 524288;
        const std::int64_t z = static_cast<std::int64_t>(cz) + 524288;
        if (p < 0 || p > 0xFFFF || x < 0 || x > 0xFFFFF || z < 0 || z > 0xFFFFF)
        {
            Core::Logger::warn("DirtyWriter: key out of packed range (plane=" + std::to_string(planeId) +
                               ", chunk=" + std::to_string(cx) + "," + std::to_string(cz) + ")");
        }
        return (static_cast<std::uint64_t>(p & 0xFFFF) << 42) |
               (static_cast<std::uint64_t>(x & 0xFFFFF) << 21) |
               static_cast<std::uint64_t>(z & 0xFFFFF);
    }

    // Chunk-local cell key: relY absolute 0..255 (8 bits), relZ/relX 0..15.
    std::uint32_t DirtyWriter::packCoord(int relX, int relY, int relZ)
    {
        const unsigned y = static_cast<unsigned>(relY < 0 ? 0 : (relY > 255 ? 255 : relY));
        const unsigned z = static_cast<unsigned>(relZ & 0x0F);
        const unsigned x = static_cast<unsigned>(relX & 0x0F);
        return (y << 8) | (z << 4) | x;
    }

    void DirtyWriter::unpackKey(std::uint64_t key, int &planeId, int &cx, int &cz)
    {
        planeId = static_cast<int>((key >> 42) & 0x1FFFF) - 32768;
        cx = static_cast<int>((key >> 21) & 0x1FFFFF) - 524288;
        cz = static_cast<int>(key & 0x1FFFFF) - 524288;
    }

    void DirtyWriter::unpackCoord(std::uint32_t key, int &relX, int &relY, int &relZ)
    {
        relX = static_cast<int>(key & 0x0F);
        relZ = static_cast<int>((key >> 4) & 0x0F);
        relY = static_cast<int>((key >> 8) & 0xFF);
    }

    namespace
    {
        // Assumes m_mutex is already held (public entry points lock, internal
        // helpers do not, so flushDue -> flushBucket cannot deadlock).
        DirtyWriter::StagedChunk *bucketForLocked(std::unordered_map<std::uint64_t, DirtyWriter::StagedChunk> &buckets,
                                                  std::uint64_t key, std::int64_t nowMs)
        {
            auto it = buckets.find(key);
            if (it == buckets.end())
            {
                DirtyWriter::StagedChunk fresh;
                fresh.firstDirtyMs = nowMs;
                it = buckets.emplace(key, fresh).first;
            }
            return &it->second;
        }

        bool isEmptyLocked(const DirtyWriter::StagedChunk &bucket)
        {
            return bucket.cells.empty() && bucket.containers.empty() && bucket.ticks.empty() && bucket.grounds.empty();
        }
    } // anonymous namespace

    void DirtyWriter::registerCellFlush(int planeId, CellFlushFn fn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cellFlushers[planeId] = std::move(fn);
    }

    void DirtyWriter::registerContainerFlush(int planeId, ContainerFlushFn fn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_containerFlushers[planeId] = std::move(fn);
    }

    void DirtyWriter::registerTickFlush(int planeId, TickFlushFn fn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tickFlushers[planeId] = std::move(fn);
    }

    void DirtyWriter::registerGroundFlush(int planeId, GroundFlushFn fn)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_groundFlushers[planeId] = std::move(fn);
    }

    void DirtyWriter::setIdleFlushMs(std::int64_t ms)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_idleFlushMs = ms > 0 ? ms : 1;
    }

    void DirtyWriter::setStagedWriteBudget(std::size_t writes)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_writeBudget = writes > 0 ? writes : 1;
    }

    void DirtyWriter::stageCell(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                std::uint16_t value, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        b->cells[packCoord(relX, relY, relZ)] = value;
        b->writes += 1;
    }

    void DirtyWriter::stageContainer(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                     std::uint16_t type, const std::vector<std::uint8_t> &blob,
                                     std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        StagedContainerRow row;
        row.relX = relX; row.relY = relY; row.relZ = relZ;
        row.type = type; row.blob = blob; row.remove = false;
        b->containers[packCoord(relX, relY, relZ)] = std::move(row);
        b->writes += 1;
    }

    void DirtyWriter::dropContainer(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                    std::uint16_t type, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        StagedContainerRow row;
        row.relX = relX; row.relY = relY; row.relZ = relZ;
        row.type = type; row.remove = true;
        b->containers[packCoord(relX, relY, relZ)] = std::move(row);
        b->writes += 1;
    }

    void DirtyWriter::stageTick(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                std::uint16_t kind, std::uint16_t state, std::uint16_t aux,
                                std::int64_t nextTickMs, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        StagedTickRow row;
        row.relX = relX; row.relY = relY; row.relZ = relZ;
        row.kind = kind; row.state = state; row.aux = aux;
        row.nextTickMs = nextTickMs; row.remove = false;
        b->ticks[packCoord(relX, relY, relZ)] = std::move(row);
        b->writes += 1;
    }

    void DirtyWriter::dropTick(int planeId, int cx, int cz, int relX, int relY, int relZ,
                               std::uint16_t kind, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        StagedTickRow row;
        row.relX = relX; row.relY = relY; row.relZ = relZ;
        row.kind = kind; row.remove = true;
        b->ticks[packCoord(relX, relY, relZ)] = std::move(row);
        b->writes += 1;
    }

    void DirtyWriter::stageGround(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                  std::int32_t itemId, std::int32_t count, std::int32_t damage,
                                  std::int64_t spawnMs, std::int64_t lifetimeMs, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        StagedGroundRow row;
        row.relX = relX; row.relY = relY; row.relZ = relZ;
        row.itemId = itemId; row.count = count; row.damage = damage;
        row.spawnMs = spawnMs; row.lifetimeMs = lifetimeMs; row.remove = false;
        b->grounds[packCoord(relX, relY, relZ)] = std::move(row);
        b->writes += 1;
    }

    void DirtyWriter::dropGround(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                 std::int32_t itemId, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        StagedGroundRow row;
        row.relX = relX; row.relY = relY; row.relZ = relZ;
        row.itemId = itemId; row.remove = true;
        b->grounds[packCoord(relX, relY, relZ)] = std::move(row);
        b->writes += 1;
    }

    void DirtyWriter::touch(int planeId, int cx, int cz, const char *reason, std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        StagedChunk *b = bucketForLocked(m_buckets, packKey(planeId, cx, cz), nowMs);
        ++b->epoch;
        (void)reason; // reason kept in the signature so callers can log what bumped it
    }

    std::uint32_t DirtyWriter::epoch(int planeId, int cx, int cz) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_buckets.find(packKey(planeId, cx, cz));
        return it == m_buckets.end() ? 0 : it->second.epoch;
    }

    bool DirtyWriter::peekCell(int planeId, int cx, int cz, int relX, int relY, int relZ,
                               std::uint16_t &outValue) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_buckets.find(packKey(planeId, cx, cz));
        if (it == m_buckets.end())
            return false;
        auto c = it->second.cells.find(packCoord(relX, relY, relZ));
        if (c == it->second.cells.end())
            return false;
        outValue = c->second;
        return true;
    }

    bool DirtyWriter::peekContainer(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                    StagedContainerRow &outRow) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_buckets.find(packKey(planeId, cx, cz));
        if (it == m_buckets.end())
            return false;
        auto c = it->second.containers.find(packCoord(relX, relY, relZ));
        if (c == it->second.containers.end())
            return false;
        outRow = c->second;
        return true;
    }

    bool DirtyWriter::peekTick(int planeId, int cx, int cz, int relX, int relY, int relZ,
                               StagedTickRow &outRow) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_buckets.find(packKey(planeId, cx, cz));
        if (it == m_buckets.end())
            return false;
        auto c = it->second.ticks.find(packCoord(relX, relY, relZ));
        if (c == it->second.ticks.end())
            return false;
        outRow = c->second;
        return true;
    }

    bool DirtyWriter::peekGround(int planeId, int cx, int cz, int relX, int relY, int relZ,
                                 StagedGroundRow &outRow) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_buckets.find(packKey(planeId, cx, cz));
        if (it == m_buckets.end())
            return false;
        auto c = it->second.grounds.find(packCoord(relX, relY, relZ));
        if (c == it->second.grounds.end())
            return false;
        outRow = c->second;
        return true;
    }

    std::size_t DirtyWriter::stagedChunks() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_buckets.size();
    }

    std::size_t DirtyWriter::stagedWrites() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::size_t total = 0;
        for (const auto &kv : m_buckets)
            total += kv.second.writes;
        return total;
    }

    // Defaulted at its declaration (see DirtyWriter.hpp line 175) via
    // an explicit definition here so the singleton getInstance() can
    // construct the static instance while the ctor stays private to
    // callers (only getInstance() fabricates a DirtyWriter).
    DirtyWriter::DirtyWriter() = default;

    void DirtyWriter::flushBucketLocked(std::uint64_t key, int planeId, int cx, int cz, const char *reason)
    {
        auto it = m_buckets.find(key);
        if (it == m_buckets.end())
            return;

        StagedChunk &bucket = it->second;
        if (!isEmptyLocked(bucket))
        {
            std::vector<StagedCell> cells;
            cells.reserve(bucket.cells.size());
            for (const auto &c : bucket.cells)
            {
                StagedCell cell;
                unpackCoord(c.first, cell.relX, cell.relY, cell.relZ);
                cell.value = c.second;
                cells.push_back(cell);
            }
            std::vector<StagedContainerRow> containers;
            containers.reserve(bucket.containers.size());
            for (auto &cv : bucket.containers)
                containers.push_back(cv.second);
            std::vector<StagedTickRow> ticks;
            ticks.reserve(bucket.ticks.size());
            for (auto &tv : bucket.ticks)
                ticks.push_back(tv.second);
            std::vector<StagedGroundRow> grounds;
            grounds.reserve(bucket.grounds.size());
            for (auto &gv : bucket.grounds)
                grounds.push_back(gv.second);

            auto cellIt = m_cellFlushers.find(planeId);
            if (cellIt != m_cellFlushers.end() && cellIt->second)
                cellIt->second(cx, cz, cells);
            else if (!cells.empty())
                Core::Logger::warn("DirtyWriter: no cell flusher for plane " + std::to_string(planeId) +
                                   " -- dropped " + std::to_string(cells.size()) + " staged cells of chunk " +
                                   std::to_string(cx) + "," + std::to_string(cz) + " (" + reason + ")");

            auto contIt = m_containerFlushers.find(planeId);
            if (contIt != m_containerFlushers.end() && contIt->second)
                contIt->second(cx, cz, containers);
            else if (!containers.empty())
                Core::Logger::warn("DirtyWriter: no container flusher for plane " + std::to_string(planeId) +
                                   " -- dropped " + std::to_string(containers.size()) + " staged rows");

            auto tickIt = m_tickFlushers.find(planeId);
            if (tickIt != m_tickFlushers.end() && tickIt->second)
                tickIt->second(cx, cz, ticks);
            else if (!ticks.empty())
                Core::Logger::warn("DirtyWriter: no tick flusher for plane " + std::to_string(planeId) +
                                   " -- dropped " + std::to_string(ticks.size()) + " staged tick rows");

            auto groundIt = m_groundFlushers.find(planeId);
            if (groundIt != m_groundFlushers.end() && groundIt->second)
                groundIt->second(cx, cz, grounds);
            else if (!grounds.empty())
                Core::Logger::warn("DirtyWriter: no ground flusher for plane " + std::to_string(planeId) +
                                   " -- dropped " + std::to_string(grounds.size()) + " staged ground rows");

            Core::Logger::debug("DirtyWriter: flushed chunk [" + std::to_string(cx) + "," + std::to_string(cz) +
                                "] plane " + std::to_string(planeId) + " (" + reason + "): " +
                                std::to_string(cells.size()) + " cells, " + std::to_string(containers.size()) +
                                " containers, " + std::to_string(ticks.size()) + " tick rows, " +
                                std::to_string(grounds.size()) + " ground rows");
        }

        m_buckets.erase(it);
    }

    bool DirtyWriter::flushChunk(int planeId, int cx, int cz, const char *reason)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::uint64_t key = packKey(planeId, cx, cz);
        auto it = m_buckets.find(key);
        if (it == m_buckets.end())
            return false;
        flushBucketLocked(key, planeId, cx, cz, reason);
        return true;
    }

    void DirtyWriter::flushDue(std::int64_t nowMs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::tuple<int, int, int, const char *>> due; // plane, cx, cz, reason
        for (const auto &kv : m_buckets)
        {
            if (isEmptyLocked(kv.second))
                continue;
            const bool idle = nowMs - kv.second.firstDirtyMs >= m_idleFlushMs;
            const bool overBudget = kv.second.writes >= m_writeBudget;
            if (!idle && !overBudget)
                continue;
            int planeId, cx, cz;
            unpackKey(kv.first, planeId, cx, cz);
            due.emplace_back(planeId, cx, cz, overBudget ? "staged-write budget" : "idle flush");
        }
        for (const auto &t : due)
            flushBucketLocked(packKey(std::get<0>(t), std::get<1>(t), std::get<2>(t)),
                              std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t));
    }

    void DirtyWriter::flushAll(const char *reason)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::tuple<int, int, int, const char *>> all;
        for (const auto &kv : m_buckets)
        {
            if (isEmptyLocked(kv.second))
                continue;
            int planeId, cx, cz;
            unpackKey(kv.first, planeId, cx, cz);
            all.emplace_back(planeId, cx, cz, reason);
        }
        for (const auto &t : all)
            flushBucketLocked(packKey(std::get<0>(t), std::get<1>(t), std::get<2>(t)),
                              std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t));
    }

} // namespace LightweightMC::Storage
