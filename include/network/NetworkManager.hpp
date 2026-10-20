#pragma once
#include <cstdint>
#include <sys/epoll.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <optional>
#include <memory>

#include "storage/WorldStorage.hpp"
#include "storage/Database.hpp"
#include "player/PlayerSession.hpp"
#include "managers/WorldManager.hpp"
#include "world/GameRules.hpp"
#include "world/EntityManager.hpp"

namespace LightweightMC::Network
{
    class NetworkManager
    {
    private:
        int m_serverFd{-1};
        int m_epollFd{-1};
        bool antiscan{true};
        epoll_event m_events[64];
        std::unordered_map<int, LightweightMC::Player::PlayerSession> m_clients;
        // Multiworld storage keyed by worldId. Standard planes follow the
        // pre-1.12 vanilla dimension IDs (see Minecraft Legends layout):
        //    0   = Overworld   (world.db)          -- vanilla default plane
        //   -1   = Nether      (world-nether.db)   -- underworld
        //    1   = End         (world-end.db)      -- deepest plane
        //  100   = Aether      (world-aether.db)   -- special dimension
        // IDs -1/0/1 are hard-coded in NetworkManager::start(); the 100
        // "Aether" special dimension is registered explicitly so it stays
        // in the SQLite world_registry even when the [world] aether-active
        // config flag is off (allowing /world hot-swap without re-allocating
        // SQLite rows). IDs >= 101 are auto-allocated for custom worlds on
        // `/world create <name> <style>` via the world_registry SQLite table
        // in server_data.db (see src/storage/Database.cpp::registerWorld).
        //
        // Using a map (rather than a vector indexed by worldId) is required
        // because the pre-1.12 scheme includes negative IDs: static_casting
        // worldId = -1 to std::size_t before indexing a vector is UB and
        // previously crashed the server when a player crossed into Nether.
        // Lookup sites use find(); misses fall back to the Overworld (id 0).
        std::unordered_map<int32_t, std::unique_ptr<Storage::WorldStorage>> m_worlds;
        LightweightMC::Storage::Database m_database{"server_data.db"};
        World::WorldManager m_worldManager{m_worlds};
        // Phase 8 world-model trigger (Storage::WorldModel::SQLITE / ANVIL /
        // LEGACY). Set at boot from `[world] world-model` in server.properties
        // (parsed in NetworkSocket.cpp::NetworkManager::start) and consulted by
        // NetworkWorlds.cpp's importWorldFromPath / exportWorldToPath to decide
        // which file-format backend to speak. Defaults to SQLITE so a bare
        // console run without a boot pass still produces sensible output.
        Storage::WorldModel m_worldModel{Storage::WorldModel::SQLITE};
        std::string m_worldModelName{"sqlite"}; // cached string form so CommandContext::worldModelName can be a plain getter.
        // save-on / save-off toggle for the vanilla 1.8 autosave cadence.
        // Defaults to true so a bare console run flushes level.dat sidecars
        // every 5 minutes; /save-off flips it off.
        bool m_autosaveEnabled{true};
        // Wall-clock milliseconds since epoch of the last saveAllPlanes call
        // made by tickChunkStreaming; 0 means "never yet", and the first
        // autosave check after boot fires immediately.
        int64_t m_lastAutosaveMs{0};
        // Server-side entity model (mobs / aggressive mobs / villagers) is
        // accessed via World::EntityManager::getInstance(); no member needed
        // because the singleton is process-lifetime and NetworkManager is
        // itself a singleton.

        void setNonBlocking(int fd);
        void handleClientData(int clientFd);
        void handleDisconnect(int fd);
        void flushSendBuffer(LightweightMC::Player::PlayerSession &client);

        // Drain every ClientState::DISCONNECTED tombstone queued by handleDisconnect:
        // removes them from m_clients (socket already closed / epoll-removed by then)
        // so no PlayerSession survives past the tick that still holds pending packets.
        // Called by pollEvents and ~NetworkManager (before member teardown); callers
        // must not invoke inside an iteration over m_clients itself, flushPendingDisconnects
        // walks a copy of the fds for that reason.
        void flushPendingDisconnects(int64_t nowMs);

        // Phase 10 (ban/kick/whitelist) login-gate helper. allowLoginGate answers
        // whether a player name may connect given the ban/whitelist state loaded
        // from banned-players.json / whitelist.json sidecars (see src/network/BanList.cpp):
        // bans always win, and when the whitelist is active only whitelisted names pass.
        // The kick + denial wire helpers (denyLoginOnFd sends CannotConnect 0x03,
        // kickSessionByName pushes Disconnect wire 0x00 -- PacketCodec47 contract, not
        // the "0x40" the roadmap draft guessed) live in NetworkEvents.cpp.

