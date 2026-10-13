// Player-state setters that live on the NetworkManager singleton because
// they need to touch both m_clients (the live sessions) and m_database
// (the persisted rows). Kept separate from the framing / socket / events
// files so each concern stays small.
//
// See src/network/NetworkManager.cpp for the class-level invariant notes.

#include "network/NetworkManager.hpp"
#include "network/Packet.hpp"
#include "core/Logger.hpp"
#include "managers/PlayerManager.hpp"
#include "storage/Database.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace LightweightMC::Network
{

    // Update a player's op level: mutate the live session if the player is
    // currently online, and persist the change so a re-login keeps it.
    // Returns true if the player was found online OR the DB write succeeded.
    bool NetworkManager::setPlayerOpLevel(const std::string &playerName, uint8_t opLevel)
    {
        bool foundOnline = false;
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state == ClientState::PLAY && ses.username == playerName)
            {
                ses.opLevel = opLevel;
                foundOnline = true;
                break;
            }
        }

        // Persist the op status so it survives a re-login.
        Storage::PlayerData data;
        data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        data.username = playerName;
        data.opLevel = opLevel;
        const bool persisted = m_database.savePlayerData(data);

        return foundOnline || persisted;
    }

    // Update a player's game mode (0=survival, 1=creative, 2=adventure,
    // 3=spectator). Loads the existing DB row, mutates only the gamemode
    // column, saves it back, updates the live session when the player is
    // currently online, and forwards a Tab List "Update Player" packet
    // (0x38) to that client so the change is reflected in-game without
    // requiring a reconnect.
    bool NetworkManager::setPlayerGameMode(const std::string &playerName, uint8_t mode)
    {
        if (mode > 3)
            return false;

        bool foundOnline = false;
        int liveFd = -1;
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state == ClientState::PLAY && ses.username == playerName)
            {
                ses.gamemode = mode;
                foundOnline = true;
                liveFd = fd;
                break;
            }
        }

        Storage::PlayerData data;
        data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        data.username = playerName;
        data.gamemode = mode;
        const bool persisted = m_database.savePlayerData(data);

        // Tell the client about the mode change with a Tab List Update
        // Player packet (0x38): varint count(1), string name, byte mode,
        // int32 score=0. The client then shows the new mode in the tab
        // list and switches physics/abilities on the next tick.
        if (foundOnline && liveFd >= 0)
        {
            std::vector<uint8_t> updateTab;
            Packet::writeVarInt(updateTab, 1);
            Packet::writeString(updateTab, playerName);
            updateTab.push_back(mode);
            Packet::writeInt(updateTab, 0);
            sendPacket(liveFd, 0x38, updateTab);
        }

        return foundOnline || persisted;
    }

    // Teleport an online player (looked up by username) to absolute world
    // coordinates. Sends an S2C Position packet (0x08) so the client
    // animates the jump, and mutates the live session so scoreboard /
    // chunk-streaming / save-on-logout see the new position. The player's
    // current yaw/pitch are preserved.
    bool NetworkManager::teleportToCoords(const std::string &playerName, double x, double y, double z)
    {
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state == ClientState::PLAY && ses.username == playerName)
            {
                // "~" wildcard support (Hattrick-style /tp): any NaN axis
                // falls back to the player's current coordinate on that axis,
                // so "/tp ~ 200 ~" means "move to Y=200, keep X/Z".
                const double finalX = std::isnan(x) ? ses.x : x;
                const double finalY = std::isnan(y) ? ses.y : y;
                const double finalZ = std::isnan(z) ? ses.z : z;
                auto packetSender = [this](int f, int32_t p, const std::vector<uint8_t> &b)
                { sendPacket(f, p, b); };
                Player::PlayerManager::getInstance().teleportPlayer(
                    fd, ses, finalX, finalY, finalZ, ses.yaw, ses.pitch, packetSender);
                Core::Logger::debug("[NET TELEPORT] name=" + playerName +
                                    " fd=" + std::to_string(fd) +
                                    " dest=(" + std::to_string(finalX) + ", " +
                                    std::to_string(finalY) + ", " +
                                    std::to_string(finalZ) + ")");
                return true;
            }
        }
        Core::Logger::debug("[NET TELEPORT] player=" + playerName + " NOT ONLINE — no packet emitted");
        return false;
    }

    // Teleport one player to the current world position of another.
    // Resolves the target's live coordinates first, then reuses
    // teleportToCoords so both /tp self->target and /tp player->target
    // share the same Position-packet path.
    bool NetworkManager::teleportToPlayer(const std::string &moverName, const std::string &targetName)
    {
        double tx = 0.0, ty = 0.0, tz = 0.0;
        for (const auto &[fd, ses] : m_clients)
        {
            if (ses.state == ClientState::PLAY && ses.username == targetName)
            {
                tx = ses.x;
                ty = ses.y;
                tz = ses.z;
                return teleportToCoords(moverName, tx, ty, tz);
            }
        }
        Core::Logger::debug("[NET TELEPORT] target=" + targetName + " NOT ONLINE — no packet emitted");
        return false;
    }

} // namespace LightweightMC::Network
