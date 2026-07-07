# ---------------------------------------------------------------------------
# deps/ — bundled third-party dependencies.
#
# This subdirectory owns ALL the external-library build blocks (libpng12,
# libjpeg-turbo, libwebp) plus the qrcodegen static lib and the imported png12
# target. It was extracted from the root CMakeLists to keep the root lean.
#
# SCOPING NOTE: add_subdirectory() runs this file in a CHILD scope. The custom
# targets defined here (libpng12_build, libjpeg62_build, libwebp_build,
# qrcodegen) and the IMPORTED GLOBAL png12 target are globally
# referenceable by name. But plain variables set here are NOT visible to the
# root or to webp-imageformat/. Every variable consumed outside deps/ is
# therefore exported with `CACHE INTERNAL ""` so it is globally visible.
# ---------------------------------------------------------------------------

include(ExternalProject)
find_package(Git REQUIRED)

# ---------------------------------------------------------------------------
# libpng12 — both targets.
#
# Host: the Qt4 Simulator libraries (libQtGui.so) are linked against libpng12
# and reference versioned symbols (png_*@PNG12_0). Those versioned symbols come
# only from libpng's autotools build, and modern hosts ship only libpng16.
#
# Device (N9): a fresh, ABI-compatible libpng12.so.0 cross-built for armv7hf
# and bundled into /opt/meetube/lib (the app prefers it via DT_RPATH), instead
# of the device sysroot's stock copy. zlib comes from the device sysroot.
# ---------------------------------------------------------------------------
set(LIBPNG12_INSTALL ${CMAKE_BINARY_DIR}/libpng12-install CACHE INTERNAL "")
set(LIBPNG12_SO ${LIBPNG12_INSTALL}/lib/libpng12.so.0 CACHE INTERNAL "")

if(BUILD_N9)
    # Cross-build for armv7hf. Derive the cross C compiler from the C++ one like
    # the openssl/libwebp blocks do. --host only puts configure in cross mode;
    # the real compiler is CC. The sysroot flags make configure find the device
    # zlib (headers + libz.so under the sysroot).
    string(REGEX REPLACE "g\\+\\+$" "gcc" PNG_CC "${CMAKE_CXX_COMPILER}")
    ExternalProject_Add(libpng12_build
        SOURCE_DIR        ${CMAKE_SOURCE_DIR}/deps/libpng12
        DOWNLOAD_COMMAND  ""
        CONFIGURE_COMMAND ${CMAKE_SOURCE_DIR}/deps/libpng12/configure
                          CC=${PNG_CC}
                          --host=arm-linux-gnueabi
                          CPPFLAGS=--sysroot=${CMAKE_SYSROOT}
                          CFLAGS=--sysroot=${CMAKE_SYSROOT}
                          LDFLAGS=--sysroot=${CMAKE_SYSROOT}
                          --prefix=${LIBPNG12_INSTALL}
                          --enable-shared --disable-static
                          --disable-dependency-tracking
        BUILD_COMMAND     make
        INSTALL_COMMAND   make install
        BUILD_BYPRODUCTS  ${LIBPNG12_SO}
    )
else()
    ExternalProject_Add(libpng12_build
        SOURCE_DIR        ${CMAKE_SOURCE_DIR}/deps/libpng12
        DOWNLOAD_COMMAND  ""
        CONFIGURE_COMMAND ${CMAKE_SOURCE_DIR}/deps/libpng12/configure
                          --prefix=${LIBPNG12_INSTALL}
                          --enable-shared --disable-static
                          --disable-dependency-tracking
        BUILD_COMMAND     make
        INSTALL_COMMAND   make install
        BUILD_BYPRODUCTS  ${LIBPNG12_SO}
    )
endif()

file(MAKE_DIRECTORY ${LIBPNG12_INSTALL}/include)
add_library(png12 SHARED IMPORTED GLOBAL)
set_target_properties(png12 PROPERTIES
    IMPORTED_LOCATION             ${LIBPNG12_SO}
    INTERFACE_INCLUDE_DIRECTORIES ${LIBPNG12_INSTALL}/include)

