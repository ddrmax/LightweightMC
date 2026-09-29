#include "managers/WorldManager.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include <cmath>
#include <cstdio>

namespace LightweightMC::World
{
    void WorldManager::sendFlatChunk(int clientFd, int chunkX, int chunkZ, LightweightMC::Player::PlayerSession &session, const Managers::PacketSender &sendPacket)
    {
        auto blocks = m_worldStorage.getChunkBlocks(chunkX, chunkZ);

        // 1.7.10 (protocol < 47) and 1.8+ (protocol >= 47) use different Chunk packet layouts:
        //   * 1.7.10 S2C 0x21: chunkX:int, chunkZ:int, isGroundUp:byte, blocks[4096] (big-endian u16),
        //                       blockLight[2048], skyLight[2048]. No bitmask, no VarInt length.
        //   * 1.8+    S2C 0x21: chunkX:int, chunkZ:int, groundUp:byte, sectionBitmask:short,
        //                       dataLen:VarInt, then the concatenated section payloads.
        const bool legacy = session.clientProtocol < 47;

        // Full modern height: 24 sections covering Y = -64 .. 319.
        // Each section payload is blocks[8192] + blockLight[2048] + skyLight[2048] = 12288 bytes.
        const int NUM_SECTIONS = Storage::NUM_SECTIONS;
        const size_t SECTION_PAYLOAD = 4096 * 2 + 2048 + 2048 + 256; // blocks + blockLight + skyLight + biomes

        // Translate a stored (id << 4 | meta) block to the client's combined data, or 0 for air.
        auto translateBlock = [&](uint16_t rawBlock) -> uint16_t
        {
            if (rawBlock == 0)
                return 0;
            uint16_t blockId = rawBlock >> 4;
            uint8_t meta = static_cast<uint8_t>(rawBlock & 0x0F);
            auto translated = Network::Protocol::ProtocolTranslator::translateBlockToClient(session.clientProtocol, blockId, meta);
            return translated.toCombinedData();
        };

        // The chunk block data is always emitted big-endian. Verify once that the write and read
        // sides agree -- a mismatch would make every block decode to a wrong ID on the client and
        // render as air ("no blocks show up").
        static const bool kEndianOk = []
        {
            bool ok = Network::Packet::chunkU16RoundTripConsistent();
            if (!ok)
                fprintf(stderr, "[WorldManager] WARNING: chunk block-data byte order is inconsistent; "
                                "blocks may render as air\n");
            return ok;
        }();
        (void)kEndianOk;

        // Build each section's payload and track which sections are non-empty (bitmask).
        std::vector<uint8_t> sectionPayloads;
        sectionPayloads.reserve(NUM_SECTIONS * SECTION_PAYLOAD);
        uint16_t sectionBitmask = 0;

        for (int s = 0; s < NUM_SECTIONS; ++s)
        {
            const int sectionBaseY = Storage::MIN_WORLD_Y + s * 16; // absolute Y of the section's bottom block

            std::vector<uint8_t> payload;
            payload.reserve(SECTION_PAYLOAD);

            bool nonEmpty = false;

            // Reconstruct this section (16x16x16). Minecraft indexing: Y -> Z -> X.
            for (int y = 0; y < 16; ++y)
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                    {
                        Storage::BlockCoord coord{x, sectionBaseY + y, z};
                        uint16_t blockCombined = 0;
                        auto it = blocks.find(coord);
                        if (it != blocks.end())
                            blockCombined = translateBlock(it->second);

                        if (blockCombined != 0)
                            nonEmpty = true;

                        // Block data is big-endian in the chunk format for every supported version.
                        // writeU16BE fixes the byte order explicitly (see endianness check above),
                        // so this can never silently swap on a differently-ordered host.
                        Network::Packet::writeU16BE(payload, blockCombined);
                    }

            // Light & Biomes (biome data is only read by pre-1.8 clients, but keep the layout uniform).
            for (size_t i = 0; i < 2048; ++i)
                payload.push_back(0x00); // Block Light
            for (size_t i = 0; i < 2048; ++i)
                payload.push_back(0xFF); // Sky Light
            for (size_t i = 0; i < 256; ++i)
                payload.push_back(1); // Biome

            if (nonEmpty)
                sectionBitmask |= static_cast<uint16_t>(1u << s); // bit s selects payload slot s (ascending order)

            sectionPayloads.insert(sectionPayloads.end(), payload.begin(), payload.end());
        }

        std::vector<uint8_t> blockData;
        if (legacy)
        {
            // 1.7.10: no bitmask and no VarInt length -- only the bottom section's data follows directly.
            const size_t skyLightEnd = 4096 * 2 + 2048 + 2048;
            blockData.insert(blockData.end(), sectionPayloads.begin(), sectionPayloads.begin() + static_cast<std::ptrdiff_t>(skyLightEnd));
        }
        else
        {
            // 1.8+: bitmask for all non-empty sections, then a VarInt length prefixing the section payloads.
            blockData = std::move(sectionPayloads);
        }

        std::vector<uint8_t> payload;
        Network::Packet::writeInt(payload, chunkX);
        Network::Packet::writeInt(payload, chunkZ);
        payload.push_back(1); // Ground-Up continuous
        if (legacy)
        {
            // 1.7.10: no bitmask and no VarInt length -- the block data follows directly.
            const size_t skyLightEnd = 4096 * 2 + 2048 + 2048;
            payload.insert(payload.end(), blockData.begin(), blockData.begin() + static_cast<std::ptrdiff_t>(skyLightEnd));
        }
        else
        {
            // 1.8+: bitmask for all non-empty sections, then a VarInt length prefixing the section payloads.
            Network::Packet::writeShort(payload, sectionBitmask);
            Network::Packet::writeVarInt(payload, static_cast<int32_t>(blockData.size()));
            payload.insert(payload.end(), blockData.begin(), blockData.end());
        }

        // EXPERIMENTAL TEST: the 0x21 (Chunk) send is disabled while we test sending chunks via
        // the 0x26 (Map Chunk Bulk) packet -- see sendMapChunkBulk() below. If the bulk format
        // does not work, restore this line and remove the 0x26 call in updateChunksAroundPlayer().
        // sendPacket(clientFd, 0x21, payload);
        session.loadedChunks.insert({chunkX, chunkZ});
    }

