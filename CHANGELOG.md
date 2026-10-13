# Changelog

All notable changes made to the project **LightweightMC** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and this project respects [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---
## [0.0.13] - 2026-12-10

### Added

- **`/world info <name>` subcommand** (`WorldCommands.hpp`, op-level 3). Resolves a
  plane name through the `world_registry` SQLite table and reports its dimension
  id, generation style and the per-plane SQLite file it serves (`world.db` /
  `world-nether.db` / `world-end.db` / `world-aether.db` for the fixed planes;
  `world-<safeName>.db` for custom planes, mirroring the path-traversal sanitizer
  in `createWorld`).
  - Backed by a new `NetworkManager::describeWorld(name, outId, outStyle, outDbPath)`
    built on `Database::lookupWorld` (which already returned id + style), so
    `/world info` reflects registered-but-inactive planes too, not just the
    live in-memory `m_worlds` set.
  - Wired through a new `CommandContext::describeWorld` closure in both
    console contexts (`Server::processConsoleInput`, `Server::startConsoleThread`)
    and the PLAY chat context (`PlayerHandler.cpp`), matching how
    `createWorld` / `listWorlds` are already wired.

### Fixed

- **Console `/gotor <world>` rejected the Nether plane.** The console lambdas
  tested `lookupWorldId(...) < 0` to detect "not found", but the function uses
  `-2147483648` as its sentinel and Nether's pre-1.12 id is legitimately `-1`
  (`NetworkManager.hpp` still documented the stale "-1 when not found" contract).
  Both `Server::processConsoleInput` and `Server::startConsoleThread` now
  compare against the sentinel, matching `PlayerHandler.cpp`'s existing check.
- **Heap corruption seen in the chest→reorg test sequence** — the inventory
  deserializer in `PlayerSession.cpp` used an always-true unsigned guard
  (`slot >= 0` where `slot` is a `uint16_t`) before indexing the output
  vector, so a bad wire slot wrote past the end of the vector and corrupted
  the heap; the corruption then surfaced as a segfault during the later
  reorg test. Guarded with `static_cast<std::size_t>(slot) < out.size()`.
- **Chest test exit-code inversion** (`chest_test.py`): the PASS branch
  returned non-zero and the WARN condition drove `return 0`, so the runner
  recorded the opposite verdict; fixed the PASS branch to `return 0`.
- **Unused-constant warning noise** — `WOOL` block id in `WorldStorage.cpp`
  marked `[[maybe_unused]]` so the build stays warning-clean.

---
## [0.0.12] - 2026-10-10

### Added

- **Per-chunk overworld biome picker** (`Gen::biomeForChunk`). Mirrors vanilla
  1.7's `RegionChoose.cpp`: two coarse 2D noises (height at ~1/32 chunks,
  temperature at ~1/11 chunks) map to six regions — FOREST, PLAINS, DESERT,
  ARCTIC, OCEAN, MOUNTAINS — so adjacent chunks agree on their biome and
  biomes appear as ~30-chunk (480-block) patches.
- **Biome-specific surface materials** inside `overworldChunk`: FOREST/PLAINS
  stay GRASS/DIRT, DESERT becomes SAND/SANDSTONE, ARCTIC becomes SNOW/DIRT,
  OCEAN becomes SAND/SAND, MOUNTAINS becomes COBBLESTONE/STONE.
- **Biome altitude bias**: OCEAN pushes the density threshold down
  (−0.25) so water actually dominates those chunks; MOUNTAINS pushes it up
  (+0.20) so mountains read as raised stone.
- **Biome-aware vegetation and structures**:
  * Tree density is per-biome (Forest 3%, Plains 0.8%, Desert 0.4%,
    Arctic/Ocean 0%, Mountains 1.2%). Trees now span the whole 16×16 chunk
    instead of the previous 8×8 sub-block that made forests look sparse.
  * Trunk height is randomised 4..6 blocks (vanilla oak is `4 + nextInt(3)`),
    and the canopy is a squashed sphere so tall and short oaks look varied
    instead of "all oaks are exactly 5 tall".
  * DESERT gets sparse CACTUS (1..3 tall) instead of oaks; ARCTIC/OCEAN
    get no trees; grass/flower decor is only placed on grass-topped biomes.
