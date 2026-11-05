#include "network/handlers/play/WorldHandler.hpp"
#include "versions/v1_8/food.hpp"
#include "versions/v1_8/xp.hpp"
#include "network/protocol/PacketCodec47.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <random>
#include "network/handlers/play/InventoryHandler.hpp"
#include "world/MC18ItemIds.hpp"
#include "world/EntityManager.hpp"
#include "world/GameRules.hpp"
#include "network/Packet.hpp"
#include "managers/PlayerManager.hpp"

namespace WorldHandler
{
    static inline std::string hexId(int32_t v)
    {
        std::ostringstream o; o << "0x" << std::hex << v; return o.str();
    }

    static inline std::string hexPreview(const uint8_t *b, size_t n)
    {
        std::ostringstream o; const size_t k = n<48?n:48;
        for (size_t i=0;i<k;++i){ o<<std::hex<<std::setw(2)<<std::setfill('0')<<(unsigned)b[i]; if(i+1<k)o<<' '; }
        return o.str();
    }

    // --- Block-breaking mining model (maps the vanilla 1.8 tier concept onto
    // LWMC's own tool/item space from include/world/MC18ItemIds.hpp). The user
    // guide lists vanilla numeric ids (270/274...) which do not exist here; we
    // key on LWMC tools instead: MATTOCK(85) is the pickaxe-class tool, HOE(84)
    // digs soft ground, HATCHET(80)/ADZE(83) fell wood, CHISEL(86) is a finer
    // stone tool. Tier follows the LWMC durability ladder (sturdier tool = can
    // mine harder blocks). Hand = tier 0. Unknown tools = tier 0. ---
    static inline int toolTier(int itemId)
    {
        switch (itemId)
        {
            case 85: return 2; // MATTOCK (pickaxe-class, mid tier)
            case 86: return 2; // CHISEL
            case 87: return 3; // HAMMER (sturdiest -> highest tier)
            case 84: return 1; // HOE (soft ground)
            case 80: return 1; // HATCHET
            case 83: return 1; // ADZE
            default: return 0; // hand / non-tool
        }
    }

    // Required mining tier per block id (LWMC block ids from
    // include/world/MC18BlockIds.hpp). LWMC tool tiers come from toolTier()
    // above: hand/none = 0, soft-dig tools (HOE/HATCHET/ADZE) = 1, pickaxe-class
    // (MATTOCK/CHISEL) = 2, sturdiest (HAMMER) = 3. A block whose requiredTier is
    // above the held tool's tier drops nothing (vanilla "wrong tool" rule). Unlisted
    // blocks default to 0 (hand-diggable). Covers the common 1.8 diggable set.
    static inline int blockRequiredTier(int blockId)
    {
        switch (blockId)
        {
            case 1:  return 1;   // STONE (Granite/Diorite/Andesite via meta)
            case 4:  return 1;   // COBBLESTONE
            case 5:  return 1;   // PLANKS / WOOD_PLANK
            case 7:  return 9;   // BEDROCK -- effectively unbreakable (never < heldTier realistically)
            case 13: return 1;   // GRAVEL
            case 14: return 2;   // GOLD_ORE (iron-grade+)
            case 15: return 2;   // IRON_ORE (stone+ pickaxe-class)
            case 16: return 1;   // COAL_ORE
            case 21: return 2;   // LAPIS_ORE
            case 22: return 2;   // LAPIS_BLOCK
            case 24: return 1;   // SANDSTONE
            case 41: return 2;   // GOLD_BLOCK
            case 42: return 2;   // IRON_BLOCK
            case 45: return 1;   // BRICK_BLOCK
            case 47: return 1;   // BOOKSHELF (axe-class, but tier-gated to a tool)
            case 48: return 1;   // MOSSY_COBBLESTONE
            case 49: return 3;   // OBSIDIAN (diamond/highest tier)
            case 56: return 3;   // DIAMOND_ORE
            case 57: return 3;   // DIAMOND_BLOCK
            case 73: return 2;   // REDSTONE_ORE
            case 129:return 2;   // EMERALD_ORE
            case 153:return 1;   // QUARTZ_ORE (Nether Quartz)
            case 165:return 3;   // SLIME_BLOCK
            case 168:return 1;   // PRISMARINE
            case 172:return 1;   // HARDENED_CLAY / Terracotta
            case 173:return 1;   // COAL_BLOCK
            case 179:return 1;   // RED_SANDSTONE
            default: return 0;   // dirt/sand/grass/leaves/log/etc. hand-diggable
        }
    }