    void WorldManager::sendMapChunkBulk(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket)
    {
        if (session.clientProtocol < 47)
            return;

        const int playerChunkX = static_cast<int>(std::floor(session.x / 16.0));
        const int playerChunkZ = static_cast<int>(std::floor(session.z / 16.0));

        const int NUM_SECTIONS = Storage::NUM_SECTIONS;

        auto translateBlock = [&](uint16_t rawBlock) -> uint16_t
        {
            if (rawBlock == 0)
                return 0;
            uint16_t blockId = rawBlock >> 4;
            uint8_t meta = static_cast<uint8_t>(rawBlock & 0x0F);
            auto translated = Network::Protocol::ProtocolTranslator::translateBlockToClient(session.clientProtocol, blockId, meta);
            return translated.toCombinedData();
        };

        struct ChunkBlob
        {
            int x = 0;
            int z = 0;
            uint16_t bitmask = 0;
            std::vector<uint8_t> data;
        };

        std::vector<ChunkBlob> chunks;
        bool skyLightSent = true; // Overworld = true (inclut SkyLight)

        for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; ++cx)
        {
            for (int cz = playerChunkZ - renderDistance; cz <= playerChunkZ + renderDistance; ++cz)
            {
                ChunkPos pos{cx, cz};
                if (session.loadedChunks.find(pos) != session.loadedChunks.end())
                    continue;

                auto blocks = m_worldStorage.getChunkBlocks(cx, cz);

                ChunkBlob blob;
                blob.x = cx;
                blob.z = cz;

                // Buffers temporaires pour ordonner correctement les données du chunk
                std::vector<uint8_t> blocksBuffer;
                std::vector<uint8_t> blockLightBuffer;
                std::vector<uint8_t> skyLightBuffer;

                for (int s = 0; s < NUM_SECTIONS; ++s)
                {
                    const int sectionBaseY = Storage::MIN_WORLD_Y + s * 16;
                    std::vector<uint16_t> sectionBlocks(4096, 0);
                    bool nonEmpty = false;

                    // 1. Extraction et vérification de la section (16x16x16)
                    for (int y = 0; y < 16; ++y)
                        for (int z = 0; z < 16; ++z)
                            for (int x = 0; x < 16; ++x)
                            {
                                Storage::BlockCoord coord{x, sectionBaseY + y, z};
                                auto it = blocks.find(coord);
                                if (it != blocks.end())
                                {
                                    uint16_t blockCombined = translateBlock(it->second);
                                    if (blockCombined != 0)
                                    {
                                        sectionBlocks[y * 256 + z * 16 + x] = blockCombined;
                                        nonEmpty = true;
                                    }
                                }
                            }

                    if (!nonEmpty)
                        continue; // Ignorer les sections entièrement vides !

                    // Marquer la section dans le masque
                    blob.bitmask |= static_cast<uint16_t>(1u << s);

                    // Appendre les blocs (U16 Big-Endian)
                    for (uint16_t b : sectionBlocks)
                    {
                        Network::Packet::writeU16BE(blocksBuffer, b);
                    }

                    // Appendre BlockLight (0.5 octet par bloc = 2048 octets)
                    blockLightBuffer.insert(blockLightBuffer.end(), 2048, 0x00);

                    // Appendre SkyLight si nécessaire (0.5 octet par bloc = 2048 octets)
                    if (skyLightSent)
                    {
                        skyLightBuffer.insert(skyLightBuffer.end(), 2048, 0xFF);
                    }
                }

                // Assemblage final du payload du chunk : Blocs -> BlockLight -> SkyLight -> Biomes
                blob.data.insert(blob.data.end(), blocksBuffer.begin(), blocksBuffer.end());
                blob.data.insert(blob.data.end(), blockLightBuffer.begin(), blockLightBuffer.end());
                if (skyLightSent)
                {
                    blob.data.insert(blob.data.end(), skyLightBuffer.begin(), skyLightBuffer.end());
                }

                // En mode groundUp (1), on ajoute 256 octets de Biomes à la fin du chunk
                blob.data.insert(blob.data.end(), 256, 1); // Biome ID 1 (Plains)

                chunks.push_back(std::move(blob));
            }
        }

