#include "world/EntityManager.hpp"
#include "world/GameRules.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

// -----------------------------------------------------------------------------
// Server-side entity model for Minecraft 1.8 (protocol 47).
//
// The manager is a lazy-evaluation simulation: NetworkManager::pollEvents
// invokes EntityManager::tick every ~500 ms with the wall-clock timestamp;
// each entity computes its own delta from `lastTickMs`. Because there is
// no fixed tick loop, the entity subsystem stays dormant when the world is
// idle (no CPU cost) and only advances when NetworkManager is actively
// polling - which matches the project's "Event-Driven & Zero-Tick" README.
//
// Wire protocol note (updated in Phase 7): the manager emits S2C entity
// packets with the vanilla-1.8 Java Edition wire IDs (SpawnEntityLiving
// 0x0f, DestroyEntity 0x10, EntityMovement 0x15, EntityAction 0x0c) by
// default (era-appropriate sync -- see EntityManager.hpp m_networkSync
// comment). NetworkFraming::sendPacket's s2cWireIdFromLegacy leaves those
// IDs unmapped in the MODERN / LATEST reverse tables so MODERN / LATEST
// clients see them as unknown packet IDs and drop them without crashing,
// while vanilla 1.8 clients parse them correctly. setNetworkSync(false)
// stays available for H1Z1 replay recording where the operator wants
// byte-aligned wire with no S2C spawn / despawn at all. The simulation
// state (HP, aggro, wander, follow, pathfinding) always runs
// regardless so admin commands and UseEntity / EntityAction packet
// handlers see coherent answers.
// -----------------------------------------------------------------------------

namespace LightweightMC::World
{
    namespace
    {
        // Per-template behaviour profiles. Kind (PASSIVE / AGGRESSIVE /
        // VILLAGER) is the coarse bucket the AI dispatches on; template
        // fine-tunes speed, HP and range. Values follow minecraft.wiki mob
        // stats with a small nudge for the H1Z1 mob set (wolf, tiger, bear,
        // hunter, bandit, etc.). Kept inline so the table lives with the
        // behaviour it feeds and can be extended by a future PR without
        // inventing a new translation unit.
        struct TemplateInfo
        {
            const char *name;
            EntityManager::EntityKind kind;
            uint8_t mobId; // ID officiel du mob en Minecraft 1.8 (Protocol 47)
            double speed;
            uint16_t hp;
            double aggroRange;
            double leashRange;
        };

        const TemplateInfo kTemplates[] = {
            // Passive animals (IDs 1.8: Cow=92, Pig=90, Sheep=91, Chicken=93, Rabbit=101, Horse=100, etc.)
            {"cow", EntityManager::EntityKind::PASSIVE, 92, 0.6, 20, 8.0, 24.0},
            {"pig", EntityManager::EntityKind::PASSIVE, 90, 0.7, 16, 8.0, 20.0},
            {"sheep", EntityManager::EntityKind::PASSIVE, 91, 0.6, 8, 8.0, 20.0},
            {"chicken", EntityManager::EntityKind::PASSIVE, 93, 0.5, 6, 6.0, 12.0},
            {"rabbit", EntityManager::EntityKind::PASSIVE, 101, 0.7, 3, 6.0, 12.0},
            {"deer", EntityManager::EntityKind::PASSIVE, 92, 1.0, 12, 10.0, 28.0}, // Fallback ou type personnalisé
            {"llama", EntityManager::EntityKind::PASSIVE, 100, 0.9, 16, 10.0, 24.0},
            {"horse", EntityManager::EntityKind::PASSIVE, 100, 1.2, 24, 10.0, 32.0},

            // Aggressive mobs (IDs 1.8: Wolf=95, Zombie=54, Skeleton=51, Spider=52, Cave Spider=59, Enderman=58)
            {"wolf", EntityManager::EntityKind::AGGRESSIVE, 95, 1.1, 14, 20.0, 32.0},
            {"tiger", EntityManager::EntityKind::AGGRESSIVE, 95, 1.4, 20, 24.0, 36.0},
            {"bear", EntityManager::EntityKind::AGGRESSIVE, 95, 0.9, 30, 18.0, 24.0},
            {"zombie", EntityManager::EntityKind::AGGRESSIVE, 54, 0.9, 20, 16.0, 32.0},
            {"skeleton", EntityManager::EntityKind::AGGRESSIVE, 51, 1.0, 14, 20.0, 32.0},
            {"hunter", EntityManager::EntityKind::AGGRESSIVE, 32, 1.1, 16, 24.0, 32.0}, // 32 = Player (ou skin humain)
            {"bandit", EntityManager::EntityKind::AGGRESSIVE, 32, 1.1, 24, 24.0, 32.0},
            {"spider", EntityManager::EntityKind::AGGRESSIVE, 52, 1.2, 8, 20.0, 32.0},

            // Villagers / NPCs (IDs 1.8: Villager=120)
            {"wandering_trader", EntityManager::EntityKind::VILLAGER, 120, 0.9, 24, 12.0, 40.0},
            {"patrol", EntityManager::EntityKind::VILLAGER, 120, 1.0, 24, 20.0, 48.0},
            {"warden", EntityManager::EntityKind::VILLAGER, 54, 1.0, 24, 24.0, 56.0},
            {"phantom", EntityManager::EntityKind::VILLAGER, 65, 1.2, 16, 24.0, 32.0}, // 65 = Bat ou 93
        };

        const TemplateInfo *lookupTemplate(const std::string &name)
        {
            for (const auto &t : kTemplates)
                if (name == t.name)
                    return &t;
            return nullptr;
        }

