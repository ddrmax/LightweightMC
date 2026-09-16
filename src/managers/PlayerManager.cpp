#include "managers/PlayerManager.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "core/Logger.hpp"
#include <cstdio>

namespace LightweightMC::Player
{
    std::string PlayerManager::getPlayerUuid(const std::string &username)
    {
        uint64_t hash1 = 0xcbf29ce484222325ULL;
        uint64_t hash2 = 0x100000001b3ULL;
        for (char c : username)
        {
            hash1 = (hash1 ^ static_cast<uint8_t>(c)) * 1099511628211ULL;
            hash2 = (hash2 ^ static_cast<uint8_t>(c)) * 14695981039346656037ULL;
        }
        char uuidBuf[37];
        std::snprintf(uuidBuf, sizeof(uuidBuf), "%08x-%04x-4%03x-%04x-%012lx",
                      static_cast<uint32_t>(hash1 >> 32),
                      static_cast<uint16_t>(hash1 & 0xFFFF),
                      static_cast<uint16_t>((hash1 >> 16) & 0x0FFF),
                      static_cast<uint16_t>(((hash2 >> 48) & 0x3FFF) | 0x8000),
                      static_cast<uint64_t>(hash2 & 0xFFFFFFFFFFFFULL));
        return std::string(uuidBuf);
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
            currentSession.yaw = playerData.lastYaw;
            currentSession.pitch = playerData.lastPitch;
            deserializeInventory(playerData.inventoryData, currentSession.inventory);
            Core::Logger::info("Restored player state for UUID [" + playerUuid + "] (" + username + ") at (" +
                              std::to_string(currentSession.x) + ", " + std::to_string(currentSession.y) + ", " + std::to_string(currentSession.z) + ") with " +
                              std::to_string(currentSession.inventory.size()) + " saved items.");
        }
        else
        {
            currentSession.x = 0.5;
            currentSession.y = 7.0;
            currentSession.z = 0.5;
            currentSession.yaw = 0.0f;
            currentSession.pitch = 0.0f;
            Core::Logger::info("Created new player session for UUID [" + playerUuid + "] (" + username + ")");
        }

        // -------------------------------------------------------------
        // A. Send required initialization packets (Minecraft 1.8)
        // -------------------------------------------------------------

        // 1. Join Game (Packet 0x01)
        std::vector<uint8_t> joinGame;
        Network::Packet::writeInt(joinGame, clientFd);  // Entity ID
        joinGame.push_back(1);                         // Gamemode (1 = Creative)
        joinGame.push_back(0);                         // Dimension (0 = Overworld)
        joinGame.push_back(1);                         // Difficulty (1 = Easy)
        joinGame.push_back(100);                       // Max Players
        Network::Packet::writeString(joinGame, "flat"); // Level Type
        joinGame.push_back(0);                         // Reduced Debug Info
        sendPacket(clientFd, 0x01, joinGame);

        // 2. Spawn Position (Packet 0x05)
        std::vector<uint8_t> spawnPos;
        uint64_t location = ((0ULL & 0x3FFFFFF) << 38) | ((6ULL & 0xFFF) << 26) | (0ULL & 0x3FFFFFF);
        for (int i = 7; i >= 0; --i)
            spawnPos.push_back((location >> (i * 8)) & 0xFF);
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

        // 5. Synchronize inventory to client screen via Window Items (Packet 0x30)
        std::vector<uint8_t> windowItems;
        windowItems.push_back(0); // Window ID 0 = Inventory
        Network::Packet::writeShort(windowItems, 45); // 45 slots

        for (int16_t i = 0; i < 45; ++i)
        {
            auto it = currentSession.inventory.find(i);
            if (it != currentSession.inventory.end() && it->second.id != -1 && it->second.count > 0)
            {
                auto translated = Network::Protocol::ProtocolTranslator::translateItemToClient(47, it->second.id, it->second.count, it->second.damage);
                Network::Packet::writeShort(windowItems, translated.id);
                windowItems.push_back(translated.count);
                Network::Packet::writeShort(windowItems, translated.damage);
                windowItems.push_back(0); // NBT tag = 0
            }
            else
            {
                Network::Packet::writeShort(windowItems, -1);
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
        Network::Packet::writeVarInt(addTab, 1); // Gamemode
        Network::Packet::writeVarInt(addTab, 0); // Ping
        addTab.push_back(0);                    // Display name

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

    void PlayerManager::broadcastEquipment(int entityId, int16_t itemSlot, int16_t itemId, const BroadcastSender &broadcastPacket)
    {
        std::vector<uint8_t> packet;
        Network::Packet::writeVarInt(packet, entityId);
        Network::Packet::writeShort(packet, itemSlot);

        auto translatedItem = Network::Protocol::ProtocolTranslator::translateItemToClient(47, itemId);

        Network::Packet::writeShort(packet, translatedItem.id);
        if (translatedItem.id != -1)
        {
            packet.push_back(translatedItem.count);
            Network::Packet::writeShort(packet, translatedItem.damage);
            packet.push_back(0);
        }

        broadcastPacket(0x04, packet, entityId);
    }

    void PlayerManager::savePlayerState(const PlayerSession &session, Storage::Database &db)
    {
        if (session.username.empty() || session.state != ClientState::PLAY)
            return;

        Storage::PlayerData data;
        data.uuid = getPlayerUuid(session.username);
        data.username = session.username;
        data.spawnX = 0.5;
        data.spawnY = 7.0;
        data.spawnZ = 0.5;
        data.spawnYaw = 0.0f;
        data.spawnPitch = 0.0f;

        data.lastX = session.x;
        data.lastY = session.y;
        data.lastZ = session.z;
        data.lastYaw = session.yaw;
        data.lastPitch = session.pitch;

        data.inventoryData = serializeInventory(session.inventory);

        if (db.savePlayerData(data))
        {
            Core::Logger::info("Successfully saved player state to SQLite for UUID [" + data.uuid + "] (" + session.username + ") with " +
                              std::to_string(data.inventoryData.size()) + " bytes of inventory payload.");
        }
    }
}