    // Drop item per block id. IMPORTANT: LWMC keeps two separate id spaces --
    // block ids (MC18BlockIds.hpp) and item ids (MC18ItemIds.hpp). A drop value
    // is consumed as an ITEM (mergeItemIntoInventory / spawnItemDrop), so we only
    // cross-map when a genuine processed LWMC item exists; otherwise the block
    // drops itself (drop == blockId), which is how the pre-existing dirt/grass/
    // log/leaves behaviour already worked. Extend this table as real LWMC items
    // are added rather than inventing vanilla ids.
    static inline int blockDropItem(int blockId)
    {
        switch (blockId)
        {
            case 1:   return 4;     // STONE -> COBBLESTONE
            case 2:   return 3;     // GRASS_BLOCK -> DIRT
            default:  return blockId;  // otherwise drop the block itself
        }
    }

    void handleDigging(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                       LightweightMC::Storage::WorldStorage &worldStorage, LightweightMC::Storage::Database &database, LightweightMC::Network::NetworkManager &net)
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

        // [WIRE C2S] Player Digging (0x07) -- always log the decoded request so a
        // real client breaking a block produces a WARN even before any world
        // mutation. status meanings (protocol 47): 0 = Start, 1 = Cancelled,
        // 2 = Finished. If a client sends anything other than 0/2 the break
        // branch below is skipped -- this trace makes that visible.
        LightweightMC::Core::Logger::warn(std::string("[WIRE Digging C2S] ")
            + "player="+session.username+" status="+std::to_string(status)
            +" pos=("+std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+")"
            +" face="+std::to_string((int)face));

        // Game-mode aware breaking gate (session.gamemode: 0=survival,1=creative,2=adventure,3=spectator).
        // Vanilla-1.8 protocol-47 Player Digging sends status 0=Start, 1=Cancelled, 2=Finished.
        //  - Survival/adventure: a block is only destroyed on status 2 (Finished). Status 0 is just
        //    the click/Start and must NOT clear the cell -- doing so made later status packets read
        //    Air and dropped nothing (the bug in this report).
        //  - Creative: instant break -- destroy on status 0 (Start); also honour a lone status 2.
        //  - Spectator: cannot break blocks at all -- never mutate, never drop, never broadcast.
        const bool spectator = (session.gamemode == 3);
        const bool creativeInstant = (session.gamemode == 1);
        const bool canBreakNow = !spectator &&
            (status == 2 || (creativeInstant && status == 0));

