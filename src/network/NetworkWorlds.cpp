// -----------------------------------------------------------------------------
// Multiworld / dimension-plane API (Fix 3).
//
// Owns the plumbing that lets a single server host multiple Minecraft-style
// dimension planes at once, indexed by PlayerSession::worldId. Standard IDs
// follow the vanilla pre-1.12 layout used since Minecraft 1.7:
//
//     0   = Overworld   (world.db)          -- vanilla default plane
//    -1   = Nether      (world-nether.db)   -- underworld
//     1   = End         (world-end.db)      -- deepest plane
//   100   = Aether      (world-aether.db)   -- special dimension
//
// IDs 101+ are allocated by the world_registry SQLite table in server_data.db
// (see src/storage/Database.cpp::registerWorld) for custom worlds created at
// runtime via /world create. The 100 Aether slot is deliberately placed in
// that same custom range so a future Minecraft Legends / Adventure crossover
// plane can claim another 100-series id without colliding with the fixed
// vanilla 0/-1/1 slots.
//
// NetworkManager::start() seeds the map with the three standard planes and,
// when [world] aether-active=true, the Aether special dimension at startup.
// m_worlds is a std::unordered_map<int32_t, std::unique_ptr<WorldStorage>>
// rather than a vector indexed by worldId because Nether's id is -1 --
// static_casting that to std::size_t before subscripting a vector is UB and
// crashed the server every time a player crossed into the underworld.
//
// This file implements the runtime API used by the rest of the codebase:
//   - getWorldStorage(worldId)   -- session-aware accessor (map lookup,
//                                    falls back to Overworld on miss)
//   - createWorld(name, style)   -- allocates a fresh id >=101, opens a fresh
//                                    SQLite file, pregenerates a small radius
//                                    so players spawning into the new plane
//                                    always have solid ground beneath them.
//   - lookupWorldId(name)        -- resolves the name to an id, or -2147483648
//   - setPlayerWorldId(name, id) -- in-place plane switch for a live session
// -----------------------------------------------------------------------------

#include "network/NetworkManager.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include "storage/Database.hpp"
#include "storage/WorldStorage.hpp"
#include "world/EntityManager.hpp"
#include "core/Logger.hpp"
#include "core/utils.hpp"
#include "network/Packet.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <sys/socket.h>

namespace LightweightMC::Network
{
    static Storage::GenStyle styleFromName(const std::string &s)
    {
        if (s == "superflat")
            return Storage::GenStyle::SUPERFLAT;
        if (s == "test-grid")
            return Storage::GenStyle::TEST_GRID;
        if (s == "nether")
            return Storage::GenStyle::NETHER;
        if (s == "aether")
            return Storage::GenStyle::AETHER;
        if (s == "end")
            return Storage::GenStyle::END;
        return Storage::GenStyle::OVERWORLD;
    }

    Storage::WorldStorage &NetworkManager::getWorldStorage(int32_t worldId)
    {
        auto it = m_worlds.find(worldId);
        if (it != m_worlds.end() && it->second)
            return *it->second;
        // Fall back to the primary plane (Overworld id 0) so callers that
        // have not yet populated session.worldId, or that reference a plane
        // deactivated via the [world] <name>-active=false flag, keep working.
        // Never returns nullptr.
        auto over = m_worlds.find(0);
        if (over != m_worlds.end() && over->second)
            return *over->second;
        // Absolute last resort: any live storage. Only reachable if the
        // operator explicitly disabled Overworld without NetworkManager::start
        // refusing (start currently aborts in that case).
        for (auto &kv : m_worlds)
            if (kv.second)
                return *kv.second;
        static Storage::WorldStorage fallback;
        return fallback;
    }

    int32_t NetworkManager::lookupWorldId(const std::string &worldName) const
    {
        if (worldName.empty())
            return -2147483648;
        // Standard planes are hard-registered by start(), but do the lookup via
        // the SQLite table so a rename / hot-swap of the standard planes still
        // works without recompiling the server.
        int32_t id = -2147483648;
        std::string style;
        if (m_database.lookupWorld(worldName, id, style))
            return id;
        return -2147483648;
    }

    bool NetworkManager::describeWorld(const std::string &worldName,
                                       int32_t &outId,
                                       std::string &outStyle,
                                       std::string &outDbPath) const
    {
        if (worldName.empty())
            return false;
        if (!m_database.lookupWorld(worldName, outId, outStyle))
            return false;
        // Fixed planes use the hard-coded per-plane SQLite files NetworkManager::start()
        // opens (see the id table in NetworkSocket.cpp); custom planes created
        // through createWorld() use "world-<safeName>.db" where safeName lower-cases
        // the name and replaces anything outside [A-Za-z0-9_-] with '_' so a rogue
        // name cannot smuggle a path traversal into the working directory.
        switch (outId)
        {
        case 0:   outDbPath = "world.db"; break;
        case -1:  outDbPath = "world-nether.db"; break;
        case 1:   outDbPath = "world-end.db"; break;
        case 100: outDbPath = "world-aether.db"; break;
        default:
        {
            std::string safeName;
            safeName.reserve(worldName.size());
            for (char c : worldName)
            {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
                    safeName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                else
                    safeName.push_back('_');
            }
            outDbPath = "world-" + safeName + ".db";
            break;
        }
        }
        return true;
    }

