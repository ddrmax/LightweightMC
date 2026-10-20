# Troubleshooting

Every diagnostic string below is emitted by `Core::Logger::info / warn / error` at the
line references given, so you can grep for it verbatim. The wiki is organised by
**failure mode**: what you see, what causes it, and where the code that prints it lives.

## Startup refusals

`Server::start()` (see [Installation](Installation#running-the-server)) logs a fixed banner
and then calls `NetworkManager::start(port)` in
`src/network/NetworkSocket.cpp`. That function contains several early `return false`
paths; each one prints one line and the process exits. Nothing is listening on the
port — the console thread already logged the config banner, so the failure is always
the *last* thing printed before exit.

| Last line printed before exit | Where | Cause / fix |
| --- | --- | --- |
| `Failed to initialize SQLite player database.` | `NetworkSocket.cpp:81` | `server_data.db` cannot be opened. Usually the file exists but is owned by a different uid or the filesystem is read-only. `chown` the SQLite files, or start the server from the tree that owns them. |
| `Failed to initialize multiworld registry.` | `NetworkSocket.cpp:91` | `Database::initWorldRegistry()` returned false — the `world_registry` table exists but `lookupWorld` / `lookupWorldById` / `setWorldById` SQL failed to prepare. Usually a stale DB left over from a build that predates multiworld; delete `server_data.db` and re-generate (you lose characters). |
| `Failed to register standard world row in world_registry` | `NetworkSocket.cpp:115` | `Database::setWorldById` failed for one of the four standard planes (Overworld 0 / Nether −1 / End 1 / Aether 100). See [Multiworld & Dimensions](Multiworld-and-Dimensions) — a stale registry with an id collision triggers this. |
| `Overworld (id=0) is disabled; refusing to start because every accessor falls back to it when worldId is unknown or inactive.` | `NetworkSocket.cpp:152` | `overworld-active = false` in `server.properties` (see [Configuration](Configuration)). By design, this is fatal: every accessor path falls back to id 0. Turn it back on. |
| `Failed to initialize SQLite world storage <path>` | `NetworkSocket.cpp:182` | The per-plane world DB (`world.db`, `nether.db`, `end.db`, `aether.db`) cannot be opened. Usually a disk-full or perms issue on the working directory. |
| `Failed to bind socket to port.` | `NetworkSocket.cpp:212` | `::bind()` on `m_serverFd` returned −1. Most common cause: another process (an earlier `lightweight_mc`, a shell `nc`, or something unrelated) already holds the port. Kill it, or bump `[server] port` in `server.properties`. |
| `epoll_wait failed: <strerror(errno)>` | `NetworkEvents.cpp:256` | Not a startup refusal — it is logged inside the running tick loop, and the loop simply returns. If you see this repeatedly the epoll fd is dead and the server is not consuming events any more. Usually `EBADF` after `NetworkManager::stop()` was called while the loop was still spinning; treat it as a stop-the-process situation. |

After a successful start the console should end with:

```
SQLite Database Init...
Starting Network Manager (epoll)...
Server is ready and listening. Type /help for available commands.
```

If the "Server is ready" line is missing, one of the rows above is the cause — scroll
up to the last line printed.

## Handshake rejections & protocol/era issues

Everything below is emitted from `src/network/handlers/HandshakeHandler.cpp`, so you can
grep the string, then read the file. See [Protocol & Clients](Protocol-and-Clients) for
the era table behind these log lines.

A single connect prints up to *three* handshake lines:

```
[NET] Handshake from 192.168.1.20:54123 protocol=47
Handshake from 192.168.1.20:54123 protocol=47 (1.8, era=LEGACY)
```

The suffixes are what tells you something is wrong:

| Log line (verbatim) | Cause | Fix |
| --- | --- | --- |
| `Handshake from … protocol=<n> [UNSUPPORTED ERA - legacy handlers only]` | `EraAdapter::create(session.clientProtocol)` returned `nullptr` — see `src/network/protocol/EraAdapter.cpp`. The session still runs (all handlers fall back to the legacy code path) but 1.8+ clients under this protocol number will not decode chunk-stream / window packets correctly. | Check `ProtocolVersion::eraFor(<n>)` against the generated table in `include/protocol/data/protocol_versions.h`; if it maps to an era for which no adapter exists, add one, or add the row to `tools/protocol_seed/versions.json` and regenerate. |
| `Handshake from … protocol=<n> (unsupported; nearest supported: <version>)` | `ProtocolVersion::resolve(<n>)` returned `nullptr` — the raw protocol number is not a row in the registry. The nearest known row is *only* a hint; the client still talks with `<n>`. | Add a `versions.json` row for the version, then run `tools/gen_protocol_data.py` (see [Testing → Offline tools](Testing#offline-tools)). |
| `Handshake from … protocol=<n> (<version>, era=<era>)` | Success path — nothing to do. | — |
| Client connects but never moves, no handshake line at all | `nextState=2` (LOGIN) arrived **before** the client's protocol varint was captured, so `session.clientProtocol` is still 0 and `EraAdapter::create(0)` produced nothing. | Read raw bytes with `packet_probe.py` (see [Testing](Testing)). Usually a client that sends `Hello` twice, or a proxy that fragments the frame. |

Note the deliberate mismatch documented on [Protocol & Clients](Protocol-and-Clients#registry-coverage):
`BaseProtocol::protocolVersion()` returns hard-coded 47 / 491 / 769 for LEGACY/MODERN/LATEST,
while `advertisedProtocol()` reports the *registry-canonical* 47 / 766 / 769. Do not
compare a session's `clientProtocol` against `protocolVersion()` — always go through
`eraFor()` or `resolve()`.

## Frame size and stream corruption

| Log line | Where | Cause / fix |
| --- | --- | --- |
| `sendPacket: client <fd> packet id=<n> body=<N> bytes exceeds 21-bit wire length limit, dropping.` | `src/network/NetworkFraming.cpp:103` | The semantic packet's body (varint-header + payload) grew past 2 097 151 bytes. 1.8's varint length field caps at 21 bits, so the frame would be truncated on the wire. Either the packet is a legitimate big `MapChunkBulk` — enable the chunk-stream throttle in `Server.cpp::start()` (5 Hz flush, described on [Installation](Installation#running-the-server)) — or a source-side bug forgot to split. |
| `Client <fd> rxBuffer exceeded cap (<N> bytes), dropping client.` | `src/network/NetworkEvents.cpp:125` | The client sent a stream that grew past `kMaxRxBufferBytes = 4 MiB` before any valid frame boundary was found. The client is force-disconnected. Almost always a spam/malformed-stream client or a proxy that fragmented a big login burst. Kill the client or fix the client's framing. |
| `Client <fd> read()=0, treating as disconnect.` | `src/network/NetworkEvents.cpp:117` | Peer closed the socket. Normal — this is what you see every time a client exits. |
| `send() returned 0 for fd=<fd>, treating as disconnect.` | `src/network/NetworkEvents.cpp:560` | Kernel reports a hangup on the send side. Client vanished mid-frame; the server tears it down. |
| `send() failed for fd=<fd>: <strerror(errno)>, disconnecting.` | `src/network/NetworkEvents.cpp:575` | Real write error (`ECONNRESET`, `ENOTSOCK`, …). The fd is closed. Often paired with a client crash on the other end of a large chunk burst. |
| `[NET RECV] abort fd=<n> — client not in m_clients (skipped read)` | `src/network/NetworkEvents.cpp:80` | Epoll surfaced a socket that was already removed from `m_clients` (double disconnect race). Harmless debug line, not a real failure. |


## Socket setup / epoll-loop errors

These are runtime, not startup — the loop keeps running and the socket error is transient,
but if you see them *continuously* the connection is dead:

| Log line | Where | Notes |
| --- | --- | --- |
| `epoll_wait failed: <strerror(errno)>` | `src/network/NetworkEvents.cpp:256` | `errno != EINTR`; the epoll fd is broken. Check that the process is still the same one that opened it — a `SIGSEGV` handler may have already closed it (see [Installation](Installation#shutdown--signals)). |
| `Failed to bind socket to port.` | `src/network/NetworkSocket.cpp:212` | Fatal — see the startup-refusals section above. |
| `[NET FLUSH] EAGAIN fd=<n> remainingBytes=<k>` (debug) | `src/network/NetworkEvents.cpp:571` | The client's socket is not currently writable; epoll will re-arm it with `EPOLLOUT`. Normal under load. |

`NetworkManager::setNonBlocking(fd)` (`NetworkEvents.cpp:66`) strips the `O_NONBLOCK`
lookup failure silently; if a client socket starts returning `send() failed …
Operation would block` repeatedly, the flag was not set and the epoll thread is stalling.

## Deferred disconnect teardown (`src/network/NetworkEvents.cpp`)

Disconnects are two-stage on purpose. `handleDisconnect(fd)` is the *queueing* half:
it saves the player once, flips the session to `ClientState::DISCONNECTED`, stamps a
steady-clock `disconnectedAtMs` ("queued" marker), removes only the epoll registration,
and logs at **DEBUG** — `[NET DISCONNECT] tombstone queued fd=<n> …` plus
`Client <fd> (<name>) queued for disconnect teardown. [IP: …]`. It deliberately does **not**
`close()` the socket and does not erase `m_clients`, so packet sites that already captured a
reference to the session earlier in the same tick can still resolve their queued writes without
a dangling entry.

`flushPendingDisconnects(nowMs)` is the *teardown* half, invoked right after the epoll event loop
in `pollEvents` (and explicitly in `~NetworkManager`, which is why the destructor body is explicit
now — a stop/terminate must still drain queued saves + closes). It erases every DISCONNECTED handle,
performs the deferred `::close(fd)`, emits one **DEBUG** `[NET DISCONNECT] flushed tombstone fd=<n> …`
diagnostic and the single **INFO** line clients' logs key on:

```
Client <fd> (<username>) disconnected. [IP: <ip>:<port>]
```

Idempotence matters: an RD-HUP event, a `send()` failure and a `/kick` in the same tick can all call
`handleDisconnect(fd)`. The first call does the save; later calls short-circuit with a **DEBUG**
"already queued, skipping re-save" line so player state is never saved twice into the SQLite row and
epoll registration is never double-deleted. If you see two INFO "disconnected" lines for one client
in a run, that's a bug — there should be exactly one, printed by the flush pass.

## AntiScan interference

See [AntiScan & Security](AntiScan-and-Security) for the model. The two strings that mean
"anti-scan is doing something bad right now" are:

| Log line | Where | Fix |
| --- | --- | --- |
| `[AntiScan] Warning: Failed to fetch online list. Using local files.` | `src/network/AntiScan.cpp:74` | `blocklist-url` is not reachable (offline target, DNS blocked, or upstream list moved). Falls back to `antiscan_ips.txt` + `custom_ips.txt`. Set `blocklist-url` to an empty string on offline hardware to silence this on every boot. |
| `[AntiScan] Error: Could not write to <custom_ips.txt>` | `src/network/AntiScan.cpp:115` | `/antiscan block <ip>` failed to persist because the working directory is read-only. Set `[security] enable-antiscan = false` on locked-down deployments, or run the server from a writable directory. |

If a real player is getting dropped, check whether the ip is in `custom_ips.txt` or the
remote blocklist. Use `/antiscan stats` (op level 3) and `/antiscan unblock <ip>` — see
[AntiScan & Security → Command verbs](AntiScan-and-Security#command-verbs).

## Storage / LZ4 errors

| Log line | Where | Meaning |
| --- | --- | --- |
| `Failed to open SQLite database: <sqlite3_errmsg>` | `src/storage/Database.cpp:19`, `src/storage/WorldStorage.cpp:571` | SQLite refused to open one of the world / player / gamerule DBs. Usually read-only file or bad file-system. |
| `Error creating SQLite tables: <errmsg>` | `src/storage/WorldStorage.cpp:594` | The chunk-table CREATE failed. Delete the world DB and re-generate. |
| `LZ4 Compression failed!` | `src/storage/WorldStorage.cpp:520` | A chunk blob could not be compressed on write. Chunk is not persisted; the world will regenerate it. Rare; usually a corrupted generation worker output. |
| `LZ4 Decompression failed!` | `src/storage/WorldStorage.cpp:540` | **The 2026-09-16 breaking change** (see [Home → Version history & breaking changes](Home#version-history--breaking-changes)). Old blobs were stored uncompressed and this codebase has *no* uncompressed fallback reader. There is no in-place migration: regenerate the world, or restore an old engine binary. |

## Multiworld errors

| Log line | Where | Meaning |
| --- | --- | --- |
| `createWorld: refusing to allocate an unnamed world` | `src/network/NetworkWorlds.cpp:151` | `/world create` was called without a name. |
| `createWorld: unable to register world "<name>"` | `src/network/NetworkWorlds.cpp:162` | `Database::registerWorld` failed — usually `world_registry` is corrupt or the custom-id allocator exhausted. |
| `createWorld: unable to open SQLite storage <path>` | `src/network/NetworkWorlds.cpp:197` | The per-plane DB path is un-writable. |
| `setPlayerWorldId: target worldId=<n> …` (warn) | `src/network/NetworkWorlds.cpp:263` | `/gotor` or `/world join` targeted a world that is not currently active. Session stays on the previous plane. |
| `setPlayerWorldId: could not persist worldId for <player>` | `src/network/NetworkWorlds.cpp:282` | The player's `world_id` column write failed; the plane change took effect for the running session but will not survive a reconnect. |

## Command dispatch

| Log line | Where | Cause |
| --- | --- | --- |
| `[CommandManager] Unknown command attempt: <cmd> by <sender>` | `src/core/CommandManager.cpp:68` | Verb is not registered at all. Check [Commands](Commands) — some verbs are registered only in a specific build variant. |
| `[CommandManager] Permission denied for <sender> on command /<cmd>` | `src/core/CommandManager.cpp:77` | Player's op level < `Command::getRequiredOpLevel()` (see [Op Levels](Op-Levels)). Grant with `/op <player> <level>`. |
| `[CommandManager] Exception caught executing /<cmd>: <what>` | `src/core/CommandManager.cpp:92` | A command handler threw. The exception text tells you which storage / SQL / era path failed. |
| `[CommandManager] Unknown exception caught executing /<cmd>` | `src/core/CommandManager.cpp:98` | The exception is not derived from `std::exception` (often a `throw int` or a raw `throw`; not seen in-tree but happens if you patch handlers). |
| `[CommandManager] Overwriting existing command or alias: <name>` (warn) | `src/core/CommandManager.cpp:13` | A `registerCommand` call replaced a previous registration. Usually a duplicate `cmdMgr.registerCommand(...)` in `Server.cpp` — remove one. |
| `[CommandManager] Alias /<alias> conflicts with an existing command or alias.` (warn) | `src/core/CommandManager.cpp:32` | An alias collides; the alias is skipped, the primary still works. Rename in the command header. |


## `/testpackets` — protocol sanity-check

Defined in `include/commands/TestPacketsCommand.hpp`, op level 3, wired into `NetworkManager::start`
(`src/network/NetworkSocket.cpp:229`). Builds the canonical S2C packet set (Join Game, Chat, Keep
Alive, Spawn Position, Block Change, Add Player) through the caller's live `BaseProtocol` and logs
the resolved wire IDs. See [Protocol & Clients → /testpackets](Protocol-and-Clients#testpackets--dumping-the-full-s2c-set).

| What you see | Meaning |
| --- | --- | --- |
| `TestPackets: building S2C packet set for protocol <n> (<versionName>)` | Command found the caller's session and knows which protocol to build for. |
| `TestPackets: <k> packet(s) built and logged. Check server console for wire IDs.` | Six packets were built. Console should have six `[TestPackets] #<i> kind=…` lines (warn for unmapped kind, info for mapped kind — see `TestPacketsCommand.hpp:147`/`152`). |
| Command prints `Usage: /testpackets [player]` and stops | More than one argument was supplied. Drop the extra arg. |
| Line reads `TestPackets: building S2C packet set for protocol 0 (1.7.2)` on a **console** invocation | Expected: the console session has no live session provider (it isn't a client), so `session.clientProtocol` stays 0 and the default `BaseProtocol::create(0)` resolves to LEGACY. For a real check, run `/testpackets <playername>` **from an in-game chat window** — see `TestPacketsCommand.hpp:44`. |
| One of the `[TestPackets] #… kind=… wire=…` lines is a `warn` line | That kind has no era-mapped wire ID in the current registry — the packet was logged as raw (1.8 ID) rather than era-translated. Check `s2cWireIdFromLegacy(era, kind)` in `NetworkFraming.cpp:78` and confirm the registry row. |
| Wire IDs on the console do not match what a wire-sniffer reports | The session provider at `NetworkSocket.cpp:229` returned a **different** session than the one the sniffing client is on — usually the map is keyed by username and the client logged in twice. Re-run with the *exact* username. |

## Known quirks

- **Join Game dimension byte is hard-coded `0`** (`Overworld`) in `src/managers/PlayerManager.cpp`.
  Multiworld sessions that reconnect on a non-Overworld plane still see `0` on the wire — the
  client lands visually in Overworld even though the server-side `PlayerSession` has the
  correct `worldId`. Do not chase this in the network layer; see
  [Multiworld & Dimensions](Multiworld-and-Dimensions) for how the rest of the pipeline copes.

- **`clientProtocol >= 47` gates the 1.8 trailer** on Join Game (Max Players / Level Type / Reduced
  Debug Info), plus the NBT end-of-compound tag in `InventoryHandler.cpp` and the 12 vs 16-bit Y field
  in `WorldHandler.cpp`. Re-ordering that guard desyncs any pre-1.8 client.

- **`BaseProtocol::protocolVersion()` returns 47 / 491 / 769** but the registry-canonical
  `advertisedProtocol()` reports 47 / **766** / 769. Do not compare a session's raw `clientProtocol`
  against `protocolVersion()`; go through `eraFor()` or `resolve()`.

- **Header comments on the era adapters are aspirational.**
  `include/network/protocol/adapters/LegacyProtocol.hpp` says "1.7.x through 1.12.x (protocol 4-340)",
  `ModernProtocol.hpp` says "1.13-1.20.x (protocol 393-769)". The **generated `eraFor()` table** wins —
  if a client version looks routed "wrong", it is the comment that is stale. Report the mismatch so
  the comment can be fixed (see
  [Protocol & Clients → Registry coverage](Protocol-and-Clients#registry-coverage)).

- **`EraAdapter::create` returning `nullptr`** does *not* throw. The session is left with `adapter == nullptr`,
  `sendPacket` skips the wire-ID translation (`NetworkFraming.cpp:76`) and falls through to the 1.8
  IDs; the handshake log line `… [UNSUPPORTED ERA - legacy handlers only]` is your only early warning.
  Check `eraFor(protocol)` returns an era you actually have an adapter for.

- **`rxBuffer` cap is per-session, not per-server.** Ten flooding clients × 4 MiB = 40 MiB of heap in the
  worst case before anyone is dropped. On the tiny-RAM targets in [Installation](Installation#runtime-footprint)
  that matters — do not disable the cap.

- **`sendPacket` drops frames silently for pre-handshake sessions.** A pre-handshake session has
  `session.protocol == nullptr` so the frame falls back to plain varint framing
  (`NetworkFraming.cpp:126`). This is the Server-List-Ping path; if a *client* sees the Server-List-Ping
  bytes on the *game* socket, the client is reusing one socket for both — not a server bug.

- **Chunk-stream throttle is 5 Hz for `clientProtocol >= 47` only.** LEGACY sessions get
  the initial `updateChunksAroundPlayer` burst and nothing after that
  (`NetworkWorlds.cpp`, throttled in `Server.cpp::start()`); a LEGACY client that expects a second
  burst is a client bug, not a server one.

- **`antiscan_ips.txt` and `custom_ips.txt` are read at boot** and merged; a stale `custom_ips.txt`
  with an IP that later turned into a legitimate home network blocks that family forever. Grep the file.


## Cross-links

- [Home](Home) — general overview and the version history that shows when a quirk was introduced.
- [Installation](Installation) — build/link errors and the expected startup banner.
- [Configuration](Configuration) — every key that appears above (`port`, `overworld-active`,
  `enable-antiscan`, `blocklist-url`, `gen-style`, `world-seed`).
- [Commands](Commands) — verbs referenced above (`/testpackets`, `/antiscan`, `/op`, `/stop`,
  `/world`, `/gotor`).
- [Op Levels](Op-Levels) — the permission gate behind `[CommandManager] Permission denied`.
- [Multiworld & Dimensions](Multiworld-and-Dimensions) — where the Join-Game-0 byte and worldRegistry failures live.
- [Storage & Save Files](Storage-and-Save-Files) — SQLite/LZ4 layout that emits `LZ4 Decompression failed!`.
- [Protocol & Clients](Protocol-and-Clients) — the era tables, handshake state machine and what breaks when the era is wrong.
- [AntiScan & Security](AntiScan-and-Security) — the blocklist files and decoy-response path.
- [Testing](Testing) — `packet_probe.py`, `handshake_probe.py`, `chest_test.py` and `run_tests.sh`, plus the protocol-data generator.

