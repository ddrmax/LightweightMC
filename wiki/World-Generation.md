# World Generation

Every chunk on a plane is derived from **one 64-bit world seed** plus **one preset**
chosen for that plane at startup. Two runs with the same seed + preset produce
byte-identical terrain — terrain, ore veins, tree positions, dungeon coordinates,
everything. The pipeline lives in `src/storage/WorldGen.cpp` (public API in
`include/storage/WorldGen.hpp`); the entry point is `WorldStorage::generateChunk`
→ `WorldStorage::generateFlatChunk`, and the preset/seed selector lives in
`src/network/NetworkSocket.cpp::NetworkManager::start`.

The [Storage & Save Files](Storage-and-Save-Files) page covers how the generated
chunk bytes are stored on disk. This page only covers *how they are produced*.

## Configuration

The `[world]` block in `server.properties` is the sole source of preset + seed:

```ini
[world]
# One of: overworld (MC 1.7 style, sea level 62, Perlin, caves, trees),
#         nether    (underworld caverns with lava floor),
#         aether    (floating islands, slime-trunk trees),
#         superflat (legacy flat-layer presets),
#         test-grid (protocol-validation grid)
gen-style = overworld
# Only used when gen-style = superflat.
superflat-preset = minecraft:flat
# 64-bit world seed. Java MC's own seed is a signed long; every preset mixes
# this value into its LCG/Perlin stream so the same seed reproduces the same
# world across runs. Set to 0 (default) for an unseeded world.
world-seed = 1337
overworld-active = true
nether-active = true
end-active = true
aether-active = true
aether-special-dimension = true
```

