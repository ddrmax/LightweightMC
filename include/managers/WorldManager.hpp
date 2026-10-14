#pragma once
#include "storage/WorldStorage.hpp"
#include "storage/Database.hpp"
#include "player/PlayerSession.hpp"
#include "managers/ScoreboardManager.hpp"
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <memory>

namespace LightweightMC::World
{
    // Multiworld WorldManager. Owns (by reference) a map of WorldStorage
    // instances keyed by worldId; the NetworkManager singleton owns the
    // actual storage objects (see NetworkManager::m_worlds). Callers select
    // the correct instance via PlayerSession::worldId.
    //
    // Standard Minecraft dimension-plane IDs (pre-1.12 vanilla layout used
    // by Minecraft Legends / vanilla lore):
    //    0   = Overworld    (world.db)
    //   -1   = Nether       (world-nether.db)   -- underworld
    //    1   = End          (world-end.db)      -- deepest plane / Abyss
    //  100   = Aether       (world-aether.db)   -- special dimension
    // IDs >= 101 are auto-allocated for custom worlds created at runtime
    // via /world create and stored in the world_registry SQLite table in
    // server_data.db.
    //
    // Storing worlds in an unordered_map (rather than a vector indexed by
    // id) is required because Nether's canonical id is -1: static_casting
    // that value to std::size_t before indexing a vector is UB and
    // crashed the server when a player crossed into Nether.
    //
    // The reference to the map stays valid across insertions; each element
    // is a std::unique_ptr<WorldStorage>, whose pointed-to storage object
    // lives at a stable heap address regardless of map rehashing.
    class WorldManager
    {
    public:
        explicit WorldManager(std::unordered_map<int32_t, std::unique_ptr<Storage::WorldStorage>> &worlds) : m_worlds(worlds) {}

        // Generates and sends a chunk packet in flat format.
        void sendFlatChunk(int clientFd, int chunkX, int chunkZ, LightweightMC::Player::PlayerSession &session, const Managers::PacketSender &sendPacket);

        // Streaming Map Chunk Bulk (S2C 0x26) for 1.8+ clients. Walks the not-yet-loaded
        // chunks inside the render box and emits ONE 0x26 packet per chunk (chunkCount=1),
        // in the same outer-body layout as vanilla's jar server (byte hasSkyLight at body
        // position 0 + varint chunkCount + metas + payloads), verified against Wireshark
        // traces. This keeps every packet body under the 21-bit (2,097,151-byte) wire cap
        // enforced by NetworkFraming::sendPacket at any reasonable render distance --
        // the old monolithic variant (kept around below as sendMapChunkBulkMonolithic)
        // blew that cap for render distance >= 3 and the wire layer silently dropped the
        // body, which is why the world stalled past the initial view.
        void sendMapChunkBulk(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket);

        // Archived monolithic variant: packs EVERY not-yet-loaded chunk within the render
        // box into a single 0x26 body. Verified working for render distance 1..2 by the
        // user; blows the 21-bit wire cap at renderDistance >= 3, so it is not the active
        // entry point (updateChunksAroundPlayer uses the streaming sendMapChunkBulk above
        // for 1.8+ clients).
        void sendMapChunkBulkMonolithic(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket);

        // Streaming variant of Map Chunk Bulk: emit ONE 0x26 packet per chunk (chunkCount=1).
        // Same wire format as sendMapChunkBulk but body is bounded to a single chunk (~197 KB)
        // so no packet ever exceeds the 21-bit wire length cap (2,097,151 bytes).
        void sendChunkBulkSingle(int clientFd, int chunkX, int chunkZ,
                                 LightweightMC::Player::PlayerSession &session,
                                 const Managers::PacketSender &sendPacket);

        // Updates the surrounding chunks around the player based on their render distance.
        void updateChunksAroundPlayer(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket);

        // Backwards-compatible accessor -- returns the primary plane (Overworld)
        // storage. Falls back to any live world if Overworld (id 0) is missing
        // (which can only happen if a caller deactivated it via config without
        // re-enabling it) so the reference is never null. Callers that need a
        // non-default plane must use getWorldStorageFor(session.worldId)
        // instead.
        Storage::WorldStorage &getWorldStorage()
        {
            auto it = m_worlds.find(0);
            if (it != m_worlds.end() && it->second)
                return *it->second;
            for (auto &kv : m_worlds)
                if (kv.second)
                    return *kv.second;
            // Never reached in practice -- NetworkManager::start always keeps
            // the Overworld active (server refuses to boot without it).
            static Storage::WorldStorage fallback;
            return fallback;
        }

