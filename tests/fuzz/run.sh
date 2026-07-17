#!/usr/bin/env bash
# Build + run the libFuzzer/ASan/UBSan harness for Fmp4Demuxer (host-only).
#
#   tests/fuzz/run.sh [seconds]     # default 60; corpus/ grows persistently
#
# Needs clang (libFuzzer is clang-only; the app itself builds with GCC).
# Links the Simulator QtCore the demuxer is built against elsewhere — the
# path is hard-coded the same way ./configure hard-codes the SDK paths.
# ASan leak detection is off: Qt 4.7.4's global statics are noise, the
# target here is overreads/UB in the box parser.
set -euo pipefail
cd "$(dirname "$0")"
QT="/home/iarchep/QtSDK/Simulator/Qt/gcc"

clang++ -std=c++2b -g -O1 -fno-omit-frame-pointer \
  -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
  -I../../src/core -isystem "$QT/include" -isystem "$QT/include/QtCore" \
  fuzz_fmp4.cpp \
  ../../src/core/media/fmp4demux.cpp \
  ../../src/core/core/debuglog.cpp \
  -L"$QT/lib" -lQtCore -Wl,-rpath,"$QT/lib" \
  -o fuzz_fmp4

# Seed the working corpus from the versioned seeds/ (a real YouTube init, an
# init+sidx+moof slice, a synthetic mp4a stream) — corpus/ itself is gitignored
# scratch the fuzzer grows.
mkdir -p corpus
cp -n seeds/* corpus/ 2>/dev/null || true

ASAN_OPTIONS=detect_leaks=0 \
  ./fuzz_fmp4 corpus -max_total_time="${1:-60}" -max_len=262144 -rss_limit_mb=4096
