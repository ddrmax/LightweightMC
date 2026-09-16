#include "managers/WorldManager.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include <cmath>

namespace LightweightMC::World
{
    void WorldManager::sendFlatChunk(int clientFd, int chunkX, int chunkZ, LightweightMC::Player::PlayerSession &session, const Managers::PacketSender &sendPacket)
    {
        auto blocks = m_worldStorage.getChunkBlocks(chunkX, chunkZ);

        std::vector<uint8_t> blockData;
        blockData.reserve(16 * 16 * 16 * 2); // 16 sections * 4096 blocks * 2 bytes

        // Reconstruct section 0 (Y = 0 to 15)
        for (int y = 0; y < 16; ++y)
        {
            for (int z = 0; z < 16; ++z)
            { // Minecraft Indexing: Y -> Z -> X
                for (int x = 0; x < 16; ++x)
                {
                    uint16_t blockCombined = 0;

                    Storage::BlockCoord coord{x, y, z};
                    auto it = blocks.find(coord);
                    if (it != blocks.end())
                    {
                        uint16_t rawBlock = it->second;
                        uint16_t blockId = rawBlock >> 4;
                        uint8_t meta = rawBlock & 0x0F;

                        // Multi-version translator (ViaVersion preparation)
                        auto translated = Network::Protocol::ProtocolTranslator::translateBlockToClient(47, blockId, meta);
                        blockCombined = translated.toCombinedData();
                    }

                    blockData.push_back(static_cast<uint8_t>(blockCombined & 0xFF));
                    blockData.push_back(static_cast<uint8_t>((blockCombined >> 8) & 0xFF));
                }
            }
        }

        // Light & Biomes
        for (int i = 0; i < 2048; ++i)
            blockData.push_back(0x00); // Block Light
        for (int i = 0; i < 2048; ++i)
            blockData.push_back(0xFF); // Sky Light
        for (int i = 0; i < 256; ++i)
            blockData.push_back(1); // Biome

        std::vector<uint8_t> payload;
        Network::Packet::writeInt(payload, chunkX);
        Network::Packet::writeInt(payload, chunkZ);
        payload.push_back(1);                        // Ground-Up continuous
        Network::Packet::writeShort(payload, 0x0001); // Bitmask section 0
        Network::Packet::writeVarInt(payload, static_cast<int32_t>(blockData.size()));
        payload.insert(payload.end(), blockData.begin(), blockData.end());

        sendPacket(clientFd, 0x21, payload);
        session.loadedChunks.insert({chunkX, chunkZ});
    }

    void WorldManager::updateChunksAroundPlayer(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket)
    {
        int playerChunkX = static_cast<int>(std::floor(session.x / 16.0));
        int playerChunkZ = static_cast<int>(std::floor(session.z / 16.0));

        // Generate and send all chunks within view distance that haven't been transmitted yet
        for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; ++cx)
        {
            for (int cz = playerChunkZ - renderDistance; cz <= playerChunkZ + renderDistance; ++cz)
            {
                ChunkPos pos{cx, cz};
                if (session.loadedChunks.find(pos) == session.loadedChunks.end())
                {
                    sendFlatChunk(clientFd, cx, cz, session, sendPacket);
                }
            }
        }

        session.currentChunkX = playerChunkX;
        session.currentChunkZ = playerChunkZ;
    }
}