#pragma once
#include "storage/WorldStorage.hpp"
#include "player/PlayerSession.hpp"
#include "managers/ScoreboardManager.hpp"
#include <cstdint>

namespace LightweightMC::World
{
    class WorldManager
    {
    public:
        explicit WorldManager(Storage::WorldStorage &storage) : m_worldStorage(storage) {}

        // Generates and sends a chunk packet in flat format.
        void sendFlatChunk(int clientFd, int chunkX, int chunkZ, LightweightMC::Player::PlayerSession &session, const Managers::PacketSender &sendPacket);

        // EXPERIMENTAL TEST (1.8+ only): sends all not-yet-loaded chunks around the player in a
        // single Map Chunk Bulk packet (S2C 0x26) instead of one Chunk (0x21) per chunk. Used to
        // verify whether the client renders terrain from the bulk format. The 0x21 path is
        // currently disabled (see sendFlatChunk); re-enable it if this does not work.
        void sendMapChunkBulk(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket);

        // Updates the surrounding chunks around the player based on their render distance.
        void updateChunksAroundPlayer(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket);

    private:
        Storage::WorldStorage &m_worldStorage;
    };
}