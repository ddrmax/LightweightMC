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
// 0.0.16 phase 8: importWorldFromPath / exportWorldToPath stream the plane's
// SQLite file to/from a caller-supplied path with ifstream/ofstream so the
// command's memory footprint stays bounded regardless of the plane's disk
// size; a bare <fstream> include (no <filesystem>) keeps the CMake target
// unchanged.
#include <fstream>

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
        // 0.0.15 phase 4: "flat" is a CLI alias for the CREATIVE generator
        // (flat infinite canvas: solid dirt/grass column, no ore, trees or
        // structures). Keeping it a name-only alias rather than a new enum
        // value avoids a new FlatGen.cpp / CMake target while still giving
        // admins a semantically obvious name in /world create's usage hint.
        if (s == "flat")
            return Storage::GenStyle::CREATIVE;
        if (s == "creative")
            return Storage::GenStyle::CREATIVE;
        if (s == "mining")
            return Storage::GenStyle::MINING;
        if (s == "twilight")
            return Storage::GenStyle::TWILIGHT;
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

    //
    // 0.0.15 phase 4: the widened signature carries per-plane inventory
    // policy (Storage::kInventoryPolicy*), forced game mode on entry
    // (Storage::kForceGameMode*) and explicit world seed (kDefaultWorldSeed
    // == 0 meaning "inherit from primary/Overworld"). The header declares
    // the three defaults; the definition here does not repeat them, so
    // existing two-argument call sites in Server.cpp (which the console
    // command layer wires to this function) keep compiling and running
    // with the standard SYNC / INHERIT / inherit behaviour.
    int32_t NetworkManager::createWorld(const std::string &worldName, const std::string &style,
                                        int32_t inventoryPolicy, int32_t forceGameMode, int64_t seed)
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

        // 0.0.15 phase 4: propagate --inventory / --force-gamemode / --seed
        // from the /world create CLI into the world_registry SQLite row and
        // mirror the policy + game-mode choice into WorldManager so the hot
        // path (NetworkManager::setPlayerWorldAt / PlayerHandler) sees the
        // same values the DB just persisted. Only write when the caller
        // passed a non-default value so plain `/world create foo overworld`
        // keeps SYNC / INHERIT / inherit-from-Overworld untouched and the
        // row layout stays byte-identical to the 0.0.15 phase 3 form.
        if (inventoryPolicy != Storage::kInventoryPolicySync)
        {
            if (!m_database.setWorldInventoryPolicy(assignedId, inventoryPolicy))
                Core::Logger::warn("createWorld: could not persist inventory_policy for id=" +
                                   std::to_string(assignedId) + " (in-memory mirror still applies this session)");
            m_worldManager.setInventoryPolicy(assignedId, inventoryPolicy);
        }
        if (forceGameMode != Storage::kForceGameModeInherit)
        {
            if (!m_database.setWorldForceGameMode(assignedId, forceGameMode))
                Core::Logger::warn("createWorld: could not persist force_gamemode for id=" +
                                   std::to_string(assignedId) + " (in-memory mirror still applies this session)");
            m_worldManager.setForceGameModeOnEntry(assignedId, forceGameMode);
        }
        if (seed != Storage::kDefaultWorldSeed)
        {
            if (!m_database.setWorldSeed(assignedId, seed))
                Core::Logger::warn("createWorld: could not persist world_seed for id=" +
                                   std::to_string(assignedId) +
                                   " (WorldStorage::setWorldSeed below still pins this session's seed)");
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
        // 0.0.15 phase 4: when the caller supplied an explicit --seed, prefer
        // it over the Overworld's value so a custom plane (e.g. a --inventory
        // separate flat sandbox) can pin its own deterministic terrain.
        const Storage::WorldStorage *primary = nullptr;
        auto overIt = m_worlds.find(0);
        if (overIt != m_worlds.end())
            primary = overIt->second.get();
        storage->setWorldSeed(seed != Storage::kDefaultWorldSeed
                                  ? static_cast<std::uint64_t>(seed)
                                  : (primary ? primary->getWorldSeed() : 0ULL));
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

    // ---- Multiworld world-control command backends (0.0.16) -----------------
    // Wire the /setworldspawn, /seed, /spawnpoint, /setblock and /place
    // console + chat commands into NetworkManager's live state. Each helper
    // resolves the plane name via the world_registry SQLite table (like
    // lookupWorldId / describeWorld) so all standard + custom planes work
    // identically; then dispatches to the appropriate SQLite column and/or
    // WorldManager in-memory mirror so the login path sees the change
    // without a fresh SQLite lookup.

    bool NetworkManager::setWorldSpawnByName(const std::string &worldName,
                                              double x, double y, double z)
    {
        if (worldName.empty())
            return false;
        const int32_t id = lookupWorldId(worldName);
        if (id == -2147483648)
        {
            Core::Logger::warn("setWorldSpawnByName: plane \"" + worldName + "\" is not registered");
            return false;
        }
        // SQLite write (persists across restarts) + in-memory mirror so the
        // login path (NetworkManager::setPlayerWorldAt / PlayerManager::sendPlay
        // Packets fall-through default) reads it back without another lookup.
        const bool sqliteOk = m_database.setWorldSpawn(id, x, y, z);
        m_worldManager.setWorldSpawn(id, x, y, z);
        Core::Logger::info("setWorldSpawnByName: plane \"" + worldName + "\" (id=" + std::to_string(id) +
                           ") spawn -> (" + std::to_string(x) + ", " + std::to_string(y) + ", " +
                           std::to_string(z) + ")");
        return sqliteOk;
    }

    bool NetworkManager::getWorldSpawnByName(const std::string &worldName,
                                              double &x, double &y, double &z) const
    {
        x = Storage::kDefaultWorldSpawnX;
        y = Storage::kDefaultWorldSpawnY;
        z = Storage::kDefaultWorldSpawnZ;
        if (worldName.empty())
            return false;
        const int32_t id = lookupWorldId(worldName);
        if (id == -2147483648)
            return false;
        // Cache first so we return the runtime value even if SQLite is
        // momentarily out-of-sync (e.g. after a hot /setworldspawn that
        // has not yet been flushed to disk).
        if (m_worldManager.getWorldSpawn(id, x, y, z))
            return true;
        return m_database.getWorldSpawn(id, x, y, z);
    }

    int64_t NetworkManager::getWorldSeedByName(const std::string &worldName) const
    {
        if (worldName.empty())
            return Storage::kDefaultWorldSeed;
        const int32_t id = lookupWorldId(worldName);
        if (id == -2147483648)
            return Storage::kDefaultWorldSeed;
        // Prefer the live WorldStorage value so /seed reports what terrain
        // generation actually used (NetworkManager::start seeds it from
        // [world] world-seed in server.properties even if the SQLite world
        // _seed column has not yet been written for that plane).
        auto it = m_worlds.find(id);
        if (it != m_worlds.end() && it->second)
        {
            const std::uint64_t live = it->second->getWorldSeed();
            if (live != 0ULL)
                return static_cast<int64_t>(live);
        }
        const int64_t cached = m_worldManager.getWorldSeed(id);
        if (cached != Storage::kDefaultWorldSeed)
            return cached;
        return m_database.getWorldSeed(id);
    }

    bool NetworkManager::setWorldSeedByName(const std::string &worldName, int64_t seed)
    {
        if (worldName.empty())
            return false;
        const int32_t id = lookupWorldId(worldName);
        if (id == -2147483648)
        {
            Core::Logger::warn("setWorldSeedByName: plane \"" + worldName + "\" is not registered");
            return false;
        }
        const bool sqliteOk = m_database.setWorldSeed(id, seed);
        m_worldManager.setWorldSeed(id, seed);
        auto it = m_worlds.find(id);
        if (it != m_worlds.end() && it->second)
        {
            // Chunks already on disk keep their terrain; only future
            // generateChunk() calls pick up the new seed.
            it->second->setWorldSeed(static_cast<std::uint64_t>(seed));
        }
        Core::Logger::info("setWorldSeedByName: plane \"" + worldName +
                           "\" (id=" + std::to_string(id) + ") world_seed=" + std::to_string(seed));
        return sqliteOk;
    }

    bool NetworkManager::setBlockAtPlane(int32_t planeId, int wx, int wy, int wz,
                                          uint16_t combined)
    {
        auto it = m_worlds.find(planeId);
        if (it == m_worlds.end() || !it->second)
        {
            Core::Logger::warn("setBlockAtPlane: plane id " + std::to_string(planeId) + " not live");
            return false;
        }
        // Same arithmetic-shift chunk mapping WorldStorage::blockAtWorld
        // uses so wx = -1 lands in chunk -1 with relX = 15.
        const int chunkX = (wx >> 4);
        const int chunkZ = (wz >> 4);
        const int relX = wx & 0xF;
        const int relZ = wz & 0xF;
        it->second->saveBlockChange(chunkX, chunkZ, relX, wy, relZ, combined);
        return true;
    }

    bool NetworkManager::updatePlayerSpawnByName(const std::string &playerName,
                                                  double x, double y, double z)
    {
        if (playerName.empty())
            return false;
        const std::string uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        if (uuid.empty())
        {
            Core::Logger::warn("updatePlayerSpawnByName: no known uuid for player \"" + playerName + "\"");
            return false;
        }
        return m_database.updatePlayerSpawn(uuid, x, y, z);
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

            // ----------------------------------------------------------------
            // Plane-scoped inventory transfer (0.0.15 plane-switch inventory
            // fix). Standard Minecraft keeps one inventory across Overworld /
            // Nether / End / Aether (kInventoryPolicySync); special planes
            // opt into a plane-scoped blob via world_registry's inventory_
            // policy column: SEPARATE for creative sandboxes (flat world,
            // overworld height, no gen -- their loot lives only on that
            // plane) and RESTRICTED for PVP / PVE training maps whose items
            // must never leak back to a SYNC chain. The transfer runs
            // BEFORE any wire packet so the client's inventory view already
            // matches what the server will persist when the player next
            // disconnects:
            //   1. Snapshot session.inventory, save into the DEPARTING
            //      plane's slot (SEPARATE/RESTRICTED go to
            //      player_inventories(uuid, oldWorldId); SYNC slots are
            //      written by the periodic savePlayerState flush so we
            //      skip a redundant mid-switch write here).
            //   2. Load the ARRIVING plane's slot (SYNC reads the flat
            //      players.inventory_blob column; SEPARATE/RESTRICTED
            //      read their own player_inventories row -- empty on first
            //      visit, which is exactly the intended "fresh creative
            //      sandbox / fresh training map" state and also prevents
            //      items from a SYNC plane from leaking into a RESTRICTED
            //      plane on arrival).
            //   3. Overwrite session.inventory from the arrival blob so
            //      downstream consumers (broadcast 0x05, savePlayerState
            //      mirror, next /gotor) see the plane-scoped slot.
            //
            // Same-plane switches skip the SQLite round-trips because the
            // slot is identical to itself.
            const int32_t departurePolicy = m_worldManager.getInventoryPolicy(oldWorldId);
            const int32_t arrivalPolicy = m_worldManager.getInventoryPolicy(newWorldId);
            if (oldWorldId != newWorldId)
            {
                const std::vector<uint8_t> departureBlob =
                    Player::PlayerManager::serializeInventory(session.inventory);
                if (departurePolicy != Storage::kInventoryPolicySync)
                {
                    if (!m_database.savePlayerInventory(playerUuid, oldWorldId, departureBlob))
                    {
                        Core::Logger::warn("setPlayerWorldAt: could not persist departure-plane "
                                           "inventory for " + playerName + " on worldId=" +
                                           std::to_string(oldWorldId) + " (SEPARATE / RESTRICTED slot)");
                    }
                }

                std::vector<uint8_t> arrivalBlob;
                if (arrivalPolicy == Storage::kInventoryPolicySync)
                {
                    m_database.loadSyncInventoryBlob(playerUuid, arrivalBlob);
                }
                else
                {
                    bool hasRow = false;
                    m_database.loadPlayerInventory(playerUuid, newWorldId, arrivalBlob, hasRow);
                }
                session.inventory.clear();
                Player::PlayerManager::deserializeInventory(arrivalBlob, session.inventory);
            }

            // Forced game mode on arrival: pin session.gamemode BEFORE the
            // 0x07 Respawn byte so the wire byte and the session's next
            // savePlayerState mirror agree (creative sandbox planes pin
            // CREATIVE even when the player was on SURVIVAL elsewhere;
            // PVP training maps pin whatever the plane author set).
            const int32_t arrivalForceGm = m_worldManager.getForceGameModeOnEntry(newWorldId);
            if (arrivalForceGm != Storage::kForceGameModeInherit)
                session.gamemode = static_cast<uint8_t>(arrivalForceGm);

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

    // ------------------------------------------------------------------------
    // Phase 8 world-model wiring.
    //
    // setWorldModelFromString parses a boot/config string into a Storage::
    // WorldModel enum and caches both the enum (m_worldModel) and the string
    // form (m_worldModelName) so /import-world and /export-world can report
    // which backend they picked without re-parsing. Unknown values log a warn
    // and fall back to sqlite; the empty string also maps to sqlite so the
    // config default ("sqlite") and the "no key present" boot case behave
    // identically.
    // ------------------------------------------------------------------------
    void NetworkManager::setWorldModelFromString(const std::string &modelString)
    {
        Storage::WorldModel m = Storage::WorldModel::SQLITE;
        if (modelString == "anvil")
            m = Storage::WorldModel::ANVIL;
        else if (modelString == "legacy")
            m = Storage::WorldModel::LEGACY;
        else if (!modelString.empty() && modelString != "sqlite")
        {
            Core::Logger::warn("[NetworkManager] Unknown world-model '" + modelString +
                               "', falling back to sqlite");
            m = Storage::WorldModel::SQLITE;
        }
        m_worldModel = m;
        m_worldModelName = Storage::worldModelToName(m);
        if (modelString != m_worldModelName)
        {
            Core::Logger::info("[NetworkManager] World model set to " + m_worldModelName);
        }
    }

    // File-level import for one plane. `path` is the source world file on
    // disk; `planeName` is the plane's friendly name resolved via
    // describeWorld (unregistered names return false rather than corrupting
    // the world_registry / a bogus db file); `formatHint` -- when non-empty
    // -- overrides the boot-selected world model for a single invocation.
    // Only the sqlite backend is wired in this build (anvil / legacy region
    // import is planned for phase 8b), so anvil/legacy return false with a
    // warn. The sqlite path streams `path` -> the plane's `world-*.db` file
    // with a fixed 16 KiB buffer and a 512 MiB sanity cap so an admin typo
    // pointing at /dev/zero or a full-disk image cannot blow the server's
    // RSS. Empty source aborts with a warn so the command never silently
    // blanks a live plane.
    bool NetworkManager::importWorldFromPath(const std::string &path,
                                             const std::string &planeName,
                                             const std::string &formatHint)
    {
        if (path.empty() || planeName.empty())
        {
            Core::Logger::warn("[NetworkManager] /import-world: missing path or plane name");
            return false;
        }
        int32_t id = 0;
        std::string style;
        std::string dbPath;
        if (!describeWorld(planeName, id, style, dbPath))
        {
            Core::Logger::warn("[NetworkManager] /import-world: plane '" + planeName +
                               "' is not registered (try /world list)");
            return false;
        }
        Storage::WorldModel effective = m_worldModel;
        if (!formatHint.empty())
            effective = Storage::worldModelFromName(formatHint);
        if (effective != Storage::WorldModel::SQLITE)
        {
            Core::Logger::warn("[NetworkManager] /import-world: only the sqlite backend is wired in this build "
                               "(anvil/legacy region import is planned for phase 8b)");
            return false;
        }
        std::ifstream src(path, std::ios::binary);
        if (!src)
        {
            Core::Logger::warn("[NetworkManager] /import-world: cannot open " + path);
            return false;
        }
        std::ofstream dst(dbPath, std::ios::binary | std::ios::trunc);
        if (!dst)
        {
            Core::Logger::warn("[NetworkManager] /import-world: cannot open " + dbPath);
            return false;
        }
        constexpr uint64_t kMaxImportBytes = 512ull * 1024ull * 1024ull;
        const size_t kChunkBytes = 16ull * 1024ull;
        std::vector<char> buf(kChunkBytes);
        uint64_t copied = 0;
        while (src)
        {
            src.read(buf.data(), static_cast<std::streamsize>(kChunkBytes));
            std::streamsize got = src.gcount();
            if (got <= 0)
                break;
            dst.write(buf.data(), got);
            copied += static_cast<uint64_t>(got);
            if (copied > kMaxImportBytes)
            {
                Core::Logger::warn("[NetworkManager] /import-world: aborted after " +
                                   std::to_string(copied) + " bytes (exceeded 512 MiB cap)");
                dst.close();
                return false;
            }
        }
        if (copied == 0)
        {
            Core::Logger::warn("[NetworkManager] /import-world: " + path +
                               " is empty; refusing to blank plane " + planeName);
            return false;
        }
        dst.close();
        Core::Logger::info("[NetworkManager] /import-world: copied " + std::to_string(copied) +
                           " bytes from " + path + " to plane '" + planeName +
                           "' (sqlite; reload at next boot)");
        return true;
    }

    // File-level export for one plane. `path` is the destination file on
    // disk; `planeName` resolves to the plane's SQLite database via
    // describeWorld. `formatHint` overrides the boot-selected model for one
    // invocation (so /export-world out.mca overworld anvil still produces an
    // Anvil-tagged filename even on a sqlite-default server; the actual
    // region-codec write is deferred to phase 8b). The sqlite path streams
    // the plane's `world-*.db` file to `path` using a 16 KiB buffer and no
    // size cap (admins are allowed to write big files out -- the reading
    // side of the loop stops when the source hits EOF naturally). Missing
    // source logs a warn and returns false so a plain `/export-world
    // out.db typo-nether` produces an actionable error message.
    bool NetworkManager::exportWorldToPath(const std::string &path,
                                           const std::string &planeName,
                                           const std::string &formatHint)
    {
        if (path.empty() || planeName.empty())
        {
            Core::Logger::warn("[NetworkManager] /export-world: missing path or plane name");
            return false;
        }
        int32_t id = 0;
        std::string style;
        std::string dbPath;
        if (!describeWorld(planeName, id, style, dbPath))
        {
            Core::Logger::warn("[NetworkManager] /export-world: plane '" + planeName +
                               "' is not registered (try /world list)");
            return false;
        }
        Storage::WorldModel effective = m_worldModel;
        if (!formatHint.empty())
            effective = Storage::worldModelFromName(formatHint);
        if (effective != Storage::WorldModel::SQLITE)
        {
            Core::Logger::warn("[NetworkManager] /export-world: only the sqlite backend is wired in this build "
                               "(anvil/legacy region export is planned for phase 8b)");
            return false;
        }
        std::ifstream src(dbPath, std::ios::binary);
        if (!src)
        {
            Core::Logger::warn("[NetworkManager] /export-world: plane '" + planeName +
                               "' has no database on disk at " + dbPath);
            return false;
        }
        std::ofstream dst(path, std::ios::binary | std::ios::trunc);
        if (!dst)
        {
            Core::Logger::warn("[NetworkManager] /export-world: cannot open " + path);
            return false;
        }
        const size_t kChunkBytes = 16ull * 1024ull;
        std::vector<char> buf(kChunkBytes);
        uint64_t copied = 0;
        while (src)
        {
            src.read(buf.data(), static_cast<std::streamsize>(kChunkBytes));
            std::streamsize got = src.gcount();
            if (got <= 0)
                break;
            dst.write(buf.data(), got);
            copied += static_cast<uint64_t>(got);
        }
        dst.close();
        Core::Logger::info("[NetworkManager] /export-world: wrote " + std::to_string(copied) +
                           " bytes from plane '" + planeName + "' (sqlite) to " + path);
        return true;
    }

}