- **Real village clusters**: `Gen::generateVillage` now seeds 4-7 houses
  (5×5..7×7 with pitched roofs and a doorway), one cobble WELL, one DIRT+WHEAT
  farm, and one fenced animal PEN, each at its own chunk anchor inside a
  16×16 chunk region (256×256 blocks). Neighbouring chunks see the same
  layout, so a walk across chunk seams reveals a real Minecraft village
  instead of "one box house per chunk."
- **Real mineshaft shape**: `Gen::generateMineshaft` now starts with a
  3×3-column **vertical shaft** (startY = 15..50 down 5..45 blocks) lined
  with WOOD cross-beam supports every 4 blocks, then walks 3-7 horizontal
  corridors that carve 3×3 tunnels with the same WOOD support pattern and
  RAILS on the floor. Spider webs are sprinkled on the ceiling. This
  matches vanilla's `MinShaft.cpp` structure and fixes the previous
  FENCE+PISTON_HEAD supports that read as a bizarre squiggle.

### Fixed

- **Tree height uniformity**: `for (h = 1; h <= 5; ++h)` replaced with
  `4 + nextInt(3)`; forest height now varies per tree as in vanilla.
- **Tree sparsity**: the tree loop was clipped to `lx, lz ∈ [4, 11]`
  (an 8×8 patch inside the 16×16 chunk), which left the outer 3/4 of
  every chunk bare of trees. Now uses `lx, lz ∈ [2, 13]` with a small
  margin so canopies fit within the chunk.
- **Village "one box house"**: previous `generateVillageComponents`
  placed a single 5×5×4 box per 32×32 chunk region; new multi-house
  cluster covers villages properly.
- **Mineshaft "bizarre shape"**: previous `generateMineshaft` skipped
  the vertical shaft entirely and used FENCE/PISTON_HEAD as "supports",
  which reads as a squiggle of strange blocks in game.

---
## [0.0.11] - 2026-10-10

### Added

- **Per-plane ore generation** (`Gen::generateOverworldOre` / `generateNetherOre` /
  `generateAetherOre` / `generateEndOre`). Each wrapper drives the vanilla 1.7.x
  ore vein distribution:
  * Coal: 27 veins / max 12 blocks / y 5..132
  * Iron: 10 veins / max 5 / y 5..52
  * Gold: 4 veins / max 5 / y 0..31
  * Lapis: 2 veins / max 5 / y 0..31
  * Diamond: 2 veins / max 5 / y 0..15
  * Quartz: 2 veins / max 5 / y 16..71
  and swaps in the correct **host material** per plane:
  * Overworld → `STONE` (existing behaviour)
  * Nether → `NETHERRACK` (vanilla Java shares one `createOre()` across planes)
  * Aether → `SANDSTONE` (the sky-island material `aetherChunk` lays down) with
    density halved and y band clipped to the aether elevation range (y 30..100)
  * End → intentionally no ore (matches vanilla End.cpp: End is a hollow wall
    with no ore veins by design). The stub exists so plane wiring stays
    symmetric and to enable sparse gems later without touching call-sites.
- **`Gen::generateOre` host-material parameter**: the vein walker now takes a
  `hostId` (default `STONE`) so a vein only replaces blocks matching the plane's
  rock. This is what lets Nether veins appear inside pink Netherrack and Aether
  veins inside Sandstone rather than only inside STONE.

### Fixed

- **`Gen::generateOre` per-chunk seed derivation**: old signature `(void)cx;
  (void)cz;` — the seed was `seed ^ oreType`, so **every chunk showed identical
  ore layouts** (visible tiling). Fixed by mixing `worldSeed ^ mix(cx) ^ mix(cz)
  ^ mix(oreType) ^ mix(hostId)` into the LCG state before the vein walk.
