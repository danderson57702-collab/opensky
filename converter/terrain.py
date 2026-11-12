"""Convert Skyrim LAND records into .skcell terrain chunks plus a baked color map per cell.

A Skyrim exterior cell is 4096 units (~58.5 m) square with a 33x33 vertex grid.
Its ground is up to ~9 texture layers per quadrant, blended by per-vertex alpha.
That's too heavy for the Vita, so each cell's layers are composited offline into
one color map; the engine adds a tiled grayscale detail texture for close-up
sharpness and bakes lighting x Skyrim's vertex colors into vertex colors.

.skcell layout (little-endian), version 1:
    char[4] magic = "SKCL"
    u16 version, u16 reserved
    i32 grid_x, i32 grid_y
    f32 water_height            meters, NaN if the cell has no water
    f32 height[33*33]           meters; row 0 = south edge, column 0 = west edge
    s8  normal[33*33][3]        engine space (x east, y up, z south), x127
    u8  color[33*33][3]         Skyrim vertex color (VCLR), 255 if absent
The color map is a separate .sktex, row 0 = north edge.
"""
import io
import struct

import numpy as np
from PIL import Image

SKCL_VERSION = 1
GRID = 33
CELL_UNITS = 4096.0
UNITS_PER_METER = 70.0
QUAD_GRID = 17
DEFAULT_LAND_TEXTURE = "textures\\landscape\\dirt02.dds"
TILES_PER_CELL = 6  # landscape textures repeat this many times across a cell
LAYER_TEX_SIZE = 64  # layer textures are reduced to this before compositing


# --- LAND decoding ---------------------------------------------------------------

def decode_heights(vhgt):
    """VHGT -> (33,33) float32 heights in Skyrim units, row 0 = south."""
    offset = struct.unpack_from("<f", vhgt)[0]
    deltas = np.frombuffer(vhgt, dtype=np.int8, count=GRID * GRID, offset=4).reshape(GRID, GRID)
    deltas = deltas.astype(np.float32)
    # First column accumulates down the rows; each row then accumulates across.
    row_starts = offset + np.cumsum(deltas[:, 0])
    heights = row_starts[:, None] + np.cumsum(np.concatenate([np.zeros((GRID, 1), np.float32), deltas[:, 1:]], axis=1), axis=1)
    return heights * 8.0


