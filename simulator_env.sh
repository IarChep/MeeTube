#!/usr/bin/env bash
# Source this file in your shell before running the simulator build:
#   source simulator_env.sh

QTSIM_ROOT="/home/iarchep/QtSDK/Simulator/Qt/gcc"
# QtMobility (Simulator) provides the QtMultimediaKit QML module; its
# libdeclarative_multimedia.so links libQtMobilitySimulator.so.1, which lives
# here (not under Qt/gcc/lib), so it must be on the library path.
QTMOBILITY_LIB="/home/iarchep/QtSDK/Simulator/QtMobility/gcc/lib"

export QTDIR="$QTSIM_ROOT"
export QMAKE="$QTSIM_ROOT/bin/qmake"
export PATH="$QTSIM_ROOT/bin:$PATH"
export QML_IMPORT_PATH="$QTSIM_ROOT/imports"
export QT_PLUGIN_PATH="$QTSIM_ROOT/plugins"
# Bundled shims the simulator's Qt 4.7.4 needs but modern hosts no longer ship:
#   libpng12       - libQtGui needs PNG12_0-versioned symbols
#   libjpeg.so.62  - the qjpeg image plugin needs the old IJG v6b ABI (else NO JPEG
#                    decoding -> all YouTube thumbnails "Unsupported image format")
# (TLS is no longer a shim: HTTPS goes through the system libcurl + its OpenSSL 3.)
# libwebp: on the simulator the host /usr/lib libwebp is used (default loader path);
#          the N9 device build bundles libwebp in /opt/meetube/lib instead.
# All shims above are built under the simulator build dir.
export LD_LIBRARY_PATH="$QTSIM_ROOT/lib:$QTMOBILITY_LIB:$(pwd)/build-sim/libpng12-install/lib:$(pwd)/build-sim/libjpeg62-install/lib:${LD_LIBRARY_PATH:-}"

echo "Qt Simulator env loaded: QMAKE=$QMAKE"
