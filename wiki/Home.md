# LightweightMC wiki

**LightweightMC** is a native C++20 Minecraft server engine written from scratch — no JVM, no garbage
collector, no tick loop for unloaded chunks. It targets micro-hardware (ARM SBCs, Raspberry Pi, reflashed
Android TV boxes) and boots in well under 30 MB of RAM.

- Source repository: <https://github.com/ddrmax/LightweightMC>
- Language standard: **C++20** (GCC 11+ / Clang 13+, CMake 3.20+)
- World storage: **SQLite + LZ4** (`.mcc` hybrid chunk format)
- Networking: **epoll**, non-blocking, event-driven; 1.7.x wire (protocol 47) with era-aware adapters

## Quick start

```bash
git clone https://github.com/ddrmax/LightweightMC.git
cd LightweightMC
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
cd ..
./build/lightweight_mc          # runs from the repo root so it can read ./server.properties
```

The server prints an ASCII banner, logs the loaded configuration, opens the SQLite databases, then prints
`Server is ready and listening. Type /help for available commands.` Type `/help` on the console for the
verbs your op level can use.

## Page index

| Page | Covers |
| --- | --- |
| [Installation](Installation) | Build requirements, release/debug/sanitizer builds, running the binary, startup logs |
| [Configuration](Configuration) | `server.properties` format and **every option** (name, section, type, default, possible values, notes) |
| [Commands](Commands) | **All 84 admin commands**: aliases, usage, **op level**, description, notes |
| [Game Rules](Game-Rules) | Every `gamerule` entry: type, default value, category, engine consumers |
| [Op Levels](Op-Levels) | Permission model (0–4), how `/op` / `/deop` / console interact with it |
| [World Generation](World-Generation) | Gen styles, biomes, villages, mineshafts, dungeons, ore distributions |
| [Multiworld & Dimensions](Multiworld-and-Dimensions) | Planes, `world_registry`, `/world`, `/gotor`, per-plane DBs |
| [Storage & Save Files](Storage-and-Save-Files) | SQLite + LZ4 chunk store, async generation workers, chunk lifecycle |
| [Protocol & Clients](Protocol-and-Clients) | Supported versions, protocol↔era mapping, packet-ID translation, what breaks |
| [AntiScan & Security](AntiScan-and-Security) | IP blocklists, ban/kick flow, whitelist, `antiscan_ips.txt` |
| [Testing](Testing) | Python probes, `run_tests.sh`, `/testpackets`, protocol-data generator |
| [Troubleshooting](Troubleshooting) | Build failures, startup refusals, protocol/era issues, known quirks |

## Version history & breaking changes

| Date | Change | Client impact |
| --- | --- | --- |
| 2026-09-16 | Chunk blobs switched to LZ4-compressed storage; **uncompressed blobs from earlier builds are no longer readable** | Old worlds must be regenerated or migrated; there is no fallback reader |
| 2026-10-10 | Era adapters added: LEGACY (1.7.2–1.12.2), MODERN (1.13–1.20.4), LATEST (1.20.5+) | 1.9+ clients are now accepted instead of kicked |
| 2026-10-10 | Per-plane ore distribution, villages, mineshafts, dungeons added | World output differs byte-for-byte from 0.0.9 for the same seed |

Breaking changes are recorded in `CHANGELOG.md` at the repo root.
