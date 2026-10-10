#!/usr/bin/env bash
# Build and run the Auto Run bench on the host (no Android SDK needed).
# Any arguments are passed to the bench, e.g.
#   scripts/autorun-bench.sh --runs 500 --levels --campaigns
#   scripts/autorun-bench.sh --width 2400 --height 1080     # landscape
# See app/src/main/cpp/test/autorun_bench.cpp for the options.
set -euo pipefail
cd "$(dirname "$0")/.."
cpp=app/src/main/cpp
out=build/autorun-bench
mkdir -p "$out"
cxx="${CXX:-c++}"
"$cxx" -std=c++17 -O2 -Wall -Wextra -I"$cpp" -I"$cpp/test/host" \
    "$cpp/test/autorun_bench.cpp" "$cpp/game.cpp" "$cpp/test/audio_stub.cpp" \
    -o "$out/autorun_bench"
exec "$out/autorun_bench" "$@"
