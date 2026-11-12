"""The way across Tamriel, coarse: where each cell's walkable ground (its navmesh, from the
.sknav files navmesh.py wrote) crosses into the next, and which crossings the same ground
joins. The engine's navmesh is only loaded about you; this graph is how Clairvoyance finds
the way to a far objective - up the 7000 Steps, not the cliff beside them - and aims the
nearby path at the right crossing.

  PACK_DIR/navmesh/ways.txt
    N x y z            a crossing (engine metres): its node number is its line among the N lines
    E a b cost         walkable between crossings a and b, this far (metres; along Skyrim's
                       preferred triangles - its roads - counted shorter, as its pathing does)

python3 waygraph.py ../pack
"""
import glob
import math
import os
import struct
import sys

PREFERRED = 0x40     # Skyrim's navmesh triangle flag: a road, the way its pathing prefers
ROAD_SHARE = 0.75    # a stretch between crossings both on roads: counted this much of its length
NONE = 0xFFFFFFFF


def _load(path):
    d = open(path, "rb").read()
    _magic, _ver, _pad, cx, cy, nv, nt = struct.unpack_from("<4sHHiiII", d)
    verts = [struct.unpack_from("<3f", d, 24 + i * 12) for i in range(nv)]
    o = 24 + nv * 12
    tris = [struct.unpack_from("<3HH3I", d, o + i * 20) for i in range(nt)]
    return (cx, cy), verts, tris


def _components(tris):
    """Each triangle's island within its cell (joined by same-cell links)."""
    parent = list(range(len(tris)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t, tri in enumerate(tris):
        for n in tri[4:7]:
            if n != NONE and (n >> 24) & 3 == 1 and (n >> 26) & 3 == 1:
                a, b = find(t), find(n & 0xFFFFFF)
                if a != b:
                    parent[a] = b
    return [find(t) for t in range(len(tris))]


def build(pack):
    cells = {}
    for f in glob.glob(os.path.join(pack, "navmesh", "tamriel_*.sknav")):
        cell, verts, tris = _load(f)
        cells[cell] = (verts, tris, _components(tris))
    crossings = {}  # {((cell, comp), (cell, comp)) sorted: [sum x, y, z, count, preferred count]}
    for cell, (verts, tris, comp) in cells.items():
        for t, tri in enumerate(tris):
            for k, n in enumerate(tri[4:7]):
                if n == NONE:
                    continue
                dx, dy = ((n >> 24) & 3) - 1, ((n >> 26) & 3) - 1
                if dx == 0 and dy == 0:
                    continue
                other = (cell[0] + dx, cell[1] + dy)
                if other not in cells:
                    continue
                ocomp = cells[other][2]
                u = n & 0xFFFFFF
                if u >= len(ocomp):
                    continue
                key = tuple(sorted(((cell, comp[t]), (other, ocomp[u]))))
                a, b = verts[tri[k]], verts[tri[(k + 1) % 3]]
                c = crossings.setdefault(key, [0.0, 0.0, 0.0, 0, 0])
                for i in range(3):
                    c[i] += (a[i] + b[i]) * 0.5
                c[3] += 1
                c[4] += 1 if tri[3] & PREFERRED else 0
    nodes, by_ground = [], {}
    for key, c in crossings.items():
        i = len(nodes)
        nodes.append((c[0] / c[3], c[1] / c[3], c[2] / c[3], c[4] * 2 > c[3]))
        for ground in key:
            by_ground.setdefault(ground, []).append(i)
    edges = []
    for members in by_ground.values():
        for x in range(len(members)):
            for y in range(x + 1, len(members)):
                a, b = nodes[members[x]], nodes[members[y]]
                d = math.dist(a[:3], b[:3]) * (ROAD_SHARE if a[3] and b[3] else 1.0)
                edges.append((members[x], members[y], d))
    with open(os.path.join(pack, "navmesh", "ways.txt"), "w") as f:
        f.write("".join(f"N {x:.1f} {y:.1f} {z:.1f}\n" for x, y, z, _r in nodes))
        f.write("".join(f"E {a} {b} {d:.0f}\n" for a, b, d in edges))
    print(f"ways: {len(nodes)} crossings, {len(edges)} ways between, from {len(cells)} cells")


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else "../pack")
