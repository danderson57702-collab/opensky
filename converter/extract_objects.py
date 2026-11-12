#!/usr/bin/env python3
"""Convert the placed objects (REFR) of a rectangle of Tamriel cells.

usage: extract_objects.py PACK_DIR MIN_X MIN_Y MAX_X MAX_Y

Writes:
  PACK_DIR/objects/models.txt            model table, line N = model index N:
                                         "<mesh> <far LOD mesh or -> <mid LOD mesh or ->"
  PACK_DIR/objects/tamriel_X_Y.skobj     placed objects per cell
  PACK_DIR/objects/fx.txt                fire/glow points per model, one per line:
                                         "model_index kind x y z width height" (kind 0 fire, 1 glow;
                                         model space, meters, Y-up; y is the base of the flame)
  PACK_DIR/textures/fx_fire.sktex, fx_glow.sktex   sprites for those points
  PACK_DIR/meshes/<model>.skmesh         each referenced model, simplified
  PACK_DIR/objects/baked/*.skbat          cells pre-built for the engine (bake_cells.py)
  PACK_DIR/meshes/<model>.skcol          its Skyrim (Havok) collision as triangles, if any:
                                         "SKCO", u16 version 1, u16 0, u32 count,
                                         f32 tris[count][3][3] (model space, meters, Y-up)
  PACK_DIR/textures/<texture>.sktex      each texture those models use
  PACK_DIR/actors/*.sknpc                unique NPCs with their schedules (npcs.py)
  PACK_DIR/navmesh/tamriel_X_Y.sknav     where they can walk (navmesh.py)
  PACK_DIR/interiors/                    buildings and dungeons behind the doors (interiors.py)
  PACK_DIR/actors/player.sknpc           the player's character and wardrobe (npcs.py)
  PACK_DIR/anims/*.skanim                Skyrim's character animations (animations.py, hkx.py)

.skobj layout (little-endian), version 1:
    char[4] magic = "SKOB"
    u16 version, u16 reserved
    i32 grid_x, i32 grid_y, u32 count
    count x { u16 model_index; u16 reserved; f32 m[9] (row-major rotation*scale, engine space);
              f32 t[3] (meters) }                                            (52 bytes each)
"""
import glob
import math
import os
import struct
import sys
from collections import Counter

import numpy as np

import animations
import interiors
import items
import navmesh
from esm import SWITCH_ON_SHOWS
import skills
import npcs
from dialogue import StringTables
from esm import LoadOrder
from extract_meshes import open_mesh_archives
from extract_textures import SKYRIM_DATA, find, open_texture_archives
from bake_cells import bake
from meshes import LOD_FAR, LOD_MID, build_submeshes, simplify_submesh, to_engine_space, write_skmesh
from nif import NIF, Reader
from textures import convert

SKOB_VERSION = 1
def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


SKLO_VERSION = 6  # (2: containers' aim radius, as SKIT 5; 3: furniture after the loose items, as SKIT 7;
                  #  4: traps after the furniture, as SKIT 8; 5: activators after the traps; 6: doors that
                  #  open in place after the activators: Valtheim Towers' are its way in)
UNITS_PER_METER = 70.0

# Base object types that place visible static geometry. Loose items (MISC, WEAP,
# ...) and actors are out of scope for now.
PLACEABLE_TYPES = ("STAT", "MSTT", "TREE", "FLOR", "FURN", "DOOR", "CONT", "ACTI", "LIGH")
WATER_MODEL_PREFIX = "water\\"
# Road pieces lie on the terrain (a tenth of their surface within a few cm of it) and are
# two exactly coplanar layers, the cobbles over a gravel base, which Skyrim resolves by
# draw order. Here the piece (and its collision) is lifted off the terrain and the cobbles
# off the gravel, so neither pair fights over the same pixels.
ROAD_MODEL_PREFIX = "landscape\\roads\\"
LANDSCAPE_PREFIXES = ("landscape\\", "plants\\")  # rocks, mountains, trees, plants (simplified loosely)
# ... of which these meet each other: their open edges stay put (meshes.simplify_submesh)
# Placed for Skyrim's opening and moved by its scripts: left out (the engine moves its own)
OPENING_MOVED = ("mq101cart1ref", "mq101cart2ref")
# (blockers Skyrim's quest scripts take away: the gate across the caves' way out of Helgen Keep -
# no script, no lever; it shut you in at the end)
OPENING_REMOVED = ("mq101endgatetemp",)
LANDSCAPE_SEAM_PREFIXES = ("landscape\\mountains\\", "landscape\\rocks\\", "landscape\\cliffs\\")
ROAD_LIFT_M, ROAD_COBBLE_LIFT_M = 0.06, 0.015


def layer_road(model_path, submeshes):
    if not model_path.startswith(ROAD_MODEL_PREFIX):
        return
    for sub in submeshes:
        lift = ROAD_LIFT_M + (ROAD_COBBLE_LIFT_M if "_roads_" in sub["texture"] else 0.0)
        sub["vertices"] = sub["vertices"].copy()
        sub["vertices"]["pos"][:, 1] += lift
WATER_TEXTURE = "_water"  # engine draws this as translucent water, no texture file

SKCO_VERSION = 1
COLLISION_BOX_MODEL = "marker_collisionbox"  # (Skyrim's CollisionMarker boxes: ModelConverter.collision_box)
XPRM_BOX = 1
# Collision on effects (salmon jumps, waterfalls) and critters are trigger volumes, not walls
NO_COLLISION_PREFIXES = ("critters\\",)
# Effects' collision is mostly for their own sake (salmon leaping, debris, moths) - but a creek's
# or rapids' bed is ground (Skyrim's static layer): only that is kept. (Without it, Bleak Falls
# Sanctum's stream had no bottom: you sank through to the void)
EFFECTS_PREFIX, EFFECTS_SOLID_LAYERS = "effects\\", {1}

# Models drawn beyond the 3x3 cells around the player (bounding radius > 15 m, see
# engine/src/objects.c) need a distance LOD.
LOD_MIN_RADIUS_M = 15.0
MID_MIN_RADIUS_M = 6.0  # (bake_cells LARGE_RADIUS_M: every large object has a mid mesh beyond ~50 m)

FX_FIRE, FX_GLOW = 0, 1
FIRE_ATLAS = "textures\\effects\\fxfireatlas02.dds"  # 4x2 flipbook of flame frames


