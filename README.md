# LightweightMC 🚀

**LightweightMC** is an alternative Minecraft server engine, developed from scratch in **C++20**. Designed specifically to run on hardware with very limited resources (ARM SBCs, Raspberry Pi, reflashed Android TV boxes), it abandons the traditional JVM architecture in favor of a **100% event-driven** approach and **on-demand time resolution**.

---

## 🔑 Key Features

* **No JVM, No Garbage Collector:** Native C++20 binary with a minimal RAM footprint (< 30 MB at startup).
* **100% Event-Driven Architecture:** Network handling based on `epoll` / non-blocking I/O to manage connections without blocking the main thread.
* **Timestamp Catch-up (Lazy Evaluation):** Elimination of *tick* loops for unloaded chunks. Calculations for furnaces, crops, and inventories are performed using time deltas ($\Delta t$) upon reactivation.
* **Custom I/O Format (`.mcc`):** Hybrid chunk storage combining **SQLite** and ultra-fast **LZ4** compression.
* **Strict Chunk Radius Management:**
  * `Active` (0 to 12 chunks): Full simulation and real-time ticking across the standard render distance.
  * `Frozen` (> 12 chunks at cache edge): Kept in RAM for rendering; AI & Redstone paused.
  * `Unloaded` (Out of range): Saved to disk with a UNIX timestamp.

---

## 📊 Target Hardware

Optimized for micro-servers and SBCs (Single Board Computers):
* **SoC:** ARMv7 / ARMv8 (Raspberry Pi 2/3/4, RK3318, Allwinner, Orange Pi).
* **Required RAM:** < 256 MB free.
* **OS:** Linux (Debian, Armbian, Alpine). ---
## ⛓️‍💥 Version Breaking History:
* September 16, 2026: World data from earlier versions where uncompressed, new worls data is compressed, LightweightMC doesn't support uncompressed data anymore


## 🛠️ Compilation

### Prerequisites
* GCC 11+ or Clang 13+ (C++20 support)
* CMake 3.20+
* `liblz4-dev` & `libsqlite3-dev`

```bash
# Clone the repository
git clone [https://github.com/ddrmax/LightweightMC.git](https://github.com/ddrmax/LightweightMC.git)
cd LightweightMC

# Prepare the build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Compile
make -j$(nproc)
