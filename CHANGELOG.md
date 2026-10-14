# Changelog

All notable changes made to the project **LightweightMC** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and this project respects [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---
## [0.0.15] - 2026-10-13

### Added

- **Per-plane inventory policy (world_registry extension + `player_inventories`
  SQLite table).** Vanilla Minecraft carries one shared inventory across
  dimension planes; the server keeps that behaviour for the standard chain
  (Overworld #0 / Nether #-1 / End #1 / Aether #100 by default / mining
  worlds) but 0.0.15 lets special planes break it in two documented ways:
  - **SEPARATE (1)** — per-plane blob keyed on `(uuid, world_id)` in the
    new `player_inventories` SQLite table, intended for flat / creative
    sandbox planes (flat world, overworld height, no structures/ores/
    villages/trees) whose loot never leaks back to the mining chain and
    vice-versa. Almost always paired with `force_gamemode = 1 CREATIVE`
    so the plane pins creative on entry even when the player was on
    survival elsewhere.
  - **RESTRICTED (2)** — same storage as SEPARATE but treated as one-way:
    arriving on the plane from any other plane never inherits the source's
    inventory (loads empty / that plane's own blob if the player was
    training there earlier), and leaving never carries items back.
    Intended for PVP / PVE training maps.
  - **SYNC (0)** — the default (and pre-0.0.15 behaviour for every plane):
    one blob persisted on the flat `players.inventory_blob` column so
    items transfer across planes transparently (standard Minecraft).

- **`world_registry` extension columns (`inventory_policy INTEGER DEFAULT 0`
  and `force_gamemode INTEGER DEFAULT -1`).** `force_gamemode` is
  orthogonal and lets the plane pin a game mode on entry (`-1 INHERIT`
  keeps whatever mode the player carried in; `0 SURVIVAL`, `1 CREATIVE`,
  `2 ADVENTURE`, `3 SPECTATOR` override `session.gamemode` on arrival).
  `Database::initWorldRegistry` adds them to the CREATE TABLE and also
  runs ALTER TABLE migrations with a pragma_table_info probe so existing
  0.0.14 server_data.db rows pick them up transparently. Encoding lives
  in `include/storage/Database.hpp` as `Storage::kInventoryPolicy*` and
  `Storage::kForceGameMode*` constexpr ints so callers write
  `Storage::kInventoryPolicySeparate` instead of a magic `1`.

- **New `Storage::Database` API for the plane-scoped inventory chain:**
  `setWorldInventoryPolicy` / `getWorldInventoryPolicy`,
  `setWorldForceGameMode` / `getWorldForceGameMode`,
  `initPlayerInventoriesTable` (CREATE TABLE with PRIMARY KEY `(uuid,
  world_id)`), `savePlayerInventory(uuid, worldId, blob)`,
  `loadPlayerInventory(uuid, worldId, blobOut, hasRowOut)`, and
  `loadSyncInventoryBlob(uuid, blobOut)` (reads ONLY the flat
  `players.inventory_blob` column so SEPARATE / RESTRICTED save paths can
  preserve the shared SYNC slot rather than overwrite it with a plane
  blob).

- **`WorldManager` policy maps + hot-path accessors**
  (`m_inventoryPolicy` and `m_forceGameModeOnEntry` keyed by planeId with
  `setInventoryPolicy` / `getInventoryPolicy` and
  `setForceGameModeOnEntry` / `getForceGameModeOnEntry`). Both default to
  the standard-Minecraft values (`SYNC` / `INHERIT`) when a plane is not
  in the cache so unregistered custom planes and legacy DBs keep working.
  `NetworkManager::start` seeds the Aether entries at boot by reading
  `[world] aether-inventory-policy` (`standard`/`separate`/`restricted`)
  and `[world] aether-force-gamemode` (`none`/`survival`/`creative`/
  `adventure`/`spectator`) from `server.properties`, persisting them to
  SQLite via `Database::setWorldInventoryPolicy` / `setWorldForceGameMode`
  and mirroring them onto `WorldManager` for the hot path. Vanilla
  Overworld / Nether / End stay on their CREATE defaults.

- **Plane-scoped inventory transfer inside
  `NetworkManager::setPlayerWorldAt`** runs BEFORE any wire packet
  (0x07 Respawn / drop-point chunk / 0x08 Position / rest-of-box chunks)
  so the client's inventory view and game mode already match what the
  server will persist on the next save flush. Departure blob is written
  to the departure's slot (SEPARATE / RESTRICTED go to
  `player_inventories(uuid, oldWorldId)`; SYNC blobs stay on the flat
  column and are flushed by the periodic `savePlayerState`), the arrival
  blob is read from the arrival's slot (SYNC via
  `Database::loadSyncInventoryBlob`; SEPARATE / RESTRICTED via
  `Database::loadPlayerInventory(uuid, newWorldId, blob, hasRow)`), and
  `session.inventory` is cleared then re-hydrated from the arrival blob.
  `session.gamemode` is also pinned from
  `WorldManager::getForceGameModeOnEntry(newWorldId)` before the 0x07
  Respawn's gamemode byte is written, so the wire byte and the session's
  next save mirror agree (creative sandbox planes pin CREATIVE even
  when the player was on SURVIVAL elsewhere).

