# Configuration

The server reads a single file: **`server.properties`** in the current working directory
(`ConfigManager::getInstance().load("server.properties")` inside `Server::start()`).
`/reload` (op level 4) re-reads it at runtime, so most options can be changed without a restart —
options that gate boot-time allocation (planes, port) still need a restart.

## File format

The parser is `include/core/IniParser.hpp`, driven by `include/core/ConfigManager.hpp`:

| Rule | Detail |
| --- | --- |
| Sections | `[name]` headers scope the keys below them |
| Assignment | `key = value`; whitespace around `=` is trimmed |
| Comments | `#` at line start; blank lines ignored |
| Case | Section and key names match **as written** (`gen-style` ≠ `GEN-STYLE`) |
| Missing file | logs `[ConfigManager] Warning: Could not open server.properties. Generating default settings.`, writes a fresh canonical file, and uses defaults for the current run |
| Older schema | missing canonical keys are backfilled, user values are never overwritten, and the log reads `[ConfigManager] Upgraded server.properties from an older schema to the current one (N key(s) added).` |
| Truthiness | `getBool()` accepts `true`, `1`, `yes`, `on` (case-insensitive); anything else is `false` |

### Canonical example (what a fresh install writes)

```ini
[server]
port = 25565
max-players = 100
motd = LightweightMC C++ Engine
[security]
enable-antiscan = true
blocklist-url = https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt
[world]
gen-style = overworld
superflat-preset = minecraft:flat
world-seed = 0
overworld-active = true
nether-active = true
end-active = true
aether-active = true
aether-plane-id = 100
aether-inventory-policy = standard
aether-force-gamemode = none
[auth]
server-icon = 
```

## `server.properties` reference

Every canonical key lives in `ConfigManager::forEachCanonicalDefault()` — that list is the single
source of truth for both fresh files and schema migration.

