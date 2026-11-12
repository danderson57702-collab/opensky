"""Pre-builds each cell's object batches, so the Vita only reads and uploads them.

Usage: python3 bake_cells.py PACK_DIR

Runs on the output of extract_objects.py (which calls it at the end). For every cell
with placed objects, and every (size group, detail level) the engine draws, this does
what the engine's loader used to do at runtime: transforms each placed model's mesh
into the world, bakes the fixed midday light into the vertex colours, merges the
result into one batch per texture (split at 65535 vertices for 16-bit indices) and
quantizes positions and UVs to 16 bits around each batch's own centre and scale.

  PACK_DIR/objects/baked/<world>_X_Y_G_L.skbat    G: 0 detail, 1 large, 2 huge (world: tamriel,
                                                 or a walled city's own cells, e.g. whiterun)
                                                  L: 0 full, 1 mid, 2 far

Levels written (must match engine/src/objects.c variant_radius):
  detail objects (bounding radius <= 6 m)   full
  large objects (6..25 m)                   mid (the full mesh)
  huge objects (> 25 m)                     mid (the full mesh) and far (LOD mesh)

.skbat layout (little-endian), version 5:
  char[4] "SKBT", u16 version, u16 batch count
  per batch, 4-byte aligned:
    u8 kind (0 opaque, 1 alpha-tested, 2 water), u8 flow (scrolling water texture),
    u8 texture name length, u8 two-sided (no back-face culling), name bytes (no
    terminator), zero-padded to 4
    u16 switch, u16 0   shown by a world switch (esm.SWITCHES: its number, + 0x80 if shown once
                 it's on, else until then); 0 always
    f32 pos[4]   world = position / 32767 * pos[3] + pos[0..2]; pos[3] / (32 x 32767) is a
                 power of two and pos[0..2] are on a 1/1024 m grid: exact (see quantize_watertight)
    f32 uv[3]    uv = texcoord / 32767 * uv[2] + uv[0..1]
    u32 vertex count, u32 index count
    vertices: s16 x, y, z, fine, s16 u, v, u8 r, g, b, a   (16 bytes); fine: 5 more bits
              per axis (x << 10 | y << 5 | z), 1/32 of a position step
    u16 indices, zero-padded to 4 bytes
"""
import glob
import os
import struct
import sys

import numpy as np

from meshes import VERTEX_DTYPE

SKBT_VERSION = 5  # (5: each batch's world switch)
LARGE_RADIUS_M, HUGE_RADIUS_M = 6.0, 25.0
GROUP_DETAIL, GROUP_LARGE, GROUP_HUGE = 0, 1, 2
LOD_FULL, LOD_MID, LOD_FAR = 0, 1, 2
# (Large and huge objects: full detail near the player, their mid mesh beyond (to 117 m),
# huge ones the far LOD after that; full detail all the way out overflowed Vita3K's buffer)
VARIANTS = ((GROUP_DETAIL, LOD_FULL), (GROUP_LARGE, LOD_FULL), (GROUP_LARGE, LOD_MID), (GROUP_HUGE, LOD_FULL),
            (GROUP_HUGE, LOD_MID), (GROUP_HUGE, LOD_FAR))
KIND_OPAQUE, KIND_ALPHA, KIND_WATER = 0, 1, 2
ALPHA_TEST_FLAG, DOUBLE_SIDED_FLAG = 1, 2  # skmesh submesh flags
WATER_TEXTURE = "_water"
MAX_BATCH_VERTICES = 65535
PACKED_DTYPE = np.dtype([("pos", "<i2", 4), ("uv", "<i2", 2), ("col", "u1", 4)])

# engine/src/lighting.c lighting_bake: the midday key and the fixed sun direction
SKY_AMBIENT = np.array([0.62, 0.68, 0.78], np.float32)
GROUND_AMBIENT = np.array([0.42, 0.38, 0.32], np.float32)
SUNLIGHT = np.array([1.25, 1.125, 0.90], np.float32)
SUN_DIR = np.array([-0.5, 0.65, 0.6], np.float32)
SUN_DIR /= np.float32(np.sqrt((SUN_DIR * SUN_DIR).sum()))


