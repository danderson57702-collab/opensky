"""Old saves kept their follower as a number: its line in followers.txt - whose order changed each
time the map grew (people of the new cells joined the list). This writes, for each earlier map,
the list as it stood then - today's list without the followers who weren't on the map yet (the
list keeps the converter's order, so leaving those out gives the old one) - for the engine to
read an old save's number by its save's date.

  PACK_DIR/legacy/followers_eras.txt   per earlier map: "<made before (save date)>|<editor id>,..."

python3 tools/legacy_followers.py pack
"""
import math
import os
import struct
import sys

CELL_M = 4096 / 70.0
# (when each wider map's pack was made; its cells (x0, y0, x1, y1) and walled cities, from the
# git history of tools/reexport.py and converter/worlds.py)
ERAS = [("2026-10-21 21", (-14, -25, 19, 12), {"whiterun"}),
        ("2026-10-25 09", (-14, -25, 35, 24), {"whiterun"}),
        ("2026-10-28 06", (-22, -25, 35, 30), {"whiterun", "solitude", "windhelm"}),
        ("2026-10-29 06", (-35, -27, 45, 30), {"whiterun", "solitude", "windhelm", "riften"})]


def home(pack, eid, cities):
    """Where the person's day starts: (world name or None, cell x, cell y)."""
    d = open(os.path.join(pack, "actors", eid + ".sknpc"), "rb").read()
    o = 4 + 2 + 2 + 32
    nb = struct.unpack_from("<I", d, o)[0]
    o += 4 + nb * 16
    ns = struct.unpack_from("<I", d, o)[0]
    o += 4 + ns * 52
    world = d[o + 7]
    x, _y, z = struct.unpack_from("<3f", d, o + 8)
    name = cities[world - 1][0] if 0 < world <= len(cities) else None
    if name:  # (inside a walled city: that city's first cell)
        cx, cy = map(int, cities[world - 1][1].split(","))
        return name, cx, cy
    return None, math.floor(x / CELL_M), math.floor(-z / CELL_M)


def main(pack):
    cities = []
    for line in open(os.path.join(pack, "worlds.txt")):
        f = line.split()
        if len(f) >= 3 and f[0] != "=" and f[2] != "!":
            cities.append((f[0], f[2]))
    order = [l.split("|")[1] for l in open(os.path.join(pack, "followers.txt")) if l.startswith("F|")]
    out = []
    for date, (x0, y0, x1, y1), had in ERAS:
        kept = []
        for eid in order:
            world, cx, cy = home(pack, eid, cities)
            if (world is None or world in had) and x0 <= cx <= x1 and y0 <= cy <= y1:
                kept.append(eid)
        out.append(f"{date}|{','.join(kept)}")
        print(date, len(kept), kept[:6])
    os.makedirs(os.path.join(pack, "legacy"), exist_ok=True)
    with open(os.path.join(pack, "legacy", "followers_eras.txt"), "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "pack")
