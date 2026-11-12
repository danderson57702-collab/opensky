"""Skyrim's world map: the province from the game's distant-terrain textures (Tamriel's LOD:
level 32 tiles for the whole of it, level 4 for the converted region up close), shaded by
the LOD normal maps as the sun from the north-west would light it, and the map markers
(placed references with XMRK: their name, kind and whether they always show).

  PACK_DIR/map/province.sktex  the province (PROVINCE cells across, north up)
  PACK_DIR/map/region.sktex    the converted region, more closely
  PACK_DIR/map/map.txt         lines:
    P x0 y0 x1 y1        the province image's cell bounds (x0..x1, y0..y1; y up)
    R x0 y0 x1 y1        the region image's
    M kind flags x z name   a marker: Skyrim's map marker kind (1 city, 2 town, 4 cave,
                            5 camp, 6 fort...), flags (1 always shown, 2 fast travel, 4 in the
                            converted region), engine position (metres)
"""
import io
import os
import struct

import numpy as np
from PIL import Image

from bsa import BSA
from extract_textures import SKYRIM_DATA
from textures import convert_image

UNITS_PER_METER = 70.0
CELL_UNITS = 4096.0
PROVINCE = (-64, -64, 64, 64)  # LOD32 tiles x -64, -32, 0, 32 and y the same: the land
MAP_PX = 1024
LIGHT = np.array([-0.55, 0.55, 0.63])  # (x east, y north, z up): from the north-west, high
LIGHT /= np.linalg.norm(LIGHT)


def _lod_archives():
    return [BSA(os.path.join(SKYRIM_DATA, f)) for f in sorted(os.listdir(SKYRIM_DATA))
            if f.startswith("Skyrim - Textures") and f.endswith(".bsa")]


def _read(archives, path):
    for a in archives:
        if path in a.entries:
            return a.read(path)
    return None


def _tile(archives, level, x, y, px):
    """A LOD tile's colour shaded by its normals (px square, north up), or None."""
    base = f"textures\\terrain\\tamriel\\tamriel.{level}.{x}.{y}"
    d = _read(archives, base + ".dds")
    if d is None:
        return None
    col = np.asarray(Image.open(io.BytesIO(d)).convert("RGB").resize((px, px), Image.LANCZOS), np.float32) / 255.0
    n = _read(archives, base + "_n.dds")
    if n is not None:
        nrm = np.asarray(Image.open(io.BytesIO(n)).convert("RGB").resize((px, px), Image.LANCZOS), np.float32) / 127.5 - 1.0
        # (DDS rows run north to south: the normal's green points north)
        shade = np.clip(nrm @ LIGHT, 0.0, 1.0)
        col = col * (0.45 + 0.85 * shade[..., None])
    return np.clip(col * 1.15, 0.0, 1.0)


def _compose(archives, level, x0, y0, x1, y1, tile_px):
    """Tiles of a LOD level over cells x0..x1, y0..y1 (tile-aligned) into one image."""
    nx, ny = (x1 - x0) // level, (y1 - y0) // level
    out = np.zeros((ny * tile_px, nx * tile_px, 3), np.float32)
    out[...] = (0.18, 0.24, 0.30)  # (the sea beyond)
    for j in range(ny):
        for i in range(nx):
            t = _tile(archives, level, x0 + i * level, y0 + j * level, tile_px)
            if t is not None:  # (row 0 is the north edge)
                out[(ny - 1 - j) * tile_px:(ny - j) * tile_px, i * tile_px:(i + 1) * tile_px] = t
    return out


def export(lo, cells, pack, bounds, strings):
    out_dir = os.path.join(pack, "map")
    os.makedirs(out_dir, exist_ok=True)
    archives = _lod_archives()
    x0, y0, x1, y1 = PROVINCE
    prov = _compose(archives, 32, x0, y0, x1, y1, 256)
    img = Image.fromarray((prov * 255).astype(np.uint8)).resize((MAP_PX, MAP_PX), Image.LANCZOS)
    with open(os.path.join(out_dir, "province.sktex"), "wb") as f:
        f.write(convert_image(img, max_size=MAP_PX))
    # The region up close: level 4 tiles (4 cells each) over it, made square
    bx0, by0, bx1, by1 = bounds
    side = max(bx1 - bx0 + 1, by1 - by0 + 1)
    side = (side + 3) // 4 * 4
    rx0, ry0 = (bx0 + bx1) // 2 - side // 2, (by0 + by1) // 2 - side // 2
    rx0, ry0 = rx0 // 4 * 4, ry0 // 4 * 4
    rx1, ry1 = rx0 + side + 4, ry0 + side + 4
    reg = _compose(archives, 4, rx0, ry0, rx1, ry1, 128)
    img = Image.fromarray((reg * 255).astype(np.uint8)).resize((MAP_PX, MAP_PX), Image.LANCZOS)
    with open(os.path.join(out_dir, "region.sktex"), "wb") as f:
        f.write(convert_image(img, max_size=MAP_PX))
    lines = [f"P {x0} {y0} {x1} {y1}", f"R {rx0} {ry0} {rx1} {ry1}"]
    # The markers
    count = 0
    # (and the walled cities' own: Whiterun's and Dragonsreach's markers are in Whiterun's
    # worldspace, not Tamriel's - neither was on the map. "@prefix" before the name: a city's)
    from worlds import CITIES
    worlds = [("", cells)] + [(prefix, lo.exterior_cells(w)) for w, prefix, _t in CITIES]
    for world, (cx, cy), cell in ((w, k, c) for w, wc in worlds for k, c in wc.items()):
        for r in cell["refs"]:
            if r.sub("XMRK") is None or not r.sub("DATA"):
                continue
            tnam, fnam, full = r.sub("TNAM"), r.sub("FNAM"), r.sub("FULL")
            kind = tnam[0] if tnam else 0
            flags = fnam[0] if fnam else 0
            name = ""
            if full and len(full) == 4:
                name = strings.get(r.plugin, struct.unpack("<I", full)[0], "strings") or ""
            elif full:
                name = full.rstrip(b"\0").decode("cp1252", "replace")
            if not name:
                continue
            px, py, pz = struct.unpack_from("<3f", r.sub("DATA"))
            inside = bx0 <= cx <= bx1 and by0 <= cy <= by1
            ours = (1 if flags & 1 else 0) | (2 if flags & 2 else 0) | (4 if inside else 0)
            name = name.replace("\n", " ").strip()
            at = f"@{world} " if world else ""
            # (^height: where Skyrim's marker stands - fast travel lands there, not on whatever's
            # highest over the terrain: High Hrothgar's roof, its marker on the steps before its doors)
            lines.append(f"M {kind} {ours} {px / UNITS_PER_METER:.1f} {-py / UNITS_PER_METER:.1f} {at}^{pz / UNITS_PER_METER:.1f} {name}")
            count += 1
    with open(os.path.join(out_dir, "map.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print(f"map: province {x1 - x0}x{y1 - y0} cells, region {rx1 - rx0} cells, {count} markers")
