#!/usr/bin/env python3
"""Convert a rectangle of Tamriel exterior cells to .skcell chunks + color maps.

usage: extract_terrain.py PACK_DIR MIN_X MIN_Y MAX_X MAX_Y [--gain G]

Writes PACK_DIR/terrain/tamriel_X_Y.skcell and .sktex, a shared
terrain_detail.sktex, and terrain/index.txt listing the converted cells.
"""
import argparse
import io
import os
import sys

import numpy as np
from PIL import Image

from esm import LoadOrder
from extract_textures import SKYRIM_DATA, find, open_texture_archives
from terrain import (LandTextures, bake_color_map, cell_water_height, world_water_height, decode_land,
                     normals_from_heights, stitched_heights, write_skcell)
from textures import convert_image

DETAIL_SOURCE = "textures\\landscape\\tundra01.dds"


def make_detail_texture(find_texture):
    """Grayscale detail map centered on mid-gray, so 'x2 modulate' is neutral on average."""
    img = Image.open(io.BytesIO(find_texture(DETAIL_SOURCE))).convert("L").resize((128, 128), Image.LANCZOS)
    px = np.asarray(img, np.float32)
    px = 128.0 + (px - px.mean()) * (28.0 / max(px.std(), 1.0))  # fixed, gentle contrast
    gray = np.clip(px, 0, 255).astype(np.uint8)
    return Image.fromarray(np.stack([gray] * 3, axis=-1))


def export_cells(load_order, index, keys, prefix, pack, land_textures, gain=1.6, water_default=None):
    """Writes the cells' terrain (<prefix>_X_Y.skcell and .sktex) for the keys of a
    worldspace's cells (index: its exterior_cells); returns the cells written."""
    out_dir = os.path.join(pack, "terrain")
    os.makedirs(out_dir, exist_ok=True)
    decoded = {}
    for (x, y) in keys:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                k = (x + dx, y + dy)
                entry = index.get(k)
                if k not in decoded and entry and entry["land"]:
                    decoded[k] = decode_land(entry["land"], load_order)
    written = []
    for (x, y) in sorted(keys):
        land = decoded.get((x, y))
        if land is None or land["heights"] is None:
            continue
        normals = normals_from_heights(stitched_heights(decoded, x, y))
        name = f"{prefix}_{x}_{y}"
        with open(os.path.join(out_dir, name + ".skcell"), "wb") as f:
            f.write(write_skcell(x, y, land["heights"], normals, land["colors"],
                                 cell_water_height(index[(x, y)]["cell"], water_default, float(land["heights"].min()))))
        with open(os.path.join(out_dir, name + ".sktex"), "wb") as f:
            f.write(convert_image(bake_color_map(land, land_textures, gain=gain), max_size=256))
        written.append((x, y))
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack")
    ap.add_argument("min_x", type=int)
    ap.add_argument("min_y", type=int)
    ap.add_argument("max_x", type=int)
    ap.add_argument("max_y", type=int)
    ap.add_argument("--gain", type=float, default=1.6, help="brighten landscape textures (authored for HDR)")
    args = ap.parse_args()

    out_dir = os.path.join(args.pack, "terrain")
    preview_dir = os.path.join(out_dir, "_preview")
    os.makedirs(preview_dir, exist_ok=True)

    # Skyrim + Update + DLC masters merged in load order (Update.esm rewrites a lot of terrain)
    load_order = LoadOrder(SKYRIM_DATA)
    index = load_order.exterior_cells("Tamriel")
    sea = world_water_height(load_order, "Tamriel")  # (cells with water and none of their own: the sea's)
    tex_archives = open_texture_archives()
    find_texture = lambda p: find(tex_archives, p)
    land_textures = LandTextures(load_order, find_texture)

    # Decode the rectangle plus a one-cell border (needed for seamless edge normals)
    decoded = {}
    for x in range(args.min_x - 1, args.max_x + 2):
        for y in range(args.min_y - 1, args.max_y + 2):
            entry = index.get((x, y))
            if entry and entry["land"]:
                decoded[(x, y)] = decode_land(entry["land"], load_order)

    written = []
    for y in range(args.min_y, args.max_y + 1):
        for x in range(args.min_x, args.max_x + 1):
            land = decoded.get((x, y))
            if land is None or land["heights"] is None:
                continue
            normals = normals_from_heights(stitched_heights(decoded, x, y))
            name = f"tamriel_{x}_{y}"
            with open(os.path.join(out_dir, name + ".skcell"), "wb") as f:
                f.write(write_skcell(x, y, land["heights"], normals, land["colors"],
                                     cell_water_height(index[(x, y)]["cell"], sea, float(land["heights"].min()))))
            color_map = bake_color_map(land, land_textures, gain=args.gain)
            with open(os.path.join(out_dir, name + ".sktex"), "wb") as f:
                f.write(convert_image(color_map, max_size=256))
            color_map.resize((64, 64), Image.BOX).save(os.path.join(preview_dir, name + ".png"))
            written.append((x, y))
        print(f"row y={y}: {sum(1 for c in written if c[1] == y)} cells")

    with open(os.path.join(args.pack, "textures", "terrain_detail.sktex"), "wb") as f:
        # Detail fades to neutral gray in lower mips (~25 m, 50 m, 100 m away), so its
        # 4 m repeat never reads as a grid in the distance.
        f.write(convert_image(make_detail_texture(find_texture), max_size=128,
                              mip_fade=[1.0, 0.6, 0.25, 0.0]))
    with open(os.path.join(out_dir, "index.txt"), "w") as f:
        f.write("".join(f"{x} {y}\n" for x, y in written))

    print(f"{len(written)} cells, {len(land_textures.cache)} distinct landscape textures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
