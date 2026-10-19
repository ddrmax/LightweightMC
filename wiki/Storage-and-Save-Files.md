# Storage & Save Files

There is **no "world folder" like vanilla Minecraft**. All persistent state lives in
SQLite 3 database files sitting directly in the working directory next to `server.properties`.
Two kinds of DB are used:

- **`server_data.db`** — one file, opened by `NetworkManager::m_database` (a
  `Storage::Database` instance). Holds the player rows, containers, game rules, and
  the multiworld registry.
- **One file per plane** — `world.db`, `world-nether.db`, `world-end.db`,
  `world-aether.db`, and one `world-<safeName>.db` per `/world create` plane. Each is
  opened by its own `Storage::WorldStorage` instance; terrain chunks are stored as
  LZ4-compressed blobs inside those files.

Chunk blobs are compressed with LZ4 (`compressBuffer` in
`src/storage/WorldStorage.cpp` wraps `LZ4_compress_default`/`LZ4_decompress_safe`;
`include/storage/RegionFile.hpp` exposes the same primitives for the offline
chunk verifier in `tools/verify_chunk_packet.cpp`).

## Files on disk

Every DB is a normal SQLite 3 file (the sqlite3 CLI can open it). WAL mode is on for
both database classes — expect `*.db-wal` and `*.db-shm` sidecars beside each
`.db` while the server runs, folded back into the DB on the next open.

| File | Opened by | Created by | Purpose |
| --- | --- | --- | --- |
| `server_data.db` | `NetworkManager::m_database` in `NetworkManager::start` | `Database::init` | Player rows, block-entity (container) rows, gamerules, `world_registry`. |
| `world.db` | `WorldStorage` for plane **id 0** (Overworld) | `WorldStorage::init` | `chunks` + `generated_chunks` tables for the primary plane. |
| `world-nether.db` | `WorldStorage` for plane **id -1** (Nether) | same | Underworld plane. |
| `world-end.db` | `WorldStorage` for plane **id 1** (End / Abyss) | same | Deepest plane. |
| `world-aether.db` | `WorldStorage` for plane **id 100** (Aether special dimension) | same | Minecraft Legends "Aether" plane; gated by `server.properties` `[world] aether-active`. |
| `world-<safeName>.db` | `WorldStorage` for a custom plane (id ≥ 101) | `NetworkManager::createWorld` | Created on `/world create <name> <style>`. `<safeName>` is the world name lower-cased with every char outside `[a-z0-9_-]` replaced by `_` (see `src/network/NetworkWorlds.cpp::getWorldDbPath` and `createWorld`). |

The pre-1.12 vanilla plane IDs (Overworld 0, Nether -1, End 1) and the reserved
Aether special-dimension id **100** are hard-wired in
`src/network/NetworkSocket.cpp::NetworkManager::start` (see the `kStandardPlanes`
and `planeSpecs` arrays there). Custom `/world create` planes allocate ids
starting at **101** (see `Database::registerWorld`, `kFirstCustomWorldId = 101`).
The full mapping is documented on [Multiworld & Dimensions](Multiworld-and-Dimensions).

## SQLite schema

### `server_data.db` — created in `Database::init`

Journal is set to WAL with `synchronous = NORMAL` (`sqlite3_exec(m_db, "PRAGMA
journal_mode = WAL;", …)` at the top of `Database::init`). The tables
below are created with `CREATE TABLE IF NOT EXISTS`, so re-running the
server on an existing DB does not clobber it.

```sql
CREATE TABLE IF NOT EXISTS books (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_uuid TEXT UNIQUE,
    title TEXT, author TEXT,
    pages_json TEXT
);
CREATE TABLE IF NOT EXISTS block_entities (
    chunk_x INTEGER, chunk_z INTEGER,
    rel_x INTEGER, y INTEGER, rel_z INTEGER,
    type INTEGER, data_blob BLOB,
    PRIMARY KEY (chunk_x, chunk_z, rel_x, y, rel_z)
);
CREATE TABLE IF NOT EXISTS players (
    uuid TEXT PRIMARY KEY,
    username TEXT,
    spawn_x DOUBLE, spawn_y DOUBLE, spawn_z DOUBLE, spawn_yaw FLOAT, spawn_pitch FLOAT,
    last_x  DOUBLE, last_y  DOUBLE, last_z  DOUBLE, last_yaw FLOAT, last_pitch FLOAT,
    inventory_blob BLOB,
    last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    world_id INTEGER DEFAULT 0
);
CREATE TABLE IF NOT EXISTS gamerules (
    name TEXT PRIMARY KEY,
    type INTEGER NOT NULL DEFAULT 0,
    bool_value INTEGER NOT NULL DEFAULT 0,
    int_value  INTEGER NOT NULL DEFAULT 0
);
```

