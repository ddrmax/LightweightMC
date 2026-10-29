#include "network/handlers/play/InventoryHandler.hpp"
#include "core/Logger.hpp"
#include "managers/PlayerManager.hpp"

#include <chrono>
#include <unordered_map>

namespace InventoryHandler
{

    void handleHeldItemChange(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, LightweightMC::Network::NetworkManager &net)
    {
        int16_t slot = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);
        session.selectedSlot = slot;

        std::vector<uint8_t> equipPacket;
        LightweightMC::Network::Packet::writeVarInt(equipPacket, clientFd);
        LightweightMC::Network::Packet::writeShort(equipPacket, 0);

        // Use find() rather than operator[] so we do not accidentally insert
        // a default {0,0,0} entry for empty hotbar slots (block id 0 would
        // be reported to other players as "Air" instead of "empty").
        int16_t itemId = -1;
        int16_t itemSlot = slot;
        if (slot >= 0 && slot < 36)
        {
            auto it = session.inventory.find(itemSlot);
            if (it != session.inventory.end() && it->second.id != -1 && it->second.count > 0)
                itemId = it->second.id;
        }

        auto translatedItem = LightweightMC::Network::Protocol::ProtocolTranslator::translateItemToClient(session.clientProtocol, itemId);

        LightweightMC::Network::Packet::writeShort(equipPacket, translatedItem.id);
        if (translatedItem.id != -1)
        {
            auto it = session.inventory.find(itemSlot);
            const int16_t damage = (it != session.inventory.end()) ? it->second.damage : 0;
            const uint8_t count = (it != session.inventory.end()) ? it->second.count : 0;
            equipPacket.push_back(count);
            LightweightMC::Network::Packet::writeShort(equipPacket, damage);
            if (session.clientProtocol >= 47)
                equipPacket.push_back(0); // NBT end-of-compound tag (1.8+ only)
        }

