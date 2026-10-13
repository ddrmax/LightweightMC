cmake_minimum_required(VERSION 3.20)
project(LightweightMC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Limit the stack of main thread to 1 MB (1048576 bytes) instead of 8 MB
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,stack-size=1048576")
# Aggressive optimization flags for ARM / C++
add_compile_options(-O3 -flto -Wall -Wextra)

# Optional sanitizer build: turn ON with -DWITH_SANITIZERS=ON to get
# ASAN + UBSAN instrumentation instead of -O3/-flto. Useful for
# reproducing protocol/memory bugs under a real client. Note that ASAN
# and -flto interact poorly, so when the flag is on we drop -O3 and -flto.
option(WITH_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(WITH_SANITIZERS)
    add_compile_options(-g -O1 -fno-inline -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# System dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

include_directories(
    include
    ${LZ4_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(lightweight_mc ${SOURCES})

target_link_libraries(lightweight_mc
    PRIVATE
    ${LZ4_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    z
    pthread
)