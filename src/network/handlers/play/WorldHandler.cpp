#include "network/handlers/play/WorldHandler.hpp"
#include "versions/v1_8/food.hpp"
#include <cmath>
#include <unordered_map>
#include <utility>

namespace WorldHandler
{

    void handleDigging(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                       LightweightMC::Storage::WorldStorage &worldStorage, LightweightMC::Network::NetworkManager &net)
    {
        int32_t status = LightweightMC::Network::Packet::readVarIntBounded(data, offset, packetEnd, 1);
        uint64_t val = LightweightMC::Network::Packet::readUInt64Bounded(data, offset, packetEnd);

        int32_t x = static_cast<int32_t>(val >> 38);
        int32_t y = (session.clientProtocol >= 47) ? LightweightMC::Network::Packet::decodeModernY(val) : static_cast<int32_t>((val >> 26) & 0xFFF);
        int32_t z = static_cast<int32_t>(val << 38 >> 38);

        if (x >= (1 << 25))
            x -= (1 << 26);
        if (session.clientProtocol < 47 && y >= (1 << 11))
            y -= (1 << 12);
        if (z >= (1 << 25))
            z -= (1 << 26);

        uint8_t face = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        (void)face;

        if (status == 0 || status == 2)
        {
            int chunkX = LightweightMC::Network::Packet::floorDiv(x, 16.0);
            int chunkZ = LightweightMC::Network::Packet::floorDiv(z, 16.0);
            int relX = LightweightMC::Network::Packet::floorMod(x, 16.0);
            int relZ = LightweightMC::Network::Packet::floorMod(z, 16.0);

            worldStorage.saveBlockChange(chunkX, chunkZ, relX, y, relZ, 0);

            std::vector<uint8_t> blockChange;
            uint64_t posEnc = ((static_cast<uint64_t>(x) & 0x3FFFFFF) << 38) |
                              ((static_cast<uint64_t>(y) & 0xFFF) << 26) |
                              (static_cast<uint64_t>(z) & 0x3FFFFFF);

            for (int i = 7; i >= 0; --i)
                blockChange.push_back((posEnc >> (i * 8)) & 0xFF);
            LightweightMC::Network::Packet::writeVarInt(blockChange, 0); // Air

            net.broadcastPacket(0x23, blockChange, clientFd);
        }
    }

    void handleBlockPlacement(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                              LightweightMC::Storage::WorldStorage &worldStorage, LightweightMC::Storage::Database &database, LightweightMC::Network::NetworkManager &net)
    {
        LightweightMC::Core::Logger::info("Block Placement Packet from " + session.username +
                                          " | Pos: (" + std::to_string(session.x) + ", " + std::to_string(session.y) + ", " + std::to_string(session.z) +
                                          ") | Yaw: " + std::to_string(session.yaw) + " | Pitch: " + std::to_string(session.pitch));

        uint64_t val = LightweightMC::Network::Packet::readUInt64Bounded(data, offset, packetEnd);
        int32_t targetX = static_cast<int32_t>(val >> 38);
        int32_t targetY = (session.clientProtocol >= 47) ? LightweightMC::Network::Packet::decodeModernY(val) : static_cast<int32_t>((val >> 26) & 0xFFF);
        int32_t targetZ = static_cast<int32_t>(val << 38 >> 38);

        if (targetX >= (1 << 25))
            targetX -= (1 << 26);
        if (session.clientProtocol < 47 && targetY >= (1 << 11))
            targetY -= (1 << 12);
        if (targetZ >= (1 << 25))
            targetZ -= (1 << 26);

        uint8_t face = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        int16_t itemSlot = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);

        LightweightMC::Core::Logger::info("Touched Slot " + std::to_string(itemSlot) +
                                          " at Target Block (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ", " + std::to_string(targetZ) +
                                          ") Face: " + std::to_string(face));