    public:
        NetworkManager() = default;
        ~NetworkManager();

        bool allowLoginGate(const std::string &username);

        // Kick a connected player by (case-insensitive) username: pushes the
        // Disconnect packet (wire 0x00, payload JSON reason -- PacketCodec47)
        // to that socket then closes it via handleDisconnect(). Returns the
        // resolved session username ("" when nobody is connected by that name).
        std::string kickSessionByName(const std::string &name, const std::string &reason);
        void denyLoginOnFd(int fd, const std::string &reason);
        // Phase 13: InvalidSessionToken (S2C wire 0x04) counterpart for the
        // token-era auth path — sends reason string but keeps the socket open.
        void denySessionTokenOnFd(int fd, const std::string &reason);
        void sendPacket(int fd, int32_t packetId, const std::vector<uint8_t> &payload);
        void broadcastPacket(int32_t packetId, const std::vector<uint8_t> &payload, int ignoreFd = -1);
        bool start(uint16_t port);
        void pollEvents(int timeoutMs);
        void stop();

        // Number of sessions currently in ClientState::PLAY. Used by the
        // server-browser ping response so master-server listings see the
        // correct player count.
        int countOnline() const;

        // Update a player's op level: update the live session (if online) and
        // persist to the database. Returns true if the player is online or
        // the data was persisted successfully.
        bool setPlayerOpLevel(const std::string &playerName, uint8_t opLevel);

        // Update a player's game mode (0=survival, 1=creative, 2=adventure,
        // 3=spectator). Loads the existing database row, mutates only the
        // gamemode column, saves it back, updates the live session when the
        // player is currently online, and forwards a Change Game State
        // packet (wire op 0x2B, sub-action 3 = Change Game Mode) to that
        // client so the change is reflected in-game. 0x2B is the
        // vanilla-1.8 op the client actually uses to switch survival /
        // creative / adventure / spectator rules; 0x38 "Update Player
        // Info" is only for tab-list scoring and player properties.
        bool setPlayerGameMode(const std::string &playerName, uint8_t mode);

        // Teleport an online player (looked up by username) to absolute
        // world coordinates. Sends an S2C Position packet (0x08) to the
        // target's client so the client animates the jump, and mutates the
        // live session so scoreboard / chunk-streaming / save-on-logout
        // see the new position. Returns true if the player was found
        // online (PLAY state) or false if no matching session exists.
        bool teleportToCoords(const std::string &playerName, double x, double y, double z);

        // Teleport a player to the current world position of another
        // player. Returns false if either player is offline.
        bool teleportToPlayer(const std::string &moverName, const std::string &targetName);

        // ---- Multiworld / dimension-plane API --------------------------------
        // Returns the live WorldStorage for the given worldId, or the primary
        // (Overworld) storage when worldId is out of range. Never returns
        // nullptr -- callers can rely on a valid reference once start() has
        // registered the standard planes.
        Storage::WorldStorage &getWorldStorage(int32_t worldId);

        // Registers a new world in the `world_registry` SQLite table (server_data.db)
        // and inserts a fresh WorldStorage into m_worlds at the returned id.
        // Returns the assigned worldId on success or -1 on failure. IDs for
        // custom worlds are allocated starting at 101 (the reserved 100
        // "Aether" slot belongs to the special-dimension config; see the
        // fixed 0/-1/1 assignments for Overworld/Nether/End).
        // The `style` string maps to a Storage::GenStyle --
        // "overworld" / "superflat" / "test-grid" / "nether" / "aether" / "end"
        // / "creative" / "mining" / "twilight" / "flat" (0.0.15 phase 4 alias
        // for "creative").
        //
        // Phase 4 widens the signature so /world create can also carry the
        // per-plane inventory policy, forced game mode and world seed chosen
        // at creation time. Defaults preserve the pre-0.0.15 follow-up
        // behaviour (SYNC / INHERIT / inherit-from-primary) so the older
        // two-argument call sites keep working without changes.
        int32_t createWorld(const std::string &worldName, const std::string &style,
                            int32_t inventoryPolicy = Storage::kInventoryPolicySync,
                            int32_t forceGameMode = Storage::kForceGameModeInherit,
                            int64_t seed = Storage::kDefaultWorldSeed);

        // Looks up an existing world by registered name. Returns its worldId
        // (Overworld=0, Nether=-1, End=1, Aether=100, custom>=101) or the
        // -2147483648 sentinel when not found. Negative ids (Nether's -1) are
        // returned verbatim so callers can round-trip them through
        // PlayerSession::worldId; callers must test the sentinel, not "id < 0".
        int32_t lookupWorldId(const std::string &worldName) const;

