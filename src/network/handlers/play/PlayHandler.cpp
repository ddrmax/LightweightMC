#include "network/handlers/play/PlayHandler.hpp"
#include "network/handlers/play/PlayerHandler.hpp"
#include "network/handlers/play/WorldHandler.hpp"
#include "network/handlers/play/InventoryHandler.hpp"
#include "network/handlers/play/EntityHandler.hpp"

#include <cstring>
#include <vector>
#include <cstdint>
#include <sstream>
#include <cstdio>

namespace PlayHandler
{

    void handle(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                int32_t packetId, ClientMap &clients, LightweightMC::World::WorldManager &worldManager, LightweightMC::Storage::WorldStorage &worldStorage, LightweightMC::Storage::Database &database, LightweightMC::Network::NetworkManager &net)
    {
        switch (packetId)
        {
        case 0x00: // Keep Alive
            PlayerHandler::handleKeepAlive(clientFd, data, offset, packetEnd, net);
            break;
        case 0x01: // Chat Message
            PlayerHandler::handleChatMessage(clientFd, session, data, offset, packetEnd, clients, net);
            break;
        case 0x02: // Use Entity (attack / interact)
            EntityHandler::handleUseEntity(clientFd, session, data, offset, packetEnd, clients);
            break;
        case 0x03: // Flying (airborne snapshot)
            LightweightMC::Core::Logger::debug(std::string("[PLAY 0x03] C2S Flying frame fd=") +
                                               std::to_string(clientFd) + " bodyBytes=" +
                                               std::to_string(packetEnd));
            PlayerHandler::handleFlying(clientFd, session, data, offset, packetEnd);
            break;
        case 0x04: // Player Position
        case 0x05: // Player Look
        case 0x06: // Player Position & Look
            PlayerHandler::handlePlayerMovement(clientFd, session, data, offset, packetEnd, packetId, worldManager, net);
            break;
        case 0x07: // Player Digging
            WorldHandler::handleDigging(clientFd, session, data, offset, packetEnd, worldStorage, database, net);
            break;
        case 0x08: // Block Placement / Container Interaction
            WorldHandler::handleBlockPlacement(clientFd, session, data, offset, packetEnd, worldStorage, database, net);
            break;
        case 0x09: // Held Item Change
            InventoryHandler::handleHeldItemChange(clientFd, session, data, offset, packetEnd, net);
            break;
        case 0x0A: // Animation (Arm Swing)
            PlayerHandler::handleAnimation(clientFd, net, session);
            break;
        case 0x0B: // Entity Action (start/stop following)
            EntityHandler::handleEntityAction(clientFd, session, data, offset, packetEnd, clients);
            break;
        case 0x0C: // Steer Vehicle
            EntityHandler::handleSteerVehicle(clientFd, session, data, offset, packetEnd);
            break;
        case 0x0D: // Close Window
            InventoryHandler::handleCloseWindow(clientFd, session, data, offset, packetEnd, database);
            break;
        case 0x0E: // Click Window
            InventoryHandler::handleClickWindow(clientFd, session, data, offset, packetEnd, database, net);
            break;
        case 0x0F: // Transaction (client ACK)
            InventoryHandler::handleTransactionAck(clientFd, session, data, offset, packetEnd);
            break;
        case 0x10: // Creative Inventory Action
            InventoryHandler::handleCreativeAction(clientFd, session, data, offset, packetEnd, net);
            break;
        case 0x11: // Enchant Item
            InventoryHandler::handleEnchantItem(clientFd, session, data, offset, packetEnd);
            break;
        case 0x12: // Update Sign
            WorldHandler::handleUpdateSign(clientFd, session, data, offset, packetEnd, worldStorage);
            break;
        case 0x13: // Client Abilities
        {
            // Read the wire (byte flags + 2 float speeds) here rather than
            // delegating to PlayerHandler so we can immediately echo the
            // flags back via S2C PLAYER_ABILITIES. On the 1.8 ClientboundId
            // table that slot is 0x39 (Abilities), NOT 0x33 (UpdateSign).
            // Sending (flags,float,float) into the UpdateSign slot made
            // the client parse garbage and desync its stream -- that was
            // the creative-fly crash. Era translation in sendPacket maps
            // this semantic 0x39 to the correct per-era wire ID.
            uint8_t flags = 0;
            if (offset < packetEnd)
                flags = data[offset++];
            for (int i = 0; i < 8 && offset < packetEnd; ++i)
                ++offset; // Two big-endian floats (flying speed, walking speed) we do not override.
            session.flying = ((flags & 0x01) != 0);

            LightweightMC::Core::Logger::debug(std::string("[PLAY 0x13] C2S ClientAbilities fd=") +
                                               std::to_string(clientFd) +
                                               " flags=0x" + [] (uint8_t v) { std::ostringstream o; o << std::hex << (int)v; return o.str(); }(flags) +
                                               " flying=" + std::string(session.flying ? "1" : "0"));

            // 1.0-style default speeds (fly=1.0, walk=1.0). Encoded
            // big-endian so no host-byte-order assumptions.
            std::vector<uint8_t> echo;
            echo.push_back(flags);
            const auto pushFloatBE = [](std::vector<uint8_t> &v, float x)
            {
                uint32_t raw;
                std::memcpy(&raw, &x, sizeof(raw));
                v.push_back(static_cast<uint8_t>((raw >> 24) & 0xFF));
                v.push_back(static_cast<uint8_t>((raw >> 16) & 0xFF));
                v.push_back(static_cast<uint8_t>((raw >> 8)  & 0xFF));
                v.push_back(static_cast<uint8_t>(raw & 0xFF));
            };
            pushFloatBE(echo, 1.0f);
            pushFloatBE(echo, 1.0f);

            LightweightMC::Core::Logger::debug(std::string("[PLAY 0x13] S2C echo id=0x39 (Abilities) fd=") +
                                               std::to_string(clientFd) +
                                               " payloadHex=" + [&]{ std::ostringstream o;
                                                                     for (size_t i = 0; i < echo.size(); ++i) {
                                                                         char buf[3];
                                                                         std::snprintf(buf, sizeof(buf), "%02x", echo[i]);
                                                                         o << buf << ' ';
                                                                     }
                                                                     return o.str(); }());
            net.sendPacket(clientFd, 0x39, echo);
            (void)session;
            break;
        }
        case 0x14: // Tab Complete
            PlayerHandler::handleTabComplete(clientFd, session, data, offset, packetEnd);
            break;
        case 0x15: // Client Settings
            PlayerHandler::handleClientSettings(clientFd, session, data, offset, packetEnd);
            break;
        case 0x16: // Client Command
            PlayerHandler::handleClientCommand(clientFd, session, data, offset, packetEnd, net);
            break;
        case 0x17: // Custom Payload
            PlayerHandler::handleCustomPayload(clientFd, session, data, offset, packetEnd);
            break;
        case 0x18: // Spectate
            PlayerHandler::handleSpectate(clientFd, session, data, offset, packetEnd);
            break;
        case 0x19: // Resource Pack Receive
            PlayerHandler::handleResourcePackReceive(clientFd, session, data, offset, packetEnd);
            break;
        default:
            // Unknown C2S ID for protocol 47 — surface in the log but do
            // not try to consume an arbitrary body length (the framing
            // already isolated the packet via packetEnd, so skipping is
            // safe).
            LightweightMC::Core::Logger::info("[NET] Unknown C2S packet id=" +
                                              std::to_string(packetId) + " from " + session.username);
            break;
        }
    }

} // namespace PlayHandler