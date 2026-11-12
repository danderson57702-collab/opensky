"""Interiors: the buildings and dungeons whose doors are in the converted region.

Every interior reachable through a door from the region (and through their own doors,
a few levels deep) is converted whole: its objects (sharing the exterior's models),
Skyrim's lights baked into the vertex colours together with the cell's ambient light,
its fog, navmesh, collision placements and doors.

  PACK_DIR/interiors/interiors.txt     interior names (editor ids, lowercase), a tab and the
                                       place's name ("Dustman's Cairn": the map's markers and
                                       "Cleared" go by it), one per line; the line number is
                                       the interior's index
  PACK_DIR/interiors/<name>.skint      the interior
  PACK_DIR/interiors/<name>.sknav      its navmesh (navmesh.py layout, cell 0, 0)
  PACK_DIR/interiors/exterior.skdoor   the doors outside that lead into them (and into cities)
  PACK_DIR/interiors/<city>.skdoor     a walled city's doors (into interiors, out to Tamriel)

.skint layout (little-endian), version 1:
  char[4] "SKIT", u16 version, u16 0, u8 name length, display name (cp1252), pad to 4
  f32 fog rgb[3], f32 fog near, f32 fog far (metres)
  f32 bounds min[3], max[3]
  u32 door count, doors (below)
  u32 tile count; per tile: f32 min[3], f32 max[3], u32 size, a .skbat (bake_cells.py)
      of the tile's objects, zero-padded to 4
  u32 placement count, placements (.skobj entries, 52 bytes), for collision
  (version 2) u32 swing door count; per door (doors that open in place, Skyrim's "Open"
      sequences; their frames are in the tiles and placements):
      f32 m[9], t[3] (placement), f32 centre[3] (where to activate it), u8 light rgb
      (interior light at the door, x2 scale), u8 part count; per moving part: u16 model,
      u16 0, f32 pivot[3], f32 axis[3], f32 angle (radians), f32 slide[3] (model space:
      closed -> open turns angle about axis through pivot, then slides)
  (version 3) u32 container count; per container: u32 reference form id, u16 container
      (index in items/items.txt), u8 lock level (0: unlocked), u8 flags (1 owned: taking is
      stealing), (version 5) f32 aim radius (metres, about its model's centre), f32 m[9], t[3], f32 centre[3], u8 light rgb, u8 part count, parts as for
      swing doors (lids and drawers that open with it: Skyrim's Open sequence; a container
      without one is in the tiles whole)
  u32 loose item count; per item (not in the tiles: they can be taken): u32 reference form
      id, u16 item (index in items/items.txt), u16 model, u8 flags (1 owned), u8 light rgb,
      f32 m[9], t[3]
  (version 6) u32 activator count; per activator (Skyrim's scripted ones the engine runs):
      u32 reference form id, u8 kind (1 puzzle pillar, 2 lever / pull chain, 3 a pillar
      puzzle's lever, 4 gate), u8 state (a pillar's starting face, 1-3; a gate: 2 open at the start), u8 solve (its solved
      face), u8 label length, f32 aim radius, u32 controller (a pillar's puzzle lever), u8 n,
      u8 failures, u16 0, u32 refs it activates x n (a puzzle lever: on success), u32 refs x
      failures (on failure: the traps), label (cp1252, zero-padded to 4), then a door-like
      record as for swing doors (its moving part's motion: a pillar's one step)
  (version 7) u32 furniture count; per piece (chairs, benches, stools, beds: its model is in
      the tiles; this is how to use it): u32 reference form id, u8 kind (1 seat, 2 stool, 3
      bed, 4 bedroll, 5 alchemy lab, 6 crafting station: no markers), u8 marker count, u8 label length, u8 station
      (STATIONS: 1 forge .. 7 enchanter; 0), f32 centre[3], f32 aim radius,
      per marker: f32 pos[3] (engine space, world), f32 heading (as engine headings), u16
      animation type (1 sit, 2 sleep), u16 entry points (Skyrim's: 1 front, 2 behind, 4
      right, 8 left), then the label ("Sit", "Sleep (Bed)"), zero-padded to 4
.skdoor: char[4] "SKDR", u16 version, u16 0, u32 count, doors
door: f32 pos[3] (the door), i32 destination (-1: Tamriel, -2 - n: walled city n (worlds.txt
      line n, see worlds.py), else an interior index),
      f32 arrival[3], f32 arrival yaw (radians; facing (sin, 0, -cos)),
      (SKDR 2, SKIT 4) f32 centre[3], f32 radius (the model as placed: where the crosshair
      has to pass to use it), u8 label length, label (where it leads, cp1252), zero-padded to 4
"""
import os
import struct
from collections import Counter, deque

import numpy as np

from bake_cells import LOD_FULL, Models, batches_file, build_variant
from extract_textures import find
from items import ITEM_TYPES
from nif import NIF
from worlds import CITIES, OWN_LAND

SKIT_VERSION = 9  # (8: traps after the furniture; 9: rooms and portals, each tile's room)
ROOM_MARKER, PORTAL_MARKER = 0x1F, 0x20  # (Skyrim's room bounds and the portals between them: what's drawn)
FURN_SIT, FURN_SLEEP = 1, 2  # BSFurnitureMarkerNode animation types
FURN_KIND_SEAT, FURN_KIND_STOOL, FURN_KIND_BED, FURN_KIND_BEDROLL, FURN_KIND_ALCHEMY, FURN_KIND_CRAFT = 1, 2, 3, 4, 5, 6
FURN_KIND_RESOURCE = 7  # an ore vein (ACTI, mineorescript), a chopping block (resourcefurniturescript): items.py Z lines
ORE_PER_VEIN = 3        # (Skyrim's veins: a few strikes' worth, then depleted for a month)
WORKBENCH_ALCHEMY = (5, 6)  # FURN WBDT bench types: alchemy, alchemy experiment
WORKBENCH_ENCHANTING = (3, 4)  # ... enchanting, enchanting experiment
# Crafting stations by their keyword (items.py's recipes say which): 1 forge (the Skyforge too),
# 2 smelter, 3 tanning rack, 4 cooking pot, 5 grindstone, 6 armour workbench; 7 enchanter
STATIONS = {0x88105: 1, 0xF46CE: 1, 0xA5CCE: 2, 0x7866A: 3, 0xA5CB3: 4, 0x88108: 5, 0xADB78: 6}
STATION_NAMES = {1: "Forge", 2: "Smelter", 3: "Tanning Rack", 4: "Cooking Pot", 5: "Grindstone", 6: "Workbench",
                 7: "Arcane Enchanter"}
ACT_PILLAR, ACT_LEVER, ACT_PUZZLE_LEVER, ACT_GATE, ACT_BRIDGE, ACT_KEYHOLE, ACT_CLAWBASE, ACT_WEB = 1, 2, 3, 4, 5, 6, 7, 8
# Ustengrav's Whirlwind Sprint puzzle (dunUstenPuzTrigScript, dunUstenPuzGateScript): a dragon stone
# opens its linked gate while you stand on it and lets it shut 1.5 s after you step off; past the
# gates, dunUstengravQST's stage 50 trigger opens all of them for good (its fragment 17)
ACT_STONE, ACT_OPENER = 9, 10
PILLAR_SCRIPTS = ("defaultpuzzlepillarscript", "defaultpuzzlepillarnofurn", "dunskuldafnpuzzlepillar01script",
                  "dunskuldafnpuzzlepillartwostage")
PUZZLE_LEVER_SCRIPTS = ("defaultpillarpuzzlelever", "defaultpillarpuzzlelevernofurn", "dunskuldafnpuzzlecontrolscript")
USTENGRAV_QUEST, USTENGRAV_OPEN_STAGE = 0x0002125B, 50
# A Hall of Stories door (Bleak Falls Barrow's, the Golden Claw's): three rings turned to the
# claw's three signs - the engine's pillars, their "lever" the keyhole (ACT_KEYHOLE: it wants
# the claw); the door's base goes with them when it opens. The rings carry this for a controller.
CLAW_CONTROLLER = 0xC1A30000
# Traps (engine traps.c): triggers, a hub passing a trigger on, and what they spring
(TRAP_PLATE, TRAP_WIRE, TRAP_BOX, TRAP_LINKER, TRAP_BEAR, TRAP_BLADE, TRAP_MACE, TRAP_RAM, TRAP_WALL, TRAP_SPEAR,
 TRAP_ROCKS, TRAP_LAMP, TRAP_OIL, TRAP_DARTS, TRAP_FLAME, TRAP_BONES) = range(1, 17)