- **`Database::loadPlayerData` plane-scoped inventory restore.** After
  reading `world_id` (column 34) from the flat players row, the login
  path consults the plane's `inventory_policy` on `world_registry` and,
  when SEPARATE / RESTRICTED, replaces `PlayerData::inventoryData` with
  the plane-scoped blob on `player_inventories(uuid, world_id)` so
  `PlayerManager`'s login handler restores the plane's own inventory
  instead of the shared SYNC chain. Missing plane row (first visit)
  yields an empty blob, matching the intended "fresh creative sandbox /
  fresh training map" state and preventing items from a SYNC chain from
  leaking into a RESTRICTED plane on arrival.

- **`PlayerManager::savePlayerState` policy-aware save.** Consults
  `Database::getWorldInventoryPolicy(session.worldId)` for the plane the
  session is currently on; SYNC planes keep using the flat
  `players.inventory_blob` column, and SEPARATE / RESTRICTED planes route
  the current session blob to `savePlayerInventory(uuid, worldId, blob)`
  and re-read the existing flat SYNC blob into `data.inventoryData`
  (via `loadSyncInventoryBlob`) so the INSERT-OR-REPLACE that
  `Database::savePlayerData` runs preserves the shared SYNC chain rather
  than overwriting it with a plane-scoped blob.

- **Wiki: Multiworld-and-Dimensions "Plane inventory policy" section**
  documenting the SYNC / SEPARATE / RESTRICTED table, the `force_gamemode`
  encoding, the `[world] aether-inventory-policy` / `[world] aether-force-
  gamemode` config keys, and the three server-side entry points that
  consult the policy (`setPlayerWorldAt`, `loadPlayerData`,
  `savePlayerState`).

### Notes

- **Existing 0.0.14 server_data.db continues to work.** The two new
  `world_registry` columns come from CREATE TABLE (fresh DBs) or ALTER
  TABLE migrations (legacy DBs); `player_inventories` is a brand-new
  CREATE TABLE that pre-0.0.14 DBs get empty. Vanilla planes stay on
  their SYNC / INHERIT defaults so the 4-packet plane-switch sequence
  added in 0.0.14 (0x07 → 0x26 → 0x08 → rest-chunks) is unchanged on
  the wire.
- **First fix for 0.0.15.** Later 0.0.15 work will extend `/world
  create` with `--inventory <standard|separate|restricted>` and
  `--force-gamemode <mode>` flags and add a `GenStyle::FLAT` style so
  a "flat world with overworld height, no structures / ores / villages /
  trees, creative mode" plane can be described end-to-end in one command.
  The DB-level setters and the in-memory `WorldManager` mirrors are
  already in place, so the CLI follow-up only needs to route the flags
  through them.

---
## [0.0.14] - 2026-10-13

### Added

- **`Storage::PlayerData::worldId`** field (mirrors `PlayerSession::worldId`)
  persisted in the `players` SQLite table as `world_id INTEGER DEFAULT 0`.
  Round-trips through both halves of the login flow:
  `Database::loadPlayerData` reads column 34 (world_id) and the login handler in
  `PlayerManager.cpp` copies it onto `session.worldId` before the first
  `getWorldStorageFor(session.worldId)` call in `sendPlayPackets`.
  `Database::savePlayerData` and `PlayerSession::applyTo` also write the field
  back on every save, so a live `/gotor` switch persists across relogins even
  without the `Database::setPlayerWorldIdByName` path firing.

