#include "storage/WorldStorage.hpp"
#include "core/Logger.hpp"
#include "core/ConfigManager.hpp"
#include "storage/WorldGen.hpp"
#include "world/MC17BlockIds.hpp"
#include <lz4.h>
#include <chrono>
#include <iostream>
#include <cstring>
#include <cmath>
#include <random>

namespace LightweightMC::Storage
{
    // Internal (pre-1.13) block IDs, stored as (id << 4 | meta) in the chunk blob
    namespace Blocks
    {
        constexpr uint16_t AIR = 0;
        constexpr uint16_t STONE = 1;
        constexpr uint16_t GRASS_BLOCK = 2;
        constexpr uint16_t DIRT = 3;
        constexpr uint16_t COBBLESTONE = 4;
        constexpr uint16_t BEDROCK = 7;
        constexpr uint16_t SAND = 12;
        constexpr uint16_t WATER = 8;
        constexpr uint16_t LAVA = 10;

        constexpr uint16_t combined(uint16_t id, uint8_t meta = 0) { return static_cast<uint16_t>((id << 4) | (meta & 0x0F)); }
    }
    // Fonction de bruit 2D ultra-rapide sans dépendance externe
    inline float grad(int hash, float x, float z)
    {
        int h = hash & 7;
        float u = h < 4 ? x : z;
        float v = h < 4 ? z : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
    }

    inline float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