def decode_land(rec, load_order=None):
    """Returns dict with heights, colors and per-quadrant texture layers.

    With a load_order, LTEX form ids (local to the plugin that wrote the LAND) are
    converted to global load-order ids."""
    def gid(form):
        if not form or load_order is None:
            return form
        return load_order.global_id(rec.plugin, form) or 0
    heights = None
    colors = None
    quads = {q: {"base": 0, "layers": []} for q in range(4)}
    last_layer = None
    for st, payload in rec.subrecords():
        if st == "VHGT":
            heights = decode_heights(payload)
        elif st == "VCLR":
            colors = np.frombuffer(payload, dtype=np.uint8, count=GRID * GRID * 3).reshape(GRID, GRID, 3)
        elif st == "BTXT":
            form, quad = struct.unpack_from("<IB", payload)
            quads[quad]["base"] = gid(form)
        elif st == "ATXT":
            form, quad, _pad, layer = struct.unpack_from("<IBBH", payload)
            last_layer = {"form": gid(form), "layer": layer, "alpha": np.zeros((QUAD_GRID, QUAD_GRID), np.float32)}
            quads[quad]["layers"].append(last_layer)
        elif st == "VTXT" and last_layer is not None:
            for pos, _unused, opacity in struct.iter_unpack("<HHf", payload):
                if pos < QUAD_GRID * QUAD_GRID:
                    last_layer["alpha"][pos // QUAD_GRID, pos % QUAD_GRID] = opacity
    for q in quads.values():
        q["layers"].sort(key=lambda l: l["layer"])
    return {"heights": heights, "colors": colors, "quads": quads}


# --- Texture lookup and compositing ------------------------------------------------

class LandTextures:
    """Resolves LTEX form ids to small RGB arrays of the landscape texture."""

    def __init__(self, load_order, find_texture):
        self.lo = load_order
        self.find_texture = find_texture
        self.cache = {}

    def path_for(self, ltex_form):
        if ltex_form == 0:
            return DEFAULT_LAND_TEXTURE
        ltex = self.lo.record(ltex_form, ("LTEX",))
        tnam = ltex.sub("TNAM") if ltex else None
        txst_id = self.lo.global_id(ltex.plugin, struct.unpack("<I", tnam)[0]) if tnam else None
        txst = self.lo.record(txst_id, ("TXST",)) if txst_id is not None else None
        tx00 = txst.sub("TX00") if txst else None
        if not tx00:
            return DEFAULT_LAND_TEXTURE
        p = tx00.rstrip(b"\0").decode("cp1252").lower().replace("/", "\\")
        return p if p.startswith("textures\\") else "textures\\" + p

    def rgb(self, ltex_form, gain):
        path = self.path_for(ltex_form)
        if path not in self.cache:
            try:
                img = Image.open(io.BytesIO(self.find_texture(path))).convert("RGB")
            except KeyError:
                img = Image.open(io.BytesIO(self.find_texture(DEFAULT_LAND_TEXTURE))).convert("RGB")
            img = img.resize((LAYER_TEX_SIZE, LAYER_TEX_SIZE), Image.LANCZOS)
            self.cache[path] = np.asarray(img, dtype=np.float32) / 255.0
        return np.clip(self.cache[path] * gain, 0.0, 1.0)


def _bilinear(grid, size):
    """Upsample a (17,17) vertex grid to (size,size) texel centers, row 0 = south."""
    n = grid.shape[0] - 1
    t = (np.arange(size) + 0.5) / size * n
    i0 = np.clip(np.floor(t).astype(int), 0, n - 1)
    f = t - i0
    rows = grid[i0] * (1 - f)[:, None] + grid[i0 + 1] * f[:, None]
    return rows[:, i0] * (1 - f)[None, :] + rows[:, i0 + 1] * f[None, :]


def bake_color_map(land, textures, size=256, gain=1.0):
    """Composite a cell's texture layers into a (size,size,3) RGB image, row 0 = north."""
    half = size // 2
    out = np.zeros((size, size, 3), np.float32)  # built row 0 = south, flipped at the end
    # Texel -> tiled layer texture coordinates (the same tiling across the whole cell)
    tex_idx = ((np.arange(size) + 0.5) / size * TILES_PER_CELL * LAYER_TEX_SIZE).astype(int) % LAYER_TEX_SIZE
    for q, (qx, qy) in enumerate([(0, 0), (1, 0), (0, 1), (1, 1)]):  # SW, SE, NW, NE
        rows = slice(qy * half, (qy + 1) * half)
        cols = slice(qx * half, (qx + 1) * half)
        ty, tx = tex_idx[rows], tex_idx[cols]
        quad = land["quads"][q]
        # Texture images have row 0 = top (north), so sample them flipped
        color = textures.rgb(quad["base"], gain)[::-1][ty][:, tx]
        for layer in quad["layers"]:
            a = np.clip(_bilinear(layer["alpha"], half), 0.0, 1.0)[..., None]
            color = color * (1 - a) + textures.rgb(layer["form"], gain)[::-1][ty][:, tx] * a
        out[rows, cols] = color
    return Image.fromarray((np.clip(out[::-1], 0, 1) * 255 + 0.5).astype(np.uint8))


# --- Geometry ------------------------------------------------------------------------

def stitched_heights(cells, x, y):
    """(35,35) heights with a one-vertex border from neighbors, for seamless normals."""
    h = np.empty((GRID + 2, GRID + 2), np.float32)
    center = cells[(x, y)]["heights"]
    h[1:-1, 1:-1] = center
    # Edges: neighbor's second row/column (their first equals our last)
    def nb(dx, dy):
        c = cells.get((x + dx, y + dy))
        return c["heights"] if c is not None and c["heights"] is not None else None
    w, e, s, n = nb(-1, 0), nb(1, 0), nb(0, -1), nb(0, 1)
    h[1:-1, 0] = w[:, -2] if w is not None else 2 * center[:, 0] - center[:, 1]
    h[1:-1, -1] = e[:, 1] if e is not None else 2 * center[:, -1] - center[:, -2]
    h[0, 1:-1] = s[-2, :] if s is not None else 2 * center[0, :] - center[1, :]
    h[-1, 1:-1] = n[1, :] if n is not None else 2 * center[-1, :] - center[-2, :]
    h[0, 0], h[0, -1], h[-1, 0], h[-1, -1] = h[0, 1], h[0, -2], h[-1, 1], h[-1, -2]
    return h


def normals_from_heights(h35):
    """Central-difference normals for the inner 33x33, in engine space (x east, y up, z south)."""
    spacing = CELL_UNITS / (GRID - 1)
    dx = (h35[1:-1, 2:] - h35[1:-1, :-2]) / (2 * spacing)  # d height / d east
    dn = (h35[2:, 1:-1] - h35[:-2, 1:-1]) / (2 * spacing)  # d height / d north
    # Surface y = h(east, north); engine z = -north
    n = np.stack([-dx, np.ones_like(dx), dn], axis=-1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


# XCLW of FLT_MAX, or Skyrim's 0x4F7FFFC9 (4294953216.0) and -2^31, aren't a level of their own:
# a level hundreds of km from the sea is never a real one (Dustman's Cairn swam in the air). A
# cell flagged as having water (DATA 0x2) with none of its own has its worldspace's default (DNAM:
# Tamriel's -14000, the Sea of Ghosts - the whole coast at Solitude had no sea, its ships in the air)
NO_WATER_THRESHOLD = 1e6
CELL_HAS_WATER = 0x2


def world_water_height(load_order, world_editor_id):
    """A worldspace's default water height (Skyrim units, WRLD DNAM), or None."""
    found = None
    for p in load_order.plugins:
        for w in p.records_of_type("WRLD"):
            if w.editor_id() == world_editor_id and w.sub("DNAM") and len(w.sub("DNAM")) >= 8:
                found = struct.unpack_from("<f", w.sub("DNAM"), 4)[0]
    return found


def cell_water_height(cell_record, world_default=None, lowest=None):
    """Water height in Skyrim units: the CELL's XCLW, else its world's default if it has water - and
    its land dips under it (lowest: the land's lowest point; the sea under every hill isn't drawn);
    or None."""
    xclw = cell_record.sub("XCLW")
    h = struct.unpack("<f", xclw[:4])[0] if xclw else None
    if h is not None and abs(h) < NO_WATER_THRESHOLD:
        return h
    data = cell_record.sub("DATA")
    if world_default is None or abs(world_default) >= NO_WATER_THRESHOLD or not (data and data[0] & CELL_HAS_WATER):
        return None
    return world_default if lowest is None or lowest < world_default else None


def write_skcell(x, y, heights, normals, colors, water_height=None):
    water_m = float("nan") if water_height is None else water_height / UNITS_PER_METER
    out = bytearray(struct.pack("<4sHHiif", b"SKCL", SKCL_VERSION, 0, x, y, water_m))
    out += (heights / UNITS_PER_METER).astype("<f4").tobytes()
    out += np.clip(np.round(normals * 127), -127, 127).astype(np.int8).tobytes()
    out += (colors if colors is not None else np.full((GRID, GRID, 3), 255, np.uint8)).tobytes()
    return bytes(out)
