#include "network/handlers/LoginHandler.hpp"
#include "managers/PlayerManager.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "world/EntityManager.hpp"

namespace LoginHandler
{

    void handle(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                int32_t packetId, ClientMap &clients, LightweightMC::World::WorldManager &worldManager,
                LightweightMC::Storage::Database &database, LightweightMC::Network::NetworkManager &net)
    {
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