        // -----------------------------------------------------------------
        // Phase 7 (0.0.15) entity-roster alignment. Vanilla 1.8's Java
        // Edition mob table (PrismarineJS protocol_1_8.yaml, minecraft.wiki
        // Java Edition 1.8) covers {chicken, cow, pig, sheep} passive and
        // {zombie, skeleton (skeleton_warrior + skeleton_archer on the wire),
        // spider} aggressive; the wider H1Z1 roster (wolf/tiger/bear/hunter/
        // bandit/rabbit/deer/llama/horse/wandering_trader/patrol/warden/
        // phantom) is H1Z1-flavour-only and stays selectable separately.
        // spawnForSession consults the world's GenStyle to pick the era-
        // appropriate subset of templates per login seed, so a Twilight
        // forest never spawns a warden and a Mining plane never spawns a
        // chicken.
        // -----------------------------------------------------------------
        struct StyleRoster
        {
            // Each vector lists template names for that style in the same
            // order they should be placed near the login point.
            std::vector<std::string> passive;    // farmyard / animals
            std::vector<std::string> aggressive; // monsters / beasts
            std::vector<std::string> villagers;  // traders / wardens / etc
        };

        StyleRoster rosterForStyle(Storage::GenStyle style)
        {
            using G = Storage::GenStyle;
            switch (style)
            {
            case G::NETHER:
                // Nether is dark stone-cavern flavor: cave spiders
                // (H1Z1) + vanilla skeletons / zombie. No farmyard.
                return {
                    {},
                    {"spider", "skeleton", "zombie"},
                    {"phantom"}};
            case G::AETHER:
                // Sky islands + quartz temples: temple guardians (skeleton
                // + warden) and flying animals (llama/deer) as passive.
                return {
                    {"llama", "deer"},
                    {"skeleton"},
                    {"warden"}};
            case G::END:
                // "The End" obsidian plain: phantom/warden + skeleton
                // only, no farmyard.
                return {
                    {},
                    {"skeleton"},
                    {"phantom", "warden"}};
            case G::CREATIVE:
                // Flat creative canvas: farmyard only, no monsters, no
                // villagers (admin creative worlds are usually peaceful
                // sandboxes).
                return {
                    {"cow", "sheep", "pig", "chicken"},
                    {},
                    {}};
            case G::MINING:
                // Mining plane: cave fauna (rabbit/spider) + zombies.
                return {
                    {"rabbit"},
                    {"spider", "zombie"},
                    {}};
            case G::TWILIGHT:
                // Twilight forest: deer/rabbit + wolves, no farmyard.
                return {
                    {"deer", "rabbit"},
                    {"wolf"},
                    {}};
            case G::TEST_GRID:
                // Deterministic block grid for protocol validation: no
                // entities at all so packet golden-tests stay stable.
                return {{}, {}, {}};
            case G::SUPERFLAT:
            case G::OVERWORLD:
            default:
                // Default (and vanilla 1.8 OVERWORLD) mixing so the
                // vanilla-1.8 roster is a subset of what the server
                // seeds, keeping H1Z1 flavour visible to existing
                // testing while vanilla 1.8 clients see the same wire
                // shape they expect.
                return {
                    {"cow", "sheep", "pig", "chicken"},
                    {"zombie", "skeleton", "wolf"},
                    {"wandering_trader"}};
            }
        }

        inline int64_t steadyNowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        inline double clampd(double v, double lo, double hi)
        {
            return (v < lo) ? lo : (v > hi ? hi : v);
        }

        struct Vec3
        {
            double x, y, z;
        };
        inline double len2(const Vec3 &v)
        {
            return v.x * v.x + v.y * v.y + v.z * v.z;
        }

        // Phase 6 (0.0.15) item / XP / loot tables. Kept in the anonymous
        // namespace so applyPlayerAttack (which fires the drop path) and
        // spawnItemDrop (which resolves template aliases to numeric IDs)
        // can share them without cross-TU indirection.
        struct ItemTemplateInfo
        {
            const char *name;
            int32_t itemId;
        };
        static const ItemTemplateInfo kItemTemplates[] = {
            {"leather", 0},
            {"meat_raw", 1},
            {"meat_cooked", 2},
            {"wool", 3},
            {"feather", 4},
            {"wolf_pelt", 5},
            {"pelt", 6},
            {"brain", 7},
            {"bone", 8},
            {"hide", 9},
            {"rabbit_foot", 10},
        };
        inline int32_t lookupItemTemplate(const std::string &name)
        {
            for (const auto &t : kItemTemplates)
                if (name == t.name)
                    return t.itemId;
            return -1;
        }
        inline int xpForTemplate(const std::string &key)
        {
            static const std::pair<const char *, int> tbl[] = {
                {"cow", 3},
                {"sheep", 3},
                {"pig", 3},
                {"chicken", 1},
                {"horse", 3},
                {"deer", 3},
                {"rabbit", 1},
                {"llama", 3},
                {"wolf", 5},
                {"tiger", 5},
                {"bear", 5},
                {"zombie", 8},
                {"bandit", 8},
                {"hunter", 8},
                {"spider", 8},
                {"wandering_trader", 15},
                {"patrol", 15},
                {"warden", 15},
                {"phantom", 15},
            };
            for (const auto &e : tbl)
                if (key == e.first)
                    return e.second;
            return 2;
        }
        inline const char *pickDropForTemplate(const std::string &key)
        {
            static const std::pair<const char *, const char *> tbl[] = {
                {"cow", "leather"},
                {"sheep", "wool"},
                {"pig", "meat_raw"},
                {"chicken", "feather"},
                {"horse", "hide"},
                {"deer", "hide"},
                {"rabbit", "rabbit_foot"},
                {"llama", "wool"},
                {"wolf", "wolf_pelt"},
                {"tiger", "pelt"},
                {"bear", "pelt"},
                {"zombie", "brain"},
                {"bandit", "pelt"},
                {"hunter", "hide"},
                {"spider", "bone"},
                {"wandering_trader", "leather"},
                {"patrol", "leather"},
                {"warden", "leather"},
                {"phantom", "hide"},
            };
            for (const auto &e : tbl)
                if (key == e.first)
                    return e.second;
            return "leather";
        }
    } // anonymous namespace