        if (chunks.empty())
            return;

        // Construction du paquet 0x26
        std::vector<uint8_t> packet;
        packet.push_back(skyLightSent ? 1 : 0);                                    // skyLightSent (boolean)
        Network::Packet::writeVarInt(packet, static_cast<int32_t>(chunks.size())); // Nombre de chunks!

        // Méta-données des chunks (Header)
        for (const auto &blob : chunks)
        {
            Network::Packet::writeInt(packet, blob.x);
            Network::Packet::writeInt(packet, blob.z);
            Network::Packet::writeShort(packet, blob.bitmask);
        }

        // Octets de données réelles pour l'ensemble des chunks
        for (const auto &blob : chunks)
        {
            packet.insert(packet.end(), blob.data.begin(), blob.data.end());
        }

        sendPacket(clientFd, 0x26, packet);

        // Marquer comme chargés
        for (const auto &blob : chunks)
            session.loadedChunks.insert({blob.x, blob.z});
    }
    /*    // EXPERIMENTAL TEST (1.8+ only): build a single Map Chunk Bulk packet (S2C 0x26) carrying every
        // not-yet-loaded chunk within renderDistance of the player, and send it in one shot.
        //
        // 1.8 wire format:
        //   x:int, z:int (anchor chunk), groundUp:byte, sectionCount:VarInt (total sections across all
        //   chunks), then per chunk: chunkX:short, chunkZ:short, sectionBitmask:short, dataLen:VarInt,
        //   followed by that chunk's non-empty section payloads. Each section payload is the same layout
        //   used by sendFlatChunk: blocks[8192] (big-endian u16) + blockLight[2048] + skyLight[2048].
        void WorldManager::sendMapChunkBulk(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket)
        {
            if (session.clientProtocol < 47)
                return;

            const int playerChunkX = static_cast<int>(std::floor(session.x / 16.0));
            const int playerChunkZ = static_cast<int>(std::floor(session.z / 16.0));

            const int NUM_SECTIONS = Storage::NUM_SECTIONS;
            const size_t SECTION_PAYLOAD = 4096 * 2 + 2048 + 2048 + 256; // blocks + blockLight + skyLight + biomes

            auto translateBlock = [&](uint16_t rawBlock) -> uint16_t
            {
                if (rawBlock == 0)
                    return 0;
                uint16_t blockId = rawBlock >> 4;
                uint8_t meta = static_cast<uint8_t>(rawBlock & 0x0F);
                auto translated = Network::Protocol::ProtocolTranslator::translateBlockToClient(session.clientProtocol, blockId, meta);
                return translated.toCombinedData();
            };

            static const bool kEndianOk = []
            {
                bool ok = Network::Packet::chunkU16RoundTripConsistent();
                if (!ok)
                    fprintf(stderr, "[WorldManager] WARNING: chunk block-data byte order is inconsistent; "
                                    "blocks may render as air\n");
                return ok;
            }();
            (void)kEndianOk;

            struct ChunkBlob
            {
                int x = 0;
                int z = 0;
                uint16_t bitmask = 0;
                std::vector<uint8_t> data;
            };
            std::vector<ChunkBlob> chunks;
            int totalSections = 0;

            for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; ++cx)
            {
                for (int cz = playerChunkZ - renderDistance; cz <= playerChunkZ + renderDistance; ++cz)
                {
                    ChunkPos pos{cx, cz};
                    if (session.loadedChunks.find(pos) != session.loadedChunks.end())
                        continue;

                    auto blocks = m_worldStorage.getChunkBlocks(cx, cz);

                    ChunkBlob blob;
                    blob.x = cx;
                    blob.z = cz;

                    for (int s = 0; s < NUM_SECTIONS; ++s)
                    {
                        const int sectionBaseY = Storage::MIN_WORLD_Y + s * 16; // absolute Y of the section's bottom block

                        std::vector<uint8_t> payload;
                        payload.reserve(SECTION_PAYLOAD);
                        bool nonEmpty = false;

                        // Reconstruct this section (16x16x16). Minecraft indexing: Y -> Z -> X.
                        for (int y = 0; y < 16; ++y)
                            for (int z = 0; z < 16; ++z)
                                for (int x = 0; x < 16; ++x)
                                {
                                    Storage::BlockCoord coord{x, sectionBaseY + y, z};
                                    uint16_t blockCombined = 0;
                                    auto it = blocks.find(coord);
                                    if (it != blocks.end())
                                        blockCombined = translateBlock(it->second);

                                    if (blockCombined != 0)
                                        nonEmpty = true;

                                    // Big-endian, fixed byte order regardless of host endianness.
                                    Network::Packet::writeU16BE(payload, blockCombined);
                                }

                        // Light & Biomes (biome data is only read by pre-1.8 clients, but keep the layout uniform).
                        for (size_t i = 0; i < 2048; ++i)
                            payload.push_back(0x00); // Block Light
                        for (size_t i = 0; i < 2048; ++i)
                            payload.push_back(0xFF); // Sky Light
                        for (size_t i = 0; i < 256; ++i)
                            payload.push_back(1); // Biome

                        if (nonEmpty)
                        {
                            blob.bitmask |= static_cast<uint16_t>(1u << s); // bit s selects payload slot s (ascending order)
                            blob.data.insert(blob.data.end(), payload.begin(), payload.end());
                            totalSections++;
                        }
                    }

                    chunks.push_back(std::move(blob));
                }
            }

            if (chunks.empty())
                return;

            // Build the 0x26 packet.
            std::vector<uint8_t> packet;
            Network::Packet::writeInt(packet, playerChunkX);
            Network::Packet::writeInt(packet, playerChunkZ);
            packet.push_back(1); // groundUp: continuous
            Network::Packet::writeVarInt(packet, totalSections);

            for (const auto &blob : chunks)
            {
                Network::Packet::writeShort(packet, static_cast<int16_t>(blob.x));
                Network::Packet::writeShort(packet, static_cast<int16_t>(blob.z));
                Network::Packet::writeShort(packet, blob.bitmask);
                Network::Packet::writeVarInt(packet, static_cast<int32_t>(blob.data.size()));
                packet.insert(packet.end(), blob.data.begin(), blob.data.end());
            }

            sendPacket(clientFd, 0x26, packet);

            // Mark all sent chunks as loaded so they are not re-sent.
            for (const auto &blob : chunks)
                session.loadedChunks.insert({blob.x, blob.z});
        }
    */
    void WorldManager::updateChunksAroundPlayer(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket)
    {
        int playerChunkX = static_cast<int>(std::floor(session.x / 16.0));
        int playerChunkZ = static_cast<int>(std::floor(session.z / 16.0));

        // EXPERIMENTAL TEST: for 1.8+ clients, send all not-yet-loaded chunks in a single Map Chunk
        // Bulk packet (0x26) instead of one Chunk (0x21) per chunk. Legacy (<47) keeps the per-chunk path.
        if (session.clientProtocol >= 47)
        {
            sendMapChunkBulk(clientFd, session, renderDistance, sendPacket);
            session.currentChunkX = playerChunkX;
            session.currentChunkZ = playerChunkZ;
            return;
        }

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