        if (status == 0 || status == 1 || status == 2)
        {
            int chunkX = LightweightMC::Network::Packet::floorDiv(x, 16.0);
            int chunkZ = LightweightMC::Network::Packet::floorDiv(z, 16.0);
            int relX = LightweightMC::Network::Packet::floorMod(x, 16.0);
            int relZ = LightweightMC::Network::Packet::floorMod(z, 16.0);

            // Read the existing block before we overwrite it so block-breaking
            // drops (Bug 4 fix) can be resolved from the real block type.
            //
            // The chunk must be generated first: getBlockAt() returns 0 ("air/
            // unknown") for a not-yet-generated chunk, so reading oldBlockId
            // before generation would see Air and drop nothing on the very first
            // dig into a fresh chunk. generateChunk() is idempotent (early-returns
            // if already generated) and saveBlockChange() does this same guard on
            // write, so calling it here only makes the read correct -- no extra
            // cost on later digs of an already-generated chunk.
            worldStorage.generateChunk(chunkX, chunkZ);

            uint16_t oldBlockData = worldStorage.getBlockAt(chunkX, chunkZ, relX, y, relZ);
            uint16_t oldBlockId = oldBlockData >> 4;

            // [WIRE] getBlockAt result at click/dig time -- logs the raw combined
            // cell value and decoded block id for EVERY Player Digging status (0=Start,
            // 1=Cancelled, 2=Finished), so you can confirm the correct block is returned
            // when the client clicks a block to break it, BEFORE any world mutation. If
            // this shows oldBlockId=<expected> at status 0 but 0 at status 2, the cell was
            // cleared mid-sequence; if it shows 0 at every status, the server's stored
            // column has no block where the client renders one (terrain mismatch).
            LightweightMC::Core::Logger::warn(std::string("[WIRE Digging getBlockAt] ")
                + "player="+session.username+" status="+std::to_string(status)
                +" pos=("+std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+")"
                +" chunk="+std::to_string(chunkX)+","+std::to_string(chunkZ)
                +" rel="+std::to_string(relX)+","+std::to_string(relZ)
                +" rawCombined="+std::to_string(oldBlockData)
                +" oldBlockId="+std::to_string(oldBlockId));

            // If a break was allowed now but the server's stored column has no block
            // at this exact cell, client and server terrain disagree here -- log it so
            // the mismatch is visible rather than silent (no drop).
            if (canBreakNow && oldBlockId == 0)
            {
                LightweightMC::Core::Logger::warn(std::string("[WIRE Digging FINISHED-BUT-AIR] ")
                    + "player="+session.username+" pos=("+std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+")"
                    +" chunk="+std::to_string(chunkX)+","+std::to_string(chunkZ)
                    +" rel="+std::to_string(relX)+","+std::to_string(relZ)
                    +" (server sees Air at this cell -- client terrain and server storage disagree; no drop)");
            }

            if (!canBreakNow)
                return;

            worldStorage.saveBlockChange(chunkX, chunkZ, relX, y, relZ, 0);

            std::vector<uint8_t> blockChange;
            uint64_t posEnc = ((static_cast<uint64_t>(x) & 0x3FFFFFF) << 38) |
                              ((static_cast<uint64_t>(y) & 0xFFF) << 26) |
                              (static_cast<uint64_t>(z) & 0x3FFFFFF);

            for (int i = 7; i >= 0; --i)
                blockChange.push_back((posEnc >> (i * 8)) & 0xFF);
            LightweightMC::Network::Packet::writeVarInt(blockChange, 0); // Air

            net.broadcastPacket(0x23, blockChange, clientFd);

            // Block-breaking drop: we only get here when a break is allowed for this
            // game mode + status (canBreakNow), so any real block in the cell is broken.
            if (oldBlockId > 0 && oldBlockId != 7 /* bedrock */)
            {
                // Resolve held tool tier from the hotbar band, then gate the drop
                // on the block's required mining tier (vanilla 1.8 rule: wrong or
                // too-weak a tool drops nothing). LWMC tools live at ids 80..88.
                int heldTier = 0;
                if (session.selectedSlot >= 0 && session.selectedSlot < 9)
                {
                    const int16_t hk = static_cast<int16_t>(36 + session.selectedSlot);
                    const auto h = session.inventory.find(hk);
                    if (h != session.inventory.end()) heldTier = toolTier(h->second.id);
                }
                const int reqTier = blockRequiredTier(static_cast<int>(oldBlockId));
                const bool canMine = heldTier >= reqTier;

                // Drop item resolved through the LWMC drop map (defaults to the
                // block itself). Empty when the tool cannot mine this block.
                const int16_t dropId = canMine
                    ? static_cast<int16_t>(blockDropItem(static_cast<int>(oldBlockId)))
                    : static_cast<int16_t>(-1);

                // [WIRE] Block Change (0x23) -- tells clients the cell became Air.
                LightweightMC::Core::Logger::warn(std::string("[WIRE BlockChange 0x23] ")
                    + "pos=("+std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+") "
                    + "old="+std::to_string(oldBlockId)+" new=0 hex="+hexPreview(blockChange.data(), blockChange.size()));

                bool blockDrops = true;
                LightweightMC::World::GameRules::getInstance().getBool("block_drops", blockDrops);
                if (!canMine)
                {
                    // Tool tier too low for this block: vanilla drops nothing. Log so
                    // the "wrong tool" case is visible rather than silent.
                    LightweightMC::Core::Logger::warn(std::string("[WIRE Digging NO-DROP] ")
                        + "player="+session.username+" blockId="+std::to_string(oldBlockId)
                        +" reqTier="+std::to_string(reqTier)+" heldTier="+std::to_string(heldTier)
                        +" (tool too weak -- drops nothing)");
                }
                else if (blockDrops && dropId >= 0)
                {
                    // Try to collect straight into the inventory first (the item
                    // is grabbed off the block just broken). Only if the inventory
                    // cannot accept it do we leave a ground entity for the pickup
                    // sweep so the drop is not silently lost.
                    const double cx = x + 0.5, cy = y + 0.5, cz = z + 0.5;
                    const int remaining = InventoryHandler::mergeItemIntoInventory(session, dropId, 1, 0);
                    if (remaining >= 1)
                    {
                        LightweightMC::World::EntityManager::getInstance().spawnItemDrop(
                            std::string(), cx, cy, cz, dropId, remaining);

                        // [WIRE] Item spawn on the ground -- decoded fields plus the
                        // encoded varint payload in hex so the drop is traceable.
                        std::vector<uint8_t> spawnPayload;
                        LightweightMC::Network::Packet::writeVarInt(spawnPayload, dropId);
                        LightweightMC::Core::Logger::warn(std::string("[WIRE SpawnItem] ")
                            + "player="+session.username+" itemId="+std::to_string(dropId)
                            +" count="+std::to_string(remaining)
                            +" pos=("+std::to_string(cx)+","+std::to_string(cy)+","+std::to_string(cz)+")"
                            +" payloadHex="+hexPreview(spawnPayload.data(), spawnPayload.size()));
                    }

                    // [WIRE] Inventory update (Window Items 0x30) -- authoritative full
                    // inventory sync so the collected item is visible on the client.
                    LightweightMC::Core::Logger::warn(std::string("[WIRE InvAdd 0x30] ")
                        + "player="+session.username+" itemId="+std::to_string(dropId)+" +1 "
                        + "accepted="+(remaining<1?"T":"F")+" (remaining="+std::to_string(remaining)+")");

                    if (remaining < 1)
                    {
                        InventoryHandler::sendPlayerInventoryWindowItems(clientFd, session, net);
                        LightweightMC::Core::Logger::info("[World] " + session.username +
                            " broke block id=" + std::to_string(oldBlockId) +
                            " collected itemId=" + std::to_string(dropId) +
                            " at (" + std::to_string(x) + "," + std::to_string(y) + "," +
                            std::to_string(z) + ")");
                    }
                    else
                    {
                        // Inventory full: the item stays on the ground (the entity we
                        // spawned above); log so it is not silently lost.
                        LightweightMC::Core::Logger::warn("[World] inventory full for " + session.username +
                            ": broke id=" + std::to_string(oldBlockId) + " drop stays on ground");
                    }
                }
                else
                {
                    LightweightMC::Core::Logger::debug("[World] block_drops=false for " + session.username +
                        " broke id=" + std::to_string(oldBlockId));
                }
            }

            // Ore XP: destroying an ore block awards random XP per the v1_8
            // xp.hpp ore table (coal 0..2, quartz/redstone/lapis 2..5,
            // diamond/emerald 3..7; iron/gold award none — their XP comes from
            // cooking the meat they drop). We only reach here when a break was
            // actually allowed for this game mode + status, so any real block is
            // a successful destruction.
            if (oldBlockId > 0)
            {
                const auto &oreTable = LightweightMC::Versions::V1_8::Xp::ores;
                for (const auto &o : oreTable)
                {
                    if (o.blockId != static_cast<int>(oldBlockId))
                        continue;
                    std::uniform_int_distribution<int> oreDist(o.minXp, o.maxXp);
                    static thread_local std::minstd_rand oreRng{std::random_device{}()};
                    const int xpAmount = oreDist(oreRng);
                    if (xpAmount > 0)
                    {
                        const int levelsGained = session.grantXp(xpAmount);
                        LightweightMC::Core::Logger::info("[XP Ore] " + session.username +
                            " mined block id=" + std::to_string(oldBlockId) +
                            " xp+" + std::to_string(xpAmount) +
                            " levels+" + std::to_string(levelsGained));

                        // XP bar refresh (S2C UpdateExperience 0x1f).
                        std::vector<uint8_t> xpPayload;
                        LightweightMC::Network::Packet::writeFloat(xpPayload, session.experienceProgress);
                        LightweightMC::Network::Packet::writeVarInt(xpPayload, static_cast<int32_t>(session.experienceLevel));
                        LightweightMC::Network::Packet::writeVarInt(xpPayload, session.totalExperience);
                        net.sendPacket(clientFd, 0x1f, xpPayload);

                        // Persist so XP survives crash/disconnect — same
                        // rationale as the tool-wear save in this handler.
                        Player::PlayerManager::getInstance().savePlayerState(session, database);
                    }
                    break;
                }
            }

            // Tool wear: consume durability whenever a real block was actually broken
            // (we only reach here when canBreakNow held). Survival wears on Finished
            // (status 2); creative-instant wears on the Start packet that broke it.
            if (session.selectedSlot >= 0 && session.selectedSlot < 9)
            {
                const int16_t hotbarKey = static_cast<int16_t>(36 + session.selectedSlot);
                const auto held = session.inventory.find(hotbarKey);
                if (held != session.inventory.end() && InventoryHandler::getMaxDurability(held->second.id) > 0)
                {
                    InventoryHandler::applyToolDamage(net, clientFd, session, hotbarKey, 1);

                    // Persist inventory changes (including tool damage)
                    // immediately so they survive a crash — Bug 2 fix. The
                    // normal disconnect path (handleDisconnect → savePlayerState)
                    // already covers graceful logoff, but crashes or plane
                    // switches between dig events would otherwise lose the
                    // durability increment.
                    Player::PlayerManager::getInstance().savePlayerState(session, database);
                }
            }
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
            // Nutrition values come from LightweightMC::Versions::V1_8::Food::forItem()
            // (curated from prismarineJS minecraft-data, see include/versions/v1_8/food.hpp).

            LightweightMC::Core::Logger::info("[Food] " + session.username + " right-click eat (face=255)");

            if (session.selectedSlot >= 0 && session.selectedSlot < 9)
            {
                // Single canonical basis: hotbar stacks live at wire keys 36..44
                // in session.inventory, so the held stack is exactly the entry
                // consumeSelectedFood() decrements. The old dual-key (slot vs
                // 36+slot) dance is gone; keys 0..8 are craft-output/grid/armor
                // slots on the wire and must never hold hotbar stacks.
                const int16_t slotKey = static_cast<int16_t>(36 + session.selectedSlot);
                auto heldIt = session.inventory.find(slotKey);
                int16_t actualKey = slotKey;
                if (heldIt != session.inventory.end() &&
                    (heldIt->second.id <= 0 || heldIt->second.count == 0))
                {
                    heldIt = session.inventory.end();
                }
                if (heldIt != session.inventory.end() && heldIt->second.id > 0 && heldIt->second.count > 0)
                {
                    const int16_t heldId = heldIt->second.id;
                    const uint8_t heldCount = heldIt->second.count;

                    // Experience-bottle use mirrors eating: right-click with a
                    // Bottle o' Enchanting (item id 384) in the selected hotbar
                    // slot consumes one unit and grants random XP per the v1_8
                    // xp.hpp bottle table (3..11, avg 7 — vanilla ThrownExpBottle
                    // yield). Checked before food so bottles never feed hunger.
                    const auto &bottles = LightweightMC::Versions::V1_8::Xp::bottles;
                    const LightweightMC::Versions::V1_8::Xp::Bottle *bottleHit = nullptr;
                    for (const auto &b : bottles)
                        if (b.itemId == heldId) { bottleHit = &b; break; }
                    if (bottleHit)
                    {
                        static thread_local std::minstd_rand xpRng{std::random_device{}()};
                        std::uniform_int_distribution<int> dist(bottleHit->minXp, bottleHit->maxXp);
                        const int xpAmount = dist(xpRng);

                        // Consume exactly one held unit — same slot-basis rules
                        // consumeSelectedFood applies (hotbar key 36+selectedSlot).
                        if (heldIt->second.count > 0)
                            --heldIt->second.count;

                        const int levelsGained = session.grantXp(xpAmount);

                        LightweightMC::Core::Logger::info("[XP Bottle] " + session.username +
                            " used bottle id=" + std::to_string(heldId) +
                            " xp+" + std::to_string(xpAmount) +
                            " levels+" + std::to_string(levelsGained) +
                            " level=" + std::to_string(session.experienceLevel) +
                            " total=" + std::to_string(session.totalExperience));

                        // Persist the consumed unit so reconnects don't restore
                        // a phantom bottle — mirrors the food path.
                        Player::PlayerManager::getInstance().savePlayerState(session, database);

                        // Full inventory sync so the hotbar shows decremented
                        // count — same rationale as the food path (0x30 beats
                        // a single 0x2F for slot-mapping desync safety).
                        InventoryHandler::sendPlayerInventoryWindowItems(clientFd, session, net);

                        // XP bar refresh (S2C UpdateExperience 0x1f): float
                        // progress + varint level + varint total.
                        std::vector<uint8_t> xpPayload;
                        LightweightMC::Network::Packet::writeFloat(xpPayload, session.experienceProgress);
                        LightweightMC::Network::Packet::writeVarInt(xpPayload, static_cast<int32_t>(session.experienceLevel));
                        LightweightMC::Network::Packet::writeVarInt(xpPayload, session.totalExperience);
                        net.sendPacket(clientFd, 0x1f, xpPayload);

                        return;
                    }

                    auto n = LightweightMC::Versions::V1_8::Food::forItem(heldId);
                    if (n.foodPoints > 0)
                    {
                        const bool eaten = session.consumeSelectedFood(n.foodPoints, n.saturation);
                        if (eaten)
                        {
                            LightweightMC::Core::Logger::info("[Food] " + session.username +
                                " ate item id=" + std::to_string(heldId) +
                                " count=" + std::to_string(heldCount) +
                                " food+" + std::to_string(n.foodPoints) +
                                " sat+" + std::to_string(n.saturation));

                            // Persist inventory change (food consumed) immediately
                            // so it survives a crash/disconnect — mirrors the
                            // tool-damage path in handleDigging.
                            Player::PlayerManager::getInstance().savePlayerState(session, database);

                            // Full inventory sync (S2C Window Items 0x30) so the
                            // client hotbar shows the decremented count — mirrors
                            // shift-click handling in handleClickWindow. A single
                            // Set Slot (0x2F) can desync if the client's internal
                            // slot mapping differs from ours (hotbar 0-8 vs wire
                            // 36-44); Window Items sends all 45 slots authoritatively.
                            InventoryHandler::sendPlayerInventoryWindowItems(clientFd, session, net);
                            LightweightMC::Core::Logger::info("[Food] [WindowItems] sent fd=" + std::to_string(clientFd) +
                                " slot=" + std::to_string(actualKey) +
                                " count=" + std::to_string((int)heldIt->second.count));

                            std::vector<uint8_t> hpPayload;
                            LightweightMC::Network::Packet::writeFloat(hpPayload, session.health);
                            LightweightMC::Network::Packet::writeVarInt(hpPayload, static_cast<int32_t>(session.foodLevel));
                            LightweightMC::Network::Packet::writeFloat(hpPayload, session.foodSaturationLevel);
                            net.sendPacket(clientFd, 0x06, hpPayload);
                        }
                        else
                        {
                            // foodPoints > 0 but eaten==false means food bar was full (20).
                            LightweightMC::Core::Logger::warn("[Food] " + session.username +
                                " food bar full — cannot eat item id=" + std::to_string(heldId));
                        }
                    }
                    else
                    {
                        LightweightMC::Core::Logger::warn("[Food] " + session.username +
                            " held item id=" + std::to_string(heldId) +
                            " is not food (foodPoints=0) at slot " + std::to_string(slotKey));
                    }
                }
                else
                {
                    LightweightMC::Core::Logger::warn("[Food] " + session.username +
                        " no edible item in selected slot " + std::to_string(session.selectedSlot));
                    // Dump non-empty inventory keys for diagnosis
                    std::string keys;
                    for (const auto &kv : session.inventory)
                    {
                        if (kv.second.id != -1 && kv.second.count > 0)
                        {
                            if (!keys.empty()) keys += ", ";
                            keys += std::to_string(kv.first) + ":id=" + std::to_string(kv.second.id) +
                                    " cnt=" + std::to_string(kv.second.count);
                        }
                    }
                    LightweightMC::Core::Logger::warn("[Food] " + session.username +
                        " inventory non-empty keys: [" + keys + "]");
                }
            }
            else
            {
                LightweightMC::Core::Logger::warn("[Food] " + session.username +
                    " selectedSlot=" + std::to_string(session.selectedSlot) + " out of range");
            }

            return;
        }