| Option | Section | Type | Default | Possible values | Notes |
| --- | --- | --- | --- | --- | --- |
| `port` | `server` | uint16 | `25565` | `1`–`65535` | Passed to `NetworkManager::start(port)` (epoll listener). Fallback when absent: `25565`. |
| `max-players` | `server` | int | `100` | `1`…`1024` practical | Logged as `[CONFIG] Max Players: N`. ⚠️ `Server::start()` passes a **50** fallback, so a missing key yields 50 while a canonical file yields 100 — keep the key present. |
| `difficulty` | `gameplay` | int | `1` | `0` (Peaceful) .. `3` (Hard) | **Phase 14a.** Canonical `[gameplay]` gamerule key read at JoinGame (0x01 byte 2) and Respawn (0x07 difficulty byte) sites — hardcoded literals replaced with `ConfigManager::getInstance().get<int>("gameplay", "difficulty", N)` clamped to 0..3, so `/difficulty <n>` updates the same key both wire spawn-info packets read. Missing key fallback keeps Easy(1) for JoinGame and default for Respawn; bad file values degrade gracefully via the clamp rather than shifting subsequent wire bytes. |
| `enable-antiscan` | `security` | bool | `true` | `true` / `false` | Master switch for the IP-blocklist module; read in `Server.cpp`, `NetworkSocket.cpp` and `ReloadCommand`. Fallback if absent: **`true`**. |
| `blocklist-url` | `security` | string (URL) | `https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt` | any HTTP(S) raw-text URL, or empty = disabled | Fetched list merged into `antiscan_ips.txt`; a local `custom_ips.txt` is merged too — see [AntiScan & Security](AntiScan-and-Security). |
| `gen-style` | `world` | enum string | `overworld` | `overworld`, `nether`, `aether`, `superflat`, `test-grid` | Base-plane generator: 1.7-style (sea level 62, Perlin, caves, trees) / underworld caverns with lava floor / floating islands with slime-trunk trees / legacy flat layers / protocol-validation grid. |
| `superflat-preset` | `world` | enum string | `minecraft:flat` | `minecraft:flat`, `minecraft:stone`, `minecraft:water` | **Only consulted when `gen-style = superflat`** (read in `src/storage/WorldStorage.cpp`). |
| `world-seed` | `world` | uint64 | `0` | `0`…`18446744073709551615`; `0` = unseeded | Mixed into every LCG/Perlin stream, so the same seed reproduces terrain, biomes, villages, mineshafts, dungeons and ore. Java seeds are signed longs — values ≥ 2^63 are engine-only. |
| `overworld-active` | `world` | bool | `true` | `true` / `false` | ⚠️ Plane id 0 is the fallback plane every accessor uses; setting it `false` makes the server **refuse to boot**. |
| `nether-active` | `world` | bool | `true` | `true` / `false` | Gates allocation of the Nether plane (id `-1`) and its `world-nether.db`. |
| `end-active` | `world` | bool | `true` | `true` / `false` | Gates the End plane (id `1`) — a hollow shell with no ore veins by design. |
| `aether-active` | `world` | bool | `true` | `true` / `false` | Gates the Aether "sky world" (id `100`), a Legends-style **special dimension** sharing the `world_registry`. |
| `aether-plane-id` | `world` | int | `100` | Any signed 32-bit int outside `0` / `-1` / `1` (the vanilla pre-1.12 slots) — the server refuses to boot with a colliding value. | The plane id the Aether special dimension occupies. Read once by `NetworkManager::start()` and used for both the `world_registry` row (via `Database::setWorldById`) and the `m_worlds` map key. Setting a value ≥ 101 pushes `Database::registerWorld`'s `max(world_registry.id) + 1` allocator higher so `/world create` never collides. |
| `aether-inventory-policy` | `world` | enum string | `standard` | `standard`, `separate`, `restricted` | **0.0.15.** Per-plane inventory policy for the Aether special dimension. `standard` keeps vanilla-Minecraft's cross-plane blob sharing (the flat `players.inventory_blob` column, `world_registry.inventory_policy=0 SYNC`); `separate` opens a per-plane blob keyed on `player_inventories(uuid, world_id)` (`inventory_policy=1`) so a creative sandbox's loot never leaks back to the mining chain (and vice-versa); `restricted` (`inventory_policy=2`) uses the same storage but treats it as one-way — arriving never inherits the source's inventory and leaving never carries items back (PVP / PVE training maps). Persisted to `world_registry` via `Database::setWorldInventoryPolicy` and mirrored onto `WorldManager::setInventoryPolicy` for the hot path. See [Multiworld & Dimensions](Multiworld-and-Dimensions) § "Plane inventory policy". |
| `aether-force-gamemode` | `world` | enum string | `none` | `none`, `survival`, `creative`, `adventure`, `spectator` | **0.0.15.** Pins `session.gamemode` on entry to Aether (`world_registry.force_gamemode`, `-1 INHERIT` / `0 SURVIVAL` / `1 CREATIVE` / `2 ADVENTURE` / `3 SPECTATOR`). `none` keeps whatever mode the player carried in; the four concrete modes override `session.gamemode` on arrival so a creative sandbox stays creative even when the player was on survival elsewhere. Applied **before** the 0x07 Respawn packet in `NetworkManager::setPlayerWorldAt` so the wire byte and the session's next save mirror agree. |
| `server-icon` | `auth` | string (Base64) | *(empty)* | Base64-encoded PNG/JPEG ≤ 128×128, or empty = no icon field | **Phase 13.** Appended to the `\xFE\xFD` server-status datagram as a trailing `&icon=<base64>` suffix so master-server listings that support icons render the server logo. Empty (the default) emits no suffix at all — status pings stay byte-identical for servers that never configure an icon. |

### Machine-readable option array

