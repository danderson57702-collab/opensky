"""Convert NIF shapes into .skmesh files for the Vita engine.

.skmesh layout (little-endian), version 1:
    char[4] magic = "SKMS"
    u16 version, u16 submesh_count
    f32 bounds_min[3], f32 bounds_max[3]               (meters, Y-up)
    submesh_count x {
        u8 texture_name_len, char texture_name[len]     (pack name, no extension)
        u8 flags (1 = alpha test), u16 reserved
        u32 vertex_count, u32 index_count
        vertex_count x { f32 pos[3]; f32 uv[2]; s8 normal[3]; u8 pad; u8 color[4] }   (28 bytes)
        u16 indices[index_count], zero-padded to 4 bytes
    }
"""
import struct

import meshoptimizer
import numpy as np

SKMS_VERSION = 1
FLAG_ALPHA_TEST = 1
FLAG_DOUBLE_SIDED = 2  # Skyrim's material flag: draw without back-face culling
FLAG_DECAL = 4         # lies on another surface: lifted off it (DECAL_LIFT_M), never simplified
# Skyrim draws decals (moss, plaster patches, wall caps) with a depth offset; without one
# they sat exactly on their walls and the two fought (shimmering patches). Lifted this far
# along their normals instead: more than the walls' own simplification error (1 cm).
DECAL_LIFT_M = 0.015
SKYRIM_UNITS_PER_METER = 70.0

VERTEX_DTYPE = np.dtype([("pos", "<f4", 3), ("uv", "<f4", 2), ("n", "i1", 3), ("pad", "u1"), ("col", "u1", 4)])


def texture_pack_name(texture_path):
    """textures\\dungeons\\ridgedstone08.dds -> dungeons_ridgedstone08"""
    p = texture_path.lower().replace("/", "\\")
    if p.startswith("textures\\"):
        p = p[9:]
    return p.rsplit(".", 1)[0].replace("\\", "_").replace(" ", "_")


def to_engine_space(v):
    """Skyrim units, Z-up -> engine meters, Y-up."""
    return _to_engine_space(np.asarray(v, dtype=np.float64)) / SKYRIM_UNITS_PER_METER


def _to_engine_space(v):
    # Skyrim: Z-up, units. Engine: Y-up, meters. (x, y, z) -> (x, z, -y) is a proper
    # rotation, so triangle winding is preserved.
    return np.stack([v[:, 0], v[:, 2], -v[:, 1]], axis=1)


def build_submeshes(shapes):
    """Group shapes by (texture, alpha test, double-sided) and merge each group into one
    vertex/index list."""
    groups = {}
    for s in shapes:
        if not s.texture:
            continue
        key = (s.texture, s.alpha_test, s.double_sided, bool(getattr(s, "decal", False)))
        groups.setdefault(key, []).append(s)

    submeshes = []
    for (src, alpha, double_sided, decal), group in sorted(groups.items()):
        # Alpha-tested uses need the DXT5 variant, so they get their own pack name
        tex = texture_pack_name(src) + ("_a" if alpha else "")
        verts, indices, base = [], [], 0
        for s in group:
            n = len(s.positions)
            if base + n > 0xFFFF:
                raise ValueError(f"{tex}: more than 65535 vertices in one submesh")
            v = np.zeros(n, VERTEX_DTYPE)
            pos = s.positions.astype(np.float64)
            if decal and s.normals is not None:
                nrm = s.normals.astype(np.float64)
                nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-6)
                pos = pos + nrm * (DECAL_LIFT_M * SKYRIM_UNITS_PER_METER)
            v["pos"] = _to_engine_space(pos) / SKYRIM_UNITS_PER_METER
            v["uv"] = s.uvs
            if s.normals is not None:
                v["n"] = np.clip(np.round(_to_engine_space(s.normals) * 127.0), -127, 127)
            else:
                v["n"] = (0, 127, 0)
            v["col"] = s.colors if s.colors is not None else 255
            verts.append(v)
            indices.append(s.triangles.astype(np.uint32).reshape(-1) + base)
            base += n
        submeshes.append({
            "texture": tex,
            "source": src,
            "flags": (FLAG_ALPHA_TEST if alpha else 0) | (FLAG_DOUBLE_SIDED if double_sided else 0) |
                     (FLAG_DECAL if decal else 0),
            "vertices": np.concatenate(verts),
            "indices": np.concatenate(indices).astype(np.uint16),
        })
    return submeshes


# --- Simplification -------------------------------------------------------------------
#
# Budget: a 3x3 block of full-detail cells should stay around 100-150k triangles on the
# Vita. Skyrim's vegetation dominates (tundra shrubs are ~4k-triangle clusters of leaf
# cards), and card clusters can't be simplified by edge collapse without wrecking them,
# so alpha-tested submeshes are thinned card-by-card first.

MIN_TRIS_TO_SIMPLIFY = 300
SOLID_TARGET_RATIO = 0.35
CARD_KEEP_EVERY = 3          # keep 1 of every 3 leaf cards...
CARD_SCALE = 1.4             # ...and enlarge the survivors so the plant still reads as full
MIN_CARDS_TO_THIN = 24
SIMPLIFY_ERROR = 0.02        # relative to mesh extent (distance levels)
# Full detail: at most this far off the original surface (metres), and open borders kept
# where they are. A relative error let a big wall drift tens of cm, so layered meshes
# (stone under its moss and ivy) poked through each other and glimmered, and modular
# cave pieces opened cracks where they meet.
# (Locked borders kept mountains, open shells, at nearly every triangle: 24 MB of geometry
# a frame around Riverwood, past what Vita3K copies safely. Landscape doesn't need them.)
LANDSCAPE_ERROR = 0.006
FULL_DETAIL_ERROR_M = 0.01  # (5 mm kept 91% of triangles; 1 cm 86%; the old relative error 47%)


