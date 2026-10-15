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

} // namespace LightweightMC::Storage::Gen
