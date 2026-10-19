# Multiworld & Dimensions

A single LightweightMC server hosts **multiple Minecraft-style dimension planes**
at once, keyed by `PlayerSession::worldId`. Each plane gets its own `WorldStorage`
instance, its own SQLite file, its own `GenStyle`, and its own off-thread generation
worker. Standard planes use the pre-1.12 vanilla dimension IDs from Java MC 1.7
(including a **negative** one); a Minecraft Legends-style "special dimension" sits
at the configurable id given by the `[world] aether-plane-id` key (default **100**);
custom planes created with `/world create` allocate ids 101+.

## The plane table

Every plane the server knows about is registered in the SQLite `world_registry`
table inside `server_data.db`. Standard planes are hard-wired in
`NetworkSocket.cpp::NetworkManager::start`; custom planes are appended by
`Database::registerWorld`. See [Storage & Save Files](Storage-and-Save-Files) for
the `world_registry` schema.

| id | Name | Style | SQLite file | Registered by |
| -- | ---- | ----- | ----------- | ------------- |
| **0** | `overworld` | `OVERWORLD` / `SUPERFLAT` / `TEST_GRID` per `[world] gen-style` | `world.db` | `NetworkManager::start` — `planeSpecs[4]` → `Database::setWorldById` |
| **-1** | `nether` | `NETHER` | `world-nether.db` | same |
| **1** | `end` | `END` | `world-end.db` | same |
| **`aether-plane-id` (default 100)** | `aether` | `AETHER` | `world-aether.db` | same — configurable via the `[world] aether-plane-id` key so a future Legends-style plane can claim a different 100-series id; must not collide with 0 / −1 / 1 |
| **>= 101** | custom `<name>` | caller-supplied | `world-<safeName>.db` | `Database::registerWorld` via `/world create` |

Two consequences of using the pre-1.12 IDs:

1. **`m_worlds` is a `std::unordered_map<int32_t, unique_ptr<WorldStorage>>`,
   not a vector indexed by worldId.** Subscripting a vector with `worldId = -1`
   (the Nether id) after `static_cast` to `std::size_t` is UB and previously
   crashed the server on any Nether transition. Every accessor uses `find()`
   and falls back to Overworld on a miss.
2. **`NetworkManager::lookupWorldId(name)` returns the int32 sentinel
   `std::numeric_limits<int32_t>::min()` (i.e. -2147483648) on miss**, not `-1`,
   because `-1` is a real Nether id. Callers must test the sentinel, not `id < 0`.

`describeWorld(name, outId, outStyle, outDbPath)` is a friendlier lookup used by
`/world info`; on a hit for Nether it returns `true` with `outId = -1`, so callers
must **not** test `outId` for "not found" the way `lookupWorldId`'s sentinel does.

## Sky render trick for non-standard planes

Vanilla 1.8 clients only know **three sky renders**, and they pick which one
to draw from the `dimension` field of the S2C `JoinGame` (0x01) byte at login
and the S2C `Respawn` (0x07) int32 on plane switch:

| Wire dimension | Sky shown by client |
| -------------- | ------------------- |
| `0`  | Overworld (blue) |
| `1`  | End / Star Keep (purple) |
| `-1` | Nether (dark grey) |

There is no `100` or `>=101` sky. Any plane the server runs at a non-vanilla
id (Aether at 100 by default, custom `/world create` at 101+, and the planes
landing in 0.0.15) would otherwise cause the client to keep the previous
plane's sky alive across the switch and default to Overworld on reconnect —
the reported "Aether keeps last world's sky until reconnect" symptom.

`WorldManager::skyDimensionFor(planeId)` resolves the trick:

- `planeId` in `{0, 1, -1}` → returns itself, so vanilla planes draw their
  own sky.
- Everything else → returns whatever `WorldManager::setSkyDimension(planeId,
  hint)` registered, or **0 (Overworld sky)** as the default for custom /
  future planes.