def _vertex_components(indices, vertex_count):
    """Connected-component id per vertex (vertices linked by shared triangles)."""
    parent = np.arange(vertex_count)

    def root(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for a, b, c in indices.reshape(-1, 3):
        ra, rb, rc = root(a), root(b), root(c)
        parent[rb] = ra
        parent[rc] = ra
    return np.array([root(v) for v in range(vertex_count)])


def _thin_cards(vertices, indices):
    tris = indices.reshape(-1, 3)
    comp = _vertex_components(indices, len(vertices))
    tri_comp = comp[tris[:, 0]]
    cards = np.unique(tri_comp)
    if len(cards) < MIN_CARDS_TO_THIN:
        return vertices, indices
    keep = set(cards[::CARD_KEEP_EVERY].tolist())
    kept_tris = tris[np.isin(tri_comp, list(keep))]
    vertices = vertices.copy()
    for c in keep:
        vmask = comp == c
        center = vertices["pos"][vmask].mean(axis=0)
        vertices["pos"][vmask] = center + (vertices["pos"][vmask] - center) * CARD_SCALE
    return vertices, kept_tris.reshape(-1).astype(indices.dtype)


def _compact(vertices, indices):
    used, remap = np.unique(indices, return_inverse=True)
    return vertices[used], remap.astype(np.uint16)


# Distance levels for big models: MID is seen from ~60-120 m (the 5x5 cells around the
# player), FAR beyond that when Skyrim has no hand-made distant model.
LOD_MID, LOD_FAR = 1, 2
LOD_LEVELS = {LOD_MID: (0.08, 0.06), LOD_FAR: (0.08, 0.08)}  # (target ratio, max error; mid: below the full detail's 0.35)


def simplify_submesh(sub, lod=0, thin_cards=True, precise=True, lock_border=False):
    """Reduces a submesh's triangle count in place (see budget notes above).
    lod: 0 full detail, LOD_MID or LOD_FAR for distance versions. thin_cards: thin
    alpha-tested card clusters - right for foliage, wrong for e.g. thatch roofs. precise: the
    full detail stays within FULL_DETAIL_ERROR_M with open borders where they are (pieces
    that meet or lie on each other: buildings, dungeons, roads); else (rocks, mountains,
    trees: open shells sunk into the ground, alone) LANDSCAPE_ERROR of its size. lock_border:
    the open edges kept where they are even so - rocks and mountains, which do meet: unlocked,
    a cliff's edge drifted up to 23 m and hillsides opened gaps you could sit in; locked, they
    cost 12% more triangles.)"""
    vertices, indices = sub["vertices"], sub["indices"].astype(np.uint32)
    if sub["flags"] & FLAG_DECAL and not lod:
        return sub  # (kept exactly: it must stay above the surface it lies on)
    if thin_cards and sub["flags"] & FLAG_ALPHA_TEST:
        vertices, indices = _thin_cards(vertices, indices)
    min_tris = 64 if lod else MIN_TRIS_TO_SIMPLIFY
    if len(indices) // 3 >= min_tris:
        if lod:
            ratio, error = LOD_LEVELS[lod]
        else:
            ratio = SOLID_TARGET_RATIO if not sub["flags"] & FLAG_ALPHA_TEST else 0.6
            error = SIMPLIFY_ERROR
        options = meshoptimizer.SIMPLIFY_LOCK_BORDER if precise or (lock_border and not lod) else 0
        if not lod and precise:
            options |= meshoptimizer.SIMPLIFY_ERROR_ABSOLUTE
            error = FULL_DETAIL_ERROR_M
        elif not lod:
            error = LANDSCAPE_ERROR
        dst = np.zeros_like(indices)
        positions = np.ascontiguousarray(vertices["pos"], dtype=np.float32)
        count = meshoptimizer.simplify(dst, indices, positions,
                                       target_index_count=max(3, int(len(indices) * ratio) // 3 * 3),
                                       target_error=error, options=options)
        if count > 0:
            indices = dst[:count]
    sub["vertices"], sub["indices"] = _compact(vertices, indices)
    return sub


def write_skmesh(submeshes, bounds=None):
    """bounds: (min, max) to use when there is no geometry (effect-only models)."""
    if submeshes:
        all_pos = np.concatenate([m["vertices"]["pos"] for m in submeshes])
        bmin, bmax = all_pos.min(0), all_pos.max(0)
    else:
        bmin, bmax = bounds
    out = bytearray(struct.pack("<4sHH6f", b"SKMS", SKMS_VERSION, len(submeshes), *bmin, *bmax))
    for m in submeshes:
        # A few Skyrim meshes carry NaN UVs, which the GPU samples as garbage
        uv = m["vertices"]["uv"]
        if not np.isfinite(uv).all():
            m["vertices"] = m["vertices"].copy()
            m["vertices"]["uv"] = np.nan_to_num(uv, nan=0.0, posinf=0.0, neginf=0.0)
        name = m["texture"].encode("ascii")
        out += struct.pack("<B", len(name)) + name
        out += struct.pack("<BHII", m["flags"], 0, len(m["vertices"]), len(m["indices"]))
        out += m["vertices"].tobytes()
        idx = m["indices"].tobytes()
        out += idx + b"\0" * (-len(idx) % 4)
    return bytes(out)
