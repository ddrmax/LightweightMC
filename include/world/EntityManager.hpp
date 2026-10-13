#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <random>
#include <chrono>
#include <cmath>

#include "player/PlayerSession.hpp"
#include "core/Logger.hpp"
#include "world/GameRules.hpp"
#include "managers/ScoreboardManager.hpp"
#include "storage/WorldStorage.hpp"

namespace LightweightMC::World
{
    // -------------------------------------------------------------
    // EntityManager - server-side model of Minecraft entities (mobs,
    // villagers, patrol/warden/phantom/wandering-trader NPCs, vehicles,
    // projectiles). LightweightMC has no tick loop (see README: "Event-
    // Driven & Zero-Tick"), so the simulation is advanced lazily: each
    // entity carries a `lastTickMs` timestamp and NetworkManager::tick
    // advances it by the real-world delta since the last time it was
    // touched. Entities far from every player stay dormant (no CPU cost
    // while the world is idle), which keeps the "Frozen" and "Unloaded"
    // chunk categories meaningful for the entity subsystem too.
    //
    // Kind bucketing:
    //   PASSIVE     : cow / pig / sheep / chicken / horse / deer / rabbit / llama
    //                 - wander around home, flee when a player comes close.
    //   AGGRESSIVE  : wolf / tiger / bear / zombie / hunter / bandit / spider
    //                 - approach the nearest player within aggro radius,
    //                 attack on cooldown.
    //   VILLAGER    : wandering_trader / patrol / warden / phantom
    //                 - patrol home waypoints, warden herds players away,
    //                 phantom hunts at "night" only, trader wanders + greets.
    //
    // The behaviour toggles are driven by existing GameRules entries:
    //   spawn_mobs, spawn_monsters, spawn_patrols, spawn_phantoms,
    //   spawn_wandering_traders, spawn_wardens, mob_griefing,
    //   universal_anger, raids, max_entity_cramming.
    // -------------------------------------------------------------
    class EntityManager
    {
    public:
        enum class EntityKind
        {
            PASSIVE,
            AGGRESSIVE,
            VILLAGER,
            VEHICLE,
            PROJECTILE,
            UNKNOWN
        };

        struct EntityRecord
        {
            int32_t id{0};
            EntityKind kind{EntityKind::UNKNOWN};
            std::string templateKey;
            double x{0.0}, y{0.0}, z{0.0};
            double vx{0.0}, vy{0.0}, vz{0.0};
            double homeX{0.0}, homeY{0.0}, homeZ{0.0};
            double heading{0.0};
            uint16_t health{10};
            uint16_t maxHealth{10};
            double speed{1.0};
            double aggroRange{16.0};
            double leashRange{24.0};
            int32_t targetFd{-1};
            bool angry{false};
            int64_t lastAttackMs{0};
            int64_t lastTickMs{0};
            int64_t spawnMs{0};
            int64_t lifetimeMs{600000};
            bool follower{false};
            bool networkVisible{false};
        };

        static EntityManager &getInstance()
        {
            static EntityManager instance;
            return instance;
        }

        EntityManager(const EntityManager &) = delete;
        EntityManager &operator=(const EntityManager &) = delete;

        void init(class Storage::Database *db);

        // ---- Spawn / Despawn ------------------------------------------------
        int32_t spawn(EntityKind kind,
                      const std::string &templateKey,
                      double x, double y, double z,
                      double speed = 1.0,
                      uint16_t maxHealth = 10);
        bool despawn(int32_t entityId);
        bool despawnAll();
        void clearAll();

        // ---- Queries --------------------------------------------------------
        size_t count() const;
        size_t countKind(EntityKind k) const;
        const EntityRecord *find(int32_t entityId) const;
        std::vector<int32_t> listIds() const;
        void dumpToLog() const;

        // Human-readable name for a bucket (used in logs and the /lwmc
        // entity list admin command).
        static std::string kindToString(EntityKind k);

        // ---- Simulation tick ------------------------------------------------
        void tick(const std::unordered_map<int, LightweightMC::Player::PlayerSession> &clients,
                  int64_t nowMs);

