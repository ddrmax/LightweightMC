# 📋 LightweightMC — Roadmap & TODO List

High-performance, event-driven C++20 Minecraft server optimized for low end or Linux SBC's production environments .

---

## 🏗️ Build & Multi-Version Architecture

- [ ] **Compile-Time Protocol Versioning**
  - [ ] Isolate version-specific packet IDs, serialization rules, and bitwise metadata structures into header-only protocol modules (`src/protocol/v1_8/`, `src/protocol/v1_9/`).
  - [ ] Add ViaVersion / ViaBackwards / ViaRewind style support to support players with Minecraft 1.8 up to latest 26.X relase.

---

## 🟢 Implemented & Operational

### 🌐 Core Network Engine
- [x] Non-blocking event loop using Linux `epoll`.
- [x] Asynchronous write queue driven by `EPOLLOUT` to prevent socket buffer congestion and packet fragmentation.
- [x] Full binary serialization/deserialization suite (VarInt, String, Double, Float, Long, Position).
- [x] Safe client disconnection handling with resource cleanup (`close(fd)`, `m_clients.erase`) and remote peer IP/port resolution via `getpeername()`.
- [x] Centralized logging framework.

### 🔐 Handshake & Authentication
- [x] Handshake state machine transitions (State 1: Status, State 2: Login).
- [x] Legacy and modern Server List Ping (SLP) handling with dynamic JSON MOTD responses.
- [x] Ping/Pong latency measuring packets.
- [x] `Login Start` processing and transition to `PLAY` state.

### 💬 Chat & Command System
- [x] UTF-8 character encoding and hex escape sequence isolation (`\xC2\xA7`).
- [x] Raw String Literals (`R"(...)"`) for JSON chat payloads.
- [x] Argument parsing framework using `std::istringstream`.
- [x] Global chat broadcasting to active players.
- [ ] **Player Commands**
  - [x] Command `/help` (interactive command listing).
  - [x] Command `/ping` (latency test).
  - [x] Command `/list` (connected players overview).
  - [x] Commands `/hud` & `/stats` (triggers Scoreboard and Tablist header/footer updates).
  - [x] Private messaging `/tell`, `/w`, `/msg` (bi-directional whisper routing).
  - [x] Teleportation commands: `/tp <x y z>`, `/tp <player>`, `/tp <p1 p2>`, `/tpall`.
- [ ] **Administration Commands**
  - [x] Commands `/antiscan <block|add|reload>` & `/hunter <block|add|reload>` (AntiScan Feature).
- [ ] **PaperMC / Bukkit Style Commands (Refer to PaperMC Commands Reference)**
  - [x] Commands `/version`, `/ver`, `/about` (displays the version information about the server).

### 🗺️ World & Player Teleportation
- [x] `0x01 Join Game` and `0x05 Spawn Position` dispatch.
- [x] `0x08 Player Position And Look` packet handling for server-side teleportation and client coordinate syncing.
- [x] Basic block interaction handling (`0x23 Block Change`).

### 2. 👥 Player Tracking & Visibility
- [ ] **Player Animations & Status Flags**
  - [x] Arm-swing animation propagation (`0x0B Animation`).


### 📦 Inventories & Tile Entity Storage
- [ ] **Window & Slot Management**
  - [x] Dispatch `0x30 Window Items` on player login.
  - [x] Handle inventory click packets (`0x0E Click Window`).
  - [x] Synchronize hotbar slot selection (`0x09 Held Item Change`).

### 💾 Database & State Persistence
- [ ] **SQLite Integration**
  - [x] `players` table: UUID, username, inventory binary data, location, gamemode.

### 🔒 Extras Features
- [ ] **🔑 Core Management & Security**
  - [x] Hunter : integration of PebbleHost's Minecraft "server scanning" IP list, that will send garbage (simulates another type of service when detecting these IPs). 
  - [x] Hunter : Custom list of IPs that can be added via commands 

---

## 🟡 In Progress & Debugging

- [ ] **Thread Safety & Concurrent Iteration**
  - [ ] Fix `Segmentation Fault` occurrences during high-frequency bot connection/disconnection stress tests.
  - [ ] Secure `m_clients` map access using `std::shared_mutex` or safe iterator copying during network broadcasts.
  - [ ] Add guard clauses in packet dispatch handlers to prevent processing dangling socket descriptors.