    int32_t NetworkManager::createWorld(const std::string &worldName, const std::string &style)
    {
        if (worldName.empty())
        {
            Core::Logger::error("createWorld: refusing to allocate an unnamed world");
            return -1;
        }

        // 1) Persist the mapping in server_data.db first so a crash between now
        //     and the WorldStorage::init does not leave us with a live-but-
        //     untracked storage.
        int32_t assignedId = -1;
        const std::string normalizedStyle = style.empty() ? std::string("overworld") : style;
        if (!m_database.registerWorld(worldName, normalizedStyle, assignedId))
        {
            Core::Logger::error("createWorld: unable to register world \"" + worldName +
                                "\" in world_registry");
            return -1;
        }

        // 2) Allocate a fresh WorldStorage instance using a per-world SQLite
        //    file. Use a name that is safe to embed in a filesystem path (alnum
        //    + underscore + dash) so a rogue /world create invocation cannot
        //    smuggle a path traversal into the working directory.
        std::string safeName;
        safeName.reserve(worldName.size());
        for (char c : worldName)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
                safeName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            else
                safeName.push_back('_');
        }
        const std::string dbPath = "world-" + safeName + ".db";

        // If an id was reused across restarts (the registry is idempotent), we
        // may already have a live WorldStorage for it -- return that instead of
        // double-allocating. Map lookup handles negative keys and sparse ids
        // naturally so the "already-live" fast path is now a single find().
        auto live = m_worlds.find(assignedId);
        if (live != m_worlds.end() && live->second)
        {
            Core::Logger::info("createWorld: world \"" + worldName +
                               "\" (id=" + std::to_string(assignedId) + ") already live");
            return assignedId;
        }

        auto storage = std::make_unique<Storage::WorldStorage>();
        if (!storage->init(dbPath))
        {
            Core::Logger::error("createWorld: unable to open SQLite storage " + dbPath);
            return -1;
        }

        // Mirror the current server's Overworld seed so a custom plane sits in
        // the same LCG stream as the primary plane. Preset from the requested
        // style; unknown style names fall back to OVERWORLD (see styleFromName).
        const Storage::WorldStorage *primary = nullptr;
        auto overIt = m_worlds.find(0);
        if (overIt != m_worlds.end())
            primary = overIt->second.get();
        storage->setWorldSeed(primary ? primary->getWorldSeed() : 0ULL);
        storage->setGenerationStyle(styleFromName(normalizedStyle));
        // storage->pregenerateWorld(12);

        m_worlds.emplace(assignedId, std::move(storage));

