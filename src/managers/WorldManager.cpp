#include "managers/WorldManager.hpp"
#include "network/Packet.hpp"
#include "network/protocol/ProtocolTranslator.hpp"
#include <cmath>
#include <cstdio>
#include <zlib.h>
#include <chrono>
#include <thread>

namespace LightweightMC::World
{
    void WorldManager::sendFlatChunk(int clientFd, int chunkX, int chunkZ, LightweightMC::Player::PlayerSession &session, const Managers::PacketSender &sendPacket)
    {
        // Session-aware storage selection: in multiworld mode this caller operates
        // on the WorldStorage instance matching session.worldId (0=Overworld,
        // 1=Nether, 2=End, 100+ custom). Falls back to the primary plane when the
        // session hasn't been routed yet.
        Storage::WorldStorage &worldStorage = getWorldStorageFor(session.worldId);
        auto blocks = worldStorage.getChunkBlocks(chunkX, chunkZ);
        (void)clientFd;
        (void)sendPacket;
        // 1.7.10 (protocol < 47) and 1.8+ (protocol >= 47) use different Chunk packet layouts:
        //   * 1.7.10 S2C 0x21: chunkX:int, chunkZ:int, isGroundUp:byte, blocks[4096] (big-endian u16),
        //                       blockLight[2048], skyLight[2048]. No bitmask, no VarInt length.
        //   * 1.8+    S2C 0x21: chunkX:int, chunkZ:int, groundUp:byte, sectionBitmask:short,
        //                       dataLen:VarInt, then the concatenated section payloads.
        const bool legacy = session.clientProtocol < 47;

        // Full modern height: sections covering Y = MIN_WORLD_Y .. MAX_WORLD_Y.
        // Vanilla 1.8 section payload = blocks[8192] + blockLight[2048] + skyLight[2048]
        // = 12288 bytes per section (biomes are NOT per-section; they appear once at the end
        // of the whole chunk's concatenated data blob). Writing biomes per-section (which the
        // pre-fix code did) drifted every subsequent section by 256 bytes on the wire and
        // produced the "level 2 shows level 1's data" symptom the user reported.
        const int NUM_SECTIONS = Storage::NUM_SECTIONS;
        const size_t SECTION_PAYLOAD = 4096 * 2 + 2048 + 2048; // blocks + blockLight + skyLight (no biomes)

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

            // Light travels inside the section payload (0.5 octets per block for blockLight,
            // full-bright 0xFF for skyLight -- we do not compute real lighting). Biomes do
            // NOT go here: vanilla 1.8 puts them once at the end of the whole chunk.
            for (size_t i = 0; i < 2048; ++i)
                payload.push_back(0x00); // Block Light
            for (size_t i = 0; i < 2048; ++i)
                payload.push_back(0xFF); // Sky Light

            if (nonEmpty)
            {
                sectionBitmask |= static_cast<uint16_t>(1u << s); // bit s marks section s present
            }

            // VARIABLE-SLOT for 1.8+: only sections with a bitmask bit are on the wire.
            // LEGACY (1.7.10): always emit the bottom section (bitmask is absent on the wire,
            // the client reads exactly 12,288 bytes as section 0 regardless of emptiness).
            if ((!legacy && nonEmpty) || (legacy && s == 0))
                sectionPayloads.insert(sectionPayloads.end(), payload.begin(), payload.end());
        }

        // Vanilla 1.8 appends 256 biome bytes once at the very end of the whole chunk payload
        // (only when bitmask != 0). These are read by the client only in ground-up mode but the
        // bytes are required for correct framing: omitting them makes the following chunk's header
        // (or EOF) misread as biomes, which is what crashed the JVM 1.8 client when sendChunkBulkSingle
        // was active.
        if (!legacy && sectionBitmask != 0)
        {
            for (size_t i = 0; i < 256; ++i)
                sectionPayloads.push_back(1); // Biome (unused by client but required for framing)
        }