`NetworkWorlds::setPlayerWorldAt` writes that value into the 0x07 `dimension`
int32 (instead of the raw newWorldId), and `PlayerManager::sendPlayPackets`
writes `static_cast<uint8_t>(static_cast<int8_t>(skyDimensionFor(session.worldId)))`
into the 0x01 `dimension` byte at login (Nether's -1 becomes the expected
`0xFF` wire byte). Server-side state — `session.worldId`, `m_worlds` keys,
`player_positions` SQLite rows — keeps the **true** plane id, so storage,
chunk streaming and the persistence paths are unaffected; only the wire
value visible to the client is remapped.

`setSkyDimension` silently ignores hints outside `{0, 1, -1}` (any other
value would render as an unrecognised sky). Callers added in 0.0.15 (the
`/world create --sky <overworld|nether|end>` option, dimension registry, etc.)
call it to opt each new plane into the sky it wants.

## Configuration


The `[world]` block in `server.properties` decides which standard planes exist
on startup. Custom planes live only in `world_registry` — no config key per
custom plane:

```ini
[world]
gen-style = overworld
world-seed = 1337
overworld-active = true
nether-active = true
end-active = true
aether-active = true
aether-plane-id = 100
```

| Key | Type | Default | Read at | Effect |
| --- | --- | --- | --- | --- |
| `overworld-active` | bool | `true` | `NetworkManager::start` | If `false`, `start()` refuses to boot and logs an error — every accessor falls back to plane 0 when a session's `worldId` is unknown or a plane is missing. Cannot be turned off. |
| `nether-active` | bool | `true` | `NetworkManager::start` | If `false`, the `world-nether.db` plane is skipped at startup but the `world_registry` row is preserved (`Skipping inactive plane id=…` info log). Re-enable by flipping the key; nothing needs to be re-registered. |
| `end-active` | bool | `true` | same | Same for plane id = 1. |
| `aether-active` | bool | `true` | same | Same for plane id = 100. |
| `aether-plane-id` | int | `100` | `NetworkManager::start` | The plane id the Aether special dimension occupies. Read once at startup, then used for both the `world_registry` row (`Database::setWorldById`) and the `m_worlds` map key so the same config value drives both halves. Must not collide with `0` / `-1` / `1` (the vanilla pre-1.12 slots — the server refuses to boot with a colliding value). Setting it to a value ≥ 101 pushes `Database::registerWorld`'s `max(world_registry.id) + 1` allocator higher so `/world create` never collides with the reserved slot. |

Standard planes are registered by walking a fixed `planeSpecs` array in
`NetworkSocket.cpp::NetworkManager::start`:

```cpp
struct PlaneSpec
{
    int32_t id;
    const char *dbPath;
    Storage::GenStyle style;
    bool active;
};
const PlaneSpec planeSpecs[4] = {
    {   0,   "world.db",        overworldStyle,              overworldActive},
    {  -1,   "world-nether.db", Storage::GenStyle::NETHER,   netherActive},
    {   1,   "world-end.db",    Storage::GenStyle::END,      endActive},
    { 100,   "world-aether.db", Storage::GenStyle::AETHER,   aetherActive},
};
```

For each spec: if `!spec.active`, log an info (`Skipping inactive plane id=…`)
and continue — the registry row still exists but no `WorldStorage` is created.
Otherwise allocate a `WorldStorage` and `storage->init(spec.dbPath)`, then
call `Database::setWorldById(spec.id, name, style)` (idempotent).

`Database::registerWorld(name, style, &outId)` and `Database::setWorldById(id,
name, style)` are the two halves of the registry: `setWorldById` is used only
for the four hard-wired ids, so a fork that wants `overworld` at a different id
has to switch both places. `registerWorld` picks the next id as
`max(id) + 1` starting at `kFirstCustomWorldId = 101`.

## Style ↔ name mapping

`NetworkWorlds.cpp::styleFromName` (used by `/world create <name> <style>`):

| Style string | `GenStyle` value |
| ------------ | ---------------- |
| `overworld` | `OVERWORLD` (also the fallback for unknown / empty) |
| `nether` | `NETHER` |
| `end` | `END` |
| `aether` | `AETHER` |
| `superflat` | `SUPERFLAT` |
| `test-grid` | `TEST_GRID` |
| anything else | `OVERWORLD` silently (no warn — unlike `NetworkManager::start` which logs) |



