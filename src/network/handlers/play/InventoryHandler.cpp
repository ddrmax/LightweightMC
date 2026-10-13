#include "network/handlers/play/InventoryHandler.hpp"
#include "core/Logger.hpp"

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
        int16_t itemSlot = 36 + slot;
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
                if (mode == 0) // grab (client already picked it up locally)
                {
                    after = {-1, 0, 0};
                    accepted = true;
                }
                else if (mode == 1) // place
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
                else if (mode == 2 || mode == 3) // throw to world / destroy
                {
                    after = {-1, 0, 0};
                    accepted = true;
                }

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
                // Move into player inventory: stack on a matching stack (cap
                // 12 per MC 1.8 stack), else first empty slot in 0..44.
                if (taken.id != -1 && taken.count > 0)
                {
                    bool merged = false;
                    for (auto &kv : session.inventory)
                    {
                        if (kv.first >= 0 && kv.first < 45 &&
                            kv.second.id == taken.id &&
                            kv.second.damage == taken.damage &&
                            int(kv.second.count) + int(taken.count) <= 12)
                        {
                            kv.second.count = static_cast<uint8_t>(kv.second.count + taken.count);
                            merged = true;
                            break;
                        }
                    }
                    if (!merged)
                    {
                        for (int i = 0; i < 45; ++i)
                        {
                            if (session.inventory.find(i) == session.inventory.end())
                            {
                                session.inventory[i] = taken;
                                merged = true;
                                break;
                            }
                        }
                    }
                    // If inventory is completely full we intentionally drop
                    // the item: MC 1.8 would throw it at the player's feet,
                    // but we don't model ground drops yet, so the item is
                    // silently discarded (mirrors other servers' fallback).
                }
                after = {-1, 0, 0};
                accepted = true;
            }
            else if (mode == 1) // place item from cursor into container slot
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

        if (slot == (36 + session.selectedSlot))
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

} // namespace InventoryHandler