        net.broadcastPacket(0x04, equipPacket, clientFd);
    }

    void handleClickWindow(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, LightweightMC::Storage::Database &database, LightweightMC::Network::NetworkManager &net)
    {
        uint8_t windowId = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        int16_t slot = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);
        uint8_t button = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        (void)button;
        int16_t actionNumber = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);
        uint8_t mode = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        int16_t itemId = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);

        // Optional item body (present only when itemId != -1). The wire format
        // for MC 1.8 (protocol 47) is:
        //     byte   count
        //     short  damage
        //     byte   NBT end-of-compound tag (0 for empty NBT)
        // Older protocols (<47) omit the NBT terminator; we tolerate both.
        uint8_t itemCount = 0;
        int16_t itemDamage = 0;
        const bool hasItem = (itemId != -1);
        if (hasItem)
        {
            if (offset < packetEnd)
                itemCount = data[offset++];
            if (offset + 1 < packetEnd)
                itemDamage = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);
            // Protocol 47+ appends a single-byte NBT terminator (0x00 = TAG_End
            // for a stack with no attached NBT). Consume it when present.
            if (session.clientProtocol >= 47 && offset < packetEnd)
                offset++;
        }

        // windowId == 0 is the player's own inventory window. It is never
        // registered in session.openWindows (that map only tracks container
        // windows: chests, furnaces, ...), yet the real MC 1.8 client still
        // sends WindowClick packets against it when the player reorganises
        // slots. Without a special case here, the "unknown window" branch
        // fires, the server replies Transaction(accepted=false), and the
        // client's UI visually drops the item even though we never touched
        // session.inventory. Handle it directly against the session's slot
        // table (0..44 covers the 3-row inventory + 5-row hotbar).
        if (windowId == 0)
        {
            const int invSize = 45;
            bool accepted = false;
            if (slot >= 0 && slot < invSize)
            {
                const Player::ItemStack before = (session.inventory.find(slot) != session.inventory.end())
                                                     ? session.inventory[slot]
                                                     : Player::ItemStack{-1, 0, 0};
                Player::ItemStack after = before;
                bool skipSetSlot = false;
                if (mode == 0) // grab (client already picked it up locally)
                {
                    after = {-1, 0, 0};
                    accepted = true;
                }
                else if (mode == 1) // shift-click
                {
                    const Player::ItemStack clicked = before;
                    if (clicked.id != -1 && clicked.count > 0)
                    {
                        // Vanilla 1.8 layout: 0..8 = hotbar, 9..35 = main inventory.
                        int targetStart = 0, targetEnd = 0;
                        if (slot >= 0 && slot < 9)
                        {
                            targetStart = 9;  targetEnd = 36; // hotbar → main inventory
                        }
                        else if (slot >= 9 && slot < 36)
                        {
                            targetStart = 0;  targetEnd = 9;  // main inventory → hotbar
                        }
                        else
                        {
                            // Slots 36..44 (armor/off-hand): do not shift-click.
                            accepted = false;
                        }

                        if (accepted)
                        {
                            const int kMaxStack = 64;
                            int remaining = static_cast<int>(clicked.count);

                            // Merge into matching stacks in target region
                            for (auto &kv : session.inventory)
                            {
                                if (kv.first >= targetStart && kv.first < targetEnd &&
                                    kv.second.id == clicked.id &&
                                    kv.second.damage == clicked.damage &&
                                    kv.second.count < static_cast<uint8_t>(kMaxStack))
                                {
                                    int space = kMaxStack - static_cast<int>(kv.second.count);
                                    int add = std::min(remaining, space);
                                    kv.second.count = static_cast<uint8_t>(kv.second.count + add);
                                    remaining -= add;
                                    if (remaining == 0)
                                        break;
                                }
                            }

                            // Fill empty slots in target region
                            for (int i = targetStart; i < targetEnd && remaining > 0; ++i)
                            {
                                if (session.inventory.find(i) == session.inventory.end())
                                {
                                    int give = std::min(remaining, kMaxStack);
                                    session.inventory[i] = {clicked.id, static_cast<uint8_t>(give), clicked.damage};
                                    remaining -= give;
                                }
                            }

                            // Update source slot
                            if (remaining > 0)
                            {
                                session.inventory[slot] = {clicked.id, static_cast<uint8_t>(remaining), clicked.damage};
                            }
                            else
                            {
                                session.inventory.erase(slot);
                            }

                            // Full inventory sync replaces per-slot Set Slot
                            sendPlayerInventoryWindowItems(clientFd, session, net);
                            // Persist shift-click changes so they survive reconnect
                            Player::PlayerManager::getInstance().savePlayerState(session, database);
                        }
                    }
                    else
                    {
                        accepted = true;
                    }
                    skipSetSlot = true;
                }
                else if (mode == 2 || mode == 3) // throw to world / destroy
                {
                    after = {-1, 0, 0};
                    accepted = true;
                }
                else if (mode >= 5 && mode <= 7) // mouse drag (start/mid/end)
                {
                    // Client manages the drag state locally; we just need to
                    // acknowledge the transaction so the UI doesn't show items
                    // as "despawned" (drag fell through to default which erased
                    // the slot). Don't mutate the slot — the drag's final
                    // redistribution is a subsequent click that we handle.
                    accepted = true;
                    skipSetSlot = true;
                }

                if (!skipSetSlot)
                {
                    session.inventory[slot] = after;

                    std::vector<uint8_t> setSlot;
                    setSlot.push_back(windowId);
                    LightweightMC::Network::Packet::writeShort(
                        setSlot, static_cast<int16_t>(slot));
                    auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::
                        translateItemToClient(session.clientProtocol, after.id);
                    LightweightMC::Network::Packet::writeShort(setSlot, tr.id);
                    if (tr.id != -1)
                    {
                        setSlot.push_back(after.count);
                        LightweightMC::Network::Packet::writeShort(setSlot, after.damage);
                        if (session.clientProtocol >= 47)
                            setSlot.push_back(0); // NBT end-of-compound
                    }
                    net.sendPacket(clientFd, 0x2F, setSlot);
                }

                std::vector<uint8_t> confirm;
                confirm.push_back(windowId);
                LightweightMC::Network::Packet::writeShort(confirm, actionNumber);
                confirm.push_back(accepted ? 1 : 0);
                net.sendPacket(clientFd, 0x32, confirm);
            }
            return;
        }

        auto wIt = session.openWindows.find(windowId);
        if (wIt == session.openWindows.end())
        {
            // No container registered for this windowId (session was likely
            // reloaded after logout but the client kept the window open).
            // Tell the client the transaction failed and drop.
            std::vector<uint8_t> confirm;
            confirm.push_back(windowId);
            LightweightMC::Network::Packet::writeShort(confirm, actionNumber);
            confirm.push_back(0);
            net.sendPacket(clientFd, 0x32, confirm);
            LightweightMC::Core::Logger::debug("[Container] Click Window " + std::to_string(windowId) +
                                               " unknown (no registered window for " + session.username + ")");
            return;
        }

        Player::WindowInfo &win = wIt->second;
        const int slotCount = static_cast<int>(win.items.size());
        bool accepted = false;
        bool stateChanged = false;

        if (slot >= 0 && slot < slotCount)
        {
            const Player::ItemStack before = win.items[slot];
            Player::ItemStack after = before;

            if (mode == 0) // grab (take the item out into the player's inventory)
            {
                // Prefer the client-supplied item (usually == slot contents);
                // fall back to the slot's own content when the client did not
                // include an item on a grab click.
                Player::ItemStack taken = before;
                if (hasItem)
                {
                    auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::
                        translateItemFromClient(session.clientProtocol, itemId, itemCount, itemDamage);
                    taken = {tr.id, tr.count, tr.damage};
                }
                // Move into player inventory via the shared merge helper.
                // Items that can't fit remain in the container slot.
                if (taken.id != -1 && taken.count > 0)
                {
                    int remaining = mergeItemIntoInventory(
                        session, taken.id, taken.count, taken.damage);
                    if (remaining > 0)
                    {
                        after = {taken.id, static_cast<uint8_t>(remaining), taken.damage};
                    }
                    else
                    {
                        after = {-1, 0, 0};
                    }

                    // Sync player inventory to the client. Without this the
                    // client never sees items pulled from a container, which
                    // manifests as "items lost" until reconnect.
                    sendPlayerInventoryWindowItems(clientFd, session, net);
                    Player::PlayerManager::getInstance().savePlayerState(session, database);
                }
                else
                {
                    after = {-1, 0, 0};
                }
                accepted = true;
            }
            else if (mode == 1) // shift-click (MC 1.8 protocol: mode 1 = shift)
            {
                // Shift-click moves the entire stack from the container slot
                // into the player's inventory. Matching stacks are merged,
                // empty slots fill in, and any remainder stays in the slot.
                const Player::ItemStack clicked = win.items[slot];
                if (clicked.id != -1 && clicked.count > 0)
                {
                    int remaining = mergeItemIntoInventory(
                        session, clicked.id, clicked.count, clicked.damage);

                    if (remaining == 0)
                    {
                        after = {-1, 0, 0};
                    }
                    else
                    {
                        after = {clicked.id, static_cast<uint8_t>(remaining), clicked.damage};
                    }
                    win.items[slot] = after;
                    win.dirty = true;

                    // Full inventory sync — the player's inventory changed.
                    sendPlayerInventoryWindowItems(clientFd, session, net);
                    Player::PlayerManager::getInstance().savePlayerState(session, database);
                    accepted = true;
                }
            }
            else if (mode == 2 || mode == 3) // throw to world / destroy
            {
                if (before.id != -1)
                {
                    after = {-1, 0, 0};
                    accepted = true;
                }
                else
                {
                    accepted = false; // nothing to throw / destroy
                }
            }
            else // unknown mode – fall back to legacy behavior
            {
                if (hasItem)
                {
                    auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::
                        translateItemFromClient(session.clientProtocol, itemId, itemCount, itemDamage);
                    after = {tr.id, tr.count, tr.damage};
                }
                else
                {
                    after = {-1, 0, 0};
                }
                accepted = true;
            }

            win.items[slot] = after;
            stateChanged = (after.id != before.id) ||
                           (after.count != before.count) ||
                           (after.damage != before.damage);
            // Only mark dirty when the state actually changes. Rapid
            // mouse-down click repeats that don't change anything
            // (~150ms intervals per MC 1.8 client) must not hammer the
            // DB or emit log spam.
            if (stateChanged)
                win.dirty = true;
        }

        // Persist immediately: small write per click, crash-safe. Keeps the
        // container row authoritative even if the server is killed after
        // this packet (server shutdown or player disconnect).
        //
        // The saveContainer() call is still invoked on every state change
        // so the DB stays authoritative. Only the INFO log line is throttled:
        // a client holding down the mouse button emits one WindowClick per
        // ~150ms, and with mode-aware code above those no-op clicks no
        // longer flip win.dirty anyway. But for genuinely distinct state
        // changes (real grabs, real places), we still don't want 20 lines
        // per second to blow up the console, so we coalesce log lines to
        // at most one per (clientFd, windowId) per wall-clock second.
        if (win.dirty)
        {
            std::vector<uint8_t> blob;
            Player::serializeContainerItems(win.items, blob);
            if (database.saveContainer(win.chunkX, win.chunkZ, win.relX,
                                       win.y, win.relZ, win.blockType, blob))
            {
                static std::unordered_map<int64_t, std::chrono::steady_clock::time_point> lastPersistLogAt;
                const int64_t key = (static_cast<int64_t>(clientFd) << 8) | static_cast<int64_t>(windowId);
                const auto nowTp = std::chrono::steady_clock::now();
                auto lit = lastPersistLogAt.find(key);
                if (lit == lastPersistLogAt.end() ||
                    std::chrono::duration_cast<std::chrono::milliseconds>(nowTp - lit->second).count() >= 1000)
                {
                    LightweightMC::Core::Logger::debug("[Container] Persisted window " +
                                                       std::to_string(windowId) + " for " + session.username);
                    lastPersistLogAt[key] = nowTp;
                }
            }
            win.dirty = false;
        }

        // Reflect the change to the client with S2C 0x2F Set Slot.
        //
        // Wire format expected by the MC 1.8 (protocol 47) client decoder
        // for a container window:
        //     uint8  windowId
        //     int16  slot            <- short, NOT byte
        //     int16  itemClientID    (-1 to clear the slot)
        //     uint8  count           (only if itemClientID != -1)
        //     int16  damage          (only if itemClientID != -1)
        //     uint8  NBT end tag (0) (only if itemClientID != -1 and
        //                             clientProtocol >= 47)
        //
        // Packet ID notes:
        //   - Clientbound (S2C) IDs per PrismarineJS protocol_1_8.hpp:
        //       0x2D OpenWindow, 0x2E CloseWindow,
        //       0x2F SetSlot, 0x30 WindowItems,
        //       0x31 CraftProgressBar, 0x32 Transaction.
        //     -> SetSlot is 0x2F, NOT 0x31. Sending the SetSlot body
        //        under packet ID 0x31 makes the client treat it as a
        //        CraftProgress packet (which is only 5 bytes), so a 4-
        //        or 9-byte body caused UnsafeReadException. The correct
        //        ID also fixes the "chests don't keep state" symptom:
        //        the client never saw a SetSlot packet, so subsequent
        //        UI/window state on the client never reflected the
        //        server-side update even though the DB blob did.
        //   - Minimum body length is 5 bytes (empty case). The previous
        //     implementation wrote slotId as a single byte, which produced
        //     a 4-byte body. That caused the client to read short(slot) at
        //     ridx=2 and then try short(itemClientID) at ridx=4 with cap=5,
        //     throwing UnsafeReadException.
        {
            std::vector<uint8_t> setSlot;
            setSlot.push_back(windowId);
            LightweightMC::Network::Packet::writeShort(
                setSlot, static_cast<int16_t>(slot));
            const Player::ItemStack &cur = (slot >= 0 && slot < slotCount)
                                               ? win.items[slot]
                                               : Player::ItemStack{-1, 0, 0};
            auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::
                translateItemToClient(session.clientProtocol, cur.id);
            LightweightMC::Network::Packet::writeShort(setSlot, tr.id);
            if (tr.id != -1)
            {
                setSlot.push_back(cur.count);
                LightweightMC::Network::Packet::writeShort(setSlot, cur.damage);
                if (session.clientProtocol >= 47)
                    setSlot.push_back(0); // NBT end-of-compound
            }
            net.sendPacket(clientFd, 0x2F, setSlot);
        }

        std::vector<uint8_t> confirm;
        confirm.push_back(windowId);
        LightweightMC::Network::Packet::writeShort(confirm, actionNumber);
        confirm.push_back(accepted ? 1 : 0);
        net.sendPacket(clientFd, 0x32, confirm);
    }

    void handleCreativeAction(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, LightweightMC::Network::NetworkManager &net)
    {
        int16_t slot = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);
        int16_t itemId = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);

        if (itemId != -1 && offset + 3 <= packetEnd)
        {
            uint8_t count = data[offset++];
            int16_t damage = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);

            auto translatedItem = LightweightMC::Network::Protocol::ProtocolTranslator::translateItemFromClient(session.clientProtocol, itemId, count, damage);
            if (slot >= 0 && slot < 45)
            {
                session.inventory[slot] = {translatedItem.id, translatedItem.count, translatedItem.damage};
            }
        }
        else
        {
            if (slot >= 0 && slot < 45)
            {
                session.inventory[slot] = {-1, 0, 0};
            }
        }

        if (slot == session.selectedSlot)
        {
            std::vector<uint8_t> equipPacket;
            LightweightMC::Network::Packet::writeVarInt(equipPacket, clientFd);
            LightweightMC::Network::Packet::writeShort(equipPacket, 0);

            int16_t currentItem = session.inventory[slot].id;
            auto translatedItem = LightweightMC::Network::Protocol::ProtocolTranslator::translateItemToClient(session.clientProtocol, currentItem);

            LightweightMC::Network::Packet::writeShort(equipPacket, translatedItem.id);
            if (translatedItem.id != -1)
            {
                equipPacket.push_back(session.inventory[slot].count);
                LightweightMC::Network::Packet::writeShort(equipPacket, session.inventory[slot].damage);
                equipPacket.push_back(0);
            }

            net.broadcastPacket(0x04, equipPacket, clientFd);
        }
    }

    // ---------------------------------------------------------------------
    // Remaining MC 1.8 (protocol 47) inventory-window C2S handlers.
    // ---------------------------------------------------------------------

    void handleCloseWindow(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, LightweightMC::Storage::Database &database)
    {
        (void)clientFd;
        const uint8_t windowId = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));

        auto wIt = session.openWindows.find(windowId);
        if (wIt != session.openWindows.end())
        {
            Player::WindowInfo &win = wIt->second;
            if (win.dirty)
            {
                std::vector<uint8_t> blob;
                Player::serializeContainerItems(win.items, blob);
                database.saveContainer(win.chunkX, win.chunkZ, win.relX,
                                       win.y, win.relZ, win.blockType, blob);
                win.dirty = false;
            }
            session.openWindows.erase(wIt);
            LightweightMC::Core::Logger::debug("[Container] Closed window " +
                                               std::to_string(windowId) + " for " + session.username);
        }
        else
        {
            LightweightMC::Core::Logger::debug("[Container] CloseWindow " +
                                               std::to_string(windowId) + " (already closed) for " + session.username);
        }
    }

    void handleTransactionAck(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        (void)clientFd;
        const uint8_t windowId = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        const int16_t action = LightweightMC::Network::Packet::readShortBounded(data, offset, packetEnd);
        const uint8_t accepted = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));

        LightweightMC::Core::Logger::debug("[Container] Transaction ack from " + session.username +
                                           " window=" + std::to_string(windowId) +
                                           " action=" + std::to_string(action) +
                                           " accepted=" + std::to_string(accepted));
    }

    void handleEnchantItem(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        (void)clientFd;
        const uint8_t windowId = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));
        const uint8_t enchantment = static_cast<uint8_t>(LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd));

        LightweightMC::Core::Logger::debug("[Enchant] " + session.username +
                                           " window=" + std::to_string(windowId) +
                                           " enchantment=" + std::to_string(enchantment) +
                                           " (enchanting model pending)");
    }

    void setItem(LightweightMC::Player::PlayerSession &session, int slot, const LightweightMC::Player::ItemStack &item)
    {
        if (slot < 0 || slot >= 45) return;
        session.inventory[slot] = item;

        std::vector<uint8_t> buf;
        buf.push_back(0); // window id
        LightweightMC::Network::Packet::writeShort(buf, static_cast<int16_t>(slot));
        LightweightMC::Network::Packet::writeShort(buf, item.id);
        if (item.id != -1)
        {
            buf.push_back(item.count);
            LightweightMC::Network::Packet::writeShort(buf, item.damage);
            buf.push_back(0);
        }
        // NetworkManager::sendPacket(session.fd, 0x29, buf); // transmit via caller net
    }

    void removeItem(LightweightMC::Player::PlayerSession &session, int slot)
    {
        if (slot < 0 || slot >= 45) return;
        session.inventory.erase(slot);

        std::vector<uint8_t> buf;
        buf.push_back(0);
        LightweightMC::Network::Packet::writeShort(buf, static_cast<int16_t>(slot));
        LightweightMC::Network::Packet::writeShort(buf, -1);
        // NetworkManager::sendPacket(session.fd, 0x29, buf); // transmit via caller net
    }

    void decreaseCount(LightweightMC::Player::PlayerSession &session, int slot, int amount)
    {
        if (slot < 0 || slot >= 45) return;
        auto it = session.inventory.find(slot);
        if (it == session.inventory.end()) return;
        it->second.count -= static_cast<uint8_t>(amount);
        if (it->second.count <= 0) session.inventory.erase(it);

        std::vector<uint8_t> buf;
        buf.push_back(0);
        LightweightMC::Network::Packet::writeShort(buf, static_cast<int16_t>(slot));
        auto it2 = session.inventory.find(slot);
        if (it2 != session.inventory.end())
        {
            LightweightMC::Network::Packet::writeShort(buf, it2->second.id);
            buf.push_back(it2->second.count);
            LightweightMC::Network::Packet::writeShort(buf, it2->second.damage);
            buf.push_back(0);
        }
        else
        {
            LightweightMC::Network::Packet::writeShort(buf, -1);
        }
        // NetworkManager::sendPacket(session.fd, 0x29, buf); // transmit via caller net
    }

    uint16_t getMaxDurability(int32_t itemId)
    {
        // LWMC tool class - the item ids used by this codebase live in
        // world/MC18ItemIds.hpp: HATCHET=80 ... SCYTHE=88. Durability values
        // mirror the vanilla 1.8 tier pattern (weakest ~45, sturdiest tool
        // ~100 uses) scaled into the LWMC item space; every other item
        // returns 0 so wear logic can gate on the zero sentinel.
        switch (itemId)
        {
            case 80: return 60;  // HATCHET
            case 81: return 55;  // MACHETE
            case 82: return 45;  // SPIKE
            case 83: return 70;  // ADZE
            case 84: return 90;  // HOE
            case 85: return 65;  // MATTOCK
            case 86: return 80;  // CHISEL
            case 87: return 100; // HAMMER
            case 88: return 75;  // SCYTHE
            default: return 0;   // not durability-bearing
        }
    }
    void applyToolDamage(LightweightMC::Network::NetworkManager &net, int clientFd, LightweightMC::Player::PlayerSession &session, int16_t slotKey, int amount)
    {
        if (slotKey < 0 || slotKey >= 45) return;
        auto it = session.inventory.find(slotKey);
        if (it == session.inventory.end()) return;

        uint16_t maxDur = getMaxDurability(it->second.id);
        if (maxDur == 0) return; // not durability-bearing — no wear

        bool broken = static_cast<uint16_t>(it->second.damage + amount) >= maxDur;
        if (broken)
        {
            session.inventory.erase(it);
        }
        else
        {
            it->second.damage += static_cast<int16_t>(amount);
        }

        // Mirror the slot with an S2C Set Slot packet using the same wire
        // layout handleClickWindow emits at runtime: windowId byte, short
        // slot, short item (-1 when empty; otherwise count + damage + NBT
        // end tag for protocol 47). Send id follows the in-repo convention
        // (0x2F) that the python probes assert on, not the dead 0x29 path.
        std::vector<uint8_t> buf;
        buf.push_back(0); // windowId 0 = player inventory / hotbar window
        LightweightMC::Network::Packet::writeShort(buf, slotKey);
        auto cur = session.inventory.find(slotKey);
        if (cur == session.inventory.end())
        {
            LightweightMC::Network::Packet::writeShort(buf, -1);
        }
        else
        {
            auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::translateItemToClient(session.clientProtocol, cur->second.id);
            LightweightMC::Network::Packet::writeShort(buf, tr.id);
            buf.push_back(cur->second.count);
            LightweightMC::Network::Packet::writeShort(buf, cur->second.damage);
            if (session.clientProtocol >= 47)
                buf.push_back(0); // NBT end-of-compound tag (1.8 only)
        }
        net.sendPacket(clientFd, 0x2F, buf);

        if (broken)
            LightweightMC::Core::Logger::info("Tool in slot " + std::to_string(slotKey) + " shattered for player " + session.username);
    }

    // --- Inventory merge / sync helpers ----------------------------------------

    int mergeItemIntoInventory(LightweightMC::Player::PlayerSession &session,
                               int16_t itemId, uint8_t count, int16_t damage)
    {
        const int kMaxStack = 64; // vanilla 1.8 protocol 47 stack cap
        int remaining = static_cast<int>(count);

        // 1. Merge into existing matching stacks (slots 0..35: hotbar 0..8 + main inv 9..35)
        for (auto &kv : session.inventory)
        {
            if (kv.first < 0 || kv.first >= 36)
                continue;
            if (kv.second.id == itemId && kv.second.damage == damage &&
                kv.second.count < static_cast<uint8_t>(kMaxStack))
            {
                const int space = kMaxStack - static_cast<int>(kv.second.count);
                const int add = std::min(remaining, space);
                if (add > 0)
                {
                    kv.second.count = static_cast<uint8_t>(kv.second.count + add);
                    remaining -= add;
                    if (remaining == 0)
                        return 0;
                }
            }
        }

        // 2. Fill empty slots (hotbar 0..8 first, then main inv 9..35)
        for (int slot = 0; slot < 9 && remaining > 0; ++slot)
        {
            if (session.inventory.find(slot) == session.inventory.end())
            {
                const int give = std::min(remaining, kMaxStack);
                session.inventory[slot] = {itemId, static_cast<uint8_t>(give), damage};
                remaining -= give;
            }
        }
        for (int slot = 9; slot < 36 && remaining > 0; ++slot)
        {
            if (session.inventory.find(slot) == session.inventory.end())
            {
                const int give = std::min(remaining, kMaxStack);
                session.inventory[slot] = {itemId, static_cast<uint8_t>(give), damage};
                remaining -= give;
            }
        }

        return remaining;
    }

    void sendPlayerInventoryWindowItems(int clientFd,
                                        LightweightMC::Player::PlayerSession &session,
                                        LightweightMC::Network::NetworkManager &net)
    {
        std::vector<uint8_t> payload;
        payload.push_back(0); // windowId 0 = player inventory
        LightweightMC::Network::Packet::writeShort(payload, static_cast<int16_t>(45));

        for (int16_t i = 0; i < 45; ++i)
        {
            auto it = session.inventory.find(i);
            if (it != session.inventory.end() && it->second.id != -1 && it->second.count > 0)
            {
                auto tr = LightweightMC::Network::Protocol::ProtocolTranslator::
                    translateItemToClient(session.clientProtocol, it->second.id,
                                          it->second.count, it->second.damage);
                LightweightMC::Network::Packet::writeShort(payload, tr.id);
                payload.push_back(tr.count);
                LightweightMC::Network::Packet::writeShort(payload, tr.damage);
                if (session.clientProtocol >= 47)
                    payload.push_back(0); // NBT end-of-compound tag
            }
            else
            {
                LightweightMC::Network::Packet::writeShort(payload, static_cast<int16_t>(-1));
            }
        }
        net.sendPacket(clientFd, 0x30, payload);
    }
} // namespace InventoryHandler