        // Session-aware accessor. Falls back to the Overworld when worldId
        // is unknown (e.g. an id belonging to a plane that was deactivated
        // via the [world] <name>-active=false config flag) so callers that
        // don't yet populate session.worldId still get a valid storage
        // reference. Accepts the pre-1.12 negative Nether id (-1) as a
        // proper map key.
        Storage::WorldStorage &getWorldStorageFor(int32_t worldId)
        {
            auto it = m_worlds.find(worldId);
            if (it != m_worlds.end() && it->second)
                return *it->second;
            auto over = m_worlds.find(0);
            if (over != m_worlds.end() && over->second)
                return *over->second;
            for (auto &kv : m_worlds)
                if (kv.second)
                    return *kv.second;
            static Storage::WorldStorage fallback;
            return fallback;
        }

        // Client-facing sky-dimension hint. Vanilla 1.8 clients only know
        // three sky renders -- dimension 0 (Overworld / blue sky), 1 (End /
        // Star Keep purple), and -1 (Nether / dark grey). Custom planes (Aether
        // id 100, /world create >=101, future 0.0.15 planes) have no sky of
        // their own; the client picks whatever the S2C JoinGame 0x01 / Respawn
        // 0x07 packet's `dimension` field says, so every non-standard plane
        // must be paired with one of the three vanilla ids to look correct.
        // skyDimensionFor(worldId) resolves that: vanilla plane ids (0, 1, -1)
        // return themselves, custom / future planes return whatever
        // setSkyDimension registered (defaulting to 0 = Overworld sky), so
        // Aether currently reports the blue Overworld sky and reconnect
        // defaults to the same. /world create and the future dimension
        // command can override per plane.
        void setSkyDimension(int32_t planeId, int32_t skyHint)
        {
            // Only the three vanilla sky ids are meaningful for the client's
            // renderer; anything else would show up as an unrecognised sky,
            // so we snap to the caller's intent and silently ignore bad hints
            // rather than pulling a Logger dependency into the header.
            if (skyHint == 0 || skyHint == 1 || skyHint == -1)
                m_skyDimension[planeId] = skyHint;
        }

        int32_t skyDimensionFor(int32_t planeId) const
        {
            auto it = m_skyDimension.find(planeId);
            if (it != m_skyDimension.end())
                return it->second;
            // Vanilla planes are their own sky; custom planes fall back to
            // Overworld unless setSkyDimension opted them into Nether or End.
            if (planeId == 0 || planeId == 1 || planeId == -1)
                return planeId;
            return 0;
        }

        // ------------------------------------------------------------------
        // Per-plane inventory policy + forced game mode (0.0.15 plane-switch
        // inventory fix).
        //
        // Vanilla Minecraft carries one global inventory across dimension
        // planes, which is what most players expect when hopping Overworld
        // <-> Nether <-> End. Special planes break that assumption in two
        // ways:
        //   * "flat / creative sandbox" (a special flat world with
        //     overworld height, no structures/ores/villages/trees, forced
        //     creative) wants its OWN inventory so loot on that plane never
        //     leaks back to the mining chain and vice versa.
        //   * PVP / PVE training planes want a one-way training slot: items
        //     gained there stay there, arriving never inherits from any
        //     other plane, leaving never carries items back.
        //
        // The server-side state machine (NetworkManager::setPlayerWorldAt
        // and Database::loadPlayerData) consults these two accessors to
        // decide which SQLite slot to read / write. Policies are declared
        // via Storage::kInventoryPolicy{Sync,Separate,Restricted} and modes
        // via Storage::kForceGameMode{Inherit,Survival,Creative,Adventure,Spectator}
        // in include/storage/Database.hpp so the encoding stays in one
        // place.
        //
        // NetworkManager::start seeds both maps at boot by reading
        // `[world] aether-inventory-policy` and `[world] aether-force-
        // gamemode` from server.properties (custom /world create planes
        // can call setInventoryPolicy / setForceGameModeOnEntry after
        // allocation, and Database::getWorldInventoryPolicy is the fallback
        // for anything not in the cache -- it reads the world_registry row
        // directly).
        void setInventoryPolicy(int32_t planeId, int32_t policy)
        {
            m_inventoryPolicy[planeId] = policy;
        }

