#pragma once

#include <cstdint>
#include <cstddef>
#include "player/PlayerSession.hpp"
#include "network/NetworkManager.hpp"
#include "storage/Database.hpp"
#include "core/utils.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include "network/protocol/BaseProtocol.hpp"
#include <vector>

using namespace LightweightMC;
namespace InventoryHandler
{

    void handleHeldItemChange(int clientFd,
                              LightweightMC::Player::PlayerSession &session,
                              const uint8_t *data,
                              size_t &offset,
                              size_t packetEnd,
                              LightweightMC::Network::NetworkManager &net);

    void handleClickWindow(int clientFd,
                           LightweightMC::Player::PlayerSession &session,
                           const uint8_t *data,
                           size_t &offset,
                           size_t packetEnd,
                           LightweightMC::Storage::Database &database,
                           LightweightMC::Network::NetworkManager &net);

    void handleCreativeAction(int clientFd,
                              LightweightMC::Player::PlayerSession &session,
                              const uint8_t *data,
                              size_t &offset,
                              size_t packetEnd,
                              LightweightMC::Network::NetworkManager &net);

    // 0x0D CloseWindow — C2S packet: one byte windowId. Client signals it
    // is closing an inventory window (chest, crafting-table, sign editor
    // …). The server flushes any pending dirty container to SQLite and
    // removes the entry from the session map so a subsequent click on the
    // same id is treated as a fresh interaction.
    void handleCloseWindow(int clientFd,
                           LightweightMC::Player::PlayerSession &session,
                           const uint8_t *data,
                           size_t &offset,
                           size_t packetEnd,
                           LightweightMC::Storage::Database &database);

    // 0x0F Transaction (client -> server) — C2S packet:
    //   byte windowId, short action, byte accepted.
    // Client echoes a server transaction (from 0x32) back to the server,
    // typically to report whether the operation succeeded. We log the
    // state so admins can trace plugin-style flows; the server does not
    // currently mutate anything from this.
    void handleTransactionAck(int clientFd,
                              LightweightMC::Player::PlayerSession &session,
                              const uint8_t *data,
                              size_t &offset,
                              size_t packetEnd);

    // 0x11 EnchantItem — C2S packet: byte windowId, byte enchantment.
    // Client chose an enchantment-table option after 0x31 Craft Progress
    // Bar. The server has not modelled enchanting yet; we consume the
    // payload to keep the byte stream aligned.
    void handleEnchantItem(int clientFd,
                           LightweightMC::Player::PlayerSession &session,
                           const uint8_t *data,
                           size_t &offset,
                           size_t packetEnd);

    // Atomic inventory mutation helpers (Phase 7/12).
    void setItem(LightweightMC::Player::PlayerSession &session, int slot, const LightweightMC::Player::ItemStack &item);
    void removeItem(LightweightMC::Player::PlayerSession &session, int slot);
    void decreaseCount(LightweightMC::Player::PlayerSession &session, int slot, int amount);

    // Item durability - getMaxDurability returns max damage for LWMC tool ids
    // 80..88 (world/MC18ItemIds.hpp); 0 means the stack is not durability-bearing.
    // Digging (C2S wire id 0x07) applies tool wear on successful destruction, and
    // applyToolDamage mirrors each slot update with an S2C Set Slot packet.
    uint16_t getMaxDurability(int32_t itemId);
    void applyToolDamage(LightweightMC::Network::NetworkManager &net, int clientFd, LightweightMC::Player::PlayerSession &session, int16_t slotKey, int amount);

    // --- Inventory merge / sync helpers (used by Click Window shift-click
    // and block-break drop logic) ---

    // Merges an item stack into the player's inventory (slots 9..44).
    // Matching stacks are filled first, then empty slots, capped at 64.
    // Returns the count that could not be placed.
    int mergeItemIntoInventory(LightweightMC::Player::PlayerSession &session,
                               int16_t itemId, uint8_t count, int16_t damage);

    // Sends S2C Window Items (0x30) with all 45 player-inventory slots so
    // the client view matches the server after a grab, shift-click, or
    // block-break drop. Window 0 = player inventory.
    void sendPlayerInventoryWindowItems(int clientFd,
                                        LightweightMC::Player::PlayerSession &session,
                                        LightweightMC::Network::NetworkManager &net);

} // namespace InventoryHandler
