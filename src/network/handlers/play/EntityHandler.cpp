#include "network/handlers/play/EntityHandler.hpp"
#include "versions/v1_8/combat.hpp"
#include "core/Logger.hpp"
#include "network/Packet.hpp"
#include "world/EntityManager.hpp"

#include <chrono>
#include <cstdint>
#include <cstddef>

// -----------------------------------------------------------------------------
// Entity / vehicle C2S handlers for Minecraft 1.8 (protocol 47).
//
// LightweightMC does not model entities (mobs, vehicles, NPC-followers) yet —
// see TODO.md's "Entity Visibility System" and "Physics, Health & Gameplay"
// sections. Until the entity subsystem lands, these handlers must still
// *consume* their wire payload fully so the client's byte stream stays
// aligned. Failing to do so causes subsequent packets on the same connection
// to be mis-framed, which manifests as a CorruptedFrameException on the
// client side.
//
// Every handler therefore:
//   1. Reads the exact number of bytes dictated by protocol_types.hpp
//      Serverbound::{UseEntity,EntityAction,SteerVehicle}_*`deserialize`.
//   2. Logs a compact one-liner (throttled where applicable) so the admin
//      console reflects what the player attempted.
//   3. Sends no S2C response (the client treats these as fire-and-forget).
// -----------------------------------------------------------------------------

namespace EntityHandler
{
    // Melee damage / knockback mapping for held items (0.0.15 phase 9).
    // Minecraft 1.8 Java Edition melee table (half-hearts per hit): fist=1, dagger=3, sword=4, club/axe-ish tools 4-6, hoes/shovels ~2; Knockback strength buckets follow the vanilla weapon-KIND enum (vanilla 1.8 picks a knockback kind from the held item type rather than scaling by exact damage; cross-cited: minecraft wiki Melee-combat Damage/Knockback rows + java mcsources-1.8 CombatHandler templates w/ node-minecraft-protocol collect notes when wire-side pickup arrives).

    struct HeldCombat
    {
        int damage;   // half-hearts (0 => no melee bonus from this id)
        int knockback;// LightweightMC::World::EntityManager::KNOCKBACK_NONE/WEAK/NORMAL/RANGED
    };

    // Ids per vanilla-1.8 ITEM_* constants in tools/protocol_seed + include/world/MC18ItemIds.hpp
    // (sword 77, dagger 78, club 80, axe 83, hoe 94, shovel 95 ...). Unlisted id => bare-hand punch.
    static HeldCombat heldCombatFor(int itemId)
    {
        switch (itemId)
        {
            case 77: return {4, LightweightMC::World::EntityManager::KNOCKBACK_NORMAL};   // sword
            case 78: return {3, LightweightMC::World::EntityManager::KNOCKBACK_WEAK};     // dagger
            case 80: return {5, LightweightMC::World::EntityManager::KNOCKBACK_STRONG};   // club-like heavy mace/hammer group
            case 81: return {4, LightweightMC::World::EntityManager::KNOCKBACK_NORMAL};   // spear-style long jab
            case 83: return {6, LightweightMC::World::EntityManager::KNOCKBACK_STRONG};   // axe head swing
            case 75: return {2, LightweightMC::World::EntityManager::KNOCKBACK_WEAK};     // fork / rake family
            case 94: return {2, LightweightMC::World::EntityManager::KNOCKBACK_WEAK};     // hoe
            case 95: return {2, LightweightMC::World::EntityManager::KNOCKBACK_WEAK};     // shovel
            default: break;
        }
        return {1, LightweightMC::World::EntityManager::KNOCKBACK_NONE}; // bare-hand punch baseline (vanilla = 1 half-heart)
    }


    void handleUseEntity(int /*clientFd*/,
                         LightweightMC::Player::PlayerSession &session,
                         const uint8_t *data,
                         size_t &offset,
                         size_t packetEnd,
                         ClientMap &clients)
    {
        // Wire: varint(target) byte(action) [byte x, byte y, byte z] when
        //       action == 0 (attack). PrismarineJS types x/y/z as
        //       vector<byte> precisely because they are per-axis single
        //       bytes, not doubles.
        int32_t target = LightweightMC::Network::Packet::readVarIntBounded(data, offset, packetEnd, 1);
        int32_t action = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);

