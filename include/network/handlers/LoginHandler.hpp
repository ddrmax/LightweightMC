#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include "network/NetworkManager.hpp"
#include "managers/WorldManager.hpp"
#include "player/PlayerSession.hpp"
#include "core/utils.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "network/protocol/EraAdapter.hpp"
#include "network/protocol/ProtocolVersion.hpp"
#include "network/protocol/BaseProtocol.hpp"
#include "network/Authentication.hpp"

namespace LoginHandler
{

    using ClientMap = std::unordered_map<int, LightweightMC::Player::PlayerSession>;

    void handle(int clientFd,
                LightweightMC::Player::PlayerSession &session,
                const uint8_t *data,
                size_t &offset,
                size_t packetEnd,
                int32_t packetId,
                ClientMap &clients,
                LightweightMC::World::WorldManager &worldManager,
                LightweightMC::Storage::Database &database,
                LightweightMC::Network::NetworkManager &net);

} // namespace LoginHandler