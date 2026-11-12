#!/bin/bash
# Copies the converted pack into Vita3K's emulated ux0:data/opensky/.
# (On a real Vita, copy pack/ to ux0:data/opensky/ over FTP/USB with VitaShell.)
set -e
cd "$(dirname "$0")"
DEST="$HOME/Library/Application Support/Vita3K/Vita3K/fs/ux0/data/opensky"
mkdir -p "$DEST"
rsync -a --delete --exclude '_preview' pack/ "$DEST/"
echo "Pack deployed to Vita3K ux0:data/opensky/"
find "$DEST" -type f | sed "s|$DEST/|  |"
