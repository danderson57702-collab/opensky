#!/usr/bin/env python3
"""Pull static meshes out of the Skyrim SE BSAs and write .skmesh files (plus every
texture they reference as .sktex) to the pack.

usage: extract_meshes.py PACK_DIR NAME=meshes\\path\\model.nif[@gain=1.4] [NAME=... ...]

The optional gain brightens all textures of that mesh (see textures.convert).
"""
import glob
import os
import sys

from bsa import BSA
from extract_textures import SKYRIM_DATA, find, open_texture_archives
from meshes import build_submeshes, write_skmesh
from nif import NIF
from textures import convert


def open_mesh_archives():
    return [BSA(p) for p in sorted(glob.glob(os.path.join(SKYRIM_DATA, "Skyrim - Meshes*.bsa")))]


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    pack = argv[1]
    mesh_dir = os.path.join(pack, "meshes")
    tex_dir = os.path.join(pack, "textures")
    preview_dir = os.path.join(tex_dir, "_preview")
    for d in (mesh_dir, tex_dir, preview_dir):
        os.makedirs(d, exist_ok=True)

    mesh_archives = open_mesh_archives()
    tex_archives = open_texture_archives()

    for spec in argv[2:]:
        name, rest = spec.split("=", 1)
        path, *opts = rest.split("@")
        gain = next((float(o[5:]) for o in opts if o.startswith("gain=")), 1.0)

        shapes = NIF(find(mesh_archives, path)).shapes()
        submeshes = build_submeshes(shapes)
        if not submeshes:
            print(f"{name}: {path} has no textured static geometry, skipped")
            continue
        with open(os.path.join(mesh_dir, name + ".skmesh"), "wb") as f:
            f.write(write_skmesh(submeshes))

        tris = sum(len(m["indices"]) // 3 for m in submeshes)
        verts = sum(len(m["vertices"]) for m in submeshes)
        print(f"{name}: {path} -> {len(submeshes)} submeshes, {verts} verts, {tris} tris")

        for m in submeshes:
            src = m["source"]
            keep_alpha = bool(m["flags"] & 1)
            sktex, preview = convert(find(tex_archives, src), keep_alpha=keep_alpha, gain=gain)
            with open(os.path.join(tex_dir, m["texture"] + ".sktex"), "wb") as f:
                f.write(sktex)
            preview.save(os.path.join(preview_dir, m["texture"] + ".png"))
            print(f"    texture {m['texture']} ({'DXT5 alpha-test' if keep_alpha else 'DXT1'})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