    // Dans noise2D, s'assurer que std::floor fonctionne avec des float/double :
    float noise2D(float x, float z, uint32_t seed)
    {
        int X = static_cast<int>(std::floor(x));
        int Z = static_cast<int>(std::floor(z));
        float xf = x - std::floor(x);
        float zf = z - std::floor(z);

        // Masquage 255
        int Xm = X & 255;
        int Zm = Z & 255;
        (void)Xm;
        (void)Zm;
        float u = smoothstep(xf);
        float v = smoothstep(zf);

        auto hash = [seed](int ix, int iz)
        {
            uint32_t h = seed ^ (static_cast<uint32_t>(ix) * 73856093u) ^ (static_cast<uint32_t>(iz) * 19349663u);
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            return h;
        };

        int g00 = hash(X, Z), g10 = hash(X + 1, Z);
        int g01 = hash(X, Z + 1), g11 = hash(X + 1, Z + 1);

        float n00 = grad(g00, xf, zf);
        float n10 = grad(g10, xf - 1.0f, zf);
        float n01 = grad(g01, xf, zf - 1.0f);
        float n11 = grad(g11, xf - 1.0f, zf - 1.0f);

        float nx0 = n00 + u * (n10 - n00);
        float nx1 = n01 + u * (n11 - n01);
        return (nx0 + v * (nx1 - nx0)) * 0.5f + 0.5f;
    } // ---------------------------------------------------------------------------
    // Terrain generation
    // ---------------------------------------------------------------------------
    // Fills a full-chunk buffer (UNCOMPRESSED_CHUNK_SIZE bytes, 24 sections,
    // absY = MIN_WORLD_Y .. MAX_WORLD_Y) with the terrain layers for the given style.
    // The buffer must already be zeroed and sized UNCOMPRESSED_CHUNK_SIZE.
    //
    // worldSeed is the 64-bit world seed. MC-style presets (OVERWORLD, NETHER,
    // AETHER) drive their Perlin/LCG streams entirely from it, so two servers
    // started with the same world-seed produce identical worlds. The
    // SUPERFLAT presets keep their existing noise2D-based layout but fold
    // worldSeed into their hardcoded seed values so /reload is stable.
    void WorldStorage::generateFlatChunk(GenStyle style, std::vector<uint8_t> &rawBuffer, int chunkX, int chunkZ,
                                         std::uint64_t worldSeed)
    {
        // Fully procedural presets delegate to the WorldGen namespace (see
        // storage/WorldGen.hpp + src/storage/WorldGen.cpp). Those routines
        // use 1.7 IDs only and mix worldSeed into every LCG/Perlin stream.
        if (style == GenStyle::OVERWORLD)
        {
            Gen::overworldChunk(chunkX, chunkZ, worldSeed,
                                reinterpret_cast<uint16_t *>(rawBuffer.data()));
            return;
        }
        if (style == GenStyle::NETHER)
        {
            Gen::netherChunk(chunkX, chunkZ, worldSeed,
                             reinterpret_cast<uint16_t *>(rawBuffer.data()));
            return;
        }
        if (style == GenStyle::AETHER)
        {
            Gen::aetherChunk(chunkX, chunkZ, worldSeed,
                             reinterpret_cast<uint16_t *>(rawBuffer.data()));
            return;
        }
        if (style == GenStyle::END)
        {
            Gen::endChunk(chunkX, chunkZ, worldSeed,
                          reinterpret_cast<uint16_t *>(rawBuffer.data()));
            return;
        }
        if (style == GenStyle::CREATIVE)
        {
            Gen::creativeChunk(chunkX, chunkZ, worldSeed,
                               reinterpret_cast<uint16_t *>(rawBuffer.data()));
            return;
        }
        if (style == GenStyle::MINING)
        {
            Gen::miningChunk(chunkX, chunkZ, worldSeed,
                             reinterpret_cast<uint16_t *>(rawBuffer.data()));
            return;
        }
        if (style == GenStyle::TWILIGHT)
        {
            Gen::twilightChunk(chunkX, chunkZ, worldSeed,
                               reinterpret_cast<uint16_t *>(rawBuffer.data()));
            return;
        }

        uint16_t *blocksArray = reinterpret_cast<uint16_t *>(rawBuffer.data());
        // Maps an absolute world Y to the flat index inside the full-chunk buffer.
        auto idx = [](int absY, int z, int x) -> int
        {
            return ((absY - MIN_WORLD_Y) * 256) + (z * 16) + x;
        };

        if (style == GenStyle::TEST_GRID) // deterministic block grid for protocol validation
        {
            auto fillLayer = [&](int absY, uint16_t block)
            {
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                        blocksArray[idx(absY, z, x)] = block;
            };

            // Layout (world coordinates, so the pattern is continuous across chunks):
            //   Y=0        : bedrock
            //   Y=1..4     : stone
            //   Y=5        : dirt
            //   Y=6        : surface — a colored block grid + air gaps.
            // The grid uses easily distinguishable legacy blocks: the X axis alternates
            // wool colors (white/red/green/blue), the Z axis alternates gold/iron, and
            // every 4th cell in both axes is left as air so chunk borders and block
            // placement can be verified visually. Player spawn (0,7,0) sits on the grid.
            fillLayer(0, Blocks::combined(Blocks::BEDROCK));
            for (int y = 1; y <= 4; ++y)
                fillLayer(y, Blocks::combined(Blocks::STONE));
            fillLayer(5, Blocks::combined(Blocks::DIRT));

            // Legacy block IDs used by the grid (pre-1.13 numeric IDs).
            // WOOL is kept as documentation of the reference palette: the
            // checker cell below deliberately emits a *random* legacy id
            // instead, which is what makes this style useful for validating
            // that a client decoder tolerates ids outside the palette.
            [[maybe_unused]] const uint16_t WOOL = 35; // meta: 0=white, 1=orange, 2=magenta, 3=light blue, ...
            const uint16_t GOLD_BLOCK = 41;
            const uint16_t IRON_BLOCK = 42;
            auto random_id = []()
            { static std::mt19937 gen(std::random_device{}()); return std::uniform_int_distribution<int>(0, 197)(gen); };
            for (int z = 0; z < 16; ++z)
            {
                for (int x = 0; x < 16; ++x)
                {
                    int wx = chunkX * 16 + x;
                    int wz = chunkZ * 16 + z;
                    uint16_t block = Blocks::combined(Blocks::AIR);

                    if (wx % 4 == 0 && wz % 4 == 0)
                        block = Blocks::combined(Blocks::AIR); // air gap every 4x4 cells
                    else if (wz % 2 == 1)
                        block = Blocks::combined(GOLD_BLOCK); // Z axis: gold stripes
                    else if (wx % 2 == 1)
                        block = Blocks::combined(IRON_BLOCK); // X axis: iron stripes
                    else
                        block = Blocks::combined(/*WOOL*/ random_id() /*, static_cast<uint8_t>((wx / 2 + wz / 2) % 14)*/); // wool checker

                    blocksArray[idx(6, z, x)] = block;
                }
            }
            return;
        }
        else // GenStyle::SUPERFLAT — modern Minecraft superflat presets
        {
            const std::string preset = ConfigManager::getInstance().getString("world", "superflat-preset", "minecraft:flat");

            auto fillLayer = [&](int absY, uint16_t block)
            {
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                        blocksArray[idx(absY, z, x)] = block;
            };

            // Superflat surface sits a few blocks above Y=0 (player spawns at Y=7).
            // All layers stay within Y >= 0 so they fall in the client-visible sections.
            if (preset == "minecraft:stone")
            {
                // Classic "stone" preset: bedrock, stone with granite/diorite/andesite patches, dirt, grass.
                fillLayer(0, Blocks::combined(Blocks::BEDROCK));
                for (int y = 1; y <= 5; ++y)
                    fillLayer(y, Blocks::combined(Blocks::STONE));
                // Deterministic stone-variant patches (stable per chunk position)
                uint32_t rng = 0x811C9DC5u;
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x)
                        rng ^= static_cast<uint32_t>(z * 73856093u + x * 19349663u);
                for (int y = 0; y <= 3; ++y)
                    for (int z = 0; z < 16; ++z)
                        for (int x = 0; x < 16; ++x)
                        {
                            rng ^= rng << 13;
                            rng ^= rng >> 7;
                            rng ^= rng << 17; // xorshift32
                            uint8_t r = static_cast<uint8_t>(rng & 0xFF);
                            if (r < 24)                                                                                         // ~9% of the stone surface gets a variant
                                blocksArray[idx(y, z, x)] = Blocks::combined(Blocks::STONE, static_cast<uint8_t>(2 + (r % 3))); // meta 2/3/5
                        }
                fillLayer(6, Blocks::combined(Blocks::DIRT));
                fillLayer(7, Blocks::combined(Blocks::GRASS_BLOCK));
            }
            else if (preset == "minecraft:water")
            {
                // Water preset: bedrock at Y=0, stone Y=1..4, dirt Y=5, water surface Y=6.
                fillLayer(0, Blocks::combined(Blocks::BEDROCK));
                for (int y = 1; y <= 4; ++y)
                    fillLayer(y, Blocks::combined(Blocks::STONE));
                fillLayer(5, Blocks::combined(Blocks::DIRT));
                fillLayer(6, Blocks::combined(Blocks::WATER));
            }
            else if (preset == "minecraft:natural_hills")
            {
                // Fold the 64-bit world seed into the preset's own key so
                // natural_hills is reproducible via [world] world-seed.
                uint32_t seed = static_cast<uint32_t>(worldSeed ^ (worldSeed >> 32) ^ 0x1337ULL);

                for (int z = 0; z < 16; ++z)
                {
                    for (int x = 0; x < 16; ++x)
                    {
                        int worldX = chunkX * 16 + x;
                        int worldZ = chunkZ * 16 + z;

                        // Bruit de relief avec échelle adaptée au monde
                        float scale1 = 0.015f;
                        float scale2 = 0.05f;
                        float n1 = noise2D(static_cast<float>(worldX) * scale1, static_cast<float>(worldZ) * scale1, seed);
                        float n2 = noise2D(static_cast<float>(worldX) * scale2, static_cast<float>(worldZ) * scale2, seed + 1);

                        float combinedNoise = (n1 * 0.8f) + (n2 * 0.2f);
                        int height = 10 + static_cast<int>(combinedNoise * 35.0f); // Hauteur entre Y=10 et Y=45

                        // 1. Bedrock à Y=0
                        blocksArray[idx(0, z, x)] = Blocks::combined(Blocks::BEDROCK);

                        // 2. Génération de la sous-couche de roche + Ores
                        for (int y = 1; y < height - 3; ++y)
                        {
                            // PRNG 3D déterministe pour chaque bloc de pierre
                            uint32_t oreRng = seed ^ (static_cast<uint32_t>(worldX) * 73856093u) ^ (static_cast<uint32_t>(y) * 83492791u) ^ (static_cast<uint32_t>(worldZ) * 19349663u);
                            oreRng ^= oreRng >> 13;
                            oreRng *= 0x5bd1e995u;
                            oreRng ^= oreRng >> 15;

                            uint32_t roll = oreRng % 10000; // Tirage sur 10 000
                            uint16_t blockToPlace = Blocks::combined(Blocks::STONE);

                            // Distribution des minerais selon la hauteur Y
                            if (y <= 16 && roll < 8) // Diamant (~0.08%)
                                blockToPlace = Blocks::combined(56);
                            else if (y <= 16 && roll < 23) // Redstone (~0.15%)
                                blockToPlace = Blocks::combined(73);
                            else if (y <= 32 && roll < 33) // Or (~0.10%)
                                blockToPlace = Blocks::combined(14);
                            else if (y <= 32 && roll < 38) // Lapis Lazuli (~0.05%)
                                blockToPlace = Blocks::combined(21);
                            else if (y <= 64 && roll < 100) // Fer (~0.60%)
                                blockToPlace = Blocks::combined(15);
                            else if (y <= 128 && roll < 200) // Charbon (~1.00%)
                                blockToPlace = Blocks::combined(16);
                            else if (roll < 800) // Variantes de roche (Granite/Diorite/Andésite)
                                blockToPlace = Blocks::combined(Blocks::STONE, static_cast<uint8_t>(2 + (oreRng % 3)));

                            blocksArray[idx(y, z, x)] = blockToPlace;
                        }

                        // 3. Terre sous la surface
                        for (int y = std::max(1, height - 3); y < height; ++y)
                        {
                            blocksArray[idx(y, z, x)] = Blocks::combined(Blocks::DIRT);
                        }

                        // 4. Bloc de surface (Herbe / Sable / Eau)
                        if (height >= 12)
                        {
                            blocksArray[idx(height, z, x)] = Blocks::combined(Blocks::GRASS_BLOCK);
                        }
                        else
                        {
                            blocksArray[idx(height, z, x)] = Blocks::combined(Blocks::SAND);
                            for (int y = height + 1; y <= 12; ++y)
                            {
                                blocksArray[idx(y, z, x)] = Blocks::combined(Blocks::WATER);
                            }
                        }
                    }
                }
            }
            else if (preset == "minecraft:desert")
            {
                // Fold the 64-bit world seed into this preset's key so
                // desert is reproducible via [world] world-seed.
                uint32_t seed = static_cast<uint32_t>(worldSeed ^ (worldSeed >> 32) ^ 0x4242ULL);

                for (int z = 0; z < 16; ++z)
                {
                    for (int x = 0; x < 16; ++x)
                    {
                        int worldX = chunkX * 16 + x;
                        int worldZ = chunkZ * 16 + z;

                        float scale = 0.02f;
                        float n = noise2D(static_cast<float>(worldX) * scale, static_cast<float>(worldZ) * scale, seed);
                        int height = 12 + static_cast<int>(n * 18.0f); // Dunes douces Y=12 à Y=30

                        blocksArray[idx(0, z, x)] = Blocks::combined(Blocks::BEDROCK);

                        // Roche + Minerais
                        for (int y = 1; y < height - 6; ++y)
                        {
                            uint32_t oreRng = seed ^ (static_cast<uint32_t>(worldX) * 73856093u) ^ (static_cast<uint32_t>(y) * 83492791u) ^ (static_cast<uint32_t>(worldZ) * 19349663u);
                            oreRng ^= oreRng >> 13;
                            oreRng *= 0x5bd1e995u;
                            oreRng ^= oreRng >> 15;

                            uint32_t roll = oreRng % 10000;
                            uint16_t blockToPlace = Blocks::combined(Blocks::STONE);

                            if (y <= 16 && roll < 8)
                                blockToPlace = Blocks::combined(56); // Diamant
                            else if (y <= 32 && roll < 35)
                                blockToPlace = Blocks::combined(14); // Or
                            else if (y <= 64 && roll < 100)
                                blockToPlace = Blocks::combined(15); // Fer
                            else if (y <= 128 && roll < 200)
                                blockToPlace = Blocks::combined(16); // Charbon

                            blocksArray[idx(y, z, x)] = blockToPlace;
                        }

                        // Couche de Grès (Sandstone ID 24) sous le sable
                        for (int y = std::max(1, height - 6); y < height - 2; ++y)
                        {
                            blocksArray[idx(y, z, x)] = Blocks::combined(24);
                        }

                        // Couche de surface en Sable (Sand ID 12)
                        for (int y = height - 2; y <= height; ++y)
                        {
                            blocksArray[idx(y, z, x)] = Blocks::combined(Blocks::SAND);
                        }
                    }
                }
            }
            else if (preset == "minecraft:mountains")
            {
                uint32_t seed = static_cast<uint32_t>(worldSeed ^ (worldSeed >> 32) ^ 0x0515ULL);

                for (int z = 0; z < 16; ++z)
                {
                    for (int x = 0; x < 16; ++x)
                    {
                        int worldX = chunkX * 16 + x;
                        int worldZ = chunkZ * 16 + z;

                        float scale1 = 0.008f;
                        float scale2 = 0.03f;
                        float n1 = noise2D(static_cast<float>(worldX) * scale1, static_cast<float>(worldZ) * scale1, seed);
                        float n2 = noise2D(static_cast<float>(worldX) * scale2, static_cast<float>(worldZ) * scale2, seed + 1);

                        float combinedNoise = (n1 * 0.7f) + (n2 * 0.3f);

                        // Hauteur allant jusqu'à Y=110 (traverse les sections 0 à 6)
                        int height = 15 + static_cast<int>(combinedNoise * 95.0f);
                        height = std::clamp(height, 5, 100); // Sécurité anti-débordement Y < 256

                        blocksArray[idx(0, z, x)] = Blocks::combined(Blocks::BEDROCK);

                        // Roche & Minerais
                        for (int y = 1; y < height - 1; ++y)
                        {
                            uint32_t oreRng = seed ^ (static_cast<uint32_t>(worldX) * 73856093u) ^ (static_cast<uint32_t>(y) * 83492791u) ^ (static_cast<uint32_t>(worldZ) * 19349663u);
                            oreRng ^= oreRng >> 13;
                            oreRng *= 0x5bd1e995u;
                            oreRng ^= oreRng >> 15;

                            uint32_t roll = oreRng % 10000;
                            uint16_t blockToPlace = Blocks::combined(Blocks::STONE);

                            if (y <= 16 && roll < 10)
                                blockToPlace = Blocks::combined(56); // Diamant
                            else if (y <= 16 && roll < 25)
                                blockToPlace = Blocks::combined(73); // Redstone
                            else if (y <= 32 && roll < 40)
                                blockToPlace = Blocks::combined(14); // Or
                            else if (y <= 32 && roll < 45)
                                blockToPlace = Blocks::combined(21); // Lapis
                            else if (y <= 64 && roll < 120)
                                blockToPlace = Blocks::combined(15); // Fer
                            else if (roll < 250)
                                blockToPlace = Blocks::combined(16); // Charbon

                            blocksArray[idx(y, z, x)] = blockToPlace;
                        }

                        // Sommets enneigés / Terrains
                        if (height >= 80)
                        {
                            blocksArray[idx(height - 1, z, x)] = Blocks::combined(Blocks::STONE);
                            blocksArray[idx(height, z, x)] = Blocks::combined(80); // Snow Block (ID 80)
                        }
                        else
                        {
                            blocksArray[idx(height - 1, z, x)] = Blocks::combined(Blocks::DIRT);
                            blocksArray[idx(height, z, x)] = Blocks::combined(Blocks::GRASS_BLOCK);
                        }
                    }
                }
            }
            else if (preset == "minecraft:islands")
            {
                uint32_t seed = static_cast<uint32_t>(worldSeed ^ (worldSeed >> 32) ^ 0x9999ULL);
                const int seaLevel = 14;

                for (int z = 0; z < 16; ++z)
                {
                    for (int x = 0; x < 16; ++x)
                    {
                        int worldX = chunkX * 16 + x;
                        int worldZ = chunkZ * 16 + z;

                        float scale = 0.035f;
                        float n = noise2D(static_cast<float>(worldX) * scale, static_cast<float>(worldZ) * scale, seed);

                        // Hauteur bornée entre Y=3 et Y=30
                        int height = 3 + static_cast<int>(n * 27.0f);
                        height = std::clamp(height, 3, 60);

                        blocksArray[idx(0, z, x)] = Blocks::combined(Blocks::BEDROCK);

                        // Roche sous-marine
                        for (int y = 1; y < height - 2; ++y)
                        {
                            uint32_t oreRng = seed ^ (static_cast<uint32_t>(worldX) * 73856093u) ^ (static_cast<uint32_t>(y) * 83492791u) ^ (static_cast<uint32_t>(worldZ) * 19349663u);
                            oreRng ^= oreRng >> 13;
                            oreRng *= 0x5bd1e995u;
                            oreRng ^= oreRng >> 15;

                            uint16_t blockToPlace = Blocks::combined(Blocks::STONE);
                            if (y <= 16 && (oreRng % 1000) < 10)
                                blockToPlace = Blocks::combined(56);
                            else if ((oreRng % 1000) < 30)
                                blockToPlace = Blocks::combined(15);

                            blocksArray[idx(y, z, x)] = blockToPlace;
                        }

                        // Surface et niveau d'eau sécurisés
                        if (height > seaLevel)
                        {
                            blocksArray[idx(height - 2, z, x)] = Blocks::combined(Blocks::DIRT);
                            blocksArray[idx(height - 1, z, x)] = Blocks::combined(Blocks::DIRT);
                            blocksArray[idx(height, z, x)] = Blocks::combined(Blocks::GRASS_BLOCK);
                        }
                        else
                        {
                            blocksArray[idx(height - 1, z, x)] = Blocks::combined(Blocks::SAND);
                            blocksArray[idx(height, z, x)] = Blocks::combined(Blocks::SAND);

                            // Remplissage d'eau sécurisé sans risque de dépassement
                            for (int y = height + 1; y <= seaLevel; ++y)
                            {
                                if (y < 256)
                                    blocksArray[idx(y, z, x)] = Blocks::combined(Blocks::WATER);
                            }
                        }
                    }
                }
            }
            else // "minecraft:flat" and any unknown preset fall back to the classic flat world
            {
                fillLayer(0, Blocks::combined(Blocks::BEDROCK));
                for (int y = 0; y <= 4; ++y)
                    fillLayer(y, Blocks::combined(Blocks::STONE));
                fillLayer(5, Blocks::combined(Blocks::DIRT));
                fillLayer(6, Blocks::combined(Blocks::GRASS_BLOCK));

                if (preset != "minecraft:flat")
                {
                    Core::Logger::warn("[WorldStorage] Unknown generator preset '" + preset + "', falling back to minecraft:flat");
                }
            }
        }
    }

    // ---------------------------------------------------------------------------
    // LZ4 compression helpers
    // ---------------------------------------------------------------------------
    std::vector<uint8_t> WorldStorage::compressBuffer(const std::vector<uint8_t> &uncompressed)
    {
        int maxCompressedSize = LZ4_compressBound(static_cast<int>(uncompressed.size()));
        std::vector<uint8_t> compressed(maxCompressedSize);

        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char *>(uncompressed.data()),
            reinterpret_cast<char *>(compressed.data()),
            static_cast<int>(uncompressed.size()),
            maxCompressedSize);

        if (compressedSize <= 0)
        {
            Core::Logger::error("LZ4 Compression failed!");
            return {};
        }

        compressed.resize(compressedSize);
        return compressed;
    }

    std::vector<uint8_t> WorldStorage::decompressBuffer(const uint8_t *compressedData, size_t compressedSize, size_t uncompressedSize)
    {
        std::vector<uint8_t> decompressed(uncompressedSize);

        int decompressedBytes = LZ4_decompress_safe(
            reinterpret_cast<const char *>(compressedData),
            reinterpret_cast<char *>(decompressed.data()),
            static_cast<int>(compressedSize),
            static_cast<int>(uncompressedSize));

        if (decompressedBytes < 0 || static_cast<size_t>(decompressedBytes) != uncompressedSize)
        {
            Core::Logger::error("LZ4 Decompression failed!");
            return {};
        }

        return decompressed;
    }

    WorldStorage::~WorldStorage()
    {
        // Ask the generation worker to exit and join it so ~WorldStorage returns
        // only when no thread is still touching m_db / m_saveChunkStmt. If the
        // worker was never started (m_genWorkerStarted false / default-
        // constructed std::thread), join() would throw, so we gate on the flag
        // and a joinable() check.
        m_genStopFlag.store(true);
        m_genCv.notify_all();
        if (m_genWorker.joinable())
            m_genWorker.join();

        if (m_getChunkStmt)
            sqlite3_finalize(m_getChunkStmt);
        if (m_saveChunkStmt)
            sqlite3_finalize(m_saveChunkStmt);
        if (m_db)
            sqlite3_close(m_db);
    }

    bool WorldStorage::init(const std::string &dbPath)
    {
        if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK)
        {
            Core::Logger::error("Failed to open SQLite database: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        // Enable WAL mode and NORMAL synchronization for optimal I/O performance
        sqlite3_exec(m_db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);

        const char *sqlTables = R"(
        CREATE TABLE IF NOT EXISTS chunks (
            chunk_x INT, chunk_z INT, 
            data_blob BLOB,
            PRIMARY KEY (chunk_x, chunk_z)
        );
        CREATE TABLE IF NOT EXISTS generated_chunks (
            chunk_x INT, chunk_z INT, 
            PRIMARY KEY (chunk_x, chunk_z)
        );
    )";

        char *errMsg = nullptr;
        if (sqlite3_exec(m_db, sqlTables, nullptr, nullptr, &errMsg) != SQLITE_OK)
        {
            Core::Logger::error("Error creating SQLite tables: " + std::string(errMsg));
            sqlite3_free(errMsg);
            return false;
        }

        // Prepared statements for reading and writing compressed chunks
        const char *sqlSelect = "SELECT data_blob FROM chunks WHERE chunk_x = ? AND chunk_z = ?;";
        if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &m_getChunkStmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Error preparing getChunkStmt: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        const char *sqlInsert = "INSERT OR REPLACE INTO chunks (chunk_x, chunk_z, data_blob) VALUES (?, ?, ?);";
        if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &m_saveChunkStmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("Error preparing saveChunkStmt: " + std::string(sqlite3_errmsg(m_db)));
            return false;
        }

        return true;
    }

    bool WorldStorage::isChunkGenerated(int chunkX, int chunkZ)
    {
        const char *sql = "SELECT 1 FROM generated_chunks WHERE chunk_x = ? AND chunk_z = ? LIMIT 1;";
        sqlite3_stmt *stmt;
        bool exists = false;

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, chunkX);
            sqlite3_bind_int(stmt, 2, chunkZ);
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                exists = true;
            }
        }
        sqlite3_finalize(stmt);
        return exists;
    }

    // ---------------------------------------------------------------------------
    // Blob reading with legacy migration
    // ---------------------------------------------------------------------------
    bool WorldStorage::readChunkBlob(int chunkX, int chunkZ, std::vector<uint8_t> &outFull)
    {
        outFull.assign(UNCOMPRESSED_CHUNK_SIZE, 0);

        // m_getChunkStmt is a per-instance prepared statement, so exactly one
        // thread can drive it at a time. The network send loop reads chunks
        // through this path; keep it mutually exclusive against any other
        // readChunkBlob call (e.g. EntityManager probing standing surfaces, or
        // legacy sendFlatChunk hitting getChunkBlocks' sync fallback).
        std::lock_guard<std::mutex> lock(m_getStmtMutex);

        sqlite3_reset(m_getChunkStmt);
        sqlite3_clear_bindings(m_getChunkStmt);
        sqlite3_bind_int(m_getChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_getChunkStmt, 2, chunkZ);

        if (sqlite3_step(m_getChunkStmt) != SQLITE_ROW)
            return false;

        const void *blobData = sqlite3_column_blob(m_getChunkStmt, 0);
        int blobSize = sqlite3_column_bytes(m_getChunkStmt, 0);

        if (!blobData || blobSize <= 0)
            return false;

        // Legacy blobs decompress to a single section (8192 bytes); modern blobs
        // decompress to the full 24-section chunk (196,608 bytes). Decompress into
        // a buffer sized for whichever layout we get, then expand into outFull.
        const size_t legacySize = UNCOMPRESSED_SECTION_SIZE;
        const size_t modernSize = UNCOMPRESSED_CHUNK_SIZE;

        std::vector<uint8_t> raw;
        //= decompressBuffer(static_cast<const uint8_t *>(blobData), blobSize, legacySize);
        if (raw.size() != legacySize)
        {
            // Try the modern layout.
            raw = decompressBuffer(static_cast<const uint8_t *>(blobData), blobSize, modernSize);
            if (raw.size() != modernSize)
                return false;
            outFull.assign(raw.begin(), raw.end());
            return true;
        }

        // Legacy layout: content is section 0 (absY 0..15). Copy it into the full buffer.
        std::memcpy(outFull.data() + static_cast<size_t>(0 - MIN_WORLD_Y) * UNCOMPRESSED_SECTION_SIZE, raw.data(), legacySize);
        return true;
    }

    void WorldStorage::generateChunk(int chunkX, int chunkZ)
    {
        // m_saveChunkStmt is a per-instance prepared statement; only one thread
        // can drive it at a time. The generation worker owns this method, but
        // saveBlockChange and getChunkBlocks (via their sync fallback) can also
        // reach it, so both callers hold m_saveStmtMutex. Recursive to allow
        // saveBlockChange -> generateChunk nesting without deadlock.
        std::lock_guard<std::recursive_mutex> saveLock(m_saveStmtMutex);

        if (isChunkGenerated(chunkX, chunkZ))
            return;

        // Construct the full 24-section chunk using the selected generation style.
        std::vector<uint8_t> rawBuffer(UNCOMPRESSED_CHUNK_SIZE, 0);
        generateFlatChunk(m_genStyle, rawBuffer, chunkX, chunkZ, m_worldSeed);

        // Compress chunk data using LZ4
        std::vector<uint8_t> compressed = compressBuffer(rawBuffer);

        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        sqlite3_reset(m_saveChunkStmt);
        sqlite3_clear_bindings(m_saveChunkStmt);
        sqlite3_bind_int(m_saveChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_saveChunkStmt, 2, chunkZ);
        sqlite3_bind_blob(m_saveChunkStmt, 3, compressed.data(), static_cast<int>(compressed.size()), SQLITE_TRANSIENT);
        sqlite3_step(m_saveChunkStmt);

        const char *sqlMark = "INSERT INTO generated_chunks (chunk_x, chunk_z) VALUES (?, ?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sqlMark, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, chunkX);
            sqlite3_bind_int(stmt, 2, chunkZ);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);

        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);

        Core::Logger::debug("Generated and saved LZ4 compressed chunk [" + std::to_string(chunkX) + ", " + std::to_string(chunkZ) + "]");
    }

    void WorldStorage::pregenerateWorld(int radius)
    {
        Core::Logger::info("World verification and LZ4 pre-generation in progress...");

        int count = 0;
        for (int cx = -radius; cx < radius; ++cx)
        {
            for (int cz = -radius; cz < radius; ++cz)
            {
                if (!isChunkGenerated(cx, cz))
                {
                    auto start = std::chrono::high_resolution_clock::now();

                    generateChunk(cx, cz);

                    auto end = std::chrono::high_resolution_clock::now();
                    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

                    Core::Logger::bench("Compressed Chunk (" + std::to_string(cx) + ", " + std::to_string(cz) + ") generated in: " + std::to_string(duration_us) + " us");
                    count++;
                }
            }
        }

        Core::Logger::info("LZ4 Pre-generation complete: " + std::to_string(count) + " new compressed chunks created.");
    }

    // -----------------------------------------------------------------------
    // Off-thread chunk generation worker (1 thread per WorldStorage).
    //
    // Rationale for this granularity:
    //   * A thread PER CLIENT (as the user hinted at) does not scale -- 50
    //     concurrent players on a modest server would spawn 50 terrain threads,
    //     thrash caches, and each thread would fight for the same
    //     sqlite3_stmt* handle anyway (there is exactly one per WorldStorage),
    //     forcing them to serialize on the DB write.
    //   * A SINGLE global generation thread (one for every world) works but
    //     serializes cross-plane generation; if 4 players are on Nether and
    //     4 players on Overworld, chunks for one plane wait behind chunks for
    //     the other. Splitting per-plane (i.e. per WorldStorage) gives
    //     independent progress and only costs ~3-4 threads total across
    //     Overworld/Nether/End/Aether.
    //
    // The worker pulls (cx, cz) requests from m_genQueue (which is filled by
    // requestChunk from the network send loop when it walks the render box
    // extended by a prefetch margin -- see WorldManager::updateChunksAroundPlayer).
    // Because generateChunk locks m_saveStmtMutex while writing, and the
    // network read path locks m_getStmtMutex while reading, a chunk that
    // finishes generation mid-tick becomes visible to the next tick's read
    // (isChunkReady) with no risk of tearing.
    //
    // Prefetch policy itself (which chunks to submit for generation vs which
    // chunks to actually put on the wire this tick) is a per-client decision
    // living in updateChunksAroundPlayer: it sees the player's velocity and
    // biases the "generate-ahead" box in the direction of motion so
    // high-speed travel (boat / flight / spectate) doesn't outrun generation.
    // -----------------------------------------------------------------------
    void WorldStorage::generationWorkerLoop()
    {
        while (true)
        {
            std::pair<int, int> job;
            {
                std::unique_lock<std::mutex> q(m_genQueueMutex);
                m_genCv.wait(q, [&]
                             { return !m_genQueue.empty() || m_genStopFlag.load(); });
                if (m_genStopFlag.load() && m_genQueue.empty())
                    return;
                job = m_genQueue.front();
                m_genQueue.pop_front();
            }

            // Generate the chunk on the DB side (this locks m_saveStmtMutex
            // internally so a concurrent readChunkBlob on the network thread
            // never sees a half-written blob).
            generateChunk(job.first, job.second);

            // Once the DB write landed, drop the key from m_genQueued so a
            // later requestChunk for the same chunk hits the fast
            // isChunkGenerated() reject instead of the queue dedup set.
            const std::uint64_t key =
                (static_cast<std::uint64_t>(static_cast<std::uint32_t>(job.first)) << 32) |
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(job.second));
            {
                std::lock_guard<std::mutex> q(m_genQueueMutex);
                m_genQueued.erase(key);
            }
        }
    }

    void WorldStorage::requestChunk(int chunkX, int chunkZ)
    {
        // Fast reject: chunk's already on disk. isChunkGenerated opens its own
        // local sqlite3_stmt and doesn't touch m_getChunkStmt / m_saveChunkStmt,
        // so this is safe to call concurrently from the network thread while the
        // worker is mid-write on m_saveChunkStmt.
        if (isChunkGenerated(chunkX, chunkZ))
            return;

        const std::uint64_t key =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkZ));

        bool spawnWorker = false;
        {
            std::lock_guard<std::mutex> q(m_genQueueMutex);
            if (m_genQueued.find(key) != m_genQueued.end())
                return;
            m_genQueued.insert(key);
            m_genQueue.emplace_back(chunkX, chunkZ);
            if (!m_genWorkerStarted.load())
                spawnWorker = true;
        }

        if (spawnWorker)
        {
            m_genWorkerStarted.store(true);
            m_genWorker = std::thread([this]
                                      { generationWorkerLoop(); });
        }
        m_genCv.notify_all();
    }

    bool WorldStorage::isChunkReady(int chunkX, int chunkZ)
    {
        return isChunkGenerated(chunkX, chunkZ);
    }

    void WorldStorage::saveBlockChange(int chunkX, int chunkZ, int relX, int absY, int relZ, uint16_t blockId)
    {
        // Guard the shared m_saveChunkStmt. Also protects against a race where
        // the generation worker concurrently inserts a fresh blob for the same
        // chunk; whoever holds the lock wins, and this call re-reads under it
        // via readChunkBlob (getStmtMutex) after any sync generateChunk.
        std::lock_guard<std::recursive_mutex> saveLock(m_saveStmtMutex);

        if (!isChunkGenerated(chunkX, chunkZ))
        {
            auto start = std::chrono::high_resolution_clock::now();

            generateChunk(chunkX, chunkZ);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Core::Logger::bench("Compressed Chunk (" + std::to_string(chunkX) + ", " + std::to_string(chunkZ) + ") generated in: " + std::to_string(duration_us) + " us");
        }

        // 1. Read existing chunk (full 24-section buffer, legacy blobs migrated)
        std::vector<uint8_t> rawBuffer;
        if (!readChunkBlob(chunkX, chunkZ, rawBuffer))
            return;

        // 2. Modify block in memory (absY is an absolute world coordinate)
        uint16_t *blocksArray = reinterpret_cast<uint16_t *>(rawBuffer.data());
        if (absY >= MIN_WORLD_Y && absY <= MAX_WORLD_Y && relX >= 0 && relX < 16 && relZ >= 0 && relZ < 16)
        {
            int index = ((absY - MIN_WORLD_Y) * 256) + (relZ * 16) + relX;
            blocksArray[index] = blockId;
        }

        // 3. Compress and update database
        std::vector<uint8_t> compressed = compressBuffer(rawBuffer);

        sqlite3_reset(m_saveChunkStmt);
        sqlite3_clear_bindings(m_saveChunkStmt);
        sqlite3_bind_int(m_saveChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_saveChunkStmt, 2, chunkZ);
        sqlite3_bind_blob(m_saveChunkStmt, 3, compressed.data(), static_cast<int>(compressed.size()), SQLITE_TRANSIENT);
        sqlite3_step(m_saveChunkStmt);
    }

    std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> WorldStorage::getChunkBlocks(int chunkX, int chunkZ)
    {
        if (!isChunkGenerated(chunkX, chunkZ))
        {
            auto start = std::chrono::high_resolution_clock::now();

            generateChunk(chunkX, chunkZ);

            auto end = std::chrono::high_resolution_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Core::Logger::bench("Compressed Chunk (" + std::to_string(chunkX) + ", " + std::to_string(chunkZ) + ") generated in: " + std::to_string(duration_us) + " us");
        }

        std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> blocks;
        blocks.reserve(4096);

        std::vector<uint8_t> rawBuffer;
        if (!readChunkBlob(chunkX, chunkZ, rawBuffer))
            return blocks;

        const uint16_t *blocksArray = reinterpret_cast<const uint16_t *>(rawBuffer.data());
        for (int absY = MIN_WORLD_Y; absY <= MAX_WORLD_Y; ++absY)
        {
            for (int z = 0; z < 16; ++z)
            {
                for (int x = 0; x < 16; ++x)
                {
                    int index = ((absY - MIN_WORLD_Y) * 256) + (z * 16) + x;
                    uint16_t bId = blocksArray[index];
                    if (bId != 0)
                    {
                        blocks[{x, absY, z}] = bId;
                    }
                }
            }
        }

        return blocks;
    }

    // Read-only twin of getChunkBlocks() for the streaming 1.8+ send path.
    // Never calls generateChunk(); if the chunk's blob isn't on disk yet,
    // returns an empty map so the caller can decide whether to skip this
    // chunk (paired with requestChunk()) or block on a sync generation.
    // Login path still uses the sync getChunkBlocks() so the initial burst
    // of ~rd^2 chunks feels identical to the pre-refactor server; only
    // movement crossings use this variant, which lets the async worker
    // prefetch boxes so the read is a straight LZ4-decompress + hash-map
    // build with zero terrain cost on the socket thread.
    std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> WorldStorage::getChunkBlocksIfReady(int chunkX, int chunkZ)
    {
        std::unordered_map<BlockCoord, uint16_t, BlockCoordHash> blocks;
        if (!isChunkGenerated(chunkX, chunkZ))
            return blocks;
        blocks.reserve(4096);

        std::vector<uint8_t> rawBuffer;
        if (!readChunkBlob(chunkX, chunkZ, rawBuffer))
            return blocks;

        const uint16_t *blocksArray = reinterpret_cast<const uint16_t *>(rawBuffer.data());
        for (int absY = MIN_WORLD_Y; absY <= MAX_WORLD_Y; ++absY)
        {
            for (int z = 0; z < 16; ++z)
            {
                for (int x = 0; x < 16; ++x)
                {
                    int index = ((absY - MIN_WORLD_Y) * 256) + (z * 16) + x;
                    uint16_t bId = blocksArray[index];
                    if (bId != 0)
                    {
                        blocks[{x, absY, z}] = bId;
                    }
                }
            }
        }
        return blocks;
    }

    uint16_t WorldStorage::getBlockAt(int chunkX, int chunkZ, int relX, int absY, int relZ)
    {
        if (absY < MIN_WORLD_Y || absY > MAX_WORLD_Y || relX < 0 || relX >= 16 || relZ < 0 || relZ >= 16)
            return 0;

        if (!isChunkGenerated(chunkX, chunkZ))
            return 0;

        std::vector<uint8_t> rawBuffer;
        if (!readChunkBlob(chunkX, chunkZ, rawBuffer))
            return 0;

        const uint16_t *blocksArray = reinterpret_cast<const uint16_t *>(rawBuffer.data());
        int index = ((absY - MIN_WORLD_Y) * 256) + (relZ * 16) + relX;
        return blocksArray[index];
    }

    // World-coordinate block lookup. Handles negative world X/Z (arithmetic
    // shift for chunkX/chunkZ, two's-complement mask for relX/relZ) so the
    // caller can pass any (wx, wy, wz) and get a valid chunk-cell lookup.
    // Returns 0 for out-of-range Y (matches getBlockAt's "air/unknown"
    // contract) so callers can compare against MIN/MAX_WORLD_Y uniformly.
    uint16_t WorldStorage::blockAtWorld(int wx, int wy, int wz)
    {
        if (wy < MIN_WORLD_Y || wy > MAX_WORLD_Y)
            return 0;
        // Arithmetic-shift / two's-complement chunk indexing. For wx = -1:
        // wx >> 4 == -1 (arith shift) and wx & 0xF == 15, i.e. the last
        // cell of chunk -1. Matches the (chunkX * 16 + relX == wx)
        // invariant WorldStorage::getBlockAt expects.
        const int chunkX = (wx >> 4);
        const int chunkZ = (wz >> 4);
        const int relX = wx & 0xF;
        const int relZ = wz & 0xF;
        return getBlockAt(chunkX, chunkZ, relX, wy, relZ);
    }

    bool WorldStorage::canStandAtWorld(int wx, int wy, int wz)
    {
        // Need feetY-1 inside the world range so we can check the ground
        // block; and headBlock = wy+1 must also be inside so the 2-air
        // check is meaningful. Both are required by vanilla's spawn rule.
        if (wy < MIN_WORLD_Y + 1)
            return false;
        if (wy + 1 > MAX_WORLD_Y)
            return false;
        const uint16_t below = blockAtWorld(wx, wy - 1, wz);
        const uint16_t here = blockAtWorld(wx, wy, wz);
        const uint16_t above = blockAtWorld(wx, wy + 1, wz);
        // combined = blockId << 4 | meta, so a non-zero high nibble means
        // a real block occupies the cell and would block a mob's standing
        // space. Ground must be a real block; feet AND head must be air.
        return ((below >> 4) != 0) && ((here >> 4) == 0) && ((above >> 4) == 0);
    }

    // Expand outward from hintY (clamped into the legal range) until the
    // whole column has been scanned. Nearest-legal wins so the spawn ends
    // up close to the caller's current altitude when the terrain surface
    // sits well below (or above) them.
    int WorldStorage::findStandingFeetY(int wx, int wz, int hintY)
    {
        constexpr int lo = MIN_WORLD_Y + 1;
        constexpr int hi = MAX_WORLD_Y - 1;
        int start = (hintY < lo) ? lo : (hintY > hi ? hi : hintY);
        for (int d = 0; d <= hi - lo; ++d)
        {
            int cand = start - d;
            if (cand >= lo && canStandAtWorld(wx, cand, wz))
                return cand;
            cand = start + d;
            if (d > 0 && cand <= hi && canStandAtWorld(wx, cand, wz))
                return cand;
        }
        return -1;
    }

    // ---- Phase 8b region-codec surface -------------------------------------
    // See NetworkWorlds.cpp's /import-world and /export-world phase 8b
    // branches: they call these two accessors to walk a plane's chunk rows
    // for an Anvil (.mca) or Legacy (.mcc) container without exposing the
    // private sqlite3* handle to NetworkManager.
    bool WorldStorage::forEachChunk(ForEachChunkFn fn)
    {
        if (!m_db)
            return false;
        // Iteration must not disturb m_getChunkStmt (getChunkBlob may be
        // running concurrently on the network send thread), so we prepare a
        // separate statement for the walk. Row lifetime is inside this call
        // only -- sqlite3_column_blob returns a pointer into stmt-owned
        // memory that the next sqlite3_step invalidates.
        sqlite3_stmt *stmt = nullptr;
        const char *sql = "SELECT chunk_x, chunk_z, data_blob FROM chunks;";
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            Core::Logger::error("WorldStorage: forEachChunk SELECT failed: " +
                                std::string(sqlite3_errmsg(m_db)));
            if (stmt)
                sqlite3_finalize(stmt);
            return false;
        }
        int rc = sqlite3_step(stmt);
        while (rc == SQLITE_ROW)
        {
            const int cx = sqlite3_column_int(stmt, 0);
            const int cz = sqlite3_column_int(stmt, 1);
            const void *blob = sqlite3_column_blob(stmt, 2);
            const int blobBytes = sqlite3_column_bytes(stmt, 2);
            fn(cx, cz, static_cast<const uint8_t *>(blob),
                static_cast<std::size_t>(blobBytes < 0 ? 0 : blobBytes));
            rc = sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        return true;
    }

    bool WorldStorage::saveRawChunkBlob(int chunkX, int chunkZ,
                                        const uint8_t *compressedBlob,
                                        std::size_t blobBytes)
    {
        if (!m_db || !m_saveChunkStmt)
            return false;
        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        sqlite3_reset(m_saveChunkStmt);
        sqlite3_clear_bindings(m_saveChunkStmt);
        sqlite3_bind_int(m_saveChunkStmt, 1, chunkX);
        sqlite3_bind_int(m_saveChunkStmt, 2, chunkZ);
        sqlite3_bind_blob(m_saveChunkStmt, 3, compressedBlob,
                          static_cast<int>(blobBytes), SQLITE_TRANSIENT);
        // INSERT OR REPLACE statements produce no output rows, so sqlite3_step
        // returns SQLITE_DONE (101) or the number of rows written (>=1) on
        // success and a negative SQLite error code on failure.
        const int stepRc = sqlite3_step(m_saveChunkStmt);
        if (stepRc < 0)
        {
            sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
            Core::Logger::warn("WorldStorage: saveRawChunkBlob at (" +
                                std::to_string(chunkX) + "," +
                                std::to_string(chunkZ) + ") failed: " +
                                sqlite3_errmsg(m_db));
            return false;
        }
        // Mark the plane's chunk as pre-generated so subsequent WorldStorage
        // reads skip the on-demand generator (matching the "generated_chunks"
        // bookkeeping used in generateChunk above).
        const char *sqlMark =
            "INSERT INTO generated_chunks (chunk_x, chunk_z) VALUES (?, ?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sqlMark, -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, chunkX);
            sqlite3_bind_int(stmt, 2, chunkZ);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
        return true;
    }

} // namespace LightweightMC::Storage