## Session → plane resolution

`PlayerSession::worldId` (see `include/player/PlayerSession.hpp:116`) is the
single integer that decides which plane a client is currently on. Every
accessor in `WorldManager` goes through `getWorldStorageFor(session.worldId)`:

```cpp
Storage::WorldStorage &WorldManager::getWorldStorageFor(int32_t worldId)
{
    auto it = network.m_worlds.find(worldId);
    if (it != network.m_worlds.end())
        return *it.second;
    return *network.m_worlds.at(0);   // fall back to Overworld
}
```

The `m_worlds` map is `std::unordered_map<int32_t, unique_ptr<WorldStorage>>` —
keyed by signed `int32_t`, never indexed with `[]` or `at(worldId)` directly.
The pre-1.12 Nether id **−1** is legal; `m_worlds.at(-1)` works and
`std::vector<...>` indexed by −1 is UB. That was the bug `NetworkManager::start`
calls out in the startup comment:

```cpp
Core::Logger::info("[NetworkManager] Multiworld ready: pre-1.12 dimension IDs "
                   "are now signed int32_t (Overworld=0, Nether=-1, End=1, "
                   "Aether=100 reserved special dimension); custom planes via "
                   "/world create get IDs 101+ and are stored in the SQLite "
                   "world_registry table in server_data.db.");
```

## Login flow

`LoginHandler.cpp::handle` (Login Start, packet `0x00`) does not set
`session.worldId` itself. The login path:

1. `LoginHandler::handle` reads the username, resolves the player UUID via
   `PlayerManager::getPlayerUuid(username)`, replies `0x02` Login Success.
2. Calls `PlayerManager::sendPlayPackets(clientFd, session, username, clients,
   worldManager, database, …)`.
3. `sendPlayPackets` calls `worldManager.getWorldStorageFor(session.worldId)`
   (in `src/managers/WorldManager.cpp:219` and other sites).
4. Right after login, `LoginHandler.cpp:52` calls
   `EntityManager::spawnForSession(session, worldManager.getWorldStorageFor(session.worldId), …)`
   so a session on a non-default plane gets its mobs seeded from that plane's
   terrain, not the Overworld's.

`session.worldId` starts at 0 for every fresh session because `PlayerSession`
default-initialises it to 0 (line 116). The `players` SQLite table has a
`world_id INTEGER DEFAULT 0` column and `Database::setPlayerWorldIdByName`
persists a plane change, but **nothing currently reads that column back on
login** — see the mismatch note below.

## Plane inventory policy (0.0.15 plane-switch inventory fix)

