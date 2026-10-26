#include "player/PlayerSession.hpp"
#include "versions/v1_8/health.hpp"
#include "storage/Database.hpp"
#include "world/GameRules.hpp"
#include <algorithm>

namespace LightweightMC::Player
{
    void PlayerSession::loadFrom(const Storage::PlayerData &data)
    {
        // Position & rotation (the spawn position the client should restore).
        x = data.lastX;
        y = data.lastY;
        z = data.lastZ;
        yaw = data.lastYaw;
        pitch = data.lastPitch;

        // player.dat-mirrored persistent fields.
        health = std::clamp(data.health, 0.0f, 20.0f);
        foodLevel = std::clamp<int8_t>(data.foodLevel, 0, 20);
        foodSaturationLevel = std::clamp(data.foodSaturationLevel, 0.0f, 20.0f);
        foodExhaustionLevel = std::clamp(data.foodExhaustionLevel, 0.0f, 20.0f);
        gamemode = data.gamemode;

        // Spawn point (world spawn). Only trusted when flagged.
        if (data.hasSpawn)
        {
            spawnX = static_cast<int32_t>(data.spawnX);
            spawnY = static_cast<int32_t>(data.spawnY);
            spawnZ = static_cast<int32_t>(data.spawnZ);
            hasSpawn = true;
        }
        else
        {
            hasSpawn = false;
            spawnX = 0;
            spawnY = 64;
            spawnZ = 0;
        }

        // Bed spawn ("not present" when not flagged or coordinates invalid).
        if (data.hasBedSpawn)
        {
            bedX = data.bedX;
            bedY = data.bedY;
            bedZ = data.bedZ;
            hasBedSpawn = true;
        }
        else
        {
            bedX = -1;
            bedY = -1;
            bedZ = -1;
            hasBedSpawn = false;
        }

        // Volatile-at-login fields: restored from disk so the player returns
        // to their last known condition (vanilla persists these in player.dat).
        fallDistance = data.fallDistance;
        air = std::clamp(data.air, 0, 30);
        onFire = data.onFire;
        fire = data.fire;

        sleeping = false; // never restored: always wake up on login
        sleepTimer = 0;
        flying = false; // flight is re-enabled by the client when applicable
        sprinting = false;

        experienceLevel = data.experienceLevel;
        experienceProgress = std::clamp(data.experienceProgress, 0.0f, 1.0f);
        totalExperience = data.totalExperience;

        // Operator status (0 = not an operator, 1-4 = permission levels).
        opLevel = data.opLevel;

        deathCount = data.deathCount;
        lastSeen = data.lastSeen;

        // Phase 6: session-only alive/lastHurtMs/respawnAtMs always reset
        // to a fresh-awake state on login. If a saved row carries health
        // at 0 (e.g. the player logged out mid-death), we restore them
        // alive with full HP rather than queuing a partial-death respawn,
        // which matches vanilla 1.8's "log out then log back in" semantics.
        alive = true;
        lastHurtMs = 0;
        respawnAtMs = 0;

        // Plane the previous session was on (0 by default for fresh rows or
        // rows migrated without the world_id column; pre-1.12 -1 for Nether,
        // 1 for End, 100 for the Aether special dimension, 101+ for `/world
        // create` planes). loadedChunks is empty on login so the client
        // re-streams the new plane's terrain without an explicit clear here.
        worldId = data.worldId;
    }

