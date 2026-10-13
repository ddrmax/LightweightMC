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
        // Server-side entity model (mobs / aggressive mobs / villagers) is
        // accessed via World::EntityManager::getInstance(); no member needed
        // because the singleton is process-lifetime and NetworkManager is
        // itself a singleton.

        void setNonBlocking(int fd);
        void handleClientData(int clientFd);
        void handleDisconnect(int fd);
        void flushSendBuffer(LightweightMC::Player::PlayerSession &client);

    public:
        NetworkManager() = default;
        ~NetworkManager() = default;
        void sendPacket(int fd, int32_t packetId, const std::vector<uint8_t> &payload);
        void broadcastPacket(int32_t packetId, const std::vector<uint8_t> &payload, int ignoreFd = -1);
        bool start(uint16_t port);
        void pollEvents(int timeoutMs);
        void stop();

        // Update a player's op level: update the live session (if online) and
        // persist to the database. Returns true if the player is online or
        // the data was persisted successfully.
        bool setPlayerOpLevel(const std::string &playerName, uint8_t opLevel);

        // Update a player's game mode (0=survival, 1=creative, 2=adventure,
        // 3=spectator). Loads the existing database row, mutates only the
        // gamemode column, saves it back, updates the live session when the
        // player is currently online, and forwards a Tab List "Update Player"
        // packet (0x38) to that client so the change is reflected in-game.
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
        // "overworld" / "superflat" / "test-grid" / "nether" / "aether" / "end".
        int32_t createWorld(const std::string &worldName, const std::string &style);

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

        // Number of live in-memory WorldStorage instances. Registered-but-
        // deactivated planes (active=false config flag) are in world_registry
        // but not in m_worlds, so this count only reflects the planes the
        // server currently serves chunks for.
        size_t worldCount() const { return m_worlds.size(); }

        // Change a live PLAY session's worldId, reset loadedChunks (so the
        // new plane's chunks are streamed to the client on next updateChunks
        // AroundPlayer), and drop the client at a legal surface on the new
        // plane. Persists the worldId change to the player row so a relogin
        // keeps them in the same dimension. Returns true on success. Accepts
        // the pre-1.12 negative Nether id (-1) since m_worlds is now a map
        // rather than a vector indexed by worldId.
        bool setPlayerWorldId(const std::string &playerName, int32_t newWorldId);

        // Enumerate registered world names by walking the in-memory world
        // map (registered-but-inactive planes are omitted because they hold
        // no WorldStorage). Used by /world list to produce a display list
        // without the caller having to know SQLite.
        std::vector<std::string> listWorldNames() const;

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