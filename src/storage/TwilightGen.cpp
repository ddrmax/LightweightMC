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

    // plantGiantTree -- write a "redwood"-style columnar tree into the
    // chunk buffer. Called 5x per column from twilightChunk.
    //
    // Signature knobs:
    //   * height is 10..50 blocks (chosen by caller via rand);
    //   * trunk cross-section is w x d where w,d are 1..4 (caller picks
    //     the mix, so up to 4x4 trunks with 50-block heights give the
    //     Twilight-forrest silhouette the user described);
    //   * species pick chooses WOOD/LEAVES or WOOD/LEAVES2 for the two
    //     canopy colors so "mixed species" is visible without needing
    //     a per-species block-id extension to the 1.7 palette.
    //
    // Coordinates are chunk-local (lx/lz are 0..15, absY is world y).
    // Any block that lands outside [0..15] x [0..15] x [1..250] is
    // silently clipped -- neighbouring chunks will place their own
    // giant trees, so seams at chunk borders are just canopy clipping.
    void plantGiantTree(int lx, int lz, int surfaceY,
                        int height, int trunkW, int trunkD,
                        std::uint16_t trunkCombined,
                        std::uint16_t canopyCombined,
                        std::uint16_t *blocks)
    {
        const int topY = surfaceY + height;

        // Trunk column: cross-section (trunkW, trunkD) anchored so the
        // base position (lx, lz) is the trunk's northwest corner.
        for (int dy = 1; dy <= height; ++dy)
        {
            int by = surfaceY + dy;
            if (by < 1 || by > 250) continue;
            for (int ix = 0; ix < trunkW; ++ix)
            {
                for (int iz = 0; iz < trunkD; ++iz)
                {
                    int bx = lx + ix;
                    int bz = lz + iz;
                    if (bx < 0 || bx >= 16 || bz < 0 || bz >= 16) continue;
                    blocks[cell(bx, bz, by)] = trunkCombined;
                }
            }
        }

        // Canopy: a squashed sphere around (lx + trunkW/2, topY, lz + trunkD/2)
        // sized to fit the trunk -- bigger trunks get proportionally wider
        // canopies so redwoods read as columns with a bulge on top.
        int cX = lx + trunkW / 2;
        int cZ = lz + trunkD / 2;
        int canopyR = 2 + trunkW;   // radius grows with trunk size
        if (canopyR > 6) canopyR = 6;
        for (int dy = -3; dy <= 2; ++dy)
        {
            for (int dx = -canopyR; dx <= canopyR; ++dx)
            {
                for (int dz = -canopyR; dz <= canopyR; ++dz)
                {
                    int r2 = dx * dx + dz * dz + (dy < 0 ? dy * dy * 2 : dy * dy);
                    if (r2 > canopyR * canopyR) continue;
                    int bx = cX + dx;
                    int bz = cZ + dz;
                    int by = topY + dy;
                    if (bx < 0 || bx >= 16 || bz < 0 || bz >= 16) continue;
                    if (by < 1 || by > 250) continue;
                    // Skip trunk column positions so the canopy doesn't
                    // overwrite the trunk itself.
                    bool inTrunk = (bx >= lx && bx < lx + trunkW &&
                                    bz >= lz && bz < lz + trunkD);
                    if (inTrunk && by <= topY) continue;
                    // Only overwrite AIR so we don't overwrite neighbours' canopies.
                    if (blocks[cell(bx, bz, by)] == B::AIR)
                        blocks[cell(bx, bz, by)] = canopyCombined;
                }
            }
        }
    }

    // --------------------------------------------------------------- Twilight plane
    //
    // "Twilight" is the giant-forrest plane. Terrain is a fixed PLAINS
    // surface (grass over dirt over stone over bedrock, no ore) that
    // never varies in height -- the whole plane is flat -- and the
    // signature of the world is the density of plantGiantTree: 5
    // trees per 16x16 column at 10..50 blocks tall with trunks up to
    // 4x4 and mixed WOOD/LEAVES/LEAVES2 species.
    //
    // Per-column tree placement is deliberately seeded by (worldSeed,
    // cx, cz) via the shared JavaRandom so the forest replays
    // identically every restart while staying visually varied inside
    // a single world.
    void twilightChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        std::fill_n(blocks, 16 * 16 * 256, B::AIR);

        // PLAINS surface: bedrock floor, dirt sub, grass top, at a fixed
        // height (5 blocks). Kept flat -- giant trees are the whole
        // spectacle; height noise is deliberately omitted so canopy
        // clipping at chunk borders stays visually clean.
        for (int lz = 0; lz < 16; ++lz)
        {
            for (int lx = 0; lx < 16; ++lx)
            {
                blocks[cell(lx, lz, 0)] = B::combined17(B::BEDROCK);
                for (int y = 1; y <= 3; ++y)
                    blocks[cell(lx, lz, y)] = B::combined17(B::STONE);
                blocks[cell(lx, lz, 4)] = B::combined17(B::DIRT);
                blocks[cell(lx, lz, 5)] = B::combined17(B::GRASS_BLOCK);
            }
        }

        const int SURFACE_Y = 5;

        // 5 giant trees per column (chosen from a coarse 3x3 anchor
        // cluster grid by the JavaRandom, so trees are irregular but
        // not overlapping).
        JavaRandom rand(worldSeed ^
                        (static_cast<uint64_t>(static_cast<uint32_t>(cx)) * 0x9E3779B185EBCA87ULL) ^
                        (static_cast<uint64_t>(static_cast<uint32_t>(cz)) * 0xC2B2AE3D27D4EB4FULL));

        for (int i = 0; i < 5; ++i)
        {
            int anchorX = static_cast<int>(rand.nextInt(14));  // 0..13 (leaves 2 blocks of canopy margin)
            int anchorZ = static_cast<int>(rand.nextInt(14));
            int height = 10 + static_cast<int>(rand.nextInt(41));   // 10..50
            int trunkW = 1 + static_cast<int>(rand.nextInt(4));     // 1..4
            int trunkD = 1 + static_cast<int>(rand.nextInt(4));     // 1..4
            // Species pick -- pick one of 7 wood/leaf species so trunk and
            // canopy share the same (id, meta). LOG (id 17) meta 0=Oak /
            // 1=Pine / 2=Jungle / 3=Bamboo / 4=Palm pairs with LEAVES
            // (id 18) at the same meta; LOG2 (id 162) meta 0=Acacia /
            // 1=Dark Oak pairs with LEAVES2 (id 161) at the same meta.
            const int species = static_cast<int>(rand.nextInt(7));
            std::uint16_t trunkCombined;
            std::uint16_t canopyCombined;
            if (species < 5)
            {
                trunkCombined = B::combined17(B::LOG, static_cast<std::uint8_t>(species));
                canopyCombined = B::combined17(B::LEAVES, static_cast<std::uint8_t>(species));
            }
            else
            {
                const int m = species - 5;   // 0=Acacia, 1=Dark Oak
                trunkCombined = B::combined17(B::LOG2, static_cast<std::uint8_t>(m));
                canopyCombined = B::combined17(B::LEAVES2, static_cast<std::uint8_t>(m));
            }
            plantGiantTree(anchorX, anchorZ, SURFACE_Y,
                           height, trunkW, trunkD,
                           trunkCombined, canopyCombined, blocks);
        }

        // Deliberately skipped: ore, caves, dungeons, villages, mineshafts.
    }

} // namespace LightweightMC::Storage::Gen
