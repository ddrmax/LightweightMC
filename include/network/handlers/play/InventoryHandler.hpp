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

} // namespace InventoryHandler