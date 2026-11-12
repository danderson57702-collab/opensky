"""Skyrim's navmeshes: where NPCs can walk.

Each exterior cell has a few NAVM records (the main walkable area plus small islands
such as decks and rooftops). This merges a cell's navmeshes into one triangle list and
resolves the links between them, including links across cell borders, so the engine can
path-find across cells.

  PACK_DIR/navmesh/tamriel_X_Y.sknav

.sknav layout (little-endian), version 1:
  char[4] "SKNV", u16 version, u16 0, s32 cell x, s32 cell y, u32 vertex count,
  u32 triangle count
  vertices: f32 x, y, z (engine space, metres)
  triangles (20 bytes): u16 vertex[3], u16 flags, u32 neighbour[3]
    neighbour across edge k (vertex k -> vertex k+1): 0xFFFFFFFF for none, otherwise
    triangle | (dx + 1) << 24 | (dy + 1) << 26, the triangle's index in the cell dx, dy
    away (each -1..1)
"""
import os
import struct

SKNV_VERSION = 1
NVNM_VERSION = 12
UNITS_PER_METER = 70.0
EDGE_LINK_FLAGS = (0x1, 0x2, 0x4)  # edge k leads into another navmesh (an edge-link index)
NONE = 0xFFFFFFFF
SAME_CELL = 1 << 24 | 1 << 26  # dx = dy = 0


LINK_TOLERANCE_UNITS = 112.0  # 1.6 m (a step between two navmeshes: Helgen's square south of the rubble missed by 1.0 m - cut in two)


def _distance_to_triangle_edges(p, tri):
    """Distance from p to the nearest point on the triangle's edges (Skyrim units)."""
    best = float("inf")
    for i in range(3):
        a, b = tri[i], tri[(i + 1) % 3]
        ab = [b[k] - a[k] for k in range(3)]
        ap = [p[k] - a[k] for k in range(3)]
        den = sum(c * c for c in ab)
        t = max(0.0, min(1.0, sum(ab[k] * ap[k] for k in range(3)) / den)) if den > 0 else 0.0
        d = sum((ap[k] - ab[k] * t) ** 2 for k in range(3)) ** 0.5
        best = min(best, d)
    return best


def parse_nvnm(data):
    """(vertices [(x, y, z)], triangles [(v0, v1, v2, e0, e1, e2, flags)], edge links
    [(navmesh local form id, triangle)]) or None."""
    if len(data) < 20 or struct.unpack_from("<I", data)[0] != NVNM_VERSION:
        return None
    p = 16
    nv = struct.unpack_from("<I", data, p)[0]
    p += 4
    verts = [struct.unpack_from("<3f", data, p + i * 12) for i in range(nv)]
    p += nv * 12
    nt = struct.unpack_from("<I", data, p)[0]
    p += 4
    tris = [struct.unpack_from("<3H3hH", data, p + i * 16) for i in range(nt)]
    p += nt * 16
    ne = struct.unpack_from("<I", data, p)[0]
    p += 4
    links = [struct.unpack_from("<IIH", data, p + i * 10)[1:] for i in range(ne)]
    return verts, tris, links


