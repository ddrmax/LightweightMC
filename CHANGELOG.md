# Changelog

All notable changes made to the project **LightweightMC** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and this project respects [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

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

[0.0.5]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.5
[0.0.4]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.4
[0.0.3]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.3
[0.0.2]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.2
[0.0.1]: https://github.com/ddrmax/LightweightMC/releases/tag/v0.0.1