        std::vector<uint8_t> blockData;
        if (legacy)
        {
            // 1.7.10: no bitmask and no VarInt length -- only the bottom section's data follows directly.
            const size_t skyLightEnd = 4096 * 2 + 2048 + 2048;
            blockData.insert(blockData.end(), sectionPayloads.begin(),
                             sectionPayloads.begin() + static_cast<std::ptrdiff_t>(skyLightEnd));
        }
        else
        {
            // 1.8+: vanilla servers (java mcsources, PrismarineJS, SputnikMC, minecraft-server, ...)
            // DEFLATE-compress the whole section payload + biomes blob and prefix it with a varint
            // of the compressed size. Raw uncompressed data would make the JVM InflateReader mis-parse
            // every section boundary -- that is exactly the "level 2 = level 1's data" symptom.
            uLongf bound = compressBound(static_cast<uLong>(sectionPayloads.size()));
            std::vector<uint8_t> deflated(static_cast<size_t>(bound));
            uLongf destLen = bound;
            int rc = compress2(
                deflated.data(), // dest
                &destLen,        // in/out: actual compressed size
                sectionPayloads.data(),
                static_cast<uLong>(sectionPayloads.size()),
                6 /* zlib default level */);
            if (rc != Z_OK)
            {
                fprintf(stderr, "[WorldManager] zlib compress2 failed: rc=%d\n", rc);
                deflated = std::move(sectionPayloads);
            }
            else
            {
                deflated.resize(static_cast<size_t>(destLen));
            }
            blockData = std::move(deflated);
        }

        // vanilla-1.8 empty-chunk skip. Java's readChunk (S2C 0x21) reads x/z/groundUp/mask
        // and then short-circuits when sectionBitmask == 0: it does NOT consume the
        // varint length or the DEFLATE blob that follows. Every air chunk we sent a
        // tail for left ~15 bytes of garbage in the stream, which desynchronised
        // every subsequent 0x21 and made the client render the entire rest of the
        // render box as void. Legacy (1.7.10) has no mask/varint-length header at
        // all, so this only applies to protocol >= 47.
        if (!legacy && sectionBitmask == 0)
        {
            session.loadedChunks.insert({chunkX, chunkZ});
            return;
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
        sendPacket(clientFd, 0x21, payload);
        session.loadedChunks.insert({chunkX, chunkZ});
    }

