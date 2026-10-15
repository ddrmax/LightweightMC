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