- **Per-plane player coordinates (`Storage::Database::player_positions` SQLite
  table + `Storage::PlayerData::hasPositionData`).** New SQLite table
  keyed on `(uuid, world_id)` records `x`, `y`, `z`, `yaw`, `pitch` and an
  `updated_at` timestamp for every plane a player has occupied, so a
  player who used `/gotor` or (later) a portal no longer inherits their
  source-plane coordinates on the destination plane. Three write paths
  feed the table: (1) the periodic `savePlayerData` mirror — guarded by
  the new `PlayerData::hasPositionData` bool which defaults to `false`
  and is flipped to `true` inside `PlayerSession::applyTo`, the *only*
  entry point reached by live per-plane saves, so partial-write callers
  that construct a fresh `PlayerData` with placeholder coords
  (`setPlayerOpLevel`, `setPlayerGameMode`) never clobber the real
  per-plane values; (2) the new `NetworkManager::setPlayerWorldAt(player
  Name, newWorldId, x, y, z, hasExplicitPosition)` which saves the
  departure-plane coords and either loads the previously-stored arrival
  coords or falls back to the plane's default spawn on first visit
  (`hasExplicitPosition=false`), or else writes the caller-provided
  `(x,y,z)` verbatim as the arrival point (`hasExplicitPosition=true`,
  which is the primitive a future portal system will call to pin the
  exit on the destination plane); and (3) the login path — `Database::
  loadPlayerData` now calls `Database::loadPlayerPosition(uuid, worldId,
  ...)` for the plane the flat `world_id` column says the player is on
  and, if an entry exists, overrides the flat `lastX/lastY/lastZ/yaw/
  pitch` fields with the per-plane row. `setPlayerWorldId` becomes a
  thin wrapper delegating to `setPlayerWorldAt(..., 0.0, 0.0, 0.0, false)`
  so the existing `/gotor` console path inherits the new behaviour
  without any call-site change. `setPlayerWorldAt` also resolves the
  uuid via `PlayerManager::getInstance().getPlayerUuid(playerName)` as a
  fallback when `session.uuid` is empty (mid-login players), keeping
  both coordinate-save paths keyed on the same identifier.

- **`WorldManager::setSkyDimension` + `WorldManager::skyDimensionFor` sky-render
  trick for non-vanilla planes.** Vanilla 1.8 clients only know three sky
  renders keyed by the `dimension` field of the S2C `JoinGame` (0x01) login
  byte and S2C `Respawn` (0x07) plane-switch int32 — `0` (Overworld blue),
  `1` (End / Star Keep purple), `-1` (Nether dark grey). Any server plane
  with a non-vanilla id (Aether at 100 by default, `/world create` custom
  ids 101+, and the planes landing in 0.0.15) has no sky of its own;
  without the trick the client kept the previous plane's sky across the
  switch and defaulted to Overworld on reconnect. `WorldManager::skyDimensionFor(planeId)`
  returns the planeId itself for `{0, 1, -1}` and otherwise returns whatever
  `WorldManager::setSkyDimension(planeId, hint)` registered, defaulting to
  0 (Overworld sky). `NetworkManager::setPlayerWorldAt` now writes that
  hint into the 0x07 `dimension` int32 (instead of the raw newWorldId) and
  `PlayerManager::sendPlayPackets` writes `static_cast<uint8_t>(static_cast<int8_t>(...))`
  into the 0x01 `dimension` byte at login (so Nether's -1 becomes the expected
  `0xFF` on the wire); server-side state (`session.worldId`, `m_worlds`
  keys, `player_positions` rows) keeps the true plane id so storage lookups,
  chunk streaming and persistence paths are unaffected. Callers added in 0.0.15
  (`/world create --sky <overworld|nether|end>` and the new dimension
  registry) will call `setSkyDimension` to opt each new plane into the sky it
  wants; `setSkyDimension` silently ignores hints outside `{0, 1, -1}`.
- **Wiki: Multiworld-and-Dimensions "Sky render trick for non-standard planes"
  section** documenting the 0/1/-1 sky table, the two entry points that
  consume `skyDimensionFor`, and the persistence caveat (server-side keys
  stay on the true plane id, only the wire value is remapped).


### Fixed

