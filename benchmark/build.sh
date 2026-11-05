#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-benchmark --parallel 2
