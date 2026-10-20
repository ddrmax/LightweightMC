# Protocol & Clients

LightweightMC speaks the pre-1.13 Java Edition wire protocol with era-aware
adapters layered on top of it. One negotiation path (the `Hello` handshake
inside `HandshakeHandler.cpp`) turns the client's advertised protocol number
into a per-session **era adapter**, a per-session **base protocol framer**,
and a per-session **client protocol integer** (`session.clientProtocol`).
Every subsequent S2C / C2S packet picks its wire encoding off those
three pieces of state.

The full set of recognised protocol numbers lives in
[`tools/protocol_seed/versions.json`](../tools/protocol_seed/versions.json)
and is compiled into `include/protocol/data/protocol_versions.h` by
`tools/gen_protocol_data.py` (see [Testing](Testing#protocol-data-generator)
for the generator path).

## The three eras

`include/protocol/data/protocol_versions.h` exposes an `Era` enum:

```cpp
enum class Era : uint8_t { LEGACY = 0, MODERN = 1, LATEST = 2 };
constexpr uint32_t kEraCount = 3;
```

The seed's `notes` column is what actually distinguishes them (quoted from
`versions.json`):

| Era | Field encoding | Chunk packet shape |
| --- | --- | --- |
| **LEGACY** | Blocks are `id + meta` (1.7-style combined uint16) | One chunk per packet; `0x21 Chunk` (1.8) or the pre-1.8 chunk form |
| **MODERN** | Flattened blockstate arrays; chunks are paletted | `0x26 Map Chunk Bulk` (1.8 / protocol ≥ 47) streams many chunks per packet |
| **LATEST** | Introduces 1.20.5+ field deltas (state IDs / variations, string enums) | Same paletted bulk path as MODERN with 1.20.5+ framing quirks |

`EraAdapter::create` in `src/network/protocol/EraAdapter.cpp` picks an
adapter from the era returned by `ProtocolVersion::eraFor(protocol)`:

```cpp
EraAdapter *EraAdapter::create(int32_t protocol)
{
    switch (ProtocolVersion::eraFor(protocol))
    {
    case Era::LEGACY:  return new LegacyAdapter(...);
    case Era::MODERN:  return new ModernAdapter(...);
    case Era::LATEST:  return new LatestAdapter(...);
    ...
    }
}
```

### Registry coverage

As generated from `versions.json` on 2026-10-10 the registry marks **every
client version from 1.7.2 up to 26.4 as `supported = true`**, and the
era split lands where vanilla field encoding changes:

| Era | Registry rows | Protocol numbers |
| --- | --- | --- |
| LEGACY | 1.7.2, 1.7.10, 1.8 | 4, 5, 47 |
| MODERN | 1.9 through 1.20.4 | 107 → 765 |
| LATEST | 1.20.5 through 26.4 (title-update line) | 766 → 778 |

> **Doc/code mismatch to be aware of:** the *headers* of the three
> protocol classes carry their own era ranges in comments —
> `LegacyProtocol.hpp` says "1.7.x through 1.12.x (protocol 4-340)"
> and `ModernProtocol.hpp` says "1.13-1.20.x (protocol 393-769)". Those
> comments are aspirational; the actual adapter dispatch follows the
> generated `eraFor()` table above. If you hit a bug that looks
> like "my 1.10 client is being routed to MODERN", that is the
> generated table being right and the comment being stale — please
> report it so the comment can be fixed.

## Handshake state machine

`NetworkSocket.cpp::handleClientData` (dispatched via `NetworkEvents.cpp`)
walks each connected socket through a per-session state machine defined
by `PlayerSession::state`. The four states are:

| State | Set by | What the server does next |
| --- | --- | --- |
| `ClientState::HANDSHAKE` | socket creation default | Runs `HandshakeHandler::handle` until the client sends Hello (finished) |
| `ClientState::STATUS` | HandshakeHandler when `nextState == 1` | Waits for `CLIENT_SEND_MAP_DATA` chunk selection |
| `ClientState::LOGIN` | HandshakeHandler when `nextState == 2` | Waits for a Login Start packet (`0x00`) |
| `ClientState::PLAY` | `PlayerManager::sendPlayPackets` | Runs `PlayHandler::handle` (C2S switch) |

