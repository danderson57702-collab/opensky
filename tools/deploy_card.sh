#!/bin/bash
# Puts the program and its data on the Vita's card as a pair: stamps both with the same build
# stamp (engine/src/buildstamp.h, pack/build.txt), builds, copies what changed, checks it, and
# leaves the card ready to eject. The title screen shows the stamp; a program that finds another
# stamp in its data says the .vpk has to be installed again (VitaShell).
#   tools/deploy_card.sh [/Volumes/Untitled]
set -e
cd "$(dirname "$0")/.."
CARD="${1:-/Volumes/Untitled}"
DATA_DIR="$CARD/data/opensky"
if [ ! -d "$DATA_DIR" ]; then
	# (the program reads only data/opensky: a pack copied to the old data/sklt would never be found)
	if [ -d "$CARD/data/sklt" ]; then
		echo "the card has the old data/sklt but no data/opensky: rename it (VitaShell or the Mac) and run this again"; exit 1
	else
		echo "no card data directory found at $CARD/data/opensky"; exit 1
	fi
fi
STAMP="$(date +%m%d-%H%M)"
sed -i '' "s/^#define BUILD_STAMP .*/#define BUILD_STAMP \"$STAMP\"/" engine/src/buildstamp.h
(cd engine/build && make -j8 > /tmp/opensky-make.log 2>&1) || { grep error /tmp/opensky-make.log | head; echo "BUILD FAILED"; exit 1; }
echo "$STAMP" > pack/build.txt
rm -f "$DATA_DIR/autotest.txt"
python3 tools/card_copy.py pack "$DATA_DIR" # (rsync - macOS's openrsync - hung on the card mid-file)
cp engine/build/opensky.vpk "$DATA_DIR/opensky.vpk"
# The installed program too (a copied VPK alone runs nothing new till it's installed): the
# last one kept beside the data
if [ -f "$CARD/app/OSKY00001/eboot.bin" ]; then
	cp "$CARD/app/OSKY00001/eboot.bin" "$CARD/data/opensky_eboot_previous.bin"
	unzip -p engine/build/opensky.vpk eboot.bin > "$CARD/app/OSKY00001/eboot.bin"
	unzip -p engine/build/opensky.vpk eboot.bin | cmp - "$CARD/app/OSKY00001/eboot.bin" && echo "card: the installed program updated"
fi
cmp engine/build/opensky.vpk "$DATA_DIR/opensky.vpk" && cmp pack/build.txt "$DATA_DIR/build.txt" && echo "card: build $STAMP (vpk and data match)"
dot_clean -m "$CARD" 2>/dev/null || true
sed -i "" "s/^#define BUILD_STAMP .*/#define BUILD_STAMP \"dev\"/" engine/src/buildstamp.h  # (the repo keeps "dev": a stamp is a deploy's)