    // ---------------------------------------------------------------------
    // Construction + refresh of cached gamerules.
    // ---------------------------------------------------------------------
    EntityManager::EntityManager() : m_rng(std::mt19937::result_type())
    {
    }

    // ---------------------------------------------------------------------
    // init - read every relevant gamerule so the per-tick AI does not have
    // to lock GameRules for each entity. Safe to call after GameRules::init().
    // ---------------------------------------------------------------------
    void EntityManager::init(class Storage::Database *db)
    {
        (void)db;
        refreshRuleCache();
    }

    void EntityManager::refreshRuleCache()
    {
        auto &gr = World::GameRules::getInstance();
        bool v;
        if (gr.getBool("spawn_mobs", v))
            m_spawnMobs = v;
        if (gr.getBool("spawn_monsters", v))
            m_spawnMonsters = v;
        if (gr.getBool("spawn_patrols", v))
            m_spawnPatrols = v;
        if (gr.getBool("spawn_phantoms", v))
            m_spawnPhantoms = v;
        if (gr.getBool("spawn_wandering_traders", v))
            m_spawnTraders = v;
        if (gr.getBool("spawn_wardens", v))
            m_spawnWardens = v;
        if (gr.getBool("mob_griefing", v))
            m_griefing = v;
        if (gr.getBool("universal_anger", v))
            m_universalAnger = v;
        int64_t i;
        if (gr.getInt("max_entity_cramming", i))
            m_maxCramming = i;
    }

    // ---------------------------------------------------------------------
    // Spawn / Despawn
    // ---------------------------------------------------------------------
    int32_t EntityManager::spawn(EntityKind kind,
                                 const std::string &templateKey,
                                 double x, double y, double z,
                                 double speed,
                                 uint16_t maxHealth)
    {
        if ((int32_t)m_entities.size() >= m_maxTotal)
            return 0;

        EntityRecord rec;
        rec.id = m_nextId++;
        rec.kind = kind;
        rec.templateKey = templateKey;
        rec.x = x;
        rec.y = y;
        rec.z = z;
        rec.homeX = x;
        rec.homeY = y;
        rec.homeZ = z;
        rec.heading = 0.0;

        if (const TemplateInfo *t = lookupTemplate(templateKey))
        {
            rec.maxHealth = t->hp;
            rec.speed = (speed > 0.0) ? speed : t->speed;
            rec.aggroRange = t->aggroRange;
            rec.leashRange = t->leashRange;
        }
        else
        {
            rec.maxHealth = (maxHealth > 0) ? maxHealth : uint16_t{10};
            rec.speed = (speed > 0.0) ? speed : 1.0;
            rec.aggroRange = m_aggroRadius;
            rec.leashRange = m_leashRadius;
        }
        rec.health = rec.maxHealth;
        rec.lastTickMs = steadyNowMs();
        rec.spawnMs = rec.lastTickMs;
        rec.targetFd = -1;

        m_entities.emplace(rec.id, rec);

        Core::Logger::warn("[Entity] spawned id=" + std::to_string(rec.id) +
                           " kind=" + kindToString(kind) +
                           " templateKey=" + templateKey +
                           " at (" + std::to_string(x) + "," + std::to_string(y) + "," +
                           std::to_string(z) + ")");
        return rec.id;
    }

    bool EntityManager::despawn(int32_t entityId)
    {
        auto it = m_entities.find(entityId);
        if (it == m_entities.end())
            return false;
        Core::Logger::warn("[Entity] despawned id=" + std::to_string(entityId) +
                           " templateKey=" + it->second.templateKey);
        m_entities.erase(it);
        return true;
    }

    bool EntityManager::despawnAll()
    {
        if (m_entities.empty())
            return false;
        Core::Logger::warn("[Entity] despawned ALL (" + std::to_string(m_entities.size()) + ")");
        m_entities.clear();
        return true;
    }

    void EntityManager::clearAll()
    {
        m_entities.clear();
    }

    size_t EntityManager::count() const
    {
        return m_entities.size();
    }

    size_t EntityManager::countKind(EntityKind k) const
    {
        size_t n = 0;
        for (const auto &[id, rec] : m_entities)
            if (rec.kind == k)
                ++n;
        return n;
    }

    const EntityManager::EntityRecord *EntityManager::find(int32_t entityId) const
    {
        auto it = m_entities.find(entityId);
        if (it == m_entities.end())
            return nullptr;
        return &it->second;
    }