        // Resolves a registered plane name to its id, style name and the
        // per-plane SQLite file it serves (world.db / world-nether.db /
        // world-end.db / world-aether.db for the fixed planes; world-<safeName>.db
        // for custom planes allocated by createWorld). Returns false when the
        // name is not in world_registry; a registered Nether plane (id -1)
        // returns true with outId=-1, so callers must not test outId for
        // "not found" the way lookupWorldId's sentinel does.
        bool describeWorld(const std::string &worldName,
                           int32_t &outId,
                           std::string &outStyle,
                           std::string &outDbPath) const;

        // ---- Multiworld world-control commands (0.0.16) ----------------------
        // Writes the plane's spawn triple to world_registry (spawn_x /
        // spawn_y / spawn_z DOUBLE columns) and mirrors it in
        // WorldManager::m_worldSpawn so the login path and NetworkManager::setPlayerWorldAt
        // can consult the cache. The plane name resolves through the
        // world_registry table (like lookupWorldId / describeWorld) so
        // "overworld" / "nether" / "end" / "aether" / any custom name are
        // all valid targets.
        bool setWorldSpawnByName(const std::string &worldName,
                                 double x, double y, double z);
        // Reads the plane's spawn triple (cache first, SQLite world_registry
        // fallback). Fills x/y/z with the vanilla (0.5, 64, 0.5) default
        // when neither layer has a value; returns true only when an
        // explicit value was found.
        bool getWorldSpawnByName(const std::string &worldName,
                                 double &x, double &y, double &z) const;

        // Reads the plane's seed: prefers the live WorldStorage::getWorldSeed()
        // value (so `/seed` reflects whatever server boot seeded it with from
        // [world] world-seed in server.properties) and falls back to the
        // SQLite world_registry world_seed column. Returns kDefaultWorldSeed
        // when neither layer carries a value.
        int64_t getWorldSeedByName(const std::string &worldName) const;
        // Writes both layers: SQLite world_registry world_seed column AND
        // WorldStorage::setWorldSeed on the live plane so subsequent
        // chunk generations on that plane use the new seed (already-
        // generated chunks stay unchanged until re-generated).
        bool setWorldSeedByName(const std::string &worldName, int64_t seed);

        // Write a single block on the given plane at absolute world
        // coordinates (wx, wy, wz). `combined` carries the pre-1.13
        // storage form (blockId << 4 | meta) matching WorldStorage's
        // in-memory chunk layout. Dispatches through
        // WorldStorage::saveBlockChange, which lazily generates the
        // chunk when it is not yet on disk so the write always succeeds
        // on registered planes.
        bool setBlockAtPlane(int32_t planeId, int wx, int wy, int wz,
                             uint16_t combined);

        // Patch the player row's spawn_x / spawn_y / spawn_z columns
        // (via Database::updatePlayerSpawn so inventory, coordinates,
        // gamemode, world_id, etc. are not clobbered the way savePlayer
        // Data's INSERT-OR-REPLACE would). Resolves the display name to
        // a UUID via PlayerManager::getPlayerUuid so an unknown name
        // returns false rather than writing garbage.
        bool updatePlayerSpawnByName(const std::string &playerName,
                                     double x, double y, double z);

        // Number of live in-memory WorldStorage instances. Registered-but-
        // deactivated planes (active=false config flag) are in world_registry
        // but not in m_worlds, so this count only reflects the planes the
        // server currently serves chunks for.
        size_t worldCount() const { return m_worlds.size(); }

        // Snapshot of every live plane id currently in m_worlds. Used by
        // Server.cpp's fixed 20 Hz tick loop to bind ChunkManager to every
        // plane (Overworld 0, Nether -1, End 1, custom >=101, Aether 100
        // if the special-dimension switch is on) so per-tick sweeps cover
        // any plane a session might have teleported into. Returns a fresh
        // vector so callers may safely iterate while NetworkWorlds adds or
        // removes planes concurrently (map mutation is not permitted during
        // iteration in unordered_map -- the copy side-steps that).
        std::vector<int32_t> planeIds() const;

        // ---- Phase 6 (0.0.15) vanilla-1.8 HP / XP / respawn / chat ------
        // Every entry-point resolves a display name to a live PLAY session
        // by username first (so damage/respawn reach the same PlayerSession
        // the movement handlers are mutating); when the caller is offline
        // we mutate only the SQLite row so the next login picks up the new
        // state. Damage/respawn/XP emitters only fire for online sessions
        // because the S2C packets have nowhere to go otherwise.