        int32_t getInventoryPolicy(int32_t planeId) const
        {
            auto it = m_inventoryPolicy.find(planeId);
            if (it != m_inventoryPolicy.end())
                return it->second;
            // SYNC is the standard Minecraft behaviour every vanilla plane
            // and pre-0.0.15 custom plane inherits; returning it here keeps
            // unregistered planes functional even if the SQLite lookup
            // happens to be unavailable.
            return Storage::kInventoryPolicySync;
        }

        void setForceGameModeOnEntry(int32_t planeId, int32_t forceGm)
        {
            m_forceGameModeOnEntry[planeId] = forceGm;
        }

        int32_t getForceGameModeOnEntry(int32_t planeId) const
        {
            auto it = m_forceGameModeOnEntry.find(planeId);
            if (it != m_forceGameModeOnEntry.end())
                return it->second;
            // INHERIT is the standard Minecraft behaviour: a plane with no
            // explicit force preserves whatever mode the player carried in
            // (creative on creative, survival on survival, ...).
            return Storage::kForceGameModeInherit;
        }

        // ------------------------------------------------------------------
        // Per-plane world spawn + per-plane world seed (0.0.16 world-
        // control commands: /setworldspawn, /seed, /spawnpoint, /setblock,
        // /place). Mirrors the world_registry SQLite columns
        //   spawn_x DOUBLE DEFAULT 0.5,
        //   spawn_y DOUBLE DEFAULT 64.0,
        //   spawn_z DOUBLE DEFAULT 0.5,
        //   world_seed INTEGER DEFAULT 0
        // so PlayerManager::sendPlayPackets and NetworkManager::setPlayer
        // WorldAt can consult the cache without a fresh SQLite lookup on
        // every login / portal transition.
        //
        // setWorldSpawn / getWorldSpawn carry the plane's default spawn
        // triple; callers that want the standard Minecraft behaviour get
        // the vanilla (0.5, 64, 0.5) values back (Storage::kDefaultWorld
        // SpawnX/Y/Z) when the plane has never been touched by /setworld
        // spawn, so pre-0.0.16 planes keep working unchanged.
        //
        // setWorldSeed / getWorldSeed mirror WorldStorage::setWorldSeed
        // to a hot-path cache keyed on planeId; callers reading the seed
        // for a plane whose WorldStorage was boot-seeded from server.
        //properties get the SQLite-column value unless NetworkManager
        // prefers the WorldStorage value (see getWorldSeedByName).
        // ------------------------------------------------------------------
        void setWorldSpawn(int32_t planeId, double x, double y, double z)
        {
            m_worldSpawn[planeId] = {x, y, z};
        }

        bool getWorldSpawn(int32_t planeId, double &x, double &y, double &z) const
        {
            auto it = m_worldSpawn.find(planeId);
            if (it == m_worldSpawn.end())
            {
                x = Storage::kDefaultWorldSpawnX;
                y = Storage::kDefaultWorldSpawnY;
                z = Storage::kDefaultWorldSpawnZ;
                return false;
            }
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }

        void setWorldSeed(int32_t planeId, int64_t seed)
        {
            m_worldSeed[planeId] = seed;
        }

        int64_t getWorldSeed(int32_t planeId) const
        {
            auto it = m_worldSeed.find(planeId);
            if (it != m_worldSeed.end())
                return it->second;
            return Storage::kDefaultWorldSeed;
        }

    private:
        std::unordered_map<int32_t, std::unique_ptr<Storage::WorldStorage>> &m_worlds;
        std::unordered_map<int32_t, int32_t> m_skyDimension;
        std::unordered_map<int32_t, int32_t> m_inventoryPolicy;
        std::unordered_map<int32_t, int32_t> m_forceGameModeOnEntry;
        // Per-plane world spawn cache (0.0.16). Keyed by planeId; values
        // are (x, y, z) so a single lookup returns the whole triple.
        // Empty cache implies "use Storage::kDefaultWorldSpawn*".
        std::unordered_map<int32_t, std::array<double, 3>> m_worldSpawn;
        // Per-plane seed cache mirroring WorldStorage::m_worldSeed so
        // /seed reports the plane's runtime seed without walking SQLite
        // on every invocation.
        std::unordered_map<int32_t, int64_t> m_worldSeed;
    };
}

