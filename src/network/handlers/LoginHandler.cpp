#include "network/handlers/LoginHandler.hpp"
#include "managers/PlayerManager.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "world/EntityManager.hpp"
#include "network/Authentication.hpp"
#include "network/AesCfb.hpp"
#include "network/protocol/ProtocolVersion.hpp"

namespace LoginHandler
{

    // Vanilla 1.8 login sequence has two C2S packet shapes:
    //   0x00 Login Start          -- plain username string (offline path)
    //   0x01 Login (Access Token) -- byte tokenLength + tokenLength ASCII bytes
    // Server replies 0x02 LoginSuccess (uuid + username), 0x03 CannotConnect,
    // or 0x04 InvalidSessionToken. Community relays (auth-mode = token) send
    // the 0x01 packet BEFORE 0x00 LoginStart; offline-mode clients skip it
    // entirely. When a relay returns a session_key we arm an AES-128 CFB
    // cipher for the session, but only for MODERN/LATEST eras -- LEGACY 1.8
    // never encrypts the wire post-login and doing so would corrupt the
    // byte-aligned client stream.
    void handle(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                int32_t packetId, ClientMap &clients, LightweightMC::World::WorldManager &worldManager,
                LightweightMC::Storage::Database &database, LightweightMC::Network::NetworkManager &net)
    {
        if (packetId == 0x01) // Login (Access Token) C2S
        {
            const int32_t tokenLength = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
            std::string token;
            if (tokenLength > 0 &&
                offset + static_cast<std::size_t>(tokenLength) <= packetEnd)
            {
                token.assign(reinterpret_cast<const char *>(data + offset),
                             static_cast<std::size_t>(tokenLength));
                offset += static_cast<std::size_t>(tokenLength);
            }
            LightweightMC::Core::Logger::info("Login (Access Token) received for client fd=" +
                                              std::to_string(clientFd) + " tokenBytes=" + std::to_string(token.size()));

            std::string username, uuid, sessionKey;
            const bool ok = LightweightMC::Network::Auth::verifyToken(token, username, uuid, sessionKey);

            if (ok)
            {
                session.username = username;
                session.uuid = uuid;
                session.authenticated = true;
                session.authPending = false;

                if (!sessionKey.empty() && session.adapter != nullptr &&
                    session.adapter->era() != LightweightMC::Network::Protocol::Era::LEGACY)
                {
                    if (auto *c = LightweightMC::Network::Auth::lastCipher())
                    {
                        session.cipherS2C = c;
                        session.cipherC2S = c;
                    }
                }

                std::vector<uint8_t> loginSuccess;
                LightweightMC::Network::Packet::writeString(loginSuccess, uuid);
                LightweightMC::Network::Packet::writeString(loginSuccess, username);
                net.sendPacket(clientFd, 0x02, loginSuccess);
                return;
            }

            LightweightMC::Core::Logger::warn("Login (Access Token) verify chain failed for fd=" +
                                              std::to_string(clientFd));
            std::vector<uint8_t> cannot;
            LightweightMC::Network::Packet::writeShort(cannot, 2);
            net.sendPacket(clientFd, 0x03, cannot);
            return;
        }

        if (packetId == 0x00) // Login Start
        {
            session.username = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
            LightweightMC::Core::Logger::info("Login Start received for: " + session.username);

            std::string playerUuid = LightweightMC::Player::PlayerManager::getPlayerUuid(session.username);

            std::vector<uint8_t> loginSuccess;
            LightweightMC::Network::Packet::writeString(loginSuccess, playerUuid);
            LightweightMC::Network::Packet::writeString(loginSuccess, session.username);
            net.sendPacket(clientFd, 0x02, loginSuccess);

            auto packetSender = [&net](int f, int32_t p, const std::vector<uint8_t> &b)
            {
                net.sendPacket(f, p, b);
            };
            auto broadcastSender = [&net](int32_t p, const std::vector<uint8_t> &b, int i)
            {
                net.broadcastPacket(p, b, i);
            };

            LightweightMC::Player::PlayerManager::getInstance().sendPlayPackets(
                clientFd, session, session.username, clients, worldManager, database, packetSender, broadcastSender);

            // Seed a default mob/villager roster around the newly-connected
            // player. EntityManager honours spawn_mobs / spawn_monsters /
            // spawn_patrols / spawn_phantoms / spawn_wandering_traders /
            // spawn_wardens and max_entity_cramming from GameRules; see
            // EntityManager.cpp for the per-template stat table and the
            // tick dispatch. Terrain-aware overload also enforces the
            // vanilla "solid below + 2 air above" spawn predicate on the
            // generated world so a mob never gets buried inside a hill or
            // a player-built wall. Emission of S2C packets is opt-in via
            // EntityManager::setNetworkSync(true) so a build that must stay
            // byte-aligned with vanilla 1.8 clients can keep it OFF;
            // NetworkManager::start currently enables it so the client can
            // actually see the mobs during AI testing.
            LightweightMC::World::EntityManager::getInstance().spawnForSession(
                session, worldManager.getWorldStorageFor(session.worldId), clientFd, packetSender);
        }
    }

} // namespace LoginHandler