`world_registry` is created separately by `Database::initWorldRegistry` (called
from `NetworkManager::start` right after `Database::init`):

```sql
CREATE TABLE IF NOT EXISTS world_registry (
    id   INTEGER UNIQUE,
    name TEXT UNIQUE COLLATE NOCASE,
    style TEXT
);
```

### Per-plane `world-*.db` — created in `WorldStorage::init`

Same WAL/NORMAL pragmas.

```sql
CREATE TABLE IF NOT EXISTS chunks (
    chunk_x INT, chunk_z INT,
    data_blob BLOB,
    PRIMARY KEY (chunk_x, chunk_z)
);
CREATE TABLE IF NOT EXISTS generated_chunks (
    chunk_x INT, chunk_z INT,
    PRIMARY KEY (chunk_x, chunk_z)
);
```

`generated_chunks` is the "this chunk was generated already" flag consulted by
`WorldStorage::isChunkGenerated` (which uses its own local `sqlite3_stmt` —
safe to call concurrently with a generation write on `m_saveChunkStmt`). It
exists so a chunk that is legitimately empty (all air, no blobs to store) is
still recognised as generated; `chunks` alone can't express that.

## The chunk blob

A row in `chunks` carries one LZ4-compressed blob keyed by `(chunk_x, chunk_z)`.
The uncompressed payload is a `NUM_SECTIONS × UNCOMPRESSED_SECTION_SIZE` array
of big-endian `uint16_t` block entries laid out as
`((absY - MIN_WORLD_Y) * 256) + (relZ * 16) + relX`. Values are the
`(blockId << 4) | meta` encoding (`Blocks::combined` in
`src/storage/WorldStorage.cpp`); 0 means air, which is what makes LZ4 useful
for terrain.

The exact constants live in `include/storage/WorldStorage.hpp`:

```cpp
constexpr int MIN_WORLD_Y = 0;
constexpr int MAX_WORLD_Y = 255;
constexpr int NUM_SECTIONS = (MAX_WORLD_Y - MIN_WORLD_Y) / 16 + 1;   // 16
static_assert(NUM_SECTIONS <= 16,
              "Wire sectionBitmask is 16-bit: NUM_SECTIONS must stay <= 16");
constexpr size_t UNCOMPRESSED_SECTION_SIZE = 16 * 16 * 16 * sizeof(uint16_t); // 8 192
constexpr size_t UNCOMPRESSED_CHUNK_SIZE   = NUM_SECTIONS * UNCOMPRESSED_SECTION_SIZE; // 131 072
```

`NUM_SECTIONS` is bounded to 16 because the MC 1.8 Map Chunk Bulk / Chunk wire
formats carry a **16-bit section bitmask** — see
[Protocol & Clients](Protocol-and-Clients). A `Y > 255` world would need to be
split across separate packets; the `s < 16` guards in `sendFlatChunk` and
`sendMapChunkBulk` (`src/managers/WorldManager.cpp`) rely on that ceiling.

**Two blob layouts are read today.** `WorldStorage::readChunkBlob` first tries
`UNCOMPRESSED_SECTION_SIZE` (8 192 bytes = one section, the *legacy* layout),
and if that fails tries `UNCOMPRESSED_CHUNK_SIZE` (the *modern* full-chunk
layout). On a legacy hit the content is copied to section 0 (absY 0..15) of an
all-zero full-chunk buffer; on a modern hit the raw decompressed buffer is
moved into the output directly. So old `world.db` files are readable without a
rewrite. The legacy path exists for pre-refactor saves; do not rely on it for
new writes — `generateChunk` and `saveBlockChange` always emit the modern
layout.

> **Doc/code mismatch.** Several comments in `src/storage/WorldStorage.cpp`
> (e.g. near `readChunkBlob` line 665, near `getChunkBlocks` line 916, and in
> the "Off-thread chunk generation worker" comment) describe the modern blob
> as a **"24-section chunk (196,608 bytes)"**. The header constant is
> 16 sections / 131 072 bytes and the wire sectionBitmask is 16-bit; the
> `static_assert` in the header enforces the 16-section ceiling. Read the
> **header** as authoritative — the comments are stale.