Handshake flow in code (`src/network/handlers/HandshakeHandler.cpp`):

1. Read the protocol varint from the Hello body and store it on
   `session.clientProtocol`.
2. Resolve an era with `ProtocolVersion::eraFor(protoVersion)` and
   construct the per-session objects:

   ```cpp
   session.adapter  = EraAdapter::create(session.clientProtocol);
   session.protocol = BaseProtocol::create(session.clientProtocol);
   ```

3. If the protocol number is not in the generated table, the server logs

   ```
   Handshake from <ip:port> protocol=<n> (unsupported; nearest supported: <version>)
   ```

   using `ProtocolVersion::nearestSupported()` and keeps going with the
   closest row's era. A missing adapter additionally emits
   `[UNSUPPORTED ERA - legacy handlers only]` once — further
   warnings for the same client are suppressed by
   `session.unsupportedEraWarned`.

4. Depending on the `nextState` byte in the Hello packet the session
   transitions to `STATUS` or `LOGIN`.

The `session` fields set here drive every other decision in the network
stack; **anything that wants to know "is this a 1.8+ client?" reads
`session.clientProtocol >= 47`, not the era**.

## Era adapter — field translation, not framing

`class EraAdapter` (in `include/network/protocol/adapters/EraAdapter.hpp`)
defines the *per-era* semantics:

- `encodeBlock` / `decodeBlock` — LEGACY = `(id << 4) | meta`, MODERN
  translates flattened state IDs into internal id/meta, LATEST adds the
  1.20.5+ variation prefix.
- `encodeItem` / `decodeItem` — same idea for item stacks.
- `encodeChunkSection` — writes 256 blocks (or 512 for MODERN/LATEST)
  plus a 128-entry palette run-length table for MODERN/LATEST.
- `advertisedProtocol()` — the protocol number the adapter will put
  into the Hello response when the client has not yet negotiated one.
  Values in the code today are **LEGACY = 47** ("1.8 baseline"), **MODERN
  = 766**, **LATEST = 769**.
- `era()` — returns the `Era` enum the adapter represents.

The generated `packet_ids.h` (`include/protocol/data/packet_ids.h`)
holds the per-era S2C/C2S ID arrays and the reverse lookups used by
`PacketIdHandler.hpp`:

```cpp
s2cPacketId(era, kind);            // semantic kind → wire id
c2sPacketKind(era, wireId);        // wire id → semantic kind
s2cWireIdFromLegacy(era, legacy);  // 1.8 id → this era's id
```

`ProtocolTranslator` (in `include/network/protocol/ProtocolTranslator.hpp`)
sits on top of both adapters. It exposes the "server world → what the
client wants" helpers:

```cpp
translateBlockToClient(clientProtocol, id, meta);
translateItemToClient(clientProtocol, id, count, damage);
translateChunkSection(clientProtocol, ...);
```

All of these are called from `WorldManager`, `PlayerManager`, and the
play handlers; the C2S side (`InventoryHandler.cpp`, `WorldHandler.cpp`)
uses the mirrored `translateBlockFromClient` / `translateItemFromClient`.

## Framing layer — `BaseProtocol`

`class BaseProtocol` (in `include/network/protocol/BaseProtocol.hpp`) is
the *transport* abstraction:

- `protocolVersion()` — the protocol number this framer was created with
  (LEGACY returns **47**, MODERN returns **491**, LATEST returns **769** —
  these are hard-coded in the class methods, not the `advertisedProtocol()`
  values).
- `s2cPacketId(kind)` — looks up the wire id for a semantic `PacketKind`
  via `packet_ids.h`.
- `c2sPacketKind(rawId)` — reverse lookup on incoming bytes.
- `encodeFrame(body)` — produces the full wire bytes for one packet
  (`varint(len) ‖ varint(wireId) ‖ body` for unencrypted sessions; RPC
  XOR for the "Login" RPC-era framer if you flip it on).

The 1.8 wire framing is byte-identical across MODERN and LATEST; only
the *content* changes, so `BaseProtocol::encodeFrame` for MODERN is
"the same 1.8 bytes as LEGACY, different field semantics".


## The send path — `NetworkManager::sendPacket`

Defined in `src/network/NetworkFraming.cpp`. Every server module
eventually calls this helper. It:

1. Looks up the client's `PlayerSession` from `clients` (an
   `unordered_map<int, PlayerSession>`).
2. Translates the semantic packet ID to a wire ID using
   `session.adapter->s2cPacketId(packetId)` when an adapter is set.
3. Builds `varint(wireId) ‖ payload` and hands it to
   `session.protocol->encodeFrame(body)`. If `session.protocol` is
   still null (e.g. pre-handshake packets) it falls back to plain
   1.8 framing.
4. Prefixes the encoded frame with a varint **total length**.
5. Refuses to send if the body exceeds `MAX_FRAME_BODY_BYTES = 2_097_151`
   (a 21-bit varint is the widest prefix the reader accepts). Anything
   bigger is logged and dropped:

   ```
   sendPacket: client <fd> packet <id> body=<n> bytes, dropped (exceeds 21-bit wire length)
   ```

6. Writes the framed bytes to the epoll-registered socket.

`broadcastPacket(packetId, payload, exceptFd)` iterates all `PLAY`
sessions and calls `sendPacket` for each.

## The receive path — chunked reassembly

Defined in `src/network/NetworkEvents.cpp::handleClientData` (server-browser GET-ping interception for listing bots lives above its frame loop, and the token-era reject helper is `denySessionTokenOnFd`, both added in Phase 13):

- Each client has a growable `rxBuffer` (per-session), capped at
  `kMaxRxBufferBytes = 4 MiB`. Buffers over that cap are dropped with a
  warning; the client will usually kick on the other side.
- Reads a varint length prefix (up to 3 bytes, max 21 bits — same
  limit as the send side).
- Reads the wire packet ID varint, decodes it via `PacketIdHandler`
  against the session's era.
- Dispatches to the right handler based on `session.state`.

Consequence: a client that sends packets larger than 2 MiB will get
its frames silently dropped, and the *server* rx buffer will drop
incoming packets over 4 MiB. Chunk streaming is deliberately throttled
in `Server.cpp::start()` (see the "chunk stream throttle" comment)
so this is only hit by pathological payloads.

## Chunk streaming — two wire paths

`WorldManager::updateChunksAroundPlayer` (`src/managers/WorldManager.cpp`)
branches on `session.clientProtocol < 47`:

| Path | Trigger | Wire packet |
| --- | --- | --- |
| Legacy (< 1.8, protocol < 47) | Any PLAY session | One `0x21 Chunk` packet per chunk in the render box; block bytes are `id + meta` |
| 1.8+ (protocol ≥ 47) | Any PLAY session | A single `0x26 Map Chunk Bulk` carrying every ready chunk in the render box, with `sectionBitmask:short` + concatenated section payloads |

`NetworkWorlds.cpp::tickChunkStreaming(renderDistance)` re-runs
`sendMapChunkBulk` against every PLAY session with
`clientProtocol >= 47`, throttled to 5 Hz inside the main tick loop in
`Server.cpp::start()` (every 4th 50 ms tick) so it does not compete
with world generation. It is **skipped for LEGACY sessions** — legacy
clients only get the initial `updateChunksAroundPlayer` burst and
rely on their own client-side rescan.

Full wire layout comments are inline in `WorldManager.cpp`; see
[Storage & Save Files](Storage-and-Save-Files) for the chunk-level
encoding details.


## Play packets — the C2S switch

`PlayHandler::handle` (`src/network/handlers/play/PlayHandler.cpp`)
dispatches on the wire packet ID as decoded by
`PacketIdHandler::getC2SPacketKind`. The 1.8 (protocol 47) IDs in code are:

| Wire id | Kind | Handler |
| --- | --- | --- |
| `0x00` | Keep Alive | `PlayHandler` (noop — refresh last-seen) |
| `0x01` | Chat Message | `ChatHandler` |
| `0x02` | Use Entity (attack / interact) | `EntityHandler` |
| `0x03` | Flying (airborne snapshot) | `PlayerHandler` |
| `0x04` / `0x05` / `0x06` | Player Position / Look / Position & Look | `PlayerHandler::handlePlayerMovement` |
| `0x07` | Player Digging | `WorldHandler::handleDigging` |
| `0x08` | Block Placement / Container Interaction | `WorldHandler::handleBlockPlacement` |
| `0x09` | Held Item Change | `InventoryHandler::handleHeldItem` |
| `0x0A` | Animation (Arm Swing) | `EntityHandler::handleAnimation` |
| `0x0B` | Entity Action (start/stop following) | `EntityHandler::handleEntityAction` |
| `0x0C` | Steer Vehicle | (ack-only) |
| `0x0D` / `0x0E` | Close Window / Click Window | `UiHandler` |
| `0x0F` | Transaction (client ACK) | `UiHandler` |
| `0x10` | Creative Inventory Action | `InventoryHandler::handleCreativeInventoryAction` |
| `0x11` | Enchant Item | `InventoryHandler` |
| `0x12` | Update Sign | `WorldHandler` |
| `0x13` | Client Abilities | `GameRules` hook |
| `0x14` | Tab Complete | (ignored) |
| `0x15` | Client Settings | (ignored) |
| `0x16` | Client Command | (ignored) |
| `0x17` | Custom Payload | (ignored) |
| `0x18` | Spectate | (ignored) |
| `0x19` | Resource Pack Receive | (ignored) |

Anything that does not match logs

```
Received packet id=<n> from <username>
```

before falling through.

> **Caveat:** the switch is hard-coded to the **1.8 ID table**, which is
> correct for LEGACY (protocol < 47 only maps to 1.7.2 / 1.7.10 / 1.8,
> and 1.8 IDs are already 47+). If you are running a MODERN client,
> `PacketIdHandler::getC2SPacketKind` translates the era-specific wire
> id back to the same `PacketKind` before the switch runs, so the
> switch above stays correct for MODERN / LATEST too.


## Play-side S2C — `sendPlayPackets`

`PlayerManager::sendPlayPackets` sends the classic 1.8 init sequence:
`Join Game (0x01)` → `Spawn Position (0x05)` → `Time (0x00)` → ...
Each *field* inside the packet is era-translated by `EraAdapter` before
`BaseProtocol::encodeFrame` turns it into wire bytes.

Two quirks worth remembering when you touch this file:

- **`Join Game` dimension byte is hard-coded `0`** (`Overworld`) in
  `PlayerManager.cpp`. Multiworld sessions that reconnect in a
  non-Overworld plane still see a `0` in the wire byte — the client
  lands in the Overworld visually even though the server-side
  `PlayerSession` has `session.worldId` on the correct plane. See
  [Multiworld & Dimensions](Multiworld-and-Dimensions) for how the
  rest of the pipeline copes.
- **`clientProtocol >= 47` gates the 1.8 trailer** (Max Players, Level
  Type, Reduced Debug Info) on Join Game. Re-ordering or dropping that
  guard will desync any pre-1.8 client.

Similar `clientProtocol >= 47` guards appear in `InventoryHandler.cpp`
(NBT end-of-compound tag) and `WorldHandler.cpp` (12 vs 16-bit Y field).

## `/testpackets` — dumping the full S2C set

Defined in `include/commands/TestPacketsCommand.hpp`, **op level 3**,
wired into `NetworkManager::start`. It iterates the live session's
`session.protocol` (creating one if missing), calls the era's
`versionName()` accessor, and pushes a full set of S2C packets over
the socket so a Python probe can byte-compare them against vanilla
fixtures.

```
/testpackets [playername]
```

It is the recommended way to sanity-check that your adapter edits
actually change the wire.


## What breaks when the era is wrong

| Symptom | Root cause / fix |
| --- | --- |
| Server logs `Handshake from ... protocol=<n> (unsupported; nearest supported: <version>)` | Client's protocol number is missing from `versions.json`. Add a row and regenerate headers with `tools/gen_protocol_data.py`, or accept the fallback era. |
| Server logs `Handshake from ... [UNSUPPORTED ERA - legacy handlers only]` | `EraAdapter::create` returned `nullptr` — no adapter for that era. Check `eraFor(protocol)` returns an era you have an adapter for. |
| Client connects but never moves | `session.clientProtocol < 47` mis-set during handshake. Usually because the client sent `Hello` with `nextState=2` (LOGIN) before its protocol varint was captured. Read raw bytes with `packet_probe.py`. |
| MODERN client renders 1.7-style blocks (no palette) | `EraAdapter::encodeChunkSection` fell back to LEGACY. Check the era table. |
| Client rejects Join Game trailer | `clientProtocol >= 47` mismatch: 1.7 client got the 1.8 trailer, or 1.8 client got the truncated LEGACY trailer. |
| Client drops connection right after first chunk | `sendPacket` logged `dropped (exceeds 21-bit wire length)` — payload > 2 MiB. Enable the chunk-stream throttle in `Server.cpp` or split the packet on the source side. |
| `rxBuffer` grew too large / "buffer overflow" log | Incoming packet > 4 MiB. Rare; usually a client bug. Kill the connection. |