    void PlayerSession::applyTo(Storage::PlayerData &data) const
    {
        // Position & rotation (last known position at save time).
        data.lastX = x;
        data.lastY = y;
        data.lastZ = z;
        data.lastYaw = yaw;
        data.lastPitch = pitch;

        // player.dat-mirrored persistent fields.
        data.health = health;
        data.foodLevel = foodLevel;
        data.foodSaturationLevel = foodSaturationLevel;
        data.foodExhaustionLevel = foodExhaustionLevel;
        data.gamemode = gamemode;

        // Spawn point (world spawn).
        data.hasSpawn = hasSpawn;
        if (hasSpawn)
        {
            data.spawnX = static_cast<double>(spawnX);
            data.spawnY = static_cast<double>(spawnY);
            data.spawnZ = static_cast<double>(spawnZ);
        }

        // Bed spawn.
        data.hasBedSpawn = hasBedSpawn;
        if (hasBedSpawn)
        {
            data.bedX = bedX;
            data.bedY = bedY;
            data.bedZ = bedZ;
        }
        else
        {
            data.bedX = -1;
            data.bedY = -1;
            data.bedZ = -1;
        }

        // Volatile-at-login fields (persisted so the next login restores them).
        data.fallDistance = fallDistance;
        data.air = air;
        data.onFire = onFire;
        data.fire = fire;

        data.sleeping = sleeping;
        data.sleepTimer = sleepTimer;
        data.flying = flying;
        data.sprinting = sprinting;

        data.experienceLevel = experienceLevel;
        data.experienceProgress = experienceProgress;
        data.totalExperience = totalExperience;

        // Operator status (0 = not an operator, 1-4 = permission levels).
        data.opLevel = opLevel;

        data.deathCount = deathCount;

        // Multiworld plane selector. Persisted with every save so a `/gotor`
        // or `/world join` switch on a live session is picked up on the next
        // login (see Database::loadPlayerData -> LoginHandler in PlayerManager).
        data.worldId = worldId;

        // Flag that the payload carries real session coordinates so Database::
        // savePlayerData knows to mirror them into the per-plane `player_posi-
        // tions` table (standard Minecraft keeps one coordinate space per plane
        // -- see the matching block in Database.cpp). Partial-write callers
        // (setPlayerOpLevel, setPlayerGameMode) build a fresh PlayerData and
        // never call applyTo, so hasPositionData stays false and the mirror is
        // skipped, preventing a default 0.5/7.0/0.5 from clobbering the plane's
        // last-known position.
        data.hasPositionData = true;
    }

    void PlayerSession::resetVolatileState()
    {
        health = LightweightMC::Versions::V1_8::MAX_HEALTH;
        foodLevel = LightweightMC::Versions::V1_8::MAX_FOOD;
        foodSaturationLevel = 5.0f; /* V1_8 default */
        foodExhaustionLevel = 0.0f;
        fallDistance = 0.0f;
        air = 30;
        onFire = false;
        fire = 0;
        sleeping = false;
        sleepTimer = 0;
        flying = false;
        sprinting = false;

        // Phase 6 (vanilla-1.8 HP / respawn): respawn state resets to
        // alive on login. lastHurtMs / respawnAtMs are wall-clock ms and
        // callers seed them on the first applyDamage/onDeath after this
        // point, so zero is a legal "no damage yet" sentinel.
        alive = true;
        lastHurtMs = 0;
        respawnAtMs = 0;

        // ---- Phase 14c movement-validation bookkeeping (session-only). Seed the
        // tracker with the spawn point so the first incoming 0x04/0x06 packet after
        // login or respawn compares against a legitimate anchor rather than stale
        // coordinates from a previous plane. lastMoveMs stays 0 to signal "no prior
        // accepted move yet", which handlePlayerMovement treats as the login burst.
        lastMoveX = x;
        lastMoveY = y;
        lastMoveZ = z;
        lastMoveMs = 0;

        // Hunger cadence cursor: first tickSurvivalDrain after login/respawn anchors here so idle depletion starts from a full saturation window rather than an epoch-stale elapsed flood.
        lastHungerTickMs = 0;

        // Clear transient per-session container windows: on reconnect a fresh
        // window is opened that re-populates its contents from the SQLite
        // block_entities row for the container's coord.
        openWindows.clear();
        openWindowId = 0;

        // Spawn at the configured spawn point (bed first, then world spawn).
        if (hasBedSpawn)
        {
            x = static_cast<double>(bedX) + 0.5;
            y = static_cast<double>(bedY);
            z = static_cast<double>(bedZ) + 0.5;
        }
        else if (hasSpawn)
        {
            x = static_cast<double>(spawnX) + 0.5;
            y = static_cast<double>(spawnY);
            z = static_cast<double>(spawnZ) + 0.5;
        }
        else
        {
            x = 0.5;
            y = 7.0;
            z = 0.5;
        }
        yaw = 0.0f;
        pitch = 0.0f;
    }