def export(load_order, cells, pack, bounds, prefix="tamriel", only=None):
    """prefix: the world's file names (a walled city's cells: "whiterun"); only: just these
    cells (keys), and the others' files are kept."""
    min_x, min_y, max_x, max_y = bounds
    out_dir = os.path.join(pack, "navmesh")
    os.makedirs(out_dir, exist_ok=True)
    for old in os.listdir(out_dir):
        if old.startswith(prefix + "_"):
            os.remove(os.path.join(out_dir, old))

    # Pass 1: every navmesh of the region (plus a ring around it, for links) -> (cell, base)
    meshes = {}   # global navmesh id -> (cell key, parsed, record)
    per_cell = {}
    for key, c in cells.items():
        x, y = key
        if not (min_x - 1 <= x <= max_x + 1 and min_y - 1 <= y <= max_y + 1):
            continue
        base = 0
        for g, rec in sorted(c.get("navm", {}).items()):
            parsed = parse_nvnm(rec.sub("NVNM") or b"")
            if parsed is None:
                continue
            meshes[g] = (key, parsed, rec, base)
            base += len(parsed[1])
            per_cell.setdefault(key, []).append(g)

    files = tris_total = links_total = dropped = 0
    for key, ids in per_cell.items():
        x, y = key
        if not (min_x <= x <= max_x and min_y <= y <= max_y) or (only is not None and key not in only):
            continue
        verts, tris = [], []
        for g in ids:
            _key, (nv, nt, links), rec, base = meshes[g]
            vbase = len(verts)
            verts += [(px / UNITS_PER_METER, pz / UNITS_PER_METER, -py / UNITS_PER_METER) for px, py, pz in nv]
            for v0, v1, v2, e0, e1, e2, flags in nt:
                nbr = []
                for k, e in enumerate((e0, e1, e2)):
                    if flags & EDGE_LINK_FLAGS[k]:
                        if not 0 <= e < len(links):
                            nbr.append(NONE)
                            continue
                        other, tri = links[e]
                        target = meshes.get(load_order.global_id(rec.plugin, other))
                        if target is None:
                            nbr.append(NONE)
                            continue
                        (tx, ty), _p, _r, tbase = target
                        dx, dy = tx - x, ty - y
                        if abs(dx) > 1 or abs(dy) > 1 or tri >= len(target[1][1]):
                            nbr.append(NONE)
                            continue
                        # A handful of links point at triangles nowhere near the edge
                        tv = target[1][0]
                        t3 = target[1][1][tri]
                        a, b = nv[(v0, v1, v2)[k]], nv[(v0, v1, v2)[(k + 1) % 3]]
                        mid = tuple((a[i] + b[i]) * 0.5 for i in range(3))
                        if _distance_to_triangle_edges(mid, [tv[t3[0]], tv[t3[1]], tv[t3[2]]]) > LINK_TOLERANCE_UNITS:
                            nbr.append(NONE)
                            dropped += 1
                            continue
                        nbr.append((tbase + tri) | (dx + 1) << 24 | (dy + 1) << 26)
                        links_total += 1
                    else:
                        nbr.append((base + e) | SAME_CELL if 0 <= e < len(nt) else NONE)
                tris.append((v0 + vbase, v1 + vbase, v2 + vbase, flags, *nbr))
        if not tris:
            continue
        data = bytearray(struct.pack("<4sHHiiII", b"SKNV", SKNV_VERSION, 0, x, y, len(verts), len(tris)))
        data += b"".join(struct.pack("<3f", *v) for v in verts)
        data += b"".join(struct.pack("<4H3I", *t) for t in tris)
        with open(os.path.join(out_dir, f"{prefix}_{x}_{y}.sknav"), "wb") as f:
            f.write(data)
        files += 1
        tris_total += len(tris)
    print(f"navmesh: {files} cells, {tris_total} triangles, {links_total} links between navmeshes "
          f"({dropped} that don't meet their edge dropped)")


def export_interior(load_order, navm_records, path):
    """One interior cell's navmeshes merged into a .sknav (cell 0, 0; links only within
    the cell). Returns the triangle count."""
    meshes, base = {}, 0
    for rec in navm_records:
        parsed = parse_nvnm(rec.sub("NVNM") or b"")
        if parsed is None:
            continue
        meshes[load_order.global_id(rec.plugin, rec.form_id)] = (parsed, rec, base)
        base += len(parsed[1])
    verts, tris = [], []
    for g, ((nv, nt, links), rec, base) in meshes.items():
        vbase = len(verts)
        verts += [(px / UNITS_PER_METER, pz / UNITS_PER_METER, -py / UNITS_PER_METER) for px, py, pz in nv]
        for v0, v1, v2, e0, e1, e2, flags in nt:
            nbr = []
            for k, e in enumerate((e0, e1, e2)):
                if flags & EDGE_LINK_FLAGS[k]:
                    other = meshes.get(load_order.global_id(rec.plugin, links[e][0])) if 0 <= e < len(links) else None
                    tri = links[e][1] if other else 0
                    nbr.append((other[2] + tri) | SAME_CELL if other and tri < len(other[0][1]) else NONE)
                else:
                    nbr.append((base + e) | SAME_CELL if 0 <= e < len(nt) else NONE)
            tris.append((v0 + vbase, v1 + vbase, v2 + vbase, flags, *nbr))
    if not tris:
        return 0
    data = bytearray(struct.pack("<4sHHiiII", b"SKNV", SKNV_VERSION, 0, 0, 0, len(verts), len(tris)))
    data += b"".join(struct.pack("<3f", *v) for v in verts)
    data += b"".join(struct.pack("<4H3I", *t) for t in tris)
    with open(path, "wb") as f:
        f.write(data)
    return len(tris)