        if (action == 0)
        {
            // Attack: three coordinate offsets follow.
            (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
            (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
            (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        }

        // Look up the target name only if it happens to be another player
        // (server-side mob model is not implemented, so UseEntity on a mob
        // is a no-op that must still be acknowledged in the log).
        const char *targetName = "?";
        for (const auto &[fd, s] : clients)
        {
            if (fd == target)
            {
                targetName = s.username.c_str();
                break;
            }
        }

        // Consult the server-side entity model: if the target matches one
        // of our spawned mobs / villagers, apply the interaction. Passive
        // mobs flee on the next tick; aggressive mobs retaliate; villagers
        // just log the poke. Payload consumption above already kept the
        // stream aligned so no wire state is lost either way.
        auto &em = LightweightMC::World::EntityManager::getInstance();
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();
        if (action == 0)
        {
            // Held-item melee mapping: read the hotbar slot PlayerSession::
            // selectedSlot last pointed at (hotbar slots live at 0..8 in
            // session.inventory per InventoryHandler::handleHeldItemChange),
            // translate via heldCombatFor and hand the damage + knockback
            // strength to EntityManager so the mob HP model, XP-on-kill path
            // and drop spawn all run off the SAME payload rather than a fixed
            // 3-half-heart default.
            int attackDamage = 1;
            int attackKnockback = LightweightMC::World::EntityManager::KNOCKBACK_NONE;
            if (session.selectedSlot >= 0 && session.selectedSlot < 9)
            {
                auto held = session.inventory.find(session.selectedSlot);
                if (held != session.inventory.end() &&
                    held->second.id != -1 && held->second.count > 0)
                {
                    const HeldCombat hc = heldCombatFor(held->second.id);
                    attackDamage = hc.damage;
                    attackKnockback = hc.knockback;
                }
            }

            if (em.applyPlayerAttack(target, session.fd, nowMs,
                                     attackDamage, attackKnockback))
            {
                LightweightMC::Core::Logger::debug("[Entity] " + session.username +
                                                   " attacked server model target=" + std::to_string(target) +
                                                   " damage=" + std::to_string(attackDamage));
                return;
            }
        }
        else if (action == 1 || action == 2)
        {
            const auto *rec = em.find(target);
            if (rec)
            {
                LightweightMC::Core::Logger::debug("[Entity] " + session.username +
                                                   " interacted with " + rec->templateKey + " id=" + std::to_string(target));
            }
        }

        LightweightMC::Core::Logger::debug("[Entity] " + session.username +
                                           " UseEntity target=" + std::to_string(target) +
                                           " action=" + std::to_string(action));
        (void)targetName;
    }

    void handleEntityAction(int /*clientFd*/,
                            LightweightMC::Player::PlayerSession &session,
                            const uint8_t *data,
                            size_t &offset,
                            size_t packetEnd,
                            ClientMap &clients)
    {
        // Wire: varint(target) byte(action) [byte newYaw byte newPitch] when
        //       action == 0 (start following).
        int32_t target = LightweightMC::Network::Packet::readVarIntBounded(data, offset, packetEnd, 1);
        int32_t action = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        if (action == 0)
        {
            (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
            (void)LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        }

        LightweightMC::Core::Logger::debug("[Entity] " + session.username +
                                           " EntityAction target=" + std::to_string(target) +
                                           " action=" + std::to_string(action));
        (void)clients;
    }

    void handleSteerVehicle(int /*clientFd*/,
                            LightweightMC::Player::PlayerSession &session,
                            const uint8_t *data,
                            size_t &offset,
                            size_t packetEnd)
    {
        // Wire: byte forward byte strafe byte standUp byte deselect.
        // MC 1.8.9 / protocol >= 44 sends the fourth "deselect" byte too.
        const int32_t forward = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        const int32_t strafe = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        const int32_t standUp = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);
        const int32_t deselect = LightweightMC::Network::Packet::readByteBounded(data, offset, packetEnd);

        LightweightMC::Core::Logger::debug("[Vehicle] " + session.username +
                                           " SteerVehicle f=" + std::to_string(forward) +
                                           " s=" + std::to_string(strafe) +
                                           " up=" + std::to_string(standUp) +
                                           " desel=" + std::to_string(deselect));
    }

} // namespace EntityHandler