    // ------------------------------------------------------------
    // Survival hunger pipeline (wiki/Hunger.md flow, best-effort not byte-exact 1.8 step counters):
    //   movement / action hooks accrue foodExhaustionLevel (see PlayerHandler.cpp::handlePlayerMovement exhaustion-only hook)
    //   tickSurvivalDrain consumes pending whole exhaustion units into saturation loss every 20 Hz NetworkGameplay sweep
    //   plus an idle standing drain floor on its own wall-clock cadence so non-moving survival players still bleed saturation slowly
    // When saturation bottoms at <=0 AND foodLevel>0: one starvation step --foodLevel, snap saturation back full (20), zero pending exhaustion remainder.
    // Creative/adventure/spectator short-circuit; gamerule `hunger-decay-rate-percent` scales motion + idle pressure (hidden extensibility hook like other gamerule reads — no ConfigManager key registration churn).
    void PlayerSession::tickSurvivalDrain(int64_t nowMs)
    {
        if (!alive || gamemode != 0) // survival-only depletion; creative/adventure/spectator untouched
            return;

        auto &rules = LightweightMC::World::GameRules::getInstance();
        int64_t decayPct = 100;
        if (!rules.getInt("hunger-decay-rate-percent", decayPct) || decayPct < 0)
            decayPct = 100;
        const double rate = static_cast<double>(decayPct) / 100.0;

        // Idle-standing baseline drain even when zero pending exhaustion from movement: wall-clock accumulator so standing still also decays saturation on cadence (first call anchors cursor).
        if (lastHungerTickMs == 0)
            lastHungerTickMs = nowMs;

        const int64_t elapsedMs = nowMs - lastHungerTickMs;
        const int64_t idleTicks = elapsedMs / 50; // 20 Hz loop cadence
        if (idleTicks > 0)
        {
            lastHungerTickMs += idleTicks * 50;
            const double idlePressure = static_cast<double>(idleTicks) * 0.02 * rate;
            foodExhaustionLevel += static_cast<float>(idlePressure);
            if (foodExhaustionLevel > 40.0f)
                foodExhaustionLevel = 40.0f;
        }

        // Convert pending whole exhaustion units into saturation loss regardless of whether movement just fired this tick.
        while (foodExhaustionLevel >= 1.0f && foodLevel >= 0)
        {
            foodExhaustionLevel -= 1.0f;
            if (foodSaturationLevel > 0.0f)
                foodSaturationLevel = std::max(0.0f, foodSaturationLevel - 1.0f);
            else if (foodLevel > 0)
            {
                // Starvation transition per observed client logs: empty saturation snaps full and burns one whole food unit.
                --foodLevel;
                foodSaturationLevel = 20.0f;
            }
        }

        if (foodSaturationLevel < 0.0f)
            foodSaturationLevel = 0.0f;
    }

    // ------------------------------------------------------------
    // Phase 6 (0.0.15) vanilla-1.8 HP / damage / death / respawn / XP.
    // The simulation is intentionally event-driven (see README: "Zero-T
    // ick"): applyDamage / tickRegen callers pull wall-clock ms from
    // std::chrono::steady_clock and pass it through so the state machine
    // advances only when an actual game event fires (chat damage packet,
    // UseEntity attack, movement delta, chat command). Idle sessions pay
    // no CPU cost.
    // ------------------------------------------------------------
    bool PlayerSession::applyDamage(float amount, int64_t nowMs, int damageTypeCode)
    {
        // Damage is a no-op for a session already queued for respawn and
        // for invulnerable game modes (creative/adventure/spectator).
        // PvP / fall / drowning / fire / freeze gating is the caller's
        // responsibility -- see NetworkManager::applyDamageToPlayer and
        // EntityManager::tick which consult GameRules for each damage type.
        if (!alive || amount <= 0.0f)
            return false;
        if (gamemode == 1 || gamemode == 3)
            return false; // creative / spectator invuln

        health -= amount;
        lastHurtMs = nowMs;
        if (health <= 0.0f)
        {
            health = 0.0f;
            onDeath(nowMs);
        }
        (void)damageTypeCode;
        return true;
    }