        // Apply HP damage to a player. `damageTypeCode` maps a damage-cause
        // label to a numeric code that PlayerSession::applyDamage stores for
        // diagnostics (0=melee, 1=fall, 2=drowning, 3=fire, 4=freeze, 5=zombie
        // contact, 6=poison, 7=lightning, 8=thorns, 9=magic -- see DamageCommand
        // in commands/EntityCommands.hpp which is what parses the admin-facing
        // damageType token). Codes 0-4 mirror vanilla's canonical causes so
        // external tooling that hardcodes those numbers keeps working; 5+ are
        // LightweightMC extensions. The caller is expected to have already
        // gated the call through the matching GameRules entry (pvp / fall_damage
        // / drowning_damage / fire_damage / freeze_damage) -- damageTypeCode
        // itself is used only for logging. Returns true when damage was
        // actually applied (i.e. the target exists AND was vulnerable).
        // When damage kills the target, emits an S2C DeathCombatEvent
        // (0x42) to that client; every non-fatal damage step emits Update
        // Health (0x06).
        bool applyDamageToPlayer(const std::string &playerName,
                                 float amount,
                                 int64_t nowMs,
                                 int damageTypeCode = 0);

        // Kill a player: applies enough HP damage to send them to zero
        // unconditionally (bypasses creative/spectator invuln because the
        // /kill command is explicit) so the target's next respawnAtMs
        // window gates respawn. Returns true when the target exists.
        bool killPlayer(const std::string &playerName, int64_t nowMs);

        // Force-respawn a player immediately (bypasses respawn_radius /
        // immediate_respawn gates): moves their session to spawnX/Y/Z,
        // restores full HP, emits an S2C Position packet (0x08) so the
        // client animates the jump. Returns true when the target exists.
        bool respawnPlayer(const std::string &playerName);

        // Grant XP to a player and emit Experience (0x1f) when online.
        // Returns the number of levels gained (0-15) so the /experience
        // command can decide whether to fire show_advancement_messages.
        int grantPlayerXp(const std::string &playerName, int32_t amount);

        // Natural-HP sweep driven from Server.cpp's fixed 20 Hz tick loop.
        // Consults natural_health_regeneration gamerule (skip when false); per-session PlayerSession::tickRegen gates the vanilla
        // 5 s no-damage window + food gate internally, and each healed session gets a fresh S2C Update Health (0x06) refresh
        // so idle/stopped players get their HP bar back without waiting on a damage packet.
        void tickRegeneration(int64_t nowMs);

        // Convenience used by /respawnwaves: set the boolean rule globally
        // via GameRules::setBool("respawnwaves", ...) so it persists to
        // SQLite gamerules and every live session picks up the new wave
        // semantics on the next applyDamageToPlayer / respawn pass.
        bool setRespawnWaves(bool enabled);

        // Snapshot of PLAY-state usernames, sorted for /list output. Returns
        // an empty vector when nobody is online.
        std::vector<std::string> listOnlinePlayers() const;

        // /msg backend: encode a chat-only packet and dispatch it to the
        // target's client. Returns false when the target is offline.
        bool sendPrivateChat(const std::string &targetName,
                             const std::string &message);

        // Change a live PLAY session's worldId, reset loadedChunks (so the
        // new plane's chunks are streamed to the client on next updateChunks
        // AroundPlayer), and drop the client at a legal surface on the new
        // plane. Persists the worldId change to the player row so a relogin
        // keeps them in the same dimension. Returns true on success. Accepts
        // the pre-1.12 negative Nether id (-1) since m_worlds is now a map
        // rather than a vector indexed by worldId.
        //
        // Standard Minecraft keeps one coordinate space per plane, so a plain
        // /gotor switch does NOT inherit the source plane's (x, z) -- that is
        // the bug the user reported (walking far in Overworld and ending up in
        // End's void at the Overworld's (x, z)). The public wrapper delegates
        // to setPlayerWorldAt with hasExplicitPosition=false, which saves the
        // departing plane's coordinates into the SQLite `player_positions`
        // table and either (a) restores the arrival-plane coordinates if the
        // player was previously on that plane, or (b) drops them at the
        // destination plane's default spawn. Portal / jump-point code should
        // call setPlayerWorldAt directly with hasExplicitPosition=true so the
        // portal author can pin the arrival point (that's the hook the user
        // asked for to support a future portal implementation).
        bool setPlayerWorldId(const std::string &playerName, int32_t newWorldId);

