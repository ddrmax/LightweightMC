#pragma once

#include <cstdint>
#include <cstddef>
#include "player/PlayerSession.hpp"
#include "core/utils.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include "network/protocol/BaseProtocol.hpp"
namespace HandshakeHandler
{

    void handle(int clientFd,
                LightweightMC::Player::PlayerSession &session,
                const uint8_t *data,
                size_t &offset,
                size_t packetEnd,
                int32_t packetId);

} // namespace HandshakeHandler