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
            // Phase 6 (0.0.15): ground item drops spawned when a mob is
            // slain (mob_drops rule), when a block is destroyed (block_drops
            // rule), or on explosion (block_explosion_drop_decay /
            // mob_explosion_drop_decay / tnt_explosion_drop_decay decay
            // rules). DROPPED_ITEM entities are non-AI: they don't tick,
            // they don't wander, and NetworkManager emits them on the
            // vanilla-1.8 wire as SpawnEntity (0x0E) with the item ID
            // captured in the extra `itemId` field below.
            DROPPED_ITEM,
            UNKNOWN
        };

        // Knockback strength buckets for melee UseEntity attacks (0.0.15 phase 9): vanilla-1.8 picks one knockback kind
        // from the held-item TYPE rather than scaling by the exact half-heart damage -- EntityHandler.cpp heldCombatFor() maps each item id to one of these buckets before handing it to applyPlayerAttack (cross-cited minecraft wiki Melee-combat rows + java mcsources-1.8 CombatHandler templates; collect/pickup semantics still entity-side since 0x0D Collect Item has no C2S dispatch in PlayHandler.cpp for protocol 47).
        enum KnockbackStrength : uint8_t { KNOCKBACK_NONE = 0, KNOCKBACK_WEAK = 1, KNOCKBACK_NORMAL = 2, KNOCKBACK_STRONG = 3, KNOCKBACK_RANGED = 4 };

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
            // Phase 6 (0.0.15): the item type carried by a DROPPED_ITEM
            // entity. Kept as int32_t so callers can pass either a numeric
            // item ID (0..255) or a stable template string alias like
            // "leather" resolved through lookupItemTemplate(). Zero means
            // "no item bound" for non-DROPPED_ITEM records.
            int32_t itemId{0};
            int32_t stackCount{1}; // 1.8 caps ground stacks at 12; set
                                   // by callers that need multi-drop.
            // Knockback lane recorded in-memory for melee hits from the attacker (see EntityHandler.cpp heldCombatFor + the enum docs above): strength keeps the vanilla kind bucket rather than raw damage so a club-vs-sword distinction survives relog; hitMs anchors when the impulse was last applied so EntityManager::tick can skip stale/dead targets. Not persisted to SQLite this batch (velocity knock impulse still TODO per PvE/PvP hit-detection box).
            uint8_t knockbackStrength{KNOCKBACK_NONE};
            int64_t knockbackMs{0};
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
        // applyPlayerAttack damages the target and returns true when the
        // mob's health reached zero (i.e. the caller should trigger the
        // kill path: XP grant + optional DROPPED_ITEM drop + Entity
        // Destroy emission). meleeDamage is half-hearts from the attacker's held item
        // (vanilla-1.8 table, see EntityHandler.cpp heldCombatFor); 0 or negative falls back to the bare-hand punch inside
        // the impl rather than silently defaulting to a heavier weapon hit. knockbackStrength picks the knock lane enum above that EntityManager::tick will
        // later turn into an impulse on next tick while alive.
        bool applyPlayerAttack(int32_t entityId, int32_t attackerFd,
                               int64_t nowMs, int32_t meleeDamage = 0,
                               int knockbackStrength = KNOCKBACK_NONE);
        bool applyPlayerFollow(int32_t entityId, int32_t followerFd);
        bool applyPlayerUnfollow(int32_t entityId);

        // Grant XP to the *session* identified by attackerFd. Returns the
        // number of levels gained (0-15). Caller should honour show_adv
        // ancement_messages before sending a chat notification.
        int grantKillXpToSession(int32_t entityId,
                                 LightweightMC::Player::PlayerSession &session,
                                 int64_t nowMs);

        // Spawn a ground item. `templateKey` maps through lookupItemTemplate
        // ("leather", "meat_raw", "wolf_pelt", ...) to a numeric item ID;
        // pass a numeric ID directly via the itemId argument if the caller
        // already knows it. Returns the assigned entity ID on success (non-
        // zero) or 0 when the entity cap is reached.
        int32_t spawnItemDrop(const std::string &templateKey,
                              double x, double y, double z,
                              int32_t itemId = 0,
                              int32_t stackCount = 1);

        // Despawn (or "pick up") a DROPPED_ITEM by entity ID. Returns true
        // when the entity was present and removed.
        bool removeItemDrop(int32_t entityId);

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
        // Overload that additionally emits one S2C SpawnEntityLiving (0x0f
        // vanilla 1.8 / 0x09 NamedEntitySpawn after MODERN remap)
        // per entity when networkSyncEnabled() is true (defaults ON --
        // Phase 7 era-appropriate flip). Only the
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
        // Phase 7 (0.0.15) entity-roster alignment. Wire IDs follow the
        // vanilla-1.8 Java Edition 47 table (PrismarineJS protocol_types.hpp
        // SpawnEntityLiving_d6229256 tail = VarInt entity_id, byte type,
        // 3 int32 fixed-point coords scaled by 1000, 3 int8 yaw/pitch/head
        // pitch, 3 int16 velocity scaled 5000, 1 byte metadata length):
        //   0x0f SpawnEntityLiving -- initial visible spawn
        //   0x10 DestroyEntity      -- per-despawn (entity_id VarInt)
        //   0x15 EntityMovement    -- per-tick movement delta
        //   0x0c EntityAction      -- per-attack / sound cue
        // NetworkFraming::sendPacket runs s2cWireIdFromLegacy on the wire ID
        // first; when that mapping returns -1 (unmapped in packet_ids.h's
        // kS2CReverse_MODERN / _LATEST tables) the raw vanilla-1.8 ID
        // passes through unchanged. LEGACY clients decode it as SpawnEntity
        // Living; MODERN/LATEST clients treat the packet as unknown and
        // drop it without crashing -- matching what a vanilla 1.8 client
        // expects for a 1.8 server. Modern-only IDs (NamedEntitySpawn 0x09
        // / EntityMoveLook 0x17 / EntityDestroy 0x13) remain available to
        // callers that need MODERN/LATEST clients to see the mob.
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
        // Phase 7 (0.0.15): era-appropriate sync. Vanilla-1.8 (the target
        // era for this build) expects S2C entity packets to be emitted so
        // the client sees mobs; the wire IDs (0x0f / 0x10 / 0x15) pass
        // through NetworkFraming's s2cWireIdFromLegacy unchanged because
        // packet_ids.h leaves those raw IDs unmapped in the MODERN /
        // LATEST reverse tables, so MODERN / LATEST clients treat them as
        // unknown and drop the packet without crashing. setNetworkSync(false)
        // is still available for H1Z1 network replay recording where the
        // operator wants byte-aligned wire with no S2C spawn / despawn.
        bool m_networkSync{true};

        // Ok -- login-time debug spawn state. See spawnLoginDebugTest /
        // removeLoginDebugTest above for the semantics.
        bool m_debugLoginSpawn{false};
        std::vector<int32_t> m_debugTestIds;

        std::mt19937 m_rng;
    };

} // namespace LightweightMC::World