def read_skmesh(path):
    """(radius, [(texture, flags, vertices, indices)]) or None."""
    try:
        d = open(path, "rb").read()
    except OSError:
        return None
    if d[:4] != b"SKMS":
        return None
    count = struct.unpack_from("<H", d, 6)[0]
    bmin = np.array(struct.unpack_from("<3f", d, 8), np.float32)
    bmax = np.array(struct.unpack_from("<3f", d, 20), np.float32)
    radius = float(np.sqrt((np.maximum(np.abs(bmin), np.abs(bmax)) ** 2).sum()))
    p, subs = 32, []
    for _ in range(count):
        n = d[p]
        tex = d[p + 1:p + 1 + n].decode("ascii")
        p += 1 + n
        flags, _r, vc, ic = struct.unpack_from("<BHII", d, p)
        p += 11
        verts = np.frombuffer(d, VERTEX_DTYPE, vc, p)
        p += vc * VERTEX_DTYPE.itemsize
        idx = np.frombuffer(d, "<u2", ic, p)
        p += ic * 2 + (-ic * 2) % 4
        subs.append((tex, flags, verts, idx))
    return radius, subs


class Models:
    def __init__(self, pack):
        self.dir = os.path.join(pack, "meshes")
        self.table = [line.split() for line in open(os.path.join(pack, "objects", "models.txt"))]
        self.cache = {}

    def mesh(self, name):
        if name not in self.cache:
            self.cache[name] = read_skmesh(os.path.join(self.dir, name + ".skmesh"))
        return self.cache[name]

    def full(self, index):
        return self.mesh(self.table[index][0]) if index < len(self.table) else None

    def for_level(self, index, lod, huge=False):
        """engine model_mesh: the far level uses the LOD mesh, then the mid one, if any;
        large and huge objects use their mid mesh at the mid level too."""
        name, far, mid = self.table[index]
        if lod == LOD_FAR:
            for alt in (far, mid):
                if alt != "-":
                    m = self.mesh(alt)
                    if m is not None:
                        return m
        if lod == LOD_MID and huge and mid != "-":
            m = self.mesh(mid)
            if m is not None:
                return m
        return self.full(index)


def bake_light(normals, colors, positions=None):
    """lighting_bake for (N,3) world normals and (N,4) Skyrim vertex colours (the
    exterior's fixed midday sun; positions unused)."""
    hemi = normals[:, 1:2] * np.float32(0.5) + np.float32(0.5)
    ndl = np.maximum(normals @ SUN_DIR, 0)[:, None]
    light = GROUND_AMBIENT + (SKY_AMBIENT - GROUND_AMBIENT) * hemi + SUNLIGHT * ndl
    tint = colors[:, :3].astype(np.float32) / np.float32(255)
    v = np.clip(light * tint * np.float32(0.5), 0, 1)
    return (v * np.float32(255) + np.float32(0.5)).astype(np.uint8)


class Builder:
    def __init__(self, tex, kind, two_sided, switch=0):
        self.tex, self.kind, self.two_sided, self.switch = tex, kind, two_sided, switch
        self.parts, self.indices = [], []
        self.vcount = 0
        self.full = False


MAX_EDGE_M = 8.0  # longer triangle edges are split (see split_long_edges)


