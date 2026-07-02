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

# ---------------------------------------------------------------------------
# WebP image-format plugin (libqwebp.so).
# ---------------------------------------------------------------------------
add_subdirectory(webp-imageformat)

# ---------------------------------------------------------------------------
# Application: meetube executable
# ---------------------------------------------------------------------------
add_executable(meetube
    src/main.cpp
    src/qmlapplicationviewer/qmlapplicationviewer.cpp
    # Squircle avatar: a QDeclarativeItem (MaskedItem) + QGraphicsEffect (MaskEffect)
    # that composites children over an alpha mask (CompositionMode_SourceIn), ported
    # from cuteTube2. Both declare Q_OBJECT -> moc'd via the target's AUTOMOC.
    src/harmattan/maskeffect.cpp
    src/harmattan/maskeditem.cpp
    # Animated Perlin-noise brand banner (QDeclarativeItem) used as the HeaderBar
    # background — ported from throne-harmattan, recolored to MeeTube's red palette.
    src/harmattan/perlinbackground.cpp
    # QR image provider (image://qr/...) for the device-code login; renders via the
    # bundled qrcodegen static lib (linked below).
    src/harmattan/qrimageprovider.cpp
    # QML resources (main.qml + pages/components + avatar-mask.png) baked into the
    # binary; main.cpp loads qrc:/qml/main.qml. Compiled via the target's AUTORCC.
    resources/resources.qrc
)

# qmlapplicationviewer.h declares Q_OBJECT (the QDeclarativeView subclass), so the
# app target must be moc'd. (meetube-core is moc'd in src/CMakeLists.txt.) AUTORCC
# compiles resources/resources.qrc into the binary.
set_target_properties(meetube PROPERTIES AUTOMOC ON AUTORCC ON)

# maskeditem/maskeffect include their sibling headers as "harmattan/...". The
# squircle pulls in <QDeclarativeItem>/<QGraphicsEffect> (QtGui + QtDeclarative,
# already linked on this target below).
target_include_directories(meetube PRIVATE src)

target_link_libraries(meetube PRIVATE
    meetube-core
    qrcodegen
    Qt4::QtCore
    Qt4::QtGui
    Qt4::QtDeclarative
    Qt4::QtNetwork
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
    #
    # Harmattan startup booster: the app is now linked -pie/-rdynamic against
    # libmdeclarativecache so applauncherd can pre-fork it (faster cold start).
    # HARMATTAN_BOOSTER makes createApplication() return MDeclarativeCache::qApplication()
    # (see src/qmlapplicationviewer/qmlapplicationviewer.cpp); the booster flags/cflags come
    # from the device sysroot's qdeclarative-boostable.pc. -pie turns the binary into a
    # PIE (ELF Type=DYN), which is required by applauncherd.
    pkg_check_modules(boostable ${CMAKE_SYSROOT}/usr/lib/pkgconfig/qdeclarative-boostable.pc REQUIRED)
    target_compile_definitions(meetube PRIVATE HARMATTAN_BOOSTER)
    target_compile_options(meetube PRIVATE ${boostable_CFLAGS})
    target_include_directories(meetube PRIVATE ${CMAKE_SYSROOT}${boostable_INCLUDE_DIRS})
    target_link_options(meetube PRIVATE
        -static-libstdc++ -static-libgcc
        -Wl,--dynamic-linker,/lib/ld-linux.so.3
        -Wl,-rpath,/opt/meetube/lib -Wl,--disable-new-dtags
        -pie -rdynamic -lmdeclarativecache)
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

# ---------------------------------------------------------------------------
# CA certificate bundle path — compiled in so main.cpp can load it into
# QSslConfiguration::defaultConfiguration() at startup.
# ---------------------------------------------------------------------------
if(BUILD_N9)
    set(MEETUBE_CA_BUNDLE "/opt/meetube/ssl/cert.pem")
else()
    set(MEETUBE_CA_BUNDLE "${OSSL_INSTALL}/ssl/cert.pem")
endif()
target_compile_definitions(meetube PRIVATE MEETUBE_CA_BUNDLE="${MEETUBE_CA_BUNDLE}")

# ---------------------------------------------------------------------------
# N9 packaging (device only): the freedesktop launcher entry + icons, and the
# `package` target that stages everything through debian/rules and builds the
# .deb with the Madde-wrapped dpkg-buildpackage. The app payload itself
# (binary, bundled OpenSSL/png/jpeg/webp, WebP plugin, CA bundle) is installed
# under /opt/meetube by the install() rules above and in deps/ + webp-imageformat/.
# ---------------------------------------------------------------------------
if(BUILD_N9)
    install(FILES resources/packaging/meetube_harmattan.desktop
            DESTINATION /usr/share/applications)
    install(FILES resources/packaging/meetube80.png
            DESTINATION /usr/share/icons/hicolor/80x80/apps)
    install(FILES resources/packaging/meetube64.png
            DESTINATION /usr/share/icons/hicolor/64x64/apps)

    # Copy debian/ into the build dir so dpkg-buildpackage (run from there) finds it.
    add_custom_target(copy_debian_dir
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/debian" "${CMAKE_BINARY_DIR}/debian"
        COMMENT "Copying debian/ into the build directory")

    # Build the .deb. debian/rules' install target re-enters
    # `cmake --build . --target install` with DESTDIR, so every install() rule
    # populates the package staging root. -nc keeps the already-built tree;
    # -uc -us skip signing (aegis still signs via mad). Run: make -C build-n9 package
    add_custom_target(package
        # dpkg-genchanges chokes on a stale debian/files referencing removed .debs;
        # drop it so dpkg regenerates a fresh list each run.
        COMMAND ${CMAKE_COMMAND} -E rm -f debian/files
        COMMAND /home/iarchep/QtSDK/Madde/bin/mad -t harmattan_10.2011.34-1_rt1.2 dpkg-buildpackage -nc -uc -us
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building the Debian package with mad..."
        DEPENDS meetube copy_debian_dir)
endif()
