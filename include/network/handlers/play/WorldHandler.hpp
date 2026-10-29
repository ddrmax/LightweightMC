#pragma once

#include <cstdint>
#include <cstddef>
#include "player/PlayerSession.hpp"
#include "network/NetworkManager.hpp"
#include "storage/WorldStorage.hpp"
#include "storage/Database.hpp"
#include "core/utils.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include "network/protocol/BaseProtocol.hpp"

class NetworkManager;
namespace World
{
    class WorldStorage;
}

namespace WorldHandler
{

    void handleDigging(int clientFd,
                       LightweightMC::Player::PlayerSession &session,
                       const uint8_t *data,
                       size_t &offset,
                       size_t packetEnd,
                       LightweightMC::Storage::WorldStorage &worldStorage,
                       LightweightMC::Storage::Database &database,
                       LightweightMC::Network::NetworkManager &net);

    void handleBlockPlacement(int clientFd,
                              LightweightMC::Player::PlayerSession &session,
                              const uint8_t *data,
                              size_t &offset,
                              size_t packetEnd,
                              LightweightMC::Storage::WorldStorage &worldStorage,
                              LightweightMC::Storage::Database &database,
                              LightweightMC::Network::NetworkManager &net);

    // 0x12 UpdateSign — C2S packet: long location, four strings (up to 15
    // chars each). Sent when the client edits a sign in world. The server
    // currently has no sign-text model, so we consume and log only; the
    // location is decoded for admin traceability so future patches can
    // persist the strings via WorldStorage without touching the wire path.
    void handleUpdateSign(int clientFd,
                          LightweightMC::Player::PlayerSession &session,
                          const uint8_t *data,
                          size_t &offset,
                          size_t packetEnd,
                          LightweightMC::Storage::WorldStorage &worldStorage);

} // namespace WorldHandler