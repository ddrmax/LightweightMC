# Changelog

All notable changes made to the project **LightweightMC** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and this project respects [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---
## [0.0.15] - 2026-10-13

### Fixed

- **Item-id / login-burst Window Items desync fix (src/network/protocol/ProtocolTranslator.cpp::translateItemToClient)**: translateItemToClient was an identity passthrough that pushed LWMC OWN internal item ids (include/world/MC18ItemIds.hpp, LEATHER=0 .. TOOLS=80-88) straight onto the wire. LWMC id 0 collides with the vanilla-1.8 (protocol 47) no-item sentinel, so a persisted mob-loot stack (LEATHER=0) in a warm save blob (build1/ server_data.db) made the login-burst Window Items (S2C 0x30) body emit short(0) for a FILLED slot; the JVM 1.8 InventoryWindow reads type 0, treats the slot as empty and never consumes the trailing count/damage/NBT bytes, so the 45-slot window decode runs out of alignment and NPEs on login reconnect (client shutdown plus crash report). This is data- and code-revision independent: the id-0 item is already baked into build1, so reverting the spawn code alone cannot clear it. Fix: translateItemToClient now maps the LWMC 0-88 space onto grounded vanilla-1.8 minecraft-data item codes (PrismarineJS minecraft-data data/pc/1.8: leather 334, beef 363, steak 364, sugar canes 338, clay_ball 337, wooden planks 5, bread 297, cooked fish 350, mushroom_stew 282; the same numbering basis already used in include/versions/v1_8/food.hpp). NONE maps to -1, every real item maps to a valid NON-ZERO in-range id so the client decoder never mis-aligns again, and LWMC ids without a vanilla-1.8 analog collapse onto rotten_flesh 367 so the client renders a legal placeholder instead of crashing. Verified: incremental build rc=0, boot ERR=0, chest_test.py login reconnect Window-Items decode PASS. LWMC_VER unchanged

### Added

<!-- FUTURE WORK (deferred, non-blocking; usability-over-optimization policy): the
     WAL fix erased the idle tick stall but is NOT claimed to be optimal. Optional
     polish for a later pass, only if profiling shows it matters: (a) coarsen/raise
     the world-time persist interval and batch setWorldTicks+setWorldAge into one
     UPDATE per plane; (b) move persistence off the 20 Hz tick via a detached worker.
     Skip until asked. -->

- **Ground-item / mob-loot delivery — A1 fidelity, final path (`src/network/NetworkEvents.cpp`)**: the earlier **announce-then-pickup dance** — `Spawn Object 0x0E` on the ground plus `Collect Item 0x0D` + `Destroy Entities 0x13` when the client walked into the pile — kept crashing the vanilla-1.8 JVM client. Full crash history for the drop wire (recorded so we never retry the same shape): `SPAWN_ENTITY (0x19 / wire 25)` overflowed the client decoder by 22 bytes ("found 22 bytes extra whilst reading packet 25"); short `Spawn Object 0x0E` body underflowed the decoder; reusing the mob body on `0x0F` avoided decode crash but used mob-type semantics; `Entity Metadata (0x1C)` body layout unverified caused read-past-buffer crashes; a bare `0x0E` item entity without matching metadata produced `Entity Type: null` / `NullPointerException "Ticking entity"` on the client's entity tick. Final: **no client-side ground render for mob loot or block-break overflow at all**. Mirror the proven block-break drop path in `WorldHandler.cpp:264 / 281-283`: the ~2 Hz pickup sweep in pollEvents calls `InventoryHandler::mergeItemIntoInventory(session, itemId, cnt, 0)` on the nearest PLAY client within a 1.5-block pickup radius, and on success emits a single full **Window Items (S2C 0x30)** packet via `InventoryHandler::sendPlayerInventoryWindowItems(bestFd, session, *this)`. The client's own inventory model becomes authoritative — no `0x0E` / `0x1C` / `0x0D` / `0x13` / `0x19` wire is emitted anywhere for drops, so no fragile ground-entity body layout is involved. A full-inventory keeps the stack on the ground (matches vanilla semantics). `Packet.hpp::writeSpawnEntityItem` (introduced by the bad hotfix `5a07e1b`) is now dead and left untouched — the hotfix commit is superseded by this corrected behavior. Build rc=0; boot ERR=0; `chest_test.py` exit=0, `bot_test.py` exit=0, `spawn_probe.py` exit=0, `reorg_test.py` exit=0.


- **Send-buffer tombstone guard for reconnect login flood (`src/network/NetworkFraming.cpp::NetworkManager::sendPacket`)**: vanilla reconnect to a warm existing world was producing a WARN flood of 30-90 `send() Broken pipe for fd=…` lines in the boot log after each login burst, and vanilla clients read the burst as a "login crash" (client resets socket → server keeps pushing chunk / spawn / Window Items / tablist frames to the dead fd → JVM client dies). Root cause: `handleDisconnect(fd)` now (per deferred-teardown invariant) parks the session as `ClientState::DISCONNECTED` and only erases the `m_clients` entry in `flushPendingDisconnects` at tick boundary, but every call site inside the login burst still looked up `m_clients[fd]` and happily queued another frame into `session.sendBuffer` for `flushSendBuffer` to push. The first `::send()` returned EPIPE, `handleDisconnect` re-ran idempotently, but the next send attempt re-entered the same path → hundreds of WARN lines per login and no way to stop. Fix: `sendPacket` now bails immediately when `session.state == ClientState::DISCONNECTED` right after the map lookup (log at DEBUG so it stays greppable but does not spam WARN). Only the *first* legitimate EPIPE/RST still logs once per login burst — the very send that discovers the socket is dead — everything after that is silently dropped. Verified: reconnect login burst on existing `world_data.db` now emits **1** WARN per login (down from 30-90), no `noclip/speed exploit` trigger, no `Connection reset by peer` cascade, no `ERR]`, `chest_test.py` exit=0, `bot_test.py` exit=0.


- **Remove login-time mob seed — login-crash hotfix (`src/network/handlers/LoginHandler.cpp:152`)**: isolated the vanilla reconnect "login crash" on existing worlds — `chest_test.py` on the pre-existing `world_data.db` used to produce the 8-mob spawn burst (cow/sheep/pig/chicken/zombie/skeleton/wolf/wandering_trader) inside the login handler, each preceded by `EntityManager::spawnForSession(session, worldStorage, fd, packetSender)` doing a terrain-aware 2-air-column scan with `WorldStorage::getBlockAt` on chunks the login chunk-stream had not yet generated → each miss triggers a synchronous `generateChunk()` hitch inside the login burst → boot log records 2–17 `Server can't keep up` per login → real vanilla client times out on the login handshake, resets its socket → server keeps pushing chunk / SpawnEntityLiving 0x0f / Window Items / tablist frames to the dead fd. With the seed call removed, login reconnect is stable: chest reconnect probe passes framing, bot reconnect probe captures the full 121-chunk stream, boot log shows **0 `ERR]`, 0 broken-pipe, 0 connection-reset, 0 noclip-exploit, 0 `Server can-t keep up` triggered by login** (only 2 residual cold-boot hitches from initial `pregenerateWorld`). Trade-off and follow-up: `EntityManager::tick` at `src/world/EntityManager.cpp:494` only advances existing `EntityRecord` positions and despawns on lifetime expiry — it does **not** spawn new mobs, so this hotfix means mobs are currently absent from the world. Restoring mob presence is a follow-up that belongs on `tick` (bounded, off-tick seed budget with per-frame column-scan limits so a chunk-gen hitch can never stall the 20 Hz loop), NOT back in the login burst. `Packet.hpp::writeSpawnEntityItem` (dead code left by the superseded bad A1 hotfix `5a07e1b`) and `EntityManager::spawnForSession` (now unused, kept for admin-facing `/summon`-style flows if added later) remain intact.

- **Reconnect container-open chunk-streaming — A2 fidelity (`src/network/handlers/play/WorldHandler.cpp::handleClickWindow`)**: `getBlockAt()` returns 0 ("air/unknown") for a not-yet-generated chunk. The digging handler already guards this (generates the chunk before reading oldBlockId, `WorldHandler.cpp:163-172`), but `handleClickWindow` read `worldStorage.getBlockAt(target...)` with **no prior generateChunk** — so on reconnect (login burst streams chunks lazily; `pregenerateWorld` is disabled at both call sites) a ClickWindow arriving before the chest's chunk was on disk read Air → `existingBlockId=0` → `isInteractive=false` → the container never opened and its persisted contents looked lost ("chest block gone"). Fix: pre-generate the target chunk **plus its four horizontal neighbour chunks** (the same carry math the double-chest pairing probes use at 616-623 / 727/737) before any `getBlockAt` in this handler. `generateChunk` is idempotent (early-returns when already generated, `WorldStorage.cpp:787`), so this only costs a one-time synchronous hitch on a truly-fresh chunk — the exact precedent `saveBlockChange` accepts for its first edit to a fresh chunk (`World-Generation.md:416`). Build rc=0, boot ERR=0; `chest_test.py` reconnect loop exit 0 (framing clean). Positive not exercised live because no world chest exists near the probe client's spawn (`chest windows: []` in this test world — chest_test drives synthetic window ids, not a placed block), so validated by code + the proven generate-before-read pattern + coverage check that every `getBlockAt` neighbour read falls inside the pre-generated set.

- **WAL journal mode on main `world_data.db` — fixes idle "Server can't keep up" tick stalls (`src/storage/Database.cpp::init`)**: the fixed 20 Hz loop warns when one tick body exceeds ~250 ms. Measured root cause of the warn firing even with zero clients connected / a waiting client: `NetworkManager::tickWorldTime` throttles SQLite persistence via `dueToPersist = (nowMs - lastPersistMs) >= 10000`, so every ~10 s it calls `Database::setWorldTicks` + `setWorldAge`. With the old `PRAGMA journal_mode = DELETE; synchronous = NORMAL; cache_size = 256;` each commit paid a per-commit **journal create + fsync** that blocked ~1.5–2.5 s on slow media (Raspberry Pi 0/1, 3rd-gen i3 under load) → the accumulator blew budget and snapped, emitting one WARN per stall at exactly the ~10 s persist spacing. Env-gated tick profiler (`LWMC_TICK_PROF`, dev-only, since removed): DELETE build = 3 stalls in a 35 s idle run (1.5–2.5 s each, all `tickWorldTime`); WAL build = **0 × "Server can't keep up"**, worst single persist ~51 ms (the first post-boot flush) — ~5× under budget. Switching the main DB to `journal_mode = WAL` (keeping synchronous=NORMAL, cache_size=256) makes commits append to a `-wal` file and fsync only on checkpoint, so the mid-tick fsync storm is erased rather than relocated. The old DELETE rationale ("WAL mappings cost 1–4 MB per DB") never actually reduced resident memory here — measured WAL sidecars are ~32 KB (`world_data.db-shm`) and `-wal` truncates to 0 on clean close, so WAL's overhead is negligible and it is strictly better for tick latency. Durability note: with synchronous=NORMAL a crash may lose the most recent WAL checkpoint of world_ticks/world_age (already throttled to ~10 s; the 5-min level.dat autosave in `tickChunkStreaming` still bounds seed/spawn loss). Build rc=0, boot ERR=0; idle re-run confirmed no WARN lines.

- **Block-breaking drop + game-mode/status-aware breaking (`src/network/handlers/play/WorldHandler.cpp::handleDigging`)**: broken blocks now yield items straight into the inventory. Root cause of "nothing drops": the handler cleared the cell on `status==0 || status==2`, so in survival the Start packet (status 0) destroyed the block before Finished (status 2) arrived — later reads saw Air and dropped nothing (confirmed by the always-on `[WIRE Digging getBlockAt]` WARN logging rawCombined+oldBlockId for every status; oldBlockId=2 at status 0 but 0 at status 1/2). New gate reads `session.gamemode` (0=survival,1=creative,2=adventure,3=spectator): survival/adventure destroy **only on status 2**; creative destroys instantly on status 0 (and honours a lone 2); spectator never mutates/drops/broadcasts. A single `canBreakNow` predicate now drives the Air write + Block Change (0x23) broadcast + drop, so Start/Cancelled packets no longer clobber the cell mid-sequence. Chunk is pre-generated before reading oldBlockId (idempotent `generateChunk`) so fresh-chunk digs read the real block instead of Air; `[WIRE Digging FINISHED-BUT-AIR]` WARN surfaces genuine client↔server terrain mismatch. Mining-tier gate added (`toolTier()`/`blockRequiredTier()` over LWMC ids 80..88 tools / ~25 blocks) — wrong or too-weak tool drops nothing per vanilla rule (logged `[WIRE Digging NO-DROP]`). Drop table `blockDropItem()` kept in the **LWMC item-id space** (`MC18ItemIds.hpp`, e.g. STONE→COBBLE, GRASS→DIRT, else self); the guide's numeric ids 270/274/… do not exist here. Ore-XP and tool-wear guards switched from `status==2` to reachability (only actual breaks count) so creative-instant wears/awards correctly. Retained WARNs: `[WIRE Digging C2S]`, `[WIRE BlockChange 0x23]`, `[WIRE SpawnItem …payloadHex]`, `[WIRE InvAdd 0x30]`. Silk-Touch/enchant interception NOT implemented (no server-side NBT enchant model) — flagged follow-up. Build tail rc=0, boot ERR=0; wire probes gitignored and removed from working tree.

- **Chest / container window item deposit + out-of-range slot fix (`src/network/handlers/play/InventoryHandler.cpp::handleClickWindow`, windowId≠0)**: placing an item *into* a chest now works (previously the whole container `mode==0` path was grab-only, so deposits hit the grab branch, claim-mismatched on the empty target slot and replied `SetSlot(clear) + Transaction(accepted=false)` → "item vanishes when put into inventory"). MC 1.8 protocol-47 sends the new item directly on the wire (`btn=0 hasItem=T`, e.g. `item=18 cnt=8`), not via a server-side cursor — added a **wire-driven PLACE** sub-branch that deposits when the wire item differs from the slot's current content, kept the cursor-deposit path for completeness, and kept grab (wire item == slot content) taking it out into the player inventory. Separately, the client addresses chest cells with its own UI indices that can exceed the advertised 27-slot vector (logs show `slot=49`/`61`), which the old bounds guard silently rejected → writes landed nowhere and items looked "stuck in their old slot / count+1". Now grow `win.items` on demand to cover the requested index (capped at 96) so deposits persist where the client expects; S2C Window Items uses `win.items.size()` so both sides stay consistent. Build tail rc=0, boot ERR=0. NOT runtime-proven for a real deposit: no chest exists near the probe client's spawn (`chest windows: []` in this test world), so validated by code + compile + reasoning against captured logs only. Chest *block* not surviving reconnect is a separate chunk-streaming issue (advance_map_gen pending), not addressed here.

- **Phase 14d survival-hunger depletion hooks + optional gamerule knobs (corrected model)**: `PlayerSession::tickRegen` consults optional GameRules int keys (`regen-delay-ticks`, default 100 ticks * 20 ms = vanilla-1.8 ~5 s no-damage floor; and keeps only HP half-heart pacing (+0.5) — saturation drain no longer occurs during heals, eliminating observed drift “lose ~3 exhaustion whenever +1 HP” reports). Ownership of depletion was a two-round fix: first pass wrongly put the *whole* pipeline inside `handlePlayerMovement` (saturation/exhaustion then only checked while moving); final design splits concerns — movement hook is exhaustion-only accrual into `foodExhaustionLevel` (scaled by motion class + hidden `hunger-decay-rate-percent`, capped at 40), and a new **`PlayerSession::tickSurvivalDrain(nowMs)`** runs on every fixed 20 Hz tick from `NetworkManager::tickRegeneration` **before the natural-health-regen gamerule gate** for live survival PLAY sessions. Drain converts pending whole exhaustion units into `foodSaturationLevel` loss and fires starvation (drop one `foodLevel`, snap saturation to 20.0f) so standing still also bleeds saturation via `lastHungerTickMs` idle wall-clock cadence — not fake movement through the handler, not regen-coupled drift. Creative sessions exempt. Still best-effort pacing since C2S Position packets expose wire deltas not per-step motion counters — no fake byte-exact 1.8 step-accounting claim (gotcha rule). Server.cpp `/save-all` console lambdas switched off steady_clock epochs onto system_clock ms so NBT sidecar modified stamps + save cadence share the same wall-clock basis as damage/respawn/regen gates. Build tail link rc=0; boot grep 1 ERR line is environmental port-bind clash from a still-running daemon, not new regressions; /damage + /kill registrations intact in scratch log.
- **Phase 13 vanilla auth residuals + server-browser completion**: `NetworkManager::denySessionTokenOnFd` now emits S2C InvalidSessionToken wire 0x04 with reason string payload (distinct from CannotConnect 0x03), and LoginHandler routes token-era auth failures to it when the received access token is empty while keeping a full verify-chain outage on CannotConnect. Server-browser GET ping detection widened in `NetworkEvents.cpp::handleClientData` + `ServerBrowser.cpp::handleFromBuffer` (case-insensitive "GET" verb + any '/' in first 64 bytes, active during HANDSHAKE or PLAY states) so listing bots that ping bare paths ("GET / HTTP/1.0") are answered; the status datagram builder gained an optional `&icon=<base64>` suffix sourced from new [auth] `server-icon` config key (canonical default emitted empty via forEachCanonicalDefault, so servers without an icon keep byte-identical pings).
- **Per-plane inventory policy** (`world_registry` `inventory_policy` + `force_gamemode` columns, new `player_inventories` SQLite table). SYNC (shared blob, default) / SEPARATE (per-plane blob) / RESTRICTED (one-way). `force_gamemode` pins a plane's game mode on entry. See wiki "Plane inventory policy".
- **`Storage::Database` plane-scoped inventory API** (`setWorldInventoryPolicy`/`getWorldInventoryPolicy`, `setWorldForceGameMode`/`getWorldForceGameMode`, `initPlayerInventoriesTable`, `savePlayerInventory`, `loadPlayerInventory`, `loadSyncInventoryBlob`).
- **`WorldManager` policy mirrors** (`m_inventoryPolicy`, `m_forceGameModeOnEntry`) seeded at boot from `[world] aether-inventory-policy` / `[world] aether-force-gamemode`; policy-aware transfer in `setPlayerWorldAt`, restore in `loadPlayerData`, save in `savePlayerState`.
- **`server.properties` auto-backfill** for the two new `[world]` keys via `ConfigManager::forEachCanonicalDefault` (covers fresh + legacy-schema files).

- **Difficulty byte wired to config** (`src/managers/PlayerManager.cpp`, `src/network/NetworkWorlds.cpp`, `include/core/ConfigManager.hpp`): canonical `[gameplay] difficulty` key (default 1 Easy; 0 peaceful, 2 normal, 3 hard) auto-backfilled via `forEachCanonicalDefault`; JoinGame (0x01) byte and the Respawn (0x07) difficulty byte in the plane-swap flow now read `ConfigManager::getInstance().get<int>("gameplay","difficulty",1)` clamped 0–3 instead of hardcoded literals, so `/difficulty <mode>` updates the same key both spawn-info packets consult. Verified via boot log (no `ERR]` regressions) + incremental build (`make lightweight_mc`).

- **C2S Look / Position_Rotation yaw+pitch wire-decode fix (`src/network/handlers/play/PlayerHandler.cpp:392` + `src/network/Packet.cpp` + `include/network/Packet.hpp`)**: `handlePlayerMovement` used `Packet::readDoubleBounded` (8 bytes) for the yaw / pitch pair. LWMC's own wire format (the same shape `BaseProtocol::encodePosition` writes via `Packet::writeFloat`, mirrored on the S2C Position 0x08 that `NetworkGameplay.cpp:190` and `PlayerManager.cpp:122` emit on teleport / respawn) encodes yaw and pitch as two big-endian **4-byte IEEE-754 floats**. Reading them as one 8-byte double concatenates the two float bit patterns into one huge IEEE-754 double, so any C2S Look / Position_Rotation arriving with a float-shaped yaw/pitch pair persisted `session.yaw` (and `session.pitch`) as a garbage huge number back into `server_data.db`'s `last_yaw` (and `player_positions.yaw`) on logout, which then flowed into every subsequent login / respawn / plane-switch (`NetworkWorlds.cpp:1006/1027/1070`, `setPlayerWorldAt`). Observed on the sole surviving player `ddrmax`: top-level `server_data.db` stored `last_yaw = -851456462684160.0` (bytes `c3 08 33 29 40 00 00 00` = float pair `(-136.2, 2.0)`) and `build1/server_data.db` stored `1688849860263936.0` (bytes `43 18 00 00 00 00 00 00` = float pair `(152.0, 0.0)`) — both are exactly the byte-for-byte concatenation of two floats. Fix: added `Packet::readFloat` (unbounded) + `Packet::readFloatBounded` (bounds-checked, symmetric with `writeFloat`) in `src/network/Packet.cpp` and `include/network/Packet.hpp`, and swapped both yaw / pitch reads in `handlePlayerMovement` to `readFloatBounded`. Verified boot: `chest_test.py` exit 0, `spawn_probe.py` exit 0, boot log `ERR=0`, no new WARN regression.

- **Yaw / pitch sanity guard on login load (`src/player/PlayerSession.cpp:loadFrom` + `src/managers/PlayerManager.cpp:sendPlayPackets`)**: even after the wire-decode fix above, **legacy DB rows** that persisted a corrupt `last_yaw` (or `player_positions.yaw`) from a pre-fix session still flow through the login burst. Both login-load paths now run `lastYaw` / `lastPitch` through a `saneAngle` lambda that rejects non-finite values (NaN / ±Inf) and any angle outside `[-4π, 4π]`, forcing them to 0.0 on read. This keeps `session.yaw` / `session.pitch` legal for scoreboard / plane-switch / `NetworkGameplay.cpp:190` (`Packet::writeFloat(pos, static_cast<float>(ses.yaw))`) S2C Position 0x08 wire regardless of whether the pre-fix corrupt row has rolled forward yet; on the very next logout, `applyTo` writes the sanitised value back into SQLite, so the DB self-heals one login cycle after upgrading. One-off sanitisation of the currently-corrupt rows applied out-of-band via `sqlite3` (`UPDATE players SET last_yaw=0.0 WHERE ...` + matching `UPDATE player_positions SET yaw=0.0 WHERE ...`) for `server_data.db` + `build1/server_data.db` — `ddrmax` yaw reset from `-8.5e14` / `1.688e15` to `0.0`. Verified: `PRAGMA integrity_check` returned `ok` on all 9 top-level + 9 `build1/` DB files (SQLite structural integrity was never at fault — the corruption was purely a decoded-value bug, not SQLite frame damage), and 4 login reconnects on the fresh-booted server (chest_test.py ×3 sockets + spawn_probe.py ×1) delivered 0 `[ERR]` boot-log entries with no new broken-pipe flood. **Not-yet-addressed (documented):** login-burst chunk-stream cost on a warm reconnect still takes ~1.5–1.6s per login and trips 2× `Server can-t keep up` WARNs per login (tick accumulator catches up after synchronous chunk-stream sendMapChunkBulk at 49 chunks rd=3). This is orthogonal to the yaw / pitch corruption chain; login still succeeds and chest / spawn probes still PASS. Any further login-crash pressure on vanilla client is on the login-burst timing, not the DB / wire path.

### Appendix — SQLite memory & chunk bulk allocation (unreleased)
- `WorldStorage::init()`: `PRAGMA cache_size = -32;` (~32 KB SQLite page cache).
- `WorldStorage::releaseMemory()`: calls `sqlite3_db_release_memory(m_db)` then `sqlite3_release_memory(1024 * 1024)`.
- `NetworkEvents.cpp` disconnect path now calls `getWorldStorage(0).releaseMemory()` instead of direct `sqlite3_release_memory(1024 * 1024)`, retains `malloc_trim(0)`.
- `NetworkEvents.cpp`: m_clients.empty() guard retained.
- **XP / ExperienceCommand verified end-to-end**: `include/commands/PlayerCommands.hpp::ExperienceCommand` (alias `/xp`, add|remove|query|set verbs, implicit-add fallback for bare `<target> <amount>`), console + threaded-console + PlayerHandler `grantXpToPlayer` hooks → `NetworkManager::grantPlayerXp` (XP Store totals persisted via `Database::savePlay(db, session)` columns `experienceLevel/experienceProgress/totalExperience`), S2C Experience 0x1f level-up emission gated on the `show_advancement_messages` gamerule. No new XP work shipped this commit — audit only; the path was already live from the Phase 6 backends hotfix.

- **`server.properties` auto-backfill** for the two new `[world]` keys via `ConfigManager::forEachCanonicalDefault` (covers fresh + legacy-schema files).

- **Difficulty byte wired to config** (`src/managers/PlayerManager.cpp`, `src/network/NetworkWorlds.cpp`, `include/core/ConfigManager.hpp`): canonical `[gameplay] difficulty` key (default 1 Easy; 0 peaceful, 2 normal, 3 hard) auto-backfilled via `forEachCanonicalDefault`; JoinGame (0x01) byte and the Respawn (0x07) difficulty byte in the plane-swap flow now read `ConfigManager::getInstance().get<int>("gameplay","difficulty",1)` clamped 0–3 instead of hardcoded literals, so `/difficulty <mode>` updates the same key both spawn-info packets consult. Verified via boot log (no `ERR]` regressions) + incremental build (`make lightweight_mc`).

- **XP / ExperienceCommand verified end-to-end**: `include/commands/PlayerCommands.hpp::ExperienceCommand` (alias `/xp`, add|remove|query|set verbs, implicit-add fallback for bare `<target> <amount>`), console + threaded-console + PlayerHandler `grantXpToPlayer` hooks → `NetworkManager::grantPlayerXp` (XP Store totals persisted via `Database::savePlay(db, session)` columns `experienceLevel/experienceProgress/totalExperience`), S2C Experience 0x1f level-up emission gated on the `show_advancement_messages` gamerule. No new XP work shipped this commit — audit only; the path was already live from the Phase 6 backends hotfix.
- **Deferred disconnect teardown + idempotent tombstones** (`src/network/NetworkEvents.cpp`, `include/network/NetworkManager.hpp`): `handleDisconnect(fd)` no longer closes or erases the session — it flips `ClientState::DISCONNECTED`, stamps a steady-clock `disconnectedAtMs`, saves player state once (repeat calls for the same fd short-circuit with a DEBUG tombstone line instead of re-saving), drops only the epoll registration, and logs queueing at DEBUG. `flushPendingDisconnects(nowMs)` sweeps those tombstones right after the epoll loop (and in `~NetworkManager`, whose body is now explicit instead of defaulted so queued clients still get their save + deferred `::close()` even when `stop()/terminate()` tears the server down), erases `m_clients` and emits the single INFO "Client … disconnected. [IP: …]" line from there — keeping reads/writes on the socket alive through packet sites that were already queued in the same tick while making double-kick/reconnect races non-destructive.
- **Phase 14 movement validation + fall damage (0x08 Player Position):** `PlayerSession` gained transient anchors (`lastMoveX/Y/Z`, `lastMoveMs`) seeded on login bursts and reset via `resetVolatileState()`; `handlePlayerMovement` now validates every C2S delta against a vanilla walk/sprint budget pulled from `GameRules` ints (`walk-speed-bps`, `sprint-speed-bps`) with elapsed-ms scaling, clamps over-distance jumps back onto a legal vector while logging `[WARN] noclip/speed exploit …`, keeps fallDistance accumulating for airborne drops via the onGround flag and pushes the thresholded hit through `NetworkManager::applyDamageToPlayer(…, damage code 1)`. Knockback impulse stays pending in `EntityManager::tick` (EntityRecord persistence untouched). Build rc=0; boot log registered /respawn alias intact, zero ERR lines.

- **Phase 14 respawn + hunger policy**: JoinGame (0x01) & Respawn (0x07) difficulty bytes read the canonical `[gameplay] difficulty` key; `PlayerSession::respawn()` always restores survival food on death — full `foodLevel`, `foodSaturationLevel = 5.0f`, zeroed `foodExhaustionLevel` — independent of `keep_inventory` (XP still never drops per vanilla softcore; gamerule governs items, not hunger refill); `NetworkManager::autoRespawnDuePlayers(nowMs)` sweeps dead PLAY sessions each 20 Hz tick gated by `immediate_respawn` / `respawn_radius` / `respawnwaves` from `World::GameRules` and emits the same 0x08 Position + 0x06 UpdateHealth burst as an explicit force-respawn (hooked in Server.cpp right after `tickRegeneration`); `/respawn [targets…]` with alias `/r` registers in EntityCommands via `ctx.respawnPlayer` with `affectsPlayerState()` broadcast wiring. Villager trade windows / wolf-ocelot taming aren't modelled server-side, so nothing extra needs resetting (matches Minecraft respawn semantics where NPC relationships persist). Build rc=0; boot log 0 ERR lines + registered /respawn (/r); probe runner: chest/spawn/bot exit=0, reorg/packet container-click desync failures pre-existed this phase and remain under investigation.

- **Phase 14 respawn hunger correction + slot-bar eating hook**: Death/respawn unconditionally refills survival food per vanilla softcore pacing: `foodLevel = 20`, `foodSaturationLevel = 5.0f`, `foodExhaustionLevel = 0` now live in `PlayerSession::restoreHungerForRespawn()` — called from both `onDeath()` and `respawn()` (previously gated behind `keep_inventory=false`). Maintainer repro (`/kill` or enough `/damage`) previously left clients pinned to starving bars until autoRespawnDuePlayers finally respawned; broadcasting Update Health (0x06) right after onDeath now carries the restored numbers. Eating is wired server-side on the Block Placement (C2S 0x08 PLACE_BLOCK) face==255 right-click sentinel (dummy -1,-1,-1 position = UI/HUD intent only, never world mutation): that branch consumes one unit off the selected hotbar stack (`PlayerSession::consumeSelectedFood`) using vanilla-1.8 nutrition table coarse values for apples/bread/meat/fish, updates saturation clamped by food level, and re-sends Update Health so slot-bar state moves without waiting on client equip packets (keep_inventory remains orthogonal, governing XP/item persistence elsewhere). Correction: an earlier revision mapped the hook to Animation packet 0x0A arm swings — that never fired for eating in maintainer repro; the hook now lives only in the Block Placement face==255 branch and `consumeSelectedFood` decrements the stack once (duplicate-decrement bug fixed). Build rc=0 link; boot ERR count stayed 1 environmental port-bind clash vs a running daemon, no new regressions.

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
- **Natural health regeneration sweep on the fixed 20 Hz tick loop** (`NetworkManager::tickRegeneration(int64_t nowMs)`): iterates live PLAY sessions, defers the vanilla `natural_health_regeneration` gamerule gate to a `GameRules::getBool` read once per pass, and lets each session run `PlayerSession::tickRegen(nowMs)` (5 s no-damage delay, foodLevel ≥ 9 + saturation > 0, half-heart every 2 s — see the state machine already living in `src/player/PlayerSession.cpp`). Every healed session gets a fresh S2C Update Health (`0x06`) written with the same big-endian `writeUpdateHealthPayload` helper used by the damage path so idle players get their HP bar back without waiting on the next damage packet. Wired from the accumulator loop in `Server::start()` right after `m_chunkManager.tickActiveChunks(nowMs)` — no second timer.
- **Console `/damage` + `/kill` timestamp wiring fix (surfaced by the Phase-14c natural-regen sweep):** `Server.cpp`'s two console contexts passed `damageTypeCode` into NetworkManager's `nowMs` parameter and hard-coded `0` for `/kill`, so every hit/death recorded `lastHurtMs`/`respawnAtMs` near the epoch; `tickRegen()` then saw a gigantic elapsed window and healed hurt players back toward 20 HP on almost every tick (WARN traces like `hpBefore=1 hpAfter=20`) instead of honoring vanilla's 5 s no-damage delay, and consecutive `/damage` presses lost their death transitions because health recovered between commands. Both sites now derive the same wall-clock ms basis as the fixed-20 Hz loop (`std::chrono::system_clock::now().time_since_epoch()` — not `steady_clock`, whose epoch is process-start and would again desync damage stamps against tickRegeneration/autoRespawnDuePlayers callers) into NetworkManager's `nowMs` slot while keeping damageTypeCode as its own argument. Food/locomotion stays vanilla-plausible: 1.8 has no walking/sprinting depletion of `foodLevel` or `foodSaturationLevel`, so regen keeps the existing foodLevel≥9 + saturation>0 gate and `foodExhaustionLevel` defaults untouched (custom per-step exhaustion would be a deliberate future hook from movement distance, not real-mode drain).
- **Held-item melee damage + knockback for mob UseEntity attacks** (`src/network/handlers/play/EntityHandler.cpp` `heldCombatFor(itemId)` map, `EntityManager::applyPlayerAttack(..., meleeDamage, knockbackStrength)`): the attacker's currently selected hotbar slot (`PlayerSession::selectedSlot`, hotbar slots live at `session.inventory[36..44]` exactly like `InventoryHandler::handleHeldItemChange` reads them) resolves through a vanilla-1.8 half-heart table (bare fist=1, sword 77=4 / dagger 78=3 / axe 83=6 / hoe-shovel-fork family ≈2 ...) and a four-bucket knockback enum (`EntityManager.hpp` `KnockbackStrength` NONE/WEAK/NORMAL/RANGED — strength buckets follow the held item TYPE rather than raw damage, cross-cited against the minecraft wiki Melee-combat rows + java mcsources-1.8 CombatHandler templates). The resolved pair now feeds the XP-on-kill + mob-drop path instead of the stub's flat 3 half-hearts; `EntityRecord` carries `knockbackStrength`/`knockbackMs` in-memory only (not persisted to SQLite yet — a knock impulse applied to next-tick velocity is still TODO, see "PvP/PvE hit detection and knockback").
- **Native plugin loader (Phase 11, path A "hybrid now" pick)** (`include/plugins/PluginManager.hpp`, `src/plugins/PluginLoader.cpp`): boot-time scan of `./plugins/*.so` via `dlopen` (RTLD_GLOBAL|RTLD_NOW) + bookkeeping singleton (`loadRegisteredPlugins()` called from `Server::start()` right after the whitelist block, logs `Registered plugin: NAME (path)`); runtime `/plugins load <so-path>` / `/plugins unload <name>` (best-effort `dlclose`) and `describeLoaded()` backing `/plugins [list]`. Roadmap decision recorded in phases.md §Phase 11: maintainer chose hybrid — loader + ABI bookkeeping ship now, the optional hook entry points (`on_join`/`on_chat`/`on_tick`/`on_command`, looked up but not yet invoked) and the embedded-Lua half (path B) land with Phase 30's dual extension engine.

- **`/plugins` command de-stubbed** (`include/commands/ServerCommands.hpp`): `/plugins [list|load <so-path>|unload <name>]` (alias `/Pl`, op level 0 unchanged) now backed by the loader singleton instead of replying `Plugins (0):`; boot log keeps the `Registered command: /plugins` + `Registered alias: /Pl -> /plugins` lines. No new wire packets touched — this is console-side state only, so no protocol-seed cross-check was needed for this bullet.

### Notes

- Existing 0.0.14 `server_data.db` keeps working via CREATE/ALTER migrations; vanilla planes stay on SYNC/INHERIT so the 0.0.14 4-packet plane-switch wire sequence is unchanged.
- All 5 regression tests (chest/reorg/probe/spawn/bot) pass at exit=0 with zero `ERR]` boot-log messages.
- Roadmap and phase tracking live in `phases.md`; full design detail is on the wiki.
- **Version reconciliation:** an earlier `[0.0.16] - 2026-10-18` section was withdrawn into this entry — release marker `LWMC_VER` in `include/core/utils.hpp` stays at `"0.0.15-alpha"` (single source of truth); the regeneration-sweep, held-melee and protocol-source bullets above belong to 0.0.15, not to a bump.

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
- **Ban backends + vanilla sidecars** (`src/network/BanList.cpp`): `/ban`, `/ban-ip`, `/pardon`, `/pardon-ip` persist `banned-players.json` + `banned-ips.json` (rewrite-on-change, boot import) instead of the earlier draft's `server_data.db` tables.
- **Kick on the real Disconnect wire** (`NetworkManager::kickSessionByName`, NetworkEvents.cpp): protocol 47 sends Disconnect as S2C wire 0x00 whose payload is a JSON reason object (verified in include/network/protocol/PacketCodec47.hpp + tools/protocol_seed/packet_ids_1.8.json; the phases.md draft value `0x40` was a misread), then closes the socket via handleDisconnect().
- **Login gate + whitelist** (`src/network/handlers/LoginHandler.cpp`, `NetworkEvents::allowLoginGate/denyLoginOnFd`): bans always deny at Login Start; `[security] whitelist-enabled` (default off) + `whitelist.json` restrict logins when on; denials reply CannotConnect 0x03 then close. `/whitelist <on|off|add|remove>` shares this state with console + chat contexts.
- **PlayerSession::peerIp** captured at socket-accept in NetworkEvents.cpp for per-session IP matching against banned-ips.json entries.
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