    std::vector<int32_t> EntityManager::listIds() const
    {
        std::vector<int32_t> ids;
        ids.reserve(m_entities.size());
        for (const auto &[id, rec] : m_entities)
            ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    void EntityManager::dumpToLog() const
    {
        Core::Logger::warn("[Entity] live=" + std::to_string(m_entities.size()) +
                           " passive=" + std::to_string(countKind(EntityKind::PASSIVE)) +
                           " aggressive=" + std::to_string(countKind(EntityKind::AGGRESSIVE)) +
                           " villager=" + std::to_string(countKind(EntityKind::VILLAGER)) +
                           " max=" + std::to_string(m_maxTotal));
    }

    std::string EntityManager::kindToString(EntityKind k)
    {
        switch (k)
        {
        case EntityKind::PASSIVE:
            return "passive";
        case EntityKind::AGGRESSIVE:
            return "aggressive";
        case EntityKind::VILLAGER:
            return "villager";
        case EntityKind::VEHICLE:
            return "vehicle";
        case EntityKind::PROJECTILE:
            return "projectile";
        case EntityKind::DROPPED_ITEM:
            return "dropped_item";
        case EntityKind::UNKNOWN:
            break;
        }
        return "unknown";
    }

    // ---------------------------------------------------------------------
    // Lazy simulation tick. NetworkManager::pollEvents invokes this roughly
    // every 500 ms; each entity advances by its own lastTickMs delta.
    // ---------------------------------------------------------------------
    void EntityManager::tick(const std::unordered_map<int, LightweightMC::Player::PlayerSession> &clients,
                             int64_t nowMs)
    {
        refreshRuleCache();
        std::vector<int32_t> toDestroy;

        for (auto &[id, rec] : m_entities)
        {
            // DROPPED_ITEM entities have no AI: they sit on the ground and
            // only ever despawn on lifetime expiry. Skipping the nearest-
            // client scan keeps the per-tick cost flat when the world is
            // rich in loot.
            if (rec.kind == EntityKind::DROPPED_ITEM)
            {
                if (nowMs - rec.spawnMs > rec.lifetimeMs)
                    toDestroy.push_back(rec.id);
                continue;
            }

            const int64_t lastTick = rec.lastTickMs;
            const double dtSec = clampd(static_cast<double>(nowMs - lastTick) / 1000.0, 0.0, 1.0);
            rec.lastTickMs = nowMs;

            // Nearest PLAY client (players in a nearby chunk only).
            // Two passes on the same loop: `nearest` is any PLAY client
            // (used by passive flee, villager wish, warden/phantom scan),
            // while `hostile` skips creative/spectator sessions and is
            // the only candidate the AGGRESSIVE branch may attack —
            // vanilla Minecraft disables mob griefing in creative and
            // spectators are not attackable.
            const LightweightMC::Player::PlayerSession *nearest = nullptr;
            const LightweightMC::Player::PlayerSession *hostile = nullptr;
            double nearestD2 = std::numeric_limits<double>::max();
            double hostileD2 = std::numeric_limits<double>::max();
            for (const auto &[fd, ses] : clients)
            {
                if (ses.state != Player::ClientState::PLAY)
                    continue;
                const double dx = ses.x - rec.x;
                const double dy = ses.y - rec.y;
                const double dz = ses.z - rec.z;
                const double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < nearestD2)
                {
                    nearestD2 = d2;
                    nearest = &ses;
                    rec.targetFd = fd;
                }
                const bool peaceful = (ses.gamemode == 1 || ses.gamemode == 3); // creative / spectator
                if (!peaceful && d2 < hostileD2)
                {
                    hostileD2 = d2;
                    hostile = &ses;
                }
            }

            double desiredVx = 0.0, desiredVz = 0.0;
            if (rec.follower && rec.targetFd >= 0)
            {
                auto fIt = clients.find(rec.targetFd);
                if (fIt != clients.end())
                {
                    const auto &ses = fIt->second;
                    const double dx = ses.x - rec.x;
                    const double dz = ses.z - rec.z;
                    const double d2 = dx * dx + dz * dz;
                    if (d2 > 2.25)
                    {
                        const double d = std::sqrt(d2);
                        desiredVx = (dx / d) * rec.speed;
                        desiredVz = (dz / d) * rec.speed;
                    }
                }
                else
                {
                    rec.follower = false;
                }
            }
            else if (rec.kind == EntityKind::PASSIVE)
            {
                if (nearest && nearestD2 <= m_fleeRadius * m_fleeRadius)
                {
                    const double dx = rec.x - nearest->x;
                    const double dz = rec.z - nearest->z;
                    const double d = std::max(0.01, std::sqrt(dx * dx + dz * dz));
                    desiredVx = (dx / d) * rec.speed * 1.4;
                    desiredVz = (dz / d) * rec.speed * 1.4;
                }
                else
                {
                    wanderWish(rec, nowMs, desiredVx, desiredVz);
                }
            }

            else if (rec.kind == EntityKind::AGGRESSIVE)
            {
                const double ar = rec.aggroRange > 0.0 ? rec.aggroRange : m_aggroRadius;
                const bool griefOk = m_griefing || m_universalAnger;
                if (griefOk && hostile && hostileD2 <= ar * ar)
                {
                    rec.angry = true;
                    const double dx = hostile->x - rec.x;
                    const double dz = hostile->z - rec.z;
                    const double d = std::max(0.01, std::sqrt(dx * dx + dz * dz));
                    desiredVx = (dx / d) * rec.speed;
                    desiredVz = (dz / d) * rec.speed;

                    if (d < 1.8 && (nowMs - rec.lastAttackMs) >= m_attackCooldownMs)
                    {
                        rec.lastAttackMs = nowMs;
                        Core::Logger::warn("[Combat] " + rec.templateKey + " id=" + std::to_string(rec.id) +
                                           " attacked " + hostile->username +
                                           " at (" + std::to_string(hostile->x) + "," +
                                           std::to_string(hostile->y) + "," +
                                           std::to_string(hostile->z) + ")");
                    }
                }
                else
                {
                    rec.angry = false;
                    wanderWish(rec, nowMs, desiredVx, desiredVz);
                }
            }
            else if (rec.kind == EntityKind::VILLAGER)
            {
                villagerWish(rec, nearest, nearestD2, nowMs, desiredVx, desiredVz);
            }

            // Leash: yank back toward home when a mob strays beyond its
            // leashRange. This gives cows a small paddock and lets wardens
            // defend a much larger protected zone (per their template value).
            const double hx = rec.x - rec.homeX;
            const double hz = rec.z - rec.homeZ;
            const double hDist = std::sqrt(hx * hx + hz * hz);
            if (hDist > rec.leashRange && hDist > 0.01)
            {
                const double pull = (rec.speed * 1.2) / hDist;
                desiredVx -= hx * pull;
                desiredVz -= hz * pull;
            }

            // Integrate the wish velocity.
            rec.vx = desiredVx;
            rec.vy = 0.0;
            rec.vz = desiredVz;
            rec.x += rec.vx * dtSec;
            rec.y += rec.vy * dtSec;
            rec.z += rec.vz * dtSec;
            rec.heading = std::atan2(rec.vz, rec.vx);

            // Despawn on lifetime expiry so the model does not accumulate
            // entities forever when nobody is nearby.
            if (nowMs - rec.spawnMs > rec.lifetimeMs)
                toDestroy.push_back(rec.id);
        }

        for (const int32_t id : toDestroy)
            despawn(id);
    }