def effect_points(shapes):
    """Fire and glow sprite positions for a model, from its effect-shader shapes.

    Skyrim draws flames as particle systems and flipbook cards; neither maps to the
    Vita renderer, so each fire becomes a point the engine renders as an animated,
    additive sprite. Flame cards (fxfire*) give the flame directly; otherwise glow
    maps on burning logs/torch heads (*_g) mark where the fire sits.
    """
    def bounds(shape):
        p = to_engine_space(shape.positions)
        return p.min(0), p.max(0)

    points = []
    flames = [s for s in shapes if s.effect_texture and "fxfire" in s.effect_texture]
    embers = [s for s in shapes if s.effect_texture and s.effect_texture.endswith("_g.dds")]
    for s in flames:
        lo, hi = bounds(s)
        c = (lo + hi) / 2
        points.append((FX_FIRE, c[0], lo[1], c[2], max(hi[0] - lo[0], hi[2] - lo[2]), hi[1] - lo[1]))
    if not flames:
        for s in embers:
            lo, hi = bounds(s)
            c = (lo + hi) / 2
            w = max(0.25, 0.8 * max(hi[0] - lo[0], hi[2] - lo[2]))
            points.append((FX_FIRE, c[0], c[1], c[2], w, w * 1.5))
    # A soft halo per fire (the glow cards' own bounds are offset by their billboard
    # nodes, and glow-only ambient effects would become huge blobs, so halos follow fires).
    fires = [p for p in points if p[0] == FX_FIRE]
    for kind, x, y, z, w, h in fires:
        size = min(3.0, max(w, h) * 2.2)
        points.append((FX_GLOW, x, y + h * 0.4, z, size, size))
    return points

# Skyrim -> engine axes: (x, y, z) -> (x, z, -y)
C = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=np.float64)


# Most object textures are 128 px; text has to stay legible, so signage gets more.
LEGIBLE_TEXTURE_KEYWORDS = ("signage", "\\sign", "banner")
DEFAULT_TEXTURE_SIZE = 128
LEGIBLE_TEXTURE_SIZE = 256


ACTOR_TEXTURE_PATHS = ("textures\\actors\\", "textures\\armor\\", "textures\\clothes\\")  # people: seen up close


def texture_budget(texture_path):
    p = texture_path.lower()
    if p.startswith(ACTOR_TEXTURE_PATHS):
        return LEGIBLE_TEXTURE_SIZE
    return LEGIBLE_TEXTURE_SIZE if any(k in p for k in LEGIBLE_TEXTURE_KEYWORDS) else DEFAULT_TEXTURE_SIZE


# Card thinning keeps foliage affordable, but architecture uses card clusters too
# (thatch roofs, shingles) and must keep every card
VEGETATION_PATHS = ("landscape\\plants\\", "landscape\\trees\\", "landscape\\grass\\", "plants\\",
                    "landscape\\flora\\")


def is_vegetation(model_path):
    return model_path.startswith(VEGETATION_PATHS) or "shrub" in model_path or "tree" in model_path.rsplit("\\", 1)[-1]


def model_pack_name(model_path):
    return model_path.lower().replace("/", "\\").rsplit(".", 1)[0].replace("\\", "_").replace(" ", "_")


def ref_transform(data, scale):
    """REFR DATA (pos xyz, rot xyz radians) + XSCL -> engine-space (3x3, translation)."""
    px, py, pz, rx, ry, rz = struct.unpack_from("<6f", data)

    def rot(axis, a):
        c, s = math.cos(a), math.sin(a)
        if axis == "x":
            return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
        if axis == "y":
            return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
        return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])

    # Creation Engine euler angles: rotate about Z first, then Y, then X, each clockwise
    # (M = Rx(-x) Ry(-y) Rz(-z)). Verified by fitting the 211 tilted road pieces around
    # Whiterun to the terrain: the opposite order left 22% of road surface > 0.5 m off
    # the ground, this one 6% (the rest are roads genuinely bridging dips).
    m = rot("x", -rx) @ rot("y", -ry) @ rot("z", -rz) * scale
    return C @ m @ C.T, C @ np.array([px, py, pz]) / UNITS_PER_METER


