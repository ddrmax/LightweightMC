#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "storage/WorldStorage.hpp"
#include "world/MC17BlockIds.hpp"

namespace LightweightMC
{
    namespace Storage
    {
        namespace Gen
        {

            // ---- PRNG (48-bit LCG Java Random) ---------------------------------------
            struct Lcg64
            {
                std::uint64_t state{0};

                Lcg64(std::uint64_t s = 0) { setSeed(s); }

                void setSeed(std::uint64_t s)
                {
                    state = (s ^ 0x5DEECE66DLL) & 0xFFFFFFFFFFFFLL;
                }

                void seed(std::uint64_t s) { setSeed(s); }

                std::uint32_t next(int bits)
                {
                    state = (state * 0x5DEECE66DLL + 0xBLL) & 0xFFFFFFFFFFFFLL;
                    return static_cast<std::uint32_t>(state >> (48 - bits));
                }

                std::uint32_t nextInt(std::uint32_t n)
                {
                    if ((n & -n) == n)
                    {
                        return static_cast<std::uint32_t>((n * static_cast<std::int64_t>(next(31))) >> 31);
                    }
                    std::uint32_t bits, val;
                    do
                    {
                        bits = next(31);
                        val = bits % n;
                    } while (static_cast<std::int32_t>(bits - val + (n - 1)) < 0);
                    return val;
                }

                std::uint64_t nextLong()
                {
                    return (static_cast<std::uint64_t>(next(32)) << 32) + next(32);
                }

                double nextDouble()
                {
                    return ((static_cast<std::int64_t>(next(26)) << 27) + next(27)) / static_cast<double>(1LL << 53);
                }

                float nextFloat()
                {
                    return next(24) / static_cast<float>(1 << 24);
                }
            };

            using JavaRandom = Lcg64;

            // ---- Permutation + Perlin --------------------------------------------------
            using Perm256 = std::array<std::uint8_t, 256>;

            void buildPermutation(std::uint64_t seed, Perm256 &perm);
            inline double gradH(int h, double x, double y, double z);
            double perlin3(int x, int y, int z, double scale, const Perm256 &perm);
            double perlin2(int x, int y, double scale, const Perm256 &perm);

            double fbm2(int x, int y, double scale, int octaves, const Perm256 &perm);
            double fbm3(int x, int y, int z, double scale, int octaves, const Perm256 &perm);

            // ---- Carvers & Structure Generators ---------------------------------------
            void generateCaves(int chunkX, int chunkZ, std::uint64_t worldSeed, std::uint16_t *blocks);
            // Vanilla-style vein walker. `hostId` is the block ID the vein is allowed
            // to replace (STONE in the Overworld, NETHERRACK in the Nether, etc.);
            // defaults to STONE so legacy call-sites keep working.
            void generateOre(int cx, int cz, std::uint64_t seed, std::uint16_t *blocks, std::uint16_t oreType, int veinSize, int minY, int maxY, int veinsPerChunk, std::uint16_t hostId = LightweightMC::World::Blocks17::STONE);

            // Plane-specific ore distributors. Each calls generateOre repeatedly
            // with the real vanilla per-plane vein/height parameters. Overworld
            // uses STONE as the host material, Nether uses NETHERRACK, Aether
            // uses SANDSTONE (the sky-island material aetherChunk lays down), End
            // uses END_STONE. Real vanilla's End.cpp::createOre() does not place
            // any ore, so generateEndOre is currently a no-op stub kept for
            // symmetry -- extend it if you want sparse gems in the End wall.
            void generateOverworldOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks);
            void generateNetherOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks);
            void generateAetherOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks);
            void generateEndOre(int cx, int cz, std::uint64_t worldSeed, std::uint16_t *blocks);
            void generateDungeon(int chunkX, int chunkZ, std::uint64_t worldSeed, std::uint16_t *blocks);
            void generateVillage(int chunkX, int chunkZ, std::uint64_t worldSeed, std::uint16_t *blocks);
            void generateMineshaft(int chunkX, int chunkZ, std::uint64_t worldSeed, std::uint16_t *blocks);

            // ---- Overworld biomes ------------------------------------------------------
            //
            // Vanilla Java 1.7's RegionChoose.cpp picks a per-block region by
            // sampling a coarse 2D noise (height at 1/220 and temperature at
            // 1/40) and mapping bands to region names. We expose the same
            // concept as a per-chunk picker so overworldChunk can branch
            // surface material, tree species, and structures per biome.
            //
            //   0 = FOREST   (oak-forest, the default biome)
            //   1 = PLAINS   (grassland, sparse oaks)
            //   2 = DESERT   (sand dunes, cacti)
            //   3 = ARCTIC   (snow, ice, no trees)
            //   4 = OCEAN    (water, sandbars, no villages)
            //   5 = MOUNTAINS (elevated stone, cobble tops, sparse oaks)
            int biomeForChunk(int cx, int cz, std::uint64_t worldSeed, const Perm256 &perm);

            // ---- Top-level chunk generators -------------------------------------------
            void overworldChunk(int cx, int cz, std::uint64_t seed, std::uint16_t *blocks);
            void netherChunk(int cx, int cz, std::uint64_t seed, std::uint16_t *blocks);
            void aetherChunk(int cx, int cz, std::uint64_t seed, std::uint16_t *blocks);
            void endChunk(int cx, int cz, std::uint64_t seed, std::uint16_t *blocks);

        } // namespace Gen
    } // namespace Storage
} // namespace LightweightMC