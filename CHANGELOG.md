# Changelog

All notable changes made to the project **LightweightMC** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and this project respects [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---
## [0.0.15] - 2026-10-13

### Added

- **Per-plane inventory policy** (`world_registry` `inventory_policy` + `force_gamemode` columns, new `player_inventories` SQLite table). SYNC (shared blob, default) / SEPARATE (per-plane blob) / RESTRICTED (one-way). `force_gamemode` pins a plane's game mode on entry. See wiki "Plane inventory policy".
- **`Storage::Database` plane-scoped inventory API** (`setWorldInventoryPolicy`/`getWorldInventoryPolicy`, `setWorldForceGameMode`/`getWorldForceGameMode`, `initPlayerInventoriesTable`, `savePlayerInventory`, `loadPlayerInventory`, `loadSyncInventoryBlob`).
- **`WorldManager` policy mirrors** (`m_inventoryPolicy`, `m_forceGameModeOnEntry`) seeded at boot from `[world] aether-inventory-policy` / `[world] aether-force-gamemode`; policy-aware transfer in `setPlayerWorldAt`, restore in `loadPlayerData`, save in `savePlayerState`.
- **`server.properties` auto-backfill** for the two new `[world]` keys via `ConfigManager::forEachCanonicalDefault` (covers fresh + legacy-schema files).

- **World-control commands wired end-to-end** (`/seed`, `/setworldspawn`, `/setblock`, `/place`, `/spawnpoint`): parse args, resolve plane via `world_registry`, persist to SQLite + WorldStorage.
- **`world_registry` world-control columns** (`spawn_x`/`spawn_y`/`spawn_z` DOUBLE, `world_seed` INTEGER, ALTER-TABLE migration for legacy DBs); `WorldManager` hot-path mirrors (`m_worldSpawn`, `m_worldSeed`).
- **`Storage::Database` world-control API** (`setWorldSpawn`/`getWorldSpawn`/`setWorldSeed`/`getWorldSeed`/`updatePlayerSpawn`) and **`NetworkManager` backend** (`setWorldSpawnByName`/`getWorldSpawnByName`/`setWorldSeedByName`/`getWorldSeedByName`/`setBlockAtPlane`/`updatePlayerSpawnByName`) + new `CommandContext` hooks and `senderWorldId`.
- **`/world create` flags** `--inventory <standard|separate|restricted>`, `--force-gamemode <mode>`, `--seed <int>`; widened `createWorld` signatures (trailing defaults preserve older two-arg call sites).
- **`flat` style alias** for `Storage::GenStyle::CREATIVE` in `styleFromName` (no new `FlatGen.cpp` / CMake target needed).
- **gzip-backed world loading (Phase 8C):** legacy `.mcc`/`.dat` region reader uses gzip framing for Minecraft world import.
- **Phase 8c/8d — NBT `level.dat` sidecar + save-all / save-on / save-off:** new `Storage::LevelDatSnapshot` POD on `Database.hpp` models vanilla 1.8's `level.dat` fields (format / seed / time / modified / StartPos / mode / GameMode / used_blocks / MapStatChunk / MapStatTrackChunks). `Database::writeLevelDat` composes the compound payload in the vanilla tag order (Int, Long, Long, Long, Int, ByteArray, Int, Int, Byte, String) via an anonymous-namespace big-endian writer helper and pipes it through `RegionFile::compressGzip`, producing a standard gzip-compressed NBT file readable by any vanilla 1.8 tool. `Database::readLevelDat` gunzips via `RegionFile::decompressGzip` and walks the tag stream with a recursive skip-tag lambda so foreign / external `level.dat` files (which include tags this engine does not model) stay parseable — unmodelled fields retain struct defaults.
- **`NetworkManager::saveAllPlanes(int64_t nowMs)`:** iterates every live plane in `m_worlds`, resolves its db path via `describeWorld` (same sanitizer used by /import-world and /export-world so custom planes cannot smuggle path traversal), hydrates a `LevelDatSnapshot` from `world_registry` (spawn_x/y/z, world_seed, force_gamemode) plus live `WorldStorage::getWorldSeed` state, then writes `<dbPath>.level.dat`. Returns true iff at least one plane round-tripped so /save-all can honestly distinguish "saved" from "no live planes".
- **Autosave cadence:** `tickChunkStreaming` now checks elapsed wall-clock ms on a 5-minute window; when the elapsed threshold passes and `m_autosaveEnabled` is true, saveAllPlanes fires on boot + every 5 min so a crash between SQLite WAL fsyncs cannot lose the level.dat sidecar's spawn/seed/modification timestamps.
- **`/save-all`, `/save-on`, `/save-off` command wiring:** new `CommandContext` `std::function` hooks (`saveAllPlanes`, `setAutosaveEnabled`, `getAutosaveEnabled`); wired in both Server.cpp console contexts (`this`-capture and `network`-capture threaded loop) plus PlayerHandler.cpp chat context. `/save-all` prints `[Saved world to disk.]` on success and `World backup skipped (no live planes to flush).` otherwise; `/save-on` / `/save-off` toggle NetworkManager::`m_autosaveEnabled` and echo the post-toggle state.

### Notes

- Existing 0.0.14 `server_data.db` keeps working via CREATE/ALTER migrations; vanilla planes stay on SYNC/INHERIT so the 0.0.14 4-packet plane-switch wire sequence is unchanged.
- All 5 regression tests (chest/reorg/probe/spawn/bot) pass at exit=0 with zero `ERR]` boot-log messages.
- Roadmap and phase tracking live in `phases.md`; full design detail is on the wiki.

---
## [0.0.14] - 2026-10-13

### Added

- **`PlayerData::worldId`** persisted as `world_id INTEGER DEFAULT 0` in the `players` table; round-trips through login load + save so a `/gotor` switch survives relogin.
- **Per-plane coordinates** via new `player_positions` SQLite table keyed `(uuid, world_id)` (`x,y,z,yaw,pitch,updated_at`); `NetworkManager::setPlayerWorldAt` saves departure coords and loads arrival coords (or plane default spawn on first visit); `setPlayerWorldId` is now a wrapper over it. Guarded by `PlayerData::hasPositionData` so partial-write callers never clobber real values.
- **Sky-render trick for non-vanilla planes** (`WorldManager::setSkyDimension`/`skyDimensionFor`): remaps the wire `dimension` value to one of the three vanilla skies (`0`/`1`/`-1`) while server-side keys stay on the true plane id.
- **Wiki:** Multiworld-and-Dimensions "Sky render trick for non-standard planes" section.

- **Entity roster wire fidelity:** entity template table now carries a per-species numeric entity id (`EntityManager::templateId` / `mobId`) matching vanilla 1.8 spawn payloads (legacy-era op `0x0F Spawn Entity Living`, e.g. zombie=54, skeleton=51, sheep=58, chicken=56; passive/aggro rosters cite java mcsources-1.8 + SputnikMC template ids for custom wolves/tigers/etc.) so login seed spawns and mob models agree with what clients render; added `m_debugLoginSpawn` state to EntityManager plus the `CommandContext::despawnTestBatch` hook declaration in CommandManager (debug login-summon/despawn batch path -- not yet bound to a console command this batch).
- **`/gamemode` now forwards Change Game State:** `NetworkPlayerMeta` sends wire op `0x2B` (sub-action 3 = change game mode) instead of Tab List Update Player `0x38`, which only drives tab-list scoring -- `0x2B` is the vanilla-1.8 op clients use to observe survival/creative/adventure rule changes without reconnecting (PacketKind.hpp legacy-era naming cross-checked against `tools/protocol_seed/packet_ids_1.8.json` protocol-47 ids); CommandManager + NetworkManager hook docs updated with the same correction.
- **Phase 9 processing substrate (`ProcessingTick`):** new per-tick singleton sweeping per-plane registries (kind registry for ground items/crops/flows, sparse `TickRecord` on Chunk replacing the dense byte-slice record model -- wire voxel sections unchanged), TTL expiry path for dropped entities (`KIND_GROUND`) w/o SQLite churn; Server.cpp binds ChunkManager + every live plane id into the tick each 20 Hz pass.
- **DirtyWriter staged cell flushes:** new singleton staging chunk-cell edits into batched read/patch/write buckets dispatched via `WorldStorage::applyCellBatch` (registered per plane through `DirtyWriter::registerCellFlush`, re-pinned by `setPlaneWorldId` when NetworkWorlds/NetworkSocket assign non-default planes); lock order DirtyWriter mutex -> save-statement mutex documented in WorldStorage comments.
- **Ground-item storage API:** `Storage::Database` gained ground-item rows (`saveGroundItem`/`deleteGroundItem`/`loadGroundItems`) + ProcessingTick expiry consumes them; new `include/world/MC18ItemIds.hpp` constants header groups 1.8 item ids (materials/tools/food/misc) for upcoming drop tables and pickup paths.


### Fixed

- **Relogin after `/gotor` returned to Overworld:** `loadPlayerData` SELECT now includes `world_id`; login handler restores `session.worldId` before first chunk-stream.
- **`aether-special-dimension` (bool, never read) → `aether-plane-id` (int, default 100, read at boot):** threaded into registry row + `m_worlds` key with boot-time collision check vs vanilla slots (0/-1/1); `describeWorld` matches by name so re-homing keeps `world-aether.db`.

- **Vanilla "loading world" screen never closed on `/gotor`/portal:** `setPlayerWorldAt` now emits the required 4-packet stream (`0x07` Respawn → drop-point `0x26` → `0x08` absolute position with no trailing 1.9+ teleport VarInt → surrounding chunk stream), clearing `loadedChunks` and resetting `prevChunkX/Z` to the login sentinel first.
- **Scoreboard refresh UB on mid-refresh disconnect:** inline `pollEvents` loop replaced with `ScoreboardManager::tick` which snapshots live fds before dispatch (fixes ASan heap-use-after-free in `bot_test`).
- **`hexPreview(body, bodyOff + packetEnd)` → `hexPreview(body, packetEnd)`** at `NetworkEvents.cpp:166`: `bodyOff` was already consumed, so the call over-read past the body (ASan heap-buffer-overflow, reproducible SIGSEGV breaking `run_tests.sh`).

- **Per-plane coordinates after `/gotor`:** `player_positions` table replaces the single flat position tuple so a plane switch drops the player at their prior position on that plane (or the plane default spawn) instead of the source-plane coordinates.

### Documentation

- **`PlayerSession.hpp`** plane-layout comment rewritten to the actual pre-1.12 layout (Overworld 0 / Nether −1 / End 1 / Aether `<aether-plane-id>` default 100 / custom 101+).
- **`WorldCommands.hpp`** `/world create` summary corrected to "allocate a new plane (id >= 101)".
- **Wiki** Multiworld & Dimensions: documents configurable `[world] aether-plane-id`; stale doc/code mismatch callouts replaced with a resolved-in-0.0.14 note (keeps the open `pregenerateWorld` callout).

---
## [0.0.13] - 2026-10-12

### Added

- **`/world info <name>` subcommand** (op-level 3): reports a plane's dimension id, gen style and per-plane SQLite file via new `NetworkManager::describeWorld` (built on `Database::lookupWorld`, so it lists registered-but-inactive planes too); wired through `CommandContext::describeWorld` in console + PLAY chat contexts.

### Fixed

- **Console `/gotor` rejected the Nether plane:** "not found" check compared `< 0` but the sentinel is `-2147483648` and Nether's id is legitimately `-1`; both console contexts now compare against the sentinel.
- **Heap corruption in chest→reorg sequence:** inventory deserializer's always-true unsigned guard (`slot >= 0` on a `uint16_t`) let a bad wire slot write past the vector; guarded with `static_cast<std::size_t>(slot) < out.size()`.
- **Chest test exit-code inversion** (`chest_test.py`): PASS branch returned non-zero; fixed to `return 0`.
- **Unused-constant warning noise:** `WOOL` block id in `WorldStorage.cpp` marked `[[maybe_unused]]`.

---
## [0.0.12] - 2026-10-10

### Added

- **Per-chunk overworld biome picker** (`Gen::biomeForChunk`): two coarse 2D noises map to six regions (FOREST/PLAINS/DESERT/ARCTIC/OCEAN/MOUNTAINS) so biomes appear as ~30-chunk patches.
- **Biome surface materials + altitude bias** (DESERT→SAND, ARCTIC→SNOW, OCEAN→SAND, MOUNTAINS→COBBLESTONE; OCEAN lowers / MOUNTAINS raises the density threshold).
- **Biome-aware vegetation:** per-biome tree density, trees span the full 16×16 chunk, trunk height `4 + nextInt(3)` with squashed-sphere canopy, CACTUS on desert, no trees on arctic/ocean.
- **Real village clusters** (`Gen::generateVillage`): 4-7 houses + WELL + farm + PEN per 16×16-chunk region, shared across seams.
- **Real mineshaft shape** (`Gen::generateMineshaft`): 3×3 vertical shaft + horizontal 3×3 corridors with WOOD supports and RAILS floor, matching vanilla `MinShaft.cpp`.

### Fixed

- **Tree height uniformity:** `for (h = 1; h <= 5; ++h)` → `4 + nextInt(3)`.
- **Tree sparsity:** tree loop was clipped to an 8×8 patch; now `lx, lz ∈ [2, 13]` with margin so canopies fit the chunk.
- **Village "one box house":** replaced single 5×5×4 box per region with the new multi-house cluster.
- **Mineshaft "bizarre shape":** previous version skipped the vertical shaft and used FENCE/PISTON_HEAD supports; now uses the vanilla shaft+corridor shape.

---
## [0.0.11] - 2026-10-10

### Added

- **Per-plane ore generation** (`Gen::generateOverworldOre` / `generateNetherOre` / `generateAetherOre` / `generateEndOre`): vanilla 1.7.x vein distribution (coal 27/12/y5-132, iron 10/5/y5-52, gold 4/5/y0-31, lapis 2/5/y0-31, diamond 2/5/y0-15, quartz 2/5/y16-71) with plane-specific host material (Overworld STONE, Nether NETHERRACK, Aether SANDSTONE y30-100 halved, End none by design).
- **`Gen::generateOre` host-material parameter** (`hostId`, default `STONE`): a vein only replaces blocks matching the plane's rock.

### Fixed

- **`Gen::generateOre` per-chunk seed:** seed was `seed ^ oreType` so every chunk showed identical ore layouts (visible tiling); now mixes `worldSeed ^ mix(cx) ^ mix(cz) ^ mix(oreType) ^ mix(hostId)`.
- **Vein-walk algorithm:** replaced random-offset blob scatter with vanilla's cumulative walk + per-vein randomized length so veins read as chains.
- **Overworld ore coverage:** replaced the 3-call (coal/iron/gold, non-vanilla counts) block with the vanilla-values `generateOverworldOre` call.
- **Nether/Aether/End had no ore pass at all:** added the ore wrapper calls to `netherChunk` / `aetherChunk` / `endChunk`.

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

- **Off-thread chunk generation (one worker per WorldStorage):** `WorldStorage` owns a generation worker + mutex-guarded pending queue; `requestChunk` submits (idempotent, lazy worker spawn), `getChunkBlocksIfReady`/`isChunkReady` are non-blocking read paths, destructor joins the worker before closing SQLite.
- **Async chunk streaming flush tick** (`NetworkManager::tickChunkStreaming`): re-emits `sendMapChunkBulk` per live PLAY session, throttled to 5 Hz, to catch chunks finished between movement crossings.

### Changed

- **Chunk emission at high render distance:** `sendMapChunkBulk` branches per chunk — login burst keeps the legacy sync `getChunkBlocks()` path; movement crossings use `isChunkReady()`/`getChunkBlocksIfReady()` and skip worker-pending chunks (they ship on the next crossing or flush tick).
- **Directional prefetch in `updateChunksAroundPlayer`:** submits a box extended `frontPad = renderDistance` ahead / `backPad = 1` behind along the travel axis so high-speed travel doesn't outrun generation; `session.prevChunkX/Z` (new, `INT_MIN` login sentinel) derive the direction.
- **SQLite `sqlite3_stmt*` thread safety:** per-instance prepared stmts are stateful; `m_getStmtMutex` guards reads, `m_saveStmtMutex` (recursive, so `saveBlockChange → generateChunk` nesting doesn't deadlock) guards writes; two mutexes let network reads overlap worker writes.

### Design notes

- Per-plane (not per-client) worker granularity and velocity-biased prefetch rationale: see wiki "Async chunk generation" / `phases.md`.

### Files

- `WorldStorage.{hpp,cpp}`: worker thread, async queue, `requestChunk`/`isChunkReady`/`getChunkBlocksIfReady`, stmt mutexes, dtor join.
- `PlayerSession.hpp`: new `prevChunkX`/`prevChunkZ` (velocity prefetch, `INT_MIN` login sentinel).
- `WorldManager.cpp`: login-vs-movement streaming branch + directional prefetch box.
- `NetworkManager.hpp`/`NetworkWorlds.cpp`: `tickChunkStreaming` flush; `Server.cpp`: 5 Hz throttle.

---
## [0.0.9] - 2026-10-10

### Changed

- **Map Chunk Bulk (0x26) streaming entry point:** `sendMapChunkBulk` emits one 0x26 packet per chunk (chunkCount=1, vanilla outer layout) instead of one monolithic body that blew the 21-bit wire cap at render distance >= 3; the monolithic build is kept as `sendMapChunkBulkMonolithic` for reference. `updateChunksAroundPlayer` calls the streaming path once for 1.8+ clients, iterating `sendFlatChunk` only for legacy (<47).

### Fixed

- **Chunk (0x21) air-chunk desync:** `sendFlatChunk` now skips chunks with `sectionBitmask == 0` — vanilla `readChunk` short-circuits on an empty mask and does not consume the length/DEFLATE we used to emit, so each air chunk left ~15 garbage bytes that desynced the stream.
- **Map Chunk Bulk (0x26) outer header:** `sendChunkBulkSingle` writes the vanilla one `hasSkyLight` byte + varint chunkCount instead of a spurious `compressLevel=0` outer byte + per-chunk skylight byte (the "1 extra byte" on packet 38); this also flips vanilla from DEFLATE to the 12,288-byte section width.

### Added

- **Double-chest detection:** `handleBlockPlacement` probes the four horizontal neighbours (IDs 54/130/146); `0x2D Open Window` advertises 54 slots for a pair vs 27 for a single; chests persist via `saveBlockChange`.
- **Network safety:** `sendPacket` rejects bodies over the 21-bit wire limit (2,097,151 bytes), logging and dropping oversized packets instead of corrupting the stream.

### Changed

- **Map Chunk Bulk (0x26):** `sendMapChunkBulk` reverted to the pre-RAM monolithic build (V0.0.5) — the JVM 1.8 client accepts at most one Map Chunk Bulk per load stage, so the streaming split's extra bodies were dropped (chunks rendered as air). Peaks ~10 MB above baseline for a 7×7 view; oversized bodies are refused+logged by `sendPacket`.
- **World constants (`WorldStorage.hpp`):** stale 25-section comments corrected to the real 16-section range (`MIN_WORLD_Y=0..MAX_WORLD_Y=255`); `static_assert` documents the wire-level ceiling.
- **`minecraft:mountains` preset:** height clamp tightened `[5,250]` → `[5,100]` to keep mountains inside sections 0..6 (halves per-chunk payload).

### Fixed

- **CorruptedFrameException: length wider than 21-bit (mountains preset):** the monolithic `0x26` build pushed ~9.6 MB (7×7 × 16 sections × ~196 KiB) overflowing the wire varint; `sendPacket` now refuses+logs+drops oversized bodies so the stream stays aligned, and the `[5,100]` clamp keeps sections inside 0..6.


## [0.0.6] - 2026-10-04

### Added

- **Player management:** player persistence; player data structure completed to the vanilla `player.dat` NBT layout.
- **Biomes & presets generation:** per-block test-world generation.

### Changed

- **Commands:** moved into dedicated headers to keep files readable.
- **Minecraft Protocol:** interim multi-protocol mockup (superseded by per-protocol handling).
- **Console:** improved behaviour and command history.

### Fixed

- **Minecraft Protocol 1.8 (v47):** crash on interactive block operations (chest / anvil).


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

## [0.0.4] - 2026-09-16

### Added
- **Biomes & presets generation**: Support for `minecraft:flat`, `minecraft:stone`, `minecraft:water`, and `overworld` presets.
- **Management**: Added terminal command handling support (mockup).

### Changed
- Refactored the chunk storage system to optimize RAM usage.
### Fixed

- **Network serialization (Protocol 47 / MC 1.8)**: Better network stability.
---

## [0.0.3] - 2026-09-08


### Added
- **SQLite storage engine (WAL Mode)**: LZ4 compression/decompression implementation for chunk data.
- Initial support for Big-Endian / Little-Endian serialization per network protocol specifications.
- Chat and in game command support (basic)
---

## [0.0.2] - 2026-09-02

### Added
- **SQLite storage engine (WAL Mode)**: Persistence implementation.
- Multi-threaded handling of network compression and disk I/O operations.

### Fixed
- Server stability during rapid player disconnects and reconnects.

---

## [0.0.1] - 2026-09-02

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