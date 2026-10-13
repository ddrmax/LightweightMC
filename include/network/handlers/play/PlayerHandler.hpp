#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include "player/PlayerSession.hpp"
#include "network/NetworkManager.hpp"
#include "core/utils.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include "network/protocol/BaseProtocol.hpp"

namespace World
{
    class WorldManager;
}

namespace PlayerHandler
{

    using ClientMap = std::unordered_map<int, LightweightMC::Player::PlayerSession>;

    void handleKeepAlive(int clientFd,
                         const uint8_t *data,
                         size_t &offset,
                         size_t packetEnd,
                         LightweightMC::Network::NetworkManager &net);

    void handleChatMessage(int clientFd,
                           LightweightMC::Player::PlayerSession &session,
                           const uint8_t *data,
                           size_t &offset,
                           size_t packetEnd,
                           ClientMap &clients,
                           LightweightMC::Network::NetworkManager &net);

    void handlePlayerMovement(int clientFd,
                              LightweightMC::Player::PlayerSession &session,
                              const uint8_t *data,
                              size_t &offset,
                              size_t packetEnd, int32_t packetId,
                              LightweightMC::World::WorldManager &worldManager,
                              LightweightMC::Network::NetworkManager &net);

    void handleAnimation(int clientFd, LightweightMC::Network::NetworkManager &net);

    // 0x03 Flying — C2S packet carrying only the "on ground" flag. Sent by
    // the client every frame while the player is airborne (creative /
    // spectator flight). Consumed but not acted upon; the server keeps its
    // own session.onGround value that the Position packet already updates.
    void handleFlying(int clientFd,
                      LightweightMC::Player::PlayerSession &session,
                      const uint8_t *data,
                      size_t &offset,
                      size_t packetEnd);

    // 0x13 Abilities — C2S packet the client sends once at login. It
    // advertises whether the client permits flight and (for
    // spectator/creative servers) the flying / walking speeds the client
    // expects. We capture the flags into the session so teleporting a
    // player between game modes can restore flight state.
    void handleClientAbilities(int clientFd,
                               LightweightMC::Player::PlayerSession &session,
                               const uint8_t *data,
                               size_t &offset,
                               size_t packetEnd);

    // 0x14 TabComplete — C2S packet: one string that the client wants
    // autocompleted. The server has no command tree; log only.
    void handleTabComplete(int clientFd,
                           LightweightMC::Player::PlayerSession &session,
                           const uint8_t *data,
                           size_t &offset,
                           size_t packetEnd);

    // 0x15 Settings — C2S packet: locale + view distance + chat flags +
    // chat colors + difficulty + show-cape. We persist viewDistance for
    // chunk streaming decisions and difficulty for spawn-time HUD state.
    void handleClientSettings(int clientFd,
                              LightweightMC::Player::PlayerSession &session,
                              const uint8_t *data,
                              size_t &offset,
                              size_t packetEnd);

    // 0x16 ClientCommand — C2S packet used by the client to request
    // "client-side command" re-send (used with ViaVersion multi-version
    // bridges). Payload: byte command id, int32 parameter. Log and drop.
    void handleClientCommand(int clientFd,
                             LightweightMC::Player::PlayerSession &session,
                             const uint8_t *data,
                             size_t &offset,
                             size_t packetEnd);

    // 0x17 CustomPayload — C2S packet: channel string + opaque byte blob.
    // Plugins and ViaVersion use these to tunnel version-specific data.
    void handleCustomPayload(int clientFd,
                             LightweightMC::Player::PlayerSession &session,
                             const uint8_t *data,
                             size_t &offset,
                             size_t packetEnd);

    // 0x18 Spectate — C2S packet: target varint (an entity id). Client
    // asks the server to switch its camera to that entity. Log only.
    void handleSpectate(int clientFd,
                        LightweightMC::Player::PlayerSession &session,
                        const uint8_t *data,
                        size_t &offset,
                        size_t packetEnd);

    // 0x19 ResourcePackReceive — C2S packet: string status (OK / CANCELED
    // / error text). Sent when the server previously emitted 0x48
    // ResourcePack. Log only.
    void handleResourcePackReceive(int clientFd,
                                   LightweightMC::Player::PlayerSession &session,
                                   const uint8_t *data,
                                   size_t &offset,
                                   size_t packetEnd);

} // namespace PlayerHandler