    // ---------------------------------------------------------------------
    // wanderWish - small random walk that biases back toward home. Kept
    // deterministic-ish by seeding from id+time so replays reproduce.
    // ---------------------------------------------------------------------
    void EntityManager::wanderWish(EntityRecord &rec, int64_t nowMs, double &outVx, double &outVz)
    {
        std::uniform_real_distribution<double> jitter(-0.9, 0.9);
        const double t = static_cast<double>(nowMs >> 11);
        std::mt19937::result_type seed =
            static_cast<std::mt19937::result_type>(rec.id * 2654435761u) ^
            static_cast<std::mt19937::result_type>(static_cast<uint64_t>(t));
        auto rng = m_rng;
        rng.seed(seed);
        const double a = jitter(rng);
        outVx = std::cos(a) * rec.speed * 0.55;
        outVz = std::sin(a) * rec.speed * 0.55;
        m_rng = rng;
    }

    // ---------------------------------------------------------------------
    // villagerWish - dispatch on template. Trader wanders wide, patrol
    // walks a bigger loop, warden herds players away, phantom hunts at
    // "night" (we approximate night as "no PLAY session within 40 blocks"
    // - good enough while the server has no day/night cycle).
    // ---------------------------------------------------------------------
    void EntityManager::villagerWish(EntityRecord &rec,
                                     const LightweightMC::Player::PlayerSession *nearest,
                                     double nearestD2, int64_t nowMs,
                                     double &outVx, double &outVz)
    {
        if (rec.templateKey == "warden")
        {
            if (nearest && nearestD2 <= rec.aggroRange * rec.aggroRange)
            {
                const double dx = nearest->x - rec.x;
                const double dz = nearest->z - rec.z;
                const double d = std::max(0.01, std::sqrt(dx * dx + dz * dz));
                outVx = (dx / d) * rec.speed;
                outVz = (dz / d) * rec.speed;
                if (d < 6.0 && (nowMs - rec.lastAttackMs) >= 4000)
                {
                    rec.lastAttackMs = nowMs;
                    Core::Logger::warn("[Villager] warden id=" + std::to_string(rec.id) +
                                       " warning issued to " + nearest->username);
                }
                return;
            }
        }
        else if (rec.templateKey == "phantom")
        {
            if (nearest && nearestD2 <= rec.aggroRange * rec.aggroRange)
            {
                const double dx = nearest->x - rec.x;
                const double dz = nearest->z - rec.z;
                const double d = std::max(0.01, std::sqrt(dx * dx + dz * dz));
                outVx = (dx / d) * rec.speed;
                outVz = (dz / d) * rec.speed;
                return;
            }
        }
        else if (rec.templateKey == "wandering_trader")
        {
            static thread_local int64_t lastGreetMs = 0;
            if (nowMs - lastGreetMs > 20000)
            {
                lastGreetMs = nowMs;
                Core::Logger::warn("[Villager] wandering_trader id=" + std::to_string(rec.id) +
                                   " greeting issued (trade window pending)");
            }
        }
        // Otherwise fall through to wander.
        wanderWish(rec, nowMs, outVx, outVz);
    }

    // ---------------------------------------------------------------------
    // Player interaction hooks.
    // ---------------------------------------------------------------------
    bool EntityManager::applyPlayerAttack(int32_t entityId, int32_t attackerFd,
                                          int64_t nowMs, int32_t damageAmount)
    {
        auto it = m_entities.find(entityId);
        if (it == m_entities.end())
            return false;
        auto &rec = it->second;
        (void)nowMs;

        const int damage = (damageAmount > 0) ? damageAmount : 3;
        rec.health = (rec.health > damage) ? static_cast<uint16_t>(rec.health - damage)
                                           : uint16_t{0};
        Core::Logger::warn("[Entity] id=" + std::to_string(entityId) +
                           " templateKey=" + rec.templateKey +
                           " hp=" + std::to_string(rec.health) + "/" +
                           std::to_string(rec.maxHealth));

        if (rec.health == 0)
        {
            Core::Logger::warn("[Entity] id=" + std::to_string(entityId) +
                               " slain by fd=" + std::to_string(attackerFd));

            // mob_drops gamerule gates whether the corpse yields loot.
            // We snapshot the drop template BEFORE erasing so spawnItemDrop
            // can key off the same name lookup.
            const std::string dropKey = pickDropForTemplate(rec.templateKey);
            const double dropX = rec.x;
            const double dropY = rec.y;
            const double dropZ = rec.z;
            bool dropEnabled = true;
            bool ruleVal = false;
            if (World::GameRules::getInstance().getBool("mob_drops", ruleVal))
                dropEnabled = ruleVal;

            despawn(entityId);

            if (dropEnabled && rec.kind != EntityKind::DROPPED_ITEM)
            {
                const int32_t dropId = spawnItemDrop(dropKey, dropX, dropY, dropZ);
                (void)dropId;
            }
            return true;
        }

        if (rec.kind == EntityKind::AGGRESSIVE)
        {
            rec.angry = true;
            rec.targetFd = attackerFd;
        }
        return true;
    }

