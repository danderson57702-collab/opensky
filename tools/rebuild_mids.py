"""Rebuilds the mid-distance meshes (<model>__mid.skmesh) of huge built pieces - ruins, temples,
forts, towers - from their full models, within MID_ERROR_M (metres), not a share of their size.

The mid level (seen from ~45 to 117 m) takes huge objects' mid meshes: thinned to 8% within 6% of
their size, a 60 m arch of Bleak Falls Barrow's temple could lose metres of its shape - pillars cut
off, pieces floating in the sky (the user, Nov 8). Rocks, mountains and trees keep theirs (open
shells, where that error doesn't show).

Usage (from converter/, after a re-export; then bake the cells and pack the archives):
  python3 ../tools/rebuild_mids.py ../pack
"""
import os
import sys

import meshoptimizer
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "converter"))
from bake_cells import read_skmesh  # noqa: E402
from meshes import _compact, write_skmesh  # noqa: E402

HUGE_M = 25.0      # (bake_cells: a huge object's bounding radius - its mid mesh is used at the mid level)
MID_ERROR_M = 0.3  # (at 45 m and more a Vita pixel is ~0.1-0.2 m: within two pixels)
MID_RATIO = 0.35   # (at most this share of the full model's triangles - the full detail's own share)
BUILT = ("dungeons_", "architecture_", "clutter_", "_ruins", "nortemple", "nortmpext", "fort", "castle", "tower")
NATURAL = ("landscape_", "rocks", "mountain", "tree", "plants_", "cliff")


def built(name):
    n = name.lower()
    return any(b in n for b in BUILT) and not any(w in n for w in NATURAL)


def rebuild(full_path, mid_path):
    _radius, subs = read_skmesh(full_path)
    out, before, after = [], 0, 0
    for tex, flags, verts, idx in subs:
        idx = idx.astype(np.uint32)
        before += len(idx) // 3
        if len(idx) // 3 >= 64:
            dst = np.zeros_like(idx)
            pos = np.ascontiguousarray(verts["pos"], np.float32)
            count = meshoptimizer.simplify(dst, idx, pos, target_index_count=max(3, int(len(idx) * MID_RATIO) // 3 * 3),
                                           target_error=MID_ERROR_M,
                                           options=meshoptimizer.SIMPLIFY_LOCK_BORDER | meshoptimizer.SIMPLIFY_ERROR_ABSOLUTE)
            if count > 0:
                idx = dst[:count]
        v, i = _compact(np.array(verts), idx)
        after += len(i) // 3
        out.append({"texture": tex, "flags": flags, "vertices": v, "indices": i})
    with open(mid_path, "wb") as f:
        f.write(write_skmesh(out))
    return before, after


def main(pack):
    meshes = os.path.join(pack, "meshes")
    done = old_tris = new_tris = 0
    with open(os.path.join(pack, "objects", "models.txt")) as f:
        rows = [line.split() for line in f if line.strip()]
    for name, _far, mid in rows:
        if mid == "-" or not built(name):
            continue
        full_path, mid_path = os.path.join(meshes, name + ".skmesh"), os.path.join(meshes, mid + ".skmesh")
        if not (os.path.exists(full_path) and os.path.exists(mid_path)):
            continue
        radius, _subs = read_skmesh(full_path)
        if radius < HUGE_M:
            continue
        _r, was = read_skmesh(mid_path)
        old_tris += sum(len(i) // 3 for _t, _f, _v, i in was)
        _before, after = rebuild(full_path, mid_path)
        new_tris += after
        done += 1
    print(f"mid meshes rebuilt within {MID_ERROR_M} m: {done} huge built pieces, {old_tris} -> {new_tris} triangles")


if __name__ == "__main__":
    main(sys.argv[1])