# script (lower case) -> (kind, model motion)
TRAP_SCRIPTS = {"pressureplate": (TRAP_PLATE, "plate"), "tripwire": (TRAP_WIRE, "still"),
                "trapbear": (TRAP_BEAR, "jaws"), "bladetrap": (TRAP_BLADE, "pendulum"),
                "macetrap": (TRAP_MACE, "pendulum"), "batteringtrap": (TRAP_RAM, "pendulum"),
                "trapswingingwall": (TRAP_WALL, "wall"), "speartrap": (TRAP_SPEAR, "spear"),
                "physicstrap": (TRAP_ROCKS, "fall"), "trapfallingoillampcollayer": (TRAP_LAMP, "fall"),
                "trapoilpool": (TRAP_OIL, "still"), "darttrap": (TRAP_DARTS, "still"),
                "trapflamethrower": (TRAP_FLAME, "still"), "soundtrap": (TRAP_BONES, "still")}
TRAP_EDITOR_IDS = {"trapwall": (TRAP_WALL, "wall"), "trapspear": (TRAP_SPEAR, "spear"),
                   "trapdartport": (TRAP_DARTS, "still"), "trapmace": (TRAP_MACE, "pendulum"),
                   "trapskullram": (TRAP_RAM, "pendulum")}
LEVER_SCRIPTS = ("traplever", "norpullbar01script", "norlever01script", "defaultlever")
GATE_SCRIPT_WORDS = ("portcullis", "gatescript", "opengate", "barreddoor")  # (scripts of gates levers open)
CONTAINER_AIM_M, CONTAINER_AIM_MIN_M, CONTAINER_AIM_MAX_M = 0.55, 0.18, 1.1  # (metres)
OWNED, LOCK_LEVELS = 1, (0, 1, 25, 50, 75, 100, 255)
NO_MODEL = 0xFFFF  # (a loose item with no model of its own: a plant harvested where it stands, baked with the rest)
HARVEST = 2  # a loose thing's flag: taking it harvests its container (a coin purse's gold)
SKDR_VERSION = 2
DOOR_MID_M, DOOR_AIM_M, DOOR_AIM_MAX_M = 1.1, 0.9, 3.0  # (a door without a model: its middle, reach)
UNITS_PER_METER = 70.0
TILE_M = 8.0  # (16 till rooms came: a fort hall's 125k triangles in one tile, drawn whenever a corner was in view)
MAX_REFS = 4000
MAX_DEPTH = 3
FOG_FAR_CAP_M = 90.0
# XCLL inherit flags: these fields come from the lighting template
INHERIT_AMBIENT, INHERIT_DIRECTIONAL, INHERIT_FOG_COLOR, INHERIT_FOG_NEAR, INHERIT_FOG_FAR = 1, 2, 4, 8, 16
INHERIT_DIR_ROTATION = 32


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def to_engine(px, py, pz):
    return np.array([px, pz, -py], np.float32) / UNITS_PER_METER


def _room_of(rooms, at):
    """The smallest room bound holding the point (an index), or -1: in none (drawn always)."""
    best, best_v = -1, None
    for i, b in enumerate(rooms):
        if b[0] <= at[0] <= b[3] and b[1] <= at[1] <= b[4] and b[2] <= at[2] <= b[5]:
            v = (b[3] - b[0]) * (b[4] - b[1]) * (b[5] - b[2])
            if best_v is None or v < best_v:
                best, best_v = i, v
    return best


# Doors whose lock a quest takes off and which stay unlocked here from the first: High Hrothgar's
# front doors (Skyrim's MQ105 unlocks them as the Greybeards summon you - Fragment_9's Lock(false);
# never locked, as the user asked - they'd told you "This lock requires a key" with no key in Skyrim)
NEVER_LOCKED = ("HighHrothgarExteriorDoor1", "HighHrothgarExteriorDoor2")
# Doors a quest's script keeps shut with no lock of their own (Esbern's, in the Ratway Warrens: MQ202
# blocks it till he's told INFO 0001C0EB): barred - locked with no key - till the quest opens it
BARRED = ("MQ202EsbernsDoorREF",)

class Lighting:
    """An interior's light: ambient (by facing), a directional light, point lights."""

    def __init__(self, xcll, template, lights):
        def field(flag, offset, size):
            src = template if template and xcll is not None and _u32(xcll, 88) & flag else xcll
            src = src if src is not None else template
            return src[offset:offset + size] if src is not None else None

        rgb = lambda b: np.array(tuple(b[:3]), np.float32) / 255.0 if b else np.zeros(3, np.float32)
        self.ambient = rgb(field(INHERIT_AMBIENT, 0, 4))
        self.directional = rgb(field(INHERIT_DIRECTIONAL, 4, 4))
        rot = field(INHERIT_DIR_ROTATION, 20, 8)
        rx, rz = struct.unpack("<ii", rot) if rot else (0, 0)
        a, b = np.radians(rx), np.radians(rz)
        # Skyrim direction (rotations about X then Z of -Z) -> engine axes
        d = np.array([np.sin(b) * np.cos(a), np.cos(b) * np.cos(a), -np.sin(a)])
        self.dir = -np.array([d[0], d[2], -d[1]], np.float32)  # toward the light
        amb6 = None
        for src in (xcll, template):
            if src is not None and len(src) >= 64:
                c = np.array([tuple(src[40 + i * 4:43 + i * 4]) for i in range(6)], np.float32) / 255.0
                if c.max() > 0:
                    amb6 = c
                    break
        self.amb6 = amb6
        near_col = rgb(field(INHERIT_FOG_COLOR, 8, 4))
        self.fog = near_col
        fn = field(INHERIT_FOG_NEAR, 12, 4)
        ff = field(INHERIT_FOG_FAR, 16, 4)
        self.fog_near = struct.unpack("<f", fn)[0] / UNITS_PER_METER if fn else 10.0
        self.fog_far = struct.unpack("<f", ff)[0] / UNITS_PER_METER if ff else 60.0
        self.fog_far = min(max(self.fog_far, self.fog_near + 5.0), FOG_FAR_CAP_M)
        self.lights = lights  # [(pos (3,), radius m, rgb (3,))]

    def __call__(self, normals, colors, positions):
        n = normals.astype(np.float32)
        if self.amb6 is not None:
            s = np.stack([n[:, 0], -n[:, 2], n[:, 1]], 1)  # Skyrim axes: X+ X- Y+ Y- Z+ Z-
            w = s * s
            light = (w[:, 0:1] * np.where(s[:, 0:1] > 0, self.amb6[0], self.amb6[1]) +
                     w[:, 1:2] * np.where(s[:, 1:2] > 0, self.amb6[2], self.amb6[3]) +
                     w[:, 2:3] * np.where(s[:, 2:3] > 0, self.amb6[4], self.amb6[5]))
            light = np.maximum(light, self.ambient * 0.5)
        else:
            light = np.tile(self.ambient, (len(n), 1))
        light = light + self.directional * 0.5 * np.maximum(n @ self.dir, 0)[:, None]
        p = positions.astype(np.float32)
        for lp, radius, color in self.lights:
            to = lp - p
            d = np.sqrt((to * to).sum(1))
            near = d < radius
            if not near.any():
                continue
            dn = np.maximum(d[near], 1e-3)
            ndl = np.maximum((n[near] * to[near]).sum(1) / dn, 0) * 0.8 + 0.2  # wrapped: soft
            atten = (1.0 - dn / radius) ** 2
            light[near] += color * (atten * ndl)[:, None]
        tint = colors[:, :3].astype(np.float32) / 255.0
        v = np.clip(light * tint * 0.5, 0, 1)
        return (v * 255 + 0.5).astype(np.uint8)


