#include "player/PlayerSession.hpp"
#include "storage/Database.hpp"
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
        health = 20.0f;
        foodLevel = 20;
        foodSaturationLevel = 5.0f;
        foodExhaustionLevel = 0.0f;
        fallDistance = 0.0f;
        air = 30;
        onFire = false;
        fire = 0;
        sleeping = false;
        sleepTimer = 0;
        flying = false;
        sprinting = false;

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
            if (it.id != -1 && it.count > 0) ++nonEmpty;

        uint32_t v = nonEmpty;
        do
        {
            uint8_t b = static_cast<uint8_t>(v & 0x7FU);
            v >>= 7;
            if (v) b |= 0x80;
            out.push_back(b);
        } while (v);

        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            const ItemStack &it = items[i];
            if (it.id == -1 || it.count == 0) continue;
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
        if (blob.empty()) return;

        size_t pos = 0;
        uint32_t count = 0;
        int shift = 0;
        while (pos < blob.size())
        {
            uint8_t b = blob[pos++];
            count |= static_cast<uint32_t>(b & 0x7FU) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
            if (shift > 28) break;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            if (pos + 7 > blob.size()) break;
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