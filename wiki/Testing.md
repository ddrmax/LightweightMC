# Testing

There is **no CMake test target and no C++ unit-test framework** in this tree — `CMakeLists.txt`
declares a single executable (`add_executable(lightweight_mc …)`) and nothing else. Regression
testing is therefore done three ways:

1. **Python protocol probes** in the repo root — small hand-written sockets that log into the
   server, send a specific C2S packet (or packet set), and assert on what the S2C stream looks
   like back. They are the *only* automated way to prove that a C2S handler actually parses,
   that subsequent packets stay framed, and that the S2C side emits the expected IDs/payloads.
2. **`/testpackets`** — an in-band admin verb (`include/commands/TestPacketsCommand.hpp`, op
   level 3) that builds the canonical S2C packet set through the caller's live `BaseProtocol`
   and logs the resolved wire IDs. It lets an operator eyeball framing/ID mapping for a given
   protocol version **before** any compression is enabled on the wire.
3. **Offline tools** in `tools/` — the protocol-data generator
   (`tools/gen_protocol_data.py`, driven by `tools/protocol_seed/*.json`) and the standalone
   chunk verifier (`tools/verify_chunk_packet.cpp`). They never touch the running server;
   they check the tables and wire-format builders in isolation.

The whole set is glued together by **`run_tests.sh`** at the repo root, which brings up a
server, runs the probes in sequence, and prints a one-line verdict per test.

## Scripts on disk

Every entry lives at the repo root except the two under `tools/`. All Python probes import
shared framing/login helpers from `chest_test.py` (`PacketBuffer`, `recv_packet`,
`send_packet`, `write_varint`, `do_login`, `build_click_window`, `parse_window_items`),
so `chest_test.py` is the *de-facto* protocol library, not just a test.

