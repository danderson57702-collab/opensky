"""Thins a pack's existing mid-distance meshes (<model>__mid.skmesh) to meshes.LOD_LEVELS'
current share, for packs converted while it was higher (0.15 of the full model; now 0.08: a
city's houses at middle distance were half its triangles). Models aren't converted again on
a re-export, so this does it in place, once (a marker file remembers); bake the cells after.

Usage: python3 lighten_mids.py <pack dir>
"""
import glob
import os
import sys

import meshoptimizer
import numpy as np

from bake_cells import read_skmesh
from meshes import LOD_LEVELS, LOD_MID, _compact, write_skmesh

OLD_RATIO = 0.15
MARKER = ".mids_thinned"


def thin(path, keep):
    radius, subs = read_skmesh(path)
    out = []
    for tex, flags, verts, idx in subs:
        idx = idx.astype(np.uint32)
        if len(idx) // 3 >= 64:
            dst = np.zeros_like(idx)
            pos = np.ascontiguousarray(verts["pos"], np.float32)
            count = meshoptimizer.simplify(dst, idx, pos, target_index_count=max(3, int(len(idx) * keep) // 3 * 3),
                                           target_error=LOD_LEVELS[LOD_MID][1],
                                           options=meshoptimizer.SIMPLIFY_LOCK_BORDER)
            if count > 0:
                idx = dst[:count]
        v, i = _compact(np.array(verts), idx)
        out.append({"texture": tex, "flags": flags, "vertices": v, "indices": i})
    with open(path, "wb") as f:
        f.write(write_skmesh(out))


def main(pack):
    marker = os.path.join(pack, "meshes", MARKER)
    if os.path.exists(marker):
        print("mid meshes: already thinned")
        return
    keep = LOD_LEVELS[LOD_MID][0] / OLD_RATIO
    paths = glob.glob(os.path.join(pack, "meshes", "*__mid.skmesh"))
    for p in paths:
        thin(p, keep)
    open(marker, "w").write(f"{LOD_LEVELS[LOD_MID][0]}\n")
    print(f"mid meshes: {len(paths)} thinned to {keep:.2f} of what they were")


if __name__ == "__main__":
    main(sys.argv[1])