## Phase 13 auth/server-browser residuals (what Phase 5 left open)

Audit trail for the vanilla-auth/server-browser gaps tracked across Phase 13.

- **AES-128 CFB on the frame path**: `src/network/AesCfb.cpp` +
  `include/network/AesCfb.hpp` exist and LoginHandler already arms
  `session.cipherS2C`/`cipherC2S` on MODERN/LATEST token-auth, but
  `BaseProtocol::encodeFrame` never consults those cipher members —
  token-era sessions still travel **plain-framed** end-to-end. Open item,
  scoped for a follow-up auth phase; the recv side parses frames in
  plaintext (`NetworkEvents.cpp::handleClientData`).
- **InvalidSessionToken S2C reply**: `NetworkManager::denySessionTokenOnFd`
  emits wire `0x04` with a plain reason-string payload (kept distinct from
  CannotConnect `0x03`, which now only flags verify-chain outages) and
  LoginHandler routes empty/foreign access tokens to it on token-era auth
  failures — done in Phase 13.
- **Peer AddPlayer/RemovePlayer**: only Scoreboard packets `0x3B–0x3D` are
  sent to PLAY sessions today; `encodeAddPlayer`/`encodeRemovePlayer` in
  `PacketCodec47.hpp` have no invocation outside the `/testpackets` command.
- **Server-browser master-server ping requester**: inbound GET-ping
  detection + status datagram (`ServerBrowser.cpp`) work, and the outbound
  `[auth] master-server-ping` fire-and-forget HTTP call exists in
  `Authentication.cpp::pingMasterServers`; the `[auth] server-icon` key's
  trailing `&icon=<base64>` status-datagram suffix fed by that new key are
  both in place, leaving only Base64 loading of an actual PNG/JPEG asset.

## Phase 14 play-packet residuals (what Phases 6–7 left open)

Same audit discipline as above, for the vanilla **play**-packet gaps:

- **Ground-item packet pair**: SQLite persistence (`Database::saveGroundItem`/
  `loadGroundItems`, consumed by `src/world/ProcessingTick.cpp`) and the mob
  drop rule gate (`mob_drops` in `src/world/EntityManager.cpp`) are live, but
  no era-mapped Spawn Item / Collect Item packet is broadcast to vanilla
  clients when items land on ground or get picked up; and the block-drop side
  of that pair — the per-block drop tables backing the `block_drops` gamerule
  and `/loot`-style payloads — remains unapplied. Open item for a follow-up
  play-packet phase; the wire ops are absent from era 1.7 and have no entry in
  `tools/protocol_seed/packet_ids_1.8.json`, so any new op needs a seed +
  PacketKind entry first (never invent an op code).

## Cross-links

- [Home](Home) — general overview and version history.
- [Installation](Installation) — what libraries / build flags are
  required for the network layer to link (`libz`, `liblz4`, `libsqlite3`).
- [Configuration](Configuration) — how `[server] port`, `[security]
  enable-antiscan`, and `[world] overworld-active` interact with
  handshake / connection code.
- [Testing](Testing) — the Python probe harness and the
  `tools/gen_protocol_data.py` seed file that produces this era table.
- [Storage & Save Files](Storage-and-Save-Files) — chunk-level
  encoding; how the LZ4 blob on disk becomes an era-translated wire
  packet.
- [Multiworld & Dimensions](Multiworld-and-Dimensions) — how
  `session.worldId` interacts with the Join Game byte that is
  otherwise hard-coded to Overworld.
- [Troubleshooting](Troubleshooting) — handshake failures, socket
  errors, and startup refusals.

