#include "storage/WorldGen.hpp"
#include "storage/WorldStorage.hpp"
#include "world/MC17BlockIds.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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


    // --------------------------------------------------------------- Greek temple
    //
    // A mythology-style temple written on top of an Aether island. Uses only
    // sandstone-family blocks (SANDSTONE id 24 for the body, SANDSTONE_STAIRS
    // id 128 for the stepped pediment) so temples visually match the
    // island material and read as "carved out of the sky-island rock".
    //
    // Layout (chunk-local, anchored so (ax, az) is the west corner of the
    // temple floor):
    //   * Floor: 6x5 sandstone slab at surfaceY+1.
    //   * 4 corner pillars, 4 blocks tall, one sandstone block per level.
    //   * Two inner pillars on the long axis (6 - 2 = 4 positions wide) at
    //     height 4.
    //   * Roof: solid sandstone slab spanning the 6x5 footprint at
    //     surfaceY + 6.
    //   * Pediment (roofline ornament): a row of SANDSTONE_STAIRS at
    //     surfaceY + 7 running along the long (x) axis.
    //   * Steps on the front (west edge, az side): a two-step staircase
    //     running down from surfaceY+1 to surfaceY.
    void generateGreekTemple(int ax, int az, int surfaceY, std::uint16_t *blocks)
    {
        const int W = 6; // along x
        const int D = 5; // along z
        const int H = 4; // pillar height (roof at surfaceY + 1 + H + 1)

        auto put = [&](int lx, int lz, int y, std::uint16_t id)
        {
            if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16) return;
            if (y < 1 || y > 250) return;
            blocks[cell(lx, lz, y)] = B::combined17(id);
        };
        const std::uint16_t SAND = B::SANDSTONE;
        const std::uint16_t STAIR = B::SANDSTONE_STAIRS;

        // 1) Floor slab (surfaceY + 1 so temple sits ON the island).
        for (int i = 0; i < W; ++i)
            for (int j = 0; j < D; ++j)
                put(ax + i, az + j, surfaceY + 1, SAND);

        // 2) Pillars -- corners + inner pair along the long axis.
        auto pillar = [&](int lx, int lz)
        {
            for (int k = 1; k <= H; ++k)
                put(lx, lz, surfaceY + 1 + k, SAND);
        };
        pillar(ax, az);
        pillar(ax + W - 1, az);
        pillar(ax, az + D - 1);
        pillar(ax + W - 1, az + D - 1);
        pillar(ax + (W - 1) / 2, az);
        pillar(ax + (W - 1) / 2, az + D - 1);

        // 3) Roof.
        int roofY = surfaceY + 1 + H + 1;
        for (int i = 0; i < W; ++i)
            for (int j = 0; j < D; ++j)
                put(ax + i, az + j, roofY, SAND);

        // 4) Pediment -- stepped row along the long x axis on the roof.
        for (int i = 0; i < W; ++i)
            put(ax + i, az + D / 2, roofY + 1, STAIR);

        // 5) Entry steps on the front (west edge, az side).
        put(ax + W / 2, az - 1, surfaceY + 1, SAND);
        put(ax + W / 2, az - 2, surfaceY,     SAND);
    }

    // --------------------------------------------------------------- Aether ore
    //
    // Aether is an EV-modded plane (not in vanilla MC). We mirror the
    // Overworld vein profile but restrict to the sky-band where aetherChunk
    // actually lays SANDSTONE (y 30..100), and halve vein counts because
    // sky islands are sparse -- a walk that lands on air is a no-op, but
    // we do not want to waste half the RNG budget on empty air.
    void generateAetherOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        const std::uint16_t host = B::SANDSTONE;
        generateOre(cx, cz, worldSeed, blocks, B::COAL_ORE,    12, 30, 100, 12, host);
        generateOre(cx, cz, worldSeed, blocks, B::IRON_ORE,     5, 30, 100,  6, host);
        generateOre(cx, cz, worldSeed, blocks, B::GOLD_ORE,     5, 30, 100,  3, host);
        generateOre(cx, cz, worldSeed, blocks, B::LAPIS_ORE,    5, 30, 100,  1, host);
        generateOre(cx, cz, worldSeed, blocks, B::DIAMOND_ORE,  5, 40,  80,  2, host);
        generateOre(cx, cz, worldSeed, blocks, B::QUARTZ_ORE,   5, 30, 100,  3, host);
    }

    // --------------------------------------------------------------- Aether Chunk Generator
    //
    // The Aether plane (id 100 in the reserved special-dimension band) is
    // Minecraft-Legends-style sky world: floating sandstone islands in a
    // void, Greek temples anchored on top of islands, and a "void fall"
    // below y=-16 that teleports the player back to the overworld (handled
    // in PlayerHandler::handlePlayerMovement, gated on session.worldId == 100).
    //
    // Island shape uses fbm3 at frequency 0.017 (slightly wider than the
    // pre-rework 0.02) and threshold 0.22 (slightly lower than the pre-
    // rework 0.25) so islands are a touch larger and more likely to be
    // anchored by temples. Post-island pass places 5 Greek temple slots
    // per chunk -- 5x the pre-rework base temple rate -- each slot rolling
    // a 60/100 roll to actually place a temple on the top sandstone block
    // of that anchor column (skipped when the anchor column has no island).
    void aetherChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);
        Perm256 perm;
        buildPermutation(worldSeed ^ 0xD1B54A32B4D5EDB5ULL, perm);

        for (int lz = 0; lz < 16; ++lz)
        {
            for (int lx = 0; lx < 16; ++lx)
            {
                for (int y = 30; y <= 100; ++y)
                {
                    double n = fbm3(cx * 16 + lx, y, cz * 16 + lz, 0.017, 3, perm);
                    if (n > 0.22)
                        blocks[cell(lx, lz, y)] = B::combined17(B::SANDSTONE);
                }
            }
        }

        generateAetherOre(cx, cz, worldSeed, blocks);

        JavaRandom rand(worldSeed ^
                        (static_cast<uint64_t>(static_cast<uint32_t>(cx)) * 0xBF58476D1CE4E5B9ULL) ^
                        (static_cast<uint64_t>(static_cast<uint32_t>(cz)) * 0x94D049BB133111EBULL));

        for (int i = 0; i < 5; ++i)
        {
            int ax = static_cast<int>(rand.nextInt(11));    // 0..10 (6-wide fits inside chunk)
            int az = static_cast<int>(rand.nextInt(12));    // 0..11 (5-deep fits inside chunk)
            if (rand.nextInt(100) >= 60)
                continue;
            int topSand = -1;
            for (int y = 100; y >= 30; --y)
            {
                if ((blocks[cell(ax, az, y)] >> 4) == B::SANDSTONE)
                {
                    topSand = y;
                    break;
                }
            }
            if (topSand < 0)
                continue;
            generateGreekTemple(ax, az, topSand, blocks);
        }
    }

} // namespace LightweightMC::Storage::Gen

