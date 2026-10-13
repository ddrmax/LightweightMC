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

namespace EntityHandler
{

    using ClientMap = std::unordered_map<int, LightweightMC::Player::PlayerSession>;

    // 0x02 UseEntity — C2S packet:
    //   VarInt targetEntityId
    //   byte   action          (0 = attack, 1 = main use, 2 = secondary use)
    //   [3 bytes x,y,z]        only when action == 0 (attack): byte-quantised
    //                          attack-vector components, not doubles.
    // For now the server logs the interaction and drops it (the server-side
    // entity/mob model is still on the TODO list), so no S2C response is
    // emitted. Consuming the payload is still mandatory to keep the client
    // stream aligned.
    void handleUseEntity(int clientFd,
                         LightweightMC::Player::PlayerSession &session,
                         const uint8_t *data,
                         size_t &offset,
                         size_t packetEnd,
                         ClientMap &clients);

    // 0x0B EntityAction — C2S packet:
    //   VarInt targetEntityId
    //   byte   action          (0 = start following, 1 = stop following)
    //   [byte newYaw, byte newPitch]  only when action == 0
    void handleEntityAction(int clientFd,
                            LightweightMC::Player::PlayerSession &session,
                            const uint8_t *data,
                            size_t &offset,
                            size_t packetEnd,
                            ClientMap &clients);

    // 0x0C SteerVehicle — C2S packet:
    //   byte forward
    //   byte strafe
    //   byte standUp
    //   byte deselect          (1.7.3+ / protocol 47)
    void handleSteerVehicle(int clientFd,
                            LightweightMC::Player::PlayerSession &session,
                            const uint8_t *data,
                            size_t &offset,
                            size_t packetEnd);

} // namespace EntityHandler
