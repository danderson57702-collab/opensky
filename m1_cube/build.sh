#!/bin/bash
# Builds m1_cube.vpk into ./build
set -e
export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$HOME/Library/Python/3.9/bin:$PATH"
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