def split_long_edges(attrs, idx, max_edge=MAX_EDGE_M):
    """Splits triangles until no edge is longer than max_edge (world metres). The engine
    works fog and the distance dissolve out at the vertices, so across a long triangle
    they came out as if its middle were as far away as its corners: road chunks lying
    under the player dissolved and fogged. attrs: per-vertex arrays (positions first),
    blended at the midpoints; every long edge is split in all its triangles alike, so
    no cracks open. Returns (attrs, idx)."""
    attrs = [a.copy() for a in attrs]
    tris = idx.reshape(-1, 3).astype(np.int64)
    limit = max_edge * max_edge
    while True:
        p = attrs[0]
        e = [((p[tris[:, i]] - p[tris[:, (i + 1) % 3]]) ** 2).sum(1) > limit for i in range(3)]
        marked = e[0] | e[1] | e[2]
        if not marked.any():
            break
        mids, new_attrs = {}, [[] for _ in attrs]
        count = len(p)

        def mid(a, b):
            nonlocal count
            key = (a, b) if a < b else (b, a)
            if key not in mids:
                for k, arr in enumerate(attrs):
                    v = (arr[a].astype(np.float32) + arr[b].astype(np.float32)) * np.float32(0.5)
                    new_attrs[k].append(np.rint(v) if arr.dtype.kind in "iu" else v)
                mids[key] = count
                count += 1
            return mids[key]

        out = [tris[~marked]]
        extra = []
        for t, m0, m1, m2 in zip(tris[marked], e[0][marked], e[1][marked], e[2][marked]):
            flags = [m0, m1, m2]
            # Rotate so the pattern is: one long edge first, or the one short edge last
            n = sum(flags)
            r = flags.index(True) if n == 1 else flags.index(False) - 2 if n == 2 else 0
            a, b, c = (int(t[(i + r) % 3]) for i in range(3))
            if n == 1:
                m = mid(a, b)
                extra += [(a, m, c), (m, b, c)]
            elif n == 2:
                m, k = mid(a, b), mid(b, c)
                extra += [(m, b, k), (a, m, k), (a, k, c)]
            else:
                m, k, j = mid(a, b), mid(b, c), mid(c, a)
                extra += [(a, m, j), (m, b, k), (j, k, c), (m, k, j)]
        attrs = [np.concatenate([arr, np.array(na, arr.dtype).reshape((-1,) + arr.shape[1:])])
                 for arr, na in zip(attrs, new_attrs)]
        tris = np.concatenate(out + [np.array(extra, np.int64).reshape(-1, 3)])
    return attrs, tris.reshape(-1).astype(np.uint32)


# Roads are draped over the ground: much of Skyrim's road geometry lies a few cm under the
# landscape, which Skyrim hides by painting road textures on the ground there. Our ground
# is a low-resolution colour map, so the ground poking through showed as blurry patches
# that came and went with the view angle. Road triangles are split to about the ground's
# grid spacing and lifted to stay at least this far above it (cobbles above gravel).
ROAD_PREFIX = "landscape_roads_"
DRAPE_MAX_EDGE_M = 1.5
DRAPE_CLEARANCE_M, DRAPE_COBBLE_M = 0.04, 0.015
CELL_M = 4096 / 70.0


class GroundHeights:
    """The converted terrain's heights (pack/terrain/*.skcell), interpolated the way the
    engine draws them (each grid square split along its v00-v11 diagonal)."""

    def __init__(self, pack):
        self.dir = os.path.join(pack, "terrain")
        self.cells = {}

    def _cell(self, cx, cy):
        if (cx, cy) not in self.cells:
            try:
                d = open(os.path.join(self.dir, f"tamriel_{cx}_{cy}.skcell"), "rb").read()
                self.cells[(cx, cy)] = np.frombuffer(d, np.float32, 33 * 33, 20).reshape(33, 33)
            except (OSError, ValueError):
                self.cells[(cx, cy)] = None
        return self.cells[(cx, cy)]

    def at(self, x, z):
        """Heights for arrays of x, z (NaN where there's no terrain)."""
        out = np.full(len(x), np.nan, np.float32)
        cx, cy = np.floor(x / CELL_M).astype(int), np.floor(-z / CELL_M).astype(int)
        for key in set(zip(cx.tolist(), cy.tolist())):
            h = self._cell(*key)
            if h is None:
                continue
            m = (cx == key[0]) & (cy == key[1])
            sp = CELL_M / 32
            fx, fy = (x[m] - key[0] * CELL_M) / sp, (-key[1] * CELL_M - z[m]) / sp
            i, j = np.clip(fx.astype(int), 0, 31), np.clip(fy.astype(int), 0, 31)
            a, b = fx - i, fy - j
            h00, h10, h01, h11 = h[j, i], h[j, i + 1], h[j + 1, i], h[j + 1, i + 1]
            out[m] = np.where(a >= b, h00 + a * (h10 - h00) + b * (h11 - h10), h00 + b * (h01 - h00) + a * (h11 - h01))
        return out


