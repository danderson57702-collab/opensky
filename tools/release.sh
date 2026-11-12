#!/bin/bash
# The beta's program, stamped with the release's name (the title screen shows it): dist/opensky.vpk
# (git-ignored), and what's in it listed - the program, its parameters and its own icon, nothing else.
#   tools/release.sh beta-1
set -e
cd "$(dirname "$0")/.."
NAME="${1:?usage: tools/release.sh beta-1}"
sed -i '' "s/^#define BUILD_STAMP .*/#define BUILD_STAMP \"$NAME\"/" engine/src/buildstamp.h
(cd engine/build && make -j8 > /tmp/opensky-make.log 2>&1) || { grep error /tmp/opensky-make.log | head; echo "BUILD FAILED"; exit 1; }
mkdir -p dist
cp engine/build/opensky.vpk dist/opensky.vpk
sed -i '' "s/^#define BUILD_STAMP .*/#define BUILD_STAMP \"dev\"/" engine/src/buildstamp.h  # (the repo keeps "dev")
unzip -l dist/opensky.vpk
echo "dist/opensky.vpk: build $NAME"
