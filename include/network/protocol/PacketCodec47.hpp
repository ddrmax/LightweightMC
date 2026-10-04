#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "network/protocol/PacketKind.hpp"

// ---------------------------------------------------------------------------
// PacketCodec47 — protocol 47 (Minecraft 1.8.x) S2C/C2S packet builders.
//
// Every function builds the *payload* of one protocol-47 play-state packet
// (the wire ID is NOT included; framing is done by BaseProtocol::encodeFrame).
// The version-history comment above each function states which Minecraft
// version first added the packet and, where applicable, in which version it
// was removed or renamed. This makes the codec a self-documenting reference
// for the protocol-47 wire format.
//
// Packet IDs (protocol 47, per minecraft.wiki/w/Protocol_version_47):
//   S2C: 0x00 Disconnect, 0x01 Join Game, 0x02 Chat, 0x03 Health,
//        0x04 Entity Equipment, 0x05 Spawn Position, 0x06 Update HUD,
//        0x07 Update Health, 0x08 Player Pos & Look, 0x0B Animation,
//        0x0C Add Player, 0x11 Spawn Experience Orb, 0x12 Explosion,
//        0x13 Entity Action, 0x14 Start Obscured, 0x15 End Obscured,
//        0x16 Entity Look, 0x17 Entity Teleport, 0x18 Entity Rel Move,
//        0x19 Spawn Entity, 0x1D Spawn Painting, 0x1E Damage Effect,
//        0x20 Entity Properties, 0x21 Chunk Data, 0x22 Multi Block Change,
//        0x24 Heart Particle, 0x25 Block Change (multi), 0x26 Map Chunk Bulk,
//        0x27 Game Info, 0x28 Set Cool Down, 0x29 Succeeding Burst,
//        0x2A Set Center Chunk, 0x2B Sign Edit, 0x2C Set Title, 0x2D Open Window,
//        0x2E Close Window, 0x30 Window Items, 0x31 Set Slot,
//        0x32 Confirm Transaction, 0x33 Player Abilities, 0x34 Tab List Remove,
//        0x35 Set Default Spawn, 0x36 Carried Minecart, 0x37 Player Time,
//        0x38 Update Player Info, 0x39 Update Block Entity, 0x3A Set Particle,
//        0x3B Scoreboard Objective, 0x3C Update Score, 0x3D Display Scoreboard,
//        0x42 Combat Event, 0x43 Camera, 0x44 World Border,
//        0x47 Player List Header/Footer, 0x48 Resource Pack.
//   C2S: 0x00 Keep Alive, 0x01 Chat, 0x02 Use Entity, 0x03 Player Movement,
//        0x04 Flying, 0x05 Movement, 0x06 Look Only, 0x07 Digging,
//        0x08 Place Block, 0x09 Held Item, 0x0A Arm Animation, 0x0B Telemetry,
//        0x0D Close Window, 0x0E Click Window, 0x0F Confirm Transaction,
//        0x10 Creative Slot Action, 0x11 Enchant Item, 0x12 Update Sign,
//        0x13 Client Abilities, 0x14 Tab Complete, 0x15 Client Settings,
//        0x16 Client Status, 0x17 Plugin Message, 0x18 Spectate,
//        0x19 Resource Pack Status.
// ---------------------------------------------------------------------------

namespace LightweightMC::Network::Protocol
{
    // =====================================================================
    // S2C packet builders (protocol 47 / Minecraft 1.8.x)
    // =====================================================================

    // 0x00 Disconnect — Login/Play state. Payload: JSON string reason.
    // Added: 1.3.1 (proto 5). Still current.
    std::vector<uint8_t> encodeDisconnect(const std::string &jsonReason);

    // 0x01 Join Game — Play state. Sent once after login.
    // Added: 1.3.1 (proto 5). Still current.
    std::vector<uint8_t> encodeJoinGame(int32_t entityId, uint8_t gamemode, int32_t dimension,
                                        uint8_t difficulty, uint8_t maxPlayers,
                                        const std::string &levelType, bool reducedDebugInfo);