# ---------------------------------------------------------------------------
# libjpeg.so.62 (libjpeg-turbo) — both targets.
#
# Host: the Qt4 Simulator's JPEG image plugin is linked against the old IJG
# libjpeg v6b ABI (libjpeg.so.62); modern hosts ship only libjpeg.so.8.
#
# Device (N9): a fresh, ABI-compatible libjpeg.so.62 cross-built for armv7hf
# and bundled into /opt/meetube/lib (preferred via DT_RPATH) instead of the
# device sysroot's stock copy. The IJG v6b soname (62) is libjpeg-turbo's
# default, so the device build yields libjpeg.so.62 just like the host build.
# ---------------------------------------------------------------------------
set(LIBJPEG62_INSTALL ${CMAKE_BINARY_DIR}/libjpeg62-install CACHE INTERNAL "")
set(LIBJPEG62_SO ${LIBJPEG62_INSTALL}/lib/libjpeg.so.62 CACHE INTERNAL "")

if(BUILD_N9)
    string(REGEX REPLACE "g\\+\\+$" "gcc" JPEG_CC "${CMAKE_CXX_COMPILER}")
    ExternalProject_Add(libjpeg62_build
        SOURCE_DIR       ${CMAKE_SOURCE_DIR}/deps/libjpeg-turbo
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS  -DCMAKE_INSTALL_PREFIX=${LIBJPEG62_INSTALL}
                    -DCMAKE_INSTALL_LIBDIR=lib
                    -DCMAKE_BUILD_TYPE=Release
                    -DENABLE_STATIC=OFF -DENABLE_SHARED=ON
                    -DWITH_TURBOJPEG=OFF -DWITH_SIMD=OFF
                    -DCMAKE_SYSTEM_NAME=Linux
                    -DCMAKE_SYSTEM_PROCESSOR=arm
                    -DCMAKE_C_COMPILER=${JPEG_CC}
                    -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
                    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
                    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
                    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        BUILD_BYPRODUCTS ${LIBJPEG62_SO}
    )
else()
    ExternalProject_Add(libjpeg62_build
        SOURCE_DIR       ${CMAKE_SOURCE_DIR}/deps/libjpeg-turbo
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS  -DCMAKE_INSTALL_PREFIX=${LIBJPEG62_INSTALL}
                    -DCMAKE_INSTALL_LIBDIR=lib
                    -DCMAKE_BUILD_TYPE=Release
                    -DENABLE_STATIC=OFF -DENABLE_SHARED=ON
                    -DWITH_TURBOJPEG=OFF -DWITH_SIMD=OFF
        BUILD_BYPRODUCTS ${LIBJPEG62_SO}
    )
endif()

# ---------------------------------------------------------------------------
# Latest CA root bundle (Mozilla trust store, published by the curl project).
# Downloaded fresh each configure, TLS-verified + checksum-verified, and copied
# into a standalone build-tree ssl/ dir (${MEETUBE_SSL_DIR}/cert.pem).
# The app no longer uses Qt's TLS: HTTPS to YouTube goes through libcurl, which
# reads this bundle via CURLOPT_CAINFO (seeded from the MEETUBE_CA_BUNDLE define).
# ---------------------------------------------------------------------------
set(CACERT_PEM ${CMAKE_BINARY_DIR}/cacert.pem CACHE INTERNAL "")
file(DOWNLOAD https://curl.se/ca/cacert.pem.sha256 ${CMAKE_BINARY_DIR}/cacert.pem.sha256
     TLS_VERIFY ON STATUS _ca_sha_st)
list(GET _ca_sha_st 0 _ca_sha_rc)
if(_ca_sha_rc EQUAL 0)
    file(READ ${CMAKE_BINARY_DIR}/cacert.pem.sha256 _ca_sha_txt)
    string(REGEX MATCH "[0-9a-fA-F]+" _ca_sha "${_ca_sha_txt}")
    file(DOWNLOAD https://curl.se/ca/cacert.pem ${CACERT_PEM}
         TLS_VERIFY ON EXPECTED_HASH SHA256=${_ca_sha} STATUS _ca_st)
