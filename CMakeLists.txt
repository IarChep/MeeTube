cmake_minimum_required(VERSION 3.18.0)

project(MeeTube LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Platform: MeeGo Harmattan (Nokia N9) only.
#
# BUILD_N9 distinguishes the two Harmattan builds: ON = cross-compile for
# the physical N9 device, OFF = host build against the Qt SDK Simulator. It is
# set by ./configure (n9 -> ON, simulator -> OFF).
# ---------------------------------------------------------------------------
option(BUILD_N9 "Cross-build for the Nokia N9 (device) rather than the host Simulator" OFF)

# C++23 for Glaze (compile-time reflection). Qt 4.7 headers verified clean at
# -std=c++23 on both toolchains (cross GCC 14.1 + device sysroot, host GCC 16 +
# Simulator Qt) — only the long-standing -Wregister noise, same as at C++20.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build every object position-independent. The N9 device app is linked -pie
# (a PIE/DYN) against the MDeclarativeCache startup booster, which requires PIC
# objects; PIC is also needed so the MODULE plugins (e.g. libqwebp.so) load.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Propagate BUILD_N9 as a compile definition so device-only code can be
# guarded with #if defined(BUILD_N9).
add_compile_definitions($<$<BOOL:${BUILD_N9}>:BUILD_N9>)

# ---------------------------------------------------------------------------
# Full-release device optimizations (BUILD_N9 + Release only). Applies to our
# own targets (meetube-core, the app, the qwebp plugin) — the deps/ External
# Projects keep their own tuned build systems.
#   -mcpu=cortex-a8       pipeline-accurate scheduling for the N9's OMAP3630
#                         (the toolchain default is generic -march=armv7-a+simd;
#                         NEON + hard-float are already the defaults — the float
#                         ABI must NOT be touched, it matches the device libs).
#   -ffunction/-fdata-sections + --gc-sections
#                         drop unreferenced code/data: smaller binary, faster
#                         cold start, less icache pressure.
# NO -flto: the link deliberately goes through the 2009 binutils ld (2.19.51,
# no plugin API) for device-loader compatibility, where LTO silently degrades
# to a plain link — not worth destabilizing the loader setup for zero gain.
# ---------------------------------------------------------------------------
if(BUILD_N9 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-mcpu=cortex-a8 -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections -Wl,-O1)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Qt4 4.7.4 COMPONENTS
    QtCore QtGui QtDeclarative QtNetwork QtTest REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
enable_testing()

include(ExternalProject)

# ---------------------------------------------------------------------------
# Bundled third-party dependencies — extracted into deps/CMakeLists.txt.
#
# Declares the ExternalProjects (libpng12_build, libjpeg62_build,
# libwebp_build), the qrcodegen static lib, and the IMPORTED GLOBAL png12
# target. Added EARLY (before core/ and the app) so the targets + the
# cross-consumed CACHE INTERNAL vars (LIBPNG12_*, LIBJPEG62_*, LIBWEBP_*)
# it sets are available to everything below and to webp-imageformat/.
# ---------------------------------------------------------------------------
add_subdirectory(deps)

# ---------------------------------------------------------------------------
# Source subprojects (src/): the meetube-core lib (src/core/), the WebP plugin
# (src/webp-imageformat/) and the GUI app (src/app/) — each with its own
# CMakeLists.txt, added in dependency order by src/CMakeLists.txt. Placed before
# tests/bench_json (which link meetube-core); deps/ is already added above so
# its targets + CACHE INTERNAL vars are visible inside src/.
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Test harness — its own subproject; host/simulator only (no QtTest on device).
# Added after src/ because the tests link meetube-core (defined in src/core/).
# ---------------------------------------------------------------------------
if(NOT BUILD_N9)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# bench_json — parity-dump + micro-benchmark harness for the JSON backend.
# Built for BOTH targets (the benchmark numbers that matter are the on-device
# ones); never installed, not a ctest. No QtTest dependency — plain QtCore.
# ---------------------------------------------------------------------------
add_executable(bench_json tests/bench_json.cpp)
target_link_libraries(bench_json PRIVATE meetube-core)
if(BUILD_N9)
    # Same device-runnability recipe as the app: hard-float loader pin + static
    # C++ runtime (the device's GCC-4.x libstdc++ lacks this build's symbols).
    target_link_options(bench_json PRIVATE
        -static-libstdc++ -static-libgcc
        -Wl,--dynamic-linker,/lib/ld-linux.so.3
        -Wl,-rpath,/opt/meetube/lib -Wl,--disable-new-dtags)
endif()

# (The WebP plugin `qwebp` and the `meetube` app executable are built inside
# src/ — see src/CMakeLists.txt and src/{webp-imageformat,app}/CMakeLists.txt.)