    // ---------------------------------------------------------------------
    // Phase 6 (0.0.15) vanilla-1.8 XP-on-kill and mob loot tables.
    //
    // XP follows the vanilla 1.8 kill-XP table (a handful of canonical
    // values). Villagers award a bigger chunk because they are rarer and
    // the trader / warden quests reward a big XP burst.
    //
    // Loot is gated by the mob_drops gamerule; when the rule is disabled
    // nothing is spawned. When the rule is enabled we always drop at least
    // one thing per mob template. Explosions / fall damage do NOT trigger
    // this path -- the mob_explosion_drop_decay / block_explosion_drop_decay
    // rules are consulted separately at their own call sites.
    // ---------------------------------------------------------------------
    int EntityManager::grantKillXpToSession(int32_t entityId,
                                            LightweightMC::Player::PlayerSession &session,
                                            int64_t nowMs)
    {
        (void)nowMs;
        auto it = m_entities.find(entityId);
        if (it == m_entities.end())
            return 0;
        const int xp = xpForTemplate(it->second.templateKey);
        const int gained = session.grantXp(xp);
        Core::Logger::warn("[Entity XP] template=" + it->second.templateKey +
                           " granted=" + std::to_string(xp) +
                           " levels=" + std::to_string(gained));
        return gained;
    }

    int32_t EntityManager::spawnItemDrop(const std::string &templateKey,
                                         double x, double y, double z,
                                         int32_t itemId, int32_t stackCount)
    {
        if ((int32_t)m_entities.size() >= m_maxTotal)
            return 0;
        int32_t id32 = itemId;
        if (id32 <= 0)
        {
            const int32_t resolved = lookupItemTemplate(templateKey);
            id32 = (resolved > 0) ? resolved : 0;
        }
        if (stackCount <= 0)
            stackCount = 1;
        if (stackCount > 12)
            stackCount = 12;

        EntityRecord rec;
        rec.id = m_nextId++;
        rec.kind = EntityKind::DROPPED_ITEM;
        rec.templateKey = templateKey;
        rec.x = x;
        rec.y = y;
        rec.z = z;
        rec.homeX = x;
        rec.homeY = y;
        rec.homeZ = z;
        rec.maxHealth = 1;
        rec.health = 1;
        rec.itemId = id32;
        rec.stackCount = stackCount;
        rec.speed = 0.0;
        rec.aggroRange = 0.0;
        rec.leashRange = 0.0;
        rec.spawnMs = steadyNowMs();
        rec.lastTickMs = rec.spawnMs;
        // Ground items decay faster than mobs (5 min) so loot piles
        // do not stay around forever.
        rec.lifetimeMs = 300000;
        m_entities.emplace(rec.id, rec);
        Core::Logger::warn("[Entity] dropped item id=" + std::to_string(rec.id) +
                           " template=" + templateKey +
                           " itemId=" + std::to_string(id32) +
                           " stack=" + std::to_string(stackCount) +
                           " at (" + std::to_string(x) + "," +
                           std::to_string(y) + "," + std::to_string(z) + ")");
        return rec.id;
    }

    bool EntityManager::removeItemDrop(int32_t entityId)
    {
        auto it = m_entities.find(entityId);
        if (it == m_entities.end())
            return false;
        if (it->second.kind != EntityKind::DROPPED_ITEM)
            return false;
        Core::Logger::warn("[Entity] picked up item id=" + std::to_string(entityId) +
                           " template=" + it->second.templateKey);
        m_entities.erase(it);
        return true;
    }

    bool EntityManager::applyPlayerFollow(int32_t entityId, int32_t followerFd)
    {
        auto it = m_entities.find(entityId);
        if (it == m_entities.end())
            return false;
        it->second.follower = true;
        it->second.targetFd = followerFd;
        Core::Logger::warn("[Entity] id=" + std::to_string(entityId) + " now follows fd=" + std::to_string(followerFd));
        return true;
    }

    bool EntityManager::applyPlayerUnfollow(int32_t entityId)
    {
        auto it = m_entities.find(entityId);
        if (it == m_entities.end())
            return false;
        it->second.follower = false;
        it->second.targetFd = -1;
        Core::Logger::warn("[Entity] id=" + std::to_string(entityId) + " stopped following");
        return true;
    }

    // ---------------------------------------------------------------------
    // Seed a small default roster around a fresh PLAY session. Uses the
    // spawn_* gamerules to decide what kind to seed and honours max_entity
    // cramming so we do not spawn a wall of mobs on login.
    // ---------------------------------------------------------------------
    void EntityManager::spawnForSession(const LightweightMC::Player::PlayerSession &session)
    {
        refreshRuleCache();

        const int32_t existing = static_cast<int32_t>(m_entities.size());
        if (existing >= m_maxCramming)
        {
            Core::Logger::warn("[Entity] skipping seed spawn for " + session.username +
                               " (max_entity_cramming " + std::to_string(m_maxCramming) + " reached)");
            return;
        }

        std::uniform_real_distribution<double> jitter(-12.0, 12.0);
        auto rng = m_rng;

        auto place = [&](const std::string &templateKey, EntityKind kind) -> void
        {
            const double ox = jitter(rng);
            const double oz = jitter(rng);
            spawn(kind, templateKey, session.x + ox, session.y, session.z + oz);
        };

        // Passive: always at least one cow/sheep so the world feels alive.
        if (m_spawnMobs)
        {
            place("cow", EntityKind::PASSIVE);
            place("sheep", EntityKind::PASSIVE);
        }
        // Aggressive: only when the world allows it.
        if (m_spawnMonsters)
        {
            place("wolf", EntityKind::AGGRESSIVE);
            place("zombie", EntityKind::AGGRESSIVE);
        }
        // Villagers (H1Z1 style).
        if (m_spawnTraders)
            place("wandering_trader", EntityKind::VILLAGER);
        if (m_spawnPatrols)
            place("patrol", EntityKind::VILLAGER);
        if (m_spawnWardens)
            place("warden", EntityKind::VILLAGER);
        if (m_spawnPhantoms)
            place("phantom", EntityKind::VILLAGER);

        m_rng = rng;
        Core::Logger::warn("[Entity] seeded roster for " + session.username +
                           " (spawn_mobs=" + std::to_string(m_spawnMobs) +
                           " spawn_monsters=" + std::to_string(m_spawnMonsters) +
                           " villagers=" + std::to_string(m_spawnPatrols || m_spawnTraders || m_spawnWardens || m_spawnPhantoms) + ")");
    }

