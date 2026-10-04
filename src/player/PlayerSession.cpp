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

} // namespace LightweightMC::Player