#!/usr/bin/env python3
"""Whether each door of an interior can reach its others along Skyrim's navmesh (the pack's .sknav),
offline: the navmesh's connected pieces, and which piece each door stands on. Doors on different
pieces are a way the game may not let you take (a drop is often linked one way only - look before
calling it a blocker).

    python3 tools/door_reach.py [pack] interior ...
"""
import os
import struct
import sys
from math import hypot


def doors_of(path):
    d = open(path, "rb").read()
    o = 12
    name_len = d[8]
    o = 9 + name_len
    o += -o % 4
    o += 4 * 11  # (fog rgb, near, far, bounds min, max)
    n = struct.unpack_from("<I", d, o)[0]
    o += 4
    out = []
    for _ in range(n):
        x, y, z, dest = struct.unpack_from("<3fi", d, o)
        o += 16 + 16 + 16  # (arrival xyz yaw, centre xyz radius)
        ll = d[o]
        label = d[o + 1:o + 1 + ll].decode("cp1252", "replace")
        o += 1 + ll
        o += -o % 4
        out.append((label, (x, y, z), dest))
    return out


def pieces(path):
    d = open(path, "rb").read()
    _m, _v, _z, _cx, _cy, nv, nt = struct.unpack_from("<4sHHiiII", d, 0)
    o = 24
    verts = [struct.unpack_from("<3f", d, o + 12 * i) for i in range(nv)]
    o += 12 * nv
    parent = list(range(nt))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    tris = []
    for t in range(nt):
        v0, v1, v2, _f, n0, n1, n2 = struct.unpack_from("<4H3I", d, o + 20 * t)
        tris.append((v0, v1, v2))
        for n in (n0, n1, n2):
            if n != 0xFFFFFFFF:
                a, b = find(t), find(n & 0xFFFFFF)
                if a != b:
                    parent[a] = b
    cent = [tuple(sum(verts[v][k] for v in tri) / 3 for k in range(3)) for tri in tris]
    return cent, [find(t) for t in range(nt)]


def main():
    args = sys.argv[1:]
    pack = args.pop(0) if args and os.path.isdir(args[0]) else os.path.join(os.path.dirname(__file__), "..", "pack")
    for name in args:
        base = os.path.join(pack, "interiors", name)
        doors = doors_of(base + ".skint")
        cent, comp = pieces(base + ".sknav")
        sizes = {}
        for c in comp:
            sizes[c] = sizes.get(c, 0) + 1
        res = []
        for label, (x, y, z), _dest in doors:
            best, bc = 1e9, None
            for (cx, cy, cz), c in zip(cent, comp):
                dd = hypot(cx - x, cz - z) + abs(cy - y) * 2
                if dd < best:
                    best, bc = dd, c
            res.append((label, (x, y, z), bc, best))
        groups = {}
        for r in res:
            groups.setdefault(r[2], []).append(r)
        verdict = "one piece" if len(groups) == 1 else f"{len(groups)} PIECES"
        print(f"{name}: {len(doors)} doors, navmesh {len(cent)} triangles, {verdict}")
        if len(groups) > 1:
            for c, rs in groups.items():
                print(f"   piece of {sizes[c]} triangles: " + "; ".join(f"'{l}' ({p[0]:.0f},{p[1]:.0f},{p[2]:.0f}){' far ' + format(b, '.0f') + ' m' if b > 6 else ''}" for l, p, _c, b in rs))


if __name__ == "__main__":
    main()
