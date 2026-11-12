"""Skyrim's standing stones in the region: the Doomstone activators placed outdoors (the Guardian
Stones by Riverwood: the Warrior, the Thief, the Mage; the Lady, the Ritual...).

PACK_DIR/stones.txt, one a line: editor id, name, x, y, z (engine space, metres) - the engine
gives the Guardian Stones' blessings (engine/src/stones.c).
"""
import os
import re
import struct

UNITS_PER_METER = 70.0


def export(lo, cells, pack, bounds, strings):
    min_x, min_y, max_x, max_y = bounds
    out = []
    for (cx, cy), c in cells.items():
        if not (min_x <= cx <= max_x and min_y <= cy <= max_y):
            continue
        for r in c["refs"]:
            name, data = r.sub("NAME"), r.sub("DATA")
            if not name or not data:
                continue
            base = lo.record(lo.global_id(r.plugin, struct.unpack_from("<I", name)[0]), types=("ACTI",))
            if base is None or not (base.editor_id() or "").lower().startswith("doomstone"):
                continue
            x, y, z = struct.unpack_from("<3f", data)
            full = base.sub("FULL")
            title = strings.get(base.plugin, struct.unpack_from("<I", full)[0], "strings") if full and len(full) == 4 \
                else base.editor_id()
            out.append(f"{base.editor_id()}\t{title}\t{x / UNITS_PER_METER:.2f}\t{z / UNITS_PER_METER:.2f}\t"
                       f"{-y / UNITS_PER_METER:.2f}\n")
    with open(os.path.join(pack, "stones.txt"), "w") as f:
        f.write("".join(out))
    print(f"stones: {len(out)}")


def export_walls(lo, cells, inside, interior_index, pack, bounds):
    """PACK_DIR/walls.txt: Skyrim's word walls - the word each teaches (its activator's editor
    id: NorWordWallWordFus), where (-1 outdoors, else the interior's index) and its place; the
    engine teaches the word to whoever comes up to it (engine/src/stones.c, shouts.c)."""
    min_x, min_y, max_x, max_y = bounds
    out = []

    def consider(r, place):
        name, data = r.sub("NAME"), r.sub("DATA")
        if not name or not data:
            return
        base = lo.record(lo.global_id(r.plugin, struct.unpack_from("<I", name)[0]), types=("ACTI",))
        eid = (base.editor_id() or "") if base is not None else ""
        at = eid.lower().find("wordwallword")
        if at < 0:
            return
        word = "".join(c for c in eid[at + len("wordwallword"):] if c.isalpha())
        x, y, z = struct.unpack_from("<3f", data)
        for w in re.findall(r"[A-Z][a-z]*", word) or [word]:  # (one wall's three words in one id: "GutMeyZul", Throw Voice's)
            out.append(f"{w}\t{place}\t{x / UNITS_PER_METER:.2f}\t{z / UNITS_PER_METER:.2f}\t{-y / UNITS_PER_METER:.2f}\n")

    for (cx, cy), c in cells.items():
        if min_x <= cx <= max_x and min_y <= cy <= max_y:
            for r in c["refs"]:
                consider(r, -1)
    for cg, idx in (interior_index or {}).items():
        for r in inside.inner[cg]["refs"]:
            consider(r, idx)
    out = _keep_wall_order(os.path.join(pack, "walls.txt"), out)
    with open(os.path.join(pack, "walls.txt"), "w") as f:
        f.write("".join(out))
    print(f"word walls: {len(out)}")


def _keep_wall_order(path, lines):
    """The walls already in walls.txt first, in their order there (a save keeps the walls you've
    used by their number in it - 10-21's widened map put new ones in between, and the old file's
    interior numbers were stale), then the new ones; a wall is its first word and place."""
    def key(line):
        f = line.split("\t")
        return f[0], round(float(f[2])), round(float(f[3])), round(float(f[4]))
    try:
        old = [l for l in open(path) if l.strip()]
    except OSError:
        return lines
    walls, order = {}, []  # (a wall's lines: those at one place)
    for l in lines:
        f = l.split("\t")
        at = (round(float(f[2])), round(float(f[3])), round(float(f[4])))
        if at not in walls:
            order.append(at)
        walls.setdefault(at, []).append(l)
    out, used = [], set()
    for l in old:
        f = l.split("\t")
        at = (round(float(f[2])), round(float(f[3])), round(float(f[4])))
        if at in walls and at not in used:
            out += walls[at]
            used.add(at)
    for at in order:
        if at not in used:
            out += walls[at]
    return out
