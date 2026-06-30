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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build every object position-independent. The N9 device app is currently a
# plain EXEC (not yet -pie); PIC is kept on so that switching the app to a
# booster/-pie link in the Phase-4 packaging work needs no per-target churn,
# and so the MODULE plugins (e.g. libqwebp.so) are always PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Propagate BUILD_N9 as a compile definition so device-only code can be
# guarded with #if defined(BUILD_N9).
add_compile_definitions($<$<BOOL:${BUILD_N9}>:BUILD_N9>)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Qt4 4.7.4 COMPONENTS
    QtCore QtGui QtDeclarative QtNetwork QtTest REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
# consumed from Task 3 onward (parsers / engine); provided by conan (conanfile.txt)
find_package(nlohmann_json REQUIRED)
enable_testing()

include(ExternalProject)

# ---------------------------------------------------------------------------
# Bundled third-party dependencies — extracted into deps/CMakeLists.txt.
#
# Declares the ExternalProjects (libpng12_build, libjpeg62_build, openssl102,
# libwebp_build), the qrcodegen static lib, and the IMPORTED GLOBAL png12
# target. Added EARLY (before src/ and the meetube exe) so the targets + the
# cross-consumed CACHE INTERNAL vars (LIBPNG12_*, LIBJPEG62_*, LIBWEBP_*,
# OSSL_*) it sets are available to everything below and to webp-imageformat/.
# ---------------------------------------------------------------------------
add_subdirectory(deps)

# ---------------------------------------------------------------------------
# Core library.
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Test harness — its own subproject; host/simulator only (no QtTest on device).
# Added after src/ because the tests link meetube-core (defined in src/).
# ---------------------------------------------------------------------------
if(NOT BUILD_N9)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# WebP image-format plugin (libqwebp.so).
# ---------------------------------------------------------------------------
add_subdirectory(webp-imageformat)

# ---------------------------------------------------------------------------
# Application: meetube executable
# ---------------------------------------------------------------------------
add_executable(meetube
    src/main.cpp
)

target_link_libraries(meetube PRIVATE
    meetube-core
    Qt4::QtCore
    Qt4::QtGui
    Qt4::QtDeclarative
)

# Host Simulator: link the libpng12 shim AFTER the Qt libs so it resolves
# libQtGui's png_*@PNG12_0 references. Built before meetube via the dependency.
if(NOT BUILD_N9)
    target_link_libraries(meetube PRIVATE png12)
    add_dependencies(meetube libpng12_build libjpeg62_build openssl102)
    set_target_properties(meetube PROPERTIES BUILD_RPATH ${LIBPNG12_INSTALL}/lib)
endif()

if(BUILD_N9)
    # The N9 Harmattan rootfs ships /lib/ld-linux.so.3 (it has NO armhf-suffixed loader),
    # but the GCC-14 cross toolchain — targeting armv7hf (hard-float) — defaults PT_INTERP
    # to /lib/ld-linux-armhf.so.3 — pin it back or the binary fails to exec on device
    # (ENOENT). Static C++ runtime: the device's GCC-4.x libstdc++ lacks the newer
    # GLIBCXX/CXXABI symbols this cross build emits.
    #
    # /opt/meetube/lib RPATH: the deps/ block bundles our freshly-built, ABI-compatible
    # libpng12.so.0 / libjpeg.so.62 (and OpenSSL/libwebp) there. --disable-new-dtags emits
    # a DT_RPATH (not DT_RUNPATH); DT_RPATH is inherited by transitively-loaded libs, so
    # Qt's image plugins resolving libpng12.so.0 / libjpeg.so.62 also search
    # /opt/meetube/lib first → our copies win over the device sysroot's stock ones.
    # (Booster/-pie/-lmdeclarativecache are deferred to the Phase-4 packaging work.)
    target_link_options(meetube PRIVATE
        -static-libstdc++ -static-libgcc
        -Wl,--dynamic-linker,/lib/ld-linux.so.3
        -Wl,-rpath,/opt/meetube/lib -Wl,--disable-new-dtags)
    # The device app does not link png12, but it must wait for the bundled libs to be
    # built + installed (they package into /opt/meetube/lib via the deps/ install rules).
    add_dependencies(meetube libpng12_build libjpeg62_build)
endif()

# Install prefix: /opt/meetube
install(TARGETS meetube
    RUNTIME DESTINATION /opt/meetube/bin
)

# ---------------------------------------------------------------------------
# WebP plugin dir — let main.cpp register it as a Qt library path.
# ---------------------------------------------------------------------------
if(BUILD_N9)
    set(WEBP_PLUGIN_DIR "/opt/meetube/qtplugins")
else()
    set(WEBP_PLUGIN_DIR "${CMAKE_BINARY_DIR}/qtplugins")
endif()
target_compile_definitions(meetube PRIVATE WEBP_PLUGIN_DIR="${WEBP_PLUGIN_DIR}")
add_dependencies(meetube qwebp)