| Key | Type | Default | Read at | Effect |
| --- | --- | --- | --- | --- |
| `gen-style` | string, one of `overworld` / `superflat` / `test-grid` / `nether` / `aether` / `end` | `overworld` | `NetworkManager::start` (once per boot) | Preset for the **Overworld plane (id 0)** only. Nether, End, and Aether always run their canonical preset (`NETHER`, `END`, `AETHER`) regardless of this value. Unknown strings log a warn and fall back to `overworld`. |
| `world-seed` | 64-bit unsigned | `0` (unseeded — mirrors Java MC's "no seed given" behaviour) | `NetworkManager::start` | Mixed into every Permutation and every LCG stream on every active plane. Custom `/world create <name> <style>` planes copy the Overworld's seed (`NetworkWorlds.cpp::createWorld` → `primary->getWorldSeed()`). |
| `superflat-preset` | string | `minecraft:flat` | `WorldStorage::generateFlatChunk` (per chunk, only when `GenStyle::SUPERFLAT`) | Which layer stack `SUPERFLAT` writes. Other presets ignore this key. Unknown values log a warn and fall through to `minecraft:flat`. |
| `overworld-active` / `nether-active` / `end-active` / `aether-active` | bool | `true` | `NetworkManager::start` | Whether the server allocates a `WorldStorage` for that plane at startup. Covered on [Multiworld & Dimensions](Multiworld-and-Dimensions). If `overworld-active = false`, `start()` refuses to boot — every accessor falls back to plane 0 on a miss. |

`gen-style` is mapped once at boot with case-sensitive string compare:

```cpp
Storage::GenStyle overworldStyle = Storage::GenStyle::OVERWORLD;
if      (genStyle == "superflat")  overworldStyle = Storage::GenStyle::SUPERFLAT;
else if (genStyle == "test-grid")  overworldStyle = Storage::GenStyle::TEST_GRID;
else if (genStyle == "aether")     overworldStyle = Storage::GenStyle::AETHER;
else if (genStyle == "nether")     overworldStyle = Storage::GenStyle::NETHER;
else if (genStyle == "end")        overworldStyle = Storage::GenStyle::END;
else if (genStyle != "overworld")
    Core::Logger::warn("[NetworkManager] Unknown gen-style, falling back to overworld");
```

`src/network/NetworkWorlds.cpp::styleFromName` performs the same mapping for
`/world create <name> <style>`; unknown style strings fall back to `OVERWORLD`
there too (silently — the mapping returns the default, no warn).

## `GenStyle` presets

`enum class GenStyle` in `include/storage/WorldStorage.hpp` — the value stored
per-plane on `WorldStorage::m_genStyle`, driven by `setGenerationStyle`:

| Style | Chosen by | Terrain summary |
| ----- | --------- | --------------- |
| `OVERWORLD` | `gen-style = overworld` (default) or `/world create n overworld` | MC 1.7-style terrain: 3D Perlin density grid → stone body, sea fill at `B::SEA_LEVEL = 62`, Java-1.7 cave "drunkard's walk", six-vein ore profile (host = STONE), oak-tree scatter, per-biome surface materials, dungeons / villages / mineshafts. |
| `SUPERFLAT` | `gen-style = superflat` | Legacy flat-layer presets chosen by `superflat-preset` (see the preset table below). Everything stays in Y 0..7 so the whole world fits in section 0. |
| `TEST_GRID` | `gen-style = test-grid` | Deterministic protocol-validation grid used with `/testpackets`: bedrock Y=0, stone Y=1..4, dirt Y=5, Y=6 surface with gold/iron/wool checker + air gaps every 4th cell. Deliberately emits *random* legacy ids in some cells to prove client decoders tolerate ids outside the palette. Spawn at (0, 7, 0). |
| `NETHER` | Built-in plane **id −1** always runs this preset. Also reachable via `/world create <n> nether`. | Solid Netherrack body between bedrock capstones (y 0..4 and 123..127), cavern shape from `fbm3 > −0.1`, lava below y ≤ 31. Ore veins embedded in NETHERRACK (same vein profile as Overworld). |
| `AETHER` | Built-in plane **id 100** (the special dimension) always runs this preset. Also `/world create <n> aether`. | Floating sky-islands of SANDSTONE in y 30..100, `fbm3 > 0.25`. Ore veins host = SANDSTONE; sky band + halved vein counts because walks landing on air are no-ops. |
| `END` | Built-in plane **id 1** always runs this preset. Also `/world create <n> end`. | Radial ring of END_STONE (`distFromCenter < 200.0`), y 40..80. **No ore** — matches vanilla's `End.cpp::createOre()` which places nothing; `generateEndOre` is an explicit stub kept for symmetry. |

```cpp
enum class GenStyle
{
    OVERWORLD,  // MC 1.7 style, sea level 62, Perlin, caves, trees
    SUPERFLAT,  // modern superflat presets (minecraft:flat / stone / water …)
    TEST_GRID,  // deterministic protocol-validation grid (see /testpackets)
    NETHER,     // underworld caverns with lava floor
    AETHER,     // floating sky-islands (Enhanced Vanilla mod, not vanilla MC)
    END         // "The End" plane (obsidian monoliths, no ore)
};
```

Only the Overworld's preset is chosen from `gen-style`. The three vanilla plane
presets are hard-wired in `NetworkManager::start`'s `planeSpecs` array — you
cannot make Nether run `OVERWORLD` terrain from `server.properties`. To get a
second "Overworld-style" plane, `/world create <name> overworld`.



## PRNG + noise primitives

Every preset anchors on Java Minecraft's 48-bit LCG so that the same `world-seed`
reproduces a byte-identical world across runs. Public API in
`include/storage/WorldGen.hpp`:

```cpp
struct Lcg64
{
    uint64_t state;

    void setSeed(uint64_t s) { state = (s ^ 0x5DEECE66DLL) & 0xFFFFFFFFFFFFLL; }
    uint32_t next(int bits)
    {
        state = (state * 0x5DEECE66DLL + 0xBLL) & 0xFFFFFFFFFFFFLL;
        return uint32_t(state >> (48 - bits));
    }
    uint32_t nextInt(uint32_t n);   // vanilla Java's rejection-sampled nextInt
    uint64_t nextLong();            // (next(32) << 32) + next(32)
    double   nextDouble();          // ((next(26)<<27) + next(27)) / 2^53
    float    nextFloat();           // next(24) / 2^24
};
using JavaRandom = Lcg64;
```

Perlin + fractal Brownian motion over a shared `Perm256` table:

```cpp
using Perm256 = std::array<std::uint8_t, 256>;

void  buildPermutation(uint64_t seed, Perm256 &perm);   // Fisher-Yates on Lcg64
double perlin2(int x, int y,           double scale,   const Perm256 &perm);
double perlin3(int x, int y, int z,    double scale,   const Perm256 &perm);
double fbm2   (int x, int y,           double scale, int octaves, const Perm256 &perm);
double fbm3   (int x, int y, int z,   double scale, int octaves, const Perm256 &perm);
```

`buildPermutation` seeds an `Lcg64` with `seed`, then walks i = 255 → 1 doing
Fisher-Yates swap with `perm[i] ↔ perm[rng.nextInt(i+1)]`. The output is a 256-byte
permutation of 0..255 used by every Perlin sample in the chunk (via
`p(perm, i) = perm[i & 255]` double-hashing).

`fbm3` walks 2–4 octaves of `perlin3` with the amplitude halving and the frequency
doubling per octave (`amp *= 0.5; freq *= 2.0`). Output stays roughly in [−1, +1]
in the classic Java-Java-Perlin range, so the terrain thresholds below (0.25,
−0.1, etc.) are chosen accordingly, not for the theoretical full range.

`gradH` is Java MC's 16-case gradient selector **including its documented edge-case
duplication** (case 12 and 13 both reuse the x/y gradient and case 14/15 reuse
y/z) — the duplication is deliberate to remain bit-compatible with vanilla 1.7's
noise so a same-seed Java client and LightweightMC server produce the same
terrain.

## Per-chunk permutation seed

Each chunk generator builds its own `Perm256 perm` via
`buildPermutation(seed, perm)`. Then each plane XORs `worldSeed` with a fixed
plane constant so the same `world-seed` gives a *different* permutation on each
plane:

| Where | Permutation / seed mix |
| ----- | ---------------------- |
| Overworld chunk generator (`overworldChunk`, `biomeForChunk`) | `buildPermutation(worldSeed, perm)` — raw |
| Nether (`netherChunk`) | `buildPermutation(worldSeed ^ 0x9E3779B97F4A7C15ULL, perm)` |
| Aether (`aetherChunk`) | `buildPermutation(worldSeed ^ 0xD1B54A32B4D5EDB5ULL, perm)` |
| End   (`endChunk`)     | `buildPermutation(worldSeed ^ 0x1F123BB72B45F3B1ULL, perm)` |
| Caves (`generateCaves`) | `JavaRandom rand(worldSeed);` then per-chunk `chunkSeed = (cx·xSeed + cz·zSeed) ^ worldSeed` inside the 8-chunk walk |
| Ore veins (`generateOre`) | `chunkSeed = seed ^ (cx·0xC2B2AE3D27D4EB4F) ^ (cz·0x1654A070981119ED) ^ (oreType·0x9E3779B97F4A7C15) ^ (hostId·0xBF58476D1CE4E5B9)` — the host mix makes Overworld and Nether ore **diverge for the same chunk coords**, so the two planes don't share vein layouts. |
| Dungeon (`generateDungeon`) | `JavaRandom(worldSeed ^ (cx·0x34187D3) ^ (cz·0x1A99B08))` |
| Village (`generateVillage`) | `JavaRandom(worldSeed ^ (regionX·0x4F993) ^ (regionZ·0x1D271))` per 16×16 chunk region |
| Mineshaft (`generateMineshaft`) | `JavaRandom(worldSeed ^ (sx·0x1F2B3) ^ (sz·0x3E5C7))` per 8-chunk neighbourhood probe |

Every one of these constants lives in `src/storage/WorldGen.cpp` next to the
call site; keep them in sync if you re-tune the generator.

**Superflat** variants do not use the Perlin pipeline; they fold `worldSeed`
into a 32-bit key with `seed = worldSeed ^ (worldSeed >> 32) ^ <preset-tag>`
(see `generateFlatChunk` in `src/storage/WorldStorage.cpp`) so they stay
reproducible via `[world] world-seed` too.

## Biome picker (Overworld only)

`biomeForChunk(cx, cz, worldSeed, perm)` samples two fbm2 fields and returns
0..5:

```cpp
const double height = fbm2(cx,       cz,       0.0310, 4, perm);
const double temp   = fbm2(cx + 4096, cz - 4096, 0.0880, 3, perm);

if (height < -0.20) return 4;   // OCEAN      ~4 %
if (height >  0.32) return 5;   // MOUNTAINS  ~4 %
if (temp   >  0.28) return 2;   // DESERT     ~6 %
if (temp   < -0.28) return 3;   // ARCTIC     ~6 %
if (height >  0.10) return 1;   // PLAINS    ~15 %
return 0;                       // FOREST    ~65 %
```

Java-Java Perlin's `fbm2` lands in roughly [−0.6, +0.6], so the cut-offs are
chosen for that practical range, not the theoretical [−1, +1] Perlin range.
The picker is deterministic in `(cx, cz)`, so adjacent chunks agree on their
biome and biomes appear as continuous patches rather than per-chunk confetti.

| Code | Biome | Surface (top, sub) | Trees? | Village? | Density bias |
| ---- | ----- | ------------------ | ------ | -------- | ------------ |
| 0 | FOREST | (GRASS_BLOCK, DIRT) | yes (oak, prob 0.030) | yes | 0.00 |
| 1 | PLAINS | (GRASS_BLOCK, DIRT) | yes (oak, prob 0.008) | yes | 0.00 |
| 2 | DESERT | (SAND, SANDSTONE) | cactus 1..3 tall | no | −0.05 |
| 3 | ARCTIC | (SNOW, DIRT) | no (trees blocked at SNOW surface) | no | 0.00 |
| 4 | OCEAN | (SAND, SAND) | no | no | −0.25 (pushes threshold down → more water) |
| 5 | MOUNTAINS | (COBBLESTONE, STONE) | sparse (prob 0.012) | no | +0.20 (taller stone) |

`biomeDensityBias[6]` (the bias column above) is applied inside the density
grid before the trilinear interpolate; see `overworldChunk`.



## Overworld pipeline (`overworldChunk`)

Order of operations in `src/storage/WorldGen.cpp:870`:

1. **Clear** — `16 × 16 × 256` `uint16_t` array filled with `B::AIR`.
2. **Permute** — `buildPermutation(worldSeed, perm)`.
3. **Pick biome** — `biomeForChunk(cx, cz, worldSeed, perm)` (see the table above).
4. **Density grid** — 5 × 33 × 5 samples (4-block X/Z columns, 8-block Y columns). Per
   sample:
   ```cpp
   double mainNoise = (fbm3(wx, y, wz, 0.005, 2, perm) + 1.0) / 2.0;
   double noiseLow  =  fbm3(wx, y, wz, 0.001, 4, perm);
   double noiseHigh =  fbm3(wx, y, wz, 0.001, 4, perm);
   double density   =  lerp(mainNoise, noiseLow, noiseHigh);
   double yOffset   = (y - 64.0) * 12.0 / 32.0;  // × 4 below 64
   density -= yOffset;
   density += biomeDensityBias[biome];
   ```
5. **Trilinear interpolate** the 5³ grid into per-block density (four-way
   lerp on X, then Y, then Z). Anything with `finalDensity > 0` → STONE; anything
   `y ≤ B::SEA_LEVEL` (62) and not STONE → WATER.
6. **Caves** — `generateCaves(cx, cz, worldSeed, blocks)`. Java-1.7
   "drunkard's walk" carved out of the STONE body; per-chunk seeds are
   `(cx · xSeed + cz · zSeed) ^ worldSeed` where `xSeed`/`zSeed` come from
   `rand.nextLong() / 2 * 2 + 1` (odd numbers so different chunks don't
   collide).
7. **Bedrock base** — `blocks[lx, lz, 0] = BEDROCK` for every column; Y=1..4
   randomly patched BEDROCK with `rand.nextInt(5) >= y` so the underside is
   uneven.
8. **Surface skin** — scan each column top-down; the first solid non-air
   block starts a 3-block surface stack (`topBlock, subBlock, subBlock`)
   using the per-biome (top, sub) pair from the biome table. `topBlock` is
   only used for `y ≥ SEA_LEVEL − 1`, deeper blocks always get `subBlock`.
9. **Ore** — `generateOverworldOre(cx, cz, worldSeed, blocks)` calls
   `generateOre` six times with the vanilla 1.7 vein profile (host = STONE):

   | Ore | Vein size | Y band | Veins / chunk |
   | --- | --------- | ------ | ------------- |
   | COAL_ORE | 12 | 5..132 | 27 |
   | IRON_ORE | 5  | 5..52  | 10 |
   | GOLD_ORE | 5  | 0..32  | 4 |
   | LAPIS_ORE | 5 | 0..32  | 2 |
   | DIAMOND_ORE | 5 | 0..16 | 2 |
   | QUARTZ_ORE | 5 | 16..72 | 2 |

   `generateOre` walks a vein of `2 + rand.nextInt(veinSize − 1)` steps that
   delta ±1 per axis on a fresh `JavaRandom` seeded from `(cx, cz, oreType,
   hostId)` — so the exact same vein parameters (e.g. COAL 27/12/5..132)
   produce a **different layout on Nether than on Overworld** because the host
   mixes into the seed.

10. **Vegetation** — one `JavaRandom` seeded with
    `worldSeed ^ (cx · 0x4c1906cdf1e051LL) ^ (cz · 0x5da6b3e8dd412LL)`.
    For each (lx, lz) in 2..13 (2-block margin so canopies that spill past
    the edge are clipped, and neighbouring chunks place their own trees — no
    visible seams), find the top surface at `y ≥ SEA_LEVEL` and roll
    `nextDouble()` once:

    | Roll vs `treeProb[biome]` | Emits |
    | ------------------------- | ----- |
    | `< treeProb` and biome surface ≠ SNOW | oak: trunk height `4 + nextInt(3)` (4..6) WOOD column + squashed-sphere canopy with radius 2 (test `dx² + dz² + (dy<0 ? 2·dy² : dy²) ≤ 5`, keeping the trunk column air-free) |
    | DESERT: `< 0.010` | cactus (CACTUS block, height `1 + nextInt(3)` = 1..3) |
    | FOREST / PLAINS / ARCTIC / MOUNTAINS: `< treeProb + 0.13` | GRASS_DECOR on top of surface |
    | FOREST / PLAINS / MOUNTAINS: `< treeProb + 0.15` | FLOWER_YELLOW |
    | FOREST / PLAINS / MOUNTAINS: `< treeProb + 0.17` | FLOWER_RED |

    `treeProb = {0.030, 0.008, 0.004, 0.000, 0.000, 0.012}` indexed by biome.

11. **Structures** (in order):
    - `generateDungeon(cx, cz, worldSeed, blocks)` — every biome. 1/8 chance
      per chunk; anchored in a STONE interior; carves a 5×5..7×7 cobblestone
      chamber with a mossy-cobble floor variant and a `MOB_SPAWNER` on the
      ceiling.
    - `generateVillage(cx, cz, worldSeed, blocks)` — **only biomes FOREST and
      PLAINS** (`biome == 0 || biome == 1`), matching vanilla's
      `MakeVillage.cpp` skipping desert / arctic / ocean / mountain. Per
      16×16 chunk region it places a village center, 4..7 houses (5×5..7×7
      walled boxes with a grass floor probe anchored at (2,2) surface ≥ SEA_LEVEL),
      a well (8,8), and a farm (4×4..7×7 patch).
    - `generateMineshaft(cx, cz, worldSeed, blocks)` — every biome, 1/250
      per chunk-region. Vertical shaft (5..49 deep, ±1 3×3 carved column)
      with wood-trellis every 4 dy; 3..7 horizontal corridors 20..59 long
      with wood walls every 5 steps, `RAILS` on floor, `WEB` decor on ceiling
      with 1/12 chance per step.

The final `blocks[]` array is the full chunk buffer. `generateFlatChunk` in
`src/storage/WorldStorage.cpp` hands it back and `generateChunk` LZ4-compresses
and writes it into the plane's SQLite `chunks` table — see the
[Storage & Save Files](Storage-and-Save-Files) page for the blob layout.



## Nether, Aether, End generators

The three "special" plane generators are small — no Perlin density grid, no
surface-skin pass, no trees.

**Nether** (`netherChunk`, `src/storage/WorldGen.cpp:1103`):

```cpp
buildPermutation(worldSeed ^ 0x9E3779B97F4A7C15ULL, perm);

for lx in 0..15, lz in 0..15:
    for y in 0..127:                                    // only lower half of the chunk
        if y <= 4 or y >= 123:  blocks[lx, lz, y] = BEDROCK
        else:
            n = fbm3(cx·16 + lx, y, cz·16 + lz, 0.03, 3, perm);
            if n > -0.1: blocks[lx, lz, y] = NETHERRACK
            else if y <= 31: blocks[lx, lz, y] = LAVA
generateNetherOre(cx, cz, worldSeed, blocks);            // host = NETHERRACK, same vein profile as Overworld
```

The plane's body stops at y = 127 — sections 8..15 of the chunk stay pure air.

**Aether** (`aetherChunk`, `:1138`):

```cpp
buildPermutation(worldSeed ^ 0xD1B54A32B4D5EDB5ULL, perm);

for lx, lz:
    for y in 30..100:                                    // void gap above and below
        if fbm3(cx·16 + lx, y, cz·16 + lz, 0.02, 3, perm) > 0.25:
            blocks[lx, lz, y] = SANDSTONE
generateAetherOre(cx, cz, worldSeed, blocks);             // host = SANDSTONE, y 30..100, halved vein counts
```

Aether is an Enhanced Vanilla mod plane, not vanilla Minecraft. The halved
vein counts (12/6/3/1/2/3 vs Overworld's 27/10/4/2/2/2) exist because sky
islands are sparse: veins landing on air are no-ops but they still burn the
RNG budget, so the walk is deliberately shortened.

**End** (`endChunk`, `:1165`):

```cpp
buildPermutation(worldSeed ^ 0x1F123BB72B45F3B1ULL, perm);

wx = cx·16 + lx;  wz = cz·16 + lz;
distFromCenter = sqrt(wx² + wz²);

for lx, lz:
    for y in 40..80:
        n = fbm3(wx, y, wz, 0.02, 3, perm);
        if distFromCenter < 200.0 and (n + (1 - distFromCenter / 200.0)) > 0.5:
            blocks[lx, lz, y] = END_STONE
generateEndOre(cx, cz, worldSeed, blocks);                // no-op stub
```

The radial mask (`distFromCenter < 200.0`) means End is a *bounded* plane —
chunks outside 200 world-units from (0,0) generate to empty air, so the
"monoliths" only exist near the origin. `generateEndOre` is a documented stub
kept so the four plane ore helpers (`Overworld`, `Nether`, `Aether`, `End`)
stay symmetric across the API.

## Superflat presets (`GenStyle::SUPERFLAT`)

Selected by `[world] superflat-preset`, read inside `generateFlatChunk` on every
chunk when `GenStyle::SUPERFLAT` is active (`src/storage/WorldStorage.cpp:184`).
Every layer stays in Y 0..7 so the whole world fits inside section 0.

| Preset | Layers | Notes |
| ------ | ------ | ----- |
| `minecraft:flat` | BEDROCK Y=0, STONE Y=1..4, DIRT Y=5, GRASS Y=6 | Classic superflat surface; player spawns at Y=7. Also the fallback when `superflat-preset` is unknown (logs a warn). |
| `minecraft:stone` | BEDROCK, STONE Y=1..5 with xorshift32 variant meta patches at Y≤3, DIRT Y=6, GRASS Y=7 | Deterministic stone variants (meta 2/3/5 = granite/diorite/andesite). ~9 % of the stone surface gets a variant. |
| `minecraft:water` | BEDROCK, STONE Y=1..4, DIRT Y=5, WATER Y=6 | Flat water world. |
| `minecraft:natural_hills` | seeded from `worldSeed ^ (worldSeed >> 32) ^ 0x1337ULL` | Rolling hills superflat via 16×16 noise. |
| `minecraft:desert` | same seed mix, sand palette | Flat sand world. |
| `minecraft:mountains` | same seed mix | Elevated stone caps. |
| `minecraft:islands` | same seed mix | Scattered island patches. |

Superflat presets use their own noise helper (an inline xorshift32 / hash-based
noise), **not** the Perlin in `src/storage/WorldGen.cpp`. They still fold
`world-seed` into their key so `/reload` stays stable across restarts.

## Test grid (`GenStyle::TEST_GRID`)

The protocol-validation preset. Deterministic, per-chunk, and deliberately
spiky so a decoder bug is visible on-screen:

| Y | Layer |
| - | ----- |
| 0 | BEDROCK |
| 1..4 | STONE |
| 5 | DIRT |
| 6 | per-world-coord surface: `wx % 4 == 0 && wz % 4 == 0` → AIR (grid gap), else `wz % 2 == 1` → GOLD_BLOCK (Z stripes), else `wx % 2 == 1` → IRON_BLOCK (X stripes), else **random legacy id in [0, 197]** (the "wool" checker cell) |
| ≥ 7 | AIR |

Player spawn in the TEST_GRID preset is at (0, 7, 0), the exact first non-air
block above the Y=6 surface. The random legacy id in the checker cell is
deliberate: it makes this preset useful for **validating that a client decoder
tolerates ids outside the palette**, not just palette ids. `chest_test.py` and
the other Python probes in [Testing](Testing) lean on this preset when they
want deterministic chunks.



## Chunk dispatch and entry points

`WorldStorage::generateChunk(cx, cz)` (`src/storage/WorldStorage.cpp:687`) is
the only real writer; it always writes the modern full-chunk layout. Callers:

| Path | Synchronous? | Where |
| ---- | ------------ | ----- |
| Login burst | **yes** | `WorldManager::sendFlatChunk` / `sendPlayPackets` — the ~150 ms hitch at rd=5 the operators accept. |
| Movement crossing | **no** | `updateChunksAroundPlayer` calls `requestChunk` (queue) and `getChunkBlocksIfReady` (skips if not ready). The per-plane worker drives `generateChunk` on its own thread. |
| Block edit (`saveBlockChange`) | **yes** | `saveBlockChange` on a not-yet-generated chunk calls `generateChunk` first, then applies the single edit. Recursion is safe because `m_saveStmtMutex` is `std::recursive_mutex`. |
| `pregenerateWorld(radius)` | **yes** | Walks the square `[−radius, +radius)²` and calls `generateChunk` for every missing chunk; each one logs a `bench` line. **Currently commented out at both call sites** — see the mismatch note below. |

Inside `generateChunk`, the transaction sequence is:

1. Lock `m_saveStmtMutex` (recursive, shared with `saveBlockChange`).
2. `if (isChunkGenerated(cx, cz)) return;` — cheap SQLite EXISTS on
   `generated_chunks`; uses its own local stmt so it never contends with
   either mutex.
3. Build the raw buffer of `UNCOMPRESSED_CHUNK_SIZE` bytes and call
   `generateFlatChunk(m_genStyle, rawBuffer, cx, cz, m_worldSeed)`.
4. `compressBuffer` (LZ4) and drive `m_saveChunkStmt`
   (`INSERT OR REPLACE INTO chunks …`) inside a `BEGIN TRANSACTION; … COMMIT;`
   wrapper.
5. Separately `INSERT` into `generated_chunks` (fresh local stmt). That row is
   what `isChunkGenerated` sees from then on, so a chunk of pure air (a valid
   "empty" result of, say, a distant End chunk) is still recognised as
   generated.

The [Storage & Save Files](Storage-and-Save-Files) page covers the mutex topology,
the async worker queue, and the two blob layouts in more depth.

## Standing-surface helpers

Three helpers in `WorldStorage.hpp` are used by player / mob spawn code and by
the `/gotor` plane switch. They read chunks via `readChunkBlob` on
`m_getStmtMutex` (the **read** path, never the generation write path):

```cpp
uint16_t blockAtWorld(int wx, int wy, int wz);           // (id << 4) | meta or 0 if air/unknown
bool     canStandAtWorld(int wx, int wy, int wz);        // solid at wy-1, air at wy, air at wy+1
int      findStandingFeetY(int wx, int wz, int hintY);   // nearest Y in [MIN_WORLD_Y, MAX_WORLD_Y]
                                                         // satisfying canStandAtWorld; -1 if none
```

`canStandAtWorld` is the vanilla Java Edition "1 wide / 2 tall standing entity
fits here" predicate — solid below the feet and two air blocks above. Both
`EntityManager` (per-login mob seeding; see `src/world/EntityManager.cpp:668` and
`:785`) and `PlayerManager::sendPlayPackets` (first-login spawn) call
`findStandingFeetY` so a player or mob never spawns buried inside generated
terrain or inside a player-built wall. `NetworkWorlds.cpp::setPlayerWorldId`
also uses `findStandingFeetY(wx, wz, 62)` when teleporting a live session to
a new plane.

`spawn_probe.py` in the [Testing](Testing) page asserts on this Y band after
login (`y ≈ 8.5–10.5` in a fresh Overworld) — that assertion is what catches
regressions in these three helpers.



## Doc/code mismatch

> **`pregenerateWorld` is disabled at both call sites.** `NetworkSocket.cpp::NetworkManager::start` (line ~193, the `for (auto &kv : m_worlds) kv.second->pregenerateWorld(20);` block) and `NetworkWorlds.cpp::createWorld` (line ~210, the `storage->pregenerateWorld(12);` line) are **commented out**. The function itself is still defined and works correctly — see `src/storage/WorldStorage.cpp:730`. Until those lines are uncommented, **all** generation is on-demand: login burst (synchronous), movement crossing (queued on the per-plane worker), and block edits (synchronous). The startup log line `"Multiworld ready: pre-1.12 dimension IDs …"` still prints — it does not indicate that pregeneration ran. A fork that wants cold-start pregeneration should uncomment those two lines and pick a radius; a full radius-20 pass takes ~2 500 chunks per plane.

Two related notes:

> **`superflat-preset` reads on every chunk** (`WorldStorage.cpp:184`). If you plan to hot-swap the preset at runtime, this is the swap point. It's only hot when `GenStyle::SUPERFLAT` is active.

> **Comments in `WorldStorage.cpp` say "24 sections / 196 608 bytes"** in several places around `readChunkBlob` and the async worker. The authoritative constant is `NUM_SECTIONS = 16` / `UNCOMPRESSED_CHUNK_SIZE = 131 072` in `include/storage/WorldStorage.hpp`, enforced by a `static_assert` (the wire sectionBitmask is 16-bit). The header wins. See the same callout on [Storage & Save Files](Storage-and-Save-Files).

## Related pages

- [Storage & Save Files](Storage-and-Save-Files) — chunk blob layout, LZ4, mutexes,
  `generated_chunks` bookkeeping, and the async generation worker.
- [Multiworld & Dimensions](Multiworld-and-Dimensions) — plane table, `world_registry`,
  per-plane `active` flags, `/world create`, `/gotor`, and how the plane preset
  is picked.
- [Configuration](Configuration) — every `server.properties` key including the
  `[world]` block on this page.
- [Testing](Testing) — the Python probes that assert on this generator
  (`spawn_probe.py` and `packet_probe.py` in particular), `/testpackets`, and the
  offline chunk verifier in `tools/verify_chunk_packet.cpp`.
- [Protocol & Clients](Protocol-and-Clients) — how the 16-section chunk ceiling
  interacts with the wire `sectionBitmask`, and where era adapters sit.
- [Game Rules](Game-Rules) — gamerules that gate spawn behaviour (`spawn_mobs`,
  `spawn_monsters`, `spawn_patrols`, `spawn_phantoms`, `spawn_wandering_traders`,
  `spawn_wardens`, `max_entity_cramming`) which consume these generators'
  standing-surface helpers.

