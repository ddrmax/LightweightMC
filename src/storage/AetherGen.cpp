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

} // namespace LightweightMC::Storage::Gen