- **Relogin after `/gotor <plane>` returned to Overworld.**
  `Database::loadPlayerData`'s SELECT list previously stopped at `op_level`, and
  the login handler in `src/managers/PlayerManager.cpp` never wrote
  `session.worldId` from the loaded `PlayerData` — so a player who used `/gotor
  nether` or `/world create` and then reconnected was put back in plane 0
  (Overworld) even though the SQLite `world_id` column said plane −1. The SELECT
  now includes `world_id` and the login handler restores
  `currentSession.worldId = playerData.worldId` before the first chunk-streaming
  call. Verified by pre-seeding `world_id = -1` in `server_data.db`, booting the
  server, logging in a fresh client, and observing the log line
  `Restored player state for UUID [...] (...) ... on plane worldId=-1.`
- **`aether-special-dimension` (bool, written but never read) → `aether-plane-id`
  (int, default 100, actually read at boot).** The config key emitted by
  `ConfigManager::forEachCanonicalDefault` in `include/core/ConfigManager.hpp`
  was documented as "written only, never read"; plane id 100 was hard-coded in
  `NetworkSocket.cpp::NetworkManager::start` (in both `kStandardPlanes` and
  `planeSpecs`), in `NetworkWorlds.cpp::describeWorld`'s `switch (outId)`, and in
  the `NetworkWorlds.cpp` file-header comment. The key is now an int
  (`[world] aether-plane-id = 100` by default), read once at boot, threaded into
  both `Database::setWorldById` (registry row) and the `m_worlds` map key
  (`planeSpecs[3] = {aetherPlaneId, "world-aether.db", AETHER, aetherActive}`),
  with a boot-time validation rejecting collisions with the vanilla pre-1.12
  slots (0 / −1 / 1). `describeWorld`'s per-plane SQLite file lookup was switched
  to match by **name** rather than by numeric id so re-homing the reserved slot
  keeps the correct `world-aether.db` filename mapping. `Database::registerWorld`'s
  `max(world_registry.id) + 1` allocator already reserved the configured slot
  automatically, so custom `/world create` planes can never collide with the
  configurable Aether id.

- **Vanilla clients never closed the "loading world" screen on `/gotor` or a
  future portal plane-switch.** Pre-fix `NetworkManager::setPlayerWorldAt`
  pushed a bare S2C Position (0x08) after mutating `session.x/y/z/worldId`,
  but vanilla 1.8 requires a specific 4-packet stream to close its loading
  screen: (1) `0x07` Respawn / Init Spawn Point with `dimension:int32,
  difficulty:u8=2, gamemode:u8, levelType:"default"` — declares the new
  plane to the client, clears its cached Overworld chunks, and starts the
  loading screen for the destination; (2) a single MapChunkBulk (0x26) body
  carrying **only the drop-point chunk** so the client can decide which
  blocks to stand the player on; (3) `0x08` Player Position And Look with
  `x/y/z` doubles + `yaw/pitch` floats + `flags:u8=0x00` (absolute) and
  **no trailing teleport-ID VarInt** (that field belongs to 1.9+ only,
  adding it shifted every subsequent packet body and made the client
  reject the position); (4) the surrounding chunk stream within render
  distance. Sending only `0x08` (pre-fix) meant the client saw an updated
  position on a plane whose loading screen was still open on the previous
  plane's cache — the reported "no-op on screen" symptom. `setPlayerWorldAt`
  now emits all four in order: `0x07` via a freshly-built respawn buffer,
  drop-point chunk via `sendMapChunkBulk(fd, session, 0, packetSender)` for
  1.8+ (`sendFlatChunk` single chunk for pre-1.8 clients, whose equivalent
  is 0x21), `0x08` with the full body via `Network::Packet::writeDouble`/
  `writeFloat`, and finally `updateChunksAroundPlayer(fd, session, 5, packetSender)`
  to prefetch+emit the remaining chunks; `session.loadedChunks` is
  cleared first so the drop-point chunk inserted by the rd=0 pass is
  skipped by the rd=5 pass. `session.prevChunkX/Z` are reset to the
  login-burst sentinel (`std::numeric_limits<int32_t>::min()`) before the
  stream so the next `updateChunksAroundPlayer` pads the request box
  symmetrically around the player rather than 2x ahead in a stale
  movement direction from the previous plane.