- [ ] **MTU & Network Buffer Optimization**
  - [ ] Enforce packet chunking rules for `0x33 Chunk Data` to fit within standard VPS MTU limits (~1414–1500 bytes) without triggering socket write blocks.

---

## 🔴 Outstanding Tasks (Path to 100% Production Ready)

### 1. 🌍 World Generation & Chunk Management
- [ ] **Chunk Manager Engine**
  - [ ] Implement dynamic view-distance chunk loading/unloading centered on player coordinates.
  - [ ] Build a basic world generator (Superflat / 3D Perlin Noise terrain).
  - [ ] Integrate Anvil region file format (`.mca`) parser for loading pre-built maps.

### 2. 👥 Player Tracking & Visibility
- [ ] **Entity Visibility System (Player Tracking)**
  - [ ] Spawn visible player models for nearby clients (`0x0C Named Entity Spawn`).
  - [ ] Synchronize relative movement and rotation updates (`0x14 Entity Relative Move`, `0x18 Entity Teleport`).
  - [ ] Despawn entities leaving the client's view distance (`0x13 Destroy Entities`).
- [ ] **Player Animations & Status Flags**
  - [ ] Metadata synchronization for sneaking, sprinting, and action states.
  - [] Tile Entity initialization via `0x35 Update Block Entity` (for containers like Chests, Furnaces, Brewing Stands, Signs).

### 3. 📦 Inventories & Tile Entity Storage
- [ ] **Container State Persistence**
  - [ ] Server-side storage for Chests, Furnaces, and Brewing Stand inventories.
  - [ ] Container opening animations and window tracking (`0x2D Open Window`).
- [ ] **Dropped Items**
  - [ ] Item entity spawning (`0x0E Spawn Object`) and pickup logic (`0x0D Collect Item`).

### 4. ⚔️ Physics, Health & Gameplay Mechanics
- [ ] **Movement Validation & Collisions**
  - [ ] Server-side movement verification (`0x04 Player Position`) to prevent noclip/speed exploits.
  - [ ] Gravity calculation and fall damage tracking.
- [ ] **Health & Combat System**
  - [ ] Health, food, and saturation packet handling (`0x06 Update Health`).
  - [ ] PvP and PvE hit detection and knockback calculation.
  - [ ] Player respawn flow (`0x07 Respawn`).
- [ ] **Administration Commands**
  - [ ] `/gamemode <0|1|2|3>` (`0x2B Change Game State`).
  - [ ] `/time set <day|night|ticks>` (`0x03 Time Update`).
  - [ ] `/weather <clear|rain|thunder>`.
  - [ ] `/give <player> <item> [amount]`.
- [ ] **Player Commands**

- [ ] **PaperMC / Bukkit Style Commands (Refer to PaperMC Commands Reference)**
  - [ ] `/reload ` (reloads server config without restarting the server).
  - [ ] `/lwmc`(equivalent of paper commands).
    - [ ] `/lwmc chunkinfo [<world>]`.
    - [ ] `/lwmc dumpitem [all]`.
    - [ ] `/lwmc entity list [<filter>] [<world>]`.
    - [ ] `/lwmc holderinfo [<world>]`.
    - [ ] `/lwmc mobcaps [<world>]`.
    - [ ] `/lwmc playermobcaps [<player>]`.
    - [ ] `/lwmc chunkinfo [<world>]`.
### 5. 💾 Database & State Persistence
- [ ] **SQLite Integration**
  - [ ] `blocks` / `tile_entities` table: Modified world block state persistence.
  - [ ] Periodic asynchronous auto-save thread.

### 6. 🔒 Security & Performance Monitoring
- [ ] **Rate Limiting & Hardening**
  - [ ] Inbound packet frequency throttling to prevent packet-flooding DoS attacks.
  - [ ] Chat input sanitization and NBT bounds validation.
- [ ] **Performance Metrics**
  - [ ] Server tick loop implementation locked to 20 TPS.
  - [ ] Real-time CPU, RAM, and bandwidth usage statistics tracking.

### 7. 🔒 Extras Features
- [ ] **🛡️ Land Protection & World Control features**
  - [ ] GriefPrevention : Chunk and teritory claiming.
  - [ ] WorldGuard : An advanced region management tool for server admins.
- [ ] **🔑 Core Management & Security**
  - [ ] LuckPerms : Fine tuned permissions management.
  - [ ] Hunter : adding unblock/remove feature to allow a server (only for custom adresses) 
