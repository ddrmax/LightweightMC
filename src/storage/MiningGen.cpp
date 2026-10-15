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

    // --------------------------------------------------------------- Mining plane
    //
    // "Mining" is the ore-heavyplane: an overworld-height terrain (Perlin
    // heightmap on the same scale as the Overworld plane so walking feels
    // familiar) but pinned to the PLAINS biome (grass surface / dirt sub /
    // stone body / bedrock floor) and stripped of trees, caves, villages,
    // dungeons and mineshafts. The only pass that runs here is a 20x ore-
    // vein density with 10x cluster size so players have something to
    // actually mine.
    //
    // Vein parameters: vanilla Overworld uses 27 veins/size 12 for coal,
    // 10/5 iron, 4/5 gold, 2/5 lapis, 2/5 diamond, 2/5 quartz. We scale
    // density 20x (540 coal, 200 iron, 80 gold, 40 lapis, 40 diamond,
    // 40 quartz) and cluster size 10x (120 coal, 50 iron/gold/lapis/
    // diamond/quartz). The vein walk is bounded to the stone body so
    // the surface skin (grass / dirt) is not broken up by ore poking
    // through -- this keeps mining surfaces visually clean.
    //
    // Height sampling matches the Overworld plane at scale 0.011 (the
    // primary heightmap frequency from overworldChunk) but we never
    // branch on biomes: PLAINS is fixed.
    void miningChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);

        Perm256 perm;
        buildPermutation(worldSeed ^ 0xC2B2AE3D27D4EB4FULL, perm);

        // Height sampling: same frequency as overworldChunk's primary
        // heightmap so the terrain feel matches. We force a PLAINS
        // surface (grass/dirt/stone/bedrock) rather than branching
        // on biomesForChunk.
        const int SEA_LEVEL_LOCAL = 62;

        for (int lz = 0; lz < 16; ++lz)
        {
            for (int lx = 0; lx < 16; ++lx)
            {
                // Height baseline (same shape as the Overworld's
                // primary heightmap so walking feels familiar).
                double base = fbm3(cx * 16 + lx, 0, cz * 16 + lz,
                                   0.011, 4, perm);
                int surfaceY = SEA_LEVEL_LOCAL + static_cast<int>(base * 18.0);
                if (surfaceY < 8) surfaceY = 8;
                if (surfaceY > 100) surfaceY = 100;

                for (int y = 0; y <= 127; ++y)
                {
                    if (y == 0)
                    {
                        blocks[cell(lx, lz, y)] = B::combined17(B::BEDROCK);
                    }
                    else if (y < surfaceY - 4)
                    {
                        blocks[cell(lx, lz, y)] = B::combined17(B::STONE);
                    }
                    else if (y < surfaceY)
                    {
                        blocks[cell(lx, lz, y)] = B::combined17(B::DIRT);
                    }
                    else if (y == surfaceY)
                    {
                        blocks[cell(lx, lz, y)] = B::combined17(B::GRASS_BLOCK);
                    }
                    // else: air
                }
            }
        }

        // 20x vanilla ore density with 10x cluster size, restricted to
        // the stone body (y < surfaceY - 4 is where STONE lives; the
        // vein walker only replaces its `host` argument, and here that
        // is STONE, so we never poke ore through the grass/dirt cap).
        const std::uint16_t host = B::STONE;
        generateOre(cx, cz, worldSeed, blocks, B::COAL_ORE,    120,  5, 132, 540, host);
        generateOre(cx, cz, worldSeed, blocks, B::IRON_ORE,    50,   5,  52, 200, host);
        generateOre(cx, cz, worldSeed, blocks, B::GOLD_ORE,    50,   0,  32,  80, host);
        generateOre(cx, cz, worldSeed, blocks, B::LAPIS_ORE,   50,   0,  32,  40, host);
        generateOre(cx, cz, worldSeed, blocks, B::DIAMOND_ORE, 50,   0,  16,  40, host);
        generateOre(cx, cz, worldSeed, blocks, B::QUARTZ_ORE,  50,  16,  72,  40, host);

        // Deliberately skipped: caves, trees, dungeons, villages, mineshafts.
        // The mining plane is bare-bones terrain + ore only.
    }

} // namespace LightweightMC::Storage::Gen
