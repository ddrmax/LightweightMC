// -----------------------------------------------------------------------------
// NetworkGameplay.cpp -- Phase 6 (0.0.15) vanilla-1.8 gameplay subsystems
// that live on the NetworkManager singleton because they need both m_clients
// (the live sessions) and m_database (the persisted rows). Sits next to
// NetworkPlayerMeta.cpp (op level / gamemode / teleport) so each concern
// stays small.
//
// Wire packets emitted:
//   0x06 UpdateHealth     -- HP bar refresh after each non-fatal damage
//   0x42 DeathCombatEvent -- fired when HP hits zero so the client
//                            switches to the death HUD + starts respawn
//                            countdown
//   0x08 Position         -- respawn teleport into spawnX/Y/Z
//   0x1f Experience       -- XP bar refresh after grantXp
//
// Era mapping is handled uniformly by NetworkFraming.cpp::sendPacket via
// ProtocolVersion::s2cWireIdFromLegacy, so callers below use the vanilla-1.8
// wire IDs and MODERN/LATEST clients see the era-translated byte stream.
// -----------------------------------------------------------------------------

#include "network/NetworkManager.hpp"
#include "network/Packet.hpp"
#include "core/Logger.hpp"
#include "managers/PlayerManager.hpp"
#include "storage/Database.hpp"
#include "world/GameRules.hpp"
#include "core/utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

namespace LightweightMC::Network
{

    static void writeUpdateHealthPayload(std::vector<uint8_t> &out,
                                         float health,
                                         int32_t food,
                                         float foodSat)
    {
        Packet::writeFloat(out, health);
        Packet::writeVarInt(out, food);
        Packet::writeFloat(out, foodSat);
    }

    static void writeExperiencePayload(std::vector<uint8_t> &out,
                                       float experienceBar,
                                       int32_t level,
                                       int32_t totalXp)
    {
        Packet::writeFloat(out, experienceBar);
        Packet::writeVarInt(out, level);
        Packet::writeVarInt(out, totalXp);
    }

    static void writeDeathCombatPayload(std::vector<uint8_t> &out,
                                        int32_t playerId,
                                        int32_t entityId,
                                        const std::string &message)
    {
        Packet::writeVarInt(out, 2);        // 1. Event Type: 2 = Entity Dead (MUST be written first!)
        Packet::writeVarInt(out, playerId); // 2. Player ID (VarInt)
        Packet::writeInt(out, entityId);    // 3. Entity ID / Killer ID (Int 32-bit, or -1 if none)
        Packet::writeString(out, message);  // 4. Death Message (JSON Chat Component)
    }

