#include "storage/WorldGen.hpp"
#include "storage/WorldStorage.hpp"
#include "world/MC17BlockIds.hpp"

#include <algorithm>
#include <array>
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

    // --------------------------------------------------------------- Creative plane
    //
    // "Creative" is a flat infinite canvas: a solid column of DIRT capped by a
    // single grass-block surface at y=7 for every (cx,cz) in the plane, and
    // BEDROCK as the world-floor skin at y=0. There is deliberately no ore, no
    // vegetation, no caves, and no structure passes here -- the client wants
    // unobstructed build room in every direction. Height is a fixed value
    // chosen to match the vanilla player spawn (y=7 above the surface), so
    // the player is standing on top of grass the moment they enter the world.
    //
    // Above y=7 (surface) up to y=127 (top of the visible 8 sections) the
    // plane is AIR so build room extends all the way to the ceiling. Below the
    // surface, DIRT down to y=1 and BEDROCK at y=0 (mirrors the vanilla
    // bedrock-floor behaviour on the Underworld floor).
    //
    // The `seed` parameter is accepted for API symmetry with the other plane
    // generators but is not mixed into any RNG stream (nothing here is random
    // -- creative is 100% deterministic). Callers that want per-plane
    // variation should build their own decorations client-side, which is the
    // intended creative-build workflow.
    void creativeChunk(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks)
    {
        (void)cx;
        (void)cz;
        (void)worldSeed;

        std::fill_n(blocks, 16 * 16 * 256, B::AIR);

        // Bedrock floor skin at y=0 (matches vanilla world-floor look).
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                blocks[cell(lx, lz, 0)] = B::combined17(B::BEDROCK);

        // DIRT fill from y=1 up to y=6 (six layers of soil).
        for (int y = 1; y <= 6; ++y)
        {
            for (int lz = 0; lz < 16; ++lz)
                for (int lx = 0; lx < 16; ++lx)
                    blocks[cell(lx, lz, y)] = B::combined17(B::DIRT);
        }

        // Grass surface at y=7 (player spawns standing here).
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx)
                blocks[cell(lx, lz, 7)] = B::combined17(B::GRASS_BLOCK);

        // y=8..255 stays AIR -- infinite creative build volume.
    }

} // namespace LightweightMC::Storage::Gen
