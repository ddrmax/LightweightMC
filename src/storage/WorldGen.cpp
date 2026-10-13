#include "storage/WorldGen.hpp"
#include "storage/WorldStorage.hpp"
#include "world/MC17BlockIds.hpp"
#include "core/Logger.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace LightweightMC::Storage::Gen
{

    namespace B = LightweightMC::World::Blocks17;

    constexpr int MIN_Y = MIN_WORLD_Y;
    constexpr int CHUNK_STRIDE = 16 * 16;

    inline std::size_t cell(int cx, int cz, int absY)
    {
        return static_cast<std::size_t>((absY - MIN_Y) * CHUNK_STRIDE + cz * 16 + cx);
    }

    inline double lerp(double delta, double start, double end)
    {
        return start + delta * (end - start);
    }
    void buildPermutation(std::uint64_t seed, Perm256 &perm)
    {
        for (int i = 0; i < 256; ++i)
            perm[i] = static_cast<std::uint8_t>(i);
        Lcg64 rng;
        rng.seed(seed);
        for (int i = 255; i > 0; --i)
        {
            int j = static_cast<int>(rng.nextInt(static_cast<std::uint32_t>(i + 1)));
            std::uint8_t t = perm[i];
            perm[i] = perm[j];
            perm[j] = t;
        }
    }
    inline int p(const Perm256 &perm, int i) { return perm[i & 255]; }
    inline double gradH(int h, double x, double y, double z)
    {
        switch (h & 15)
        {
        case 0:
            return x + y;
        case 1:
            return -x + y;
        case 2:
            return x - y;
        case 3:
            return -x - y;
        case 4:
            return x + z;
        case 5:
            return -x + z;
        case 6:
            return x - z;
        case 7:
            return -x - z;
        case 8:
            return y + z;
        case 9:
            return -y + z;
        case 10:
            return y - z;
        case 11:
            return -y - z;
        case 12:
            return x + y; // Java MC's edge case (duplicated)
        case 13:
            return -x + y;
        case 14:
            return y + z;
        case 15:
            return -y + z;
        default:
            return 0.0;
        }
    }

    // Classic Perlin over [0,1]^2 with the given scale applied to inputs.
    // Output range roughly [-1, 1].
    double perlin2(int x, int y, double scale, const Perm256 &perm)
    {
        const double X = static_cast<double>(x) * scale;
        const double Y = static_cast<double>(y) * scale;
        const int fx = static_cast<int>(X < 0.0 ? std::floor(X) : X);
        const int fy = static_cast<int>(Y < 0.0 ? std::floor(Y) : Y);
        const double dx = X - fx;
        const double dy = Y - fy;
        const auto fade = [](double t)
        { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };

        auto corner = [&](int ox, int oy) -> double
        {
            const int XA = fx + ox;
            const int YA = fy + oy;
            const double dX = dx - ox;
            const double dY = dy - oy;
            const int h = p(perm, p(perm, XA) + YA);
            return gradH(h, dX, dY, 0.0);
        };
        const double n00 = corner(0, 0);
        const double n10 = corner(1, 0);
        const double n01 = corner(0, 1);
        const double n11 = corner(1, 1);
        const double w00 = fade(1.0 - dx) * fade(1.0 - dy);
        const double w10 = fade(dx) * fade(1.0 - dy);
        const double w01 = fade(1.0 - dx) * fade(dy);
        const double w11 = fade(dx) * fade(dy);
        return n00 * w00 + n10 * w10 + n01 * w01 + n11 * w11;
    }

    // Classic 3D Perlin with the same permutation.
    double perlin3(int x, int y, int z, double scale, const Perm256 &perm)
    {
        const double X = static_cast<double>(x) * scale;
        const double Y = static_cast<double>(y) * scale;
        const double Z = static_cast<double>(z) * scale;
        const int fx = static_cast<int>(X < 0.0 ? std::floor(X) : X);
        const int fy = static_cast<int>(Y < 0.0 ? std::floor(Y) : Y);
        const int fz = static_cast<int>(Z < 0.0 ? std::floor(Z) : Z);
        const double dx = X - fx, dy = Y - fy, dz = Z - fz;
        const auto fade = [](double t)
        { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };
        const double wx0 = fade(1.0 - dx), wx1 = fade(dx);
        const double wy0 = fade(1.0 - dy), wy1 = fade(dy);
        const double wz0 = fade(1.0 - dz), wz1 = fade(dz);

        auto val = [&](int ox, int oy, int oz) -> double
        {
            const int h = p(perm, p(perm, p(perm, fx + ox) + fy + oy) + fz + oz);
            return gradH(h, dx - ox, dy - oy, dz - oz);
        };

        const double c000 = val(0, 0, 0), c100 = val(1, 0, 0);
        const double c010 = val(0, 1, 0), c110 = val(1, 1, 0);
        const double c001 = val(0, 0, 1), c101 = val(1, 0, 1);
        const double c011 = val(0, 1, 1), c111 = val(1, 1, 1);
        const double x00 = c000 * wx0 + c100 * wx1;
        const double x10 = c010 * wx0 + c110 * wx1;
        const double x01 = c001 * wx0 + c101 * wx1;
        const double x11 = c011 * wx0 + c111 * wx1;
        const double y0 = x00 * wy0 + x10 * wy1;
        const double y1 = x01 * wy0 + x11 * wy1;
        return y0 * wz0 + y1 * wz1;
    }

    // Fractal Brownian Motion in 2D. Returns approximately [-1, 1].
    double fbm2(int x, int y, double scale, int octaves, const Perm256 &perm)
    {
        double sum = 0.0, amp = 1.0, freq = scale, total = 0.0;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * perlin2(x, y, freq, perm);
            total += amp;
            amp *= 0.5;
            freq *= 2.0;
        }
        return sum / total;
    }

    // 3D FBM (same convention).
    double fbm3(int x, int y, int z, double scale, int octaves, const Perm256 &perm)
    {
        double sum = 0.0, amp = 1.0, freq = scale, total = 0.0;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * perlin3(x, y, z, freq, perm);
            total += amp;
            amp *= 0.5;
            freq *= 2.0;
        }
        return sum / total;
    }

    // --------------------------------------------------------------- MC 1.7 Cave Carver
    void generateCaves(int chunkX, int chunkZ, uint64_t worldSeed, std::uint16_t *blocks)
    {
        JavaRandom rand(worldSeed);
        uint64_t xSeed = rand.nextLong() / 2L * 2L + 1L;
        uint64_t zSeed = rand.nextLong() / 2L * 2L + 1L;

        constexpr int range = 8;
        for (int cx = chunkX - range; cx <= chunkX + range; ++cx)
        {
            for (int cz = chunkZ - range; cz <= chunkZ + range; ++cz)
            {

                uint64_t chunkSeed = (static_cast<uint64_t>(cx) * xSeed + static_cast<uint64_t>(cz) * zSeed) ^ worldSeed;
                rand.setSeed(chunkSeed);

                int caveCount = rand.nextInt(rand.nextInt(rand.nextInt(15) + 1) + 1);
                if (rand.nextInt(7) != 0)
                    caveCount = 0;

                for (int i = 0; i < caveCount; ++i)
                {
                    double startX = cx * 16 + rand.nextInt(16);
                    double startY = rand.nextInt(rand.nextInt(120) + 8);
                    double startZ = cz * 16 + rand.nextInt(16);

                    int tunnels = 1;
                    if (rand.nextInt(4) == 0)
                    {
                        tunnels += rand.nextInt(4);
                    }

                    for (int t = 0; t < tunnels; ++t)
                    {
                        float yaw = rand.nextFloat() * 2.0f * 3.14159265f;
                        float pitch = (rand.nextFloat() - 0.5f) * 2.0f / 8.0f;
                        float radius = rand.nextFloat() * 2.0f + rand.nextFloat();

                        int length = 112 - rand.nextInt(28);

                        double curX = startX, curY = startY, curZ = startZ;
                        float curYaw = yaw, curPitch = pitch;

                        for (int step = 0; step < length; ++step)
                        {
                            curX += std::cos(curPitch) * std::cos(curYaw);
                            curY += std::sin(curPitch);
                            curZ += std::cos(curPitch) * std::sin(curYaw);

                            curPitch *= 0.7f;
                            curPitch += (rand.nextFloat() - rand.nextFloat()) * rand.nextFloat() * 2.0f;
                            curYaw += (rand.nextFloat() - rand.nextFloat()) * rand.nextFloat() * 4.0f;

                            double rad = radius + std::sin(step * 3.14159265f / length) * 1.5;

                            int minBlockX = std::max(static_cast<int>(curX - rad), chunkX * 16);
                            int maxBlockX = std::min(static_cast<int>(curX + rad), chunkX * 16 + 15);
                            int minBlockY = std::max(static_cast<int>(curY - rad), 1);
                            int maxBlockY = std::min(static_cast<int>(curY + rad), 250);
                            int minBlockZ = std::max(static_cast<int>(curZ - rad), chunkZ * 16);
                            int maxBlockZ = std::min(static_cast<int>(curZ + rad), chunkZ * 16 + 15);

                            for (int x = minBlockX; x <= maxBlockX; ++x)
                            {
                                for (int z = minBlockZ; z <= maxBlockZ; ++z)
                                {
                                    for (int y = maxBlockY; y >= minBlockY; --y)
                                    {
                                        double dx = (x + 0.5 - curX) / rad;
                                        double dy = (y + 0.5 - curY) / rad;
                                        double dz = (z + 0.5 - curZ) / rad;

                                        if (dx * dx + dy * dy + dz * dz < 1.0)
                                        {
                                            int lx = x - chunkX * 16;
                                            int lz = z - chunkZ * 16;
                                            std::uint16_t b = blocks[cell(lx, lz, y)];
                                            if ((b >> 4) == B::STONE || (b >> 4) == B::DIRT || (b >> 4) == B::GRASS_BLOCK)
                                            {
                                                blocks[cell(lx, lz, y)] = (y < 10) ? B::combined17(B::LAVA) : B::AIR;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --------------------------------------------------------------- Ore Generator
    //
    // Vanilla Java's CreateWorld::createOre() drives a `makeOreVeins(tries, maxVeinSize,
    // yMin, yMax, blockId)` walker:
    //   1) picks a random (x, z) inside the 16x16 column,
    //   2) picks a random y inside [yMin, yMax-1],
    //   3) walks a chain of `size` cells (size randomized in [2, maxVeinSize]), each step
    //      advancing x, y, z by -1 / 0 / +1.
    //   4) at each cell, if the block matches the plane's host material (STONE in the
    //      Overworld, NETHERRACK in the Nether, etc.), it is replaced by the ore.
    //
    // The old LWMC version scattered blobs around a random cell (rather than walking
    // veins) AND ignored cx/cz in the seed derivation -- both bugs caused every chunk
    // to show the SAME ore pattern, and made veins look round rather than vein-shaped.
    // Fixed here.
    void generateOre(int cx, int cz, std::uint64_t seed, std::uint16_t *blocks,
                     std::uint16_t oreType, int veinSize, int minY, int maxY,
                     int veinsPerChunk, std::uint16_t hostId)
    {
        std::uint64_t chunkSeed = seed
            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) * 0xC2B2AE3D27D4EB4FULL)
            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz)) * 0x1654A070981119EDULL)
            ^ (static_cast<std::uint64_t>(oreType) * 0x9E3779B97F4A7C15ULL)
            ^ (static_cast<std::uint64_t>(hostId) * 0xBF58476D1CE4E5B9ULL);

        JavaRandom rand(chunkSeed);

        if (minY > maxY - 1)
            return;

        for (int i = 0; i < veinsPerChunk; ++i)
        {
            int x = static_cast<int>(rand.nextInt(16));
            int z = static_cast<int>(rand.nextInt(16));
            int yRange = maxY - minY;
            if (yRange < 1)
                yRange = 1;
            int y = minY + static_cast<int>(rand.nextInt(yRange));

            int size = 2;
            if (veinSize > 2)
                size = 2 + static_cast<int>(rand.nextInt(veinSize - 1));
            if (size > veinSize)
                size = veinSize;

            for (int v = 0; v < size; ++v)
            {
                if (x >= 0 && x < 16 && z >= 0 && z < 16 && y >= MIN_Y && y <= MAX_WORLD_Y)
                {
                    std::size_t i2 = cell(x, z, y);
                    if ((blocks[i2] >> 4) == hostId)
                        blocks[i2] = B::combined17(oreType);
                }
                x += static_cast<int>(rand.nextInt(3)) - 1;
                y += static_cast<int>(rand.nextInt(3)) - 1;
                z += static_cast<int>(rand.nextInt(3)) - 1;
            }
        }
    }

    // --------------------------------------------------------------- Plane-specific ore
    //
    // Real vanilla Java per-plane vein parameters. Vanilla's CreateWorld.cpp
    // createOre() calls per plane differ in host material (STONE for the Overworld,
    // NETHERRACK for the Nether) but keep the same vein parameters (vanilla only
    // defines one world.cpp::createOre() shared between planes for ore vein sizes
    // and Y bands). Vanilla End.cpp does not call createOre() at all; vanilla has
    // no Aether plane (that's an Enhanced Vanilla mod, not vanilla Minecraft).
    //
    //   Coal     27 veins / size 12 / y  5..132
    //   Iron     10 veins / size  5 / y  5.. 52
    //   Gold      4 veins / size  5 / y  0.. 31
    //   Lapis     2 veins / size  5 / y  0.. 31
    //   Diamond   2 veins / size  5 / y  0.. 15
    //   Quartz    2 veins / size  5 / y 16.. 71
    //
    // Vanilla does not have copper/ruby/sapphire/etc. as vanilla 1.7 numeric IDs,
    // so those (and stone meta-variants like granite/slate/diorite/limestone) are
    // intentionally skipped; wire them in later if you add an EV-mod block set.

    void generateOverworldOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        const std::uint16_t host = B::STONE;
        generateOre(cx, cz, worldSeed, blocks, B::COAL_ORE,    12,  5, 132, 27, host);
        generateOre(cx, cz, worldSeed, blocks, B::IRON_ORE,     5,  5,  52, 10, host);
        generateOre(cx, cz, worldSeed, blocks, B::GOLD_ORE,     5,  0,  32,  4, host);
        generateOre(cx, cz, worldSeed, blocks, B::LAPIS_ORE,    5,  0,  32,  2, host);
        generateOre(cx, cz, worldSeed, blocks, B::DIAMOND_ORE,  5,  0,  16,  2, host);
        generateOre(cx, cz, worldSeed, blocks, B::QUARTZ_ORE,   5, 16,  72,  2, host);
    }

    void generateNetherOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        // Real Nether plane has the same vein/height parameters as Overworld
        // but embedded in NETHERRACK instead of STONE.
        const std::uint16_t host = B::NETHERRACK;
        generateOre(cx, cz, worldSeed, blocks, B::COAL_ORE,    12,  5, 132, 27, host);
        generateOre(cx, cz, worldSeed, blocks, B::IRON_ORE,     5,  5,  52, 10, host);
        generateOre(cx, cz, worldSeed, blocks, B::GOLD_ORE,     5,  0,  32,  4, host);
        generateOre(cx, cz, worldSeed, blocks, B::LAPIS_ORE,    5,  0,  32,  2, host);
        generateOre(cx, cz, worldSeed, blocks, B::DIAMOND_ORE,  5,  0,  16,  2, host);
        generateOre(cx, cz, worldSeed, blocks, B::QUARTZ_ORE,   5, 16,  72,  2, host);
    }

    void generateAetherOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        // Aether is an EV modded plane (not in vanilla MC). We mirror the
        // Overworld vein profile but restrict to the sky-band where aetherChunk
        // actually lays SANDSTONE (y 30..100), and halve vein counts because
        // sky islands are sparse -- a walk that lands on air is a no-op, but
        // we don't want to waste half the RNG budget on empty air.
        const std::uint16_t host = B::SANDSTONE;
        generateOre(cx, cz, worldSeed, blocks, B::COAL_ORE,    12, 30, 100, 12, host);
        generateOre(cx, cz, worldSeed, blocks, B::IRON_ORE,     5, 30, 100,  6, host);
        generateOre(cx, cz, worldSeed, blocks, B::GOLD_ORE,     5, 30, 100,  3, host);
        generateOre(cx, cz, worldSeed, blocks, B::LAPIS_ORE,    5, 30, 100,  1, host);
        generateOre(cx, cz, worldSeed, blocks, B::DIAMOND_ORE,  5, 40,  80,  2, host);
        generateOre(cx, cz, worldSeed, blocks, B::QUARTZ_ORE,   5, 30, 100,  3, host);
    }

    void generateEndOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        // Real vanilla End.cpp::createOre() places NO ore (End plane is a
        // hollow wall of END_STONE with no ore veins by design). Kept as
        // an explicit stub so plane-specific wiring stays symmetric; uncomment
        // the block below if you want sparse gems in the End wall.
        (void)cx;
        (void)cz;
        (void)worldSeed;
        (void)blocks;
    }

    // --------------------------------------------------------------- Overworld biome picker
    //
    // Vanilla 1.7's RegionChoose.cpp samples two coarse 2D noises -- a "height"
    // noise at scale 1/220 (which becomes -1..1 mapped to ocean/land/mountain bands)
    // and a "temperature" noise at scale 1/40 -- and maps the pair to a region
    // name (Oak Forest / Grassland / Desert / Arctic / Ocean / Mountains).
    //
    // We do the same thing but at chunk granularity (one pick per 16x16 chunk
    // rather than per voxel), which is what you want for a chunk-streaming
    // engine: adjacent chunks stay in the same biome, so biomes appear as
    // ~20-30 chunk patches (~320-480 blocks wide) matching the "big patches"
    // feel of vanilla.
    //
    // Returns: 0 FOREST, 1 PLAINS, 2 DESERT, 3 ARCTIC, 4 OCEAN, 5 MOUNTAINS.
    int biomeForChunk(int cx, int cz, std::uint64_t worldSeed, const Perm256 &perm)
    {
        (void)worldSeed; // perm already encodes worldSeed via buildPermutation
        const double height = fbm2(cx, cz, 0.0310, 4, perm);
        const double temp = fbm2(cx + 4096, cz - 4096, 0.0880, 3, perm);

        // fbm2 output on Java-style Perlin lands roughly in [-0.6, 0.6]
        // rather than full [-1, 1], so the cut-offs are picked accordingly:
        // OCEAN  ~4%, MOUNTAINS ~4%, DESERT ~6%, ARCTIC ~6%,
        // PLAINS ~15%, FOREST the remaining ~65%.
        if (height < -0.20) return 4;      // OCEAN
        if (height > 0.32) return 5;       // MOUNTAINS
        if (temp > 0.28) return 2;         // DESERT
        if (temp < -0.28) return 3;        // ARCTIC
        if (height > 0.10) return 1;       // PLAINS
        return 0;                          // FOREST (default)
    }

    // --------------------------------------------------------------- Dungeons
    void generateDungeon(int chunkX, int chunkZ, uint64_t worldSeed, std::uint16_t *blocks)
    {
        JavaRandom rand(worldSeed ^ (static_cast<uint64_t>(chunkX) * 0x34187D3ULL) ^ (static_cast<uint64_t>(chunkZ) * 0x1A99B08ULL));

        if (rand.nextInt(8) != 0)
            return;

        int dungeonX = rand.nextInt(16);
        int dungeonZ = rand.nextInt(16);
        int dungeonY = rand.nextInt(200) + 10;

        if ((blocks[cell(dungeonX, dungeonZ, dungeonY)] >> 4) != B::STONE)
            return;

        int sizeX = rand.nextInt(2) + 3;
        int sizeZ = rand.nextInt(2) + 3;

        int airCount = 0;
        for (int x = dungeonX - sizeX - 1; x <= dungeonX + sizeX + 1; ++x)
        {
            for (int y = dungeonY - 1; y <= dungeonY + 4; ++y)
            {
                for (int z = dungeonZ - sizeZ - 1; z <= dungeonZ + sizeZ + 1; ++z)
                {
                    if (x < 0 || x >= 16 || z < 0 || z >= 16)
                        return;

                    std::uint16_t block = blocks[cell(x, z, y)];
                    if (y == dungeonY - 1 && (block >> 4) == B::AIR)
                        return;
                    if (y == dungeonY + 4 && (block >> 4) == B::AIR)
                        return;
                    if ((block >> 4) == B::AIR)
                        airCount++;
                }
            }
        }

        if (airCount < 1 || airCount > 50)
            return;

        for (int x = dungeonX - sizeX; x <= dungeonX + sizeX; ++x)
        {
            for (int y = dungeonY; y <= dungeonY + 3; ++y)
            {
                for (int z = dungeonZ - sizeZ; z <= dungeonZ + sizeZ; ++z)
                {
                    bool isWall = (x == dungeonX - sizeX || x == dungeonX + sizeX ||
                                   z == dungeonZ - sizeZ || z == dungeonZ + sizeZ ||
                                   y == dungeonY || y == dungeonY + 3);

                    if (isWall)
                    {
                        if (y == dungeonY)
                        {
                            uint16_t floorBlock = (rand.nextInt(4) == 0) ? B::MOSSY_COBBLESTONE : B::COBBLESTONE;
                            blocks[cell(x, z, y)] = B::combined17(floorBlock);
                        }
                        else
                        {
                            blocks[cell(x, z, y)] = B::combined17(B::COBBLESTONE);
                        }
                    }
                    else
                    {
                        blocks[cell(x, z, y)] = B::AIR;
                    }
                }
            }
        }

        blocks[cell(dungeonX, dungeonZ, dungeonY + 1)] = B::combined17(B::MOB_SPAWNER);

        for (int c = 0; c < 2; ++c)
        {
            if (rand.nextInt(2) == 0)
            {
                int cx = dungeonX + (rand.nextInt(sizeX * 2) - sizeX);
                int cz = dungeonZ + (rand.nextInt(sizeZ * 2) - sizeZ);
                if ((blocks[cell(cx, cz, dungeonY + 1)] >> 4) == B::AIR)
                {
                    blocks[cell(cx, cz, dungeonY + 1)] = B::combined17(B::CHEST);
                }
            }
        }
    }

    // --------------------------------------------------------------- Villages
    //
    // Vanilla 1.7's MakeVillage.cpp produces a village *cluster*: several houses
    // (5x5..7x7 with pitched roofs and a door) plus a cobble well, a farm plot
    // with WHEAT, and a fenced animal pen, scattered over a ~5x5 chunk
    // neighborhood around a per-region village center. The old LWMC version
    // placed exactly one 5x5x4 box per 32x32 chunk region, which is why
    // villages read as "a single box house."
    void buildVillageHouse(int chunkX, int chunkZ, int localX, int localZ, int surfaceY,
                           int width, int depth, int height, std::uint16_t *blocks, JavaRandom &rand)
    {
        (void)chunkX; (void)chunkZ; // kept for symmetry with other structure helpers
        int y0 = surfaceY;
        for (int i = 0; i < width; ++i)
        {
            for (int j = 0; j < depth; ++j)
            {
                int lx = localX + i;
                int lz = localZ + j;
                if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16)
                    continue;
                blocks[cell(lx, lz, y0)] = B::combined17(B::COBBLESTONE);
                bool edgeX = (i == 0 || i == width - 1);
                bool edgeZ = (j == 0 || j == depth - 1);
                bool isCorner = edgeX && edgeZ;
                bool isWall = edgeX || edgeZ;
                for (int k = 1; k < height; ++k)
                {
                    int by = y0 + k;
                    if (by > 250) break;
                    if (isCorner)
                        blocks[cell(lx, lz, by)] = B::combined17(B::WOOD);
                    else if (isWall)
                        blocks[cell(lx, lz, by)] = B::combined17(B::WOOD_PLANK);
                    else
                        blocks[cell(lx, lz, by)] = B::AIR;
                }
                int roofY = y0 + height;
                if (roofY <= 250)
                {
                    int taper = (i < 1 || i >= width - 1) ? 0 : (j < depth / 2 ? 1 : 2);
                    blocks[cell(lx, lz, roofY)] = B::combined17(taper == 1 ? B::WOOD_PLANK : B::COBBLESTONE);
                    if (height >= 4 && roofY + 1 <= 250 && j > 0 && j < depth - 1 && i > 0 && i < width - 1)
                        blocks[cell(lx, lz, roofY + 1)] = B::combined17(B::WOOD_PLANK);
                }
            }
        }
        int doorX = localX + width / 2;
        int doorZ = localZ;
        if (doorX >= 0 && doorX < 16 && doorZ >= 0 && doorZ < 16)
        {
            blocks[cell(doorX, doorZ, y0 + 1)] = B::AIR;
            blocks[cell(doorX, doorZ, y0 + 2)] = B::AIR;
            if (rand.nextInt(3) == 0)
                blocks[cell(doorX, doorZ, y0 + 1)] = B::combined17(B::WOODEN_DOOR);
        }
    }

    // Village extras -- each writes in the current chunk when its world anchor
    // matches. All use the shared village RNG so a village stays coherent.
    void buildVillageWell(int localX, int localZ, int surfaceY, std::uint16_t *blocks)
    {
        for (int i = -1; i <= 1; ++i)
        {
            for (int j = -1; j <= 1; ++j)
            {
                int lx = localX + i, lz = localZ + j;
                if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16) continue;
                if (i == 0 && j == 0)
                {
                    blocks[cell(lx, lz, surfaceY)] = B::combined17(B::WATER);
                    blocks[cell(lx, lz, surfaceY + 1)] = B::AIR;
                }
                else
                {
                    blocks[cell(lx, lz, surfaceY)] = B::combined17(B::COBBLESTONE);
                    blocks[cell(lx, lz, surfaceY + 1)] = B::combined17(B::WOOD);
                    blocks[cell(lx, lz, surfaceY + 2)] = B::combined17(B::WOOD);
                    blocks[cell(lx, lz, surfaceY + 3)] = B::combined17(B::COBBLESTONE);
                }
            }
        }
    }

    void buildVillageFarm(int localX, int localZ, int surfaceY, int w, int d, std::uint16_t *blocks)
    {
        for (int i = 0; i < w; ++i)
            for (int j = 0; j < d; ++j)
            {
                int lx = localX + i, lz = localZ + j;
                if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16) continue;
                blocks[cell(lx, lz, surfaceY)] = B::combined17(B::DIRT);
                blocks[cell(lx, lz, surfaceY + 1)] = B::combined17(B::WHEAT);
            }
    }

    void buildVillagePen(int localX, int localZ, int surfaceY, int w, int d, std::uint16_t *blocks)
    {
        for (int i = 0; i < w; ++i)
            for (int j = 0; j < d; ++j)
            {
                int lx = localX + i, lz = localZ + j;
                if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16) continue;
                bool edge = (i == 0 || i == w - 1 || j == 0 || j == d - 1);
                if (edge)
                    blocks[cell(lx, lz, surfaceY + 1)] = B::combined17(B::FENCE);
            }
    }

    void generateVillage(int chunkX, int chunkZ, uint64_t worldSeed, std::uint16_t *blocks)
    {
        constexpr int regionSize = 16; // one village center per 16x16 chunk region

        // Walk the 3x3 region neighborhood so villages anchored in an
        // adjacent region can still spill buildings into this chunk.
        for (int rx = -1; rx <= 1; ++rx)
        {
            for (int rz = -1; rz <= 1; ++rz)
            {
                int probeX = chunkX + rx * regionSize;
                int probeZ = chunkZ + rz * regionSize;
                int regionX = (probeX < 0) ? -((-probeX + regionSize - 1) / regionSize)
                                            : (probeX / regionSize);
                int regionZ = (probeZ < 0) ? -((-probeZ + regionSize - 1) / regionSize)
                                            : (probeZ / regionSize);

                JavaRandom rand(worldSeed ^
                                (static_cast<uint64_t>(static_cast<uint32_t>(regionX)) * 0x4F993ULL) ^
                                (static_cast<uint64_t>(static_cast<uint32_t>(regionZ)) * 0x1D271ULL));

                int villageCX = regionX * regionSize + static_cast<int>(rand.nextInt(regionSize));
                int villageCZ = regionZ * regionSize + static_cast<int>(rand.nextInt(regionSize));

                int houseCount = 4 + static_cast<int>(rand.nextInt(4));
                for (int i = 0; i < houseCount; ++i)
                {
                    int anchorCX = villageCX + (static_cast<int>(rand.nextInt(7)) - 3);
                    int anchorCZ = villageCZ + (static_cast<int>(rand.nextInt(7)) - 3);
                    int w = 5 + static_cast<int>(rand.nextInt(3));
                    int d = 5 + static_cast<int>(rand.nextInt(3));
                    int h = 4 + static_cast<int>(rand.nextInt(2));
                    if (anchorCX != chunkX || anchorCZ != chunkZ)
                        continue;
                    int surfaceY = -1;
                    for (int y = 200; y >= B::SEA_LEVEL; --y)
                    {
                        std::uint16_t id = blocks[cell(2, 2, y)] >> 4;
                        if (id == B::GRASS_BLOCK || id == B::SAND || id == B::SNOW || id == B::SANDSTONE)
                        {
                            surfaceY = y;
                            break;
                        }
                    }
                    if (surfaceY >= B::SEA_LEVEL)
                        buildVillageHouse(chunkX, chunkZ, 2, 2, surfaceY, w, d, h, blocks, rand);
                }

                // Well
                int wellCX = villageCX + (static_cast<int>(rand.nextInt(5)) - 2);
                int wellCZ = villageCZ + (static_cast<int>(rand.nextInt(5)) - 2);
                if (wellCX == chunkX && wellCZ == chunkZ)
                {
                    int surfaceY = -1;
                    for (int y = 200; y >= B::SEA_LEVEL; --y)
                    {
                        std::uint16_t id = blocks[cell(8, 8, y)] >> 4;
                        if (id == B::GRASS_BLOCK || id == B::SAND || id == B::SNOW)
                        {
                            surfaceY = y;
                            break;
                        }
                    }
                    if (surfaceY >= B::SEA_LEVEL)
                        buildVillageWell(8, 8, surfaceY, blocks);
                }

                // Farm
                int farmCX = villageCX + (static_cast<int>(rand.nextInt(9)) - 4);
                int farmCZ = villageCZ + (static_cast<int>(rand.nextInt(9)) - 4);
                int farmW = 4 + static_cast<int>(rand.nextInt(4));
                int farmD = 4 + static_cast<int>(rand.nextInt(4));
                if (farmCX == chunkX && farmCZ == chunkZ)
                {
                    int surfaceY = -1;
                    for (int y = 200; y >= B::SEA_LEVEL; --y)
                    {
                        if ((blocks[cell(8, 8, y)] >> 4) == B::GRASS_BLOCK)
                        {
                            surfaceY = y;
                            break;
                        }
                    }
                    if (surfaceY >= B::SEA_LEVEL)
                        buildVillageFarm(2, 2, surfaceY, farmW, farmD, blocks);
                }

                // Pen
                int penCX = villageCX + (static_cast<int>(rand.nextInt(9)) - 4);
                int penCZ = villageCZ + (static_cast<int>(rand.nextInt(9)) - 4);
                int penW = 5 + static_cast<int>(rand.nextInt(3));
                int penD = 5 + static_cast<int>(rand.nextInt(3));
                if (penCX == chunkX && penCZ == chunkZ)
                {
                    int surfaceY = -1;
                    for (int y = 200; y >= B::SEA_LEVEL; --y)
                    {
                        if ((blocks[cell(8, 8, y)] >> 4) == B::GRASS_BLOCK)
                        {
                            surfaceY = y;
                            break;
                        }
                    }
                    if (surfaceY >= B::SEA_LEVEL)
                        buildVillagePen(4, 4, surfaceY, penW, penD, blocks);
                }
            }
        }
    }

    // --------------------------------------------------------------- Mineshafts
    //
    // Vanilla 1.7's MinShaft.cpp starts every mineshaft as a *vertical shaft*
    // (a 3x3 column from a random y=15..50 down 5..45 blocks) that is lined
    // with WOOD cross-beam supports every 4 blocks. Off the shaft it walks
    // 3..7 horizontal corridors, each 20..60 blocks long, that carve 3x3
    // tunnels with the same WOOD support pattern and occasional RAILS on the
    // floor. The old LWMC version skipped the vertical shaft entirely, carved
    // only straight corridors with random Y-jogs, and put FENCE + PISTON_HEAD
    // supports, which is what made the shape look bizarre. Rewritten below.
    void generateMineshaft(int chunkX, int chunkZ, uint64_t worldSeed, std::uint16_t *blocks)
    {
        constexpr int range = 8;
        for (int sx = chunkX - range; sx <= chunkX + range; ++sx)
        {
            for (int sz = chunkZ - range; sz <= chunkZ + range; ++sz)
            {
                JavaRandom rand(worldSeed ^
                                (static_cast<uint64_t>(static_cast<uint32_t>(sx)) * 0x1F2B3ULL) ^
                                (static_cast<uint64_t>(static_cast<uint32_t>(sz)) * 0x3E5C7ULL));
                if (rand.nextDouble() >= 0.004)
                    continue;

                const int startX = sx * 16 + static_cast<int>(rand.nextInt(16));
                const int startY = 15 + static_cast<int>(rand.nextInt(35));
                const int startZ = sz * 16 + static_cast<int>(rand.nextInt(16));

                auto carveCol = [&](int wx, int wy, int wz)
                {
                    int lx = wx - chunkX * 16;
                    int lz = wz - chunkZ * 16;
                    if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || wy < 1 || wy > 250) return;
                    blocks[cell(lx, lz, wy)] = B::AIR;
                };
                auto putWood = [&](int wx, int wy, int wz)
                {
                    int lx = wx - chunkX * 16;
                    int lz = wz - chunkZ * 16;
                    if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || wy < 1 || wy > 250) return;
                    if ((blocks[cell(lx, lz, wy)] >> 4) == B::AIR)
                        blocks[cell(lx, lz, wy)] = B::combined17(B::WOOD);
                };

                // 1) Vertical shaft
                int depth = 5 + static_cast<int>(rand.nextInt(45));
                for (int dy = 0; dy < depth; ++dy)
                {
                    int y = startY - dy;
                    if (y < 2) break;
                    for (int dx = -1; dx <= 1; ++dx)
                        for (int dz = -1; dz <= 1; ++dz)
                            carveCol(startX + dx, y, startZ + dz);
                    if (dy % 4 == 0)
                    {
                        putWood(startX - 1, y, startZ);
                        putWood(startX + 1, y, startZ);
                        putWood(startX, y, startZ - 1);
                        putWood(startX, y, startZ + 1);
                        putWood(startX, y + 1, startZ);
                        putWood(startX, y + 2, startZ);
                    }
                }

                // 2) Horizontal corridors
                int corridors = 3 + static_cast<int>(rand.nextInt(5));
                for (int i = 0; i < corridors; ++i)
                {
                    int length = 20 + static_cast<int>(rand.nextInt(40));
                    int dir = static_cast<int>(rand.nextInt(4));
                    int dx = (dir == 0) ? 1 : (dir == 1 ? -1 : 0);
                    int dz = (dir == 2) ? 1 : (dir == 3 ? -1 : 0);
                    int curX = startX;
                    int curY = startY - static_cast<int>(rand.nextInt(depth));
                    int curZ = startZ;

                    for (int step = 0; step < length; ++step)
                    {
                        curX += dx;
                        curZ += dz;
                        if (step % 8 == 0 && rand.nextInt(4) == 0)
                        {
                            curY += (rand.nextInt(2) == 0) ? 1 : -1;
                            if (curY < 10) curY = 10;
                            if (curY > 50) curY = 50;
                        }
                        for (int tx = -1; tx <= 1; ++tx)
                            for (int tz = -1; tz <= 1; ++tz)
                                for (int ty = 0; ty <= 2; ++ty)
                                    carveCol(curX + tx, curY + ty, curZ + tz);
                        if (step % 5 == 0)
                        {
                            for (int ty = 0; ty <= 2; ++ty)
                            {
                                putWood(curX - 1, curY + ty, curZ);
                                putWood(curX + 1, curY + ty, curZ);
                            }
                            putWood(curX, curY + 2, curZ - 1);
                            putWood(curX, curY + 2, curZ + 1);
                            if (rand.nextInt(2) == 0)
                            {
                                int lx = curX - chunkX * 16;
                                int lz = curZ - chunkZ * 16;
                                if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && curY >= 1)
                                    blocks[cell(lx, lz, curY)] = B::combined17(B::RAILS);
                            }
                        }
                        if (rand.nextInt(12) == 0)
                        {
                            int wx = curX + static_cast<int>(rand.nextInt(3)) - 1;
                            int wz = curZ + static_cast<int>(rand.nextInt(3)) - 1;
                            int wy = curY + 2;
                            int lx = wx - chunkX * 16;
                            int lz = wz - chunkZ * 16;
                            if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && wy >= 1 && wy <= 250)
                            {
                                std::uint16_t b = blocks[cell(lx, lz, wy)];
                                if ((b >> 4) == B::AIR)
                                    blocks[cell(lx, lz, wy)] = B::combined17(B::WEB);
                            }
                        }
                    }
                }
            }
        }
    }

    // --------------------------------------------------------------- Overworld Chunk Generator
    void overworldChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);
        Perm256 perm;
        buildPermutation(worldSeed, perm);

        // Pick this chunk's biome before generating terrain so surface
        // material, altitude bias, tree species, and structure gating can all
        // branch on it. biomeForChunk is deterministic in (cx, cz) so adjacent
        // chunks agree on their biome and biomes appear as continuous patches
        // (see biomes picker docs above).
        const int biome = biomeForChunk(cx, cz, worldSeed, perm);

        double densityGrid[5][33][5];
        const int baseX = cx * 16;
        const int baseZ = cz * 16;

        // Altitude bias per biome so OCEAN biomes push the density threshold
        // down (more water) and MOUNTAINS push it up (taller stone).
        static const double biomeDensityBias[6] = {
            0.00, // FOREST
            0.00, // PLAINS
            -0.05, // DESERT
            0.00, // ARCTIC
            -0.25, // OCEAN
            +0.20, // MOUNTAINS
        };
        const double densityBias = biomeDensityBias[biome];

        for (int iX = 0; iX < 5; ++iX)
        {
            for (int iZ = 0; iZ < 5; ++iZ)
            {
                int wx = baseX + iX * 4;
                int wz = baseZ + iZ * 4;

                for (int iY = 0; iY < 33; ++iY)
                {
                    int y = iY * 8;
                    double mainNoise = (fbm3(wx, y, wz, 0.005, 2, perm) + 1.0) / 2.0;
                    double noiseLow = fbm3(wx, y, wz, 0.001, 4, perm);
                    double noiseHigh = fbm3(wx, y, wz, 0.001, 4, perm);

                    double density = lerp(mainNoise, noiseLow, noiseHigh);
                    double yOffset = (static_cast<double>(y) - 64.0) * 12.0 / 32.0;
                    if (yOffset < 0)
                        yOffset *= 4.0;
                    density -= yOffset;
                    density += densityBias;

                    densityGrid[iX][iY][iZ] = density;
                }
            }
        }

        for (int iX = 0; iX < 4; ++iX)
        {
            for (int iZ = 0; iZ < 4; ++iZ)
            {
                for (int iY = 0; iY < 32; ++iY)
                {
                    for (int y = 0; y < 8; ++y)
                    {
                        double dy = y / 8.0;
                        for (int x = 0; x < 4; ++x)
                        {
                            double dx = x / 4.0;
                            for (int z = 0; z < 4; ++z)
                            {
                                double dz = z / 4.0;

                                double d0 = lerp(dx, lerp(dy, densityGrid[iX][iY][iZ], densityGrid[iX][iY + 1][iZ]),
                                                 lerp(dy, densityGrid[iX + 1][iY][iZ], densityGrid[iX + 1][iY + 1][iZ]));
                                double d1 = lerp(dx, lerp(dy, densityGrid[iX][iY][iZ + 1], densityGrid[iX][iY + 1][iZ + 1]),
                                                 lerp(dy, densityGrid[iX + 1][iY][iZ + 1], densityGrid[iX + 1][iY + 1][iZ + 1]));
                                double finalDensity = lerp(dz, d0, d1);

                                int bx = iX * 4 + x;
                                int by = iY * 8 + y;
                                int bz = iZ * 4 + z;

                                if (finalDensity > 0.0)
                                    blocks[cell(bx, bz, by)] = B::combined17(B::STONE);
                                else if (by <= B::SEA_LEVEL)
                                    blocks[cell(bx, bz, by)] = B::combined17(B::WATER);
                            }
                        }
                    }
                }
            }
        }

        generateCaves(cx, cz, worldSeed, blocks);

        JavaRandom rand(worldSeed ^ (static_cast<uint64_t>(cx) * 0x4c1906cdf1e051LL) ^ (static_cast<uint64_t>(cz) * 0x5da6b3e8dd412LL));
        // Per-biome surface layer -- matches vanilla's region-specific
        // surface materials (Desert = sand over sandstone, Arctic = snow
        // over dirt, Ocean = sand, Mountains = cobblestone over stone,
        // Forest/Plains = grass over dirt).
        std::uint16_t topBlock = B::GRASS_BLOCK;
        std::uint16_t subBlock = B::DIRT;
        if (biome == 2) { topBlock = B::SAND;       subBlock = B::SANDSTONE; }
        else if (biome == 3) { topBlock = B::SNOW;  subBlock = B::DIRT; }
        else if (biome == 4) { topBlock = B::SAND;  subBlock = B::SAND; }
        else if (biome == 5) { topBlock = B::COBBLESTONE; subBlock = B::STONE; }

        for (int lx = 0; lx < 16; ++lx)
        {
            for (int lz = 0; lz < 16; ++lz)
            {
                blocks[cell(lx, lz, 0)] = B::combined17(B::BEDROCK);
                for (int y = 1; y <= 4; ++y)
                {
                    if (static_cast<int>(rand.nextInt(5)) >= y)
                        blocks[cell(lx, lz, y)] = B::combined17(B::BEDROCK);
                }

                int surfaceDepth = -1;
                for (int y = 255; y >= 0; --y)
                {
                    std::uint16_t b = blocks[cell(lx, lz, y)];
                    if (b == B::AIR)
                        surfaceDepth = -1;
                    else if ((b >> 4) == B::STONE)
                    {
                        if (surfaceDepth == -1)
                        {
                            surfaceDepth = 3;
                            blocks[cell(lx, lz, y)] = (y >= B::SEA_LEVEL - 1)
                                ? B::combined17(topBlock)
                                : B::combined17(subBlock);
                        }
                        else if (surfaceDepth > 0)
                        {
                            --surfaceDepth;
                            blocks[cell(lx, lz, y)] = B::combined17(subBlock);
                        }
                    }
                }
            }
        }

        // Vanilla 1.7 per-plane ore distribution (all six vanilla ore IDs,
        // STONE as host material). Replaces the pre-refactor 3-call block
        // which used non-vanilla vein counts and only covered coal/iron/gold.
        generateOverworldOre(cx, cz, worldSeed, blocks);

        // Per-biome vegetation + surface decor.
        //
        // Trees are spread across the whole 16x16 chunk (2..13 leaves a
        // 2-block margin so a canopy that spills past the edge is simply
        // clipped -- neighbouring chunks will place their own trees, so
        // no visible seams). Trunk height is randomised 4..6 blocks (vanilla
        // oak is `4 + nextInt(3)`), and the canopy is a squashed sphere so
        // tall oaks look tall and short oaks look squat.
        const double treeProb[6] = {0.030, 0.008, 0.004, 0.000, 0.000, 0.012};
        const bool allowGrass  = (biome == 0 || biome == 1 || biome == 3 || biome == 5);
        const bool allowFlower = (biome == 0 || biome == 1 || biome == 5);
        const bool allowCactus = (biome == 2);

        for (int lx = 2; lx <= 13; ++lx)
        {
            for (int lz = 2; lz <= 13; ++lz)
            {
                for (int y = 250; y >= B::SEA_LEVEL; --y)
                {
                    std::uint16_t topId = blocks[cell(lx, lz, y)] >> 4;
                    if (topId != topBlock)
                        continue;
                    if (blocks[cell(lx, lz, y + 1)] != B::AIR)
                        continue;
                    // Trees require 2-block side margin to fit their canopy.
                    bool roomy = (lx >= 2 && lx <= 13 && lz >= 2 && lz <= 13);

                    double roll = rand.nextDouble();
                    if (roll < treeProb[biome] && roomy && topBlock != B::SNOW)
                    {
                        int trunkH = 4 + static_cast<int>(rand.nextInt(3)); // 4..6
                        int topY = y + trunkH;
                        for (int h = 1; h <= trunkH; ++h)
                        {
                            if (y + h >= 1 && y + h <= 250)
                                blocks[cell(lx, lz, y + h)] = B::combined17(B::WOOD);
                        }
                        // Squashed-sphere canopy around topY (radius 2).
                        for (int dy = -2; dy <= 1; ++dy)
                            for (int dx = -2; dx <= 2; ++dx)
                                for (int dz = -2; dz <= 2; ++dz)
                                {
                                    int r2 = dx * dx + dz * dz + (dy < 0 ? dy * dy * 2 : dy * dy);
                                    if (r2 > 5) continue;
                                    if (dx == 0 && dz == 0 && dy <= 0) continue; // keep trunk column
                                    int bx = lx + dx, bz = lz + dz, by = topY + dy;
                                    if (bx < 0 || bx >= 16 || bz < 0 || bz >= 16) continue;
                                    if (by < 1 || by > 250) continue;
                                    if (blocks[cell(bx, bz, by)] == B::AIR)
                                        blocks[cell(bx, bz, by)] = B::combined17(B::LEAVES);
                                }
                    }
                    else if (allowCactus && roll < 0.010)
                    {
                        int h = 1 + static_cast<int>(rand.nextInt(3));
                        for (int k = 1; k <= h; ++k)
                            if (y + k <= 250)
                                blocks[cell(lx, lz, y + k)] = B::combined17(B::CACTUS);
                    }
                    else if (allowGrass && roll < treeProb[biome] + 0.13)
                    {
                        blocks[cell(lx, lz, y + 1)] = B::combined17(B::GRASS_DECOR);
                    }
                    else if (allowFlower && roll < treeProb[biome] + 0.15)
                    {
                        blocks[cell(lx, lz, y + 1)] = B::combined17(B::FLOWER_YELLOW);
                    }
                    else if (allowFlower && roll < treeProb[biome] + 0.17)
                    {
                        blocks[cell(lx, lz, y + 1)] = B::combined17(B::FLOWER_RED);
                    }
                    break;
                }
            }
        }

        generateDungeon(cx, cz, worldSeed, blocks);
        // Villages only live on the flat land biomes (Forest/Plains) --
        // vanilla's MakeVillage.cpp skips desert / arctic / ocean / mountain
        // regions. Mineshafts and dungeons are placed in all biomes.
        if (biome == 0 || biome == 1)
            generateVillage(cx, cz, worldSeed, blocks);
        generateMineshaft(cx, cz, worldSeed, blocks);
    }

    // --------------------------------------------------------------- Nether & End
    void netherChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);
        Perm256 perm;
        buildPermutation(worldSeed ^ 0x9E3779B97F4A7C15ULL, perm);

        for (int lx = 0; lx < 16; ++lx)
        {
            for (int lz = 0; lz < 16; ++lz)
            {
                for (int y = 0; y <= 127; ++y)
                {
                    if (y <= 4 || y >= 123)
                    {
                        blocks[cell(lx, lz, y)] = B::combined17(B::BEDROCK);
                    }
                    else
                    {
                        double n = fbm3(cx * 16 + lx, y, cz * 16 + lz, 0.03, 3, perm);
                        if (n > -0.1)
                            blocks[cell(lx, lz, y)] = B::combined17(B::NETHERRACK);
                        else if (y <= 31)
                            blocks[cell(lx, lz, y)] = B::combined17(B::LAVA);
                    }
                }
            }
        }

        // Real Nether plane ore distribution: same vein parameters as Overworld
        // (vanilla Java shares one CreateWorld.cpp::createOre() across planes)
        // but the vein walk only replaces NETHERRACK, so veins are visible
        // against the pink Netherrack body rather than the grey STONE body.
        generateNetherOre(cx, cz, worldSeed, blocks);
    }

    void aetherChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);
        Perm256 perm;
        buildPermutation(worldSeed ^ 0xD1B54A32B4D5EDB5ULL, perm);

        for (int lx = 0; lx < 16; ++lx)
        {
            for (int lz = 0; lz < 16; ++lz)
            {
                for (int y = 30; y <= 100; ++y)
                {
                    double n = fbm3(cx * 16 + lx, y, cz * 16 + lz, 0.02, 3, perm);
                    if (n > 0.25)
                    {
                        blocks[cell(lx, lz, y)] = B::combined17(B::SANDSTONE);
                    }
                }
            }
        }

        // Sandstone-host ore veins restricted to the sky-band where aetherChunk
        // places rock (y 30..100); density halved vs Overworld because sky islands
        // are sparse and vein-walks that land on air are no-ops.
        generateAetherOre(cx, cz, worldSeed, blocks);
    }

    void endChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);
        Perm256 perm;
        buildPermutation(worldSeed ^ 0x1F123BB72B45F3B1ULL, perm);

        for (int lx = 0; lx < 16; ++lx)
        {
            for (int lz = 0; lz < 16; ++lz)
            {
                int wx = cx * 16 + lx, wz = cz * 16 + lz;
                double distFromCenter = std::sqrt(wx * wx + wz * wz);

                for (int y = 40; y <= 80; ++y)
                {
                    double n = fbm3(wx, y, wz, 0.02, 3, perm);
                    if (distFromCenter < 200.0 && (n + (1.0 - distFromCenter / 200.0)) > 0.5)
                    {
                        blocks[cell(lx, lz, y)] = B::combined17(B::END_STONE);
                    }
                }
            }
        }

        // Real vanilla End.cpp::createOre() places NO ore -- End is a hollow
        // wall by design. generateEndOre is kept as an explicit stub so plane
        // wiring stays symmetric across all four GenStyle presets and so we
        // can enable sparse gems in the wall later without touching callers.
        generateEndOre(cx, cz, worldSeed, blocks);
    }

} // namespace LightweightMC::Storage::Gen