    // 0x02 Chat — Play state. Payload: JSON string message.
    // Added: 1.4.6 (proto 17). Still current.
    std::vector<uint8_t> encodeChat(const std::string &jsonMessage);

    // 0x03 Health — Play state. Payload: health(float, 0..20).
    // Added: 1.6.2 (proto 30). Still current.
    std::vector<uint8_t> encodeHealth(float health);

    // 0x04 Entity Equipment — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeEntityEquipment(int32_t entityId, int16_t slot,
                                               int16_t itemId, uint8_t count, int16_t damage);

    // 0x05 Spawn Position — Play state. Payload: x,y,z (long packed).
    // Added: 1.3.1 (proto 5). Still current.
    std::vector<uint8_t> encodeSpawnPosition(int32_t x, int32_t y, int32_t z);

    // 0x06 Update HUD — Play state (XP bar + food/health mirror).
    // Added: 1.6.2 (proto 30). Still current.
    std::vector<uint8_t> encodeUpdateHud(float xpProgress, int32_t level, int32_t totalXp,
                                         uint8_t foodLevel, float saturation, float health);

    // 0x07 Update Health — Play state (food/saturation/exhaustion).
    // Added: 1.8 (proto 47). REMOVED in 1.9 (proto 107): folded into Update HUD (0x06).
    std::vector<uint8_t> encodeUpdateHealth(uint8_t foodLevel, float saturation, float exhaustion);

    // 0x08 Player Position And Look — Play state (server -> client teleport).
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodePlayerPosLook(double x, double y, double z, float yaw, float pitch, uint8_t flags);

    // 0x0B Animation — Play state (arm swing / fire extinguish).
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeAnimation(int32_t entityId, uint8_t animationId);

    // 0x0C Add Player — Play state (spawn a player for existing players).
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeAddPlayer(int32_t entityId, const uint8_t (&uuid)[16],
                                         double x, double y, double z,
                                         float yaw, float pitch, int16_t selectedSlot);

    // 0x11 Spawn Experience Orb — Play state.
    // Added: 1.0 (proto 22). Still current.
    std::vector<uint8_t> encodeSpawnExperienceOrb(int32_t entityId, double x, double y, double z, int16_t count);

    // 0x12 Explosion — Play state (explosion particle burst).
    // Added: 1.5 (proto 18). Still current.
    std::vector<uint8_t> encodeExplosion(double x, double y, double z, float radius);

    // 0x13 Entity Action — Play state (sneak/sprint/jump/stop-eating/etc.).
    // Added: 1.5 (proto 18). Still current.
    std::vector<uint8_t> encodeEntityAction(int32_t entityId, uint8_t actionId);

    // 0x14 Start Obscured By Block — Play state (underwater overlay on).
    // Added: 1.6.2 (proto 30). Still current.
    std::vector<uint8_t> encodeStartObscuredByBlock();

    // 0x15 End Obscured By Block — Play state (underwater overlay off).
    // Added: 1.6.2 (proto 30). Still current.
    std::vector<uint8_t> encodeEndObscuredByBlock();

    // 0x16 Entity Look — Play state (entity head rotation only).
    // Added: 1.7.2 (proto 40). Still current.
    std::vector<uint8_t> encodeEntityLook(int32_t entityId, float yaw, float pitch);

    // 0x17 Entity Teleport — Play state (entity position/rotation absolute).
    // Added: 1.7.2 (proto 40). Still current.
    std::vector<uint8_t> encodeEntityTeleport(int32_t entityId, double x, double y, double z,
                                              float yaw, float pitch, bool onGround);

    // 0x18 Entity Relative Move And Look — Play state (entity delta movement).
    // Added: 1.5 (proto 18). Still current.
    std::vector<uint8_t> encodeEntityRelativeMove(int32_t entityId, double dx, double dy, double dz,
                                                  float yaw, float pitch, bool onGround);

    // 0x19 Spawn Entity — Play state (mob/vehicle/object spawn).
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeSpawnEntity(int32_t entityId, uint8_t type, double x, double y,
                                           double z, float yaw, float pitch);

    // 0x1D Spawn Painting — Play state.
    // Added: 1.7.2 (proto 40). REMOVED in 1.8.6 (proto 53).
    std::vector<uint8_t> encodeSpawnPainting(int32_t entityId, int32_t x, int32_t y, int32_t z,
                                             uint8_t direction, const std::string &title);

    // 0x1E Damage Entity Effect — Play state (knockback / damage flash).
    // Added: 1.5 (proto 18). Still current.
    std::vector<uint8_t> encodeDamageEntityEffect(int32_t entityId, uint8_t damageAmount,
                                                  double knockbackX, double knockbackZ, bool fromPlayer);

    // 0x20 Entity Properties — Play state (attribute modifiers).
    // Added: 1.6.1 (proto 73). Still current.
    std::vector<uint8_t> encodeEntityProperties(int32_t entityId);

    // 0x21 Chunk Data — Play state (single chunk column).
    // Added: 1.0 (proto 22). Still current.
    std::vector<uint8_t> encodeChunkData(int32_t chunkX, int32_t chunkZ, bool fullChunk, uint16_t primaryBitMask,
                                         const std::vector<uint8_t> &data);

    // 0x22 Multi Block Change — Play state (batched block updates).
    // Added: 1.0 (proto 22). Still current.
    std::vector<uint8_t> encodeMultiBlockChange(int32_t chunkX, int32_t chunkZ, bool isGround);

    // 0x24 Heart Particle — Play state.
    // Added: 1.6.2 (proto 30). Still current.
    std::vector<uint8_t> encodeHeartParticle(uint8_t particleId, double x, double y, double z,
                                             uint8_t offsetY, float speed, int32_t count);

    // 0x25 Multi Block Change (1.7 format) — Play state.
    // Added: 1.7.2 (proto 40). REMOVED in 1.8 (proto 47).
    std::vector<uint8_t> encodeMultiBlockChange17(int16_t chunkX, int16_t chunkZ, bool isGround);

    // 0x26 Map Chunk Bulk — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeMapChunkBulk(int16_t chunkX, int16_t chunkZ, uint8_t groundUpToY,
                                            const std::vector<uint8_t> &compressedData);

    // 0x27 Game Info — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeGameInfo(uint8_t gamemode, int32_t dimension, uint8_t difficulty);

    // 0x28 Set Cool Down — Play state.
    // Added: 1.6.2 (proto 30). Still current.
    std::vector<uint8_t> encodeSetCoolDown(int16_t slotId, int16_t cooldownTicks);

    // 0x29 Succeeding Burst — Play state.
    // Added: 1.7.2 (proto 40). Still current.
    std::vector<uint8_t> encodeSucceedingBurst(double x, double y, double z);

    // 0x2A Set Center Chunk — Play state.
    // Added: 1.7.2 (proto 40). Still current.
    std::vector<uint8_t> encodeSetCenterChunk(int16_t centerChunkX, int16_t centerChunkZ);

    // 0x2B Sign Edit — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeSignEdit(int32_t blockId, int32_t x, int32_t y, int32_t z);

    // 0x2C Set Title — Play state.
    // Added: 1.8 (proto 47). Still current.
    std::vector<uint8_t> encodeSetTitle(const std::string &title, const std::string &subtitle,
                                        int16_t fadeInTicks, int16_t stayTicks, int16_t fadeOutTicks);

    // 0x2D Open Window — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeOpenWindow(uint8_t windowId, uint8_t windowType,
                                          const std::string &title, int16_t slotCount);

    // 0x2E Close Window — Play state.
    // Added: 1.0 (proto 22). Still current.
    std::vector<uint8_t> encodeCloseWindow(uint8_t windowId);

    // 0x30 Window Items — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeWindowItems(int16_t stateId);

    // 0x31 Set Slot — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeSetSlot(uint8_t slotId, int16_t itemId, uint8_t count, int16_t damage);

    // 0x32 Confirm Transaction — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeConfirmTransaction(uint8_t windowId, int16_t stateId, bool accepted);

    // 0x33 Player Abilities — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodePlayerAbilities(uint8_t flags, float flyingSpeed, float boundingBoxHeight);

    // 0x34 Tab List Remove — Play state.
    // Added: 1.7.2 (proto 40). Still current.
    std::vector<uint8_t> encodeTabListRemove(const uint8_t (&uuid)[16]);

    // 0x35 Set Default Spawn — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeSetDefaultSpawn(int32_t x, int32_t y, int32_t z);

    // 0x36 Carried Minecart — Play state.
    // Added: 1.4 (proto 13). REMOVED in 1.9 (proto 107).
    std::vector<uint8_t> encodeCarriedMinecart(int32_t vehicleId);

    // 0x37 Player Time — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodePlayerTime(int64_t timeOfDay);

    // 0x38 Update Player Info — Play state.
    // Added: 1.7.2 (proto 40). Still current.
    std::vector<uint8_t> encodeUpdatePlayerInfo(const uint8_t (&uuid)[16], uint8_t gamemode,
                                                int16_t ping, const std::string &name);

    // 0x39 Update Block Entity — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeUpdateBlockEntity(int32_t blockId, int32_t x, int32_t y, int32_t z, uint8_t type);

    // 0x3A Set Particle — Play state.
    // Added: 1.4 (proto 13). Still current.
    std::vector<uint8_t> encodeSetParticle(uint8_t particleId, double x, double y, double z,
                                           uint8_t offsetY, float speed, int32_t count);

    // 0x3B Scoreboard Objective — Play state.
    // Added: 1.4.7 (proto 25). Still current.
    std::vector<uint8_t> encodeScoreboardObjective(const std::string &name, uint8_t actionId,
                                                   const std::string &displayName,
                                                   const std::string &renderType);

    // 0x3C Update Score — Play state.
    // Added: 1.4.7 (proto 25). Still current.
    std::vector<uint8_t> encodeUpdateScore(const std::string &name, const std::string &objectiveName, int16_t score);

    // 0x3D Display Scoreboard — Play state.
    // Added: 1.4.7 (proto 25). Still current.
    std::vector<uint8_t> encodeDisplayScoreboard(uint8_t position, const std::string &objectiveName);

    // 0x42 Combat Event — Play state.
    // Added: 1.8 (proto 47). Still current.
    std::vector<uint8_t> encodeCombatEvent(int32_t eventId);

    // 0x43 Camera — Play state.
    // Added: 1.8 (proto 47). Still current.
    std::vector<uint8_t> encodeCamera(int32_t cameraId);

    // 0x44 World Border — Play state.
    // Added: 1.8 (proto 47). Still current.
    std::vector<uint8_t> encodeWorldBorder(int32_t actionId);

    // 0x47 Player List Header And Footer — Play state.
    // Added: 1.8 (proto 47). Still current.
    std::vector<uint8_t> encodePlayerListHeaderFooter(const std::string &header, const std::string &footer);

    // 0x48 Resource Pack — Play state.
    // Added: 1.8 (proto 47). Still current.
    std::vector<uint8_t> encodeResourcePack(const std::string &url, const std::string &hash);

    // =====================================================================
    // Registry: build a protocol-47 S2C payload for a given PacketKind.
    // Returns an empty vector if the kind has no codec (unknown / C2S).
    // The wire ID is NOT included; callers prepend it via BaseProtocol::encodeFrame.
    // =====================================================================
    std::vector<uint8_t> encodeForKind(PacketKind kind);

} // namespace LightweightMC::Network::Protocol