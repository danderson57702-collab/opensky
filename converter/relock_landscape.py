"""Converts a pack's rocks, cliffs and mountains again with their open edges kept where they
are (meshes.simplify_submesh lock_border), for packs converted before: their edges had drifted
by up to 23 m, opening gaps in hillsides. Models aren't converted again on a re-export, so this
rewrites their full-detail meshes in place, once (a marker file remembers); bake the cells after.

Usage: python3 relock_landscape.py <pack dir>
"""
import itertools
import os
import sys

from extract_objects import LANDSCAPE_SEAM_PREFIXES, ModelConverter, is_vegetation, model_pack_name
from extract_textures import find
from meshes import build_submeshes, simplify_submesh, write_skmesh
from nif import NIF

MARKER = ".landscape_locked"


def source_path(mesh_archives, name):
    """The model's path in Skyrim's archives from its pack name (its underscores are either
    folder separators or its own), or None."""
    parts = name.split("_")
    for cuts in itertools.product(("\\", "_"), repeat=len(parts) - 1):
        path = parts[0] + "".join(c + p for c, p in zip(cuts, parts[1:])) + ".nif"
        if not path.startswith(LANDSCAPE_SEAM_PREFIXES) or model_pack_name(path) != name:
            continue
        try:
            find(mesh_archives, "meshes\\" + path)
            return path
        except KeyError:
            pass
    return None


def main(pack):
    marker = os.path.join(pack, "meshes", MARKER)
    if os.path.exists(marker):
        print("landscape edges: already kept")
        return
    mc = ModelConverter(pack)
    prefixes = tuple(model_pack_name(p + "x")[:-1] for p in LANDSCAPE_SEAM_PREFIXES)
    names = [line.split()[0] for line in open(os.path.join(pack, "objects", "models.txt"))]
    done = 0
    for name in names:
        if not name.startswith(prefixes) or len(name.split("_")) > 12:
            continue
        path = source_path(mc.mesh_archives, name)
        if path is None:
            continue
        submeshes = build_submeshes(NIF(find(mc.mesh_archives, "meshes\\" + path)).shapes())
        for sub in submeshes:
            simplify_submesh(sub, thin_cards=is_vegetation(path), precise=False, lock_border=True)
        with open(os.path.join(pack, "meshes", name + ".skmesh"), "wb") as f:
            f.write(write_skmesh(submeshes))
        done += 1
    open(marker, "w").write("1\n")
    print(f"landscape edges: {done} rocks, cliffs and mountains converted again")


if __name__ == "__main__":
    main(sys.argv[1])