    // Vanilla 1.8 natural_health_regeneration rule: after 5 s without
    // taking damage AND while foodLevel >= 9 (with foodSaturationLevel
    // > 0 so the "starving" state disables regen), HP gains one half
    // heart every 2 s of elapsed no-damage time. The state machine is
    // lazy: on each tick we compute how many regen units have accrued
    // since lastHurtMs and consume them. lastHurtMs then advances to
    // `nowMs - (elapsed mod 2000)` so the 2 s step keeps its remainder
    // across polls without double-counting.
    bool PlayerSession::tickRegen(int64_t nowMs)
    {
        if (!alive)
            return false;
        if (health >= 20.0f)
            return false;
        if (foodLevel < 18)
            return false;
        if (foodSaturationLevel <= 0.0f)
            return false;

        // Skip regen while on fire or drowning. fire / air counters are
        // maintained elsewhere (fire ticks decrement in a future PR;
        // air decrement happens on movement).
        if (onFire)
            return false;
        if (air <= 0)
            return false;

        // Optional hidden trigger: `regen-delay-ticks` scales the vanilla-1.8
        // ~5 s no-damage window through the 20 Hz tick loop (default 100 ticks
        // -> 5000 ms). Absent rules keep byte-for-byte vanilla pacing; future ABI/plugin layers can lower/raise it without breaking natural regen structure (comment mirrors wiki/Game-Rules.md Player category discipline like other gamerule knobs e.g. walk-speed-bps / sprint-speed-bps read in PlayerHandler.cpp::handlePlayerMovement).
        auto &rules = LightweightMC::World::GameRules::getInstance();
        int64_t delayTicks = 100; // 20 Hz * 5 s == vanilla-1.8 half-second regen gate window
        if (!rules.getInt("regen-delay-ticks", delayTicks) || delayTicks < 0)
            delayTicks = 100;
        const int64_t regenDelayMs = delayTicks * 20;

        const int64_t elapsed = nowMs - lastHurtMs;
        if (elapsed < regenDelayMs)
            return false;
        const int64_t regenWindow = elapsed - regenDelayMs;
        const int64_t regenTicks = regenWindow / 2000;
        if (regenTicks <= 0)
            return false;

        const float before = health;
        health += 0.5; // static_cast<float>(regenTicks);
        if (health > 20.0f)
            health = LightweightMC::Versions::V1_8::MAX_HEALTH;
        // Re-anchor lastHurtMs so the 2 s cadence carries across calls:
        // regen window consumed, then (regenTicks * 2000) ms of regen.
        lastHurtMs += regenDelayMs + regenTicks * 2000;

        // Note: regeneration strictly advances HP pacing (+0.5 half-hearts per heal event) and re-anchors the hurt window; it no longer touches saturation. Survival depletion lives in PlayerSession::tickSurvivalDrain (called every 20 Hz NetworkGameplay sweep via NetworkManager::tickRegeneration for live PLAY sessions — exhaustion accrual comes from movement hooks, idle pressure uses lastHungerTickMs wall-clock cadence so standing still also drains). Heal ticks no longer compound against saturation drift that caused the "lose ~3 exhaustion whenever +1 HP" reports.

        return health > before;
    }

    void PlayerSession::onDeath(int64_t nowMs)
    {
        alive = false;
        respawnAtMs = nowMs;
        if (deathCount < 255)
            ++deathCount;

        // Phase 14 hunger-on-death correction: vanilla softcore refreshes the
        // survival window the moment a session goes dead, not only when it lands
        // back at spawn. Callers such as NetworkManager::killPlayer broadcast an
        // Update Health (0x06) immediately after onDeath(), so without this snap
        // they keep shipping stale food=starving bars until autoRespawnDuePlayers
        // eventually runs respawn(). Calling restoreHungerForRespawn() here makes
        // death unconditional for hunger regardless of keep_inventory; respawn()
        // repeats the same values at spawn for consistency and movement bookkeeping.
        restoreHungerForRespawn();
    }