## Lifecycle: reading, generating, editing, prefetching

A `WorldStorage` instance owns **two prepared `sqlite3_stmt` handles** for its
plane file:

```cpp
"SELECT data_blob FROM chunks WHERE chunk_x = ? AND chunk_z = ?;"              // m_getChunkStmt
"INSERT OR REPLACE INTO chunks (chunk_x, chunk_z, data_blob) VALUES (?, ?, ?);" // m_saveChunkStmt
```

Prepared statements are per-instance and not thread-safe on their own, so
`WorldStorage` keeps two locks around them:

- `m_getStmtMutex` (a `std::mutex`) protects `m_getChunkStmt`. Every `readChunkBlob`
  call — whether from the network send path, from `getChunkBlocksIfReady`, or from
  EntityManager probing standing surfaces — holds it.
- `m_saveStmtMutex` (a `std::recursive_mutex`) protects `m_saveChunkStmt`. It is
  recursive because `saveBlockChange` internally calls `generateChunk` for the not-yet-generated
  case; both need the same lock and the recursion is deliberate, not accidental.

Two separate mutexes (rather than one global) let the **network read path overlap**
the generation worker's writes; a chunk that finishes generation mid-tick becomes
visible on the next tick's `isChunkReady` check without tearing.

`isChunkGenerated` opens its own `sqlite3_stmt` locally, so it does **not**
contend on either of those mutexes — this is what makes the read/write overlap
in `updateChunksAroundPlayer` safe.

### Login burst (`getChunkBlocks`, synchronous)

Used by `WorldManager::sendFlatChunk` and by `sendPlayPackets` on first login. For
each chunk in the render box:

1. If `!isChunkGenerated(cx, cz)`, call `generateChunk` synchronously (this is the
   ~150 ms login hitch at rd=5 the server operators tolerate; see the
   `bench` log `Compressed Chunk (x, z) generated in: N us`).
2. `readChunkBlob` decompresses and returns a full modern-chunk buffer.
3. Iterate every absY, filter out 0-valued block entries, and build the
   `unordered_map<BlockCoord, uint16_t, BlockCoordHash>` the wire layer consumes.

### Movement crossings (`getChunkBlocksIfReady`, asynchronous)

Used by the 1.8+ `sendMapChunkBulk` streaming path (`WorldManager.cpp`, gated on
`session.clientProtocol >= 47`). For each chunk in the render box:

1. If `isChunkReady(cx, cz)` (a thin wrapper around `isChunkGenerated`), call
   `getChunkBlocksIfReady`. This is the read-only twin of `getChunkBlocks`:
   it **never calls `generateChunk`**, so a chunk missing on disk yields an
   empty map and the caller skips it this tick.
2. Else call `worldStorage.requestChunk(cx, cz)` — it pushes the `(cx, cz)` job
   to the plane's own async worker and the caller marks the chunk as *not*
   loaded. The next tick (or the crossing after that) ships it once the worker
   has written the blob.

`requestChunk` uses a 64-bit key `(uint64(chunkX) << 32) | uint64(chunkZ)`
to dedupe against `m_genQueued` and pushes onto `m_genQueue`. On the first
push it spawns `std::thread([this]{ generationWorkerLoop(); })` and flips
`m_genWorkerStarted`. **The worker is per-plane, not per-client.** Rationale
in the code (WorldStorage.cpp lines 758–785): per-client threads would spawn
~50 threads on a 50-player server fighting for the same `sqlite3_stmt*` handle;
one global thread serialises cross-plane generation and causes Nether to
stall behind Overworld. One per `WorldStorage` (≈ 3–4 threads across the four
standard planes) gives each plane independent progress.

`generationWorkerLoop` waits on `m_genCv`, pops a job, calls `generateChunk`
(which locks `m_saveStmtMutex` internally), then removes the key from `m_genQueued`
so a later `requestChunk` for the same chunk falls through to the fast
`isChunkGenerated()` reject rather than the queue-dedup set.

The **prefetch box** itself is a per-client decision living in
`updateChunksAroundPlayer` (`src/managers/WorldManager.cpp` lines ~820–855,
1.8+ clients only). It sees the player's velocity and biases the
"generate-ahead" box in the direction of motion: `frontPad = renderDistance`
in the direction of motion, `backPad = 1` behind — the "2× render distance
ahead" the client was asking for. High-speed travel (boat / flight /
spectate) does not outrun generation.