```json
[
  {"option": "port",                     "section": "server",   "type": "uint16", "default": "25565",  "possible": "1..65535",                                     "notes": "epoll listen port; fallback 25565"},
  {"option": "max-players",              "section": "server",   "type": "int",    "default": "100",    "possible": "1..1024",                                      "notes": "code fallback is 50 - keep the key present"},
  {"option": "motd",                     "section": "server",   "type": "string", "default": "LightweightMC C++ Engine", "possible": "any single-line text", "notes": "fallback 'A C++ Minecraft Server'"},
  {"option": "difficulty",               "section": "gameplay", "type": "int",    "default": "1",      "possible": "0..3 (peaceful, easy, normal, hard)",        "notes": "phase 14a: JoinGame 0x01 byte + Respawn 0x07 difficulty byte, clamped via std::clamp"},
  {"option": "enable-antiscan",          "section": "security", "type": "bool",   "default": "true",   "possible": "true|false",                                   "notes": "honoured by /reload"},
  {"option": "blocklist-url",            "section": "security", "type": "url",    "default": "https://raw.githubusercontent.com/pebblehost/hunter/master/ips.txt", "possible": "HTTP(S) raw text or empty", "notes": "merged with antiscan_ips.txt and custom_ips.txt"},
  {"option": "gen-style",                "section": "world",    "type": "enum",   "default": "overworld", "possible": "overworld|nether|aether|superflat|test-grid", "notes": "base-plane generator"},
  {"option": "superflat-preset",         "section": "world",    "type": "enum",   "default": "minecraft:flat", "possible": "minecraft:flat|minecraft:stone|minecraft:water", "notes": "only when gen-style=superflat"},
  {"option": "world-seed",               "section": "world",    "type": "uint64", "default": "0",      "possible": "0..2^64-1; 0 = unseeded",                  "notes": "deterministic terrain"},
  {"option": "overworld-active",         "section": "world",    "type": "bool",   "default": "true",   "possible": "true|false",                                   "notes": "false => server refuses to boot"},
  {"option": "nether-active",            "section": "world",    "type": "bool",   "default": "true",   "possible": "true|false",                                   "notes": "plane id -1, file world-nether.db"},
  {"option": "end-active",               "section": "world",    "type": "bool",   "default": "true",   "possible": "true|false",                                   "notes": "plane id 1, no ore veins"},
  {"option": "aether-active",            "section": "world",    "type": "bool",   "default": "true",   "possible": "true|false",                                   "notes": "plane id 100, file world-aether.db"},
    {"option": "aether-plane-id",            "section": "world",    "type": "int",    "default": "100",  "possible": "any signed int outside 0/-1/1",       "notes": "plane id for world-aether.db"},
  {"option": "aether-inventory-policy",   "section": "world",    "type": "enum",   "default": "standard", "possible": "standard|separate|restricted",          "notes": "0.0.15: SYNC=flat players.inventory_blob / SEPARATE=player_inventories row / RESTRICTED=one-way"},
  {"option": "aether-force-gamemode",     "section": "world",    "type": "enum",   "default": "none",     "possible": "none|survival|creative|adventure|spectator", "notes": "0.0.15: pins session.gamemode on plane entry (-1 INHERIT / 0..3)"}
]
```

## Reading options at runtime

| Accessor | Used for | Behaviour on a missing key |
| --- | --- | --- |
| `getString(section, key, fallback)` | text values | returns `fallback` |
| `get<T>(section, key, fallback)` | `uint16_t`, `int`, `std::uint64_t` | returns `fallback` |
| `getBool(section, key, fallback)` | booleans | returns `fallback` |
| `set(section, key, value)` | write path (`/reload`, tooling) | overwrites the canonical key |

Because every call site passes its own fallback, the **canonical defaults are the only consistent set**:
keep all keys present in the file, otherwise a typo silently changes behaviour.

Related pages: [Commands](Commands) (`/reload`), [World Generation](World-Generation) (`gen-style`,
`world-seed`), [Multiworld & Dimensions](Multiworld-and-Dimensions) (the `*-active` flags).