class ModelConverter:
    def __init__(self, pack):
        self.pack = pack
        self.mesh_archives = open_mesh_archives()
        self.tex_archives = open_texture_archives()
        self.models = []           # index -> pack name
        self.model_index = {}      # model path -> index, or None if unusable
        self.textures_done = set()
        self.fx = {}               # model index -> effect points
        self.lods = {}             # model index -> far LOD mesh name
        self.mids = {}             # model index -> mid LOD mesh name
        self.stats = Counter()

    def resume(self):
        """Continues the pack's model table (objects/models.txt, fx.txt) instead of starting
        a new one: converted models keep their indices and new ones are appended, so a part
        of the pack (the interiors) can be re-exported without the exterior cells."""
        self._resumed = {}
        with open(os.path.join(self.pack, "objects", "models.txt")) as f:
            for i, line in enumerate(f):
                name, lod, mid = line.split()
                self.models.append(name)
                self._resumed[name] = i
                if lod != "-":
                    self.lods[i] = lod
                if mid != "-":
                    self.mids[i] = mid
        fx_path = os.path.join(self.pack, "objects", "fx.txt")
        if os.path.exists(fx_path):
            with open(fx_path) as f:
                for line in f:
                    idx, kind, *v = line.split()
                    self.fx.setdefault(int(idx), []).append((int(kind), *map(float, v)))
        self.textures_done = {n[:-6] for n in os.listdir(os.path.join(self.pack, "textures")) if n.endswith(".sktex")}

    def index_for(self, model_path, lod_path=None):
        """lod_path: the base record's hand-made distant model (STAT MNAM), if any."""
        if model_path in self.model_index:
            return self.model_index[model_path]
        resumed = getattr(self, "_resumed", {}).get(model_pack_name(model_path))
        if resumed is not None:
            self.model_index[model_path] = resumed
            return resumed
        idx = self._convert(model_path, lod_path)
        self.model_index[model_path] = idx
        return idx

    def reconvert(self, model_path, lod_path=None):
        """Force re-conversion of a model while preserving its existing index in models.txt."""
        name = model_pack_name(model_path)
        existing_idx = getattr(self, "_resumed", {}).get(name)
        idx = self._convert(model_path, lod_path, existing_idx=existing_idx)
        if idx is not None:
            self.model_index[model_path] = idx
            if hasattr(self, "_resumed"):
                self._resumed[name] = idx
        return idx

    def collision_box(self):
        """The model of Skyrim's collision markers (invisible boxes: floors over gaps, walls
        where the art doesn't stop you): nothing drawn, a unit cube (-1..1, engine space) of
        collision; each placement scales it to its marker's box."""
        name = COLLISION_BOX_MODEL
        if name in self.model_index:
            return self.model_index[name]
        idx = getattr(self, "_resumed", {}).get(name)
        if idx is None:
            with open(os.path.join(self.pack, "meshes", name + ".skmesh"), "wb") as f:
                f.write(write_skmesh([], bounds=(np.full(3, -1.0), np.full(3, 1.0))))
            self.models.append(name)
            idx = len(self.models) - 1
        corners = np.array([[x, y, z] for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)], "<f4")
        faces = ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3))
        tris = np.array([[corners[a], corners[b], corners[c]] for q in faces for a, b, c in ((q[0], q[1], q[2]),
                                                                                         (q[0], q[2], q[3]))], "<f4")
        with open(os.path.join(self.pack, "meshes", name + ".skcol"), "wb") as f:
            f.write(struct.pack("<4sHHI", b"SKCO", SKCO_VERSION, 0, len(tris)) + tris.tobytes())
        self.model_index[name] = idx
        return idx

    def _write_collision(self, name, model_path, nif):
        """Skyrim's own collision (walkable ramps on stairs, smooth plank bridges, trunk
        capsules); models without any (plants, grass, small clutter) don't block."""
        if model_path.startswith(NO_COLLISION_PREFIXES):
            return
        try:
            tris = nif.collision_triangles(only_layers=EFFECTS_SOLID_LAYERS if model_path.startswith(EFFECTS_PREFIX) else None)
        except (ValueError, IndexError, struct.error):
            self.stats["collision unreadable"] += 1
            return
        if not len(tris) or not np.isfinite(tris).all():
            return
        pts = to_engine_space(tris.reshape(-1, 3)).astype("<f4")
        if model_path.startswith(ROAD_MODEL_PREFIX):
            pts[:, 1] += ROAD_LIFT_M + ROAD_COBBLE_LIFT_M  # (you walk on the cobbles)
        with open(os.path.join(self.pack, "meshes", name + ".skcol"), "wb") as f:
            f.write(struct.pack("<4sHHI", b"SKCO", SKCO_VERSION, 0, len(tris)))
            f.write(pts.tobytes())
        self.stats["collision meshes"] += 1
        self.stats["collision triangles"] += len(tris)

    def _write_mid(self, name, shapes, thin, precise=True):
        """Mid-distance LOD (5x5 cells around the player): a moderate simplification of
        the real model, so buildings don't turn into Skyrim's cardboard distant models
        until they're well into the haze."""
        submeshes = build_submeshes(shapes)
        if not submeshes:
            return None
        for sub in submeshes:
            simplify_submesh(sub, lod=LOD_MID, thin_cards=thin, precise=precise)
            self._convert_texture(sub)
        layer_road(self.current_path, submeshes)
        mid_name = name + "__mid"
        with open(os.path.join(self.pack, "meshes", mid_name + ".skmesh"), "wb") as f:
            f.write(write_skmesh(submeshes))
        self.stats["mid lod meshes"] += 1
        self.stats["mid lod triangles"] += sum(len(s["indices"]) // 3 for s in submeshes)
        return mid_name

    def _write_lod(self, name, shapes, lod_path, thin):
        """Far LOD: Skyrim's own distant model if the record names one, otherwise a
        heavy simplification of the full model. Returns the LOD mesh name or None."""
        source = "generated"
        if lod_path:
            try:
                shapes = NIF(find(self.mesh_archives, "meshes\\" + lod_path)).shapes()
                source = "skyrim"
            except (KeyError, ValueError, IndexError, struct.error):
                pass
        submeshes = build_submeshes(shapes)
        if not submeshes:
            return None
        for sub in submeshes:
            simplify_submesh(sub, lod=LOD_FAR, thin_cards=thin)
            self._convert_texture(sub)
        layer_road(self.current_path, submeshes)
        lod_name = name + "__lod"
        with open(os.path.join(self.pack, "meshes", lod_name + ".skmesh"), "wb") as f:
            f.write(write_skmesh(submeshes))
        self.stats[f"lod meshes ({source})"] += 1
        self.stats["lod triangles"] += sum(len(s["indices"]) // 3 for s in submeshes)
        return lod_name

    def _convert(self, model_path, lod_path=None, existing_idx=None):
        try:
            nif = NIF(find(self.mesh_archives, "meshes\\" + model_path))
            shapes = nif.shapes()
        except (KeyError, ValueError, IndexError, struct.error) as e:
            self.stats[f"unreadable: {type(e).__name__}"] += 1
            return None
        is_water = model_path.startswith(WATER_MODEL_PREFIX)
        if is_water:
            for s in shapes:
                s.texture, s.alpha_test = WATER_TEXTURE, False
        for s in shapes:  # (spider webs and cobwebs: Skyrim draws them with an effect shader - here
            # its texture, cut out - Arvel's web across Bleak Falls' doorway wasn't there at all)
            if not s.texture and s.effect_texture and "web" in s.effect_texture:
                s.texture, s.alpha_test, s.double_sided = s.effect_texture, True, True
        submeshes = build_submeshes(shapes)
        # Smoke columns use glow maps too; they aren't fires
        fx = [] if is_water or "smoke" in model_path else effect_points(shapes)
        if not submeshes and not fx:
            self.stats["no textured geometry"] += 1
            return None
        thin = is_vegetation(model_path)
        # Only pieces that meet or lie on each other need the precise full detail (see meshes.py)
        precise = not (model_path.startswith(LANDSCAPE_PREFIXES) and not model_path.startswith(ROAD_MODEL_PREFIX))
        for sub in submeshes:
            if not is_water and sub["texture"] != WATER_TEXTURE:  # (a model's water shapes: as they are)
                simplify_submesh(sub, thin_cards=thin, precise=precise,
                                 lock_border=model_path.startswith(LANDSCAPE_SEAM_PREFIXES))
                self._convert_texture(sub)
        layer_road(model_path, submeshes)
        self.current_path = model_path
        name = model_pack_name(model_path)
        fx_bounds = None
        if fx:
            pts = np.array([p[1:4] for p in fx])
            fx_bounds = (pts.min(0) - 0.5, pts.max(0) + 0.5)
        with open(os.path.join(self.pack, "meshes", name + ".skmesh"), "wb") as f:
            f.write(write_skmesh(submeshes, bounds=fx_bounds))
        self.stats["models"] += 1
        self.stats["model triangles"] += sum(len(s["indices"]) // 3 for s in submeshes)
        if existing_idx is not None:
            idx = existing_idx
            self.models[idx] = name
        else:
            self.models.append(name)
            idx = len(self.models) - 1
        self._write_collision(name, model_path, nif)
        if submeshes and not is_water:
            pos = np.concatenate([m["vertices"]["pos"] for m in submeshes])
            radius = float(np.linalg.norm(np.maximum(np.abs(pos.min(0)), np.abs(pos.max(0)))))
            if lod_path or radius > LOD_MIN_RADIUS_M:
                lod = self._write_lod(name, shapes, lod_path, thin)
                if lod:
                    self.lods[idx] = lod
            if lod_path or radius > MID_MIN_RADIUS_M:
                mid = self._write_mid(name, shapes, thin, precise)
                if mid:
                    self.mids[idx] = mid
        if fx:
            self.fx[idx] = fx
            self.stats["models with fire/glow"] += 1
        return idx

    def activator_models(self, model_path, kind):
        """A scripted activator's model split for animation, as door_models: its still part and
        its moving part with the motion the engine gives it (Skyrim animates these with Havok
        behaviours, which aren't converted): a puzzle pillar turns a third of a turn about its
        upright, a lever's handle tips over, a pull chain's chain slides down, a gate's bars
        rise, a secret wall sinks. kind: "pillar", "lever", "gate", "sink". None if no moving part is found."""
        key = ("activator", model_path, kind)
        if key in self.model_index:
            return self.model_index[key]
        result = None
        if kind == "web":  # (a spider's web: all of it, one piece to cut away - its effect texture cut out)
            try:
                nif = NIF(find(self.mesh_archives, "meshes\\" + model_path))
                shapes = nif.shapes()
                for s in shapes:
                    if not s.texture and s.effect_texture:
                        s.texture, s.alpha_test, s.double_sided = s.effect_texture, True, True
                self.current_path = model_path
                idx = self._convert_shapes(model_pack_name(model_path) + "__web", shapes,
                                           nif.collision_triangles(owner=None))
                if idx is not None:
                    result = (None, [(idx, np.zeros(3), np.array([0.0, 1.0, 0.0]), 0.0, np.zeros(3))])
                    self.stats["webs"] += 1
            except (KeyError, ValueError, IndexError, struct.error):
                self.stats["activator models unreadable"] += 1
            self.model_index[key] = result
            return result
        try:
            nif = NIF(find(self.mesh_archives, "meshes\\" + model_path))
            nodes = [nif.string(Reader(nif.data, nif.block_offsets[i]).i32())
                     for i, t in enumerate(nif.block_types) if t == "NiNode"]
            want = {"pillar": ("pillar",), "lever": ("lever01", "chain01", "lever", "handle"), "gate": ("gate", "bars"),
                    "sink": (),
                    "bridge": ("base01",), "clawring2": (), "clawdoor": ()}[kind]
            node = next((n for w in want for n in nodes if w in n.lower()), None)
            if node is None and kind != "lever":
                node = nodes[0] if nodes else None
            if node is not None:
                shapes, owners = nif.shapes_by_node([node])
                mine = [sh for sh, o in zip(shapes, owners) if o == node]
                still = [sh for sh, o in zip(shapes, owners) if o != node]
                if kind in ("clawring2", "clawdoor", "sink"):  # (a claw door's ring, keyhole, base; a secret wall: all of it turns / goes)
                    if kind == "clawdoor":
                        mine = [s for s in shapes if not s.name.lower().startswith("dragonclaw")]
                        still = []
                    else:
                        mine, still = list(shapes), []
                world = nif.node_world(node)
                if mine and world is not None:
                    name = model_pack_name(model_path)
                    self.current_path = model_path
                    still_idx = self._convert_shapes(name + "__still", still, nif.collision_triangles(owner=None)) \
                        if still else None
                    idx = self._convert_shapes(name + "__act", mine, nif.collision_triangles(owner=node))
                    pts = np.concatenate([sh.positions for sh in mine])
                    pivot = to_engine_space(world[1].reshape(1, 3))[0]
                    zero = np.zeros(3)
                    if kind == "pillar":
                        motion = (np.array([0.0, 1.0, 0.0]), 2.0 * np.pi / 3.0, zero)
                    elif kind == "clawring2":
                        # (a third of a turn about the door's facing: the ring's thinnest way, whichever
                        # axis its model was made along - Skyrim's (x, y, z) is the engine's (x, z, -y))
                        ext = pts.max(0) - pts.min(0)
                        thin = int(np.argmin(ext))
                        axis = [np.array([1.0, 0.0, 0.0]), np.array([0.0, 0.0, -1.0]), np.array([0.0, 1.0, 0.0])][thin]
                        motion = (axis, 2.0 * np.pi / 3.0, zero)
                        # (about its own middle: the door's models have their origin at the floor - turned
                        # about that, a ring swung down into it)
                        pivot = to_engine_space(((pts.max(0) + pts.min(0)) * 0.5).reshape(1, 3))[0]
                    elif kind == "clawdoor":
                        motion = (np.array([0.0, 1.0, 0.0]), 0.0, zero)
                    elif kind == "bridge":  # (its deck up on its axle: Skyrim's Y, the engine's -Z)
                        motion = (np.array([0.0, 0.0, 1.0]), 1.45, zero)
                    elif kind == "sink":  # (a secret wall: down into the floor, all of it)
                        drop = (pts[:, 2].max() - pts[:, 2].min()) * 0.95 / UNITS_PER_METER
                        motion = (np.array([0.0, 1.0, 0.0]), 0.0, np.array([0.0, -drop, 0.0]))
                    elif kind == "gate":
                        rise = (pts[:, 2].max() - pts[:, 2].min()) * 0.9 / UNITS_PER_METER
                        motion = (np.array([0.0, 1.0, 0.0]), 0.0, np.array([0.0, rise, 0.0]))
                    elif "chain" in node.lower():
                        motion = (np.array([0.0, 1.0, 0.0]), 0.0, np.array([0.0, -0.25, 0.0]))
                    else:  # a lever's handle: over about its base's long side
                        motion = (np.array([1.0, 0.0, 0.0]), 0.9, zero)
                    if idx is not None:
                        result = (still_idx, [(idx, pivot, motion[0], motion[1], motion[2])])
                        self.stats["animated activators"] += 1
        except (KeyError, ValueError, IndexError, struct.error):
            self.stats["activator models unreadable"] += 1
        self.model_index[key] = result
        return result

    def trap_models(self, model_path, kind):
        """A trap's model split for its motion (Skyrim's traps move by Havok, not by keyframes,
        so each kind's is the engine's): (still part index or None, [(index, pivot, axis,
        angle, slide)], (lo, hi) its bounds, muzzle point or None), engine space. kind:
        "pendulum" (a swinging blade, a mace, a battering ram: axis and angle are the swing's
        from rest to hanging straight down), "wall" (turns about its pole), "spear" (thrusts up),
        "jaws" (a bear trap's two jaws close), "fall" (it all drops: rocks, an oil lamp),
        "plate" (pressed down), "still" (nothing moves: darts' ports, tripwires, oil). None if
        the model can't be read."""
        key = ("trap", model_path, kind)
        if key in self.model_index:
            return self.model_index[key]
        result = None
        try:
            nif = NIF(find(self.mesh_archives, "meshes\\" + model_path))
            name = model_pack_name(model_path)
            self.current_path = model_path
            shapes = [s for s in nif.shapes() if s.texture]
            all_pts = np.concatenate([s.positions for s in shapes]) if shapes else np.zeros((1, 3))
            lo, hi = to_engine_space(all_pts).min(0), to_engine_space(all_pts).max(0)
            nodes = [nif.string(Reader(nif.data, nif.block_offsets[i]).i32())
                     for i, t in enumerate(nif.block_types) if t == "NiNode"]
            muzzle = nif.node_world("ProjectileNode") if "ProjectileNode" in nodes else None
            muzzle = to_engine_space(muzzle[1].reshape(1, 3))[0] if muzzle is not None else None
            parts, still = [], shapes
            zero = np.zeros(3)

            def split(moving, pivot, axis, angle, slide, label):
                nonlocal still
                if not moving:
                    return
                idx = self._convert_shapes(f"{name}__{label}", moving, np.zeros((0, 3, 3)))
                if idx is not None:
                    parts.append((idx, pivot, axis, angle, slide))
                    still = [s for s in still if all(s is not m for m in moving)]
            if kind == "pendulum":
                pivot_node = next((n for n in ("LogPivot01", "Blade01", "Base01") if n in nodes), None)
                world = nif.node_world(pivot_node) if pivot_node else None
                moving = [s for s in shapes if not s.name.lower().startswith(("braces", "pin", "base01"))] \
                    if pivot_node != "Blade01" else [s for s in shapes if s.name.startswith("Blade")]
                if world is not None and moving:
                    p = world[1]
                    arm = np.concatenate([s.positions for s in moving]).mean(0) - p
                    down = np.array([0.0, 0.0, -1.0])
                    axis = np.cross(arm, down)
                    if np.linalg.norm(axis) > 1e-3:
                        axis /= np.linalg.norm(axis)
                        angle = float(np.arccos(np.clip(arm @ down / np.linalg.norm(arm), -1, 1)))
                        split(moving, to_engine_space(p.reshape(1, 3))[0], np.array([axis[0], axis[2], -axis[1]]),
                              angle, zero, "swing")
            elif kind == "wall":
                moving = [s for s in shapes if s.name.lower().startswith("spikes")]
                # (its spikes swing out toward the corridor: the trap faces the way it's entered - seen
                # in Bleak Falls Barrow, +1.8 turned them away into the wall)
                split(moving, zero, np.array([0.0, 1.0, 0.0]), -1.8, zero, "swing")
            elif kind == "spear":
                from nif import Shape
                z_ext = float(all_pts[:, 2].max() - all_pts[:, 2].min())
                stroke = z_ext / UNITS_PER_METER
                # author authored extended (+Z in Skyrim). Shift back by z_ext
                # so the default rest pose (u=0.0) is retracted, sliding forward by stroke when sprung.
                retracted = [
                    Shape(s.name, s.positions - np.array([0.0, 0.0, z_ext], dtype=np.float32),
                          s.uvs, s.normals, s.colors, s.triangles, s.texture, s.alpha_test,
                          s.effect_texture, s.double_sided, s.decal)
                    for s in shapes
                ]
                split(retracted, zero, np.array([0.0, 1.0, 0.0]), 0.0, np.array([0.0, stroke, 0.0]), "thrust")
            elif kind == "jaws":
                for jaw, sign in (("FrontJaw01", 1.0), ("BackJaw01", -1.0)):
                    w = nif.node_world(jaw)
                    moving = [s for s in shapes if s.name.startswith(jaw)]
                    if w is not None:
                        split(moving, to_engine_space(w[1].reshape(1, 3))[0], np.array([1.0, 0.0, 0.0]), sign * 1.45,
                              zero, jaw.lower())
            elif kind == "fall":
                split([s for s in shapes if not s.name.lower().startswith("glow")], zero, np.array([0.0, 1.0, 0.0]), 0.0,
                      np.array([0.0, -1.0, 0.0]), "fall")
            elif kind == "plate":
                split([s for s in shapes if s.name.lower().startswith(("pressureplate", "plate", "triggerplate"))], zero,
                      np.array([0.0, 1.0, 0.0]), 0.0, np.array([0.0, -0.05, 0.0]), "press")
            still_idx = self._convert_shapes(name + "__trapstill", still, nif.collision_triangles(owner=None)) \
                if still else None
            result = (still_idx, parts, (lo, hi), muzzle)
            self.stats["trap models"] += 1
        except (KeyError, ValueError, IndexError, struct.error):
            self.stats["trap models unreadable"] += 1
        self.model_index[key] = result
        return result

    def door_models(self, model_path, sequence="Open"):
        """A model with an "Open" sequence (doors, gates) split for animation: (index of its
        still part or None, [(index, pivot, axis, angle, slide)]) with each moving part's
        swing from closed to open (model space, engine axes and metres: turning angle
        radians about axis through pivot, then sliding). None if it doesn't animate."""
        key = ("door", model_path) if sequence == "Open" else ("door", model_path, sequence)
        if key in self.model_index:
            return self.model_index[key]
        result = None
        try:
            nif = NIF(find(self.mesh_archives, "meshes\\" + model_path))
            pose = nif.sequence_end_pose(sequence)  # (a sarcophagus's lid: "openEnd", off and over)
            if not pose and sequence == "openEnd":  # (an upright one's lid: its behaviour's alone - Skuldafn's secret way)
                pose = nif.lone_transform_end()
            if pose:
                shapes, owners = nif.shapes_by_node(list(pose.keys()))
                name = model_pack_name(model_path)
                self.current_path = model_path
                still = [sh for sh, o in zip(shapes, owners) if o is None]
                still_idx = self._convert_shapes(name + "__still", still, nif.collision_triangles(owner=None)) if still else None
                parts = []
                for node, (end_rot, end_trans) in pose.items():
                    mine = [sh for sh, o in zip(shapes, owners) if o == node]
                    world, parent = nif.node_world(node), nif.node_parent_world(node)
                    if not mine or world is None or parent is None:
                        continue
                    (wr, wt, _ws), (pr, pt, ps) = world, parent
                    lc_r = pr.T @ wr
                    lc_t = pr.T @ (wt - pt) / ps
                    lo_r = end_rot if end_rot is not None else lc_r
                    rd = pr @ lo_r @ lc_r.T @ pr.T
                    slide = pr @ (end_trans - lc_t) * ps if end_trans is not None else np.zeros(3)
                    angle = float(np.arccos(np.clip((np.trace(rd) - 1.0) / 2.0, -1.0, 1.0)))
                    axis = np.array([rd[2, 1] - rd[1, 2], rd[0, 2] - rd[2, 0], rd[1, 0] - rd[0, 1]])
                    axis = axis / np.linalg.norm(axis) if np.linalg.norm(axis) > 1e-6 else np.array([0.0, 0.0, 1.0])
                    idx = self._convert_shapes(f"{name}__part{len(parts)}", mine, nif.collision_triangles(owner=node))
                    if idx is None:
                        continue
                    pivot = to_engine_space(wt.reshape(1, 3))[0]
                    parts.append((idx, pivot, C @ axis, angle, C @ slide / UNITS_PER_METER))
                if parts:
                    result = (still_idx, parts)
                    self.stats["animated doors"] += 1
        except (KeyError, ValueError, IndexError, struct.error, np.linalg.LinAlgError):
            self.stats["door models unreadable"] += 1
        self.model_index[key] = result
        return result

    def _convert_shapes(self, name, shapes, collision_tris):
        """A model from given shapes (root space) and collision triangles; its index or None."""
        submeshes = build_submeshes(shapes)
        if not submeshes:
            return None
        for sub in submeshes:
            simplify_submesh(sub, precise=True)
            self._convert_texture(sub)
        with open(os.path.join(self.pack, "meshes", name + ".skmesh"), "wb") as f:
            f.write(write_skmesh(submeshes))
        reuse = getattr(self, "_resumed", {}).get(name)  # (re-exporting: same name, same index)
        if reuse is None:
            self.models.append(name)
        if len(collision_tris) and np.isfinite(collision_tris).all():
            pts = to_engine_space(collision_tris.reshape(-1, 3)).astype("<f4")
            with open(os.path.join(self.pack, "meshes", name + ".skcol"), "wb") as f:
                f.write(struct.pack("<4sHHI", b"SKCO", SKCO_VERSION, 0, len(collision_tris)))
                f.write(pts.tobytes())
        self.stats["models"] += 1
        return reuse if reuse is not None else len(self.models) - 1

    def _convert_texture(self, sub):
        if sub["texture"] in self.textures_done:
            return
        self.textures_done.add(sub["texture"])
        try:
            data = find(self.tex_archives, sub["source"])
        except KeyError:
            self.stats["missing textures"] += 1
            return
        alpha_test = bool(sub["flags"] & 1)
        sktex, _preview = convert(data, max_size=texture_budget(sub["source"]),
                                  keep_alpha=alpha_test, alpha_test=alpha_test, cut=sub.get("cut", 0))
        with open(os.path.join(self.pack, "textures", sub["texture"] + ".sktex"), "wb") as f:
            f.write(sktex)


LOCKPICK_MODELS = ("interface\\lockpicking\\lockpickshiv01.nif", "interface\\lockpicking\\lockpick01.nif")


def write_lockpick_models(models, pack):
    """Skyrim's lockpicking screen: the lock (still), its tumbler (turns with the lock), the
    shiv (the screwdriver: turns with it) and the pick (rotates about the keyhole).
    PACK_DIR/ui/lockpick.txt: "<part> <model index>" lines, then "keyhole x y z" and
    "tip x y z" (the pick's point, model space: the engine puts it in the keyhole)."""
    shiv = NIF(find(models.mesh_archives, "meshes\\" + LOCKPICK_MODELS[0]))
    shapes, owners = shiv.shapes_by_node(["LockPickLockTumble", "Shiv"])
    no_col = np.zeros((0, 3, 3))
    parts = {}
    for key, owner in (("still", None), ("tumbler", "LockPickLockTumble"), ("shiv", "Shiv")):
        mine = [s for s, o in zip(shapes, owners) if o == owner]
        parts[key] = models._convert_shapes("ui_lockpick_" + key, mine, no_col)
    tumbler = np.concatenate([s.positions for s, o in zip(shapes, owners) if o == "LockPickLockTumble"])
    keyhole = to_engine_space(((tumbler.min(0) + tumbler.max(0)) * 0.5).reshape(1, 3))[0]
    pick = NIF(find(models.mesh_archives, "meshes\\" + LOCKPICK_MODELS[1])).shapes()
    parts["pick"] = models._convert_shapes("ui_lockpick_pick", pick, no_col)
    pts = np.concatenate([s.positions for s in pick])
    tip = to_engine_space(pts[np.argmax(pts[:, 1])].reshape(1, 3))[0]  # (nearest the lock face)
    os.makedirs(os.path.join(pack, "ui"), exist_ok=True)
    with open(os.path.join(pack, "ui", "lockpick.txt"), "w") as f:
        for key, idx in parts.items():
            f.write(f"{key} {idx if idx is not None else -1}\n")
        f.write("keyhole %.4f %.4f %.4f\ntip %.4f %.4f %.4f\n" % (*keyhole, *tip))
    print("lockpicking models:", parts)


def write_fx_textures(pack, tex_archives):
    """The flame flipbook (kept with alpha) and a generated soft radial glow."""
    from PIL import Image
    from textures import convert_image
    import io
    atlas = Image.open(io.BytesIO(find(tex_archives, FIRE_ATLAS))).convert("RGBA")
    with open(os.path.join(pack, "textures", "fx_fire.sktex"), "wb") as f:
        f.write(convert_image(atlas, max_size=256, keep_alpha=True))
    r = np.linalg.norm(np.mgrid[-1:1:64j, -1:1:64j], axis=0)
    a = np.clip(1.0 - r, 0, 1) ** 2
    glow = np.dstack([np.full_like(a, 1.0), np.full_like(a, 0.75), np.full_like(a, 0.45), a])
    with open(os.path.join(pack, "textures", "fx_glow.sktex"), "wb") as f:
        f.write(convert_image(Image.fromarray((glow * 255).astype(np.uint8)), max_size=64, keep_alpha=True))
    write_spell_textures(pack, tex_archives)


def _white(alpha):
    """White, with this alpha (the engine tints sprites with their vertex colour)."""
    from PIL import Image
    a = np.asarray(alpha, np.uint8)
    return Image.fromarray(np.dstack([np.full_like(a, 255)] * 3 + [a]))


def write_spell_textures(pack, tex_archives):
    """Spell sprites from Skyrim's effect textures (its effect shaders tint greyscale
    textures; here they're baked white or coloured, and the engine tints them):

      fx_flame   Flames' stream: fxfireanim04loop's 32 frames in an 8 x 4 atlas,
                 coloured along a flame ramp (white-yellow cores, orange-red edges)
      fx_rays    Healing's gleams: the fxbrightfash01 starburst
      fx_sparks  fxglowysparks: 4 x 4 sparks
      fx_white   fxglowwhite01: a soft white glow
      fx_ring    a soft ring (Healing's circle at the caster's feet)
      fx_rune    runefire01's sigil (Fire Rune), fx_bolt fxlightningbolt01's two bolt strips
                 (Sparks), fx_arcs shockarcs01's 2 x 2 arcs (Sparks' crackle)
    """
    from PIL import Image
    from textures import convert_image
    import io

    def load(name):
        return Image.open(io.BytesIO(find(tex_archives, "textures\\effects\\" + name + ".dds"))).convert("RGBA")

    def write(name, img, size):
        with open(os.path.join(pack, "textures", name + ".sktex"), "wb") as f:
            f.write(convert_image(img, max_size=size, keep_alpha=True))

    # Flames: 8 x 4 frames of 256 x 256, each 64 x 64 here
    src = load("fxfireanim04loop")
    fw, fh = src.width // 8, src.height // 4
    atlas = Image.new("RGBA", (512, 256))
    for k in range(32):
        frame = src.crop(((k % 8) * fw, (k // 8) * fh, (k % 8 + 1) * fw, (k // 8 + 1) * fh)).resize((64, 64), Image.LANCZOS)
        atlas.paste(frame, ((k % 8) * 64, (k // 8) * 64))
    a = np.asarray(atlas, np.float32) / 255.0
    g = np.clip(np.maximum(a[..., :3].max(axis=2), 0.0) * a[..., 3], 0, 1)  # (greyscale x alpha: the flame's heat)
    rgb = np.dstack([np.clip(0.55 + 0.9 * g, 0, 1), np.clip(g * 1.25, 0, 1) ** 1.3 * 0.85, np.clip(g * 1.3 - 0.3, 0, 1) ** 2 * 0.7])
    alpha = np.clip(g * 1.6, 0, 1)
    write("fx_flame", Image.fromarray((np.dstack([rgb, alpha]) * 255).astype(np.uint8)), 512)

    rays = np.asarray(load("fxbrightfash01").convert("L").resize((128, 128), Image.LANCZOS))
    write("fx_rays", _white(rays), 128)
    write("fx_sparks", _white(load("fxglowysparks").getchannel("A")), 64)
    write("fx_white", _white(load("fxglowwhite01").getchannel("A").resize((64, 64), Image.LANCZOS)), 64)
    r = np.linalg.norm(np.mgrid[-1:1:128j, -1:1:128j], axis=0)
    ring = np.clip(1.0 - np.abs(r - 0.82) / 0.14, 0, 1) ** 1.5 + 0.25 * np.clip(1.0 - np.abs(r - 0.55) / 0.08, 0, 1)
    write("fx_ring", _white((np.clip(ring, 0, 1) * 255).astype(np.uint8)), 128)
    # Fire Rune's sigil (runefire01: the glyph in its alpha), Sparks' bolts (fxlightningbolt01:
    # two strips side by side) and crackle (shockarcs01: 2 x 2 arcs)
    write("fx_rune", _white(load("runefire01").getchannel("A").resize((256, 256), Image.LANCZOS)), 256)
    for name, src, size in (("fx_bolt", "fxlightningbolt01", 256), ("fx_arcs", "shockarcs01", 256)):
        img = np.asarray(load(src), np.float32) / 255.0
        lum = np.clip(img[..., :3].max(axis=2) * img[..., 3], 0, 1)
        im = _white((lum * 255).astype(np.uint8)).resize((size, size), Image.LANCZOS)
        write(name, im, size)


def entry_for_ref(load_order, models, ref, skipped):
    """Packed .skobj entry for a placed reference (converting its model), or None."""
    name, data = ref.sub("NAME"), ref.sub("DATA")
    if not name or not data or (ref.editor_id() or "").lower() in OPENING_REMOVED:
        return None
    base_id = load_order.global_id(ref.plugin, struct.unpack("<I", name)[0])
    base = load_order.record(base_id, PLACEABLE_TYPES) if base_id is not None else None
    xprm = ref.sub("XPRM")
    if base is not None and base.editor_id() == "CollisionMarker" and xprm and len(xprm) >= 32 and struct.unpack_from("<I", xprm, 28)[0] == XPRM_BOX:
        # (an invisible box of collision: its half-extents, Skyrim's axes -> engine x, z, -y)
        hx, hy, hz = struct.unpack_from("<3f", xprm)
        xscl = ref.sub("XSCL")
        scale = struct.unpack("<f", xscl)[0] if xscl else 1.0
        m, t = ref_transform(data, scale)
        m = m @ np.diag([hx, hz, hy]) / 70.0
        skipped["collision boxes"] += 1
        return struct.pack("<HH9f3f", models.collision_box(), ref.switch, *m.reshape(-1), *t)
    modl = base.sub("MODL") if base else None
    if not modl:
        skipped["no model / unsupported base type"] += 1
        return None
    model_path = modl.rstrip(b"\0").decode("cp1252").lower().replace("/", "\\")
    if "marker" in model_path:
        skipped["editor marker"] += 1
        return None
    mnam = base.sub("MNAM") if base.type == "STAT" else None
    lod_path = None
    if mnam:
        first = mnam[:260].split(b"\0")[0].decode("cp1252").lower().replace("/", "\\")
        lod_path = first if first and "iclod" not in first else None
    idx = models.index_for(model_path, lod_path)
    if idx is None:
        skipped["model not convertible"] += 1
        return None
    xscl = ref.sub("XSCL")
    m, t = ref_transform(data, struct.unpack("<f", xscl)[0] if xscl else 1.0)
    return struct.pack("<HH9f3f", idx, ref.switch, *m.reshape(-1), *t)  # (its world switch: esm.SWITCHES)


def write_model_table(models, pack):
    """objects/models.txt (and fx.txt, the models' fire and glow points)."""
    with open(os.path.join(pack, "objects", "models.txt"), "w") as f:
        f.write("".join(f"{n} {models.lods.get(i, '-')} {models.mids.get(i, '-')}\n"
                        for i, n in enumerate(models.models)))
    with open(os.path.join(pack, "objects", "fx.txt"), "w") as f:
        for idx, points in sorted(models.fx.items()):
            for kind, x, y, z, w, h in points:
                f.write(f"{idx} {kind} {x:.3f} {y:.3f} {z:.3f} {w:.3f} {h:.3f}\n")


def cell_writer(load_order, models, inside, pack, skipped):
    """write(prefix, (x, y), refs, cell record): a cell's placed objects (objects/
    <prefix>_X_Y.skobj, for the bake) and, apart from them, the containers and loose items
    you can open and take (<prefix>_X_Y.skloot: char[4] "SKLO", u16 version, u16 0, i32 x,
    y, then the containers and loose items as in an interior's .skint, see interiors.py;
    lit by the bake's midday sun). It returns the number of objects placed."""
    from bake_cells import bake_light

    def write(prefix, key, refs, cell_rec):
        x, y = key
        entries, containers, loose, traps, acts, swings = [], [], [], [], [], []
        furniture = [f for f in (inside._furniture(r) for r in refs) if f]
        children = {}  # (activate parents: what a lever opens)
        for r in refs:
            for t, v in r.subrecords():
                if t == "XAPR" and len(v) >= 4:
                    children.setdefault(load_order.global_id(r.plugin, _u32(v)), []).append(
                        load_order.global_id(r.plugin, r.form_id))
        for ref in refs:
            if (ref.editor_id() or "").lower() in OPENING_MOVED:
                continue  # (the engine's opening draws the cart, moving: engine/src/intro.c)
            if ref.switch & SWITCH_ON_SHOWS:  # (only once a switch is on: its look alone - no doors, loot...)
                entry = entry_for_ref(load_order, models, ref, skipped)
                if entry:
                    entries.append(entry)
                continue
            swing = inside._swing_door(ref, bake_light, skipped)
            if swing is not None:  # (a tower's door, a farmhouse's gate)
                if swing[0]:
                    entries.append(swing[0])
                swings.append(swing[1])
                continue
            trap = inside._trap(ref, bake_light)
            if trap is not None:
                if trap[0]:
                    entries.append(trap[0])
                traps.append(trap[1])
                continue
            act = inside._activator(ref, children, bake_light)
            if act is not None:  # (a puzzle by the road, its lever)
                if act[0]:
                    entries.append(act[0])
                acts.append(act[1])
                continue
            loot = inside._loot(ref, cell_rec, bake_light)
            if loot is not None:
                entry, container, item = loot
                if entry:
                    entries.append(entry)
                if container:
                    containers.append(container)
                if item:
                    loose.append(item)
                continue
            entry = entry_for_ref(load_order, models, ref, skipped)
            if entry:
                entries.append(entry)
        if entries:
            with open(os.path.join(pack, "objects", f"{prefix}_{x}_{y}.skobj"), "wb") as f:
                f.write(struct.pack("<4sHHiiI", b"SKOB", SKOB_VERSION, 0, x, y, len(entries)))
                f.write(b"".join(entries))
        if containers or loose or furniture or traps or acts or swings:
            with open(os.path.join(pack, "objects", f"{prefix}_{x}_{y}.skloot"), "wb") as f:
                f.write(struct.pack("<4sHHii", b"SKLO", SKLO_VERSION, 0, x, y))
                f.write(struct.pack("<I", len(containers)) + b"".join(containers))
                f.write(struct.pack("<I", len(loose)) + b"".join(loose))
                f.write(struct.pack("<I", len(furniture)) + b"".join(furniture))
                f.write(struct.pack("<I", len(traps)) + b"".join(traps))
                f.write(struct.pack("<I", len(acts)) + b"".join(acts))
                f.write(struct.pack("<I", len(swings)) + b"".join(swings))
        return len(entries)
    return write


def write_exterior_cells(load_order, cells, models, inside, pack, bounds, skipped):
    """The region's Tamriel cells (see cell_writer), then the walled cities' (worlds.py).
    Returns (objects placed, the cities: worlds.export's)."""
    import worlds
    min_x, min_y, max_x, max_y = bounds
    for old in glob.glob(os.path.join(pack, "objects", "*.skloot")) + \
            glob.glob(os.path.join(pack, "objects", "*_*_*.skobj")):
        os.remove(old)
    write = cell_writer(load_order, models, inside, pack, skipped)
    placed = 0
    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            cell = cells.get((x, y), {"refs": [], "cell": None})
            placed += write("tamriel", (x, y), cell["refs"], cell.get("cell"))
        print(f"row y={y} done ({len(models.models)} models so far)")
    cities = worlds.export(load_order, cells, models, inside, pack, entry_for_ref, write)
    print("exterior loot:", {k: v for k, v in inside.stats.items() if k in ("containers", "loose items")})
    return placed, cities


def main(argv):
    if len(argv) != 6:
        print(__doc__)
        return 1
    pack = argv[1]
    min_x, min_y, max_x, max_y = map(int, argv[2:])
    for d in ("objects", "meshes", "textures"):
        os.makedirs(os.path.join(pack, d), exist_ok=True)

    # Skyrim + Update + DLC masters merged in load order: overrides move, replace or
    # remove placed objects (Update.esm alone touches ~8000 Tamriel references)
    load_order = LoadOrder(SKYRIM_DATA)
    cells = load_order.exterior_cells("Tamriel")
    models = ModelConverter(pack)
    # (containers and loose items, outdoors and in: one item database)
    inside = interiors.Exporter(load_order, cells, models, StringTables(load_order), entry_for_ref)
    inside.items = items.Database(load_order, StringTables(load_order))
    skipped = Counter()
    placed, cities = write_exterior_cells(load_order, cells, models, inside, pack, (min_x, min_y, max_x, max_y), skipped)
    inside.cities = cities  # (their doors and interiors)

    # Interiors behind the region's doors (their models join the model table)
    interior_index = inside.export(pack, (min_x, min_y, max_x, max_y))
    write_lockpick_models(models, pack)
    write_model_table(models, pack)
    write_fx_textures(pack, models.tex_archives)
    bake(pack)  # the engine loads cells pre-built (bake_cells.py)
    inside.bake(pack)
    # NPCs (animated, with schedules) and the navmeshes they walk on
    people = npcs.Exporter(load_order, cells, models, find, cities)
    people.items = inside.items
    people.export(pack, (min_x, min_y, max_x, max_y), interior_index)
    navmesh.export(load_order, cells, pack, (min_x, min_y, max_x, max_y))  # (the opening's cart road is found on it)
    import quests  # (the main quest's opening scenes and items: before the gear, which they add to)
    quests.export(load_order, pack, people.talk, inside.items, models)
    import weather, extract_sounds, creatures, perks, worldmap
    # Creatures and enemies (their bodies' death items and inventories join the items: before
    # the gear, so what you take from them - a soldier's armour - can be worn)
    creatures.export(load_order, models, cells, pack, (min_x, min_y, max_x, max_y), people, StringTables(load_order),
                     inside, interior_index)
    npcs.export_player(people, pack, StringTables(load_order))  # (and its gear)
    import stones
    stones.export(load_order, cells, pack, (min_x, min_y, max_x, max_y), StringTables(load_order))
    stones.export_walls(load_order, cells, inside, interior_index, pack, (min_x, min_y, max_x, max_y))
    inside.items.write(pack, models)
    write_model_table(models, pack)  # (again: the items' and creatures' own models joined it)
    animations.export(pack, models.mesh_archives, find)
    skills.export(load_order, pack, StringTables(load_order))
    perks.export(load_order, pack, StringTables(load_order))  # (the perk trees and the spells)
    worldmap.export(load_order, cells, pack, (min_x, min_y, max_x, max_y), StringTables(load_order))
    weather.export(load_order, pack)
    extract_sounds.export(pack)
    print(f"placed {placed} objects using {len(models.models)} models, {len(models.textures_done)} textures")
    print("skipped:", dict(skipped))
    print("converter stats:", dict(models.stats))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
