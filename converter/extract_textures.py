#!/usr/bin/env python3
"""Pull named textures out of the Skyrim SE BSAs and write .sktex files to the pack.

usage: extract_textures.py OUT_DIR NAME=textures\\path\\file.dds[@alpha][@gain=1.6] [NAME=... ...]
"""
import glob
import os
import sys

from bsa import BSA
from textures import convert

def _game_data_dir():
    """The Data folder of the player's own installed copy (Steam or GOG): from $SKYRIM_DATA, or the
    one line of converter/data_path.txt (git-ignored - each user's own). Nothing is converted
    without a retail install there: its master file, and the store's own files beside the game."""
    if os.environ.get("OPENSKY_SELFTEST"):  # (the data tool's check of itself: no game, and nothing to convert)
        return None
    path = os.environ.get("SKYRIM_DATA")
    local = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data_path.txt")
    if not path and os.path.exists(local):
        path = open(local).read().strip()
    if not path:
        sys.exit("Point the converter at your own installed copy's Data folder: set SKYRIM_DATA, or put "
                 "its path in converter/data_path.txt")
    game = os.path.dirname(os.path.normpath(path))
    steam = os.path.exists(os.path.join(game, "steam_api64.dll")) and os.path.exists(os.path.join(game, "SkyrimSE.exe"))
    gog = bool(glob.glob(os.path.join(game, "goggame-*.info")))
    if not os.path.exists(os.path.join(path, "Skyrim.esm")) or not (steam or gog):
        sys.exit(f"{path}: not the Data folder of a Steam or GOG install (its master file and the store's "
                 "own files beside the game are needed)")
    return path


SKYRIM_DATA = _game_data_dir()


def open_texture_archives():
    return [BSA(p) for p in sorted(glob.glob(os.path.join(SKYRIM_DATA, "Skyrim - Textures*.bsa")))]


def find(archives, path):
    key = path.lower().replace("/", "\\")
    for a in archives:
        if key in a.entries:
            return a.read(key)
    raise KeyError(f"{path} not found in texture archives")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    out_dir = argv[1]
    os.makedirs(out_dir, exist_ok=True)
    preview_dir = os.path.join(out_dir, "_preview")
    os.makedirs(preview_dir, exist_ok=True)

    archives = open_texture_archives()
    for spec in argv[2:]:
        name, rest = spec.split("=", 1)
        path, *opts = rest.split("@")
        keep_alpha = "alpha" in opts
        gain = next((float(o[5:]) for o in opts if o.startswith("gain=")), 1.0)
        sktex, preview = convert(find(archives, path), keep_alpha=keep_alpha, gain=gain)
        with open(os.path.join(out_dir, name + ".sktex"), "wb") as f:
            f.write(sktex)
        preview.save(os.path.join(preview_dir, name + ".png"))
        print(f"{name}: {path} -> {preview.width}x{preview.height}, {len(sktex)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