| Path | Kind | Purpose | Invocation | Exit 0 means |
| --- | --- | --- | --- | --- |
| `chest_test.py` | Regression | Full chest-open → click → reconnect loop on windowId 4 (MC 1.8 `0x0E ClickWindow` / `0x31 WindowItems` / `0x32 Transaction`). Verifies persistence across a **second login session** and that `0x31`/`0x32` bodies are not truncated. Also hosts the shared `do_login`, `build_click_window`, `parse_window_items` helpers. | `python3 chest_test.py 127.0.0.1 25565` | `0` — chest is empty after a grab-then-reconnect round trip and every click response is complete (no truncation). |
| `reorg_test.py` | Regression | "Reorganising inventory" path: `WindowClick` on **windowId 0** (the player's own inventory) with `mode=1`, then the same slot cleared with `item=-1`. Asserts the server replies `0x2F SetSlot` + `0x32 Transaction accepted=true` both times. | `python3 reorg_test.py 127.0.0.1 25565` | `0` — both clicks (place and clear) got a complete `SetSlot` + `Transaction` pair and were accepted. |
| `packet_probe.py` | Regression | Fires every MC 1.8 (protocol 47) C2S packet that used to fall through `default:` in `PlayHandler.cpp` and confirms the server does **not crash**, consumes the packet body cleanly, and the follow-up `ClickWindow` still yields `0x2F`+`0x32`. | `python3 packet_probe.py 127.0.0.1 25565` | `0` — both `saw_set_slot` and `saw_confirm` came back true from the follow-up click (stream stayed aligned). |
| `spawn_probe.py` | Regression | Terrain-aware spawn path: after login it drains the burst, keeps every `SpawnEntityLiving` (`0x0F`), and asserts **Y is on the world surface** (encoded int between 8 500 and 10 500, i.e. y ≈ 8.5–10.5 world units, not the old `y=50` default) and that the tail is three zero velocity shorts + a zero metadata byte, consumed exactly to `len(payload)`. | `python3 spawn_probe.py 127.0.0.1 25565` | `0` — either zero spawn packets (clean login) or every spawn packet has an in-band Y **and** a well-formed tail. |
| `bot_test.py` | Diagnostic | Debugs the `0x26 MapChunkBulk` / `0x21 Chunk` stream with zlib `Set Compression` (opcode `0x03`). Sends a login burst, reads up to 400 packets, and prints a summary of every captured chunk packet plus any parse errors. Not a strict PASS/FAIL — read the tail. | `python3 bot_test.py` — **takes no argv**, hard-codes 1.8 defaults. | Not exit-code based; inspect `[bot] Captured N chunk packet(s)` and any `Parse error` lines. |
| `raw_wire_probe.py` | Diagnostic | Dumps the raw hex of one `SpawnEntityLiving` (`0x0F`) from the login burst so the velocity / metadata **unsigned-short length prefixes** are directly observable on the wire. Uses `chest_test`'s framing helpers. | `python3 raw_wire_probe.py [host] [port]` | Not a verdict — prints `pid=0x0f payload_len=… hex=…` and a decoded `eid/type/x/y/z/vel/meta_len/consumed/tail_after` line. |
| `handshake_probe.py` | Diagnostic | 17-line socket that sends a single raw handshake frame (proto=47, host=`localhost`, port=`0x63F9`, nextstate=2) and prints any returned bytes. Used to confirm the epoll listen socket is alive and processes the handshake. | `python3 handshake_probe.py` (hard-codes `127.0.0.1 25565`) | n/a — prints `connected`, `sending handshake frame: …`, then `GOT RESPONSE (…)` or `NO RESPONSE (…)`. |
| `run_tests.sh` | Runner | Boots the server if not already running, waits 45 s for cold world pre-generation, then runs `chest`, `reorg`, `packet_probe`, `spawn_probe` with `timeout 45`, then `bot_test.py` with `timeout 120` (no argv), and tails `/tmp/rt_<name>.log` for a verdict per test. | `bash run_tests.sh` — `cd`s to `/root/LightweightMC` first, uses `./build2/lightweight_mc`. | `bash` exit code is whatever the final `for` loop's `tail` returns; **always read the printed verdicts**, not the shell exit. |

## Running the regression suite

`run_tests.sh` is the entry point. It assumes the tree is at `/root/LightweightMC` (it
hard-codes that path with `cd`) and that the server binary lives at `build2/lightweight_mc`
(sanitizer/ASAN builds land in sibling directories — see [Installation](Installation#build-variants)):

```bash
bash /root/LightweightMC/run_tests.sh
```

What it actually does, in order:

1. `pgrep -f lightweight_mc`. If a server is already up, reuses it; otherwise it starts
   one in its own session and **sleeps 45 s** for cold world pre-generation
   (`setsid ./build2/lightweight_mc > /tmp/server.log 2>&1 < /dev/null &`).
2. For each of `chest`, `reorg`, `packet_probe`, `spawn_probe`:
   `timeout 45 python3 <script> 127.0.0.1 25565 > /tmp/rt_<name>.log 2>&1`, and echoes
   `[runner] exit=<code> for <name>`.
3. Runs `bot_test.py` separately with `timeout 120` **without arguments** (it takes no argv —
   see its table row).
4. Prints `--- verdict: <name> ---` followed by the last 6 lines of each log.

Read the `PASS`/`FAIL`/`WARN` line in each verdict block rather than the shell exit code —
the exit-code convention differs per script (see the [quirks](Testing#quirks-and-known-issues)
section).

The server log is at `/tmp/server.log`; per-test logs at `/tmp/rt_<name>.log` (`chest`, `reorg`,
`probe`, `spawn`, `bot`).

## In-band protocol validation with `/testpackets`

The `/testpackets` verb (aliases `/tpk`, `/packettest`; op level **3**, `wired` — see
[Commands → Debug & diagnostics](Commands#debug--diagnostics)) is the closest thing to a unit
test you can run **against a running server** without touching the wire: it builds the
canonical S2C set through the caller's `BaseProtocol` and logs each framing result. It is
useful whenever a client is complaining about "unsafe read" / mis-framed packets and you
need to know what the server *would* send for a given protocol version.

Behaviour, from `TestPacketsCommand.hpp`:

| Step | Effect |
| --- | --- |
| Argument parsing | Accepts **at most one** arg (`/testpackets [player]`); anything more replies with `Usage: /testpackets [player]` and returns. |
| Session resolution | If invoked by a live player, `NetworkManager` installs a `SessionProvider` (`TestPacketsCommand::setSessionProvider`) that maps the sender's name to the `PlayerSession` they negotiated. Console invocations (and players without a mapped session) fall back to a fresh default 1.8 / protocol-47 session. |
| Build & log | Calls `logPacket(proto, kind, payload, index)` for six `PacketKind`s: `JOIN_GAME`, `CHAT` (`"TestPackets: S2C chat ok"`), `KEEP_ALIVE` (id = 1), `SPAWN_POSITION` (session's spawn X/Y/Z), `BLOCK_CHANGE` (0,64,0 + stone varint), `ADD_PLAYER` (entity 1, zero UUID, position × 32, yaw/pitch). |
| Log output | `info` line per packet: `[TestPackets] #<index> kind=<int> wireId=0x<hex> payloadBytes=<n> framedBytes=<n>`. If `s2cPacketId(kind) < 0` (kind not in that era's table), a `warn` line records `not supported for this protocol` instead. |
| Console reply | `TestPackets: building S2C packet set for protocol <n> (<versionName>)` at the top, `TestPackets: <k> packet(s) built and logged. Check server console for wire IDs.` at the bottom. |

Note the reply is **echoed to the issuing player or the console**, and the wire IDs are in
the **server log**, not the chat stream — read `logs/` or `/tmp/server.log` for the details.

## Offline tools

Both live under `tools/` and never need a running server.

### `tools/gen_protocol_data.py` — protocol-data generator

Reads hand-curated seed JSON from `tools/protocol_seed/` and emits three committed headers
under `include/protocol/data/`:

| Output | Contents |
| --- | --- |
| `protocol_versions.h` | `ProtocolVersionInfo` table plus `resolve()`, `eraFor()`, `nearestSupported()` |
| `packet_ids.h` | Per-era S2C/C2S ID arrays indexed by the `PacketKind` enum, plus `fromClientPacketId` reverse-lookup tables |
| `PacketIdHandler.hpp` | `PacketIdHandler` class with high-level lookups (`getC2SPacketKind`, `translateS2CLegacyWireId`, …) |

Seed inputs currently on disk: `tools/protocol_seed/versions.json` and
`tools/protocol_seed/packet_ids_1.8.json` (the 1.8 baseline). Optional per-era overrides
`packet_ids_legacy.json`, `packet_ids_modern.json`, `packet_ids_latest.json` are picked up
when present; they **update** the era's copy of the baseline rather than replace it.

Run:

```bash
python3 tools/gen_protocol_data.py            # writes to ../include/protocol/data by default
python3 tools/gen_protocol_data.py --out include/protocol/data
```

The generator asserts that every `kind` in the JSON appears in the canonical
`PACKET_KINDS` list (top of the script) — if you add a new packet to `PacketKind.hpp`,
update `PACKET_KINDS` **and** the seed JSON in the same commit; otherwise generation
fails loudly instead of silently dropping entries.

### `tools/verify_chunk_packet.cpp` — standalone chunk verifier

A self-contained `.cpp` (linked against zlib) that duplicates the vanilla 1.8 chunk wire
format produced by `WorldManager::sendFlatChunk`:

```
int chunkX · int chunkZ · byte transferMode=1 · short bitmask
· varint compressedLen · byte[compressedLen]  (zlib DEFLATE)
```

Inflated payload: for each set bit (ascending) one 12 288-byte section
(`blocks u16 BE + blockLight u8 + skyLight u8`, **no per-section biomes**), followed by a
single 256-byte biomes tail at the end of the whole chunk when `bitmask != 0`. It also
does a big-endian round-trip sanity check on the block builder's write order versus the
decoder's read order (the "no blocks show up" symptom).

It is not in `CMakeLists.txt` (that only globs `src/*.cpp`), so build it standalone when
you need it:

```bash
g++ -O2 -std=c++20 -Iinclude tools/verify_chunk_packet.cpp -o /tmp/verify_chunk -lz
```

## Prerequisites

| Requirement | Why |
| --- | --- |
| `python3`, `zlib` (stdlib) | Probes decode/inflate DEFLATE payloads on the wire. |
| `coreutils timeout` | `run_tests.sh` wraps every probe with `timeout 45`/`timeout 120`. |
| `setsid`, `pgrep` | `run_tests.sh` starts the server detached so it survives a hung probe. |
| A **cold** `world.db` / `world-*.db` for the first run | Pre-generation is what the 45 s `sleep` covers; reusing a warm server means the socket may already be up in a few seconds. |
| Build directory matching the runner's path | `run_tests.sh` currently launches `./build2/lightweight_mc`; if your build landed elsewhere (Release `build/`, sanitizer `san/`, `build-san/`, `build_verify/`, etc.), edit the path in `run_tests.sh` first. |
| Free port `25565` | Every probe hard-codes `127.0.0.1 25565`; `server.properties` must match (`Server Port`). |

## Quirks and known issues

- **Exit-code conventions differ per script.** `chest_test.py` returns `0` on pass and
  `2` on fail; `reorg_test.py`, `packet_probe.py` and `spawn_probe.py` return `0` on pass
  and `1` on fail; `bot_test.py` never sets a fail code. A `PASS` next to `exit=2` in the
  runner output is therefore not a contradiction — read the printed verdict, not the code.
  The chest regression note in `chest_test.py` documents that this used to be inverted
  (`return 2` even when the PASS branch printed) and it has been fixed by tracking
  `passed` explicitly.
- **`chest_test.py` is both a test and a shared library.** Probes `import` it (via
  `sys.path.insert(0, '/root/LightweightMC')`), so **do not** rename it, move it, or wrap
  it in a package without updating every probe's imports.
- **`bot_test.py` takes no argv.** Even though the header says `Usage: python3 bot_test.py [host] [port]`,
  the body ignores `sys.argv`. `run_tests.sh` runs it with `python3 bot_test.py`
  (no args) accordingly.
- **Cold-start timeouts.** The runner's `sleep 45` (and each probe's `timeout 45`) is sized
  for a cold `world.db` on a small SBC. If the world has already been pre-generated the
  server is up in seconds; if you regenerate per boot, bump the sleep or reuse an
  already-running server.
- **Login burst is order-sensitive.** `spawn_probe.py`, `packet_probe.py`, `reorg_test.py`
  each expect the login sequence to emit `LoginSuccess` followed by `0x0F` / `0x26` /
  `0x31`+`0x32` etc.; if you land a change in the login burst that reorders packets,
  update the probes' drain loops in the same commit — they are the closest thing to a
  protocol regression test in this tree.
- **The probes are the only thing that exercises the C2S switch in
  `src/network/PlayHandler.cpp`.** Any new packet kind you add to `PacketKind.hpp`
  must be covered by extending `packet_probe.py` in the same commit; otherwise the
  switch falls through `default:` silently and every C++ build stays green.
- **`/testpackets` is `wired` but shallow** — it builds payload byte vectors hard-coded
  in the header, not read from a running world. It catches protocol-ID/framing bugs
  (era mapping, `s2cPacketId()` mapping, encoder length prefixes), **not** content
  bugs.

## Cross-links

- [Commands → Debug & diagnostics](Commands#debug--diagnostics) — the row for `/testpackets`.
- [Protocol & Clients](Protocol-and-Clients) — protocol↔era mapping that
  `/testpackets` and `gen_protocol_data.py` both validate.
- [AntiScan & Security](AntiScan-and-Security) — a blocked socket never reaches the login
  handshake, which is why `handshake_probe.py` prints `NO RESPONSE` in some CI setups.
- [Installation → Build variants](Installation#build-variants) — pick the right
  `build*/lightweight_mc` path for `run_tests.sh`.