        // ---- Behaviour entry-points exposed for chat / commands -------------
        bool applyPlayerAttack(int32_t entityId, int32_t attackerFd, int64_t nowMs);
        bool applyPlayerFollow(int32_t entityId, int32_t followerFd);
        bool applyPlayerUnfollow(int32_t entityId);

        // Seed a small roster of mobs / villagers around a freshly-connected
        // player. The single-argument form keeps the previous "y = session.y"
        // behaviour (kept for callers that don't have world storage handy, e.g.
        // unit tests); the WorldStorage& overload enforces the vanilla 2-air
        // spawn rule: candidates must stand on a solid block with the two
        // blocks above (feet + head) free of terrain, so mobs never appear
        // buried inside generated terrain or player-placed blocks.
        void spawnForSession(const LightweightMC::Player::PlayerSession &session);
        void spawnForSession(const LightweightMC::Player::PlayerSession &session,
                             Storage::WorldStorage &worldStorage);
        // Overload that additionally emits one S2C SpawnEntityLiving (0x0f)
        // per entity when setNetworkSync(true) has been enabled. Only the
        // specified client receives the packet (the mob roster is seeded per
        // login); future movement / despawn updates are broadcast by
        // EntityManager::tick when the client is already tracking them.
        void spawnForSession(const LightweightMC::Player::PlayerSession &session,
                             Storage::WorldStorage &worldStorage,
                             int clientFd, const Managers::PacketSender &sendPacket);

        // ---- Tuning knobs ---------------------------------------------------
        void setAggroRadius(double r);
        void setFleeRadius(double r);
        void setLeashRadius(double r);
        void setAttackCooldownMs(int64_t ms);
        void setMaxTotal(int32_t cap);
        void setNetworkSync(bool enabled);
        bool networkSyncEnabled() const { return m_networkSync; }

        // ---- S2C emission helpers (only fire when networkSyncEnabled()) ------
        // Payloads follow the protocol-47 wire format described in PacketCodec47
        // and PrismarineJS protocol_types.hpp (NamedEntitySpawn for the initial
        // visible spawn, EntityMoveLook for each movement update, EntityDestroy
        // for despawn). Kept as template helpers so the caller can inject either
        // NetworkManager::sendPacket or a broadcast variant. Reserved for a
        // future PR that opts-in to on-wire entity sync; disabled by default so
        // vanilla 1.8 clients stay byte-aligned.
        template <typename Sender>
        void emitSpawn(int clientFd, const EntityRecord &rec, const Sender &sendPacket) const;
        template <typename Sender>
        void emitMoveLook(int clientFd, const EntityRecord &rec,
                          double dx, double dy, double dz, bool onGround,
                          const Sender &sendPacket) const;
        template <typename Sender>
        void emitDespawn(const std::vector<int32_t> &ids, const Sender &broadcastPacket) const;

    private:
        EntityManager();
        ~EntityManager() = default;

        void refreshRuleCache();

        // AI helpers - private because they touch m_rng and the cached
        // gamerule state.
        void wanderWish(EntityRecord &rec, int64_t nowMs, double &outVx, double &outVz);
        void villagerWish(EntityRecord &rec, const LightweightMC::Player::PlayerSession *nearest,
                          double nearestD2, int64_t nowMs, double &outVx, double &outVz);

        std::unordered_map<int32_t, EntityRecord> m_entities;
        int32_t m_nextId{1001};

        // Cached gamerule snapshots (refreshed on tick so we never lock GameRules
        // per entity per tick).
        bool m_spawnMobs{true};
        bool m_spawnMonsters{true};
        bool m_spawnPatrols{true};
        bool m_spawnPhantoms{true};
        bool m_spawnTraders{true};
        bool m_spawnWardens{true};
        bool m_griefing{true};
        bool m_universalAnger{false};
        int64_t m_maxCramming{24};

        double m_aggroRadius{16.0};
        double m_fleeRadius{8.0};
        double m_leashRadius{24.0};
        int64_t m_attackCooldownMs{1200};
        int32_t m_maxTotal{96};
        bool m_networkSync{false};

        std::mt19937 m_rng;
    };

} // namespace LightweightMC::World

