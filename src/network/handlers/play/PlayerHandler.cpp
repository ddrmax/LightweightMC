#include "network/handlers/play/PlayerHandler.hpp"
#include "core/CommandManager.hpp"
#include "managers/ScoreboardManager.hpp"
#include "managers/PlayerManager.hpp"
#include "network/AntiScan.hpp"
#include "core/Logger.hpp"
#include "core/ConfigManager.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>
#ifndef MC_PROTOCOL_VERSION_NAME
#define MC_PROTOCOL_VERSION_NAME "1.8"
#endif
namespace PlayerHandler
{

    void handleKeepAlive(int clientFd, const uint8_t *data, size_t &offset, size_t packetEnd, LightweightMC::Network::NetworkManager &net)
    {
        int32_t id = LightweightMC::Network::Packet::readVarIntBounded(data, offset, packetEnd, 1);
        std::vector<uint8_t> payload;
        LightweightMC::Network::Packet::writeVarInt(payload, id);
        net.sendPacket(clientFd, 0x00, payload);
    }

    void handleChatMessage(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                           ClientMap &clients, LightweightMC::Network::NetworkManager &net)
    {
        std::string message = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        if (message.empty())
            return;

        if (message[0] == '/')
        {
            // In-game chat commands route through the very same CommandManager
            // that the console uses. executeCommand strips the leading slash
            // itself, so callers pass the raw chat string. reply() dispatches
            // through replyCallback (below) which sends an S2C 0x02 chat packet
            // to this client only. Broadcast-to-all is a console-only feature
            // gated by Command::affectsPlayerState + CommandContext::broadcastCallback.
            CommandContext ctx;
            ctx.sender = session.username;
            ctx.isConsole = false;
            ctx.opLevel = static_cast<int>(session.opLevel);

            ctx.replyCallback = [&net, clientFd](const std::string &msg)
            {
                std::vector<uint8_t> chatMsg;
                LightweightMC::Network::Packet::writeString(chatMsg, R"({"text":")" + msg + R"("})");
                chatMsg.push_back(1);
                net.sendPacket(clientFd, 0x02, chatMsg);
            };

            ctx.setOpLevel = [&net](const std::string &name, int level) -> bool
            {
                uint8_t clamped = static_cast<uint8_t>(std::max(0, std::min(4, level)));
                return net.setPlayerOpLevel(name, clamped);
            };

            ctx.setGameMode = [&net](const std::string &name, int level) -> bool
            {
                uint8_t clamped = static_cast<uint8_t>(std::max(0, std::min(3, level)));
                return net.setPlayerGameMode(name, clamped);
            };

            // Chat /hud /stats: push the scoreboard + tablist packets to
            // this specific client (matches the legacy in-place fallback
            // that used to live in this handler).
            ctx.sendHud = [&net, clientFd, &clients]()
            {
                auto packetSender = [&net](int f, int32_t p, const std::vector<uint8_t> &b)
                { net.sendPacket(f, p, b); };
                LightweightMC::Managers::ScoreboardManager::getInstance().sendScoreboard(clientFd, clients, packetSender);
                LightweightMC::Managers::ScoreboardManager::getInstance().sendTablistHeaderFooter(clientFd, clients.size(), packetSender);
            };

            // Chat /tp: teleport an online player to absolute coords or
            // to another player's current world position. Delegates to
            // NetworkManager's teleport helpers which look up the live
            // session, mutate x/y/z, and push the S2C Position packet
            // (1.8 id 0x08) to the moved client.
            ctx.teleportPlayerToCoords = [&net](const std::string &name, double x, double y, double z) -> bool
            {
                return net.teleportToCoords(name, x, y, z);
            };
            ctx.teleportPlayerToTarget = [&net](const std::string &mover, const std::string &target) -> bool
            {
                return net.teleportToPlayer(mover, target);
            };

            // Chat /gotor and /world: dimension-plane management via the
            // same NetworkManager hooks the console wires.
            ctx.switchPlayerWorld = [&net](const std::string &playerName, const std::string &worldName) -> bool
            {
                const int32_t id = net.lookupWorldId(worldName);
                if (id == -2147483648)
                    return false;
                return net.setPlayerWorldId(playerName, id);
            };
            // 0.0.15 phase 4: widen the in-game admin chat closure to match
            // CommandContext::createWorld's new five-argument shape so a
            // `/world create foo flat --inventory separate --force-gamemode
            // creative` line sent in-game reaches the same DB setters +
            // WorldManager mirrors that the console-thread closures in
            // Server.cpp wire. WorldCommands.hpp always supplies all five
            // arguments, so the closure just forwards them.
            ctx.createWorld = [&net](const std::string &worldName, const std::string &style,
                                     int32_t inventoryPolicy, int32_t forceGameMode, int64_t seed) -> int32_t
            {
                return net.createWorld(worldName, style, inventoryPolicy, forceGameMode, seed);
            };
            ctx.listWorlds = [&net]() -> std::vector<std::string>
            {
                return net.listWorldNames();
            };
            ctx.describeWorld = [&net](const std::string &worldName, int32_t &outId, std::string &outStyle, std::string &outDbPath) -> bool
            {
                return net.describeWorld(worldName, outId, outStyle, outDbPath);
            };

            // 0.0.16 world-control hooks: /setworldspawn, /seed, /setblock,
            // /place and /spawnpoint issued from the in-game chat. /setblock
            // and /place write on the caller's current plane (PlayerSession:
            //:worldId) so a chat command from a Nether-logged player edits
            // Nether rather than the Overworld.
            ctx.senderWorldId = session.worldId;
            ctx.setWorldSpawn = [&net](const std::string &worldName, double x, double y, double z) -> bool
            {
                return net.setWorldSpawnByName(worldName, x, y, z);
            };
            ctx.getWorldSpawn = [&net](const std::string &worldName, double &x, double &y, double &z) -> bool
            {
                return net.getWorldSpawnByName(worldName, x, y, z);
            };
            ctx.getWorldSeed = [&net](const std::string &worldName) -> int64_t
            {
                return net.getWorldSeedByName(worldName);
            };
            ctx.setWorldSeed = [&net](const std::string &worldName, int64_t seed) -> bool
            {
                return net.setWorldSeedByName(worldName, seed);
            };
            ctx.setBlockAtPlane = [&net](int32_t planeId, int wx, int wy, int wz, uint16_t combined) -> bool
            {
                return net.setBlockAtPlane(planeId, wx, wy, wz, combined);
            };
            ctx.setPlayerSpawn = [&net](const std::string &name, double x, double y, double z) -> bool
            {
                return net.updatePlayerSpawnByName(name, x, y, z);
            };

            // Phase 6 gameplay backends -- the /damage, /kill and /experience
            // chat stubs used to reply "Damage inflicted."/"Entities killed."/"
            // Experience updated." without touching any state; wire them to
            // NetworkGameplay.cpp's NetworkManager entry points so HP/XP
            // mutations actually reach the target session and every non-fatal
            // damage step emits an UpdateHealth (0x06), kills emit
            // DeathCombatEvent (0x42), and XP grants emit Experience (0x1f).
            // /respawn is wired too even though it wasn't registered yet --
            // harmless and future-proof so admins can force a respawn via chat.
            ctx.applyDamageToPlayer = [&net](const std::string &name, float amount, int damageTypeCode) -> bool
            {
                return net.applyDamageToPlayer(name, amount, damageTypeCode);
            };
            ctx.killPlayer = [&net](const std::string &name) -> bool
            {
                return net.killPlayer(name, 0);
            };
            ctx.respawnPlayer = [&net](const std::string &name) -> bool
            {
                return net.respawnPlayer(name);
            };
            ctx.grantXpToPlayer = [&net](const std::string &name, int32_t amount) -> int
            {
                return net.grantPlayerXp(name, amount);
            };

            // Phase 8 world-model backends -- /import-world and /export-world
            // consult NetworkManager::m_worldModel (boot-selected via the
            // `[world] world-model` key) to decide which file format to
            // speak. `formatHint` lets the caller pin sqlite/anvil/legacy
            // independent of the boot default.
            ctx.importWorldFromPath = [&net](const std::string &path, const std::string &planeName, const std::string &formatHint) -> bool
            {
                return net.importWorldFromPath(path, planeName, formatHint);
            };
            ctx.exportWorldToPath = [&net](const std::string &path, const std::string &planeName, const std::string &formatHint) -> bool
            {
                return net.exportWorldToPath(path, planeName, formatHint);
            };
            ctx.worldModelName = [&net]() -> std::string
            {
                return net.worldModelName();
            };

            CommandManager::getInstance().executeCommand(ctx, message);
            return;
        }
        else
        {
            std::string formattedChat = "<" + session.username + "> " + message;
            std::vector<uint8_t> chatMsg;
            LightweightMC::Network::Packet::writeString(chatMsg, R"({"text":")" + formattedChat + R"("})");
            chatMsg.push_back(0);

            for (const auto &[fd, s] : clients)
            {
                if (s.state == ClientState::PLAY)
                    net.sendPacket(fd, 0x02, chatMsg);
            }

            LightweightMC::Core::Logger::info("[CHAT] " + formattedChat);
        }
    }