- **Vein-walk algorithm**: previous code scattered blobs by picking random
  offsets from a fixed start; replaced with vanilla Java's cumulative walk
  (`x += randInt(-1, 1); y += randInt(-1, 1); z += randInt(-1, 1);` each step)
  plus a per-vein randomized length in [2, veinSize] rather than always the
  max. Veins now look vein-shaped (chain-shaped) rather than round blobs.
- **Overworld chunk ore coverage**: replaced the 3-call block (coal/iron/gold
  only, non-vanilla counts: coal 20 veins / size 16 / y 0..128; iron 20 veins
  / size 8 / y 0..64; gold 2 veins / size 8 / y 0..32) with the vanilla-values
  call to `generateOverworldOre`.
- **Nether / Aether / End chunks now call an ore pass at all**: previously
  none of `Gen::netherChunk` / `Gen::aetherChunk` / `Gen::endChunk` invoked
  `generateOre` (that's why "genstyle aether/end/nether had no proper ore
  generation").

### Files

- `include/storage/WorldGen.hpp`: new `generateOre` signature with trailing
  `std::uint16_t hostId = Blocks17::STONE`; new declarations for the four plane
  ore wrappers.
- `src/storage/WorldGen.cpp`: rewritten `generateOre` (per-chunk seed, host-id
  check, cumulative vein walk, variable vein length); new `generateOverworldOre`
  / `generateNetherOre` / `generateAetherOre` / `generateEndOre`; calls to the
  wrappers added at the tail of `overworldChunk` / `netherChunk` / `aetherChunk`
  / `endChunk`.

---
## [0.0.10] - 2026-10-10

### Added

- **Off-thread chunk generation (one worker thread per WorldStorage)**: `WorldStorage`
  now owns a `std::thread` generation worker plus a mutex-guarded `m_genQueue` /
  `m_genQueued` pending-chunk queue. `requestChunk(cx, cz)` submits a chunk to the queue
  (idempotent via `isChunkGenerated` fast-reject + in-queue dedup) and lazily spawns the
  worker on the first call so a plane that is never looked at costs no threads.
  `getChunkBlocksIfReady(cx, cz)` reads the DB blob for a chunk but **never**
  triggers a sync `generateChunk()` on miss; it returns an empty map so the caller
  can skip that chunk this tick and let the worker finish it in the background.
  `isChunkReady(cx, cz)` is the cheap cross-thread read-only check used by the send
  path. The `~WorldStorage` destructor flips `m_genStopFlag` + joins the worker so
  shutdown waits for any in-flight DB write before closing the SQLite handles.
- **Async chunk streaming flush tick**: `NetworkManager::tickChunkStreaming(renderDistance)`
  re-runs `WorldManager::sendMapChunkBulk` every game tick against every live
  PLAY session with `clientProtocol >= 47`. `Server.cpp::start()` throttles it to
  5 Hz (every 4th 50ms tick) so it does not compete with the 20Hz tick work. This
  pass exists mainly to catch chunks whose generation completed between a player's
  movement crossings but before the next crossing -- e.g. the "player stopped
  moving" tail case where the prefetch queue would otherwise sit forever.

### Changed

- **Chunk emission at high render distance**: `sendMapChunkBulk` (1.8+ streaming)
  now branches per chunk:
  * Login burst (`session.loadedChunks.empty()`) keeps the legacy sync path via
    `getChunkBlocks()` so login behaves identically to the pre-refactor server
    (a one-time hitch at login that the user has already accepted).
  * Movement crossings (any subsequent call) use `isChunkReady()` + `getChunkBlocksIfReady()`
    and skip chunks that are still pending on the worker; those skipped chunks were
    just `requestChunk()`'d by the prefetch pass, so they ship on the next crossing
    or on the periodic flush tick.
- **Directional prefetch in `updateChunksAroundPlayer`**: on each 1.8+ crossing
  the server submits a box **larger than the render box**, extended
  `frontPad = renderDistance` chunks in the direction the player just moved
  and `backPad = 1` chunk behind. This gives the worker a full render distance
  of lead time along the travel axis -- the "double the render distance ahead"
  behaviour the user sketched -- so high-speed travel (boat / flight /
  spectator cam) does not outrun generation. `session.prevChunkX` / `prevChunkZ`
  (added to `PlayerSession`) track the previous chunk so the direction is
  derived from `sign(currentChunk - prevChunk)`; the sentinel `INT_MIN` marks
  the login call so no directional bias is applied for the initial burst.
- **SQLite `sqlite3_stmt*` thread safety**: WorldStorage's prepared
  statement members (`m_getChunkStmt` for reads, `m_saveChunkStmt` for writes)
  are per-instance stateful and must not be driven concurrently. `m_getStmtMutex`
  guards `readChunkBlob()`, `m_saveStmtMutex` (a `std::recursive_mutex` so
  `saveBlockChange → generateChunk` nesting doesn't deadlock) guards `generateChunk()`
  and `saveBlockChange()`. Two separate mutexes let the network thread's chunk
  reads overlap with the generation worker's writes.

### Design notes

- Thread granularity is **per WorldStorage (per plane)**, not per client.
  One-thread-per-client would spawn a terrain thread per concurrent player
  (50 players → 50 threads), all competing for the same `sqlite3_stmt*` handle
  in one SQLite DB -- a serialization bottleneck dressed up as parallelism.
  One global thread works but serializes cross-plane generation (Nether and
  Overworld players compete for the same queue). Per-plane threads (typically
  3-4 across Overworld / Nether / End / Aether) give independent per-plane
  progress while bounding total thread count by the number of active worlds.
- Prefetch is directional (biased along the player's velocity vector), which
  is what keeps high-speed travel from lagging the server: the worker only
  generates chunks the client is about to look at, so generation cost stays
  proportional to `renderDistance` per plane rather than 2-3x that per client.

### Files

- `include/storage/WorldStorage.hpp` / `src/storage/WorldStorage.cpp`: worker
  thread, async queue, `requestChunk` / `isChunkReady` / `getChunkBlocksIfReady`,
  stmt mutexes, dtor join.
- `include/player/PlayerSession.hpp`: new `prevChunkX` / `prevChunkZ` fields for
  velocity-derived prefetch (initialized to `INT_MIN` sentinel to detect the
  login burst).
- `src/managers/WorldManager.cpp`: streaming `sendMapChunkBulk` branches on login
  vs movement; `updateChunksAroundPlayer` submits a directional prefetch box to
  the WorldStorage worker before invoking the send.
- `include/network/NetworkManager.hpp` / `src/network/NetworkWorlds.cpp`:
  `tickChunkStreaming(renderDistance)` periodic flush.
- `src/core/Server.cpp`: game tick loop throttles the flush pass to 5 Hz.

---
## [0.0.9] - 2026-10-10

### Changed

- **Minecraft Protocol 1.8 (v47), Map Chunk Bulk (S2C 0x26)**: `sendMapChunkBulk` is now
  the **active streaming entry point**: it walks the not-yet-loaded chunks inside the
  render box and emits ONE 0x26 packet per chunk (chunkCount pinned to 1) in vanilla's
  outer-body layout (byte hasSkyLight at body position 0 + varint chunkCount + metas
  + payloads). The old monolithic build (which packed every not-yet-loaded chunk within
  the render box into one 0x26 body and blew the 21-bit / 2,097,151-byte wire cap enforced
  by `NetworkFraming::sendPacket` at render distance >= 3, silently dropping the body and
  stalling the world past the initial view) is retained as `sendMapChunkBulkMonolithic`
  for archival/reference -- it works at render distance 1..2 but is not the active path.
  `updateChunksAroundPlayer` now calls the streaming `sendMapChunkBulk` **once** for 1.8+
  clients (the loop is inside the function, matching how vanilla's jar server emits per-
  chunk packets verified by Wireshark traces), and only iterates `sendFlatChunk` (0x21)
  for legacy (<1.8, protocol < 47) clients.

### Fixed

- **Minecraft Protocol 1.8 (v47), Chunk (S2C 0x21)**: `sendFlatChunk` now skips
  emission for chunks whose `sectionBitmask == 0`. Vanilla java's `readChunk`
  short-circuits when the bitmask is empty -- it reads only `int x, int z, byte
  groundUp, short mask` and does **not** consume the VarInt length prefix or
  the DEFLATE blob we used to emit. Every air chunk inside the render box
  left ~15 bytes of garbage in the socket stream, which desynchronised every
  subsequent 0x21 body and made the JVM client render the entire remainder of
  the render box as void.
- **Minecraft Protocol 1.8 (v47), Map Chunk Bulk (S2C 0x26)**: `sendChunkBulkSingle`
  now writes the vanilla outer header (one `hasSkyLight` byte at body position 0
  + varint chunkCount) instead of a spurious `compressLevel=0` outer byte plus a
  per-chunk `hasSkyLight` byte. Those two extras against vanilla's one outer
  skylight byte were the "1 extra byte" observed on packet 38 (0x26) in the
  wire. Removing the per-chunk byte and switching the outer byte's meaning from
  compressLevel to hasSkyLight also flips vanilla from DEFLATE-inflating the
  body (compressLevel=1 in the previous version) to using the 12,288-byte
  section width that matches the payloads we emit.

### Added

- **Chests**: `handleBlockPlacement` now detects *double chests* by probing the
  four horizontal neighbours of a clicked chest (IDs 54, 130, 146); the
  `0x2D Open Window` payload advertises 54 slots for a detected pair (vs.
  27 for a single chest). Chest blocks continue to be persisted through
  `WorldStorage::saveBlockChange`, so they survive respawn / reconnect
  without needing any respawn-specific packet to re-emit them.
- **Network safety**: `NetworkManager::sendPacket` now rejects any packet whose
  body exceeds the 21-bit wire length limit (2 097 151 bytes). Callers that
  forget to split no longer corrupt the client stream; the server logs the
  oversized packet and drops it instead of emitting half a frame.

### Changed

- **Map Chunk Bulk (0x26)**: `WorldManager::sendMapChunkBulk` is now the pre-RAM-fix
  monolithic build restored from V0.0.5 (git 9620a09). It builds **one** 0x26 body
  (leading skyLight byte + varint chunkCount + per-chunk metas + concatenated
  chunk payloads) that contains every not-yet-loaded chunk in the render box and
  sends it in a single shot. The V0.0.6 / V0.0.7 streaming split (which emitted
  several consecutive `0x26` bodies to keep RAM bounded) broke loading on the wire:
  the JVM 1.8 client accepts at most one Map Chunk Bulk per world-load stage, so
  subsequent split bodies were dropped and the client rendered chunks as air. The
  pre-RAM monolithic is what the client actually expects and the state where
  "loading was correct" (cosmetically, pair chunks had generated heights and
  impair chunks had air up to y=80 because the active preset populated only the
  bottom five sections, but every chunk was received). RAM note: this peaks at
  ~10 MB above baseline for a 7x7 view and the body may exceed the 2 MiB wire
  length cap; `NetworkFraming::sendPacket` still refuses and logs any oversized
  body rather than corrupting the stream.
- **World constants (`WorldStorage.hpp`)**: the stale comments
  ("Y = 0 .. 384, i.e. 25 sections", "// 25", "24 * 8192 = 196 608")
  are corrected to reflect the actual 16-section range
  (`MIN_WORLD_Y = 0 .. MAX_WORLD_Y = 255`). A `static_assert` documents
  the wire-level 16-section ceiling so a future bump to `MAX_WORLD_Y`
  cannot silently produce an unrepresentable section bitmask.
- **`minecraft:mountains` preset (`WorldStorage.cpp`)**: terrain height
  clamp tightened from `[5, 250]` to `[5, 100]` to match the comment's
  own "Y=110" intent. Keeps mountains inside sections 0..6, halving per-
  chunk payload.

### Fixed

- **Minecraft Protocol 1.8 (v47)**: root cause of the client-side
  *CorruptedFrameException: length wider than 21-bit* under the
  `minecraft:mountains` preset. The monolithic `0x26` build
  previously pushed 7×7 chunks × 16 populated sections × ~196 KiB/chunk
  ≈ 9.6 MB in a single packet, overflowing the wire length varint.
  `NetworkFraming::sendPacket` now rejects any body whose length exceeds
  the 21-bit wire limit (2 097 151 bytes), logs it, and drops the frame
  instead of emitting half a frame — the client stream stays aligned.
  The mountains preset's height clamp (`[5, 100]`) keeps populated
  sections inside 0..6, so a normal 5-radius bulk body stays well under
  the cap. If it ever overflows, the log names the offending packet
  size and the preset / renderDistance to tune.


## [0.0.6] - 2026-10-04

### Added

- **Player management**:Added player persistance
- **Player management**:Completed Player data structure to latest minecraft "player.dat nbt strucuture"
- **Biomes & presets generation**: added generation for test world generating each block

### Changed

- **Commands** Refactored command implementation to put them in dedicated headers, to limit further cluter and kep files readable 
- **Minecraft Protocol** Mockup of handling different protocol, will surely be scrapped for a per protocol basis, or another style, maybe doing a c++ conversion of what Viaversion is doing in java
- **Console** Better Behaviour and command history

### Fixed
- **Minecraft Protocol 1.8 (v47)**: Crash when dealing with interactive blocs operations (chest / anvil / etc.)


## [0.0.5] - 2026-09-30

### Added

- **2D relief generator**: Added preset based on procedural noise (Perlin/Simplex).
- **Static fluid management**: Generation of water lakes below sea level ($Y=12$) and sand/dirt layers.
- **Biomes & presets generation**: Preset support for `minecraft:natural_hills`, `minecraft:mountains`, `minecraft:desert`, and `minecraft:islands`.
- **MC 1.8 ore generation**: Deterministic integration of diamond, redstone, gold, lapis lazuli, iron, and coal veins according to original Y height levels.
- **Fallback logic**: Logger warning and automatic fallback to `minecraft:flat` if an unknown preset is provided.
- **Minecraft Protocol 1.8 (v47)**: Packet implementation for `Map Chunk Bulk` (`0x26`) for bulk chunk loading at startup.

### Fixed

- **Network serialization (Protocol 47 / MC 1.8)**: Binary alignment fix and Little-Endian encoding (`writeU16BE`) for blocks in the `Map Chunk Bulk` packet (`0x26`), resolving the client crash *"length wider than 21-bit"*.
- **Negative coordinates continuity**: Indexing calculation fixes to eliminate visual shifts and cliff breaks on negative axes (`worldX`/`worldZ`).
- **Memory management**: Array bounds checking secured in 2D noise and fluid filling (`WATER`), resolving the `double free or corruption` server crash.
- **Chunk management**: Fixed chunk memory allocation and tracking.

---

## [0.0.4]- 2026-09-16

### Added
- **Biomes & presets generation**: Support for `minecraft:flat`, `minecraft:stone`, `minecraft:water`, and `overworld` presets.
- **Management**: Added terminal command handling support (mockup).

### Changed
- Refactored the chunk storage system to optimize RAM usage.
### Fixed

- **Network serialization (Protocol 47 / MC 1.8)**: Better network stability.
---

## [0.0.3]- 2026-09-08


### Added
- **SQLite storage engine (WAL Mode)**: LZ4 compression/decompression implementation for chunk data.
- Initial support for Big-Endian / Little-Endian serialization per network protocol specifications.
- Chat and in game command support (basic)
---

## [0.0.2]- 2026-09-02

### Added
- **SQLite storage engine (WAL Mode)**: Persistence implementation.
- Multi-threaded handling of network compression and disk I/O operations.

### Fixed
- Server stability during rapid player disconnects and reconnects.

---

## [0.0.1]- 2026-09-02

### Added

- **Initial C++20 architecture**: Creation of the main server loop, non-blocking TCP socket management.
- Basic support for Handshake, Ping/Status (Server List Ping), and player connection protocol.
- Support for `minecraft:flat` flat world (Bedrock + Dirt + Grass).

---

[0.0.6]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.6
[0.0.5]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.5
[0.0.4]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.4
[0.0.3]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.3
[0.0.2]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.2
[0.0.1]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.1