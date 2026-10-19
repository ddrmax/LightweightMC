# Installation

## Build requirements

| Requirement | Minimum | Why |
| --- | --- | --- |
| Compiler | GCC 11+ or Clang 13+ | The code is C++20 (`std::span`, designated initialisers, structured bindings, `__FILE__`-style macros) |
| CMake | 3.20+ | `cmake_minimum_required(VERSION 3.20)` in `CMakeLists.txt` |
| `liblz4` (`liblz4-dev`) | required | Chunk blobs are LZ4-compressed (`pkg_check_modules(LZ4 REQUIRED liblz4)`) |
| `libsqlite3` (`libsqlite3-dev`) | required | World/chunk/player/gamerule storage (`pkg_check_modules(SQLITE3 REQUIRED sqlite3)`) |
| `zlib` | linked as `-lz` | Deflate path for the 1.8 map/chunk packets |
| `pthreads` | linked as `-pthread` | Console thread, per-plane chunk-generation workers |
| Kernel/libc | Linux, glibc with `mallopt` | `main.cpp` calls `mallopt(M_ARENA_MAX, 1)` to keep one malloc arena |

CMake pins the standard and refuses extensions:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-O3 -flto -Wall -Wextra)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,stack-size=1048576")
```

The linker flag shrinks the **main-thread stack to 1 MB** (instead of the 8 MB default) so the binary
fits small RAM targets; deep recursion must therefore stay shallow.

## Build variants

| Name | Command | Output | Notes |
| --- | --- | --- | --- |
| `release` | `cmake -DCMAKE_BUILD_TYPE=Release ..` | `build/lightweight_mc`, `-O3 -flto` | The build you run in production |
| `debug` | `cmake -DCMAKE_BUILD_TYPE=Debug ..` | `build/lightweight_mc`, `-g` | Symbols retained; sanitizers stay off |
| `sanitizer` | `cmake -DCMAKE_BUILD_TYPE=Debug -DWITH_SANITIZERS=ON ..` | ASAN + UBSAN build | Adds `-g -O1 -fno-inline -fsanitize=address,undefined`; **drops `-O3` and `-flto`** because ASAN and LTO interact badly |
| `verbose` | `make -j"$(cpus)" VERBOSE=1` | Same binary, full compile lines | Use when hunting an include/link error |

```bash
# Release
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j"$(cpus)"

# Sanitizer build (separate directory so the objects never mix)
mkdir san && cd san
cmake -DCMAKE_BUILD_TYPE=Debug -DWITH_SANITIZERS=ON ..
make -j"$(cpus)"
```

`CMakeLists.txt` compiles **every** `src/*.cpp` into one executable (`file(GLOB_RECURSE SOURCES ...
"src/*.cpp")` → `add_executable(lightweight_mc ${SOURCES})`); there is no library target and no test
target, so builds fail on the first missing symbol rather than on a per-target basis.

## Running the server

The binary reads its configuration and opens its databases **relative to the current working
directory**, so start it from the folder that holds `server.properties`:

```bash
cd /path/to/LightweightMC
./build/lightweight_mc
```

To run it elsewhere, copy `server.properties` into that directory first (the loader silently
falls back to built-in defaults when the file is missing and regenerates it — see
[Configuration](Configuration)).

Expected startup sequence:

```
<MMA> ASCII banner ("Native C++ Engine | Event-Driven & Zero-Tick")
[CommandManager] Registered command: /help
...                                    (one line per verb + one per alias)
[CONFIG] Server Port: 25565
[CONFIG] Max Players: 100
[CONFIG] MOTD: LightweightMC C++ Engine
[CONFIG] AntiScan Enabled: Yes
SQLite Database Init...
Starting Network Manager (epoll)...
Server is ready and listening. Type /help for available commands.
```

`Server::start()` then runs a **fixed 20 TPS accumulator loop**: `pollEvents(10)` (10 ms epoll slice)
plus `tickActiveChunks()` every 50 ms, with a throttled 5 Hz chunk-stream flush. If more than five
ticks fall behind (a stall), the accumulator snaps to `now` instead of firing a catch-up burst.

## Shutdown & signals

| Signal | Behaviour |
| --- | --- |
| `SIGINT` (Ctrl+C) | `main.cpp`'s handler calls `Server::stop()`, which flips `m_running` and closes the epoll loop cleanly |
| `SIGTERM` | Same graceful stop |
| `SIGSEGV` / `SIGABRT` | `restoreTerminalOnSignal()` restores the terminal mode (the console thread uses `termios`), then re-raises with `SIG_DFL` so a core/traceback is still produced |
| Other signals | Exit code `128 + signal` |

`/stop` (op level 4) is the in-band equivalent of Ctrl+C and is the preferred way to shut down when a
profiler or debug session is attached.

## Runtime footprint

| Metric | Typical value |
| --- | --- |
| RSS at boot | < 30 MB |
| Free RAM needed on target | < 256 MB |
| Threads | main + console (256 KB stack) + one generation worker **per active plane** |
| Target ISA | ARMv7 / ARMv8 (Raspberry Pi 2/3/4, RK3318, Allwinner, Orange Pi) |

There is no JVM and no garbage collector; the process is a single native binary. Cross-compiling for
ARM is supported by design, but no wrapper script ships in this tree — drive CMake with a
`-D` toolchain file (e.g. `-DCMAKE_TOOLCHAIN_FILE=...aarch64....cmake`).

Next: [Configuration](Configuration) for every `server.properties` option.