### Editing (`saveBlockChange`)

Called from the player/entity edit path (block placement, mob carving, etc.):

1. Lock `m_saveStmtMutex`.
2. If `!isChunkGenerated(cx, cz)`, generate synchronously — this makes the
   very first edit to a fresh chunk a single synchronous hitch.
3. `readChunkBlob` for the full modern buffer.
4. Modify one index `((absY - MIN_WORLD_Y) * 256) + (relZ * 16) + relX`,
   guarded to `MIN_WORLD_Y ≤ absY ≤ MAX_WORLD_Y` and `relX, relZ ∈ [0, 16)`.
5. Re-LZ4-compress and `INSERT OR REPLACE` through `m_saveChunkStmt`.

The entire edit path is synchronous on the calling thread. Editing chunks is a
much rarer event than streaming them, so it does not participate in the
async worker's queue.

### Server-startup pre-generation

**Currently disabled.** The loop `for (auto &kv : m_worlds) kv.second->pregenerateWorld(20);`
is commented out in `NetworkManager::start` (`src/network/NetworkSocket.cpp`, right
after the "Multiworld ready" log line). Likewise, `createWorld`'s
`storage->pregenerateWorld(12);` (`src/network/NetworkWorlds.cpp`) is commented
out. All chunk generation today happens on-demand: login burst (sync), movement
crossings (async worker), and edit paths. `run_tests.sh`'s 45 s cold-start sleep
(see [Testing](Testing)) covers the login burst, not a boot-time pregen.

If you re-enable boot-time pregen, call `pregenerateWorld(radius)` after `init()`
and after `setWorldSeed()`/`setGenerationStyle()`; it walks `(-radius, +radius)²`
in row-major order and skips chunks already present in `generated_chunks`.



## Player save state (`players` table)

`Storage::Database::savePlayerData` / `loadPlayerData` (src/storage/Database.cpp)
write and read a single row keyed by `uuid` (the login UUID from the protocol
layer, not the username). The columns mirror the vanilla `player.dat` fields:

| Column group | Fields |
| --- | --- |
| Identity | `uuid` (PK), `username`, `world_id` |
| Spawn / last position | `spawn_{x,y,z,yaw,pitch}`, `last_{x,y,z,yaw,pitch}` |
| `player.dat`-mirrored survival state | `health`, `food_level`, `food_saturation`, `food_exhaustion`, `gamemode`, `has_spawn`, `bed_{x,y,z}`, `has_bed_spawn`, `fall_distance`, `air`, `on_fire`, `fire`, `sleeping`, `sleep_timer`, `flying`, `sprinting` |
| Progression / admin | `xp_level`, `xp_progress`, `xp_total`, `death_count`, `op_level` |
| Timestamp | `last_seen` |
| Inventory | `inventory_blob` (opaque BLOB, see below) |

New DBs get the survival columns via the second `CREATE TABLE` path; **existing
DBs get them via an idempotent `ALTER TABLE players ADD COLUMN …` migration loop**
that runs after `init()` (see `Database::init`, the `newColumns[]` array). The
loop probes `pragma_table_info('players')` per column and skips the ALTER when
the column is already present, so a re-run never corrupts existing data.

`Database::setPlayerWorldIdByName` patches `world_id` on the row for a given
username so that `NetworkManager::setPlayerWorldId` — the live-plane switch used
by `/gotor` and `/world goto` — survives across relogins (see
[Multiworld & Dimensions](Multiworld-and-Dimensions)). A relogin with a mismatching
`world_id` writes the whole `loadedChunks` state back to zero
(`PlayerSession.hpp`) so the client re-receives the render box for its new plane.

## Inventory and container blobs

### Player inventory

`players.inventory_blob` is stored **as an opaque BLOB**. The bytes are produced
by `PlayerSession` when writing the vanilla `player.dat`-equivalent NBT record
and are not schema-validated at save time — `savePlayerData` calls
`sqlite3_bind_blob(stmt, 13, data.inventoryData.data(), …)` with whatever the
in-memory session handed over. `loadPlayerData` reads them back verbatim
into `PlayerData::inventoryData`.

### Containers (chest, furnace, brewing stand)

