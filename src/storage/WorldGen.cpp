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


} // namespace LightweightMC::Storage::Gen
