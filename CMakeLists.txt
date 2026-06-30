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

# The N9 device build links the app as a PIE (-pie, required by the
# qdeclarative booster), so every object linked into it must be
# position-independent. Make all targets PIC/PIE.
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
# libpng12 — host Simulator build only.
#
# The host Qt4 Simulator libraries (libQtGui.so) are linked against libpng12
# and reference versioned symbols (png_*@PNG12_0). Those versioned symbols come
# only from libpng's autotools build, and modern hosts ship only libpng16.
#
# The N9 device links libpng12 from the device sysroot, so this is skipped there.
# ---------------------------------------------------------------------------
if(NOT BUILD_N9)
    set(LIBPNG12_INSTALL ${CMAKE_BINARY_DIR}/libpng12-install)
    set(LIBPNG12_SO ${LIBPNG12_INSTALL}/lib/libpng12.so.0)

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

    file(MAKE_DIRECTORY ${LIBPNG12_INSTALL}/include)
    add_library(png12 SHARED IMPORTED GLOBAL)
    set_target_properties(png12 PROPERTIES
        IMPORTED_LOCATION             ${LIBPNG12_SO}
        INTERFACE_INCLUDE_DIRECTORIES ${LIBPNG12_INSTALL}/include)
endif()

# ---------------------------------------------------------------------------
# libjpeg.so.62 — host Simulator build only.
#
# The host Qt4 Simulator's JPEG image plugin is linked against the old IJG
# libjpeg v6b ABI (libjpeg.so.62). Modern hosts ship only libjpeg.so.8.
# ---------------------------------------------------------------------------
if(NOT BUILD_N9)
    set(LIBJPEG62_INSTALL ${CMAKE_BINARY_DIR}/libjpeg62-install)
    set(LIBJPEG62_SO ${LIBJPEG62_INSTALL}/lib/libjpeg.so.62)
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
# OpenSSL 1.0.2 — both targets. Modern TLS without rebuilding QtNetwork.
#
# Qt 4.7.4 dlopen's OpenSSL at runtime and is ABI-compatible with the 1.0.x
# branch only. We build 1.0.2u and bundle it; Qt picks it up via LD_LIBRARY_PATH.
# ---------------------------------------------------------------------------
set(OSSL_INSTALL ${CMAKE_BINARY_DIR}/openssl-install)
set(OSSL_SSL    ${OSSL_INSTALL}/lib/libssl.so.1.0.0)
set(OSSL_CRYPTO ${OSSL_INSTALL}/lib/libcrypto.so.1.0.0)
if(BUILD_N9)
    set(OSSL_TARGET linux-armv4)
    string(REGEX REPLACE "g\\+\\+$" "" OSSL_CROSS "${CMAKE_CXX_COMPILER}")
    set(OSSL_CFG --cross-compile-prefix=${OSSL_CROSS} --sysroot=${CMAKE_SYSROOT})
else()
    set(OSSL_TARGET linux-x86_64)
    set(OSSL_CFG "")
endif()
ExternalProject_Add(openssl102
    SOURCE_DIR        ${CMAKE_SOURCE_DIR}/deps/openssl
    DOWNLOAD_COMMAND  ""
    BUILD_IN_SOURCE   1
    CONFIGURE_COMMAND <SOURCE_DIR>/Configure ${OSSL_TARGET} shared no-asm
                      --prefix=${OSSL_INSTALL} ${OSSL_CFG}
    BUILD_COMMAND     make depend
            COMMAND   make build_libs
    INSTALL_COMMAND   ${CMAKE_COMMAND} -E make_directory ${OSSL_INSTALL}/lib
            COMMAND   ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/libssl.so.1.0.0 ${OSSL_INSTALL}/lib/
            COMMAND   ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/libcrypto.so.1.0.0 ${OSSL_INSTALL}/lib/
            COMMAND   ${CMAKE_COMMAND} -DLIBDIR=${OSSL_INSTALL}/lib -P ${CMAKE_SOURCE_DIR}/tools/make_soname_links.cmake
    BUILD_BYPRODUCTS  ${OSSL_SSL} ${OSSL_CRYPTO}
)

# ---------------------------------------------------------------------------
# libwebp — N9 device only (cross armel).
# ---------------------------------------------------------------------------
if(BUILD_N9)
    set(LIBWEBP_INSTALL ${CMAKE_BINARY_DIR}/libwebp-install)
    set(LIBWEBP_SO ${LIBWEBP_INSTALL}/lib/libwebp.so)
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
# qrcodegen — C++ wrapper (static lib, both targets).
# ---------------------------------------------------------------------------
add_library(qrcodegen STATIC ${CMAKE_SOURCE_DIR}/deps/qrcodegen/cpp/qrcodegen.cpp)
set_target_properties(qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(qrcodegen PUBLIC ${CMAKE_SOURCE_DIR}/deps/qrcodegen/cpp)

# ---------------------------------------------------------------------------
# Application: meetube executable
# ---------------------------------------------------------------------------
add_executable(meetube
    src/main.cpp
)

target_link_libraries(meetube PRIVATE
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

# Install prefix: /opt/meetube
install(TARGETS meetube
    RUNTIME DESTINATION /opt/meetube/bin
)