- **`NetworkEvents.cpp` inline scoreboard refresh loop in `pollEvents` replaced
  with `ScoreboardManager::tick(m_clients, packetSender)`.** The inline
  `for (const auto &[fd, ses] : m_clients)` loop that dispatched `sendScoreboard`
  + `sendTablistHeaderFooter` per PLAY client without first snapshotting the
  fd list was UB once any client disconnected mid-refresh: those two sends go
  through `NetworkManager::sendPacket` which can cascade
  `flushSendBuffer -> handleDisconnect -> m_clients.erase(fd)` and invalidate
  the range-for iterator (and any reference to `ses`) on a later pass. The
  crash surfaced during `bot_test` under ASan as a heap-use-after-free inside
  the erased `std::unordered_map<int, PlayerSession>` node read at line 313
  of NetworkEvents.cpp. `ScoreboardManager::tick` already snapshots the live
  (fd -> PLAY) pairs into a local `std::vector<int>` before dispatching and
  re-looks each fd up on its own pass, so delegating to it fixes the use-after-
  free without any other behaviour change.
- **`hexPreview(body, bodyOff + packetEnd)` at `NetworkEvents.cpp:166` →
  `hexPreview(body, packetEnd)`.** `body` points into the receive buffer at
  offset `headerOff` (so it has exactly `packetEnd = packetLen` valid bytes)
  but the call site also added `bodyOff`, the varint prefix length that had
  already been consumed inside the packet-id decode — a two-byte over-read on
  every debug hex log line for a small-varint packet. ASan reported this as
  heap-buffer-overflow at `hexPreview` (line 52) reading `1 byte` past a
  16-byte `std::vector<uint8_t>` allocation for the standard handshake
  frame; harmless under the normal build (adjacent heap reads stay in-mapped)
  but it landed on a page boundary occasionally and now SIGSEGVs reproducibly
  after the coord changes shift heap layout, breaking `run_tests.sh`. Only the
  debug-log path was affected; the packet body itself was parsed correctly.

  after `/gotor`.** Before 0.0.14, `Database::loadPlayerData` returned a
  single flat `(lastX, lastY, lastZ, yaw, pitch)` tuple for the player
  regardless of which plane they were on, and `NetworkManager::setPlayer
  WorldId` only updated the flat `world_id` column. A player who stood at
  say `(120, 45, -80)` in the Overworld and used `/gotor nether` was
  respawned in Nether at `(120, 45, -80)` — coordinates that made sense
  in the Overworld but not necessarily in the destination plane, and any
  subsequent Overworld relogin would restore those Nether-flavoured
  coordinates. The new `player_positions` SQLite table records per-plane
  coordinates keyed by `(uuid, world_id)`; `setPlayerWorldAt` saves the
  departure-plane row before switching, loads any arrival-plane row (or
  the plane's default spawn on first visit) so `/gotor` drops the player
  where they previously stood on that plane instead of teleporting them
  to the source-plane coordinates, and the login path in `Database::
  loadPlayerData` re-reads the per-plane row for the plane the flat
  `world_id` column indicates. `setPlayerWorldId` becomes a wrapper
  around `setPlayerWorldAt(..., hasExplicitPosition=false)` so the
  existing `/gotor` console path picks this behaviour up with no
  change at the call site.

### Documentation

- **`PlayerSession.hpp` world/plane layout comment (line ~103) rewritten** to
  document the pre-1.12 vanilla Java Edition layout the code actually uses
  (Overworld 0 / Nether −1 / End 1 / Aether `<aether-plane-id>` default 100 /
  custom 101+) instead of the aspirational "0/1/2 with 3..99 reserved" block
  that has never matched the code since 0.0.6.
- **`WorldCommands.hpp` `/world create` subcommand summary** fixed to read
  "allocate a new plane (id >= 101)" (was "id >= 100", matching
  `kFirstCustomWorldId = 101` in `Database.cpp` and the Aether reserved-slot
  convention).
- Wiki [Multiworld & Dimensions](Multiworld-and-Dimensions) updated:
  config-key table, plane table, and intro paragraph now describe the
  configurable `[world] aether-plane-id` key; the four "Doc/code mismatches
  worth flagging" callouts are replaced with a resolved-in-0.0.14 note while
  keeping the still-open `pregenerateWorld` callout.

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