        Core::Logger::info("createWorld: registered \"" + worldName +
                           "\" (id=" + std::to_string(assignedId) + ", style=" + normalizedStyle +
                           ", db=" + dbPath + ")");
        return assignedId;
    }

    std::vector<std::string> NetworkManager::listWorldNames() const
    {
        std::vector<std::string> out;
        out.reserve(m_worlds.size());
        // Walk live in-memory planes only; registered-but-inactive planes
        // (active=false config flag) are intentionally omitted from /world
        // list because the server is not currently serving chunks for them.
        for (const auto &kv : m_worlds)
        {
            if (!kv.second)
                continue;
            std::string name;
            std::string style;
            if (m_database.lookupWorldById(kv.first, name, style) && !name.empty())
                out.push_back(name);
        }
        return out;
    }

    // ---- setPlayerWorldId ---------------------------------------------------
    // Live in-place plane switch for a currently-online player. The wire path:
    //   - Look up the online session by username.
    //   - Push the player to the surface of the new plane (uses the new plane's
    //     WorldStorage::findStandingFeetY so they land on real terrain).
    //   - Clear loadedChunks so the next updateChunksAroundPlayer streams
    //     fresh chunks from the new plane instead of stale chunk state from the
    //     previous one.
    //   - Send an S2C Position packet (0x08) so the client animates the jump
    //     and never walks through the walls it built on the old plane.
    //   - Persist the worldId change on the player's row so a relogin stays on
    //     the new plane.
    //
    // Returns false when the player isn't currently online (PLAY state) or the
    // target worldId isn't registered. Note: we require PLAY because a player
    // mid-login has no position yet to preserve / animate against. The worldId
    // map key is signed so the pre-1.12 Nether id (-1) is a legal target --
    // the previous vector-based guard (newWorldId < 0) silently refused Nether
    // transitions and made /gotor nether a no-op.
    bool NetworkManager::setPlayerWorldId(const std::string &playerName, int32_t newWorldId)
    {
        auto targetIt = m_worlds.find(newWorldId);
        if (targetIt == m_worlds.end() || !targetIt->second)
        {
            Core::Logger::warn("setPlayerWorldId: target worldId=" + std::to_string(newWorldId) +
                               " is not a registered / active plane");
            return false;
        }

        for (auto &kv : m_clients)
        {
            if (kv.second.username != playerName)
                continue;
            auto &session = kv.second;
            if (session.state != ClientState::PLAY)
                continue;

            // Persist in the database row so a relogin stays on the new plane.
            // (The players table's world_id column is populated by
            // Database::setPlayerWorldIdByName; when the column is absent on
            // a first run, we fall back to in-memory-only and log the miss.)
            if (!m_database.setPlayerWorldIdByName(playerName, newWorldId))
            {
                Core::Logger::warn("setPlayerWorldId: could not persist worldId for " + playerName);
            }

            // Snap to the surface of the new plane so the client never gets
            // buried in generated terrain. Hint Y at sea level keeps the
            // search bounded -- findStandingFeetY scans up/down from this.
            Storage::WorldStorage &targetWorld = *targetIt->second;
            const int wx = static_cast<int>(std::floor(session.x));
            const int wz = static_cast<int>(std::floor(session.z));
            const int feetY = targetWorld.findStandingFeetY(wx, wz, 62);
            const double newY = (feetY >= 0) ? static_cast<double>(feetY) : session.y;

            session.worldId = newWorldId;
            session.x = wx + 0.5;
            session.y = newY;
            session.z = wz + 0.5;

            // Clear loadedChunks -- the client must be told about the new
            // plane's terrain, so every chunk within renderDistance has to
            // re-emit. updateChunksAroundPlayer skips chunks already in this
            // set, so we clear it unconditionally on plane switch.
            session.loadedChunks.clear();

            // S2C Position (0x08): double x, double y, double z (see
            // NetworkEvents.cpp::broadcastPacket for era-aware encoding).
            std::vector<uint8_t> pos;
            const double coords[3] = {session.x, session.y, session.z};
            for (int axis = 0; axis < 3; ++axis)
            {
                std::uint64_t u;
                std::memcpy(&u, &coords[axis], 8);
                for (int byteIdx = 7; byteIdx >= 0; --byteIdx)
                    pos.push_back(static_cast<uint8_t>((u >> (byteIdx * 8)) & 0xFF));
            }
            sendPacket(kv.first, 0x08, pos);

            Core::Logger::info("setPlayerWorldId: moved " + playerName +
                               " to worldId=" + std::to_string(newWorldId) +
                               " at (" + std::to_string(session.x) + ", " +
                               std::to_string(session.y) + ", " +
                               std::to_string(session.z) + ")");
            return true;
        }
        return false;
    }

    // ---- tickChunkStreaming ---------------------------------------------------
    // Periodic flush of the 1.8+ chunk stream against the async generation
    // worker. Server.cpp::start() calls this on the 20 Hz game tick (throttled
    // to every 4 ticks by the caller so it runs at 5 Hz -- chunk delivery to a
    // moving player is already covered by updateChunksAroundPlayer on every
    // crossing, so this pass exists mainly for the "player crossed and then
    // stopped moving" case where the prefetch queue would otherwise sit
    // forever).
    //
    // Cost per call is O(N_players * rd^2) hash lookups + SQLite EXISTS checks
    // for chunks that aren't loaded; the sendMapChunkBulk body already skips
    // ready-and-already-loaded chunks in O(1) each, and only emits 0x26
    // packets for the not-yet-sent AND ready subset, so a no-op tick is
    // extremely cheap (~121 hash lookups per player at rd=5).
    void NetworkManager::tickChunkStreaming(int renderDistance)
    {
        auto packetSender = [this](int fd, int32_t p, const std::vector<uint8_t> &b)
        { sendPacket(fd, p, b); };

        // Only PLAY-state sessions have a chunk stream; HANDSHAKE / LOGIN are
        // mid-negotiation and haven't populated a render box yet.
        for (auto &kv : m_clients)
        {
            if (kv.second.state != LightweightMC::Player::ClientState::PLAY)
                continue;
            // Legacy (<1.8) clients do their own per-chunk 0x21 send inside
            // updateChunksAroundPlayer and don't have an async worker path to
            // flush; skip them so we don't double-send.
            if (kv.second.clientProtocol < 47)
                continue;
            m_worldManager.sendMapChunkBulk(kv.first, kv.second, renderDistance, packetSender);
        }
    }

}
