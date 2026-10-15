// CreativeGen.cpp -- creative plane generator (world id 99 in 0.0.15).
//
// The creative plane in Minecraft Legends has three properties that
// matter here:
//   * Build-first semantics -- the player can build / place / spawn anywhere
//     inside their own plane, so we only need a coherent buildable ground
//     surface, no resource nodes, no obstacles.
//   * No resource nodes: creative worlds intentionally have no ore veins,
//     trees, caves, or structures. Players bring what they need.
//   * No fall-detection: the plane is sealed, so the plane-floor fall-
//     teleport path in PlayerHandler::handlePlayerMovement is gated on
//     session.worldId == 100 (aether only) and does not trigger here.
//
// Even though creative worlds are buildable, the EV server still wants them
// to look *like an island you could walk on* rather than a perfectly flat
// white plane. So height comes from the same fbm3 heightmap as the overworld
// (frequency 0.011, amplitude 18.0, base 62.0): rolling hills, no cliffs, no
// water. What changes vs overworld is the material palette:
//   * Bedrock floor skin at y = 0 (seal the bottom).
//   * Sub-surface is STONE (id 1) all the way up, not DIRT. Stone reads
//     cleanly under redstone dust, whereas dirt visually blends with the
//     dust and makes players lose track of their circuits -- a common
//     creative-mode pitfall.
//   * Surface is GRASS_BLOCK on top of the stone column.
//   * No water, no ore, no trees, no caves, no structures.
//
// World seed is accepted for ABI parity but unused: creative worlds are
// deterministic across planes -- all creative worlds look the same so
// players can port a build between creative worlds cleanly.

#include "storage/WorldGen.hpp"
#include "storage/WorldStorage.hpp"
#include "world/MC17BlockIds.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace LightweightMC::Storage::Gen {

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

    // ------------------------------------------------------------- Creative Chunk Generator
    //
    // Same Perlin heightmap shape as the overworld (fbm3 at 0.011 with
    // amplitude 18.0 + 62.0 base) so the plane looks geographically
    // coherent with the overworld. The palette differs: sub-surface is
    // STONE (id 1) instead of DIRT -- stone reads clearly under redstone
    // dust whereas dirt visually blends with the dust and makes players
    // lose track of their circuits. Surface is GRASS_BLOCK. No water, no
    // ore, no trees, no caves, no structures. Bedrock floor skin at y = 0
    // seals the bottom.
    void creativeChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        static_cast<void>(worldSeed);
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);
        // Fixed permutation so creative worlds look identical regardless of
        // world seed -- players can port a build between creative worlds
        // cleanly.
        Perm256 perm;
        buildPermutation(0xCAFEDBEEF0000001ULL, perm);

        for (int lz = 0; lz < 16; ++lz)
        {
            for (int lx = 0; lx < 16; ++lx)
            {
                const int wx = cx * 16 + lx;
                const int wz = cz * 16 + lz;
                const double p = fbm3(wx, wz, 0.0, 0.011, 3, perm);
                int h = static_cast<int>(p * 18.0 + 62.0);
                if (h < 1) h = 1;
                if (h > 250) h = 250;
                blocks[cell(lx, lz, 0)] = B::combined17(B::BEDROCK);
                for (int y = 1; y < h; ++y)
                    blocks[cell(lx, lz, y)] = B::combined17(B::STONE);
                blocks[cell(lx, lz, h)] = B::combined17(B::GRASS_BLOCK);
            }
        }
    }

} // namespace LightweightMC::Storage::Gen