Vanilla Minecraft carries **one inventory** across dimension planes — items
gathered on Overworld appear on Nether and vice-versa. The server follows
that rule for the standard chain (Overworld #0 / Nether #-1 / End #1 /
Aether #100 by default / mining worlds). Special planes need to break that
rule, and 0.0.15 adds a per-plane policy for exactly that:

| Policy         | Value | Storage slot                                                 | Typical use                                                |
| -------------- | ----- | ------------------------------------------------------------ | ---------------------------------------------------------- |
| `SYNC`         | 0     | `players.inventory_blob` (flat column)                       | Standard Minecraft behaviour — items transfer across planes (Aether, mining worlds, vanilla 0/-1/1, ...) |
| `SEPARATE`     | 1     | `player_inventories(uuid, world_id)` row                     | Flat / creative sandbox: a plane with its own loot that never leaks back to the mining chain (typically paired with `force_gamemode=creative`) |
| `RESTRICTED`   | 2     | `player_inventories(uuid, world_id)` row (one-way)           | PVP / PVE training worlds — items gained on the plane never leave it, and arriving from any other plane never inherits from the source's inventory |

Two new `INTEGER` columns on `world_registry` (`inventory_policy` /
`force_gamemode`) carry the per-plane choice. Both default to standard
Minecraft behaviour (`SYNC` / `-1 INHERIT`) so pre-0.0.15 rows keep the
behaviour they had before. Existing 0.0.14 server_data.db rows pick up
the columns via `ALTER TABLE` migrations inside `Database::initWorldRegistry`;
new DBs get them from the CREATE.

`force_gamemode` is orthogonal and lets the plane pin a game mode on
entry: `-1 INHERIT` keeps whatever mode the player carried in, `0`
SURVIVAL / `1` CREATIVE / `2` ADVENTURE / `3` SPECTATOR override
`session.gamemode` on arrival so a creative sandbox stays creative
even when the player was on survival elsewhere.

### Configuration

The reserved Aether special dimension exposes two keys in `server.properties`:

```
[world] aether-inventory-policy = standard   # standard | separate | restricted
[world] aether-force-gamemode   = none       # none | survival | creative | adventure | spectator
```

Both default to the standard-Minecraft behaviour (`standard` / `none`),
so setting them to `separate creative` turns Aether into a flat creative
sandbox with its own inventory while the vanilla 0 / -1 / 1 planes keep
sharing the mining-chain blob.

Custom `/world create` planes can be opted in later in 0.0.15 (a
follow-up extends `/world create` with `--inventory <standard|separate|restricted>`
and `--force-gamemode <mode>`; the DB-level setters
`Database::setWorldInventoryPolicy(id, policy)` and
`Database::setWorldForceGameMode(id, gm)` plus the in-memory `WorldManager`
mirrors `setInventoryPolicy` / `setForceGameModeOnEntry` are already
wired so the follow-up only needs to route the CLI flags through them).

> **0.0.15 phase 4 update (2026-10-15):** that follow-up is now live.
> `/world create <name> <style>` grew three optional flags —
> `--inventory <standard|separate|restricted>`,
> `--force-gamemode <survival|creative|adventure|spectator|none>` and
> `--seed <int>` — routed through `NetworkManager::createWorld` into
> `Database::setWorldInventoryPolicy` / `setWorldForceGameMode` /
> `setWorldSeed` and mirrored into `WorldManager::setInventoryPolicy`
> / `setForceGameModeOnEntry` so the login path and setPlayerWorldAt
> pick them up without any further wiring. The CLI also gained a
> `flat` style alias for the CREATIVE generator (an infinite flat
> canvas: solid dirt/grass column, no ore, trees or structures) so
> admins get a semantically-obvious name for the sandbox planes
> without editing the `Storage::GenStyle` enum. Omitted flags leave
> the standard-Minecraft SYNC / INHERIT / inherit-from-Overworld
> behaviour untouched, keeping the 0.0.15 phase 3 row layout byte-
> identical.

### Server-side state

Server-side keeps the **true plane id** on `session.worldId`, on the
`m_worlds` map key, and on the `player_positions` rows; only the wire-
visible values are remapped per-plane.

- `NetworkManager::setPlayerWorldAt` transfers the inventory before
  emitting the 0x07 Respawn (dimension field via `skyDimensionFor`,
  gamemode byte reflecting the forced mode) so the client's inventory
  and mode both match what the server will persist.
- `Database::loadPlayerData` consults the plane row's `inventory_policy`
  after reading `world_id` and, for `SEPARATE`/`RESTRICTED` policies,
  overrides `PlayerData::inventoryData` with the plane-scoped blob
  (empty on first visit — matching "fresh creative sandbox / fresh
  training map").
- `PlayerManager::savePlayerState` routes the current session blob to
  the plane row for non-SYNC policies and re-reads the flat SYNC blob
  via `Database::loadSyncInventoryBlob(uuid, blob)` so the INSERT-OR-
  REPLACE that `Database::savePlayerData` runs preserves the shared
  SYNC chain rather than wiping it with the plane-scoped blob.

## Plane switch on `/gotor` (or `/gowe`, `/gworld`)


## Plane switch on `/gotor` (or `/gowe`, `/gworld`)

`GotorCommand` (`include/commands/WorldCommands.hpp`) delegates to
`Server.cpp::consoleCtx.switchPlayerWorld`, which calls
`NetworkManager::lookupWorldId(worldName)` and hands the resulting id to
`NetworkManager::setPlayerWorldId(playerName, id)`:

```cpp
const int32_t id = m_network.lookupWorldId(worldName);
// lookupWorldId returns the -2147483648 sentinel when the plane is
// unknown -- -1 is a *real* Nether id, so callers must compare to the
// sentinel, not to 0 or a sign check.
if (id == std::numeric_limits<int32_t>::min()) return false;
return m_network.setPlayerWorldId(playerName, id);
```

`setPlayerWorldId(playerName, newWorldId)` (in
`src/network/NetworkWorlds.cpp:258`) does the following in order:

1. `m_worlds.find(newWorldId)` — refuse if the plane is not registered.
2. Look up the client by username and require that it's in the `PLAY` state;
   a mid-login player has no position to preserve/animate against.
3. Read the current X/Z of the session and call
   `target->findStandingFeetY(wx, wz, 62)` on the **new** plane so the player
   surfaces on real terrain. If that returns −1 (nowhere to stand on the
   column), the change is refused.
4. Write the new plane's Y/pos into the session.
5. **Clear `session.loadedChunks`** — that set is the "chunks already sent to
   this client" set; clearing it means the next
   `updateChunksAroundPlayer` streams the new plane's chunks fresh.
6. Send an S2C `Position` packet (`0x08`) so the client animates the jump
   instead of walking through walls the player built on the old plane.
7. Assign `session.worldId = newWorldId` (the only place in the codebase that
   writes this field).
8. Persist the change with `Database::setPlayerWorldIdByName(username, newWorldId)`.

Aliases: `gowe` and `gworld`. Requires op level 2. The console can only use
the two-argument form (`/gotor <player> <worldName>`); the one-argument form
only works from a chat client because the console has no world position of
its own (see the `ctx.isConsole` guard in `GotorCommand::execute`).



## `/world` subcommand tree

`WorldCommand::execute` (also `include/commands/WorldCommands.hpp`) wires three
sub-verbs via `CommandContext` closures in `src/core/Server.cpp`:

| Verb | Backing call | Notes |
| ---- | ------------ | ----- |
| `/world list` | `ctx.listWorlds()` → `NetworkManager::listWorldNames()` | Walks live in-memory `m_worlds`; a plane registered but skipped at startup (`active = false` in the config) is **omitted** because the server isn't serving chunks for it. |
| `/world create <name> <style> [--inventory <standard\|separate\|restricted>] [--force-gamemode <survival\|creative\|adventure\|spectator\|none>] [--seed <int>]` | `ctx.createWorld(name, style, invPolicy, forceGm, seed)` → `NetworkManager::createWorld` | Registers a new plane. Allocates the next id starting at **101** (`kFirstCustomWorldId`), writes a row to `world_registry`, opens `world-<safeName>.db`, sets `GenStyle` from `styleFromName(style)`, copies the Overworld's `worldSeed` (`primary->getWorldSeed()`), and emplaces the `WorldStorage` into `m_worlds`. Idempotent — calling again for the same name returns the same id. 0.0.15 phase 4 adds three optional flags routed through `Database::setWorldInventoryPolicy` / `setWorldForceGameMode` / `setWorldSeed` (with `WorldManager::setInventoryPolicy` / `setForceGameModeOnEntry` in-memory mirrors) so a custom plane can pin a per-plane sandbox inventory, a forced game mode on entry, or an explicit world seed without boot-time `[world] aether-*` config keys. Omitting the flags preserves the standard-Minecraft SYNC / INHERIT / inherit-from-Overworld behaviour. `style` also gained a `flat` alias for `Storage::GenStyle::CREATIVE` (see `styleFromName` in `NetworkWorlds.cpp`). |
| `/world info <name>` | `ctx.describeWorld(name, &outId, &outStyle, &outDbPath)` → `NetworkManager::describeWorld` | Returns `true` on hit with `outId` (may legitimately be −1 for Nether) plus the style and the SQLite path. Uses the friendly `outId == -1 is fine` contract — see the plane table above. |

`safeName` for custom planes lowercases and replaces anything outside
`[A-Za-z0-9_-]` with `_` — see `NetworkWorlds.cpp:120`. This prevents a
rogue `/world create ../evil overworld` from smuggling a path traversal into
the working directory.

**There is no `/world delete`.** A plane that lives in `world_registry` stays
in the registry forever; to hide it, set `<name>-active = false` (standard
planes only) or leave the plane unregistered. `/world list` deliberately
skips inactive planes so they're invisible to operators without having to be
deleted.

## Custom-plane db path derivation

`NetworkWorlds.cpp::describeWorld` maps the plane id to its SQLite file:

```cpp
switch (outId)
{
case 0:   outDbPath = "world.db";          break;
case -1:  outDbPath = "world-nether.db";   break;
case 1:   outDbPath = "world-end.db";      break;
case 100: outDbPath = "world-aether.db";   break;
default:  // safeName(name): lower-case, non-[A-Za-z0-9_-] -> '_'
          outDbPath = "world-" + safeName + ".db";
}
```

All plane DBs sit next to `server_data.db` in the process working directory
(the server is documented to be started from the repo root — see
[Installation](Installation)).

## Persistence

Two SQLite structures participate:

- **`world_registry`** in `server_data.db` — one row per registered plane
  (`id`, `name`, `style`). Created by `Database::initWorldRegistry` inside
  `NetworkManager::start`, immediately after `Database::init`. See
  [Storage & Save Files](Storage-and-Save-Files) for the schema and for the
  WAL/NORMAL pragmas.
- **`players.world_id`** in `server_data.db` — the plane a logged-in player
  currently lives on. `Database::setPlayerWorldIdByName(username, worldId)`
  writes it from `NetworkManager::setPlayerWorldId`.

Each plane's per-plane `world*.db` file (`chunks` + `generated_chunks` tables)
is documented on [Storage & Save Files](Storage-and-Save-Files).



## Doc/code mismatches worth flagging

> **`pregenerateWorld` at `createWorld` is commented out.** `NetworkWorlds.cpp:210`
> contains `// storage->pregenerateWorld(12);`. A new `/world create <name>`
> currently has an empty plane DB; the first client to walk there generates
> chunks on-demand (login burst). See the same note on
> [World Generation](World-Generation).

The following four inconsistencies present in 0.0.13 were resolved in 0.0.14:

- **`players.world_id` is now read on login.** `Database::loadPlayerData` adds
  `world_id` to its SELECT (column 34) and stores it on
  `Storage::PlayerData::worldId`; the login handler in `PlayerManager.cpp`
  copies that onto `session.worldId` before the first `getWorldStorageFor(...)`
  call, so a player who used `/gotor nether` and reconnects is restored to
  plane −1. `savePlayerData` also round-trips the column, so a live-plane
  change persists across relogins even without
  `Database::setPlayerWorldIdByName` firing.
- **`PlayerSession.hpp`'s layout comment (line ~103) now matches the code.**
  It documents the pre-1.12 vanilla IDs (Overworld 0, Nether −1, End 1), the
  reserved Aether slot, and the 101+ custom range.
- **`aether-plane-id` (int, default 100) replaces `aether-special-dimension`.**
  `NetworkManager::start()` reads it, feeds it to `Database::setWorldById`,
  and uses the same value as the `m_worlds` map key in `planeSpecs`.
  `describeWorld`'s per-plane SQLite path is now resolved by **name** rather
  than by numeric id so a fork that re-homes Aether to a different 100-series
  id keeps getting the right `world-aether.db` file.
- **`WorldCommands.hpp`'s `/world create` doc-comment now reads "id >= 101"**,
  matching `kFirstCustomWorldId = 101`.

## Related pages

- [World Generation](World-Generation) — the per-plane generators, ore profiles,
  and standing-surface helpers `setPlayerWorldId` calls.
- [Storage & Save Files](Storage-and-Save-Files) — SQLite schema for `world_registry`
  and the per-plane chunk store.
- [Configuration](Configuration) — the `[world]` block keys referenced here.
- [Commands](Commands) — the full 84-verb list including `/gotor`,
  `/world list`, `/world create`, `/world info`.
- [Protocol & Clients](Protocol-and-Clients) — where the `0x08 Position` packet
  from a plane switch is on the wire for a given era.
- [Game Rules](Game-Rules) — gamerules consumed by `EntityManager::spawnForSession`
  on login (per-plane mob seeding).

