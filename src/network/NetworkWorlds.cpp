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
//   <aether-plane-id> (default 100) = Aether (world-aether.db) -- Minecraft-Legends-style
//                                special dimension; the id is read from
//                                server.properties so a fork can reserve a
//                                different 100-series slot for a future
//                                Legends / Adventure crossover without editing
//                                this file.
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
#include "managers/PlayerManager.hpp"
#include "core/Logger.hpp"
#include "core/utils.hpp"
#include "network/Packet.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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
        //
        // Match the standard planes by NAME rather than by id so the mapping
        // stays correct even when the [world] aether-plane-id key re-homes the
        // Aether special dimension to a different 100-series id.
        if (worldName == "overworld")      outDbPath = "world.db";
        else if (worldName == "nether")    outDbPath = "world-nether.db";
        else if (worldName == "end")       outDbPath = "world-end.db";
        else if (worldName == "aether")    outDbPath = "world-aether.db";
        else
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

    // ---- setPlayerWorldId / setPlayerWorldAt --------------------------------
    // Live in-place plane switch for a currently-online player. The wire path:
    //   - Look up the online session by username.
    //   - Save the departing plane's coordinates into the SQLite player_posi-
    //     tions table (keyed by uuid, old worldId) so the player returns to
    //     where they actually stood the next time they visit that plane.
    //   - Resolve the destination plane's arrival point:
    //       * hasExplicitPosition=true -> use (x, y, z) verbatim (portal hook
    //         for future portal / jump-point code).
    //       * hasExplicitPosition=false -> look up the player's saved
    //         coordinates for the new plane; if present, drop them there; if
    //         not, drop them at that plane's default spawn.
    //   - Persist the worldId change on the player row so a relogin stays on
    //     the new plane.
    //   - Clear loadedChunks so the next updateChunksAroundPlayer streams
    //     fresh chunks from the new plane instead of stale chunk state from
    //     the previous one.
    //   - Send an S2C Position packet (0x08) so the client animates the jump
    //     and never walks through the walls it built on the old plane.
    //
    // Standard Minecraft keeps one coordinate space per dimension plane, so a
    // plain /gotor switch never inherits the source plane's (x, z). The
    // previous implementation reused wx=floor(session.x) which put a player
    // who had walked far in Overworld at the same (x, z) in the End plane's
    // void -- the bug the user reported. Portal code (which will land as a
    // future jump-point primitive) should call setPlayerWorldAt with
    // hasExplicitPosition=true so the portal author chooses the arrival point.
    //
    // Returns false when the player isn't currently online (PLAY state) or the
    // target worldId isn't registered. Note: we require PLAY because a player
    // mid-login has no position yet to preserve / animate against. The worldId
    // map key is signed so the pre-1.12 Nether id (-1) is a legal target --
    // the previous vector-based guard (newWorldId < 0) silently refused Nether
    // transitions and made /gotor nether a no-op.
    bool NetworkManager::setPlayerWorldAt(const std::string &playerName, int32_t newWorldId,
                                           double x, double y, double z, bool hasExplicitPosition)
    {
        auto targetIt = m_worlds.find(newWorldId);
        if (targetIt == m_worlds.end() || !targetIt->second)
        {
            Core::Logger::warn("setPlayerWorldAt: target worldId=" + std::to_string(newWorldId) +
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

            const int32_t oldWorldId = session.worldId;

            // Persist in the database row so a relogin stays on the new plane.
            // (The players table's world_id column is populated by
            // Database::setPlayerWorldIdByName; when the column is absent on
            // a first run, we fall back to in-memory-only and log the miss.)
            if (!m_database.setPlayerWorldIdByName(playerName, newWorldId))
            {
                Core::Logger::warn("setPlayerWorldId: could not persist worldId for " + playerName);
            }

            // session.uuid is populated on login but stays empty for players
            // mid-login; resolve the UUID deterministically so both save
            // paths (player_positions and periodic savePlayerData) key on the
            // same identifier.
            const std::string playerUuid = session.uuid.empty()
                ? Player::PlayerManager::getInstance().getPlayerUuid(playerName)
                : session.uuid;

            // Standard Minecraft plane hop: remember the source-plane coordi-
            // nates so a future return to this plane finds the player where
            // they actually stood. Skip when the switch is a same-plane no-op.
            if (oldWorldId != newWorldId)
            {
                if (!m_database.savePlayerPosition(playerUuid, oldWorldId,
                                                   session.x, session.y, session.z,
                                                   session.yaw, session.pitch))
                {
                    Core::Logger::warn("setPlayerWorldId: could not persist departure-plane coords "
                                       "for " + playerName + " on worldId=" + std::to_string(oldWorldId));
                }
            }

            // Resolve the arrival point (portal hook vs standard plane-hop).
            // hasExplicitPosition=true -> (x, y, z) verbatim, matching the
            // standard Minecraft portal pattern where the portal author
            // chooses the exit on the destination plane. hasExplicitPosition=
            // false -> look up the player's saved coordinates for the new
            // plane; if present, land them there; if not, drop them at that
            // plane's default spawn (findStandingFeetY(0, 0, 50)) on first
            // visit. Never reuse the source plane's (x, z) -- the previous
            // implementation did exactly that (wx = floor(session.x)), which
            // put a player who walked far in Overworld at the same (x, z)
            // in the End plane's void -- the reported bug.
            double destX = x;
            double destY = y;
            double destZ = z;
            float destYaw = session.yaw;
            float destPitch = session.pitch;
            if (!hasExplicitPosition)
            {
                double savedX = 0.0, savedY = 0.0, savedZ = 0.0;
                float savedYaw = 0.0f, savedPitch = 0.0f;
                if (m_database.loadPlayerPosition(playerUuid, newWorldId,
                                                  savedX, savedY, savedZ,
                                                  savedYaw, savedPitch))
                {
                    destX = savedX;
                    destY = savedY;
                    destZ = savedZ;
                    destYaw = savedYaw;
                    destPitch = savedPitch;
                }
                else
                {
                    const int feetY = targetIt->second->findStandingFeetY(0, 0, 50);
                    destX = 0.5;
                    destY = (feetY >= 0) ? static_cast<double>(feetY) : 50.0;
                    destZ = 0.5;
                    destYaw = 0.0f;
                    destPitch = 0.0f;
                }
            }

            // Mirror the arrival position for this plane too. Keeps the SQLite
            // table authoritative for the plane even if the periodic savePlayer-
            // Data path lags behind (e.g. the player disconnects right after
            // switching without a save flush).
            if (!m_database.savePlayerPosition(playerUuid, newWorldId,
                                               destX, destY, destZ,
                                               destYaw, destPitch))
            {
                Core::Logger::warn("setPlayerWorldId: could not persist arrival-plane coords "
                                   "for " + playerName + " on worldId=" + std::to_string(newWorldId));
            }

            session.worldId = newWorldId;
            session.x = destX;
            session.y = destY;
            session.z = destZ;
            session.yaw = destYaw;
            session.pitch = destPitch;

            // Clear loadedChunks -- the client must be told about the new
            // plane's terrain, so every chunk within renderDistance has to
            // re-emit. updateChunksAroundPlayer skips chunks already in this
            // set, so we clear it unconditionally on plane switch.
            session.loadedChunks.clear();

            // Vanilla 1.8 clients require a specific 4-packet ordering on
            // plane switch to close the "loading world" screen:
            //   1. 0x07 Respawn / Init Spawn Point (dimension, difficulty,
            //      gamemode, levelType) -- clears the client's cached
            //      chunks from the previous plane and switches the loading
            //      screen to the destination plane.
            //   2. Chunk Data (MapChunkBulk 0x26 for 1.8+, Chunk 0x21 for
            //      1.7 clients) for the drop-point chunk ONLY -- client
            //      decides which blocks to stand the player on.
            //   3. 0x08 Position and Look (x, y, z doubles + yaw, pitch
            //      floats + flags byte 0x00 for "absolute") -- closes the
            //      loading screen and re-appears the avatar. Note: 1.8 does
            //      NOT emit a trailing teleport-ID VarInt (that's a 1.9+
            //      field); adding one shifts every subsequent packet body.
            //   4. Chunk Data for the rest of the render-distance box.
            //
            // Sending only 0x08 (the pre-fix behaviour) left the client's
            // loading screen open -- it never saw the 0x07 that names the
            // new plane and never saw the drop-point chunk that grounds the
            // player, so /gotor looked like a no-op on-screen while the
            // server-side session was already updated.

            // Reset the prefetch sentinel so updateChunksAroundPlayer below
            // treats this crossing as a fresh login burst: pad the request
            // box symmetrically around the player rather than 2x ahead in a
            // stale movement direction from the previous plane.
            session.prevChunkX = std::numeric_limits<std::int32_t>::min();
            session.prevChunkZ = std::numeric_limits<std::int32_t>::min();

            auto packetSender = [this](int fd, int32_t p, const std::vector<uint8_t> &b)
            { sendPacket(fd, p, b); };

            // 1. Respawn (0x07): dimension, difficulty, gamemode, levelType.
            // Dimension is signed int32 so vanilla's Nether (-1) and any
            // custom plane id (>=101) both fit; difficulty 2 = Normal,
            // gamemode 0 = Survival, and levelType "default" mirror what
            // vanilla's /gotor-style world swap emits over the wire.
            //
            // Sky trick: vanilla 1.8 clients only know three sky renders
            // keyed by the 0x07 dimension field -- 0 (Overworld blue), 1
            // (End / Star Keep purple) and -1 (Nether / dark grey). Custom
            // planes (Aether id 100, /world create ids >=101, and the
            // planes landing in 0.0.15) have no sky of their own, so we
            // send WorldManager::skyDimensionFor(newWorldId) as the wire
            // value instead of raw newWorldId -- vanilla planes report
            // themselves, and Aether + custom planes report whatever
            // setSkyDimension registered (defaulting to 0 / Overworld sky
            // until a caller opts them into Nether or End). session.worldId
            // and the server-side m_worlds key keep the true plane id, so
            // storage lookups and player_positions rows stay correct; only
            // the wire value visible to the client is remapped.
            std::vector<uint8_t> respawn;
            Network::Packet::writeInt(respawn, m_worldManager.skyDimensionFor(newWorldId));
            respawn.push_back(2); // difficulty (byte, 2 = Normal)
            respawn.push_back(static_cast<uint8_t>(session.gamemode)); // gamemode (byte)
            Network::Packet::writeString(respawn, "default"); // levelType (string)
            sendPacket(kv.first, 0x07, respawn);

            // 2. Drop-point chunk: emit the single chunk the player will land
            //    in so the client's renderer has ground under the avatar
            //    before the 0x08 arrives. sendMapChunkBulk with renderDistance
            //    = 0 walks only the chunk under (session.x, session.z) and, on
            //    first plane visit (empty loadedChunks), uses the login-burst
            //    synchronous generation path so the chunk is definitely ready
            //    by the time the packet is written. Pre-1.8 clients get the
            //    equivalent single 0x21 Chunk packet via sendFlatChunk.
            if (session.clientProtocol >= 47)
            {
                m_worldManager.sendMapChunkBulk(kv.first, session, 0, packetSender);
            }
            else
            {
                const int dropChunkX = static_cast<int>(std::floor(session.x / 16.0));
                const int dropChunkZ = static_cast<int>(std::floor(session.z / 16.0));
                m_worldManager.sendFlatChunk(kv.first, dropChunkX, dropChunkZ, session, packetSender);
            }

            // 3. Player Position And Look (0x08): doubles + floats + flags.
            // Network::Packet::writeInt/writeDouble is big-endian on the wire
            // for every Minecraft S2C packet, so we build the body inline.
            std::vector<uint8_t> pos;
            Network::Packet::writeDouble(pos, session.x);
            Network::Packet::writeDouble(pos, session.y);
            Network::Packet::writeDouble(pos, session.z);
            Network::Packet::writeFloat(pos, session.yaw);
            Network::Packet::writeFloat(pos, session.pitch);
            pos.push_back(0x00); // Flags = 0x00 (absolute coords, no teleport ID trailer on 1.8)
            sendPacket(kv.first, 0x08, pos);

            // 4. Rest of the world: prefetch+emit every chunk in the render
            //    box around the new position. updateChunksAroundPlayer skips
            //    the drop-point chunk via loadedChunks (sendMapChunkBulk
            //    inserted it during step 2), and enqueues the remaining
            //    chunks on WorldStorage's async worker for this tick's
            //    ready-and-unloaded ones; tickChunkStreaming flushes the
            //    rest on subsequent ticks.
            constexpr int kPlaneSwitchRenderDistance = 5;
            m_worldManager.updateChunksAroundPlayer(kv.first, session,
                                                    kPlaneSwitchRenderDistance, packetSender);

            Core::Logger::info("setPlayerWorldId: moved " + playerName +
                               " to worldId=" + std::to_string(newWorldId) +
                               " at (" + std::to_string(session.x) + ", " +
                               std::to_string(session.y) + ", " +
                               std::to_string(session.z) + ")");
            return true;
        }
        return false;
    }

    // ---- setPlayerWorldId (public wrapper) ----------------------------------
    // Delegates to setPlayerWorldAt with hasExplicitPosition=false so standard
    // Minecraft per-plane semantics decide the arrival point (either the player's
    // saved coordinates on the destination plane or that plane's default spawn
    // on first visit). Portal code should call setPlayerWorldAt directly with
    // hasExplicitPosition=true so the portal author chooses the exit point.
    bool NetworkManager::setPlayerWorldId(const std::string &playerName, int32_t newWorldId)
    {
        return setPlayerWorldAt(playerName, newWorldId, 0.0, 0.0, 0.0, false);
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