    void PlayerSession::respawn()
    {
        // Phase 14b + follow-up: keep_inventory no longer gates hunger restoration on death. Vanilla softcore refills food regardless of items carried: reset foodLevel to full (20), snap saturation to the post-starvation baseline (5.0f), and clear pending exhaustion so respawn starts with a clean survival window independent of item persistence (wiki/Game-Rules.md Player category discipline for keep_inventory still governs XP/items elsewhere, not hunger drain here).
        alive = true;
        respawnAtMs = 0;
        lastHurtMs = 0;
        health = LightweightMC::Versions::V1_8::MAX_HEALTH;
        foodLevel = LightweightMC::Versions::V1_8::MAX_FOOD;
        foodSaturationLevel = 5.0f; /* V1_8 default */
        foodExhaustionLevel = 0.0f;
        // Hunger cadence cursor: first tickSurvivalDrain after login/respawn anchors here so idle depletion starts from a full saturation window rather than an epoch-stale elapsed flood.
        lastHungerTickMs = 0;

        fallDistance = 0.0f;
        air = 30;
        onFire = false;
        fire = 0;
        onGround = false;
        sleeping = false;
        sleepTimer = 0;
        flying = false;
        sprinting = false;
        // Same priority order as resetVolatileState: bed first, then world
        // spawn, else literal (0.5, 7.0, 0.5).
        if (hasBedSpawn)
        {
            x = static_cast<double>(bedX) + 0.5;
            y = static_cast<double>(bedY);
            z = static_cast<double>(bedZ) + 0.5;
        }
        else if (hasSpawn)
        {
            x = static_cast<double>(spawnX) + 0.5;
            y = static_cast<double>(spawnY);
            z = static_cast<double>(spawnZ) + 0.5;
        }
        else
        {
            x = 0.5;
            y = 7.0;
            z = 0.5;
        }
    }

    // Companion to onDeath()/respawn(): vanilla-1.8 softcore survival semantics
    // say the hunger meter is refilled whenever a session becomes dead (client
    // immediately sees Update Health 0x06 with full bars) and again when respawn()
    // drops them at spawn. keep_inventory continues to control XP/item persistence
    // via other paths — it never suppresses this food reset.
    void PlayerSession::restoreHungerForRespawn()
    {
        foodLevel = LightweightMC::Versions::V1_8::MAX_FOOD;
        foodSaturationLevel = 5.0f; /* V1_8 default */
        foodExhaustionLevel = 0.0f;
        // Hunger cadence cursor: first tickSurvivalDrain after login/respawn anchors here so idle depletion starts from a full saturation window rather than an epoch-stale elapsed flood.
        lastHungerTickMs = 0;
    }

    bool PlayerSession::consumeSelectedFood(int32_t foodPoints, float saturatingPoints)
    {
        if (selectedSlot < 0 || selectedSlot > 8)
            return false;

        const int16_t slotKey = static_cast<int16_t>(36 + selectedSlot);
        auto it = inventory.find(slotKey);
        if (it == inventory.end() || it->second.id <= 0 || it->second.count == 0)
            return false;

        // Vanilla-1.8 survival math: food level is a small integer 0..20 and
        // saturating floats are clamped by current food once full ("min(food, sat)"
        // rule used by tickSurvivalDrain/starvation). Feeding adds raw points and
        // saturation gain but stays within legal ranges so clients don't see ghost bars.
        const int32_t previousFood = foodLevel;
        int32_t newFood = previousFood + foodPoints;
        if (newFood < 0)
            newFood = 0;
        else if (newFood > 20)
            newFood = 20;
        foodLevel = static_cast<int8_t>(newFood);

        float satGain = saturatingPoints;
        if (satGain < 0.0f)
            satGain = 0.0f;
        // Cap saturation by the lesser of remaining room to next starvation step
        // and vanilla max tied to foodLevel as used in tickSurvivalDrain checks.
        const float cap = static_cast<float>(std::min(20, newFood));
        foodSaturationLevel = std::min(cap, foodSaturationLevel + satGain);
        if (foodExhaustionLevel < 0.0f)
            foodExhaustionLevel = 0.0f;

        // Consume exactly one held unit; drop the slot when empty so later
        // clicks don't retrigger eating on a phantom item and inventory[36..44]
        // matches what handleHeldItemChange would show next equip packet.
        if (it->second.count > 0)
            --it->second.count;
        return true;
    }