        uint8_t itemCount = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        (void)itemCount;
        int16_t itemDamage = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);

        uint8_t cursorX = 0, cursorY = 0, cursorZ = 0;
        if (offset + 3 <= packetEnd)
        {
            cursorX = data[offset++];
            cursorY = data[offset++];
            cursorZ = data[offset++];
        }
        (void)cursorX;
        (void)cursorZ;

        if (face == 255)
        {
            // Vanilla clients right-click food while no block is targeted: the
            // client sends Block Placement with a dummy (-1,-1,-1) position and
            // face=255. Food consumption belongs here, not on the Animation
            // (arm-swing) packet — that path never carried the held-stack check
            // in maintainer repro, so bars stayed stuck empty. The full payload
            // was already consumed above, so just apply hunger and return:
            // face==255 must NEVER touch world modification logic below.
            static const std::unordered_map<int32_t, std::pair<int32_t, float>> FOOD_TABLE = {
                // ID -> {foodPoints, saturationRatio} — Vanilla 1.8 Protocol 47 Nutrition

                // Basic Produce & Staples
                {260, {4, 0.3f}}, // Apple (ID 260) -> 4 food (2 shanks), 2.4 saturation
                {282, {6, 0.6f}}, // Mushroom Stew (ID 282) -> 6 food, 7.2 saturation
                {297, {5, 0.6f}}, // Bread (ID 297) -> 5 food, 6.0 saturation
                {357, {2, 0.1f}}, // Cookie (ID 357) -> 2 food, 0.4 saturation
                {360, {2, 0.3f}}, // Melon Slice (ID 360) -> 2 food, 1.2 saturation
                {391, {4, 0.6f}}, // Carrot (ID 391) -> 4 food, 4.8 saturation
                {392, {1, 0.3f}}, // Potato (ID 392) -> 1 food, 0.6 saturation
                {393, {6, 0.6f}}, // Baked Potato (ID 393) -> 6 food, 7.2 saturation
                {394, {2, 0.3f}}, // Poisonous Potato (ID 394) -> 2 food, 1.2 saturation
                {400, {8, 0.3f}}, // Pumpkin Pie (ID 400) -> 8 food, 4.8 saturation

                // Meats (Raw & Cooked)
                {319, {3, 0.3f}},  // Raw Porkchop (ID 319) -> 3 food, 1.8 saturation
                {320, {8, 0.8f}},  // Cooked Porkchop (ID 320) -> 8 food, 12.8 saturation
                {363, {3, 0.3f}},  // Raw Beef (ID 363) -> 3 food, 1.8 saturation
                {364, {8, 0.8f}},  // Steak (Cooked Beef - ID 364) -> 8 food, 12.8 saturation
                {365, {2, 0.3f}},  // Raw Chicken (ID 365) -> 2 food, 1.2 saturation
                {366, {6, 0.6f}},  // Cooked Chicken (ID 366) -> 6 food, 7.2 saturation
                {367, {4, 0.1f}},  // Rotten Flesh (ID 367) -> 4 food, 0.8 saturation
                {411, {3, 0.3f}},  // Raw Rabbit (ID 411) -> 3 food, 1.8 saturation
                {412, {5, 0.6f}},  // Cooked Rabbit (ID 412) -> 5 food, 6.0 saturation
                {413, {10, 0.6f}}, // Rabbit Stew (ID 413) -> 10 food, 12.0 saturation
                {423, {2, 0.3f}},  // Raw Mutton (ID 423) -> 2 food, 1.2 saturation
                {424, {6, 0.8f}},  // Cooked Mutton (ID 424) -> 6 food, 9.6 saturation

                // Fish (ID 349 Raw Fish, ID 350 Cooked Fish - metadata differentiates types if needed)
                {349, {2, 0.1f}}, // Raw Fish (Cod - ID 349) -> 2 food, 0.4 saturation
                {350, {5, 0.6f}}, // Cooked Fish (Cod - ID 350) -> 5 food, 6.0 saturation

                // Golden Foods
                {322, {4, 1.2f}}, // Golden Apple (ID 322) -> 4 food, 9.6 saturation
                {396, {6, 1.2f}}  // Golden Carrot (ID 396) -> 6 food, 14.4 saturation
            };

            if (session.selectedSlot >= 0 && session.selectedSlot < 9)
            {
                const int16_t slotKey = static_cast<int16_t>(36 + session.selectedSlot);
                auto heldIt = session.inventory.find(slotKey);
                if (heldIt != session.inventory.end() && heldIt->second.id > 0 && heldIt->second.count > 0)
                {
                    auto n = LightweightMC::Versions::V1_8::Food::forItem(heldIt->second.id);
                    if (n.foodPoints > 0)
                    {
                        const bool eaten = session.consumeSelectedFood(n.foodPoints, n.saturation);
                        if (eaten)
                        {
                            std::vector<uint8_t> hpPayload;
                            LightweightMC::Network::Packet::writeFloat(hpPayload, session.health);
                            LightweightMC::Network::Packet::writeVarInt(hpPayload, static_cast<int32_t>(session.foodLevel));
                            LightweightMC::Network::Packet::writeFloat(hpPayload, session.foodSaturationLevel);
                            net.sendPacket(clientFd, 0x06, hpPayload);
                        }
                    }
                }
            }

            return;
        }

        // 1. VÉRIFICATION DU BLOC CIBLÉ
        int targetChunkX = LightweightMC::Network::Packet::floorDiv(targetX, 16);
        int targetChunkZ = LightweightMC::Network::Packet::floorDiv(targetZ, 16);
        int targetRelX = LightweightMC::Network::Packet::floorMod(targetX, 16);
        int targetRelZ = LightweightMC::Network::Packet::floorMod(targetZ, 16);

        uint16_t existingBlockData = worldStorage.getBlockAt(targetChunkX, targetChunkZ, targetRelX, targetY, targetRelZ);
        uint16_t existingBlockId = existingBlockData >> 4;

        bool isInteractive = (existingBlockId == 54 || existingBlockId == 61 || existingBlockId == 62 ||
                              existingBlockId == 58 || existingBlockId == 23 || existingBlockId == 145 ||
                              existingBlockId == 146 || existingBlockId == 154 || existingBlockId == 158 ||
                              existingBlockId == 130 || existingBlockId == 116 || existingBlockId == 117);

        LightweightMC::Core::Logger::info("Existing Target Block ID: " + std::to_string(existingBlockId) + " | Interactive: " + (isInteractive ? "true" : "false"));

        if (isInteractive)
        {
            uint8_t pureBlockId = static_cast<uint8_t>(existingBlockId);

            struct ContainerDef
            {
                const char *type;
                const char *title;
                uint8_t slots;
            };

            ContainerDef container{"", "", 0};
            bool isContainer = false;

            if (pureBlockId == 54 || pureBlockId == 146 || pureBlockId == 130)
            {
                // Minecraft models a "double chest" as two adjacent chest
                // blocks: the primary half is ID 54 (or 130 for ender), and
                // the secondary half is ID 146. The client UI then shows a
                // single window with 54 slots instead of the 27 of a solo
                // chest. Detect the pair by probing the 4 horizontal
                // neighbours; a 146 next to our 54 (or 54 next to our 146)
                // means we should advertise the doubled slot count.
                uint8_t slotsForChest = 27;
                const bool isPairSecondary = (pureBlockId == 146);
                bool pairDetected = isPairSecondary;
                if (!pairDetected)
                {
                    const int nChunkX[4] = {targetChunkX + (targetRelX + 1 >= 16 ? 1 : 0),
                                            targetChunkX + (targetRelX - 1 < 0 ? -1 : 0),
                                            targetChunkX, targetChunkX};
                    const int nChunkZ[4] = {targetChunkZ, targetChunkZ,
                                            targetChunkZ + (targetRelZ + 1 >= 16 ? 1 : 0),
                                            targetChunkZ + (targetRelZ - 1 < 0 ? -1 : 0)};
                    const int nRelX[4] = {(targetRelX + 1) & 15, (targetRelX + 15) & 15, targetRelX, targetRelX};
                    const int nRelZ[4] = {targetRelZ, targetRelZ, (targetRelZ + 1) & 15, (targetRelZ + 15) & 15};
                    for (int i = 0; i < 4 && !pairDetected; ++i)
                    {
                        uint16_t nb = worldStorage.getBlockAt(nChunkX[i], nChunkZ[i], nRelX[i], targetY, nRelZ[i]);
                        uint8_t nbId = static_cast<uint8_t>(nb >> 4);
                        if (nbId == 54 || nbId == 146 || nbId == 130)
                            pairDetected = true;
                    }
                }
                if (pairDetected)
                    slotsForChest = 54;

                container = {pureBlockId == 130 ? "minecraft:ender_chest" : "minecraft : chest",
                             pureBlockId == 130 ? "Ender Chest" : "Chest", slotsForChest};
                isContainer = true;
            }
            else if (pureBlockId == 61 || pureBlockId == 62)
            {
                container = {"minecraft:furnace", "Furnace", 3};
                isContainer = true;
            }
            else if (pureBlockId == 58)
            {
                container = {"minecraft:crafting_table", "Crafting Table", 0};
                isContainer = true;
            }
            else if (pureBlockId == 23)
            {
                container = {"minecraft:dispenser", "Dispenser", 9};
                isContainer = true;
            }
            else if (pureBlockId == 158)
            {
                container = {"minecraft:dropper", "Dropper", 9};
                isContainer = true;
            }
            else if (pureBlockId == 154)
            {
                container = {"minecraft:hopper", "Hopper", 5};
                isContainer = true;
            }
            else if (pureBlockId == 117)
            {
                container = {"minecraft:brewing_stand", "Brewing Stand", 4};
                isContainer = true;
            }
            else if (pureBlockId == 116)
            {
                container = {"minecraft:enchanting_table", "Enchantment Table", 2};
                isContainer = true;
            }
            else if (pureBlockId == 145)
            {
                container = {"minecraft:anvil", "Repair & Name", 3};
                isContainer = true;
            }

            if (isContainer)
            {
                session.openWindowId = (session.openWindowId % 100) + 1;
                uint8_t winId = session.openWindowId;

                std::string titleJson = "{\"text\":\"" + std::string(container.title) + "\"}";
                std::vector<uint8_t> openWin;
                openWin.push_back(winId);

                std::string invType = container.type;
                LightweightMC::Network::Packet::writeVarInt(openWin, static_cast<int32_t>(invType.size()));
                openWin.insert(openWin.end(), invType.begin(), invType.end());

                LightweightMC::Network::Packet::writeVarInt(openWin, static_cast<int32_t>(titleJson.size()));
                openWin.insert(openWin.end(), titleJson.begin(), titleJson.end());

                if (invType == "minecraft:enchanting_table" || invType == "minecraft:anvil")
                    openWin.push_back(0);
                else
                    openWin.push_back(container.slots);

                net.sendPacket(clientFd, 0x2D, openWin);
                LightweightMC::Core::Logger::info("[Container] Opened " + std::string(container.title) +
                                                  " for player " + session.username +
                                                  " (windowId=" + std::to_string(winId) + ")");

                // ----------------------------------------------------
                // Persistent container contents: look up the SQLite
                // row for this block location (or empty for first
                // interaction) and register it on the session so
                // subsequent 0x0E Click Window packets can be routed
                // to the correct window. For double chests we
                // canonicalize to the left-most/down-most block so
                // both halves map to the same SQLite row.
                // ----------------------------------------------------
                int keyChunkX = targetChunkX, keyChunkZ = targetChunkZ;
                int keyRelX = targetRelX, keyRelZ = targetRelZ;
                if (container.slots == 54)
                {
                    auto isChestId = [](uint16_t raw)
                    {
                        uint16_t id = raw >> 4;
                        return id == 54 || id == 130 || id == 146;
                    };
                    // Left neighbour (relX-1)
                    int lc = targetChunkX + (targetRelX - 1 < 0 ? -1 : 0);
                    int lr = (targetRelX + 15) & 15;
                    if (isChestId(worldStorage.getBlockAt(lc, targetChunkZ, lr, targetY, targetRelZ)))
                    {
                        keyChunkX = lc;
                        keyRelX = lr;
                    }
                    else
                    {
                        // Down neighbour (relZ-1)
                        int dz = targetChunkZ + (targetRelZ - 1 < 0 ? -1 : 0);
                        int dr = (targetRelZ + 15) & 15;
                        if (isChestId(worldStorage.getBlockAt(targetChunkX, dz, targetRelX, targetY, dr)))
                        {
                            keyChunkZ = dz;
                            keyRelZ = dr;
                        }
                    }
                }

                uint16_t persistedType = 0;
                std::vector<uint8_t> persistedBlob;
                if (database.loadContainer(keyChunkX, keyChunkZ, keyRelX, targetY, keyRelZ,
                                           persistedType, persistedBlob))
                {
                    LightweightMC::Core::Logger::info("[Container] Loaded saved contents for row (" + std::to_string(keyChunkX) + ", " + std::to_string(keyChunkZ) + ", " + std::to_string(keyRelX) + ", " + std::to_string(targetY) + ", " + std::to_string(keyRelZ) + ")");
                }

                LightweightMC::Player::WindowInfo &win = session.openWindows[winId];
                win.chunkX = static_cast<int16_t>(keyChunkX);
                win.chunkZ = static_cast<int16_t>(keyChunkZ);
                win.relX = static_cast<int16_t>(keyRelX);
                win.y = static_cast<int16_t>(targetY);
                win.relZ = static_cast<int16_t>(keyRelZ);
                win.blockType = existingBlockData;
                LightweightMC::Player::deserializeContainerItems(persistedBlob,
                                                                 static_cast<int>(container.slots),
                                                                 win.items);
                win.items.resize(container.slots, LightweightMC::Player::ItemStack{-1, 0, 0});
                win.dirty = false;

                // Advertise the current contents so the client sees the
                // saved inventory on reconnect (and on first open).
                // Wire format matches the login-time 0x30 Window Items packet
                // used for the player-inventory window (see PlayerManager.cpp):
                //   winId:byte | totalSlots:short
                //   then totalSlots entries where an empty slot is short(-1)
                //   and a non-empty slot is short(id) + byte(count) +
                //   short(damage) + optional NBT terminator byte(0) for
                //   protocol >= 47.
                std::vector<uint8_t> winItems;
                winItems.push_back(winId);
                LightweightMC::Network::Packet::writeShort(
                    winItems, static_cast<int16_t>(win.items.size()));
                for (int i = 0; i < static_cast<int>(win.items.size()); ++i)
                {
                    const LightweightMC::Player::ItemStack &slotIt = win.items[i];
                    if (slotIt.id == -1 || slotIt.count == 0)
                    {
                        LightweightMC::Network::Packet::writeShort(winItems, -1);
                        continue;
                    }
                    auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::
                        translateItemToClient(session.clientProtocol,
                                              slotIt.id, slotIt.count, slotIt.damage);
                    if (tr.id == -1)
                    {
                        LightweightMC::Network::Packet::writeShort(winItems, -1);
                        continue;
                    }
                    LightweightMC::Network::Packet::writeShort(winItems, tr.id);
                    winItems.push_back(tr.count);
                    LightweightMC::Network::Packet::writeShort(winItems, tr.damage);
                    if (session.clientProtocol >= 47)
                        winItems.push_back(0); // NBT end-of-compound tag
                }
                net.sendPacket(clientFd, 0x30, winItems);
            }
            return;
        }

        // 2. PLACEMENT D'UN NOUVEAU BLOC
        int32_t placeX = targetX;
        int32_t placeY = targetY;
        int32_t placeZ = targetZ;

        if (face == 0)
            placeY--;
        else if (face == 1)
            placeY++;
        else if (face == 2)
            placeZ--;
        else if (face == 3)
            placeZ++;
        else if (face == 4)
            placeX--;
        else if (face == 5)
            placeX++;

        LightweightMC::Core::Logger::info("Placing Block " + std::to_string(itemSlot) +
                                          " at Calculated Pos (" + std::to_string(placeX) + ", " + std::to_string(placeY) + ", " + std::to_string(placeZ) + ")");

        uint8_t orientMeta = 0;
        uint16_t blockId = static_cast<uint16_t>(itemSlot);

        float yaw = session.yaw;
        if (!std::isfinite(yaw))
            yaw = 0.0f;
        else
        {
            yaw = std::fmod(yaw, 360.0f);
            if (yaw < 0.0f)
                yaw += 360.0f;
        }

        uint8_t yawDir4 = 0;
        if (yaw < 45.0f || yaw >= 315.0f)
            yawDir4 = 0;
        else if (yaw < 135.0f)
            yawDir4 = 1;
        else if (yaw < 225.0f)
            yawDir4 = 2;
        else
            yawDir4 = 3;

        uint8_t facing5 = 3;
        switch (yawDir4)
        {
        case 0:
            facing5 = 3;
            break;
        case 1:
            facing5 = 4;
            break;
        case 2:
            facing5 = 2;
            break;
        case 3:
            facing5 = 5;
            break;
        }
        if (face == 0)
            facing5 = 0;
        if (face == 1)
            facing5 = 1;

        bool isStairs = (blockId == 53 || blockId == 67 || blockId == 108 || blockId == 109 || blockId == 114 ||
                         blockId == 128 || blockId == 134 || blockId == 135 || blockId == 136 || blockId == 156 ||
                         blockId == 163 || blockId == 164);
        bool isSlab = (blockId == 44 || blockId == 126);
        bool isLever = (blockId == 69);
        bool isButton = (blockId == 77 || blockId == 143);
        bool isLog = (blockId == 17 || blockId == 162);

        if (isStairs)
        {
            switch (yawDir4)
            {
            case 0:
                orientMeta = 2;
                break;
            case 1:
                orientMeta = 1;
                break;
            case 2:
                orientMeta = 3;
                break;
            case 3:
                orientMeta = 0;
                break;
            }
            bool upsideDown = (face == 0) || (face == 1 && cursorY > 8);
            if (upsideDown)
                orientMeta |= 0x04;
        }
        else if (isSlab)
        {
            uint8_t slabType = static_cast<uint8_t>(itemDamage & 0x07);
            orientMeta = slabType;
            bool topHalf = (face == 0) || (face == 1 && cursorY > 8);
            if (topHalf)
                orientMeta |= 0x08;
        }
        else if (isLever)
        {
            if (face == 0)
                orientMeta = (yawDir4 == 0 || yawDir4 == 2) ? 7 : 0;
            else if (face == 1)
                orientMeta = (yawDir4 == 0 || yawDir4 == 2) ? 6 : 5;
            else
                orientMeta = facing5 - 1;
        }
        else if (isButton)
        {
            switch (face)
            {
            case 0:
                orientMeta = 5;
                break;
            case 1:
                orientMeta = 0;
                break;
            case 2:
                orientMeta = 4;
                break;
            case 3:
                orientMeta = 3;
                break;
            case 4:
                orientMeta = 2;
                break;
            case 5:
                orientMeta = 1;
                break;
            }
        }
        else if (isLog)
        {
            uint8_t logType = static_cast<uint8_t>(itemDamage & 0x03);
            if (face == 0 || face == 1)
                orientMeta = logType | 0x00;
            else if (face == 4 || face == 5)
                orientMeta = logType | 0x04;
            else
                orientMeta = logType | 0x08;
        }
        else
        {
            bool isDirectional = (blockId == 23 || blockId == 26 || blockId == 29 || blockId == 33 || blockId == 54 ||
                                  blockId == 58 || blockId == 61 || blockId == 62 || blockId == 130 || blockId == 146 ||
                                  blockId == 154 || blockId == 158);
            if (isDirectional && face > 1)
                orientMeta = facing5;
            else
                orientMeta = static_cast<uint8_t>(itemDamage & 0x0F);
        }

        LightweightMC::Core::Logger::info("Computed Meta Data: " + std::to_string(orientMeta));

        int placeChunkX = LightweightMC::Network::Packet::floorDiv(placeX, 16);
        int placeChunkZ = LightweightMC::Network::Packet::floorDiv(placeZ, 16);
        int placeRelX = LightweightMC::Network::Packet::floorMod(placeX, 16);
        int placeRelZ = LightweightMC::Network::Packet::floorMod(placeZ, 16);

        auto translatedBlock = LightweightMC::Network::Protocol::ProtocolTranslator::translateBlockFromClient(
            session.clientProtocol, static_cast<uint16_t>(itemSlot), orientMeta);
        uint16_t blockData = translatedBlock.toCombinedData();

        worldStorage.saveBlockChange(placeChunkX, placeChunkZ, placeRelX, placeY, placeRelZ, blockData);

        std::vector<uint8_t> blockChange;
        uint64_t posEnc = ((static_cast<uint64_t>(placeX) & 0x3FFFFFF) << 38) |
                          ((static_cast<uint64_t>(placeY) & 0xFFF) << 26) |
                          (static_cast<uint64_t>(placeZ) & 0x3FFFFFF);

        for (int i = 7; i >= 0; --i)
            blockChange.push_back((posEnc >> (i * 8)) & 0xFF);
        LightweightMC::Network::Packet::writeVarInt(blockChange, blockData);

        net.broadcastPacket(0x23, blockChange);
    }

    // ---------------------------------------------------------------------
    // 0x12 UpdateSign — C2S packet emitted when the client edits a sign
    // in world. Wire format per protocol_types.hpp
    // UpdateSign_6cfea0d3::deserialize:
    //   long location      (8 bytes, same encoding as Digging / Place)
    //   string text1       (up to 15 chars)
    //   string text2
    //   string text3
    //   string text4
    // The server does not yet model sign text; we consume and log the
    // coordinates so future sign-persistence code can plug in without
    // touching the wire path.
    // ---------------------------------------------------------------------
    void handleUpdateSign(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, LightweightMC::Storage::WorldStorage &worldStorage)
    {
        (void)clientFd;
        (void)worldStorage;
        const uint64_t val = LightweightMC::Network::Packet::readUInt64Bounded(data, offset, packetEnd);
        int32_t signX = static_cast<int32_t>(val >> 38);
        int32_t signY = LightweightMC::Network::Packet::decodeModernY(val);
        int32_t signZ = static_cast<int32_t>(val << 38 >> 38);
        if (signX >= (1 << 25))
            signX -= (1 << 26);
        if (signZ >= (1 << 25))
            signZ -= (1 << 26);

        const std::string s1 = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        const std::string s2 = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        const std::string s3 = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        const std::string s4 = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);

        LightweightMC::Core::Logger::info("[Sign] " + session.username +
                                          " updated sign at (" +
                                          std::to_string(signX) + ", " +
                                          std::to_string(signY) + ", " +
                                          std::to_string(signZ) + ") lines: " +
                                          s1 + " / " + s2 + " / " + s3 + " / " + s4 +
                                          " (sign-text model pending)");
    }
} // namespace WorldHandler