def drape_road(pos, attrs, idx, ground, cobble):
    """Splits a road layer finely and lifts it clear of the ground. attrs[0] is pos."""
    attrs, idx = split_long_edges(attrs, idx, DRAPE_MAX_EDGE_M)
    pos = attrs[0]
    h = ground.at(pos[:, 0], pos[:, 2])
    floor = h + np.float32(DRAPE_CLEARANCE_M + (DRAPE_COBBLE_M if cobble else 0.0))
    lift = np.isfinite(floor) & (pos[:, 1] < floor)
    pos[lift, 1] = floor[lift]
    return attrs, idx


def is_fish(model_name):
    """engine objects_is_fish_model: the ambient salmon effect models."""
    return "fxambwater" in model_name and "salmon" in model_name


HEAVY_PART_TRIS = 1500  # (an interior's parts over this many triangles: simplified)
HEAVY_PART_KEEP = 0.5
HEAVY_PART_ERROR = 0.01  # (of the part's size: a stair's edges stay where they are)
_lightened = {}


def lighten_part(key, verts, idx):
    """A heavy part with about half its triangles (meshoptimizer, its borders - texture seams -
    kept): Fort Greymoor's tower stairs are 22,000 triangles each, three in one room, more than
    the Vita draws in a frame outdoors. Cached by key."""
    if key not in _lightened:
        import meshoptimizer
        pos = np.ascontiguousarray(verts["pos"], np.float32)
        src = np.ascontiguousarray(idx, np.uint32).reshape(-1)
        dst = np.zeros_like(src)
        target = int(len(src) // 3 * HEAVY_PART_KEEP) * 3
        count = meshoptimizer.simplify(dst, src, pos, target_index_count=target, target_error=HEAVY_PART_ERROR,
                                       options=meshoptimizer.SIMPLIFY_LOCK_BORDER)
        if 0 < count < len(src):
            tri = dst[:count].reshape(-1, 3)
            used = np.unique(tri)
            _lightened[key] = (verts[used], np.searchsorted(used, tri).astype(np.uint32).reshape(-1))
        else:
            _lightened[key] = (verts, idx)
    return _lightened[key]


def build_variant(models, entries, group, lod, light=bake_light, ground=None, lighten=False):
    """Batches of the entries in one size group (None: all) at one detail level; light
    (normals, colors, positions) -> rgb u8 bakes the vertex light. lighten: heavy parts
    simplified (an interior's)."""
    builders = []

    def builder_for(tex, kind, two_sided, switch=0):
        for b in builders:
            if not b.full and b.kind == kind and b.tex == tex and b.two_sided == two_sided and b.switch == switch:
                return b
        b = Builder(tex, kind, two_sided, switch)
        builders.append(b)
        return b

    for entry in entries:
        model, m9, t = entry[:3]
        switch = entry[3] if len(entry) > 3 else 0  # (shown by a world switch: its own batches)
        if is_fish(models.table[model][0]):
            continue  # Skyrim's ambient salmon: the engine swims them (fish.c)
        full = models.full(model)
        if full is None:
            continue
        scale = float(np.sqrt(m9[0, 0] ** 2 + m9[1, 0] ** 2 + m9[2, 0] ** 2))
        r = full[0] * scale
        g = GROUP_HUGE if r > HUGE_RADIUS_M else GROUP_LARGE if r > LARGE_RADIUS_M else GROUP_DETAIL
        if group is not None and g != group:
            continue
        inv_scale = np.float32(1.0 / max(scale, 1e-6))
        for part_no, (tex, flags, verts, idx) in enumerate(models.for_level(model, lod, huge=g != GROUP_DETAIL)[1]):
            if not len(verts):
                continue
            if lighten and len(idx) // 3 > HEAVY_PART_TRIS:
                verts, idx = lighten_part((model, part_no), verts, idx)
            kind = KIND_WATER if tex == WATER_TEXTURE else KIND_ALPHA if flags & ALPHA_TEST_FLAG else KIND_OPAQUE
            two_sided = kind != KIND_WATER and bool(flags & DOUBLE_SIDED_FLAG)
            # (Apple's BLAS raises spurious FP warnings in float32 matmul; results are checked)
            with np.errstate(all="ignore"):
                pos = verts["pos"] @ m9.T + t
                nrm = (verts["n"].astype(np.float32) / np.float32(127)) @ m9.T * inv_scale
                lit = light(nrm, verts["col"], pos)
            if not np.isfinite(pos).all():
                continue
            attrs = [pos.astype(np.float32), verts["uv"], lit, verts["col"][:, 3]]
            if ground is not None and models.table[model][0].startswith(ROAD_PREFIX):
                attrs, idx = drape_road(attrs[0], attrs, idx, ground, "_roads_" in tex)
            (pos, uv, lit, alpha), idx = split_long_edges(attrs, idx)
            b = builder_for("" if kind == KIND_WATER else tex, kind, two_sided, switch)
            if b.vcount + len(pos) > MAX_BATCH_VERTICES:
                b.full = True
                b = builder_for(b.tex, kind, two_sided, switch)
            b.parts.append((pos, uv, lit, alpha))
            b.indices.append(idx.astype(np.uint32) + b.vcount)
            b.vcount += len(pos)
    return builders


def quantize(values, centre, extent):
    q = (values - centre) * np.float32(32767.0 / extent)
    return np.rint(np.clip(q, -32767, 32767)).astype(np.int16)


FINE_STEPS = 32  # sub-steps per 16-bit position step, 5 bits per axis in the 4th short


def quantize_fine(values, centre, extent):
    """Positions as 16-bit steps plus 5 more bits per axis (packed into a 4th short): 1/32
    of a step. One step is extent/32767 (about 1.5 mm across a cell), which put overlapping
    wall pieces' shared faces up to that far apart, and they fought over which was in front
    (glimmering stone). Returns ((N,3) int16 steps, (N,) int16 fine bits: x<<10 | y<<5 | z)."""
    q = np.clip((values.astype(np.float64) - centre) * (32767.0 / extent), -32767, 32766.99)
    step = np.floor(q)
    fine = np.rint((q - step) * FINE_STEPS).astype(np.int64)
    carry = fine >= FINE_STEPS
    step[carry] += 1
    fine[carry] = 0
    step = np.clip(step, -32767, 32767)
    packed = (fine[:, 0] << 10) | (fine[:, 1] << 5) | fine[:, 2]
    return step.astype(np.int16), packed.astype(np.int16)


WORLD_GRID_M = 1.0 / 1024  # every object position snaps to this world grid (see quantize_watertight)


def quantize_watertight(pos):
    """Positions for one batch so that a point two batches share comes out exactly the
    same in both: snapped to a world grid, stored as whole steps of a power-of-two size
    (a divisor of the grid) from a centre on the grid. Batch-relative scaling put pieces
    meeting at a seam (floor tiles, wall sections) a few hundredths of a mm apart, and the
    background showed through pinholes along every seam. Returns (centre, scale for the
    file: step x 32 x 32767, (N,3) int16 coarse steps, (N,) int16 fine bits)."""
    g = WORLD_GRID_M
    p = np.round(pos.astype(np.float64) / g) * g
    centre = np.round((p.min(0) + p.max(0)) * 0.5 / g) * g
    half = max(float(np.abs(p - centre).max()), 1e-3)
    step = 2.0 ** np.ceil(np.log2(half / (32767.0 * FINE_STEPS)))  # (<= g: never > 1 km)
    q = np.round((p - centre) / step).astype(np.int64)                  # exact multiples
    coarse = np.floor_divide(q, FINE_STEPS)
    fine = q - coarse * FINE_STEPS
    packed = (fine[:, 0] << 10) | (fine[:, 1] << 5) | fine[:, 2]
    return (centre.astype(np.float32), float(step * FINE_STEPS * 32767.0), coarse.astype(np.int16),
            packed.astype(np.int16))


def pack_batch(b):
    pos = np.concatenate([p[0] for p in b.parts])
    uv = np.concatenate([p[1] for p in b.parts])
    ulo, uhi = uv.min(0), uv.max(0)
    uv_centre = (ulo + uhi) * np.float32(0.5)
    uv_extent = max(float(((uhi - ulo) * 0.5).max()), 1e-3)
    v = np.zeros(len(pos), PACKED_DTYPE)
    centre, extent, v["pos"][:, :3], v["pos"][:, 3] = quantize_watertight(pos)
    v["uv"] = quantize(uv, uv_centre, uv_extent)
    v["col"][:, :3] = np.concatenate([p[2] for p in b.parts])
    v["col"][:, 3] = np.concatenate([p[3] for p in b.parts])
    idx = np.concatenate(b.indices).astype("<u2")
    name = b.tex.encode("ascii")
    flow = int(b.tex.startswith("effects_fxwater"))
    out = bytearray(struct.pack("<BBBB", b.kind, flow, len(name), int(b.two_sided)) + name + b"\0" * (-len(name) % 4))
    out += struct.pack("<HH", b.switch, 0)  # (5: shown by a world switch, esm.SWITCHES; 0 always)
    out += struct.pack("<4f3fII", *centre, extent, *uv_centre, uv_extent, len(v), len(idx))
    out += v.tobytes()
    raw = idx.tobytes()
    out += raw + b"\0" * (-len(raw) % 4)
    return out


def read_entries(path):
    d = open(path, "rb").read()
    x, y, n = struct.unpack_from("<iiI", d, 8)
    out = []
    for k in range(n):
        e = struct.unpack_from("<HH9f3f", d, 20 + k * 52)
        out.append((e[0], np.array(e[2:11], np.float32).reshape(3, 3), np.array(e[11:14], np.float32), e[1]))
    return x, y, out


def batches_file(builders):
    """A .skbat file's bytes for non-empty builders, or None."""
    builders = [b for b in builders if b.vcount]
    if not builders:
        return None
    return struct.pack("<4sHH", b"SKBT", SKBT_VERSION, len(builders)) + b"".join(pack_batch(b) for b in builders)


def bake(pack):
    models = Models(pack)
    out_dir = os.path.join(pack, "objects", "baked")
    os.makedirs(out_dir, exist_ok=True)
    for old in glob.glob(os.path.join(out_dir, "*.skbat")):
        os.remove(old)
    files = total = 0
    worst = (0, None)
    ground = GroundHeights(pack)
    for path in sorted(glob.glob(os.path.join(pack, "objects", "*_*_*.skobj"))):
        prefix = os.path.basename(path).rsplit("_", 2)[0]  # (tamriel, or a walled city's: whiterun)
        x, y, entries = read_entries(path)
        cell_bytes = 0
        # Each variant's batches cover a quarter of the cell, so the engine can leave out
        # what's behind you or past a level's distance by the batch, not the whole cell
        quarters = [[], [], [], []]
        for e in entries:
            qx = int(e[2][0] - x * CELL_M >= CELL_M * 0.5)
            qz = int(-e[2][2] - y * CELL_M >= CELL_M * 0.5)
            quarters[qx + 2 * qz].append(e)
        for group, lod in VARIANTS:
            builders = []
            for q in quarters:
                if q:
                    builders += build_variant(models, q, group, lod, ground=ground)
            data = batches_file(builders)
            if data is None:
                continue
            with open(os.path.join(out_dir, f"{prefix}_{x}_{y}_{group}_{lod}.skbat"), "wb") as f:
                f.write(data)
            files += 1
            cell_bytes += len(data)
        total += cell_bytes
        worst = max(worst, (cell_bytes, (x, y)))
    print(f"baked {files} cell batch files, {total / 1e6:.0f} MB (largest cell {worst[1]}: {worst[0] / 1e6:.1f} MB)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    bake(sys.argv[1])