    /*void handlePlayerMovement(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd,
                              LightweightMC::World::WorldManager &worldManager, LightweightMC::Network::NetworkManager &net)
    {
        session.x = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);
        session.y = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);
        session.z = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);

        std::vector<uint8_t> teleport;
        LightweightMC::Network::Packet::writeVarInt(teleport, clientFd);
        LightweightMC::Network::Packet::writeInt(teleport, static_cast<int32_t>(session.x * 32.0));
        LightweightMC::Network::Packet::writeInt(teleport, static_cast<int32_t>(session.y * 32.0));
        LightweightMC::Network::Packet::writeInt(teleport, static_cast<int32_t>(session.z * 32.0));
        teleport.push_back(0); // Yaw
        teleport.push_back(0); // Pitch
        teleport.push_back(1); // On Ground

        int pChunkX = static_cast<int>(std::floor(session.x / 16.0));
        int pChunkZ = static_cast<int>(std::floor(session.z / 16.0));

        if (pChunkX != session.currentChunkX || pChunkZ != session.currentChunkZ)
        {
            auto packetSender = [&net](int f, int32_t p, const std::vector<uint8_t> &b)
            { net.sendPacket(f, p, b); };
            LightweightMC::Core::Logger::info("Player Position for UUID (" + session.username + ") at (" +
                                              std::to_string(session.x) + ", " + std::to_string(session.y) + ", " + std::to_string(session.z) + ") with ");
            worldManager.updateChunksAroundPlayer(clientFd, session, 5, packetSender);
        }

        net.broadcastPacket(0x18, teleport, clientFd);
    }*/
    void handlePlayerMovement(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd, int32_t packetId,
                              LightweightMC::World::WorldManager &worldManager, LightweightMC::Network::NetworkManager &net)
    {
        bool positionChanged = false;

        if (packetId == 0x04 || packetId == 0x06) // Position OR Position+Look
        {
            session.x = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);
            session.y = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);
            session.z = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);
            positionChanged = true;
        }

        if (packetId == 0x05 || packetId == 0x06) // Look OR Position+Look
        {
            session.yaw = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);
            session.pitch = LightweightMC::Network::Packet::readDoubleBounded(data, offset, packetEnd);
        }

        if (packetId == 0x03 || packetId == 0x04 || packetId == 0x05 || packetId == 0x06)
        {
            session.onGround = (LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd) != 0);
        }

        // 0.0.15 Aether void-fall hook: if the player has dropped below the
        // Aether plane's y=-16 sky-void (islands live at y=30..100 with a
        // floating-island underside; anything under -16 has escaped the
        // island chain and would otherwise fall through to the Overworld
        // surface far below), teleport them back to the Overworld spawn.
        //
        // Critically, NetworkManager::setPlayerWorldAt persists the DEPARTING
        // plane's session coordinates into player_positions(uuid, oldWorldId)
        // before loading the arrival plane's row, so if we teleported with
        // session.y still sitting at y<=-16 that fall position becomes the
        // authoritative Aether row and the very next /gotor aether restores
        // the player to y<=-16 -- which re-triggers this hook and locks them
        // into a teleport loop. So we snap the session to Aether's world
        // spawn (WorldManager's setWorldSpawn cache, or the plane's
        // findStandingFeetY on first visit -- which is what setPlayerWorldAt
        // itself uses for the "no saved row" arrival path) before invoking
        // setPlayerWorldAt, guaranteeing the departure-plane save records a
        // safe y. Non-Aether planes keep vanilla gravity semantics -- the
        // client drives y on its side, so we simply log a diagnostic and let
        // the existing broadcastPacket(0x18) below carry the low y back to
        // the client.
        if (positionChanged && session.y <= -16.0)
        {
            const int32_t aetherPlaneId =
                ConfigManager::getInstance().get<int32_t>("world", "aether-plane-id", 100);
            if (session.worldId == aetherPlaneId)
            {
                // Resolve the safe Aether respawn point. Prefer an explicit
                // WorldManager::setWorldSpawn entry (a fork / admin tool may
                // have pinned Aether's spawn on the world_registry column);
                // otherwise fall back to the plane's standing-feet scan at
                // (0,0) so the player lands on whatever island sits beneath
                // the plane's nominal origin.
                double spawnX = 0.5, spawnY = 64.0, spawnZ = 0.5;
                if (!worldManager.getWorldSpawn(aetherPlaneId, spawnX, spawnY, spawnZ))
                {
                    auto &aetherStorage = worldManager.getWorldStorageFor(aetherPlaneId);
                    const int feetY = aetherStorage.findStandingFeetY(0, 0, 100);
                    spawnX = 0.5;
                    spawnY = (feetY >= 0) ? static_cast<double>(feetY) + 1.0 : 64.0;
                    spawnZ = 0.5;
                }
                // Re-anchor the session before the plane switch so the
                // Aether-departure save records the safe spawn. Without this
                // line the fall position (y<=-16) is written into
                // player_positions(uuid, aetherPlaneId) and the next /gotor
                // restores the player into the void -- reproducing this hook
                // on every re-entry.
                session.x = spawnX;
                session.y = spawnY;
                session.z = spawnZ;
                session.yaw = 0.0;
                session.pitch = 0.0;
                LightweightMC::Core::Logger::info("[AetherVoid] " + session.username +
                                                  " fell below y=-16 on plane id=" +
                                                  std::to_string(aetherPlaneId) +
                                                  " (Aether); re-anchored session to plane spawn (" +
                                                  std::to_string(spawnX) + ", " +
                                                  std::to_string(spawnY) + ", " +
                                                  std::to_string(spawnZ) +
                                                  ") before routing to Overworld via setPlayerWorldId(0)");
                // setPlayerWorldAt(hasExplicitPosition=false) now writes the
                // safe Aether spawn into the departure row, then loads the
                // player's saved Overworld row (or the Overworld default
                // spawn) and updates session.worldId / x / y / z / yaw /
                // pitch accordingly. No additional manual session mutation
                // is needed after this call.
                net.setPlayerWorldId(session.username, 0);
                session.onGround = false;
            }
            else
            {
                LightweightMC::Core::Logger::info("[Fall] " + session.username +
                                                  " below y=-16 on plane id=" +
                                                  std::to_string(session.worldId) +
                                                  "; letting vanilla gravity handle the descent");
            }
        }

        // Mise à jour des chunks SEULEMENT si la position X/Z a réellement changé
        if (positionChanged)
        {
            int pChunkX = static_cast<int>(std::floor(session.x / 16.0));
            int pChunkZ = static_cast<int>(std::floor(session.z / 16.0));

            if (pChunkX != session.currentChunkX || pChunkZ != session.currentChunkZ)
            {
                auto packetSender = [&net](int f, int32_t p, const std::vector<uint8_t> &b)
                {
                    net.sendPacket(f, p, b);
                };

                LightweightMC::Core::Logger::info("Player " + session.username + " moved to chunk (" +
                                                  std::to_string(pChunkX) + ", " + std::to_string(pChunkZ) + ")");

                worldManager.updateChunksAroundPlayer(clientFd, session, 5, packetSender);
            }
        }

        // Broadcast aux autres joueurs
        std::vector<uint8_t> teleport;
        LightweightMC::Network::Packet::writeVarInt(teleport, clientFd);
        LightweightMC::Network::Packet::writeInt(teleport, static_cast<int32_t>(session.x * 32.0));
        LightweightMC::Network::Packet::writeInt(teleport, static_cast<int32_t>(session.y * 32.0));
        LightweightMC::Network::Packet::writeInt(teleport, static_cast<int32_t>(session.z * 32.0));
        teleport.push_back(static_cast<uint8_t>(session.yaw * 256.0f / 360.0f));
        teleport.push_back(static_cast<uint8_t>(session.pitch * 256.0f / 360.0f));
        teleport.push_back(session.onGround ? 1 : 0);

        net.broadcastPacket(0x18, teleport, clientFd);
    }
    void handleAnimation(int clientFd, LightweightMC::Network::NetworkManager &net)
    {
        std::vector<uint8_t> animPacket;
        LightweightMC::Network::Packet::writeVarInt(animPacket, clientFd);
        animPacket.push_back(0);
        net.broadcastPacket(0x0B, animPacket, clientFd);
    }

    // ---------------------------------------------------------------------
    // Remaining MC 1.8 (protocol 47) C2S packet handlers.
    //
    // Each handler MUST consume the full wire payload (see protocol_types
    // Serverbound::*_hex deserialize methods) so the client's byte stream
    // stays aligned. Anything less causes a CorruptedFrameException on
    // the client side, which historically manifested as the whole session
    // desyncing after the first unhandled packet type.
    // ---------------------------------------------------------------------

    void handleFlying(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        // Wire: one byte — "player is on ground" snapshot. The Position
        // packet (0x04) already carries an authoritative onGround field,
        // so we only use Flying to nudge flight-mode bookkeeping when
        // the server eventually implements creative/spectator flight.
        const int32_t onGround = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        session.onGround = (onGround != 0);
        LightweightMC::Core::Logger::debug(std::string("[PLAYER 0x03 Flying] fd=") + std::to_string(clientFd) +
                                           " onGround=" + std::to_string(onGround) +
                                           " prevFly=" + std::string(session.flying ? "1" : "0"));
        (void)clientFd;
    }

    void handleClientAbilities(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        // Wire: byte flags  float flyingSpeed  float walkingSpeed.
        const int32_t flags = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        // Bit 1 (0x01) = can fly, per minecraft.wiki Protocol_47.
        session.flying = ((flags & 0x01) != 0);
        // Speeds are floats (big-endian 4 bytes) — we do not override them
        // but we MUST consume both to keep stream aligned. 8 raw bytes.
        for (int i = 0; i < 8 && offset < packetEnd; ++i)
            ++offset;
        LightweightMC::Core::Logger::debug(std::string("[PLAYER ClientAbilities C2S] fd=") + std::to_string(clientFd) +
                                           " flags=0x" + [&]
                                           { std::ostringstream o; o << std::hex << flags; return o.str(); }() +
                                           " flying=" + std::string(session.flying ? "1" : "0"));
        (void)clientFd;
    }

    void handleTabComplete(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        const std::string text = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        LightweightMC::Core::Logger::info("[Tab] " + session.username + " autocompleted \"" + text + "\"");
        (void)clientFd;
    }

    void handleClientSettings(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        // Wire: string locale, byte viewDistance, byte chatFlags,
        // byte chatColors, byte difficulty, byte showCape.
        const std::string locale = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        const int32_t viewDistance = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd); // chatFlags
        (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd); // chatColors
        const int32_t difficulty = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd); // showCape

        LightweightMC::Core::Logger::info("[Settings] " + session.username +
                                          " locale=" + locale +
                                          " viewDistance=" + std::to_string(viewDistance) +
                                          " difficulty=" + std::to_string(difficulty));
        (void)clientFd;
    }

    void handleClientCommand(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        // Wire: byte commandId, then int32 parameter (4 raw bytes).
        const int32_t commandId = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        for (int i = 0; i < 4 && offset < packetEnd; ++i)
            ++offset;

        LightweightMC::Core::Logger::info("[ClientCommand] " + session.username +
                                          " id=" + std::to_string(commandId));
        (void)clientFd;
    }

    void handleCustomPayload(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        // Wire: string channel, then an opaque byte blob up to packetEnd.
        const std::string channel = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        if (offset < packetEnd)
            offset = packetEnd;

        LightweightMC::Core::Logger::info("[CustomPayload] " + session.username +
                                          " channel=\"" + channel + "\"");
        (void)clientFd;
    }

    void handleSpectate(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        // Wire: varint target entity id.
        const int32_t target = LightweightMC::Network::Packet::readVarIntBounded(data, offset, packetEnd, 1);
        LightweightMC::Core::Logger::info("[Spectate] " + session.username + " target=" + std::to_string(target));
        (void)clientFd;
    }

    void handleResourcePackReceive(int clientFd, LightweightMC::Player::PlayerSession &session, const uint8_t *data, size_t &offset, size_t packetEnd)
    {
        // Wire: string status ("OK" / "CANCELED" / error text).
        const std::string status = LightweightMC::Network::Packet::readStringBounded(data, offset, packetEnd);
        LightweightMC::Core::Logger::info("[ResourcePack] " + session.username +
                                          " status=" + status);
        (void)clientFd;
    }

} // namespace PlayerHandler