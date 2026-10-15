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

} // namespace LightweightMC::Storage::Gen