        // Portal-friendly plane switch. When `hasExplicitPosition` is true,
        // `x, y, z` is used verbatim as the arrival point on `newWorldId`
        // (typical portal semantics: the exit point on the destination plane
        // is authored by the portal itself). When `hasExplicitPosition` is
        // false, standard Minecraft per-plane semantics apply: the arriving
        // player is placed at their saved coordinates on `newWorldId` if the
        // SQLite `player_positions` table has an entry for that plane, or
        // dropped at the plane's default spawn (0, findStandingFeetY(0, 0,
        // 50), 0) on first visit. In either case the leaving plane's
        // coordinates are saved so future returns land where the player
        // actually stood.
        bool setPlayerWorldAt(const std::string &playerName, int32_t newWorldId,
                              double x, double y, double z, bool hasExplicitPosition);

        // Enumerate registered world names by walking the in-memory world
        // map (registered-but-inactive planes are omitted because they hold
        // no WorldStorage). Used by /world list to produce a display list
        // without the caller having to know SQLite.
        std::vector<std::string> listWorldNames() const;

        // Phase 8 world-model trigger accessors. worldModel() returns the
        // Storage::WorldModel enum; worldModelName() returns the string form
        // ("sqlite" / "anvil" / "legacy") so CommandContext::worldModelName
        // is a plain getter for /import-world and /export-world to include
        // in their confirmation replies.
        Storage::WorldModel worldModel() const { return m_worldModel; }
        std::string worldModelName() const { return m_worldModelName; }

        // Boot-time / hot-swap setter for the world model trigger. Called
        // from NetworkSocket.cpp::NetworkManager::start after parsing the
        // `[world] world-model` config key and by any future admin command
        // that decides to flip the trigger at runtime (currently we do not
        // expose such a command; keeping the setter available makes it a
        // one-line addition later).
        void setWorldModelFromString(const std::string &modelString);

        // ---- Phase 8 import / export backends -----------------------------
        // Import or export a plane's chunks to/from `path` on disk. `planeName`
        // is the friendly name (overworld / nether / end / aether / custom)
        // resolved through the same world_registry lookup the /world and /gotor
        // commands use, so an unregistered name returns false rather than
        // writing garbage. `formatHint` overrides the boot-selected world-model
        // trigger for a single invocation -- pass an empty string to use the
        // trigger, or "sqlite"/"anvil"/"legacy" to pin the format. When
        // formatHint is empty the command's handler usually already infers it
        // from the path suffix (.db -> sqlite, .mca -> anvil, .mce / .dat ->
        // legacy), so both the trigger and the caller's suffix agree most of
        // the time and disagreement just logs a warn.
        bool importWorldFromPath(const std::string &path,
                                 const std::string &planeName,
                                 const std::string &formatHint);
        bool exportWorldToPath(const std::string &path,
                               const std::string &planeName,
                               const std::string &formatHint);

        // ---- 0.0.15 phase 8d: save-all / save-on / save-off -------------------
        // Vanilla-1.8's server-side "flush disk then continue" pattern. save
        // AllPlanes walks every live plane in m_worlds, writes a per-plane
        // level.dat sidecar (Database::writeLevelDat) capturing spawn/seed/
        // mode/timestamps hydrated from world_registry + WorldManager, and
        // issues a no-op SQLite transaction so any OS-side dirty pages get
        // forced. It returns true when at least one plane round-tripped.
        //
        // setAutosaveEnabled/autosaveEnabled drive the save-on/save-off half
        // of the vanilla trio: when on, tickChunkStreaming re-checks elapsed
        // ms since the last flush and calls saveAllPlanes on a fixed cadence
        // (5 minutes) so a long-running session stays crash-safe without
        // every /save-all.
        bool saveAllPlanes(int64_t nowMs);
        void setAutosaveEnabled(bool enabled) { m_autosaveEnabled = enabled; }
        bool autosaveEnabled() const { return m_autosaveEnabled; }

        // ---- Async chunk-stream flush -----------------------------------------
        // Every game tick, iterate the live sessions and re-run WorldManager's
        // streaming sendMapChunkBulk on each. The call is cheap when nothing
        // changed: for each chunk in the render box it either finds it in
        // session.loadedChunks (skip, ~O(1) hash lookup) or finds it not-ready
        // in the DB (also skip, one SQLite EXISTS query). The chunk actually
        // ships to the client once the off-thread WorldStorage worker has
        // written its blob; without this flush pass, chunks that crossed into
        // a client's render box during a movement step but hadn't finished
        // generation yet would sit indefinitely on the worker queue if the
        // player then stopped moving.
        void tickChunkStreaming(int renderDistance);
    };

} // namespace LightweightMC::Network