    // Streaming variant of Map Chunk Bulk: emit ONE 0x26 packet per chunk so no
    // single packet body ever approaches the 21-bit wire length cap (2,097,151
    // bytes) enforced by NetworkFraming::sendPacket. Wire format matches the
    // 1.8 MapChunkBulk contract:
    //   byte   skyLightSent
    //   varint chunkCount (= 1 for a single-chunk bulk)
    //   per chunk: int chunkX, int chunkZ, unsigned short sectionBitmask
    //   per chunk: concatenated section payloads in ascending section order (one
    //              section payload per set bit, no per-chunk length prefix).
    // Body size for one chunk: skyLight(1) + chunkCount(1) + meta(4+4+2) + sum(non-empty
    // sections) <= up to 16 * 12,288 = ~196,608 bytes -- a ~10x margin below the cap.
    //
    // This replaces the earlier monolithic sendMapChunkBulk call in updateChunksAroundPlayer
    // (which packed every not-yet-loaded chunk within the render distance into a single
    // body and blew the wire cap for 11x11 render boxes).
    void WorldManager::sendChunkBulkSingle(int clientFd, int chunkX, int chunkZ,
                                           LightweightMC::Player::PlayerSession &session,
                                           const Managers::PacketSender &sendPacket)
    {
        Storage::WorldStorage &worldStorage = getWorldStorageFor(session.worldId);
        auto blocks = worldStorage.getChunkBlocks(chunkX, chunkZ);

        const int NUM_SECTIONS = Storage::NUM_SECTIONS;
        const bool skyLightSent = true; // Overworld includes SkyLight.

        auto translateBlock = [&](uint16_t rawBlock) -> uint16_t
        {
            if (rawBlock == 0)
                return 0;
            uint16_t blockId = rawBlock >> 4;
            uint8_t meta = static_cast<uint8_t>(rawBlock & 0x0F);
            auto translated = Network::Protocol::ProtocolTranslator::translateBlockToClient(session.clientProtocol, blockId, meta);
            return translated.toCombinedData();
        };

        // Static endianness check (see sendFlatChunk note).
        static const bool kEndianOk = []
        {
            bool ok = Network::Packet::chunkU16RoundTripConsistent();
            if (!ok)
                fprintf(stderr, "[WorldManager] WARNING: chunk block-data byte order is inconsistent; "
                                "blocks may render as air\n");
            return ok;
        }();
        (void)kEndianOk;

        uint16_t sectionBitmask = 0;
        std::vector<uint8_t> chunkPayload;
        chunkPayload.reserve(NUM_SECTIONS * (4096 * 2 + 2048 + 2048));

        for (int s = 0; s < NUM_SECTIONS; ++s)
        {
            const int sectionBaseY = Storage::MIN_WORLD_Y + s * 16;

            std::vector<uint16_t> sectionBlocks(4096, 0);
            bool nonEmpty = false;

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
                continue; // Empty sections are not on the wire (bitmask omits them).

            sectionBitmask |= static_cast<uint16_t>(1u << s);

            for (uint16_t b : sectionBlocks)
                Network::Packet::writeU16BE(chunkPayload, b);

            // BlockLight (0.5 octet per block = 2048 octets) -- server sends neutral 0x00.
            chunkPayload.insert(chunkPayload.end(), 2048, 0x00);

            // SkyLight (2048 octets) when the leading byte said so -- server sends full bright 0xFF.
            if (skyLightSent)
                chunkPayload.insert(chunkPayload.end(), 2048, 0xFF);
        }

        if (sectionBitmask == 0)
        {
            // Fully-empty chunk: still mark it as loaded so we never re-emit it, but skip
            // the packet send (an all-zero-mask bulk body is a valid empty payload but
            // wastes a wire frame; vanilla servers commonly omit these).
            session.loadedChunks.insert(ChunkPos{chunkX, chunkZ});
            return;
        }

        // Vanilla 1.8 appends 256 biome bytes once at the end of every chunk's payload in
        // the Map Chunk Bulk body (only when the chunk's bitmask has at least one bit set).
        // Omitting them makes the JVM client read the NEXT chunk's `int x, int z, short mask`
        // header (or hit EOF for the last chunk) as if they were biomes -- which is exactly
        // the "weird error / client crashes" the user reported for sendChunkBulkSingle.
        for (size_t i = 0; i < 256; ++i)
            chunkPayload.push_back(1); // Biome

        std::vector<uint8_t> packet;
        // Vanilla 1.8 Map Chunk Bulk body layout (java mcsources / PrismarineJS / packetcraft /
        // V0.0.5 monolithic build that "loaded correctly" per CHANGELOG 0.0.7):
        //   byte   hasSkyLight           <-- ONE outer byte at body position 0. Vanilla java's
        //                                    readMapChunkBulk reads this at position 0 as
        //                                    skyLightSent (1 = 12 288-byte sections incl. skyLight,
        //                                    0 = 10 240-byte sections without skyLight).
        //   varint chunkCount
        //   [chunk] chunkCount times, each one (NO per-chunk skylight byte):
        //       int x, int z
        //       short sectionMask          (bit s = section s, big-endian 16-bit)
        //       variable-slot section payloads in ascending order, one 12 288-byte (or 10 240
        //       when !hasSkyLight) payload per set bit
        //       byte[256] biomes
        // The prior version pushed a 0-byte "compressLevel" header (which vanilla has no notion
        // of) AND a per-chunk hasSkyLight byte (which vanilla reads only once at body position 0,
        // not per chunk). Those two extra bytes against vanilla's one outer skylight byte
        // produced the "1 extra byte" the user reported on packet 38 (0x26) Map Chunk Bulk.
        // Removing the per-chunk byte and switching the outer byte from compressLevel=0 to
        // hasSkyLight=1 (overworld always sends skylight) puts the wire at the vanilla width
        // and -- crucially -- signals vanilla to use the 12 288-byte section width, matching
        // the sections we actually emit.
        packet.push_back(skyLightSent ? 1 : 0);  // outer hasSkyLight byte (1 = overworld)
        Network::Packet::writeVarInt(packet, 1); // chunkCount (single chunk)
        Network::Packet::writeInt(packet, chunkX);
        Network::Packet::writeInt(packet, chunkZ);
        Network::Packet::writeShort(packet, static_cast<int16_t>(sectionBitmask));
        packet.insert(packet.end(), chunkPayload.begin(), chunkPayload.end());

        sendPacket(clientFd, 0x26, packet);
        session.loadedChunks.insert(ChunkPos{chunkX, chunkZ});
    }