        // 1. VÉRIFICATION DU BLOC CIBLÉ
        int targetChunkX = LightweightMC::Network::Packet::floorDiv(targetX, 16);
        int targetChunkZ = LightweightMC::Network::Packet::floorDiv(targetZ, 16);
        int targetRelX = LightweightMC::Network::Packet::floorMod(targetX, 16);
        int targetRelZ = LightweightMC::Network::Packet::floorMod(targetZ, 16);

        // A2 reconnect chunk-streaming fix: getBlockAt() returns 0 ("air/unknown") for a
        // not-yet-generated chunk. On reconnect the login burst streams chunks lazily, so a
        // ClickWindow arriving before the chest's chunk is on disk would read Air -> existingBlockId=0
        // -> the container never opens and its persisted contents look lost ("chest block gone").
        // Mirror the digging handler (WorldHandler.cpp:163-172) which generates the chunk before
        // reading it. generateChunk is idempotent (early-returns when already generated), so this
        // only costs a one-time sync hitch on a truly-fresh chunk -- the same precedent saveBlockChange
        // accepts for its first edit. Pre-generate the target plus its four horizontal neighbour
        // chunks so double-chest pairing (getBlockAt on the adjacent half, below) also sees real data.
        {
            worldStorage.generateChunk(targetChunkX, targetChunkZ);
            if (targetRelX + 1 >= 16) worldStorage.generateChunk(targetChunkX + 1, targetChunkZ);
            if (targetRelX - 1 < 0)   worldStorage.generateChunk(targetChunkX - 1, targetChunkZ);
            if (targetRelZ + 1 >= 16) worldStorage.generateChunk(targetChunkX, targetChunkZ + 1);
            if (targetRelZ - 1 < 0)   worldStorage.generateChunk(targetChunkX, targetChunkZ - 1);
        }

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