Persisted to `block_entities`, keyed by the container's block location `(chunk_x,
chunk_z, rel_x, y, rel_z)` with a `type` field for the specific container. `Database::loadContainer`
and `saveContainer` in `src/storage/Database.cpp` use the compact container-item
format defined in `src/player/PlayerSession.cpp`:

```
varint item_count
repeat item_count times:
    slot   : short
    item_id: short
    count  : byte
    damage : short
```

`deleteContainer` erases the row. `chest_test.py` and `reorg_test.py` (see
[Testing](Testing)) specifically assert this persistence across a **second login
session** — a chest that opens empty on relogin proves the blob round-tripped
through `block_entities`.

## Game rules persistence (`gamerules` table)

Owned by `World::GameRules::getInstance().init(&m_database)` in
`NetworkManager::start` — the singleton reads the whole table once at startup,
then `saveGameRule` writes individual rows back on `/gamerule` edits. See
[Game Rules](Game-Rules) for the semantic table of every rule the engine
supports today.

## Multiworld registry (`world_registry` table)

Referenced by:

- `Database::initWorldRegistry` — creates the schema.
- `Database::setWorldById(id, name, style)` — seeds the four standard planes
  (Overworld=0, Nether=-1, End=1, Aether=100) in `NetworkManager::start`
  **bypassing** the custom ID allocator. Idempotent.
- `Database::registerWorld(name, style, &outId)` — allocates a fresh id ≥ 101
  for `/world create <name> <style>` by scanning `SELECT MAX(id) FROM world_registry`
  and using `max(maxExisting, 100) + 1`. Idempotent: if the world name is already
  registered (case-insensitive), returns that row's id without touching the allocator.
- `Database::lookupWorld(name, &outId, &outStyle)` and `lookupWorldById` — resolve
  at login time. `NetworkWorlds.cpp::lookupWorldId` returns the sentinel
  `-2147483648` (`std::numeric_limits<int32_t>::min()`) when the name is not registered,
  so callers must test that sentinel, not `id < 0` (Nether's legitimate id is `-1`).
- `NetworkWorlds.cpp::getWorldDbPath(name, id, &outDbPath)` maps an id to the
  per-plane `.db` filename used by `WorldStorage::init`.

The full runtime API (`NetworkManager::createWorld`, `listWorldNames`,
`setPlayerWorldId`, `getWorldStorage`) lives on
[Multiworld & Dimensions](Multiworld-and-Dimensions).

## Reading the DBs from the shell

Everything is standard SQLite 3. The sqlite3 CLI will happily open any of the
files:

```bash
sqlite3 server_data.db "SELECT username, world_id, op_level FROM players;"
sqlite3 server_data.db "SELECT id, name, style FROM world_registry;"
sqlite3 world.db       "SELECT COUNT(*) FROM generated_chunks;"
sqlite3 world.db       "SELECT chunk_x, chunk_z, length(data_blob) FROM chunks LIMIT 5;"
```

The `chunks.data_blob` column is LZ4 raw (no header, no length prefix); pipe
it to `lz4 -d` (uncompressed stream) or use `tools/verify_chunk_packet.cpp` to
decode the block grid end-to-end. `books` and `block_entities` have no direct
in-game viewer today — they're the persistence layer for future commands.



## Quirks and known issues

- **Two prepared statements per `WorldStorage`, shared across threads.**
  `m_getChunkStmt` and `m_saveChunkStmt` are *instance fields*, not locals, so any
  code path touching them must go through the corresponding mutex. A new call site
  that reads or writes `chunks` from a new thread must take the same lock or
  prepare its own statement — SQLite will fault on two concurrent uses of the same
  prepared stmt.
- **`m_saveStmtMutex` is recursive, `m_getStmtMutex` is not.** `saveBlockChange`
  → `generateChunk` re-enters `m_saveStmtMutex`, which is why it's a
  `std::recursive_mutex`. The read path never nests — a future caller that
  tries to call `readChunkBlob` while already holding `m_getStmtMutex` will deadlock.
- **`WorldStorage::~WorldStorage` joins the generation worker before closing**
  (sets `m_genStopFlag`, `notify_all`, `join()`) so shutdown does not race a mid-write
  on `m_saveChunkStmt`. The join is gated on `m_genWorker.joinable()`, which is why
  planes without a started worker (rare, but possible if `pregenerateWorld` is
  commented out and no chunk is ever requested) do not throw.
- **`getChunkBlocks` reserves 4096 buckets for a *whole* plane chunk** (256 absY
  × 16 × 16 = 65 536 voxels). Most terrain chunks have well under 4096 non-air
  entries so the reserve is just a hint; do not shrink it — a fully solid chunk
  hits 4096 exactly (one section) and 65 536 (whole column).
- **Comments saying "24 sections" / "196 608 bytes" are stale.** The header
  constant is 16 / 131 072, the wire sectionBitmask is 16 bits, and
  `NUM_SECTIONS ≤ 16` is compile-time asserted. Fix the comments when the code
  touches them, not the constants.
- **Boot-time pre-generation is commented out** in both `NetworkManager::start`
  and `NetworkWorlds::createWorld`. Every plane is therefore fully lazy: expect
  a `bench("Compressed Chunk (x, z) generated in: …")` log line per cold chunk on
  the first login. If you re-enable `pregenerateWorld`, remember the pregen must
  run after `setWorldSeed()` and `setGenerationStyle()`.
- **`isChunkGenerated` re-prepares a `sqlite3_stmt` per call.** It's the hot
  predicate in `updateChunksAroundPlayer`, called for every chunk in the render
  box every tick. If profiling shows it hot, memoise per-session — do not fold
  it into `m_getChunkStmt` (that stmt is bound to a specific (x,z) pair and drives
  the network read; using it for the flag check would clobber the read).
- **`inventory_blob` and `block_entities.data_blob` are opaque.** Nothing validates
  their contents at save or load; a corrupt blob round-trips silently and produces
  undefined NBT behaviour in `PlayerSession`. This is why the chest / inventory
  regression probes in [Testing](Testing) verify these paths across two logins.
- **WAL sidecars (`*.db-wal`, `*.db-shm`) exist while the server runs.** They are
  folded into the DB by SQLite on the next open, so a crash-restart does not
  lose writes — but a hard-kill (`SIGKILL`) with the WAL mid-write can drop the
  tail of the last `COMMIT`. `WorldStorage::generateChunk` wraps the INSERT and
  the `generated_chunks` marker in an explicit `BEGIN TRANSACTION` / `COMMIT` so
  the flag and the blob are atomic; a crash between those two lines can leave a
  `generated_chunks` row without a `chunks` row (rare).
- **`lookupWorld` uses `COLLATE NOCASE`; the custom allocator uses
  `MAX(id) FROM world_registry`.** Because the standard planes and Aether are seeded
  with `setWorldById` at 0/-1/1/100 and the custom allocator starts at 101,
  `MAX(id)` returns 100 on an all-standard world, and the `maxExisting >= 101` guard in
  `registerWorld` keeps the next id at 101 rather than 101+100. If a legacy DB has
  a stray id outside that layout (from an older custom build), the allocator's
  `+1` behaviour follows whatever `MAX(id)` reports — the reserved 100-series for
  Legends-adjacent planes only stays clean if no other row is in that band.

## Cross-links

- [Multiworld & Dimensions](Multiworld-and-Dimensions) — the `worldId ↔ .db`
  mapping, the runtime API (`createWorld`, `setPlayerWorldId`,
  `getWorldStorage`), and the plane-active config flags.
- [World Generation](World-Generation) — `GenStyle` presets
  (`OVERWORLD` / `SUPERFLAT` / `TEST_GRID` / `NETHER` / `AETHER` / `END`), the
  `world-seed` (LCG + Perlin stream seeding), and the per-plane chunk generators
  declared in `include/storage/WorldGen.hpp`.
- [Protocol & Clients](Protocol-and-Clients) — the 16-bit sectionBitmask
  ceiling, the `Map Chunk Bulk` / `Chunk` wire bodies consuming the decoded
  chunk blob, and the section-bitmask emission in `sendFlatChunk` /
  `sendMapChunkBulk`.
- [Game Rules](Game-Rules) — the `gamerules` table semantics and the
  `/gamerule` verbs.
- [Configuration](Configuration) — `[world] gen-style`, `superflat-preset`,
  `world-seed`, and the `*-active` flags that gate each per-plane `.db`.
- [Testing](Testing) — `chest_test.py` and `reorg_test.py` prove
  `block_entities` / `inventory_blob` survive a login boundary;
  `tools/verify_chunk_packet.cpp` decodes the modern chunk blob out-of-process.
- [Troubleshooting](Troubleshooting) — the ~150 ms login-burst hitch, the
  `isChunkReady` / async worker prefetch tuning, and how a missing
  `generated_chunks` row surfaces on `/testpackets`.