    // Terrain-aware overload. Validates every candidate (x, z) offset against
    // WorldStorage: the column must contain a feet-Y where a standing mob
    // fits -- solid block under the feet + 2 air blocks above (feet + head).
    // Vanilla Java Edition refuses to spawn standing mobs inside walls or
    // under 1-block ceilings; matching that check here is what keeps cows
    // / wolves / zombies from being buried in generated or player-placed
    // terrain. When no offset in the sample set yields a legal position we
    // fall back to the classic session.y path so the mob is still visible
    // to the client.
    void EntityManager::spawnForSession(const LightweightMC::Player::PlayerSession &session,
                                        Storage::WorldStorage &worldStorage)
    {
        refreshRuleCache();

        const int32_t existing = static_cast<int32_t>(m_entities.size());
        if (existing >= m_maxCramming)
        {
            Core::Logger::warn("[Entity] skipping seed spawn for " + session.username +
                               " (max_entity_cramming " + std::to_string(m_maxCramming) + " reached)");
            return;
        }

        std::uniform_real_distribution<double> jitter(-12.0, 12.0);
        auto rng = m_rng;
        const int hintY = static_cast<int>(session.y);

        auto place = [&](const std::string &templateKey, EntityKind kind) -> void
        {
            // Sample up to 12 candidate offsets; keep the first whose column
            // satisfies the 2-air rule. Cap bounds the per-login cost (worst
            // case ~12 * ~255 Y-probes, still well under a ms).
            constexpr int kMaxTries = 12;
            bool placed = false;
            for (int attempt = 0; attempt < kMaxTries && !placed; ++attempt)
            {
                const double ox = jitter(rng);
                const double oz = jitter(rng);
                const int wx = static_cast<int>(std::floor(session.x + ox));
                const int wz = static_cast<int>(std::floor(session.z + oz));
                const int feet = worldStorage.findStandingFeetY(wx, wz, hintY);
                if (feet < 0)
                    continue;
                // Snap to the block centre so the client's collision box
                // stays clear of adjacent walls.
                spawn(kind, templateKey,
                      wx + 0.5, static_cast<double>(feet), wz + 0.5);
                placed = true;
            }
            if (!placed)
            {
                spawn(kind, templateKey, session.x, session.y, session.z);
                Core::Logger::warn("[Entity] no legal 2-air column near " + session.username +
                                   " for " + templateKey + "; falling back to player altitude");
            }
        };

        if (m_spawnMobs)
        {
            place("cow", EntityKind::PASSIVE);
            place("sheep", EntityKind::PASSIVE);
        }
        if (m_spawnMonsters)
        {
            place("wolf", EntityKind::AGGRESSIVE);
            place("zombie", EntityKind::AGGRESSIVE);
        }
        if (m_spawnTraders)
            place("wandering_trader", EntityKind::VILLAGER);
        if (m_spawnPatrols)
            place("patrol", EntityKind::VILLAGER);
        if (m_spawnWardens)
            place("warden", EntityKind::VILLAGER);
        if (m_spawnPhantoms)
            place("phantom", EntityKind::VILLAGER);

        m_rng = rng;
        Core::Logger::warn("[Entity] seeded terrain-aware roster for " + session.username +
                           " (spawn_mobs=" + std::to_string(m_spawnMobs) +
                           " spawn_monsters=" + std::to_string(m_spawnMonsters) + ")");
    }