        // ---- Tile Entity init: S2C 0x39 Update Block Entity for block types
        // that carry tile-entity state (chests, signs, furnaces, etc.). This
        // tells the client to initialise the block entity at this position so
        // container GUIs, sign text, furnace burn times, etc. render correctly.
        // For signs placed without prior text we send default blank lines; the
        // client will send UpdateSign (0x12) when the player edits the sign.
        // -------------------------------------------------------------------
        uint16_t placedBlockId = blockData >> 4;
        bool isBlockEntity = (placedBlockId == 23 || placedBlockId == 54 ||
                              placedBlockId == 61 || placedBlockId == 62 ||
                              placedBlockId == 63 || placedBlockId == 68 ||
                              placedBlockId == 84 || placedBlockId == 116 ||
                              placedBlockId == 117 || placedBlockId == 130 ||
                              placedBlockId == 145 || placedBlockId == 146 ||
                              placedBlockId == 154 || placedBlockId == 158);

        if (isBlockEntity)
        {
            // Default sign NBT with blank lines (filled in by UpdateSign later).
            std::vector<uint8_t> signNbt;
            signNbt.push_back(0x0A); // TAG_Compound (no name follows)
            auto nbtStr = [&signNbt](const std::string &nm, const std::string &val)
            {
                signNbt.push_back(0x08); // TAG_String
                uint16_t nlen = static_cast<uint16_t>(nm.size());
                signNbt.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>(nlen & 0xFF));
                signNbt.insert(signNbt.end(), nm.begin(), nm.end());
                uint16_t vlen = static_cast<uint16_t>(val.size());
                signNbt.push_back(static_cast<uint8_t>((vlen >> 8) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>(vlen & 0xFF));
                signNbt.insert(signNbt.end(), val.begin(), val.end());
            };
            auto nbtInt = [&signNbt](const std::string &nm, int32_t v)
            {
                signNbt.push_back(0x03); // TAG_Int
                uint16_t nlen = static_cast<uint16_t>(nm.size());
                signNbt.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>(nlen & 0xFF));
                signNbt.insert(signNbt.end(), nm.begin(), nm.end());
                signNbt.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>(v & 0xFF));
            };
            auto nbtShort = [&signNbt](const std::string &nm, int16_t v)
            {
                signNbt.push_back(0x02); // TAG_Short
                uint16_t nlen = static_cast<uint16_t>(nm.size());
                signNbt.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>(nlen & 0xFF));
                signNbt.insert(signNbt.end(), nm.begin(), nm.end());
                signNbt.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                signNbt.push_back(static_cast<uint8_t>(v & 0xFF));
            };
            if (placedBlockId == 63 || placedBlockId == 68)
            {
                nbtStr("Text1", "");
                nbtStr("Text2", "");
                nbtStr("Text3", "");
                nbtStr("Text4", "");
                nbtInt("x", placeX);
                nbtShort("y", static_cast<int16_t>(placeY));
                nbtInt("z", placeZ);
                nbtStr("id", "Sign");
            }
            signNbt.push_back(0x00); // TAG_End

            uint8_t actionType = (placedBlockId == 63 || placedBlockId == 68) ? 0x03 : 0x00;
            std::vector<uint8_t> bePayload =
                LightweightMC::Network::Protocol::encodeUpdateBlockEntity(
                    static_cast<int32_t>(placedBlockId), placeX, placeY, placeZ,
                    actionType, signNbt);
            net.broadcastPacket(0x35, bePayload, clientFd);
        }

        // Block placement discount (Bug 3 fix). After a successful block
        // placement the held item's stack is decremented by one. When the
        // count reaches zero the slot is cleared. A Set Slot packet (S2C
        // 0x2F) is sent so the client's local copy of the held item matches
        // the server immediately — without this the client keeps an
        // inflated count until the next full inventory sync (e.g. on
        // reconnect), at which point the phantom block re-appears and
        // players can duplicate blocks indefinitely.
        if (session.selectedSlot >= 0 && session.selectedSlot < 9)
        {
            const int16_t heldSlotKey = static_cast<int16_t>(36 + session.selectedSlot);
            auto heldIt = session.inventory.find(heldSlotKey);
            if (heldIt != session.inventory.end() && heldIt->second.id != -1 && heldIt->second.count > 0)
            {
                if (heldIt->second.count > 1)
                {
                    --heldIt->second.count;
                }
                else
                {
                    session.inventory.erase(heldIt);
                }

                // Mirror the updated slot to the client's hotbar window.
                std::vector<uint8_t> setSlot;
                setSlot.push_back(0); // windowId 0 = player inventory
                LightweightMC::Network::Packet::writeShort(setSlot, heldSlotKey);
                auto cur = session.inventory.find(heldSlotKey);
                if (cur == session.inventory.end())
                {
                    LightweightMC::Network::Packet::writeShort(setSlot, -1);
                }
                else
                {
                    auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::
                        translateItemToClient(session.clientProtocol, cur->second.id, cur->second.count, cur->second.damage);
                    LightweightMC::Network::Packet::writeShort(setSlot, tr.id);
                    setSlot.push_back(cur->second.count);
                    LightweightMC::Network::Packet::writeShort(setSlot, cur->second.damage);
                    if (session.clientProtocol >= 47)
                        setSlot.push_back(0); // NBT end-of-compound tag
                }
                net.sendPacket(clientFd, 0x2F, setSlot);
            }
        }
    }

    // ---------------------------------------------------------------------
    // 0x12 UpdateSign — C2S packet emitted when the client edits a sign
    // in world. Wire format per protocol_types.hpp:
    //   long location      (8 bytes, same encoding as Digging / Place)
    //   string text1       (up to 15 chars)
    //   string text2
    //   string text3
    //   string text4
    // The server persists the four text lines to the block_entities table
    // (keyed by block coordinate) and broadcasts S2C 0x39 (Update Block
    // Entity, action type 3 = sign) so all nearby clients render the new
    // text without desync.
    // ---------------------------------------------------------------------
    void handleUpdateSign(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, LightweightMC::Storage::WorldStorage &worldStorage, LightweightMC::Storage::Database &database, LightweightMC::Network::NetworkManager &net)
    {
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

        // ---- Persist sign text to block_entities (chunk_x, chunk_z, rel_x, y, rel_z) ----
        int chunkX = LightweightMC::Network::Packet::floorDiv(signX, 16.0);
        int chunkZ = LightweightMC::Network::Packet::floorDiv(signZ, 16.0);
        int relX = LightweightMC::Network::Packet::floorMod(signX, 16.0);
        int relZ = LightweightMC::Network::Packet::floorMod(signZ, 16.0);

        // Build sign NBT blob: {Text1..Text4, x, y, z, id:"Sign"}
        std::vector<uint8_t> nbt;
        nbt.push_back(0x0A); // TAG_Compound (no name follows)
        auto nbtStr = [&nbt](const std::string &nm, const std::string &val)
        {
            nbt.push_back(0x08); // TAG_String
            uint16_t nlen = static_cast<uint16_t>(nm.size());
            nbt.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
            nbt.push_back(static_cast<uint8_t>(nlen & 0xFF));
            nbt.insert(nbt.end(), nm.begin(), nm.end());
            uint16_t vlen = static_cast<uint16_t>(val.size());
            nbt.push_back(static_cast<uint8_t>((vlen >> 8) & 0xFF));
            nbt.push_back(static_cast<uint8_t>(vlen & 0xFF));
            nbt.insert(nbt.end(), val.begin(), val.end());
        };
        auto nbtInt = [&nbt](const std::string &nm, int32_t v)
        {
            nbt.push_back(0x03); // TAG_Int
            uint16_t nlen = static_cast<uint16_t>(nm.size());
            nbt.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
            nbt.push_back(static_cast<uint8_t>(nlen & 0xFF));
            nbt.insert(nbt.end(), nm.begin(), nm.end());
            nbt.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
            nbt.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            nbt.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            nbt.push_back(static_cast<uint8_t>(v & 0xFF));
        };
        auto nbtShort = [&nbt](const std::string &nm, int16_t v)
        {
            nbt.push_back(0x02); // TAG_Short
            uint16_t nlen = static_cast<uint16_t>(nm.size());
            nbt.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
            nbt.push_back(static_cast<uint8_t>(nlen & 0xFF));
            nbt.insert(nbt.end(), nm.begin(), nm.end());
            nbt.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            nbt.push_back(static_cast<uint8_t>(v & 0xFF));
        };
        nbtStr("Text1", s1);
        nbtStr("Text2", s2);
        nbtStr("Text3", s3);
        nbtStr("Text4", s4);
        nbtInt("x", signX);
        nbtShort("y", static_cast<int16_t>(signY));
        nbtInt("z", signZ);
        nbtStr("id", "Sign");
        nbt.push_back(0x00); // TAG_End

        // Sign post = 63, wall sign = 68. Use the block id at the location if
        // available, otherwise default to sign post (63).
        uint16_t signBlockData = worldStorage.getBlockAt(chunkX, chunkZ, relX, signY, relZ);
        uint16_t signBlockId = signBlockData >> 4;
        if (signBlockId != 63 && signBlockId != 68)
            signBlockId = 63; // default to sign post

        if (!database.saveContainer(chunkX, chunkZ, relX, signY, relZ,
                                    static_cast<uint16_t>(signBlockId), nbt))
        {
            LightweightMC::Core::Logger::warn(
                "[Sign] Failed to persist sign text at (" +
                std::to_string(signX) + ", " + std::to_string(signY) + ", " +
                std::to_string(signZ) + ") for player " + session.username);
        }

        // ---- Broadcast S2C 0x39 Update Block Entity (action type 3 = sign) ----
        std::vector<uint8_t> bePayload =
            LightweightMC::Network::Protocol::encodeUpdateBlockEntity(
                static_cast<int32_t>(signBlockId), signX, signY, signZ, 0x03, nbt);
        net.broadcastPacket(0x35, bePayload, clientFd);

        LightweightMC::Core::Logger::info("[Sign] " + session.username +
                                          " updated sign at (" +
                                          std::to_string(signX) + ", " +
                                          std::to_string(signY) + ", " +
                                          std::to_string(signZ) + ") lines: " +
                                          s1 + " / " + s2 + " / " + s3 + " / " + s4);
    }
} // namespace WorldHandler