    bool NetworkManager::applyDamageToPlayer(const std::string &playerName,
                                             float amount,
                                             int64_t nowMs,
                                             int damageTypeCode)
    {
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.username != playerName)
                continue;
            const float before = ses.health;
            const bool applied = ses.applyDamage(amount, nowMs, damageTypeCode);
            if (!applied)
            {
                Core::Logger::debug("[NET DAMAGE] player=" + playerName +
                                    " fd=" + std::to_string(fd) +
                                    " amount=" + std::to_string(amount) +
                                    " type=" + std::to_string(damageTypeCode) +
                                    " SKIPPED (already-dead or invuln gamemode)");
                return false;
            }
            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, ses.health,
                                     static_cast<int32_t>(ses.foodLevel),
                                     ses.foodSaturationLevel);
            sendPacket(fd, 0x06, hp);
            if (!ses.alive)
            {
                std::vector<uint8_t> death;
                writeDeathCombatPayload(death, fd, 0,
                                        ses.username + " has died");
                sendPacket(fd, 0x42, death);
            }
            Core::Logger::debug("[NET DAMAGE] player=" + playerName +
                                " fd=" + std::to_string(fd) +
                                " amount=" + std::to_string(amount) +
                                " type=" + std::to_string(damageTypeCode) +
                                " hpBefore=" + std::to_string(before) +
                                " hpAfter=" + std::to_string(ses.health) +
                                " alive=" + (ses.alive ? "yes" : "no"));
            return true;
        }
        Storage::PlayerData data;
        data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        data.username = playerName;
        Storage::PlayerData existing{};
        if (m_database.loadPlayerData(data.uuid, existing))
        {
            data.health = std::max(0.0f, existing.health - amount);
            data.deathCount = existing.deathCount;
            if (data.health <= 0.0f)
                data.deathCount = static_cast<uint8_t>(
                    std::min<int>(255, existing.deathCount + 1));
        }
        else
        {
            data.health = 20.0f - amount;
            if (data.health < 0.0f)
                data.health = 0.0f;
            data.deathCount = (data.health <= 0.0f) ? 1 : 0;
        }
        const bool persisted = m_database.savePlayerData(data);
        Core::Logger::debug("[NET DAMAGE] offline player=" + playerName +
                            " persisted=" + (persisted ? "yes" : "no"));
        return persisted;
    }

    bool NetworkManager::killPlayer(const std::string &playerName, int64_t nowMs)
    {
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.username != playerName)
                continue;
            ses.health = 0.0f;
            ses.onDeath(nowMs);
            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, 0.0f,
                                     static_cast<int32_t>(ses.foodLevel),
                                     ses.foodSaturationLevel);
            sendPacket(fd, 0x06, hp);
            std::vector<uint8_t> death;
            writeDeathCombatPayload(death, fd, 0, ses.username + " was slain");
            sendPacket(fd, 0x42, death);
            return true;
        }
        Storage::PlayerData data;
        data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        data.username = playerName;
        data.health = 0.0f;
        Storage::PlayerData existing{};
        if (m_database.loadPlayerData(data.uuid, existing))
            data.deathCount = static_cast<uint8_t>(
                std::min<int>(255, existing.deathCount + 1));
        else
            data.deathCount = 1;
        return m_database.savePlayerData(data);
    }

    bool NetworkManager::respawnPlayer(const std::string &playerName)
    {
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.username != playerName)
                continue;
            ses.respawn();
            std::vector<uint8_t> pos;
            Packet::writeVarInt(pos, fd);
            Packet::writeInt(pos, static_cast<int32_t>(ses.x * 32.0));
            Packet::writeInt(pos, static_cast<int32_t>(ses.y * 32.0));
            Packet::writeInt(pos, static_cast<int32_t>(ses.z * 32.0));
            pos.push_back(0); // yaw
            pos.push_back(0); // pitch
            pos.push_back(1); // onGround
            sendPacket(fd, 0x08, pos);
            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, ses.health,
                                     static_cast<int32_t>(ses.foodLevel),
                                     ses.foodSaturationLevel);
            sendPacket(fd, 0x06, hp);
            return true;
        }
        Storage::PlayerData data;
        data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        data.username = playerName;
        Storage::PlayerData existing{};
        if (m_database.loadPlayerData(data.uuid, existing))
        {
            data = existing;
            data.health = 20.0f;
        }
        else
        {
            data.health = 20.0f;
        }
        return m_database.savePlayerData(data);
    }

    int NetworkManager::grantPlayerXp(const std::string &playerName, int32_t amount)
    {
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.username != playerName)
                continue;
            const int gained = ses.grantXp(amount);
            std::vector<uint8_t> xp;
            writeExperiencePayload(xp, ses.experienceProgress,
                                   ses.experienceLevel, ses.totalExperience);
            sendPacket(fd, 0x1f, xp);
            Core::Logger::debug("[NET XP] player=" + playerName +
                                " fd=" + std::to_string(fd) +
                                " amount=" + std::to_string(amount) +
                                " gained=" + std::to_string(gained) +
                                " level=" + std::to_string(ses.experienceLevel) +
                                " total=" + std::to_string(ses.totalExperience));
            return gained;
        }
        Storage::PlayerData data;
        data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        data.username = playerName;
        Storage::PlayerData existing{};
        const bool found = m_database.loadPlayerData(data.uuid, existing);
        const int32_t prevLevel = found ? existing.experienceLevel : 0;
        int64_t newTotal = (found ? existing.totalExperience : 0) + amount;
        if (newTotal < 0)
            newTotal = 0;
        data.totalExperience = static_cast<int32_t>(newTotal);
        const int32_t newLevel = std::min(15, data.totalExperience / 1000);
        data.experienceLevel = static_cast<int16_t>(newLevel);
        data.experienceProgress =
            static_cast<float>(data.totalExperience - newLevel * 1000) / 1000.0f;
        m_database.savePlayerData(data);
        return newLevel - prevLevel;
    }

    void NetworkManager::tickRegeneration(int64_t nowMs)
    {
        // Vanilla-1.8 natural_health_regeneration gamerule gates the sweep; PlayerSession::tickRegen
        // then applies the 5 s no-damage window + foodLevel >= 9 food-gate + full-HP short-circuit internally.
        bool regenEnabled = true;
        World::GameRules::getInstance().getBool("natural_health_regeneration", regenEnabled);
        if (!regenEnabled)
            return;

        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || !ses.alive)
                continue;
            const float before = ses.health;
            if (!ses.tickRegen(nowMs))
                continue;
            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, ses.health,
                                     static_cast<int32_t>(ses.foodLevel),
                                     ses.foodSaturationLevel);
            sendPacket(fd, 0x06, hp);
            Core::Logger::debug("[NET REGEN] player=" + ses.username +
                                " fd=" + std::to_string(fd) +
                                " hpBefore=" + std::to_string(before) +
                                " hpAfter=" + std::to_string(ses.health));
        }
    }

    bool NetworkManager::setRespawnWaves(bool enabled)
    {
        auto &gr = World::GameRules::getInstance();
        const bool ok = gr.setBool("respawnwaves", enabled);
        Core::Logger::info(std::string("[RESPAWN WAVES] ") +
                           (enabled ? "enabled" : "disabled") +
                           (ok ? " (rule persisted)" : " (rule missing - no change)"));
        return ok;
    }

    std::vector<std::string> NetworkManager::listOnlinePlayers() const
    {
        std::vector<std::string> out;
        for (const auto &[fd, ses] : m_clients)
        {
            if (ses.state == ClientState::PLAY)
                out.push_back(ses.username);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool NetworkManager::sendPrivateChat(const std::string &targetName,
                                         const std::string &message)
    {
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.username != targetName)
                continue;
            std::vector<uint8_t> chatMsg;
            Packet::writeString(chatMsg, R"({"text":")" + Core::escapeChatJson(message) + R"("})");
            chatMsg.push_back(1);
            sendPacket(fd, 0x02, chatMsg);
            return true;
        }
        return false;
    }

} // namespace LightweightMC::Network