    // EXPERIMENTAL TEST (1.8+ only): build ONE Map Chunk Bulk packet (S2C 0x26) carrying every
    // not-yet-loaded chunk within renderDistance of the player and send it in a single shot.
    //
    // 1.8 wire format (matches include/protocol/PrismarineJS/protocol_types.hpp MapChunkBulk):
    //   skyLight:bool (leading byte), chunkCount:VarInt,
    //   then chunkCount metas in order: chunkX:int, chunkZ:int, sectionBitmask:unsigned short,
    //   then chunkCount payloads in the SAME order (no length prefix per chunk -- the client
    //   derives payload size from each chunk's sectionBitmask).
    //   Each section payload is blocks[8192] (u16 BE) + blockLight[2048] [+ skyLight[2048] if
    //   the leading skyLight byte was true]. Ground-up mode appends 256 biome bytes at the
    //   end of each chunk's payload.
    //
    // History note (why we are monolithic again):
    //   A later streaming rewrite (V0.0.6 -> V0.0.7 in progress, git 2ba9255) split this into
    //   several consecutive <= 1 MiB 0x26 bodies to keep server RAM bounded. That split broke
    //   client loading: the JVM 1.8 protocol accepts at most one Map Chunk Bulk per world-load
    //   stage, so subsequent split bodies were silently dropped or desynchronised the stream.
    //   The pre-RAM monolithic build from V0.0.5 (git 9620a09) is the state the client actually
    //   expects -- pair chunks generated heights, impair chunks rendered air up to y=80 (that
    //   was the cosmetic bug observed at that stage because the active preset populated only
    //   the bottom five sections), but *every* chunk was received and the world loaded fully.
    //
    //   Restore below is the V0.0.5 monolithic build with the multiworld-aware accessor
    //   getWorldStorageFor(session.worldId) instead of the pre-multiworld m_worldStorage
    //   reference so Nether / End / Aether sessions still read from the correct plane.
    //
    // RAM / wire-length tradeoff:
    //   For a 7x7 view (renderDistance=3) with 5 populated sections per chunk the packet body
    //   is ~3 MB. NetworkFraming::sendPacket silently drops any body > 2,097,151 bytes (the
    //   21-bit length field), so keep renderDistance and populated section count modest, or
    //   move back to a streaming variant once the client's bulk-loading semantics are fixed.
    //
    // Archived (kept for reference): the pre-V0.0.6 monolithic bulk sender. Packs EVERY
    // not-yet-loaded chunk within the render box into one 0x26 body. Works fine for small
    // render distances (tested up to 2 by the user), but blows the 21-bit (2,097,151-byte)
    // wire cap enforced by NetworkFraming::sendPacket at render distance >= 3, so the
    // wire layer silently drops the body and the world "stalls" past the initial view.
    // The active sendMapChunkBulk below streams ONE 0x26 body per chunk instead (matching
    // how vanilla's Java jar server behaves per Wireshark traces).
    void WorldManager::sendMapChunkBulkMonolithic(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket)
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

