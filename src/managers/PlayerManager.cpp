#include "managers/PlayerManager.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "core/Logger.hpp"
#include "core/utils.hpp"
#include "storage/WorldStorage.hpp"
#include "storage/Database.hpp"
#include "core/ConfigManager.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace LightweightMC::Player
{
    // Free function (declared in managers/PlayerManager.hpp) so the network login
    // handler can generate UUIDs without instantiating the PlayerManager singleton.
    std::string generatePlayerUuid(std::string_view username)
    {
        // 1. Concaténation "OfflinePlayer:<username>"
        std::string source = "OfflinePlayer:";
        source.append(username);

        // 2. MD5 de la chaîne
        auto hash = Core::md5(source);

        // 3. Spécification UUID v3 (RFC 4122)
        hash[6] = (hash[6] & 0x0f) | 0x30; // Version 3
        hash[8] = (hash[8] & 0x3f) | 0x80; // Variant RFC 4122

        // 4. Formatage via std::snprintf (36 caractères + 1 pour '\0')
        char buffer[37];
        std::snprintf(
            buffer, sizeof(buffer),
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            hash[0], hash[1], hash[2], hash[3],
            hash[4], hash[5],
            hash[6], hash[7],
            hash[8], hash[9],
            hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);

        return std::string(buffer, 36);
    }

    std::string PlayerManager::getPlayerUuid(const std::string &username)
    {
        return generatePlayerUuid(username);
    }

    std::vector<uint8_t> PlayerManager::serializeInventory(const std::unordered_map<int, ItemStack> &inventory)
    {
        std::vector<uint8_t> buffer;

        int32_t validCount = 0;
        for (const auto &[slot, item] : inventory)
        {
            if (item.id != -1 && item.count > 0)
            {
                validCount++;
            }
        }

        Network::Packet::writeVarInt(buffer, validCount);

        for (const auto &[slot, item] : inventory)
        {
            if (item.id != -1 && item.count > 0)
            {
                Network::Packet::writeShort(buffer, static_cast<int16_t>(slot));
                Network::Packet::writeShort(buffer, item.id);
                buffer.push_back(item.count);
                Network::Packet::writeShort(buffer, item.damage);
            }
        }

        return buffer;
    }

    void PlayerManager::deserializeInventory(const std::vector<uint8_t> &data, std::unordered_map<int, ItemStack> &outInventory)
    {
        outInventory.clear();
        if (data.empty())
            return;

        size_t offset = 0;
        int32_t count = Network::Packet::readVarInt(data.data(), offset, data.size());

        for (int32_t i = 0; i < count && offset < data.size(); ++i)
        {
            int16_t slot = Network::Packet::readShort(data.data(), offset);
            int16_t itemId = Network::Packet::readShort(data.data(), offset);
            uint8_t itemCount = data[offset++];
            int16_t itemDamage = Network::Packet::readShort(data.data(), offset);

            if (itemId != -1 && itemCount > 0)
            {
                outInventory[slot] = ItemStack{itemId, itemCount, itemDamage};
            }
        }
    }

    void PlayerManager::teleportPlayer(int fd, PlayerSession &session, double x, double y, double z, float yaw, float pitch, const PacketSender &sendPacket)
    {
        Core::Logger::debug("[PLAYER tp] teleportPlayer fd=" + std::to_string(fd) +
                            " target=(" + std::to_string(x) + ", " + std::to_string(y) + ", " +
                            std::to_string(z) + ") yaw=" + std::to_string(yaw) + " pitch=" + std::to_string(pitch));
        session.x = x;
        session.y = y;
        session.z = z;
        session.yaw = yaw;
        session.pitch = pitch;

        std::vector<uint8_t> packet;
        Network::Packet::writeDouble(packet, x);
        Network::Packet::writeDouble(packet, y);
        Network::Packet::writeDouble(packet, z);
        Network::Packet::writeFloat(packet, yaw);
        Network::Packet::writeFloat(packet, pitch);
        packet.push_back(0x00); // Flags: 0x00 = Absolute coordinates

        Core::Logger::debug("[PLAYER tp] sending S2C Position packet (1.8 id 0x08) to fd=" + std::to_string(fd) +
                            " payloadBytes=" + std::to_string(packet.size()));
        sendPacket(fd, 0x08, packet);
    }

    void PlayerManager::sendPlayPackets(int clientFd, PlayerSession &currentSession, const std::string &username,
                                        std::unordered_map<int, PlayerSession> &clients, World::WorldManager &worldManager,
                                        Storage::Database &db,
                                        const PacketSender &sendPacket, const BroadcastSender &broadcastPacket)
    {
        currentSession.username = username;
        currentSession.state = ClientState::PLAY;

        std::string playerUuid = getPlayerUuid(username);
        Storage::PlayerData playerData;

        bool loaded = db.loadPlayerData(playerUuid, playerData);

        if (loaded)
        {
            currentSession.x = playerData.lastX;
            currentSession.y = playerData.lastY;
            currentSession.z = playerData.lastZ;
            currentSession.yaw = std::fmod(playerData.lastYaw, 360.0f);
            if (currentSession.yaw < 0.0f)
            {
                currentSession.yaw += 360.0f;
            }
            currentSession.pitch = playerData.lastPitch;
            // Restore the plane the previous session was on so a player who
            // /gotor'd to Nether / End / Aether / a custom world reconnects on
            // the same plane instead of silently walking back to Overworld.
            // loadedChunks is empty on login, so the client re-streams the
            // target plane's terrain on the first updateChunksAroundPlayer.
            currentSession.worldId = playerData.worldId;
            // Persisted game mode must be restored so a client that was set
            // to survival/adventure/etc. reconnects in the same mode
            // (JoinGame's 2nd byte reads currentSession.gamemode).
            currentSession.gamemode = playerData.gamemode;
            // Persisted operator level must also be restored so that a
            // player who was made an operator via the console (/op or /deop)
            // keeps their privileges after a re-login -- and across a server
            // restart (both paths come from the DB row, not from the previous
            // live session). Without this line every fresh login silently
            // drops the player back to opLevel=0 and CommandManager's
            // per-command gate rejects /gamemode, /gamerule, /op, etc.
            currentSession.opLevel = playerData.opLevel;
            deserializeInventory(playerData.inventoryData, currentSession.inventory);
            // Persisted survival stats must also be restored so a re-joining
            // player's HUD matches the DB row: HP bar, food (both the vanilla
            // 1.8 "hunger" level and the saturation/exhaustion floats that
            // drive natural_health_regeneration) and XP level/progress/total.
            // Without these fields the PlayerSession ctor defaults (health=20
            // / foodLevel=20 / experienceLevel=0) shadow whatever
            // PlayerManager::savePlayerState wrote on the previous logout, so
            // "my XP is gone on reconnect" and "health bar resets" both happen
            // every time a client reconnects.
            //
            // If the previous session died (health=0 / alive=false) we respawn
            // the player at full HP -- same rule PlayerSession::loadFrom uses
            // and the same rule NetworkGameplay::applyDamageToPlayer's offline
            // branch enforces (data.health<=0 -> next login sees 20 HP).
            currentSession.health = playerData.health;
            currentSession.foodLevel = playerData.foodLevel;
            currentSession.foodSaturationLevel = playerData.foodSaturationLevel;
            currentSession.foodExhaustionLevel = playerData.foodExhaustionLevel;
            currentSession.experienceLevel = playerData.experienceLevel;
            currentSession.experienceProgress = playerData.experienceProgress;
            currentSession.totalExperience = playerData.totalExperience;
            currentSession.alive = currentSession.health > 0.0f;
            if (!currentSession.alive)
            {
                currentSession.health = 20.0f;
                currentSession.alive = true;
            }
            Core::Logger::info("Restored player state for UUID [" + playerUuid + "] (" + username + ") at (" +
                               std::to_string(currentSession.x) + ", " + std::to_string(currentSession.y) + ", " + std::to_string(currentSession.z) + ") with " +
                               std::to_string(currentSession.inventory.size()) + " saved items on plane worldId=" +
                               std::to_string(currentSession.worldId) +
                               " hp=" + std::to_string(currentSession.health) +
                               " food=" + std::to_string(currentSession.foodLevel) +
                               " xp_level=" + std::to_string(currentSession.experienceLevel) +
                               " xp_total=" + std::to_string(currentSession.totalExperience) + ".");
        }
        else
        {
            currentSession.x = 0.5;
            currentSession.z = 0.5;
            // Terrain-aware first-login spawn. The vanilla Java Edition
            // spawn rule for a 2-tall standing entity is: solid block
            // directly below the feet AND air at both the feet block AND
            // the head block (i.e. "at least 2 air blocks"). We apply the
            // exact same predicate WorldStorage::findStandingFeetY uses
            // for mob spawns so a new player never appears inside generated
            // terrain or a player-built wall. The hint (50) matches the
            // historical default so that if no legal feet-Y exists (rare:
            // entire column blocked), we still fall back to y=50 and the
            // client sees the classic behaviour.
            const int feetY = worldManager.getWorldStorageFor(currentSession.worldId).findStandingFeetY(0, 0, 50);
            currentSession.y = (feetY >= 0) ? static_cast<double>(feetY) : 50.0;
            currentSession.yaw = 0.0f;
            currentSession.pitch = 0.0f;
            if (feetY >= 0)
            {
                Core::Logger::info("Created new player session for UUID [" + playerUuid +
                                   "] (" + username + ") grounded at (0.5, " +
                                   std::to_string(feetY) + ", 0.5) -- "
                                   "column passed the 2-air-above rule");
            }
            else
            {
                Core::Logger::warn("New player spawn column (0,0) has no legal feet-Y; "
                                   "falling back to y=50 default");
                Core::Logger::info("Created new player session for UUID [" + playerUuid +
                                   "] (" + username + ") at fallback (0.5, 50.0, 0.5)");
            }
        }

        // -------------------------------------------------------------
        // A. Send required initialization packets (Minecraft 1.8)
        // -------------------------------------------------------------

        // 1. Join Game (Packet 0x01)
        std::vector<uint8_t> joinGame;
        Network::Packet::writeInt(joinGame, clientFd); // Entity ID
        joinGame.push_back(currentSession.gamemode);   // Gamemode (persisted in DB / PlayerSession)
        // Dimension byte for sky render. Vanilla 1.8 clients only know three
        // sky colours keyed by this byte -- 0 (Overworld blue), 1 (End /
        // Star Keep purple) and -1 (Nether / dark grey). Hard-coding 0 here
        // forced every relogin through an Overworld sky even when
        // currentSession.worldId pointed at Nether, End, Aether, or a
        // custom plane -- the reported "reconnecting defaults to Overworld
        // sky" bug. Resolve the client-facing hint via WorldManager::skyDimensionFor
        // (vanilla plane ids return themselves; custom planes report their
        // registered sky hint or 0 by default) and cast to a signed byte on
        // the wire so Nether's -1 becomes 0xFF, which vanilla expects.
        joinGame.push_back(static_cast<uint8_t>(static_cast<int8_t>(worldManager.skyDimensionFor(currentSession.worldId))));
        // 0.0.15 phase 14a: difficulty byte now comes from the canonical [gameplay]
        // difficulty gamerule key in server.properties (ConfigManager::forEachCanonicalDefault
        // emit) instead of a hardcoded literal; clamped to 0-3 so a bad file value
        // degrades gracefully instead of shifting subsequent wire bytes.
        joinGame.push_back(static_cast<uint8_t>(std::clamp(
            LightweightMC::ConfigManager::getInstance().get<int>("gameplay", "difficulty", 1), 0, 3)));
        if (currentSession.clientProtocol >= 47)
        {
            // Max Players, Level Type and Reduced Debug Info only exist in 1.8+ (protocol >= 47).
            // Network::Packet::writeInt(joinGame, 100);         // Max Players
            joinGame.push_back(static_cast<uint8_t>(std::clamp(
                LightweightMC::ConfigManager::getInstance().get<int>("server", "max-players", 100), 1, 255)));
            Network::Packet::writeString(joinGame, "flat"); // Level Type
            joinGame.push_back(0);                          // Reduced Debug Info
        }
        sendPacket(clientFd, 0x01, joinGame);

        // 2. Spawn Position (Packet 0x05)
        std::vector<uint8_t> spawnPos;
        if (currentSession.clientProtocol >= 47)
        {
            // 1.8+: packed long (angle:bit6 | yaw:bit6 | pos:bit32*3), then pitch/yaw floats.
            uint64_t location = ((0ULL & 0x3FFFFFF) << 38) | ((6ULL & 0xFFF) << 26) | (0ULL & 0x3FFFFFF);
            for (int i = 7; i >= 0; --i)
                spawnPos.push_back((location >> (i * 8)) & 0xFF);
        }
        else
        {
            // 1.7.10: x:double, y:double, z:double, yaw:float, pitch:float (no packed long).
            Network::Packet::writeDouble(spawnPos, currentSession.x);
            Network::Packet::writeDouble(spawnPos, currentSession.y);
            Network::Packet::writeDouble(spawnPos, currentSession.z);
            Network::Packet::writeFloat(spawnPos, currentSession.yaw);
            Network::Packet::writeFloat(spawnPos, currentSession.pitch);
        }
        sendPacket(clientFd, 0x05, spawnPos);

        // 3. Load chunks around player before sending position
        worldManager.updateChunksAroundPlayer(clientFd, currentSession, 3, sendPacket);

        // 4. Player Position And Look (Packet 0x08)
        std::vector<uint8_t> posLook;
        Network::Packet::writeDouble(posLook, currentSession.x);
        Network::Packet::writeDouble(posLook, currentSession.y);
        Network::Packet::writeDouble(posLook, currentSession.z);
        Network::Packet::writeFloat(posLook, currentSession.yaw);
        Network::Packet::writeFloat(posLook, currentSession.pitch);
        posLook.push_back(0x00); // Flags (0 = Absolute coordinates)
        sendPacket(clientFd, 0x08, posLook);

        // 4b. Health + Experience HUD refresh so the client's HP bar and
        // XP bar start at the DB-loaded values rather than at the client's
        // own hard-coded "full health / level 1" defaults. Without this
        // pair a reconnecting player sees 20 HP / level 1 briefly even
        // when the restored session only had 4 HP or had leveled to 5,
        // and any damage/XP command that fires after login then paints a
        // totally unrelated bar value. Era mapping (0x06/0x1f wire ids) is
        // handled inside NetworkFraming::sendPacket via
        // ProtocolVersion::s2cWireIdFromLegacy, so we use vanilla-1.8
        // packet ids unconditionally here.
        {
            std::vector<uint8_t> hpPayload;
            Network::Packet::writeFloat(hpPayload, currentSession.health);
            Network::Packet::writeVarInt(hpPayload, static_cast<int32_t>(currentSession.foodLevel));
            Network::Packet::writeFloat(hpPayload, currentSession.foodSaturationLevel);
            sendPacket(clientFd, 0x06, hpPayload);

            std::vector<uint8_t> xpPayload;
            Network::Packet::writeFloat(xpPayload, currentSession.experienceProgress);
            Network::Packet::writeVarInt(xpPayload, static_cast<int32_t>(currentSession.experienceLevel));
            Network::Packet::writeVarInt(xpPayload, currentSession.totalExperience);
            sendPacket(clientFd, 0x1f, xpPayload);
        }

        // 5. Synchronize inventory to client screen via Window Items (Packet 0x30)
        std::vector<uint8_t> windowItems;
        windowItems.push_back(0);                     // Window ID 0 = Inventory
        Network::Packet::writeShort(windowItems, 45); // 45 slots

        for (int16_t i = 0; i < 45; ++i)
        {
            auto it = currentSession.inventory.find(i);
            if (it != currentSession.inventory.end() && it->second.id != -1 && it->second.count > 0)
            {
                auto translated = Network::Protocol::ProtocolTranslator::translateItemToClient(currentSession.clientProtocol, it->second.id, it->second.count, it->second.damage);
                if (currentSession.clientProtocol >= 47)
                {
                    // 1.8+: short id, byte count, short damage, NBT tag byte.
                    Network::Packet::writeShort(windowItems, translated.id);
                    windowItems.push_back(translated.count);
                    Network::Packet::writeShort(windowItems, translated.damage);
                    windowItems.push_back(0); // NBT tag = 0
                }
                else
                {
                    // 1.7.10: short id, byte count, short damage (no NBT tag byte).
                    Network::Packet::writeShort(windowItems, translated.id);
                    windowItems.push_back(translated.count);
                    Network::Packet::writeShort(windowItems, translated.damage);
                }
            }
            else
            {
                if (currentSession.clientProtocol >= 47)
                    Network::Packet::writeShort(windowItems, -1); // 1.8+ empty slot marker
                // 1.7.10: an empty slot is simply no data at all.
            }
        }
        sendPacket(clientFd, 0x30, windowItems);

        // -------------------------------------------------------------
        // B. Add new player to everyone's Tablist (0x38)
        // -------------------------------------------------------------
        std::vector<uint8_t> addTab;
        Network::Packet::writeVarInt(addTab, 0); // Action 0 = Add Player
        Network::Packet::writeVarInt(addTab, 1); // 1 player

        for (int i = 0; i < 12; ++i)
            addTab.push_back(0);
        addTab.push_back((clientFd >> 24) & 0xFF);
        addTab.push_back((clientFd >> 16) & 0xFF);
        addTab.push_back((clientFd >> 8) & 0xFF);
        addTab.push_back(clientFd & 0xFF);

        Network::Packet::writeString(addTab, username);
        Network::Packet::writeVarInt(addTab, 0); // Properties
        Network::Packet::writeVarInt(addTab, currentSession.gamemode); // Gamemode (mirrors JoinGame)
        Network::Packet::writeVarInt(addTab, 0); // Ping
        addTab.push_back(0);                     // Display name

        broadcastPacket(0x38, addTab, -1);

        // -------------------------------------------------------------
        // C. Spawn NEW player for EXISTING players (0x0C)
        // -------------------------------------------------------------
        std::vector<uint8_t> spawnNewPlayer;
        Network::Packet::writeVarInt(spawnNewPlayer, clientFd);

        for (int i = 0; i < 12; ++i)
            spawnNewPlayer.push_back(0);
        spawnNewPlayer.push_back((clientFd >> 24) & 0xFF);
        spawnNewPlayer.push_back((clientFd >> 16) & 0xFF);
        spawnNewPlayer.push_back((clientFd >> 8) & 0xFF);
        spawnNewPlayer.push_back(clientFd & 0xFF);

        Network::Packet::writeInt(spawnNewPlayer, static_cast<int32_t>(currentSession.x * 32.0));
        Network::Packet::writeInt(spawnNewPlayer, static_cast<int32_t>(currentSession.y * 32.0));
        Network::Packet::writeInt(spawnNewPlayer, static_cast<int32_t>(currentSession.z * 32.0));
        spawnNewPlayer.push_back(static_cast<uint8_t>(currentSession.yaw));
        spawnNewPlayer.push_back(static_cast<uint8_t>(currentSession.pitch));
        Network::Packet::writeShort(spawnNewPlayer, 0);
        spawnNewPlayer.push_back(0x7F);

        broadcastPacket(0x0C, spawnNewPlayer, clientFd);

        // -------------------------------------------------------------
        // D. Spawn EXISTING players for the NEW player
        // -------------------------------------------------------------
        for (const auto &[otherFd, otherSession] : clients)
        {
            if (otherFd != clientFd && otherSession.state == ClientState::PLAY)
            {
                std::vector<uint8_t> addExistingTab;
                Network::Packet::writeVarInt(addExistingTab, 0);
                Network::Packet::writeVarInt(addExistingTab, 1);
                for (int i = 0; i < 12; ++i)
                    addExistingTab.push_back(0);
                addExistingTab.push_back((otherFd >> 24) & 0xFF);
                addExistingTab.push_back((otherFd >> 16) & 0xFF);
                addExistingTab.push_back((otherFd >> 8) & 0xFF);
                addExistingTab.push_back(otherFd & 0xFF);

                Network::Packet::writeString(addExistingTab, otherSession.username);
                Network::Packet::writeVarInt(addExistingTab, 0);
                Network::Packet::writeVarInt(addExistingTab, 1);
                Network::Packet::writeVarInt(addExistingTab, 0);
                addExistingTab.push_back(0);
                sendPacket(clientFd, 0x38, addExistingTab);

                std::vector<uint8_t> spawnExisting;
                Network::Packet::writeVarInt(spawnExisting, otherFd);
                for (int i = 0; i < 12; ++i)
                    spawnExisting.push_back(0);
                spawnExisting.push_back((otherFd >> 24) & 0xFF);
                spawnExisting.push_back((otherFd >> 16) & 0xFF);
                spawnExisting.push_back((otherFd >> 8) & 0xFF);
                spawnExisting.push_back(otherFd & 0xFF);

                Network::Packet::writeInt(spawnExisting, static_cast<int32_t>(otherSession.x * 32.0));
                Network::Packet::writeInt(spawnExisting, static_cast<int32_t>(otherSession.y * 32.0));
                Network::Packet::writeInt(spawnExisting, static_cast<int32_t>(otherSession.z * 32.0));
                spawnExisting.push_back(static_cast<uint8_t>(otherSession.yaw));
                spawnExisting.push_back(static_cast<uint8_t>(otherSession.pitch));
                Network::Packet::writeShort(spawnExisting, 0);
                spawnExisting.push_back(0x7F);

                sendPacket(clientFd, 0x0C, spawnExisting);
            }
        }

        Core::Logger::info("Player " + username + " connected and spawned in the world!");
    }

    void PlayerManager::broadcastEquipment(int entityId, int16_t itemSlot, int16_t itemId, int32_t clientProtocol, const BroadcastSender &broadcastPacket)
    {
        std::vector<uint8_t> packet;
        Network::Packet::writeVarInt(packet, entityId);
        Network::Packet::writeShort(packet, itemSlot);

        auto translatedItem = Network::Protocol::ProtocolTranslator::translateItemToClient(clientProtocol, itemId);

        Network::Packet::writeShort(packet, translatedItem.id);
        if (translatedItem.id != -1)
        {
            packet.push_back(translatedItem.count);
            Network::Packet::writeShort(packet, translatedItem.damage);
            if (clientProtocol >= 47)
                packet.push_back(0); // NBT tag = end of compound (1.8+ only)
        }

        broadcastPacket(0x04, packet, entityId);
    }

    void PlayerManager::broadcastEquipmentForSession(PlayerSession &session, int16_t slot, const BroadcastSender &broadcastPacket)
    {
        // Entity Equipment (S2C 0x04): entity ID, equipment slot (short), then the item.
        // Legacy (pre-1.8) items are short id + byte count + short damage; 1.8+ adds an
        // NBT tag byte after the damage value.
        std::vector<uint8_t> packet;
        Network::Packet::writeVarInt(packet, session.fd);
        Network::Packet::writeShort(packet, slot);

        auto it = session.inventory.find(slot);
        if (it != session.inventory.end() && it->second.id != -1 && it->second.count > 0)
        {
            auto translated = Network::Protocol::ProtocolTranslator::translateItemToClient(
                session.clientProtocol, it->second.id, it->second.count, it->second.damage);
            Network::Packet::writeShort(packet, translated.id);
            packet.push_back(translated.count);
            Network::Packet::writeShort(packet, translated.damage);
            if (session.clientProtocol >= 47)
                packet.push_back(0); // NBT tag = end of compound
        }
        else
        {
            Network::Packet::writeShort(packet, -1); // empty slot marker
        }

        broadcastPacket(0x04, packet, session.fd);
    }

    void PlayerManager::savePlayerState(const PlayerSession &session, Storage::Database &db)
    {
        if (session.username.empty() || session.state != ClientState::PLAY)
            return;

        Storage::PlayerData data;
        data.uuid = getPlayerUuid(session.username);
        data.username = session.username;

        // Copy all persistent state (position, health, food, gamemode, spawn
        // points, XP, ...) from the session into the database row.
        session.applyTo(data);

        // 0.0.15 plane-switch inventory policy: SYNC planes (default for
        // Overworld / Nether / End / Aether / mining worlds) keep using
        // the flat players.inventory_blob column; SEPARATE / RESTRICTED
        // planes (creative sandboxes and PVP / PVE training maps) route
        // the current session's inventory to the per-plane row on
        // player_inventories(uuid, session.worldId) instead, and re-load
        // the existing flat SYNC blob into data.inventoryData so the
        // INSERT OR REPLACE that Database::savePlayerData runs preserves
        // the shared SYNC chain rather than overwriting it with a plane
        // blob (which would wipe items a mining player had on their
        // SYNC slot when they logged a save flush while standing on a
        // creative sandbox).
        const int32_t planePolicy = db.getWorldInventoryPolicy(session.worldId);
        const std::vector<uint8_t> sessionBlob = Player::PlayerManager::serializeInventory(session.inventory);
        if (planePolicy == Storage::kInventoryPolicySync)
        {
            data.inventoryData = sessionBlob;
        }
        else
        {
            // Preserve the shared SYNC blob (a first-time SYNC save may
            // leave it empty; that is the correct "no items yet" state).
            std::vector<uint8_t> syncBlob;
            db.loadSyncInventoryBlob(data.uuid, syncBlob);
            data.inventoryData.swap(syncBlob);
            if (!db.savePlayerInventory(data.uuid, session.worldId, sessionBlob))
            {
                Core::Logger::warn("savePlayerState: could not persist plane-scoped "
                                   "inventory for " + session.username + " on worldId=" +
                                   std::to_string(session.worldId) + " (SEPARATE / RESTRICTED slot)");
            }
        }

        if (db.savePlayerData(data))
        {
            Core::Logger::info("Successfully saved player state to SQLite for UUID [" + data.uuid + "] (" + session.username + ") with " +
                               std::to_string(data.inventoryData.size()) + " bytes of inventory payload.");
        }
    }
}