else()
    file(DOWNLOAD https://curl.se/ca/cacert.pem ${CACERT_PEM} TLS_VERIFY ON STATUS _ca_st)
endif()
list(GET _ca_st 0 _ca_rc)
if(NOT _ca_rc EQUAL 0)
    if(EXISTS ${CACERT_PEM})
        message(WARNING "CA bundle download failed (${_ca_st}); reusing cached ${CACERT_PEM}")
    else()
        message(FATAL_ERROR "Could not download the CA bundle from curl.se: ${_ca_st}")
    endif()
endif()
# Copy into a standalone build-tree ssl/ dir (was the OpenSSL install ssl/ tree,
# which is gone now that the 1.0.2 build has been removed).
set(MEETUBE_SSL_DIR ${CMAKE_BINARY_DIR}/ssl CACHE INTERNAL "")
file(MAKE_DIRECTORY ${MEETUBE_SSL_DIR})
configure_file(${CACERT_PEM} ${MEETUBE_SSL_DIR}/cert.pem COPYONLY)

if(BUILD_N9)
    install(FILES ${CACERT_PEM} DESTINATION /opt/meetube/ssl RENAME cert.pem)
endif()

# ---------------------------------------------------------------------------
# libwebp — N9 device only (cross armv7hf, hard-float).
# ---------------------------------------------------------------------------
if(BUILD_N9)
    set(LIBWEBP_INSTALL ${CMAKE_BINARY_DIR}/libwebp-install CACHE INTERNAL "")
    set(LIBWEBP_SO ${LIBWEBP_INSTALL}/lib/libwebp.so CACHE INTERNAL "")
    string(REGEX REPLACE "g\\+\\+$" "gcc" LIBWEBP_CC "${CMAKE_CXX_COMPILER}")
    ExternalProject_Add(libwebp_build
        SOURCE_DIR       ${CMAKE_SOURCE_DIR}/deps/libwebp
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS  -DCMAKE_INSTALL_PREFIX=${LIBWEBP_INSTALL}
                    -DCMAKE_INSTALL_LIBDIR=lib
                    -DCMAKE_BUILD_TYPE=Release
                    -DCMAKE_SYSTEM_NAME=Linux
                    -DCMAKE_SYSTEM_PROCESSOR=arm
                    -DCMAKE_C_COMPILER=${LIBWEBP_CC}
                    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                    -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
                    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
                    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
                    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
                    -DBUILD_SHARED_LIBS=ON
                    -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF
                    -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF
                    -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF
                    -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF
                    -DWEBP_BUILD_EXTRAS=OFF
        BUILD_BYPRODUCTS ${LIBWEBP_SO}
    )
endif()

# ---------------------------------------------------------------------------
# Bundle the fresh libpng12 + libjpeg into /opt/meetube/lib (device only).
#
# Non-destructive — mirrors how OpenSSL/libwebp are bundled. We do NOT overwrite
# the device's /usr/lib copies; the app's DT_RPATH (/opt/meetube/lib, set on the
# meetube target) makes these win at load time, including for the Qt image
# plugins that pull in libpng12.so.0 / libjpeg.so.62 transitively.
# ---------------------------------------------------------------------------
if(BUILD_N9)
    # EXCLUDE the cmake/ + pkgconfig/ subtrees: FILES_MATCHING still recreates every
    # traversed directory, so without these the package would carry empty
    # lib/cmake/… and lib/pkgconfig/ dirs.
    install(DIRECTORY ${LIBPNG12_INSTALL}/lib/ DESTINATION /opt/meetube/lib
            FILES_MATCHING PATTERN "libpng12.so*"
            PATTERN "cmake" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
    install(DIRECTORY ${LIBJPEG62_INSTALL}/lib/ DESTINATION /opt/meetube/lib
            FILES_MATCHING PATTERN "libjpeg.so*"
            PATTERN "cmake" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
endif()

# ---------------------------------------------------------------------------
# qrcodegen — C++ wrapper (static lib, both targets).
# ---------------------------------------------------------------------------
add_library(qrcodegen STATIC ${CMAKE_SOURCE_DIR}/deps/qrcodegen/cpp/qrcodegen.cpp)
set_target_properties(qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(qrcodegen PUBLIC ${CMAKE_SOURCE_DIR}/deps/qrcodegen/cpp)

# ---------------------------------------------------------------------------
# Glaze — header-only JSON engine (both targets), submodule pinned at v7.8.4.
#
# Replaces nlohmann/json in meetube-core. Requires C++23 (set globally in the
# root — Qt 4.7 headers verified clean at -std=c++23 on both toolchains).
# SYSTEM include: Glaze's headers are not this project's warning surface.
# ---------------------------------------------------------------------------
add_library(glaze INTERFACE)
target_include_directories(glaze SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/deps/glaze/include)