                // Multiworld-aware storage lookup so Nether / End / Aether sessions
                // read from the correct plane.
                auto blocks = getWorldStorageFor(session.worldId).getChunkBlocks(cx, cz);

                ChunkBlob blob;
                blob.x = cx;
                blob.z = cz;

                // Per-chunk staging buffers (V0.0.5 layout): we accumulate the
                // concatenation of every non-empty section's blocks + blockLight
                // [+ skyLight] and then flatten into blob.data at the end.
                std::vector<uint8_t> blocksBuffer;
                std::vector<uint8_t> blockLightBuffer;
                std::vector<uint8_t> skyLightBuffer;

                for (int s = 0; s < NUM_SECTIONS; ++s)
                {
                    const int sectionBaseY = Storage::MIN_WORLD_Y + s * 16;

                    // Fresh zeroed scratch PER SECTION -- never reuse across
                    // sections or ghost values from the previous section leak
                    // into this one and the client renders them as solid blocks.
                    std::vector<uint16_t> sectionBlocks(4096, 0);
                    bool nonEmpty = false;

                    // 1. Extract + verify this section (16x16x16, Y->Z->X).
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
                        continue; // Skip entirely-empty sections.

                    // 2. Mark the section in the bitmask.
                    blob.bitmask |= static_cast<uint16_t>(1u << s);

                    // 3. Append blocks (u16 big-endian).
                    for (uint16_t b : sectionBlocks)
                        Network::Packet::writeU16BE(blocksBuffer, b);

                    // 4. Append BlockLight (0.5 octet per block = 2048 octets).
                    blockLightBuffer.insert(blockLightBuffer.end(), 2048, 0x00);

                    // 5. Append SkyLight when the leading byte said so.
                    if (skyLightSent)
                        skyLightBuffer.insert(skyLightBuffer.end(), 2048, 0xFF);
                }

                // 6. Final chunk payload layout: blocks -> blockLight -> skyLight -> biomes.
                blob.data.insert(blob.data.end(), blocksBuffer.begin(), blocksBuffer.end());
                blob.data.insert(blob.data.end(), blockLightBuffer.begin(), blockLightBuffer.end());
                if (skyLightSent)
                    blob.data.insert(blob.data.end(), skyLightBuffer.begin(), skyLightBuffer.end());

                // Ground-up mode appends 256 biome bytes (ID 1 = Plains).
                blob.data.insert(blob.data.end(), 256, 1);