    // Same as the two-argument overload but also emits an S2C SpawnEntityLiving
    // (wire ID 0x0f per PrismarineJS protocol_1_8 Clientbound) to the given
    // client when networkSyncEnabled() is true (default ON since Phase 7
    // era-appropriate sync). Wire layout mirrors
    // protocol_types.hpp's SpawnEntityLiving_d6229256: VarInt entity_id,
    // byte type, three int32 fixed-point coordinates scaled by 1000, three
    // int8 yaw/pitch/headPitch, then the vanilla 1.8 wire tail -- three
    // big-endian signed shorts for velocity (x/y/z, fixed-point scale 5000)
    // and one byte length prefix for the metadata blob (PrismarineJS's
    // velocity/metadata vector abstraction is a length-prefixed byte blob,
    // but vanilla 1.8's netty decoder reads the velocity slot as three
    // raw shorts followed by a one-byte metadata length, not as two
    // length-prefixed blobs).  Vanilla 1.8 clients that don't register
    // 0x0f will silently ignore the packet -- enable defaults ON since
    // Phase 7 era-appropriate sync (vanilla 1.8 is the target era); an
    // admin who wants H1Z1 byte-aligned replay recording or who runs
    // against MODERN / LATEST clients that mis-parse 0x0f as SpawnPaint
    // calls setNetworkSync(false).
    void EntityManager::spawnForSession(const LightweightMC::Player::PlayerSession &session,
                                        Storage::WorldStorage &worldStorage,
                                        int clientFd, const Managers::PacketSender &sendPacket)
    {
        refreshRuleCache();

        const int32_t existing = static_cast<int32_t>(m_entities.size());
        if (existing >= m_maxCramming)
        {
            Core::Logger::warn("[Entity] skipping seed spawn for " + session.username +
                               " (max_entity_cramming " + std::to_string(m_maxCramming) + " reached)");
            return;
        }

        std::uniform_real_distribution<double> jitter(-12.0, 12.0);
        auto rng = m_rng;
        const int hintY = static_cast<int>(session.y);
        const bool emitWire = m_networkSync;

        auto emitSpawnPacket = [&](const EntityRecord &rec)
        {
            if (!emitWire)
                return;
            std::vector<uint8_t> payload;
            Network::Packet::writeVarInt(payload, rec.id);
            payload.push_back(lookupTemplate(rec.templateKey)->mobId); // type byte -- 0 = default / unrecognised
            const int32_t fx = static_cast<int32_t>(rec.x * 32.0);
            const int32_t fy = static_cast<int32_t>(rec.y * 32.0);
            const int32_t fz = static_cast<int32_t>(rec.z * 32.0);
            Network::Packet::writeInt(payload, fx);
            Network::Packet::writeInt(payload, fy);
            Network::Packet::writeInt(payload, fz);
            payload.push_back(0); // yaw
            payload.push_back(0); // pitch
            payload.push_back(0); // headPitch
            // Vanilla 1.8 client's SPacketSpawnEntity parses the tail as
            // three big-endian signed shorts for velocity (x, y, z, fixed
            // point scale 5000) followed by one byte-length-prefixed
            // metadata blob.  Earlier attempts (writeVarInt(0) or two
            // writeShort(0)) left the client underflowing netty's ByteBuf
            // at readerIndex(N)+length(2) > writerIndex(N).  Emitting all
            // three velocity shorts plus a 1-byte metadata length keeps
            // the packet exactly self-describing and lets the client's
            // decoder terminate cleanly at the wire end.
            Network::Packet::writeShort(payload, 0); // velocity x (fixed 5000)
            Network::Packet::writeShort(payload, 0); // velocity y
            Network::Packet::writeShort(payload, 0); // velocity z
            // payload.push_back(0);                    // metadata byte-length (0)
            payload.push_back(0x7F); // Octet de fin des métadonnées (terminator 127)
            sendPacket(clientFd, 0x0f, payload);
            LightweightMC::Core::Logger::warn("Spawn entity sent 0x0f...");
        };

        auto place = [&](const std::string &templateKey, EntityKind kind) -> void
        {
            constexpr int kMaxTries = 12;
            bool placed = false;
            for (int attempt = 0; attempt < kMaxTries && !placed; ++attempt)
            {
                const double ox = jitter(rng);
                const double oz = jitter(rng);
                const int wx = static_cast<int>(std::floor(session.x + ox));
                const int wz = static_cast<int>(std::floor(session.z + oz));
                const int feet = worldStorage.findStandingFeetY(wx, wz, hintY);
                if (feet < 0)
                    continue;
                const int32_t id = spawn(kind, templateKey,
                                         wx + 0.5, static_cast<double>(feet), wz + 0.5);
                if (id > 0)
                {
                    if (const EntityRecord *rec = find(id))
                        emitSpawnPacket(*rec);
                }
                placed = true;
            }
            if (!placed)
            {
                const int32_t id = spawn(kind, templateKey, session.x, session.y, session.z);
                if (id > 0)
                {
                    if (const EntityRecord *rec = find(id))
                        emitSpawnPacket(*rec);
                }
                Core::Logger::warn("[Entity] no legal 2-air column near " + session.username +
                                   " for " + templateKey + "; falling back to player altitude");
            }
        };

        // Phase 7 (0.0.15): era-appropriate mob roster. Consult the world's
        // GenStyle so a Nether plane gets spiders/skeletons rather than the
        // default farmyard, a Creative plane gets only animals, a TEST_GRID
        // gets nothing. Gating is per-template via the same GameRules
        // snapshot the mob/monster/trader/patrol/warden/phantom buckets
        // already use (m_spawnMobs gates farmyard animals; m_spawnMonsters
        // gates monsters; m_spawnTraders/Patrols/Wardens/Phantoms gate the
        // matching villager templates), so admins can still toggle each
        // bucket without editing the roster table.
        const Storage::GenStyle plane = worldStorage.getGenerationStyle();
        const StyleRoster roster = rosterForStyle(plane);
        for (const auto &key : roster.passive)
        {
            if (!m_spawnMobs)
                break;
            place(key, EntityKind::PASSIVE);
        }
        for (const auto &key : roster.aggressive)
        {
            if (!m_spawnMonsters)
                break;
            place(key, EntityKind::AGGRESSIVE);
        }
        for (const auto &key : roster.villagers)
        {
            if (key == "wandering_trader" && m_spawnTraders)
                place(key, EntityKind::VILLAGER);
            else if (key == "patrol" && m_spawnPatrols)
                place(key, EntityKind::VILLAGER);
            else if (key == "warden" && m_spawnWardens)
                place(key, EntityKind::VILLAGER);
            else if (key == "phantom" && m_spawnPhantoms)
                place(key, EntityKind::VILLAGER);
        }

        m_rng = rng;
        Core::Logger::warn("[Entity] seeded terrain-aware roster for " + session.username +
                           " (spawn_mobs=" + std::to_string(m_spawnMobs) +
                           " spawn_monsters=" + std::to_string(m_spawnMonsters) + ")");
    }

    void EntityManager::setAggroRadius(double r) { m_aggroRadius = r; }
    void EntityManager::setFleeRadius(double r) { m_fleeRadius = r; }
    void EntityManager::setLeashRadius(double r) { m_leashRadius = r; }
    void EntityManager::setAttackCooldownMs(int64_t ms) { m_attackCooldownMs = ms; }
    void EntityManager::setMaxTotal(int32_t cap) { m_maxTotal = cap; }
    void EntityManager::setNetworkSync(bool enabled) { m_networkSync = enabled; }

} // namespace LightweightMC::World