class Exporter:
    def __init__(self, load_order, cells, models, strings, entry_for_ref):
        """entry_for_ref: extract_objects.entry_for_ref (converts the models)."""
        self.lo, self.cells, self.models, self.strings = load_order, cells, models, strings
        self.entry_for_ref = entry_for_ref
        self._door_bounds = {}  # door model path -> (min, max) or None
        self._markers = {}      # furniture model path -> its markers
        self.inner = load_order.interior_cells()
        self.ref_cell = {}
        for cg, c in self.inner.items():
            for r in c["refs"]:
                self.ref_cell[load_order.global_id(r.plugin, r.form_id)] = cg
        self.outdoor = {}
        for key, c in cells.items():
            for r in c["refs"]:
                self.outdoor[load_order.global_id(r.plugin, r.form_id)] = (key, r)
        self.stats = Counter()
        self._pending = []  # exported, waiting for the bake
        self.items = None   # items.Database: containers and loose items (None: neither)
        self.cities = {}    # worlds.export's: {prefix: (cells, city cell keys)}, in worlds.txt order

    def _name(self, cg):
        rec = self.inner[cg]["cell"]
        full = rec.sub("FULL") if rec is not None else None
        text = self.strings.get(rec.plugin, _u32(full), "strings") if full and len(full) == 4 else ""
        return text or (rec.editor_id() if rec is not None else "Inside")

    def _location_name(self, rec, xlcn, up=0):
        """A location's name (LCTN FULL), or its parent's (PNAM: a hold's) if it has none; None."""
        loc = self.lo.record(self.lo.global_id(rec.plugin, _u32(xlcn)), types=("LCTN",)) if xlcn else None
        for _ in range(4):
            if loc is None:
                return None
            full = loc.sub("FULL")
            name = self.strings.get(loc.plugin, _u32(full), "strings") if full and len(full) == 4 else ""
            if name and up <= 0:
                return name
            up -= 1
            pnam = loc.sub("PNAM")
            loc = self.lo.record(self.lo.global_id(loc.plugin, _u32(pnam)), types=("LCTN",)) if pnam else None
        return None

    def _outdoor_name(self, key, inside=None):
        """Where a door out leads, as Skyrim names it: the outside cell's own name if it has one (FULL),
        else the world's - "Skyrim" (Tamriel's FULL). (We named it after the place's location - "Bleak
        Falls Barrow", "Helgen" - the user, Nov 8: Skyrim's exits into its open land say "Skyrim")"""
        cell = self.cells.get(key, {}).get("cell")
        full = cell.sub("FULL") if cell is not None else None
        name = self.strings.get(cell.plugin, _u32(full), "strings") if full and len(full) == 4 else ""
        return name or self._world_name()

    def _world_name(self):
        if not hasattr(self, "_world_full"):
            wrld = self.lo.world_record("Tamriel")
            full = wrld.sub("FULL") if wrld is not None else None
            plugin = getattr(wrld, "plugin", None) or "Skyrim.esm"
            self._world_full = (self.strings.get(plugin, _u32(full), "strings") or "") if full and len(full) == 4 else ""
        return self._world_full or "Skyrim"

    def _city_refs(self):
        """{ref gid: (city number, prefix)} of the cities' references."""
        if not hasattr(self, "_cref"):
            self._cref = {}
            for n, (prefix, (cells, city)) in enumerate(self.cities.items()):
                for key in city:
                    for r in cells[key]["refs"]:
                        self._cref[self.lo.global_id(r.plugin, r.form_id)] = (n, prefix)
        return self._cref

    def _door_target(self, ref):
        """(destination ref gid, Record) of a teleport door, or None."""
        xtel = ref.sub("XTEL")
        if not xtel or len(xtel) < 28:
            return None
        return self.lo.global_id(ref.plugin, _u32(xtel)), xtel

    def select(self, bounds):
        """Interiors reachable from the region's doors, nearest first (breadth first); then
        those of the cities' doors (after: the region's keep their numbers)."""
        min_x, min_y, max_x, max_y = bounds
        order, depth = [], {}
        seeds = [[r for (x, y), c in self.cells.items() if min_x <= x <= max_x and min_y <= y <= max_y
                  for r in c["refs"]]]
        for cells, city in self.cities.values():
            seeds.append([r for key in sorted(city) for r in cells[key]["refs"]])
        for refs in seeds:
            queue = deque()
            for r in refs:
                t = self._door_target(r)
                cg = self.ref_cell.get(t[0]) if t else None
                if cg is not None and cg not in depth:
                    depth[cg] = 1
                    queue.append(cg)
            self._walk(queue, depth, order)
        return order

    def _walk(self, queue, depth, order):
        while queue:
            cg = queue.popleft()
            if len(self.inner[cg]["refs"]) > MAX_REFS:
                self.stats["too large (skipped)"] += 1
                continue
            order.append(cg)
            if depth[cg] >= MAX_DEPTH:
                continue
            for r in self.inner[cg]["refs"]:
                t = self._door_target(r)
                nxt = self.ref_cell.get(t[0]) if t else None
                if nxt is not None and nxt not in depth:
                    depth[nxt] = depth[cg] + 1
                    queue.append(nxt)

    def _door(self, ref, index):
        """Packed door record, or None if it leads nowhere converted."""
        t = self._door_target(ref)
        if not t:
            return None
        dest, xtel = t
        px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", xtel, 4)
        if dest in self.outdoor:
            target, label = -1, self._outdoor_name(self.outdoor[dest][0], self.ref_cell.get(self.lo.global_id(ref.plugin, ref.form_id)))
        elif dest in self._city_refs():
            n, prefix = self._city_refs()[dest]
            target, label = -2 - n, dict((p, t) for _w, p, t in CITIES + OWN_LAND).get(prefix, prefix)
        elif self.ref_cell.get(dest) in index:
            target = index[self.ref_cell[dest]]
            label = self._name(self.ref_cell[dest])
        else:
            return None
        data = ref.sub("DATA")
        pos = to_engine(*struct.unpack_from("<3f", data))
        label_b = label.encode("cp1252", "replace")[:255]
        centre, radius = self._door_aim(ref, pos)
        name = ref.sub("NAME")
        base = self.lo.record(self.lo.global_id(ref.plugin, _u32(name)), types=("DOOR",)) if name else None
        if base is not None and (base.editor_id() or "").lower().startswith("autoloaddoor"):
            radius = -abs(radius)  # (Skyrim's walk-through door - a cave's mouth: no model, used by walking into it)
        out = struct.pack("<3fi3ff3ffB", *pos, target, *to_engine(px, py, pz), rz, *centre, radius,
                          len(label_b)) + label_b
        return out + b"\0" * (-len(out) % 4)

    def _model_aim(self, path, m, t):
        """(centre, radius) of a container's model as placed (engine space): where the
        crosshair has to pass to open it (a strongbox is a hand's breadth, a wardrobe tall)."""
        if path not in self._door_bounds:
            try:
                shapes = NIF(find(self.models.mesh_archives, "meshes\\" + path)).shapes()
                pts = np.concatenate([sh.positions for sh in shapes]) if shapes else None
                self._door_bounds[path] = (pts.min(0), pts.max(0)) if pts is not None and len(pts) else None
            except (KeyError, ValueError, IndexError, struct.error):
                self._door_bounds[path] = None
        bounds = self._door_bounds[path]
        if bounds is None:
            return t + np.array([0.0, 0.45, 0.0]), CONTAINER_AIM_M
        lo, hi = bounds
        corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
        world = np.array([m @ to_engine(*c) + t for c in corners])
        extent = world.max(0) - world.min(0)
        radius = float(np.clip(0.5 * extent.max() * 0.85, CONTAINER_AIM_MIN_M, CONTAINER_AIM_MAX_M))
        return (world.min(0) + world.max(0)) * 0.5, radius

    def _door_aim(self, ref, pos):
        """(centre, radius) of a load door's model as placed: where the crosshair has to
        pass to use it (a city's great doors are far bigger than a house's)."""
        from extract_objects import ref_transform
        default = (pos + np.array([0.0, DOOR_MID_M, 0.0], np.float32), DOOR_AIM_M)
        name = ref.sub("NAME")
        base = self.lo.record(self.lo.global_id(ref.plugin, _u32(name)), types=("DOOR",)) if name else None
        modl = base.sub("MODL") if base is not None else None
        if not modl or len(modl) < 5:
            return default
        path = modl.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")
        if path not in self._door_bounds:
            try:
                shapes = NIF(find(self.models.mesh_archives, "meshes\\" + path)).shapes()
                pts = np.concatenate([sh.positions for sh in shapes]) if shapes else None
                self._door_bounds[path] = (pts.min(0), pts.max(0)) if pts is not None and len(pts) else None
            except (KeyError, ValueError, IndexError, struct.error):
                self._door_bounds[path] = None
        bounds = self._door_bounds[path]
        if bounds is None:
            return default
        xscl = ref.sub("XSCL")
        m, t = ref_transform(ref.sub("DATA"), struct.unpack("<f", xscl)[0] if xscl else 1.0)
        lo, hi = bounds
        corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
        world = np.array([m @ to_engine(*c) + t for c in corners])
        extent = np.sort(world.max(0) - world.min(0))
        radius = float(np.clip(0.45 * extent[2], DOOR_AIM_M * 0.8, DOOR_AIM_MAX_M))
        return (world.min(0) + world.max(0)) * 0.5, radius

    def _scripts(self, ref, base):
        """{script name (lower case): {property: value}} of a reference and its base (Papyrus,
        VMAD): numbers as numbers, objects as global form ids."""
        out = {}
        for rec in (base, ref):
            d = rec.sub("VMAD") if rec is not None else None
            if not d or len(d) < 6:
                continue
            try:
                _ver, fmt, count = struct.unpack_from("<hhH", d, 0)
                o = 6
                for _ in range(count):
                    n = struct.unpack_from("<H", d, o)[0]
                    name = d[o + 2:o + 2 + n].decode("cp1252", "replace").lower()
                    o += 2 + n + 1
                    props = {}
                    pc = struct.unpack_from("<H", d, o)[0]
                    o += 2
                    for _ in range(pc):
                        n = struct.unpack_from("<H", d, o)[0]
                        pname = d[o + 2:o + 2 + n].decode("cp1252", "replace")
                        o += 2 + n
                        ptype = d[o]
                        o += 2
                        if ptype == 1:
                            form = _u32(d, o + 4) if fmt >= 2 else _u32(d, o)
                            props[pname] = self.lo.global_id(rec.plugin, form) if form else 0
                            o += 8
                        elif ptype == 2:
                            n = struct.unpack_from("<H", d, o)[0]
                            props[pname] = d[o + 2:o + 2 + n].decode("cp1252", "replace")
                            o += 2 + n
                        elif ptype in (3, 4):
                            props[pname] = struct.unpack_from("<i" if ptype == 3 else "<f", d, o)[0]
                            o += 4
                        elif ptype == 5:
                            props[pname] = d[o]
                            o += 1
                        elif ptype == 11:
                            alen = struct.unpack_from("<I", d, o)[0]
                            o += 4
                            arr = []
                            for _ in range(alen):
                                form = _u32(d, o + 4) if fmt >= 2 else _u32(d, o)
                                arr.append(self.lo.global_id(rec.plugin, form) if form else 0)
                                o += 8
                            props[pname] = arr
                        elif ptype == 12:
                            alen = struct.unpack_from("<I", d, o)[0]
                            o += 4
                            arr = []
                            for _ in range(alen):
                                slen = struct.unpack_from("<H", d, o)[0]
                                o += 2
                                arr.append(d[o:o + slen].decode("cp1252", "replace"))
                                o += slen
                            props[pname] = arr
                        elif ptype in (13, 14):
                            alen = struct.unpack_from("<I", d, o)[0]
                            o += 4
                            fmt_elem = "<i" if ptype == 13 else "<f"
                            arr = [struct.unpack_from(fmt_elem, d, o + i * 4)[0] for i in range(alen)]
                            o += alen * 4
                            props[pname] = arr
                        elif ptype == 15:
                            alen = struct.unpack_from("<I", d, o)[0]
                            o += 4
                            props[pname] = [bool(d[o + i]) for i in range(alen)]
                            o += alen
                        else:
                            break
                    out[name] = props
            except (struct.error, ValueError, IndexError):
                continue
        return out

    def _activator(self, ref, children, light):
        """(placement entry of its still part or None, packed activator) for a scripted
        activator the engine runs (Skyrim's scripts, by name): a puzzle pillar, a pillar
        puzzle's lever, a lever or pull chain, a gate they open; None otherwise."""
        if not ref.sub("NAME") or not ref.sub("DATA"):
            return None
        base = self.lo.record(self.lo.global_id(ref.plugin, _u32(ref.sub("NAME"))), types=("ACTI", "FURN", "DOOR"))
        if base is not None and not base.sub("MODL"):
            trig = self._sprint_trigger(ref, base)
            if trig is not None:
                return None, trig
        if base is None or not base.sub("MODL"):
            return None
        if base.type == "DOOR" and (ref.sub("XTEL") or not ref.sub("XAPR") or "secret" not in (base.editor_id() or "").lower()):
            return None  # (a load door, or one opened by hand - a jail cell's a lever opens too: the doors' own)
        scripts = self._scripts(ref, base)
        gid = self.lo.global_id(ref.plugin, ref.form_id)
        state = solve = 0
        controller, kids, failures = 0, list(children.get(gid, [])), []
        beid = (base.editor_id() or "").lower()
        if "norsarcophagustopanim01script" in scripts:
            # (a sarcophagus's lid: pushed off as the draugr in it rises - Skyrim's lid script; baked
            # still, the restless dead came up through it)
            kind, model_kind = ACT_GATE, "lid"
            state = 1
        elif base.type == "DOOR":
            # (a door a lever or pull bar opens - its activate parent: Bleak Falls Sanctum's rock wall
            # by the word wall, CaveGSecretDoor01, lowered by the pedestal's bar. Not one of the gates'
            # scripts, so it was left a door no hand could open and nothing pulled did - the way out shut)
            kind, model_kind = ACT_GATE, "door"
            state = 1
        elif base.type == "ACTI" and ("spiderwebkit" in beid or "spiderwebhitstrip" in beid) and base.sub("DEST") and \
                "actor" not in beid:  # (and the strips across a way - Skuldafn Temple's behind its secret sarcophagus)
            # (Skyrim's destructible spider webs - Arvel's across Bleak Falls' passage: cut down or
            # burnt away; baked with the room, nothing could take them down and the way on was shut)
            kind, model_kind = ACT_WEB, "web"
        elif "hallofstoriesdiskscript" in scripts:
            kind, model_kind = ACT_PILLAR, "clawring2"
            props = scripts["hallofstoriesdiskscript"]
            state, solve = int(props.get("initialState", 1)), int(props.get("solveState", 1))
            controller = CLAW_CONTROLLER
        elif "hallofstorieskeyholescript" in scripts or "dunfolgunthurdragonkeyholeactivator" in scripts or "keyhole" in beid:
            kind, model_kind = ACT_KEYHOLE, "clawdoor"
            # Map keyhole to its required dragon claw FormID
            props = scripts.get("dunfolgunthurdragonkeyholeactivator") or scripts.get("hallofstorieskeyholescript") or {}
            claw_obj = props.get("myClawObject") or props.get("myMiscObject")
            if claw_obj:
                controller = self.lo.global_id(ref.plugin, int(claw_obj))
            else:
                CLAW_NAME_MAP = {
                    "coral": 0x000B634C,
                    "emerald": 0x000ED417,
                    "sapphire": 0x000663D7,
                    "diamond": 0x000AB375,
                    "ivory": 0x000AB7BB,
                    "folgunthur": 0x000AB7BB,
                    "ruby": 0x0004B56C,
                    "glass": 0x0007C260,
                    "iron": 0x0008CDFA,
                    "ebony": 0x0005AF48,
                    "golden": 0x00039647,
                    "puzzledoorkeyhole01": 0x00039647,
                    "amethystr": 0x0201CAC1,
                    "amethystl": 0x0201CAC0,
                    "amethyst": 0x0201CAC0,
                }
                for k, v in CLAW_NAME_MAP.items():
                    if k in beid:
                        controller = v
                        break
                if not controller:
                    controller = 0x00039647  # default: Golden Claw
            for k, v in props.items():
                if k.lower() in ("mainactivationlink", "initialactivationlink") and v:
                    link_gid = self.lo.global_id(ref.plugin, int(v))
                    if link_gid and link_gid not in kids:
                        kids.append(link_gid)
        elif beid.startswith("puzzledoorbase"):
            kind, model_kind = ACT_CLAWBASE, "clawdoor"
        elif any(k in scripts for k in PILLAR_SCRIPTS):
            # (a puzzle pillar: Skyrim's default, its "NoFurn" kind, Skuldafn Temple's own - and its two-stage
            # one, whose second solved face is the puzzle's other answer: in the high four bits of solve)
            kind, model_kind = ACT_PILLAR, "pillar"
            props = scripts[next(k for k in PILLAR_SCRIPTS if k in scripts)]
            state = int(props.get("initialState", 1))
            if "solveState1" in props:
                solve = int(props.get("solveState1", 1)) | int(props.get("solveState2", 0)) << 4
            else:
                solve = int(props.get("solveState", 1))
            for t, v in ref.subrecords():
                if t == "XLKR" and len(v) >= 8:
                    controller = self.lo.global_id(ref.plugin, _u32(v, 4))
        elif any(k in scripts for k in PUZZLE_LEVER_SCRIPTS):
            # (a pillar puzzle's lever: what its success opens; Skuldafn Temple's has a second success - the
            # two-stage pillar's other face - after a 0 in the list: dunSkuldafnPuzzleControlScript)
            kind, model_kind = ACT_PUZZLE_LEVER, "lever"
            props = scripts[next(k for k in PUZZLE_LEVER_SCRIPTS if k in scripts)]
            kids = [v for k, v in sorted(props.items()) if k.lower().startswith("refactonsuccess01") and v]
            alt = [v for k, v in sorted(props.items()) if k.lower().startswith("refactonsuccess") and
                   not k.lower().startswith("refactonsuccess01") and v]
            if "dunskuldafnpuzzlecontrolscript" in scripts and alt:
                kids = kids + [0] + alt
            else:
                kids = kids + alt
            failures = [v for k, v in sorted(props.items()) if k.lower().startswith("refactonfailure") and v]
        elif any(k in scripts for k in LEVER_SCRIPTS):
            kind, model_kind = ACT_LEVER, "lever"
        elif "default2stateactivator" in scripts or any(any(w in k for w in GATE_SCRIPT_WORDS) for k in scripts):
            # (a portcullis, a secret wall a pull chain lowers - Dustman's Cairn's - a barred gate;
            # state 2: Skyrim starts it open, isOpen)
            kind, model_kind = ACT_GATE, "gate"
            props = next((v for k, v in scripts.items() if "isOpen" in v), {})
            state = 2 if props.get("isOpen") else 1
            if b"retractablebridge" in (base.sub("MODL") or b"").lower():
                # (a Nordic retractable bridge: its deck swings up on its axle. Skyrim's "open" is the
                # way over open - the deck up: Helgen Keep's starts so, its lever lets it down - and
                # that's the engine's open too, the deck raised)
                kind, model_kind = ACT_BRIDGE, "bridge"
                state = 2 if props.get("isOpen") else 1
        else:
            return None
        model_path = base.sub("MODL").rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")
        split = self.models.door_models(model_path) if model_kind == "door" else \
            self.models.door_models(model_path, "openEnd") if model_kind == "lid" else None
        if split is None and model_kind == "lid":
            return None
        if split is None:
            split = self.models.activator_models(model_path, "sink" if model_kind == "door" else model_kind)
        if split is None:
            return None
        from extract_objects import ref_transform
        still, parts = split
        xscl = ref.sub("XSCL")
        m, t = ref_transform(ref.sub("DATA"), struct.unpack("<f", xscl)[0] if xscl else 1.0)
        entry = struct.pack("<HH9f3f", still, 0, *m.reshape(-1), *t) if still is not None else None
        centre, radius = self._model_aim(model_path, m, t)
        if kind == ACT_WEB:  # (struck anywhere across it: a passage-wide web, not a box's reach)
            radius = max(radius, 1.6)
        full = base.sub("FULL")
        name = self.strings.get(base.plugin, _u32(full), "strings") if full and len(full) == 4 else ""
        label = {ACT_PILLAR: f"Rotate {name or 'Pillar'}", ACT_GATE: "Lid" if model_kind == "lid" else name or "Gate", ACT_BRIDGE: name or "Bridge",
                 ACT_WEB: "Spider Web"}.get(kind) or \
            (name if name.lower().startswith("pull") else f"Pull {name}" if name else "Pull Lever")
        if model_kind in ("clawring2", "clawdoor"):  # (the door's parts: aimed at by how far from its middle - their true sizes)
            bounds = self._door_bounds.get(model_path)
            if bounds is not None:
                ext = bounds[1] - bounds[0]
                radius = float(0.5 * max(ext[0], ext[2]) / 70.0)
            label = "Turn the inner ring" if "small" in beid else "Turn the middle ring" if "medium" in beid \
                else "Turn the outer ring" if "large" in beid else "Use the claw" if kind == ACT_KEYHOLE else "Door"
        label_b = label.encode("cp1252", "replace")[:255]
        kids, failures = kids[:255], failures[:255]
        out = struct.pack("<IBBBBfIBBH", gid, kind, state, solve, len(label_b), radius, controller, len(kids),
                          len(failures), 0)
        out += b"".join(struct.pack("<I", k) for k in kids + failures)
        out += label_b + b"\0" * (-len(label_b) % 4)
        out += struct.pack("<9f3f3f3BB", *m.reshape(-1), *t, *centre, *self._light_at(light, centre), len(parts))
        for idx, pivot, axis, angle, slide in parts:
            out += struct.pack("<HH3f3ff3f", idx, 0, *pivot, *axis, angle, *slide)
        self.stats["activators"] += 1
        return entry, out

    def _sprint_trigger(self, ref, base):
        """A packed activator for Ustengrav's sprint puzzle's trigger boxes (no model): a dragon stone
        (ACT_STONE, linked to its gate) or the stage trigger past the gates (ACT_OPENER, all of them);
        None for any other trigger."""
        scripts = self._scripts(ref, base)
        xprm = ref.sub("XPRM")
        if not xprm or len(xprm) < 12:
            return None
        if "dunustenpuztrigscript" in scripts:
            kind = ACT_STONE
            kids = [self.lo.global_id(ref.plugin, _u32(v, 4)) for t, v in ref.subrecords()
                    if t == "XLKR" and len(v) >= 8 and _u32(v) == 0][:1]
        elif "defaultsetstagetrigscript" in scripts:
            props = scripts["defaultsetstagetrigscript"]
            if props.get("myQuest") != USTENGRAV_QUEST or int(props.get("stage", 0)) != USTENGRAV_OPEN_STAGE:
                return None
            kind, kids = ACT_OPENER, list(getattr(self, "_sprint_gates", []))
        else:
            return None
        if not kids:
            return None
        from extract_objects import ref_transform
        hx, hy, _hz = struct.unpack_from("<3f", xprm)
        m, t = ref_transform(ref.sub("DATA"), 1.0)
        radius = float(max(hx, hy) / 70.0)  # (the box's half size, metres: stood within it)
        gid = self.lo.global_id(ref.plugin, ref.form_id)
        out = struct.pack("<IBBBBfIBBH", gid, kind, 0, 0, 0, radius, 0, len(kids), 0, 0)
        out += b"".join(struct.pack("<I", k) for k in kids)
        out += struct.pack("<9f3f3f3BB", *m.reshape(-1), *t, *t, 128, 128, 128, 0)  # (no parts: nothing drawn)
        self.stats["sprint triggers"] += 1
        return out

    def _trap(self, ref, light):
        """(placement entry of its still part or None, packed trap) for one of Skyrim's traps or
        their triggers (by script, as activators), or None. Traps listen to their activate
        parents (XAPR: a pressure plate, a tripwire, a trigger box, a hub, a lever) and pass
        it on to their linked refs (XLKR: a dart port's other ports)."""
        if not ref.sub("NAME") or not ref.sub("DATA"):
            return None
        base = self.lo.record(self.lo.global_id(ref.plugin, _u32(ref.sub("NAME"))), types=("ACTI",))
        if base is None:
            return None
        scripts = self._scripts(ref, base)
        eid = base.editor_id().lower()
        kind = next((v for k, v in TRAP_SCRIPTS.items() if k in scripts), None)
        if kind is None:
            kind = next((v for k, v in TRAP_EDITOR_IDS.items() if eid.startswith(k)), None)
        if kind is None and "traptriggerbase" in scripts:
            kind = (TRAP_BOX if ref.sub("XPRM") else TRAP_LINKER, None)
        if kind is None:
            return None
        kind, motion = kind
        from extract_objects import ref_transform
        xscl = ref.sub("XSCL")
        m, t = ref_transform(ref.sub("DATA"), struct.unpack("<f", xscl)[0] if xscl else 1.0)
        model = base.sub("MODL")
        model_path = model.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\") if model else None
        split = self.models.trap_models(model_path, motion) if model_path and motion else None
        still, parts, (lo, hi), muzzle = split if split else (None, [], (np.zeros(3), np.zeros(3)), None)
        if kind == TRAP_BOX:  # (its primitive: a box this big)
            b = np.array(struct.unpack_from("<3f", ref.sub("XPRM"))) / UNITS_PER_METER * 0.5
            half = np.array([b[0], b[2], b[1]])
            lo, hi = -half, half
        entry = struct.pack("<HH9f3f", still, 0, *m.reshape(-1), *t) if still is not None else None
        # Its damage by the player's level (Skyrim's LvlDamage / LvlThreshold properties)
        props = next((p for p in scripts.values() if "LvlDamage1" in p), {})
        damage = [float(props.get(f"LvlDamage{i}", 0.0)) for i in range(1, 7)]
        thresholds = [float(props.get(f"LvlThreshold{i}", 1000.0)) for i in range(1, 6)] + [1000.0]
        if not any(damage):
            damage = [{TRAP_DARTS: 12.0, TRAP_FLAME: 15.0, TRAP_OIL: 20.0, TRAP_LAMP: 0.0}.get(kind, 20.0)] * 6
        parents = [self.lo.global_id(ref.plugin, _u32(v)) for k, v in ref.subrecords() if k == "XAPR" and len(v) >= 4]
        children = [self.lo.global_id(ref.plugin, _u32(v, 4)) for k, v in ref.subrecords() if k == "XLKR" and len(v) >= 8]
        parents, children = parents[:16], children[:16]
        centre = m @ ((lo + hi) * 0.5) + t
        mz = muzzle if muzzle is not None else np.zeros(3)
        out = struct.pack("<IBBBB6f6f3f3f3f", self.lo.global_id(ref.plugin, ref.form_id), kind, len(parents),
                          len(children), 0, *damage, *thresholds, *lo, *hi, *mz)
        out += b"".join(struct.pack("<I", g) for g in parents + children)
        out += struct.pack("<9f3f3f3BB", *m.reshape(-1), *t, *centre, *self._light_at(light, centre), len(parts))
        for idx, pivot, axis, angle, slide in parts:
            out += struct.pack("<HH3f3ff3f", idx, 0, *pivot, *axis, angle, *slide)
        self.stats["traps"] += 1
        return entry, out

    def _resource(self, ref, base):
        """An ore vein or chopping block: registers what it gives (items.Database.resources) and
        returns its verb ("Mine", "Chop"), or None."""
        scripts = self._scripts(ref, base)
        mine, wood = scripts.get("mineorescript"), scripts.get("resourcefurniturescript")
        props = mine or wood
        if not props:
            return None
        res = props.get("Ore" if mine else "Resource")
        tools = props.get("mineOreToolsList" if mine else "requiredItemList")
        made = self.items.entry(res) if res else None
        if not made or made[0] != "I":
            return None
        flst = self.lo.record(tools, types=("FLST",)) if tools else None
        tool_items = []
        for t, v in (flst.subrecords() if flst is not None else []):
            if t == "LNAM":
                e = self.items.entry(self.lo.global_id(flst.plugin, _u32(v)))
                if e and e[0] == "I":
                    tool_items.append(e[1])
        per_use = 1 if mine else int(props.get("ResourceCount", 2) or 2)
        total = ORE_PER_VEIN if mine else 0  # (0: never runs out)
        self.items.resources[self.lo.global_id(ref.plugin, ref.form_id)] = (made[1], per_use, total, tool_items)
        return "Mine" if mine else "Chop"

    def _furniture(self, ref):
        """Packed furniture the player can use (a chair, bench or stool to sit on, a bed or
        bedroll to sleep in) from its model's markers, or None. (Its model stays baked with
        the rest; this is where to sit / lie and which ways in.)"""
        if not ref.sub("NAME") or not ref.sub("DATA"):
            return None
        base = self.lo.record(self.lo.global_id(ref.plugin, _u32(ref.sub("NAME"))), types=("FURN", "ACTI"))
        modl = base.sub("MODL") if base is not None else None
        resource = self._resource(ref, base) if base is not None and self.items is not None else None
        if base is not None and base.type == "ACTI" and resource is None:
            return None
        if not modl or len(modl) < 5:
            return None
        path = modl.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")
        if path not in self._markers:
            try:
                from npcs import furniture_markers
                self._markers[path] = furniture_markers(NIF(find(self.models.mesh_archives, "meshes\\" + path)))
            except (KeyError, ValueError, IndexError, struct.error):
                self._markers[path] = []
        marks = [mk for mk in self._markers[path] if mk[4] in (FURN_SIT, FURN_SLEEP)][:4]
        wbdt = base.sub("WBDT")
        alchemy = bool(wbdt) and wbdt[0] in WORKBENCH_ALCHEMY
        kwda = base.sub("KWDA") or b""
        station = 7 if wbdt and wbdt[0] in WORKBENCH_ENCHANTING else next(
            (STATIONS[k] for k in (self.lo.global_id(base.plugin, k) for k in struct.unpack(f"<{len(kwda) // 4}I", kwda))
             if k in STATIONS), 0) if wbdt else 0
        if resource:
            marks = []  # (used where it stands: mined, chopped)
        elif station:
            marks = []  # (used where it stands: its crafting menu)
        elif alchemy:
            marks = []  # (used where it stands: the alchemy menu)
        elif not marks or (wbdt and wbdt[0] != 0):  # (other crafting stations - a cooking pot - aren't seats)
            return None
        from extract_objects import ref_transform
        xscl = ref.sub("XSCL")
        m, t = ref_transform(ref.sub("DATA"), struct.unpack("<f", xscl)[0] if xscl else 1.0)
        rz = struct.unpack_from("<6f", ref.sub("DATA"))[5]
        sleep = any(mk[4] == FURN_SLEEP for mk in marks)
        kind = (FURN_KIND_BEDROLL if "bedroll" in path else FURN_KIND_BED) if sleep else \
            (FURN_KIND_STOOL if "stool" in path else FURN_KIND_SEAT)
        kind = FURN_KIND_ALCHEMY if alchemy else FURN_KIND_CRAFT if station else FURN_KIND_RESOURCE if resource else kind
        centre, radius = self._model_aim(path, m, t)
        full = base.sub("FULL")
        name = self.strings.get(base.plugin, _u32(full), "strings") if full and len(full) == 4 else ""
        if "marker" in name.lower():
            name = ""  # (an invisible seat - "Invisible Chair Marker", on a stump's rim, a log: plain "Sit")
        label = f"{'Sleep' if sleep else 'Sit'}" + (f" ({name})" if name else "")
        if alchemy:
            label = f"Activate {name or 'Alchemy Lab'}"
        elif station:
            label = f"Use {name or STATION_NAMES[station]}"
        elif resource:
            label = f"{resource} {name}" if name else resource
        label_b = label.encode("cp1252", "replace")[:63]
        out = struct.pack("<IBBBB3ff", self.lo.global_id(ref.plugin, ref.form_id), kind, len(marks), len(label_b), station,
                          *centre, radius)
        for ox, oy, oz, heading, anim, entry in marks:
            pos = m @ (np.array([ox, oz, -oy]) / UNITS_PER_METER) + t
            out += struct.pack("<3ffHH", *pos, -(rz + heading), anim, entry)
        out += label_b + b"\0" * (-len(label_b) % 4)
        self.stats["furniture"] += 1
        return out

    def _swing_door(self, ref, light, skipped):
        """(placement entry of its still part or None, packed swing door) for a door that
        opens in place (no teleport), or None."""
        if self._door_target(ref) or not ref.sub("NAME") or not ref.sub("DATA"):
            return None
        base = self.lo.record(self.lo.global_id(ref.plugin, _u32(ref.sub("NAME"))), types=("DOOR",))
        modl = base.sub("MODL") if base is not None else None
        if not modl:
            return None
        split = self.models.door_models(modl.rstrip(b"\0").decode("cp1252").lower().replace("/", "\\"))
        if split is None:
            return None
        from extract_objects import ref_transform
        still, parts = split
        xscl = ref.sub("XSCL")
        m, t = ref_transform(ref.sub("DATA"), struct.unpack("<f", xscl)[0] if xscl else 1.0)
        entry = struct.pack("<HH9f3f", still, 0, *m.reshape(-1), *t) if still is not None else None
        door_path = modl.rstrip(b"\0").decode("cp1252").lower().replace("/", "\\")
        try:  # (the door's middle as placed - its origin's often the hinge: a cage's lock was off to one side)
            centre, _radius = self._model_aim(door_path, m, t)
            centre = np.asarray(centre, np.float64)
        except Exception:
            centre = t + np.array([0.0, 1.2, 0.0])
        tint = np.array([200, 200, 200], np.uint8)
        if light is not None:
            normals = np.array([[1, 0, 0], [-1, 0, 0], [0, 0, 1], [0, 0, -1], [0, 1, 0]], np.float32)
            rgb = light(normals, np.full((5, 4), 255, np.uint8), np.tile(centre.astype(np.float32), (5, 1)))
            tint = rgb.mean(0).astype(np.uint8)
        out = struct.pack("<9f3f3f3BB", *m.reshape(-1), *t, *centre, *tint, len(parts))
        for idx, pivot, axis, angle, slide in parts:
            out += struct.pack("<HH3f3ff3f", idx, 0, *pivot, *axis, angle, *slide)
        self.stats["swing doors"] += 1
        return entry, out

    def _light_at(self, light, pos):
        if light is None:
            return np.array([200, 200, 200], np.uint8)
        normals = np.array([[1, 0, 0], [-1, 0, 0], [0, 0, 1], [0, 0, -1], [0, 1, 0]], np.float32)
        rgb = light(normals, np.full((5, 4), 255, np.uint8), np.tile(np.asarray(pos, np.float32), (5, 1)))
        return rgb.mean(0).astype(np.uint8)

    def _owned(self, ref, cell):
        return bool(ref.sub("XOWN") or (cell is not None and cell.sub("XOWN")))

    def _loot(self, ref, cell, light):
        """For a container or a carriable item: (placement entry to bake or None, packed
        container or None, packed loose item or None); None if it's neither."""
        if self.items is None or not ref.sub("NAME") or not ref.sub("DATA"):
            return None
        from extract_objects import ref_transform
        gid_base = self.lo.global_id(ref.plugin, _u32(ref.sub("NAME")))
        base = self.lo.record(gid_base, types=("CONT", "FLOR") + ITEM_TYPES)
        modl = base.sub("MOD2") if base is not None and base.type == "ARMO" else base.sub("MODL") if base is not None else None
        if not modl or len(modl) < 5:  # (armour's MODL fields are its body parts; its ground model is MOD2)
            return None
        model_path = modl.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")
        xscl = ref.sub("XSCL")
        m, t = ref_transform(ref.sub("DATA"), struct.unpack("<f", xscl)[0] if xscl else 1.0)
        ref_gid = self.lo.global_id(ref.plugin, ref.form_id)
        flags = OWNED if self._owned(ref, cell) else 0
        if base.type == "CONT":
            cidx = self.items.container_for(gid_base)
            if cidx is None:
                return None
            xloc = ref.sub("XLOC")
            lock = xloc[0] if xloc else 0
            split = self.models.door_models(model_path)
            if split is not None:
                still, parts = split
                entry = struct.pack("<HH9f3f", still, 0, *m.reshape(-1), *t) if still is not None else None
            else:
                idx = self.models.index_for(model_path)
                if idx is None:
                    return None
                parts, entry = [], struct.pack("<HH9f3f", idx, 0, *m.reshape(-1), *t)
            centre, radius = self._model_aim(model_path, m, t)
            out = struct.pack("<IHBBf9f3f3f3BB", ref_gid, cidx, min(lock, 255), flags, radius, *m.reshape(-1), *t, *centre,
                              *self._light_at(light, centre), len(parts))
            for idx, pivot, axis, angle, slide in parts:
                out += struct.pack("<HH3f3ff3f", idx, 0, *pivot, *axis, angle, *slide)
            self.stats["containers"] += 1
            self.stats["containers that open (animated)"] += bool(parts)
            return entry, out, None
        if base.type == "FLOR":
            # Coin purses (Skyrim's flora: taking one gives its leveled gold, and it's gone);
            # plants and mushrooms too - harvested where they stand, part of the scenery still
            # (baked, a loose item of no model of its own: thousands outdoors, drawn as one)
            cidx = self.items.flora_for(gid_base, base)
            if "coinbag" not in model_path:
                if cidx is None:
                    return None
                from collections import Counter
                entry = self.entry_for_ref(self.lo, self.models, ref, Counter())
                aim = t + np.array([0.0, 0.25, 0.0])
                out = struct.pack("<IHHB3B9f3f", ref_gid, cidx, NO_MODEL, flags | HARVEST,
                                  *self._light_at(light, aim), *m.reshape(-1), *aim)
                self.stats["plants to harvest"] += 1
                return entry, None, out
            model = self.models.index_for(model_path) if cidx is not None else None
            if model is None:
                return None
            out = struct.pack("<IHHB3B9f3f", ref_gid, cidx, model, flags | HARVEST,
                              *self._light_at(light, t + np.array([0, 0.3, 0])), *m.reshape(-1), *t)
            self.stats["coin purses"] += 1
            return None, None, out
        item = self.items.item_for(gid_base, base)
        model = self.models.index_for(model_path) if item is not None else None
        if model is None:
            return None
        out = struct.pack("<IHHB3B9f3f", ref_gid, item, model, flags, *self._light_at(light, t + np.array([0, 0.3, 0])),
                          *m.reshape(-1), *t)
        self.stats["loose items"] += 1
        return None, None, out

    def _lighting(self, cell, refs):
        lights = []
        for r in refs:
            name = r.sub("NAME")
            base = self.lo.record(self.lo.global_id(r.plugin, _u32(name)), types=("LIGH",)) if name else None
            data = base.sub("DATA") if base is not None else None
            if not data or len(data) < 16 or not r.sub("DATA"):
                continue
            radius = _u32(data, 4)
            xrad = r.sub("XRAD")
            if xrad:
                radius = struct.unpack("<f", xrad)[0]
            fade = struct.unpack("<f", base.sub("FNAM"))[0] if base.sub("FNAM") else 1.0
            if radius <= 0 or fade <= 0:
                continue
            color = np.array(tuple(data[8:11]), np.float32) / 255.0 * min(fade, 2.0)
            pos = to_engine(*struct.unpack_from("<3f", r.sub("DATA")))
            lights.append((pos, radius / UNITS_PER_METER, color))
        xcll = cell.sub("XCLL")
        ltmp = cell.sub("LTMP")
        template = None
        if ltmp:
            t = self.lo.record(self.lo.global_id(cell.plugin, _u32(ltmp)), types=("LGTM",))
            template = t.sub("DATA") if t is not None else None
        self.stats["lights"] += len(lights)
        return Lighting(xcll, template, lights)

    def export(self, pack, bounds, only=None):
        """Writes every selected interior; returns {interior cell gid: index}. only: a set of
        interior names (editor ids, lowercase) - just those written again (tools/reexport_interiors.py),
        the rest of the folder, the names and the doors outside left as they are."""
        from navmesh import export_interior
        out_dir = os.path.join(pack, "interiors")
        os.makedirs(out_dir, exist_ok=True)
        for old in os.listdir(out_dir) if not only else []:
            os.remove(os.path.join(out_dir, old))
        order = self.select(bounds)
        index = {cg: i for i, cg in enumerate(order)}
        names = []
        bake_models = None
        for cg in order:
            c = self.inner[cg]
            eid = (c["cell"].editor_id() if c["cell"] is not None else f"cell{cg:08x}").lower()
            names.append(eid + "\t" + self._name_for(c).replace("\t", " ").replace("\n", " "))
            if only and eid not in only:
                continue
            skipped = Counter()
            light = self._lighting(c["cell"], c["refs"]) if c["cell"] is not None else None
            entries, swings, containers, loose, acts = [], [], [], [], []
            furniture = [f for f in (self._furniture(r) for r in c["refs"]) if f]
            children = {}  # (activate parents: what a lever opens)
            for r in c["refs"]:
                for t, v in r.subrecords():
                    if t == "XAPR" and len(v) >= 4:
                        children.setdefault(self.lo.global_id(r.plugin, _u32(v)), []).append(
                            self.lo.global_id(r.plugin, r.form_id))
            traps = []
            # (the sprint gates' own collision boxes: the gate's script switches its linked box with it -
            # here the gate's model collides as it moves, so the box is left out; and the gates the stage
            # trigger past them opens)
            self._sprint_gates, gate_boxes = [], set()
            for r in c["refs"]:
                base = self.lo.record(self.lo.global_id(r.plugin, _u32(r.sub("NAME"))), types=("ACTI",)) if r.sub("NAME") else None
                if base is not None and "dunustenpuzgatescript" in self._scripts(r, base):
                    self._sprint_gates.append(self.lo.global_id(r.plugin, r.form_id))
                    for t, v in r.subrecords():
                        if t == "XLKR" and len(v) >= 8 and _u32(v) == 0:
                            gate_boxes.add(self.lo.global_id(r.plugin, _u32(v, 4)))
            for r in c["refs"]:
                if self.lo.global_id(r.plugin, r.form_id) in gate_boxes:
                    continue
                trap = self._trap(r, light)
                if trap is not None:
                    if trap[0]:
                        entries.append(trap[0])
                    traps.append(trap[1])
                    continue
                act = self._activator(r, children, light)
                if act is not None:
                    if act[0]:
                        entries.append(act[0])
                    acts.append(act[1])
                    continue
                swing = self._swing_door(r, light, skipped)
                if swing is not None:
                    if swing[0]:
                        entries.append(swing[0])
                    swings.append(swing[1])
                    continue
                loot = self._loot(r, c["cell"], light)
                if loot is not None:
                    entry, container, item = loot
                    if entry:
                        entries.append(entry)
                    if container:
                        containers.append(container)
                    if item:
                        loose.append(item)
                    continue
                e = self.entry_for_ref(self.lo, self.models, r, skipped)
                if e:
                    entries.append(e)
            doors = [d for d in (self._door(r, index) for r in c["refs"]) if d]
            # Placements -> tiles (entries are (model, m, t) for the bake)
            parsed = []
            for e in entries:
                v = struct.unpack("<HH9f3f", e)
                parsed.append((v[0], np.array(v[2:11], np.float32).reshape(3, 3), np.array(v[11:14], np.float32), v[1]))
            rooms = self._rooms(c["refs"])
            self._pending.append((eid, c, entries, doors, light, parsed, swings, containers, loose, acts, furniture,
                                  traps, rooms))
            if export_interior(self.lo, c["navm"], os.path.join(out_dir, eid + ".sknav")):
                self.stats["with navmesh"] += 1
        if only:
            return index
        with open(os.path.join(out_dir, "interiors.txt"), "w") as f:
            f.write("".join(n + "\n" for n in names))
        # Exterior doors into them
        doors = []
        for (x, y), cell in self.cells.items():
            min_x, min_y, max_x, max_y = bounds
            if min_x <= x <= max_x and min_y <= y <= max_y:
                for r in cell["refs"]:
                    d = self._door(r, index)
                    if d and struct.unpack_from("<i", d, 12)[0] != -1:  # (into interiors, cities)
                        doors.append(d)
        with open(os.path.join(out_dir, "exterior.skdoor"), "wb") as f:
            f.write(struct.pack("<4sHHI", b"SKDR", SKDR_VERSION, 0, len(doors)) + b"".join(doors))
        # Each city's doors
        for prefix, (cells, city) in self.cities.items():
            cdoors = [d for key in sorted(city) for d in (self._door(r, index) for r in cells[key]["refs"]) if d]
            with open(os.path.join(out_dir, prefix + ".skdoor"), "wb") as f:
                f.write(struct.pack("<4sHHI", b"SKDR", SKDR_VERSION, 0, len(cdoors)) + b"".join(cdoors))
            self.stats[prefix + " doors"] = len(cdoors)
        self.stats["interiors"] = len(order)
        self.stats["exterior doors"] = len(doors)
        self._write_locks(out_dir, index, bounds)
        return index

    def _write_locks(self, out_dir, index, bounds):
        """Every locked door (Skyrim's XLOC on a DOOR reference - a jail's, a house's, a dungeon's
        way on): PACK_DIR/interiors/locks.txt, a line each
            place (interior index; -1 Tamriel; -2 - n the nth walled city) x y z (the door's place,
            engine space) level (1..100; 255 needs its key) key (the key's form id, hex; 0 none) ref (hex)
            owned (1: someone's - its own owner or its cell's, XOWN: picking it seen is trespass)
            night (1: a home's door, locked only at night - Skyrim's owners lock it when they turn in)
        - only chests could be picked: the doors' locks weren't exported at all."""
        from extract_objects import ref_transform
        lines = []
        # Every door's place, by reference (a house's lock is on its inside door - the other side's
        # is the one that keeps you out)
        where = {}
        groups = [(idx, self.inner[cg]["refs"], self.inner[cg].get("cell")) for cg, idx in index.items()]
        min_x, min_y, max_x, max_y = bounds
        groups += [(-1, cell["refs"], cell.get("cell")) for (x, y), cell in self.cells.items()
                   if min_x <= x <= max_x and min_y <= y <= max_y]
        for n, (_prefix, (cells, city)) in enumerate(self.cities.items()):
            groups += [(-2 - n, cells[key]["refs"], cells[key].get("cell")) for key in city]
        for place, refs, cell in groups:
            for r in refs:
                if r.sub("DATA"):
                    where[self.lo.global_id(r.plugin, r.form_id)] = (place, r, cell)

        def line(place, ref, level, key, cell, night):
            _m, t = ref_transform(ref.sub("DATA"), 1.0)
            lines.append(f"{place} {t[0]:.2f} {t[1]:.2f} {t[2]:.2f} {level} {key or 0:x} "
                         f"{self.lo.global_id(ref.plugin, ref.form_id):x} {int(self._owned(ref, cell))} {night}\n")

        def add(ref, place, cell=None):
            if (ref.editor_id() or "") in BARRED and ref.sub("DATA"):
                line(place, ref, 255, 0, cell, 0)
                return
            xloc = ref.sub("XLOC")
            if not xloc or not ref.sub("NAME") or not ref.sub("DATA") or (ref.editor_id() or "") in NEVER_LOCKED:
                return
            base = self.lo.record(self.lo.global_id(ref.plugin, _u32(ref.sub("NAME"))), types=("DOOR",))
            if base is None:
                return
            key = self.lo.global_id(ref.plugin, _u32(xloc, 4)) if len(xloc) >= 8 and _u32(xloc, 4) else 0
            xtel = ref.sub("XTEL")
            if not xloc[0] and xtel:
                # (level 0 on a load door: a home's lock, open by day - its owner locks it at night;
                # it keeps you out from the other side, never in: Alvor's door locked you inside)
                other = where.get(self.lo.global_id(ref.plugin, _u32(xtel)))
                if other is not None:
                    line(other[0], other[1], 1, key, cell or other[2], 1)  # (owned as the home is)
                    self.stats["doors locked at night"] += 1
                return
            line(place, ref, xloc[0] or 1, key, cell, 0)  # (0 on a door in place: a novice lock - Embershard's jail doors)

        for place, refs, cell in groups:
            for r in refs:
                add(r, place, cell)
        with open(os.path.join(out_dir, "locks.txt"), "w") as f:
            f.write("".join(lines))
        self.stats["locked doors"] = len(lines)

    def bake(self, pack):
        """Bakes the exported interiors (after models.txt is written: the bake reads the
        converted models)."""
        out_dir = os.path.join(pack, "interiors")
        models = Models(pack)
        for eid, c, entries, doors, light, parsed, swings, containers, loose, acts, furniture, traps, rooms in self._pending:
            tiles = {}
            room_boxes, portals, links = rooms
            for p in parsed:
                # (in its room, its tile: a room out of sight - behind its portals - isn't drawn)
                key = (_room_of(room_boxes, p[2]), int(np.floor(p[2][0] / TILE_M)), int(np.floor(p[2][2] / TILE_M)))
                tiles.setdefault(key, []).append(p)
            tile_data, lo_all, hi_all = [], np.full(3, np.inf), np.full(3, -np.inf)
            for key, tile_entries in sorted(tiles.items()):
                builders = build_variant(models, tile_entries, None, LOD_FULL, light=light, lighten=True) if light else \
                    build_variant(models, tile_entries, None, LOD_FULL, lighten=True)
                pts = [part[0] for b in builders for part in b.parts]
                data = batches_file(builders)
                if data is None or not pts:
                    continue
                allp = np.concatenate(pts)
                lo, hi = allp.min(0), allp.max(0)
                lo_all, hi_all = np.minimum(lo_all, lo), np.maximum(hi_all, hi)
                tile_data.append(struct.pack("<6fiI", *lo, *hi, key[0], len(data)) + data + b"\0" * (-len(data) % 4))
            if not tile_data:
                lo_all = hi_all = np.zeros(3)
            name = self._name_for(c).encode("cp1252", "replace")[:255]
            head = struct.pack("<4sHHB", b"SKIT", SKIT_VERSION, 0, len(name)) + name
            head += b"\0" * (-len(head) % 4)
            fog = light.fog if light else np.zeros(3)
            near = light.fog_near if light else 10.0
            far = light.fog_far if light else 60.0
            body = struct.pack("<5f6f", *fog, near, far, *lo_all, *hi_all)
            body += struct.pack("<I", len(doors)) + b"".join(doors)
            body += struct.pack("<I", len(tile_data)) + b"".join(tile_data)
            body += struct.pack("<I", len(entries)) + b"".join(entries)
            body += struct.pack("<I", len(swings)) + b"".join(swings)
            body += struct.pack("<I", len(containers)) + b"".join(containers)
            body += struct.pack("<I", len(loose)) + b"".join(loose)
            body += struct.pack("<I", len(acts)) + b"".join(acts)
            body += struct.pack("<I", len(furniture)) + b"".join(furniture)
            body += struct.pack("<I", len(traps)) + b"".join(traps)
            body += struct.pack("<I", len(room_boxes)) + b"".join(struct.pack("<6f", *b) for b in room_boxes)
            body += struct.pack("<I", len(portals)) + b"".join(struct.pack("<6f2h", *b, ra, rb) for b, ra, rb in portals)
            body += struct.pack("<I", len(links)) + b"".join(struct.pack("<2h", ra, rb) for ra, rb in links)
            self.stats["rooms"] += len(room_boxes)
            self.stats["portals"] += len(portals)
            with open(os.path.join(out_dir, eid + ".skint"), "wb") as f:
                f.write(head + body)
            self.stats["tiles"] += len(tile_data)
        self._pending = []
        print("interiors:", dict(self.stats))

    def _rooms(self, refs):
        """Skyrim's room bounds in an interior (boxes, each a room) and the portals between them
        (two rooms each), and the rooms linked with no portal: ([box (min, max) engine space],
        [(box, room a, room b)], [(room a, room b)])."""
        rooms, room_ix, portals, links = [], {}, [], []

        def box_of(r):
            prm, data = r.sub("XPRM"), r.sub("DATA")
            if not prm or len(prm) < 12 or not data:
                return None
            hx, hy, hz = struct.unpack_from("<3f", prm)
            px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", data)
            s = struct.unpack("<f", r.sub("XSCL"))[0] if r.sub("XSCL") else 1.0
            c, sn = abs(np.cos(rz)), abs(np.sin(rz))
            ex, ey = (c * hx + sn * hy) * s, (sn * hx + c * hy) * s  # (turned about the up axis)
            u = UNITS_PER_METER
            return ((px - ex) / u, (pz - hz * s) / u, (-py - ey) / u, (px + ex) / u, (pz + hz * s) / u, (-py + ey) / u)

        for r in refs:
            if r.type == "REFR" and r.sub("NAME") and _u32(r.sub("NAME")) == ROOM_MARKER:
                b = box_of(r)
                if b is not None:
                    room_ix[self.lo.global_id(r.plugin, r.form_id)] = len(rooms)
                    rooms.append(b)
        for r in refs:
            if not r.sub("NAME"):
                continue
            base = _u32(r.sub("NAME"))
            if base == PORTAL_MARKER and r.sub("XPOD") and len(r.sub("XPOD")) >= 8:
                a, b = (room_ix.get(self.lo.global_id(r.plugin, _u32(r.sub("XPOD"), k * 4)), -1) for k in range(2))
                box = box_of(r)
                if box is not None and (a >= 0 or b >= 0):
                    lo_, hi = np.array(box[:3]) - 0.3, np.array(box[3:]) + 0.3  # (a plane: given some depth)
                    portals.append((tuple(float(v) for v in lo_) + tuple(float(v) for v in hi), a, b))
            elif base == ROOM_MARKER:
                a = room_ix.get(self.lo.global_id(r.plugin, r.form_id), -1)
                for t, v in r.subrecords():
                    if t == "XLRM" and len(v) >= 4 and a >= 0:
                        b = room_ix.get(self.lo.global_id(r.plugin, _u32(v)), -1)
                        if b >= 0:
                            links.append((a, b))
        return rooms, portals, links

    def _name_for(self, c):
        rec = c["cell"]
        if rec is None:
            return "Inside"
        full = rec.sub("FULL")
        text = self.strings.get(rec.plugin, _u32(full), "strings") if full and len(full) == 4 else ""
        return text or rec.editor_id()