                chunks.push_back(std::move(blob));
            }
        }

        if (chunks.empty())
            return;

        // Build the 0x26 (Map Chunk Bulk) packet: skyLight + chunkCount + all metas + all payloads.
        std::vector<uint8_t> packet;
        packet.push_back(skyLightSent ? 1 : 0);                                    // leading skyLightSent byte
        Network::Packet::writeVarInt(packet, static_cast<int32_t>(chunks.size())); // chunkCount

        // Chunk metadata headers (in the same order as the payloads below).
        for (const auto &blob : chunks)
        {
            Network::Packet::writeInt(packet, blob.x);
            Network::Packet::writeInt(packet, blob.z);
            Network::Packet::writeShort(packet, blob.bitmask);
        }

        // Concatenated chunk payloads, in the same order as the metas above.
        for (const auto &blob : chunks)
            packet.insert(packet.end(), blob.data.begin(), blob.data.end());

        sendPacket(clientFd, 0x26, packet);

        // Mark every emitted chunk as loaded so it is not re-sent.
        for (const auto &blob : chunks)
            session.loadedChunks.insert({blob.x, blob.z});
    }

    // Active 1.8 streaming bulk sender: emit ONE 0x26 (Map Chunk Bulk) packet per not-yet-
    // loaded chunk inside the render box, in the same outer-body layout as the monolithic
    // build above (byte hasSkyLight at body position 0 + varint chunkCount + metas +
    // payloads) but with chunkCount pinned to 1. Vanilla Java's jar server does exactly
    // this per Wireshark traces of the shipped client, and this form keeps every packet
    // body under the 21-bit (2,097,151-byte) wire-length cap that NetworkFraming::sendPacket
    // enforces -- the monolithic build above blows that cap at render distance >= 3 and
    // the wire layer silently drops the whole body, which is why the world "stalled" past
    // the initial view for the user.
    void WorldManager::sendMapChunkBulk(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket)
    {
        if (session.clientProtocol < 47)
            return;

        const int playerChunkX = static_cast<int>(std::floor(session.x / 16.0));
        const int playerChunkZ = static_cast<int>(std::floor(session.z / 16.0));

        const int NUM_SECTIONS = Storage::NUM_SECTIONS;
        const bool skyLightSent = true; // Overworld always ships SkyLight.

        // Hoisted so the multiworld accessor is called once per send, not per
        // chunk; also fixes an implicit assumption that session.worldId is
        // stable inside this loop (which it is -- see PlayerHandler.cpp where
        // updateChunksAroundPlayer is invoked only on chunk crossings while
        // worldId is only mutated by explicit /gotoR or /world set commands
        // that clear loadedChunks and re-enter through this function's next
        // call).
        auto &worldStorage = getWorldStorageFor(session.worldId);

        // Login burst detection: the very first updateChunksAroundPlayer on a
        // session (right after PlayerManager::sendPlayPackets logs a client in)
        // runs the pre-refactor sync path -- it uses getChunkBlocks(), which
        // internally calls generateChunk() when the DB blob is missing. That
        // costs ~1-3ms per chunk on the socket thread, but only fires once per
        // login and matches what the user has already accepted as an
        // acceptable login hitch. Every subsequent crossing goes through the
        // async prefetch path (below) so movement -- the reported lag source
        // at high render distances -- stops blocking the send loop.
        const bool loginBurst = session.loadedChunks.empty();

        auto translateBlock = [&](uint16_t rawBlock) -> uint16_t
        {
            if (rawBlock == 0)
                return 0;
            uint16_t blockId = rawBlock >> 4;
            uint8_t meta = static_cast<uint8_t>(rawBlock & 0x0F);
            auto translated = Network::Protocol::ProtocolTranslator::translateBlockToClient(session.clientProtocol, blockId, meta);
            return translated.toCombinedData();
        };
        // Streaming emission: one 0x26 (Map Chunk Bulk) body per not-yet-loaded chunk,
        // matching vanilla Java's server behaviour (Wireshark traces of the shipped jar).
        for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; ++cx)
        {
            for (int cz = playerChunkZ - renderDistance; cz <= playerChunkZ + renderDistance; ++cz)
            {
                ChunkPos pos{cx, cz};
                if (session.loadedChunks.find(pos) != session.loadedChunks.end())
                    continue;

                // Multiworld-aware storage: hoisted above the loop (see
                // worldStorage reference above). The chunk-read path branches:
                //   * Login burst  -> legacy sync gen via getChunkBlocks() (a
                //     one-time ~150ms hitch at rd=5 the user has already
                //     accepted). Also enqueues nothing (getChunkBlocks writes
                //     directly to DB).
                //   * Movement     -> isChunkReady() check first; if not ready,
                //     requestChunk() submits it to the async worker and we
                //     skip this chunk this tick (do NOT mark it loaded). The
                //     worker will finish the chunk in the background; the next
                //     updateChunksAroundPlayer call (or a periodic flush, if
                //     the player keeps moving) will ship it.
                std::unordered_map<Storage::BlockCoord, uint16_t, Storage::BlockCoordHash> blocks;
                if (loginBurst)
                {
                    blocks = worldStorage.getChunkBlocks(cx, cz);
                }
                else if (worldStorage.isChunkReady(cx, cz))
                {
                    blocks = worldStorage.getChunkBlocksIfReady(cx, cz);
                }
                else
                {
                    worldStorage.requestChunk(cx, cz);
                    continue;
                }

                uint16_t bitmask = 0;
                std::vector<uint8_t> blocksBuffer;
                std::vector<uint8_t> blockLightBuffer;
                std::vector<uint8_t> skyLightBuffer;
                for (int s = 0; s < NUM_SECTIONS; ++s)
                {
                    const int sectionBaseY = Storage::MIN_WORLD_Y + s * 16;

                    // Fresh zeroed scratch PER SECTION -- never reuse across sections
                    // or ghost values from the previous section leak into this one and
                    // the client renders them as solid blocks.
                    std::vector<uint16_t> sectionBlocks(4096, 0);
                    bool nonEmpty = false;

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
                        continue; // Empty sections are not on the wire (bitmask omits them).

                    bitmask |= static_cast<uint16_t>(1u << s);

                    for (uint16_t b : sectionBlocks)
                        Network::Packet::writeU16BE(blocksBuffer, b);

                    // BlockLight (0.5 octet per block = 2048 octets) -- server sends neutral 0x00.
                    blockLightBuffer.insert(blockLightBuffer.end(), 2048, 0x00);

                    // SkyLight (2048 octets) when the leading byte said so -- server sends full bright 0xFF.
                    if (skyLightSent)
                        skyLightBuffer.insert(skyLightBuffer.end(), 2048, 0xFF);
                }

                // Assemble this chunk's payload: blocks -> blockLight -> skyLight -> biomes.
                std::vector<uint8_t> chunkPayload;
                chunkPayload.reserve(blocksBuffer.size() + blockLightBuffer.size() +
                                     skyLightBuffer.size() + 256);
                chunkPayload.insert(chunkPayload.end(), blocksBuffer.begin(), blocksBuffer.end());
                chunkPayload.insert(chunkPayload.end(), blockLightBuffer.begin(), blockLightBuffer.end());
                chunkPayload.insert(chunkPayload.end(), skyLightBuffer.begin(), skyLightBuffer.end());
                // Ground-up mode appends 256 biome bytes (ID 1 = Plains). Vanilla's readMapChunkBulk
                // reads biomes for every chunk in the second-phase pass REGARDLESS of mask, so we
                // always append them (matching the monolithic build the user verified works at rd 1..2).
                chunkPayload.insert(chunkPayload.end(), 256, 1);

                // Build the single-chunk 0x26 body (chunkCount=1).
                std::vector<uint8_t> packet;
                packet.reserve(chunkPayload.size() + 20);
                packet.push_back(skyLightSent ? 1 : 0);  // outer hasSkyLight byte at body position 0
                Network::Packet::writeVarInt(packet, 1); // chunkCount = 1 (one chunk per packet)
                Network::Packet::writeInt(packet, cx);
                Network::Packet::writeInt(packet, cz);
                Network::Packet::writeShort(packet, static_cast<int16_t>(bitmask));
                packet.insert(packet.end(), chunkPayload.begin(), chunkPayload.end());

                sendPacket(clientFd, 0x26, packet);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(2));
                session.loadedChunks.insert(pos);
            }
        }
    }

    //
    // 1.8 wire format:
    //   x:int, z:int (anchor chunk), groundUp:byte, sectionCount:VarInt (total sections across all
    //   chunks), then per chunk: chunkX:short, chunkZ:short, sectionBitmask:short, dataLen:VarInt,
    //   followed by that chunk's non-empty section payloads. Each section payload is the same layout
    //   used by sendFlatChunk: blocks[8192] (big-endian u16) + blockLight[2048] + skyLight[2048].
    /* void WorldManager::sendMapChunkBulk(int clientFd, LightweightMC::Player::PlayerSession &session, int renderDistance, const Managers::PacketSender &sendPacket)
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

        // ---- Prefetch pass (1.8+ clients only) ----
        // Submit a box of chunks to WorldStorage::requestChunk() for off-thread
        // generation. Box = render box + frontPad chunks in the direction of
        // travel + backPad chunks behind; frontPad == renderDistance for
        // moving players, matching the "2x render distance ahead" the user
        // sketched, while backPad stays 1 so we don't waste worker cycles
        // regenerating chunks the client is only leaving the view of.
        //
        // Threads live on WorldStorage (one per plane), NOT per client. See
        // src/storage/WorldStorage.cpp::generationWorkerLoop for the rationale.
        if (session.clientProtocol >= 47)
        {
            auto &worldStorage = getWorldStorageFor(session.worldId);
            const bool loginBurst = (session.prevChunkX == std::numeric_limits<std::int32_t>::min());
            const int dx = loginBurst
                               ? 0
                               : (playerChunkX > session.prevChunkX ? 1
                                   : (playerChunkX < session.prevChunkX ? -1 : 0));
            const int dz = loginBurst
                               ? 0
                               : (playerChunkZ > session.prevChunkZ ? 1
                                   : (playerChunkZ < session.prevChunkZ ? -1 : 0));
            const int frontPad = (dx != 0 || dz != 0) ? renderDistance : 0;
            const int backPad  = (dx != 0 || dz != 0) ? 1 : 0;
            const int minX = playerChunkX - renderDistance - (dx < 0 ? frontPad : backPad);
            const int maxX = playerChunkX + renderDistance + (dx > 0 ? frontPad : backPad);
            const int minZ = playerChunkZ - renderDistance - (dz < 0 ? frontPad : backPad);
            const int maxZ = playerChunkZ + renderDistance + (dz > 0 ? frontPad : backPad);
            for (int cx = minX; cx <= maxX; ++cx)
            {
                for (int cz = minZ; cz <= maxZ; ++cz)
                {
                    worldStorage.requestChunk(cx, cz);
                }
            }
        }

        // Emission path.
        //   * 1.8+ clients: call the streaming sendMapChunkBulk below, which
        //     walks the render box and emits ONE 0x26 (Map Chunk Bulk) packet
        //     per chunk (chunkCount=1) -- the same shape vanilla Java's jar
        //     server emits per Wireshark. Chunks not yet in the DB are skipped
        //     this tick (sendMapChunkBulk's per-chunk ready check), but were
        //     just requestChunk()'d above so the async worker will finish them
        //     shortly; the following crossing ships them.
        //     The old monolithic variant that packed every not-yet-loaded
        //     chunk within the render box into a single 0x26 body blew the
        //     21-bit wire cap at renderDistance >= 3 and the wire layer
        //     silently dropped the body, so it's archived as
        //     sendMapChunkBulkMonolithic for reference only.
        //   * Legacy (<1.8, protocol < 47): one 0x21 (Chunk) packet per chunk
        //     in the box.
        if (session.clientProtocol >= 47)
        {
            sendMapChunkBulk(clientFd, session, renderDistance, sendPacket);
        }
        else
        {
            for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; ++cx)
            {
                for (int cz = playerChunkZ - renderDistance; cz <= playerChunkZ + renderDistance; ++cz)
                {
                    ChunkPos pos{cx, cz};
                    if (session.loadedChunks.find(pos) != session.loadedChunks.end())
                        continue;
                    sendFlatChunk(clientFd, cx, cz, session, sendPacket);
                }
            }
        }

        session.currentChunkX = playerChunkX;
        session.currentChunkZ = playerChunkZ;
        session.prevChunkX = playerChunkX;
        session.prevChunkZ = playerChunkZ;
    }
}