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
#include "versions/v1_8/xp.hpp"
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
            const int64_t prevLastHurtMs = ses.lastHurtMs;
            const int64_t elapsedBeforeDamage = nowMs - prevLastHurtMs;
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
            Core::Logger::warn("[NET DAMAGE] player=" + playerName +
                               " fd=" + std::to_string(fd) +
                               " amount=" + std::to_string(amount) +
                               " type=" + std::to_string(damageTypeCode) +
                               " hpBefore=" + std::to_string(before) +
                               " hpAfter=" + std::to_string(ses.health) +
                               " alive=" + (ses.alive ? "yes" : "no") +
                               " prevLastHurtMs=" + std::to_string(prevLastHurtMs) +
                               " elapsedBeforeDamage=" + std::to_string(elapsedBeforeDamage) +
                               " nextTickGate5000=" + std::string(elapsedBeforeDamage >= 5000 ? "open" : "closed") +
                               " respawnAtMs=" + std::to_string(ses.respawnAtMs) +
                               " deathCount=" + std::to_string(ses.deathCount) +
                               " foodLevel=" + std::to_string(static_cast<int>(ses.foodLevel)) +
                               " saturation=" + std::to_string(ses.foodSaturationLevel) +
                               " exhaustion=" + std::to_string(ses.foodExhaustionLevel) +
                               " onFire=" + (ses.onFire ? "yes" : "no") +
                               " air=" + std::to_string(ses.air));
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
            Packet::writeDouble(pos, ses.x);
            Packet::writeDouble(pos, ses.y);
            Packet::writeDouble(pos, ses.z);
            Packet::writeFloat(pos, static_cast<float>(ses.yaw));
            Packet::writeFloat(pos, static_cast<float>(ses.pitch));
            pos.push_back(0); // flags
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

    bool NetworkManager::setPlayerFood(const std::string &playerName,
                                       int32_t foodLevel,
                                       float foodSaturation)
    {
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.username != playerName)
                continue;
            ses.foodLevel = static_cast<int8_t>(foodLevel);
            ses.foodSaturationLevel = foodSaturation;
            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, ses.health,
                                     static_cast<int32_t>(ses.foodLevel),
                                     ses.foodSaturationLevel);
            sendPacket(fd, 0x06, hp);
            Core::Logger::info("[NET FOOD] player=" + playerName +
                               " fd=" + std::to_string(fd) +
                               " foodLevel=" + std::to_string(static_cast<int>(ses.foodLevel)) +
                               " saturation=" + std::to_string(ses.foodSaturationLevel));
            return true;
        }
        Storage::PlayerData data;
        data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(playerName);
        data.username = playerName;
        Storage::PlayerData existing{};
        if (m_database.loadPlayerData(data.uuid, existing))
        {
            data = existing;
            data.foodLevel = static_cast<int8_t>(foodLevel);
            data.foodSaturationLevel = foodSaturation;
        }
        else
        {
            data.foodLevel = static_cast<int8_t>(foodLevel);
            data.foodSaturationLevel = foodSaturation;
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
        // Same piecewise curve as PlayerSession::grantXp — the old flat
        // min(15, total/1000) formula locked /experience grants at level 15.
        int32_t remainingXp = data.totalExperience;
        int level = 0;
        while (true)
        {
            const int step = LightweightMC::Versions::V1_8::Xp::xpToNext(level);
            if (remainingXp < step)
                break;
            remainingXp -= step;
            ++level;
        }
        data.experienceLevel = static_cast<int16_t>(level);
        data.experienceProgress = (level <= 0)
                                      ? 0.0f
                                      : static_cast<float>(remainingXp) /
                                            static_cast<float>(LightweightMC::Versions::V1_8::Xp::xpToNext(level));
        m_database.savePlayerData(data);
        return level - prevLevel;
    }

    // Phase 14b — auto-respawn sweep. Server.cpp's fixed 20 Hz tick loop calls
    // this every pass so deaths stop being "stuck offline": each PLAY session
    // with alive=false gets its respawn gated by the vanilla-1.8 rules stored
    // in World::GameRules (see wiki/Game-Rules.md Player category):
    //   * immediate_respawn=true  -> a legal respawn window is just "death
    //     happened" (PlayerSession::onDeath stamps respawnAtMs with nowMs), so
    //     every dead session respawns on the very next tick pass, and the
    //     respawn_radius distance gate is bypassed (same semantics NetworkManager::respawnPlayer
    //     uses for an explicit /respawn command).
    //   * respawnwaves=true       -> H1Z1-style all-or-nothing waves: once ANY
    //     dead session is due, every due session respawns in the same pass so
    //     a party wipes together instead of reviving one-by-one.
    // Otherwise vanilla-1.8 pacing applies: PlayerSession::onDeath() already
    // set respawnAtMs = death time, and respawn_radius (int rule, default 10)
    // bounds how far the avatar may teleport away from its registered spawn/bed
    // point; sessions that fail that distance check stay dead so the next tick
    // retries instead of yanking them across the map. Sessions that pass get
    // the same packet burst respawnPlayer() sends (0x08 Position + 0x06
    // UpdateHealth, hunger/XP policy handled by PlayerSession::respawn()).
    int NetworkManager::autoRespawnDuePlayers(int64_t nowMs)
    {
        auto &rules = World::GameRules::getInstance();
        bool immediate = false;
        rules.getBool("immediate_respawn", immediate);
        bool waves = false;
        rules.getBool("respawnwaves", waves);
        int respawnRadius = 10;
        {
            int64_t radiusValue = 10;
            if (rules.getInt("respawn_radius", radiusValue) && radiusValue >= 0)
                respawnRadius = static_cast<int>(radiusValue);
        }

        struct RespawnCandidate
        {
            int fd{0};
            double dxToSpawn{0.0};
            LightweightMC::Player::PlayerSession *session{nullptr};
        };
        std::vector<RespawnCandidate> due;
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || ses.alive)
                continue;

            const double spawnX = ses.hasBedSpawn ? static_cast<double>(ses.bedX) + 0.5
                                                  : static_cast<double>(ses.spawnX) + 0.5;
            const double spawnY = ses.hasBedSpawn ? static_cast<double>(ses.bedY)
                                                  : static_cast<double>(ses.spawnY);
            const double spawnZ = ses.hasBedSpawn ? static_cast<double>(ses.bedZ) + 0.5
                                                  : static_cast<double>(ses.spawnZ) + 0.5;
            const double dx = ses.x - spawnX;
            const double dz = ses.z - spawnZ;
            RespawnCandidate candidate{fd, std::sqrt(dx * dx + dz * dz), &ses};

            // Death must have occurred (respawnAtMs stamped by onDeath) and the
            // wall-clock window must be open. immediate_respawn skips the
            // "wait for the radius window" gate like /respawn does.
            if (ses.respawnAtMs == 0 || nowMs < ses.respawnAtMs)
                continue;
            if (!immediate && candidate.dxToSpawn > static_cast<double>(std::max(1, respawnRadius)))
                continue;
            due.push_back(candidate);
        }

        // Wave rule: only launch a wave once at least one candidate is due; the
        // distance gate still applies to every member so distant players wait
        // for their own window like vanilla does.
        if (due.empty())
            return 0;

        int respawned = 0;
        for (auto &candidate : due)
        {
            LightweightMC::Player::PlayerSession &ses = *candidate.session;
            ses.respawn();
            std::vector<uint8_t> pos;
            Packet::writeVarInt(pos, static_cast<int>(candidate.fd));
            Packet::writeInt(pos, static_cast<int32_t>(ses.x * 32.0));
            Packet::writeInt(pos, static_cast<int32_t>(ses.y * 32.0));
            Packet::writeInt(pos, static_cast<int32_t>(ses.z * 32.0));
            pos.push_back(0); // yaw
            pos.push_back(0); // pitch
            pos.push_back(1); // onGround
            sendPacket(candidate.fd, 0x08, pos);
            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, ses.health, static_cast<int32_t>(ses.foodLevel), ses.foodSaturationLevel);
            sendPacket(candidate.fd, 0x06, hp);
            ++respawned;
            Core::Logger::info("[NET AUTO-RESPAWN] player=" + ses.username + " fd=" + std::to_string(candidate.fd) +
                               " respawned via rules(immediate_respawn=" + std::string(immediate ? "true" : "false") +
                               ", respawnwaves=" + std::string(waves ? "true" : "false") +
                               ", respawn_radius=" + std::to_string(respawnRadius) + ")");
        }

        // Persist offline-death bookkeeping so a logout during the pending
        // window still restores alive=true next login (PlayerManager/loadFrom
        // already heals health=0 rows, but deathCount keeps counting here).
        for (auto &candidate : due)
        {
            LightweightMC::Player::PlayerSession &ses = *candidate.session;
            Storage::PlayerData data;
            data.uuid = Player::PlayerManager::getInstance().getPlayerUuid(ses.username);
            data.username = ses.username;
            Storage::PlayerData existing{};
            if (m_database.loadPlayerData(data.uuid, existing))
                data.deathCount = existing.deathCount;
            data.health = 20.0f;
            m_database.savePlayerData(data);
        }

        // respawnwaves flips the "wave already fired" side effect: when the
        // rule is on and at least one session revived, all remaining dead
        // sessions revive in the same pass so a squad wipes together. The loop
        // above already iterates every due candidate, so the rule flag mostly
        // affects distance gating comments plus future callers that gate chat
        // broadcasts; keep it referenced to document intent.
        (void)waves;
        return respawned;
    }

    void NetworkManager::tickRegeneration(int64_t nowMs)
    {
        // Survival hunger depletion always ticks for live PLAY survival sessions on the fixed 20 Hz loop — independent of natural_health_regeneration. Movement hooks add pending exhaustion via PlayerHandler; drain converts that into saturation + starvation steps, and idle standing still loses saturation at its own wall-clock cadence (wiki/Hunger.md pipeline exhaustion -> saturation -> starvation).
        bool regenEnabled = true;
        World::GameRules::getInstance().getBool("natural_health_regeneration", regenEnabled);

        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || !ses.alive)
                continue;
            // Survival depletion must run on every 20 Hz tick even when HP regen is gamerule-disabled or blocked by hurt/food gates: standing still still accrues saturation loss via pending foodExhaustionLevel + wall-clock pacing inside PlayerSession::tickSurvivalDrain. Movement hooks (handlePlayerMovement) only add to foodExhaustionLevel without draining sat directly — see wiki/Hunger.md pipeline exhaustion->saturation->starvation.
            ses.tickSurvivalDrain(nowMs);
            if (!regenEnabled)
                continue;
            const float before = ses.health;
            if (!ses.tickRegen(nowMs))
                continue;
            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, ses.health,
                                     static_cast<int32_t>(ses.foodLevel),
                                     ses.foodSaturationLevel);
            sendPacket(fd, 0x06, hp);
            const int64_t elapsedMs = nowMs - ses.lastHurtMs;
            Core::Logger::warn("[NET REGEN] player=" + ses.username +
                               " fd=" + std::to_string(fd) +
                               " hpBefore=" + std::to_string(before) +
                               " hpAfter=" + std::to_string(ses.health) +
                               " lastHurtMs=" + std::to_string(ses.lastHurtMs) +
                               " elapsedSinceHurt=" + std::to_string(elapsedMs) +
                               " respawnAtMs=" + std::to_string(ses.respawnAtMs) +
                               " alive=yes" +
                               " foodLevel=" + std::to_string(static_cast<int>(ses.foodLevel)) +
                               " saturation=" + std::to_string(ses.foodSaturationLevel) +
                               " exhaustion=" + std::to_string(ses.foodExhaustionLevel) +
                               " onFire=" + (ses.onFire ? "yes" : "no") +
                               " air=" + std::to_string(ses.air));
        }
    }

    // ---- Void / out-of-bounds fall damage (vanilla-1.8 Java timing rules) ---
    // Called once per world tick from Server.cpp's fixed 20 Hz loop. Vanilla
    // deals void damage every 10 game ticks (500 ms) for 4 HP per interval,
    // triggered natively below Y = -64 on normal planes. LWMC's Aether sky
    // plane (id 100) instead floors its fall at Y = -16 (AetherGen convention):
    // damage ticks there too, and once the player passes that line they are
    // teleported back to Overworld via setPlayerWorldAt's default-spawn path
    // (the documented aether "void fall" behaviour AetherGen.cpp promises but
    // which had never been implemented — the missing feature this drop adds).
    void NetworkManager::tickVoidDamage(int64_t nowMs)
    {
        const int64_t intervalMs = Storage::kVoidDamageIntervalTicks * 50; // 20 Hz basis
        for (auto &[fd, ses] : m_clients)
        {
            if (ses.state != ClientState::PLAY || !ses.alive)
                continue;

            const bool aetherPlane = (ses.worldId == 100);
            const double floorY = aetherPlane ? Storage::kAetherVoidFloorY
                                              : Storage::kDefaultVoidFloorY;
            if (ses.y >= floorY)
            {
                // Out of the void: clear the cadence cursor so the next entry
                // starts a fresh 500 ms window rather than firing instantly.
                ses.lastVoidDamageMs = 0;
                ses.inVoidNow = false;
                continue;
            }
            // Aether documented behaviour: a fall past -16 teleports back to
            // Overworld before damage would ever kill the player (islands sit
            // far above -16, so reaching it means an intentional/fall-out drop).
            if (aetherPlane)
            {
                const std::string pname = ses.username;
                if (setPlayerWorldAt(pname, 0, 0.0, 0.0, 0.0, false))
                {
                    Core::Logger::warn("[NET VOID FALL] player=" + pname +
                                       " fd=" + std::to_string(fd) +
                                       " fell below aether floor y=" + std::to_string(floorY) +
                                       " -> teleported to Overworld default spawn");
                    continue;
                }
                Core::Logger::warn("[NET VOID FALL] player=" + pname +
                                   " overworld switch failed; falling back to void damage");
            }
            ses.inVoidNow = true;
            if (ses.lastVoidDamageMs == 0)
            {
                // First tick below the floor: anchor, first hit one full interval later.
                ses.lastVoidDamageMs = nowMs;
                continue;
            }
            if (nowMs - ses.lastVoidDamageMs < intervalMs)
                continue;
            // Re-anchor on nowMs rather than += intervalMs: a long gap (lag
            // spike, respawn freeze) must not fire a catch-up burst of void
            // hits — vanilla schedules one damage per game tick it actually
            // processes, and our loop only sees wall-clock snapshots.
            ses.lastVoidDamageMs = nowMs;

            const float before = ses.health;
            // Creative/spectator invuln is applyDamage's own gate — reuse it so
            // void damage honours the same rules as /damage and fall damage.
            if (!ses.applyDamage(static_cast<float>(Storage::kVoidDamagePerInterval), nowMs, 4))
                continue;

            std::vector<uint8_t> hp;
            writeUpdateHealthPayload(hp, ses.health,
                                     static_cast<int32_t>(ses.foodLevel),
                                     ses.foodSaturationLevel);
            sendPacket(fd, 0x06, hp);
            if (!ses.alive)
            {
                std::vector<uint8_t> death;
                writeDeathCombatPayload(death, fd, 0,
                                        ses.username + " fell into the void");
                sendPacket(fd, 0x42, death);
                Core::Logger::warn("[NET VOID KILL] player=" + ses.username +
                                   " fd=" + std::to_string(fd) +
                                   " planeId=" + std::to_string(ses.worldId) +
                                   " fell below y=" + std::to_string(floorY) +
                                   " and died — autoRespawnDuePlayers will revive");
            }
            else
            {
                Core::Logger::warn("[NET VOID DAMAGE] player=" + ses.username +
                                   " fd=" + std::to_string(fd) +
                                   " planeId=" + std::to_string(ses.worldId) +
                                   " y=" + std::to_string(ses.y) +
                                   " hpBefore=" + std::to_string(before) +
                                   " hpAfter=" + std::to_string(ses.health));
            }
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
