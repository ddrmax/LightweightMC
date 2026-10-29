// Player-state setters that live on the NetworkManager singleton because
// they need to touch both m_clients (the live sessions) and m_database
// (the persisted rows). Kept separate from the framing / socket / events
// files so each concern stays small.
//
// See src/network/NetworkManager.cpp for the class-level invariant notes.

#include "network/NetworkManager.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include "core/Logger.hpp"
#include "core/utils.hpp"
#include "managers/PlayerManager.hpp"
#include "storage/Database.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
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
    // currently online, and forwards a Change Game State packet (0x2B,
    // sub-action 3 = Change Game Mode / Adventure) to that client so the
    // change is reflected in-game without requiring a reconnect.
    //
    // Wire op rationale: 0x2B is the correct vanilla-1.8 op for a mode
    // switch. The client picks up survival/creative/adventure/spectator
    // rules (gravity, invulnerability, inventory policy) from the "Change
    // Game State" T sub-action. 0x38 "Update Player Info" -- a.k.a. the
    // Tab List Update Player packet -- drives tab-list scoring and player
    // properties (canPvP / invincible flags) only, so sending it here
    // leaves the client in the old physics/abilities state.
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

        // Tell the client about the mode change with a Change Game State
        // packet (0x2B): byte subAction = 3 (Change Game Mode / Adventure),
        // then float mode (0=survival, 1=creative, 2=adventure,
        // 3=spectator). The client re-reads its physics / invulnerability /
        // inventory behaviour from this sub-action on the next tick.
        //
        // The commented block below is the legacy 0x38 "Update Player
        // Info" / Tab List Update Player path (varint count, string name,
        // byte mode, int score). That packet does NOT cause
        // the client to switch survival/creative/etc. rules; it is only
        // for tab-list scoreboard and canPvP / invincible flags, so it
        // stays disabled here.
        if (foundOnline && liveFd >= 0)
        {
            std::vector<uint8_t> updateTab;
            /* Legacy Tab List Update Player path (kept for reference only).
            Packet::writeVarInt(updateTab, 1);
            Packet::writeString(updateTab, playerName);
            updateTab.push_back(mode);
            Packet::writeInt(updateTab, 0);
            sendPacket(liveFd, 0x38, updateTab);*/
            updateTab.push_back(3); // sub-action 3 (Change Game Mode)
            Packet::writeFloat(updateTab, mode);
            sendPacket(liveFd, 0x2B, updateTab); // Change Game State
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

    namespace
    {
        // Item-name → LWMC item-ID lookup for /give. Accepts canonical
        // Items18 names plus vanilla-style aliases. All keys lowercased.
        struct ItemEntry
        {
            const char *name;
            int16_t id;
        };

        static const ItemEntry kItemTable[] = {
            {"leather",       0},
            {"bread",         50},
            {"meat",          1},
            {"raw_beef",      1},
            {"cooked_meat",   51},
            {"cooked_beef",   51},
            {"cooked_pork",   51},
            {"cooked_chicken",51},
            {"fish",          52},
            {"cooked_fish",   52},
            {"stew",          53},
            {"salad",         54},
            {"juice",         55},
            {"broth",         56},
            {"grog",          57},
            {"medicine",      58},
            {"twig",          11},
            {"stick",         11},
            {"flint",         12},
            {"reed",          13},
            {"clay",          14},
            {"charcoal",      15},
            {"coal",          15},
            {"oak_plank",     20},
            {"planks",        20},
            {"crafting_table",23},
            {"chest",         24},
            {"furnace",       25},
            {"brewing_stand", 26},
            {"hopper",        27},
            {"dispenser",     28},
            {"torch",         29},
            {"hatchet",       80},
            {"axe",           80},
            {"machete",       81},
            {"spike",         82},
            {"sword",         82},
            {"adze",          83},
            {"hoe",           84},
            {"mattock",       85},
            {"pickaxe",       85},
            {"chisel",        86},
            {"hammer",        87},
            {"scythe",        88},
            {"wool",          3},
            {"feather",       4},
            {"bone",          8},
            {"hide",          9},
            {"pelt",          6},
            {"rabbit_foot",   10},
        };

        int16_t lookupItemByName(const std::string &name)
        {
            std::string lower;
            lower.resize(name.size());
            std::transform(name.begin(), name.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (const auto &entry : kItemTable)
            {
                if (lower == entry.name)
                    return entry.id;
            }
            return -1;
        }
    }

    // Grant an item to a player's inventory. See NetworkManager.hpp header
    // doc for the full contract.
    bool NetworkManager::giveItem(const std::string &playerName,
                                  const std::string &itemName,
                                  int count)
    {
        const int16_t itemId = lookupItemByName(itemName);
        if (itemId < 0)
        {
            Core::Logger::warn("[NET GIVE] unknown item name '" + itemName +
                                "' for target=" + playerName);
            return false;
        }

        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.username != playerName)
                continue;

            int granted = 0;
            int remaining = count;

            // Helper: send a Set Slot packet (S2C 0x2F) so the client sees
            // the updated item slot immediately. Window 0 = player inventory.
            auto sendSetSlot = [&](int16_t slot, const Player::ItemStack &stack)
            {
                std::vector<uint8_t> buf;
                buf.push_back(0); // windowId 0 = player inventory
                Packet::writeShort(buf, static_cast<int16_t>(slot));
                auto tr = Protocol::ProtocolTranslator::translateItemToClient(
                    ses.clientProtocol, stack.id);
                Packet::writeShort(buf, tr.id);
                buf.push_back(stack.count);
                Packet::writeShort(buf, stack.damage);
                if (ses.clientProtocol >= 47)
                    buf.push_back(0); // NBT end tag (1.8+)
                sendPacket(fd, 0x2F, buf);
            };

            // 1. Merge into existing stacks of the same item first
            //    (slots 0..44: hotbar 0..8 + main inventory 9..35).
            for (int slot = 0; slot < 45 && remaining > 0; ++slot)
            {
                auto it = ses.inventory.find(slot);
                if (it == ses.inventory.end() || it->second.id == -1)
                    continue;
                if (it->second.id == itemId && it->second.damage == 0)
                {
                    const int space = 64 - it->second.count;
                    if (space > 0)
                    {
                        const int addCount = std::min(remaining, space);
                        it->second.count = static_cast<uint8_t>(it->second.count + addCount);
                        ses.inventory[slot] = it->second;
                        sendSetSlot(slot, it->second);
                        granted += addCount;
                        remaining -= addCount;
                    }
                }
            }

            // 2. Fill free slots: hotbar first (slots 0..8), then main
            //    inventory (slots 9..35). Each stack is capped at 64 items.
            for (int slot = 0; slot < 9 && remaining > 0; ++slot)
            {
                auto it = ses.inventory.find(slot);
                if (it != ses.inventory.end() && it->second.id != -1)
                    continue;

                const int giveCount = std::min(remaining, 64);
                Player::ItemStack stack;
                stack.id = itemId;
                stack.count = static_cast<uint8_t>(giveCount);
                stack.damage = 0;
                ses.inventory[slot] = stack;

                sendSetSlot(slot, stack);

                granted += giveCount;
                remaining -= giveCount;
            }

            if (remaining > 0)
            {
                for (int slot = 9; slot < 36 && remaining > 0; ++slot)
                {
                    auto it = ses.inventory.find(slot);
                    if (it != ses.inventory.end() && it->second.id != -1)
                        continue;

                    const int giveCount = std::min(remaining, 64);
                    Player::ItemStack stack;
                    stack.id = itemId;
                    stack.count = static_cast<uint8_t>(giveCount);
                    stack.damage = 0;
                    ses.inventory[slot] = stack;

                    sendSetSlot(slot, stack);

                    granted += giveCount;
                    remaining -= giveCount;
                }
            }

            if (granted > 0)
            {
                Core::Logger::info("[NET GIVE] gave " + std::to_string(granted) +
                                   "x " + itemName + " (id=" + std::to_string(itemId) +
                                   ") to " + playerName + " fd=" + std::to_string(fd));
                return true;
            }

            Core::Logger::warn("[NET GIVE] inventory full for " + playerName);
            return false;
        }

        Core::Logger::debug("[NET GIVE] player=" + playerName + " NOT ONLINE");
        return false;
    }

} // namespace LightweightMC::Network