    // Vanilla Java Edition 1.8 XP curve (see minecraft.wiki /w/XP):
    // linear 1000 XP per level up to level 15. Levels above 15 are not
    // tracked; totalExperience still accumulates so /experience <set>
    // can push arbitrary totals.

    int PlayerSession::grantXp(int32_t amount)
    {
        if (amount == 0)
            return 0;
        const int32_t prevLevel = experienceLevel;
        totalExperience += amount;
        if (totalExperience < 0)
            totalExperience = 0;
        experienceLevel = std::min(15, totalExperience / 1000);
        const int remainder = totalExperience - experienceLevel * 1000;
        experienceProgress = static_cast<float>(remainder) / 1000.0f;
        return experienceLevel - prevLevel;
    }

    // ------------------------------------------------------------
    // Compact blob encoding for container (chest/furnace/etc.) contents:
    //   varint(count-of-non-empty) followed by
    //   count × (slot:short, id:short, count:byte, damage:short)
    // Big-endian shorts, Minecraft-varint count. Empty containers
    // serialize as a single 0x00 byte (varint count = 0).
    // ------------------------------------------------------------
    void serializeContainerItems(const std::vector<ItemStack> &items,
                                 std::vector<uint8_t> &out)
    {
        uint32_t nonEmpty = 0;
        for (const auto &it : items)
            if (it.id != -1 && it.count > 0)
                ++nonEmpty;

        uint32_t v = nonEmpty;
        do
        {
            uint8_t b = static_cast<uint8_t>(v & 0x7FU);
            v >>= 7;
            if (v)
                b |= 0x80;
            out.push_back(b);
        } while (v);

        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            const ItemStack &it = items[i];
            if (it.id == -1 || it.count == 0)
                continue;
            const int16_t slotId = static_cast<int16_t>(i);
            out.push_back(static_cast<uint8_t>((slotId >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(slotId & 0xFF));
            out.push_back(static_cast<uint8_t>((it.id >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(it.id & 0xFF));
            out.push_back(it.count);
            out.push_back(static_cast<uint8_t>((it.damage >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(it.damage & 0xFF));
        }
    }

    void deserializeContainerItems(const std::vector<uint8_t> &blob,
                                   int expectedSlotCount,
                                   std::vector<ItemStack> &out)
    {
        out.assign(expectedSlotCount > 0 ? expectedSlotCount : 0,
                   ItemStack{-1, 0, 0});
        if (blob.empty())
            return;

        size_t pos = 0;
        uint32_t count = 0;
        int shift = 0;
        while (pos < blob.size())
        {
            uint8_t b = blob[pos++];
            count |= static_cast<uint32_t>(b & 0x7FU) << shift;
            if ((b & 0x80) == 0)
                break;
            shift += 7;
            if (shift > 28)
                break;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            if (pos + 7 > blob.size())
                break;
            uint16_t slot = static_cast<uint16_t>((static_cast<uint16_t>(blob[pos]) << 8) | blob[pos + 1]);
            pos += 2;
            ItemStack it;
            it.id = static_cast<int16_t>((static_cast<int16_t>(static_cast<uint16_t>(blob[pos]) << 8)) |
                                         static_cast<uint16_t>(blob[pos + 1]));
            pos += 2;
            it.count = blob[pos++];
            it.damage = static_cast<int16_t>((static_cast<int16_t>(static_cast<uint16_t>(blob[pos]) << 8)) |
                                             static_cast<uint16_t>(blob[pos + 1]));
            pos += 2;
            // `slot` comes off the wire as a uint16_t, so a `slot >= 0` test
            // is always true (-Wtype-limits). out was sized to
            // expectedSlotCount above, so comparing against out.size() is the
            // real bounds check and keeps the write in-bounds for malformed
            // container blobs (corrupt save data, hand-edited SQLite rows).
            if (static_cast<std::size_t>(slot) < out.size())
                out[slot] = it;
        }
    }

} // namespace LightweightMC::Player