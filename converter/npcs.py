"""Unique NPCs as animated characters with daily schedules (Milestone III, stage 3).

For every unique humanoid NPC (see actors.humanoid_npcs) living in the converted region:

  mesh      the character assembled by actors.ActorBuilder, kept skinned: bind-pose
            vertices in model space with their two strongest bones from a reduced
            skeleton (pelvis, legs, spine, neck, head, arms), which the engine animates
  schedule  their AI packages reduced to a timetable: for each part of the day, a spot
            outdoors to stand or wander around, or a building they are inside (walking
            to its door and disappearing). Sleep packages put them in a bed of that
            building (Skyrim's furniture markers: where on the mattress, which side), one
            bed per sleeper. Packages with conditions are quest- or
            situation-specific and skipped; the first package that applies wins, as in
            the game; outside every package they keep to where the game placed them

  PACK_DIR/actors/<editor id>.sknpc
  PACK_DIR/actors/actors.txt          one file name per line
  PACK_DIR/actors/<editor id>.sktalk  their greetings and idle lines (dialogue.py)

.sknpc layout (little-endian), version 5:
  char[4] "SKNP", u16 version, u16 flags (1: female: the female skeleton and clip set),
  char[32] editor id
  u32 bone count; per bone: i32 parent (-1: root), f32 joint[3] (model space, metres)
  u32 schedule entries; per entry (52 bytes): u16 start minute, u16 end minute (of the day),
      u8 kind (0 outdoors, 1 inside a building), u8 activity (0 none, 1 asleep in a bed: the
      inside spot is the bed's sleeping spot, on the mattress), u8 ways into it (1 from the
      left, 2 from the right: Skyrim's marker entry points; 4 a bedroll), u8 world (0 Tamriel,
      n: walled city n, worlds.txt line n: its streets, or its door for a building),
      f32 pos[3] (the spot, or the outside of the building's door), f32 heading, f32 radius,
      i32 interior (index in interiors/interiors.txt, -1: not converted),
      f32 inside pos[3], f32 inside heading, f32 inside radius (where they are in there)
  default entry (where the game placed them): same layout, minutes 0..1440
  u32 batch count; per batch:
      u8 two-sided, u8 texture name length, u16 part (0: always shown; 0x8000 | slots:
      skin shown unless an equipped item covers one of those body slots; else item + 1),
      name zero-padded to 4,
      u32 vertex count, u32 index count,
      vertices (32 bytes): f32 pos[3], f32 uv[2], s8 normal[3], u8 bone 2, u8 rgba[4],
                           u8 bone 0, u8 bone 1, u8 weight 0, u8 weight 1 (of 255; bone 2 gets the rest)
      u16 indices, zero-padded to 4 bytes
      u32 mouth morph count; per entry: u16 vertex, s16 offset[3] (1/10000 m, model space):
      where that vertex goes with the mouth fully open (talking; Skyrim's lip-sync morphs)
  u32 item count (the player's wardrobe; 0 for NPCs); per item: u32 body slots, u8 equipped,
      u8 name length, name (cp1252), zero-padded to 4
Model space: engine axes (Y up, metres), feet at the origin, facing -Z. A heading h
turns the model so it faces (-sin h, 0, -cos h).
"""
import math
import os
import struct
from collections import Counter

import numpy as np

from actors import ActorBuilder, SKELETONS, humanoid_npcs
from dialogue import Dialogue
from meshes import texture_pack_name
from nif import SHADER_FACEGEN, SHADER_HAIR_TINT
SHADER_SKIN_TINT = 5  # BSLightingShaderProperty type
SKIN_TONE_TAG = 0x4000  # (gear batches: tone these with the wearer's skin, see skin_tone_color)
from textures import convert_image
from skin import Skeleton, pose_shape, skinned_shapes

SKNP_VERSION = 5
UNITS_PER_METER = 70.0
OUTDOORS, INSIDE = 0, 1
DAY_MINUTES = 24 * 60
SLOT_MINUTES = 15
FACE_TEXTURE_SIZE = 256  # faces are seen up close when talking
FACEGEN_TINT_SLOT = 6

# Animated bones, parents first. Every other bone follows its nearest kept ancestor.
BONES = [
    "NPC COM [COM ]", "NPC Pelvis [Pelv]",
    "NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]",
    "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]",
    "NPC Spine [Spn0]", "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Neck [Neck]", "NPC Head [Head]",
    "NPC L Clavicle [LClv]", "NPC L UpperArm [LUar]", "NPC L Forearm [LLar]", "NPC L Hand [LHnd]",
    "NPC R Clavicle [RClv]", "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]", "NPC R Hand [RHnd]",
    # (appended: the indices above are fixed) twist bones keep elbows and wrists from
    # pinching; toes bend the feet
    "NPC L UpperarmTwist1 [LUt1]", "NPC L UpperarmTwist2 [LUt2]", "NPC L ForearmTwist1 [LLt1]",
    "NPC L ForearmTwist2 [LLt2]", "NPC R UpperarmTwist1 [RUt1]", "NPC R UpperarmTwist2 [RUt2]",
    "NPC R ForearmTwist1 [RLt1]", "NPC R ForearmTwist2 [RLt2]", "NPC L Toe0 [LToe]", "NPC R Toe0 [RToe]",
    # fingers (three joints each, thumb first): hands close round what they hold
    *[f"NPC {s} Finger{f}{j} [{s}F{f}{j}]" for s in "LR" for f in range(5) for j in range(3)],
]

HEAD_BONE = "NPC Head [Head]"
PART_SKIN = 0x8000
SLOT_HAIR, SLOT_BODY, SLOT_HANDS, SLOT_FEET = 1 << 1, 1 << 2, 1 << 3, 1 << 7
SKIN_SLOTS = SLOT_BODY | SLOT_HANDS | SLOT_FEET
SKIN_TONE_LIGHT = 0.75  # a fair skin tone (a Nord's is about this): the body texture's own
BOUND_SWORD = "BoundWeaponSword"
# (Conjuration's weapons: the Bound Sword, Bow, Battleaxe and - Dragonborn's - Dagger)
BOUND_WEAPONS = (BOUND_SWORD, "BoundWeaponBow", "BoundWeaponBattleaxe", "DLC2BoundWeaponDagger")  # (the Bound Sword spell's weapon)
# Carried from the start for trying things out (editor id, count): none (looted in Helgen Keep)
PLAYER_EXTRA_ITEMS = []
PLAYER_ITEMS = [  # (editor id, equipped at the start)
    ("ClothesPrisonerTunic", True), ("ClothesPrisonerShoes", True),
    ("ClothesFarmClothes01", False), ("ClothesFarmBoots01", False),
    ("ArmorHideCuirass", False), ("ArmorHideBoots", False), ("ArmorHideGauntlets", False), ("ArmorHideHelmet", False),
    ("ArmorIronCuirass", False), ("ArmorIronBoots", False), ("ArmorIronGauntlets", False), ("ArmorIronHelmet", False),
    ("ArmorImperialLightCuirass", False),
]
JAW_HINGE_BACK, JAW_HINGE_UP = 2.5, 2.5  # Skyrim units from the mouth: near the ears
# The open mouth for talking: a mix of Skyrim's lip-sync morphs (each head part's .tri)
MOUTH_MORPHS = (("Aah", 0.6), ("BigAah", 0.45))
MORPH_UNIT_M = 1e-4
MORPH_REC = np.dtype([("v", "<u2"), ("d", "<i2", 3)])
# The character creator's batch roles (.skmorph, chargen.py): its face (the texture the engine makes
# from Skyrim's tint layers), bare skin (the skin tone) and hair (the hair colour)
ROLE_FACE, ROLE_SKIN, ROLE_HAIR = 1, 2, 3
WEIGHT_MAX_M = 0.1  # (the thin build: no vertex further than this from the heavy one)


def read_tri_morphs(data):
    """FaceGen .tri (FRTRI003) -> (vertex count, {morph name: (N,3) offsets, shape units})."""
    if data[:8] != b"FRTRI003":
        return 0, {}
    nv, nt, nq, _u1, _u2, nuv, flags, nm, _nmm, nmv = struct.unpack_from("<10i", data, 8)
    p = 64 + (nv + nmv) * 12 + nt * 12 + nq * 16
    if flags & 1:
        p += nuv * 8 + nt * 12 + nq * 16
    morphs = {}
    for _ in range(nm):
        n = struct.unpack_from("<i", data, p)[0]
        name = data[p + 4:p + 4 + n].split(b"\0")[0].decode("ascii", "replace")
        p += 4 + n
        scale = struct.unpack_from("<f", data, p)[0]
        morphs[name] = np.frombuffer(data, "<i2", nv * 3, p + 4).reshape(nv, 3).astype(np.float64) * scale
        p += 4 + nv * 6
    return nv, morphs


HAIR_MIN_LUMINANCE = 0.2  # (dark hair: its strands kept from a black mass - at 0.55, dark brown and black came out a light grey that read as blonde in daylight: Lydia, Farkas)


def _hair_color(tint):
    """Vertex tint for hair, brows and beards: the hair colour x 2 (as Skyrim's shader), with
    dark colours lifted so the strands in the texture still show instead of a black mass."""
    c = np.array(tint, np.float64) * 2.0
    lum = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]
    if lum < HAIR_MIN_LUMINANCE:
        c = c * (HAIR_MIN_LUMINANCE / max(lum, 1e-3)) if lum > 0.02 else np.full(3, HAIR_MIN_LUMINANCE)
    return (np.clip(c, 0, 1) * 255).astype(np.uint8)


def _jaw_weight(pos, mouth_z, hinge_y, is_mouth):
    """How much each vertex (skeleton space, Skyrim units: y forward, z up) follows the jaw:
    everything just below the mouth line and in front of the hinge, fading out down the
    neck. The mouth interior splits sharply (upper teeth stay, lower teeth go)."""
    z, y = pos[:, 2], pos[:, 1]
    below = np.clip((mouth_z + 0.25 - z) / (0.25 if is_mouth else 0.6), 0.0, 1.0)
    if is_mouth:
        return below
    front = np.clip((y - (hinge_y - 1.0)) / 3.0, 0.0, 1.0)
    neck = np.clip((z - (mouth_z - 8.5)) / 2.5, 0.0, 1.0)  # the chin moves, the throat doesn't
    return below * front * neck


# PLDT location types this understands
LOC_NEAR_REF, LOC_IN_CELL, LOC_NEAR_EDITOR = 0, 1, 3
ACT_NONE, ACT_SLEEP = 0, 1
FURNITURE_SLEEP = 2                   # BSFurnitureMarkerNode animation type
ENTRY_LEFT, ENTRY_RIGHT = 0x8, 0x4    # ... entry points


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def to_engine(px, py, pz):
    return (px / UNITS_PER_METER, pz / UNITS_PER_METER, -py / UNITS_PER_METER)


class World:
    """Where references are: outdoors (position) or in an interior (its door outside)."""

    def __init__(self, load_order, cells, cities=None):
        self.lo = load_order
        self.cells = cells
        # Walled cities' references: their streets are spots too (world n + 1)
        self.city = {}
        for n, (cc, keys) in enumerate((cities or {}).values()):
            for key in keys:
                for ref in cc[key]["refs"]:
                    self.city[load_order.global_id(ref.plugin, ref.form_id)] = (n + 1, ref)
        self.outdoor = {}       # global ref id -> REFR/ACHR in Tamriel
        for c in cells.values():
            for ref in c["refs"]:
                self.outdoor[load_order.global_id(ref.plugin, ref.form_id)] = ref
        self.indoor = load_order.interior_refs()  # global ref id -> (cell id, ref)
        # Interior cell -> where you come out of its doors (an exterior door's arrival spot)
        self.doors = {}
        for g, (cell, ref) in self.indoor.items():
            xtel = ref.sub("XTEL")
            if not xtel or len(xtel) < 28:
                continue
            dest = load_order.global_id(ref.plugin, _u32(xtel))
            if dest in self.outdoor or dest in self.city:
                px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", xtel, 4)
                world = self.city[dest][0] if dest in self.city else 0
                self.doors.setdefault(cell, []).append((to_engine(px, py, pz), -rz, world))
        # (rooms within rooms - the Ragged Flagon, the Ratway Warrens, in through the Ratway from
        # Riften's streets: the way out of the room their door leads into)
        inner = {}
        for g, (cell, ref) in self.indoor.items():
            xtel = ref.sub("XTEL")
            dest = load_order.global_id(ref.plugin, _u32(xtel)) if xtel and len(xtel) >= 28 else None
            if dest in self.indoor and self.indoor[dest][0] != cell:
                inner.setdefault(cell, []).append(self.indoor[dest][0])
        for _depth in range(4):
            for cell, ways in inner.items():
                if cell not in self.doors:
                    via = next((w for w in ways if w in self.doors), None)
                    if via is not None:
                        self.doors[cell] = self.doors[via]
        # Interior cell -> where you arrive inside through its front door
        self.inside_entry = {}
        for ref in self.outdoor.values():
            xtel = ref.sub("XTEL")
            if not xtel or len(xtel) < 28:
                continue
            dest = load_order.global_id(ref.plugin, _u32(xtel))
            if dest in self.indoor:
                px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", xtel, 4)
                self.inside_entry.setdefault(self.indoor[dest][0], (to_engine(px, py, pz), -rz))
        # NPC -> its placed reference (the "editor location")
        self.placement = {}
        for refs in (self.outdoor.items(), ((g, r) for g, (_c, r) in self.indoor.items()),
                     ((g, r) for g, (_w, r) in self.city.items())):
            for g, ref in refs:
                if ref.type == "ACHR" and ref.sub("NAME"):
                    base = load_order.global_id(ref.plugin, _u32(ref.sub("NAME")))
                    self.placement.setdefault(base, g)

    def beds(self, cell_gid):
        """[(pos, heading, sleep clip)] in an interior: every sleeping spot of its beds
        (needs self.read_nif: path -> NIF or None)."""
        if not hasattr(self, "_beds"):
            self._beds = {}
            for _g, (cell, ref) in self.indoor.items():
                if ref.type != "REFR" or not ref.sub("NAME") or not ref.sub("DATA"):
                    continue
                base = self.lo.record(self.lo.global_id(ref.plugin, _u32(ref.sub("NAME"))), types=("FURN",))
                modl = base.sub("MODL") if base is not None else None
                if not modl or b"bed" not in modl.lower():
                    continue
                self._beds.setdefault(cell, []).append((ref, modl.rstrip(b"\0").decode("cp1252").lower()))
        out = []
        for ref, path in self._beds.get(cell_gid, []):
            nif = self.read_nif("meshes\\" + path) if getattr(self, "read_nif", None) else None
            if nif is None:
                continue
            from extract_objects import ref_transform
            m, t = ref_transform(ref.sub("DATA"), 1.0)
            rz = struct.unpack_from("<6f", ref.sub("DATA"))[5]
            for ox, oy, oz, heading, anim, entry in furniture_markers(nif):
                if anim != FURNITURE_SLEEP:
                    continue
                pos = m @ (np.array([ox, oz, -oy]) / UNITS_PER_METER) + t
                pos[1] = t[1]  # (on the floor: Skyrim's sleeping clips lift the body onto the bed)
                # which ways in: 1 the left, 2 the right (the engine picks a free one), 4 a bedroll
                clip = 4 if "bedroll" in path else ((1 if entry & ENTRY_LEFT else 0) | (2 if entry & ENTRY_RIGHT else 0)) or 1
                out.append((tuple(float(v) for v in pos), -(rz + heading), clip))
        return out

    def claim_bed(self, cell_gid, near, sleeper):
        """A bed's sleeping spot in the building for this sleeper (the same one every
        night; housemates get different ones), nearest `near`; None if there's none."""
        claims = self.__dict__.setdefault("_claims", {})
        if (cell_gid, sleeper) in claims:
            return claims[(cell_gid, sleeper)]
        taken = {v for (c, _s), v in claims.items() if c == cell_gid}
        free = [b for b in self.beds(cell_gid) if b not in taken]
        if not free:
            return None
        best = min(free, key=lambda b: (b[0][0] - near[0]) ** 2 + (b[0][2] - near[2]) ** 2)
        claims[(cell_gid, sleeper)] = best
        return best

    def spot(self, ref_gid, radius=0.0):
        """(kind, pos, heading, radius, interior cell, inside pos, inside heading, inside
        radius) for being near a reference, or None."""
        ref, world = self.outdoor.get(ref_gid), 0
        if ref is None and ref_gid in self.city:
            world, ref = self.city[ref_gid]
        if ref is not None and ref.sub("DATA"):
            px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", ref.sub("DATA"))
            return OUTDOORS, to_engine(px, py, pz), -rz, radius, None, (0.0, 0.0, 0.0), 0.0, 0.0, ACT_NONE, 0, world
        if ref_gid in self.indoor:
            cell, inner = self.indoor[ref_gid]
            if inner.sub("DATA"):
                px, py, pz, _rx, _ry, rz = struct.unpack_from("<6f", inner.sub("DATA"))
                return self.building(cell, (to_engine(px, py, pz), -rz, max(radius, 1.0)))
            return self.building(cell)
        return None

    def building(self, cell_gid, inside=None):
        """Inside a building: its outside door, and where in there (default: just inside
        the front door, milling about)."""
        doors = self.doors.get(cell_gid)
        if not doors:
            return None  # not entered from Tamriel directly (inner rooms, other worlds)
        pos, heading, world = doors[0]
        if inside is None:
            entry = self.inside_entry.get(cell_gid)
            inside = (entry[0], entry[1], 3.0) if entry else ((0.0, 0.0, 0.0), 0.0, 0.0)
        return INSIDE, pos, heading, 0.0, cell_gid, tuple(inside[0]), inside[1], inside[2], ACT_NONE, 0, world


def _package_target(world, npc_gid, pack):
    """Where a package keeps the NPC, or None if it can't be expressed."""
    for st, sv in pack.subrecords():
        if st != "PLDT" or len(sv) < 8:
            continue
        kind = struct.unpack_from("<i", sv)[0]
        radius = struct.unpack_from("<i", sv, 8)[0] / UNITS_PER_METER if len(sv) >= 12 else 0.0
        target = world.lo.global_id(pack.plugin, _u32(sv, 4))
        if kind == LOC_NEAR_REF:
            return world.spot(target, radius)
        if kind == LOC_IN_CELL:
            home = world.placement.get(npc_gid)  # at their own spot if it's in there
            if home in world.indoor and world.indoor[home][0] == target:
                return world.spot(home, 3.0)
            return world.building(target)
        if kind == LOC_NEAR_EDITOR:
            return world.spot(world.placement.get(npc_gid), radius)
        return None
    return None


def _template(lo, pack):
    """The editor id of the package's template (Skyrim's package kinds: Sleep, Sandbox...)."""
    pkcu = pack.sub("PKCU")
    if not pkcu or len(pkcu) < 8:
        return None
    rec = lo.record(lo.global_id(pack.plugin, _u32(pkcu, 4)), types=("PACK",))
    return rec.editor_id() if rec is not None else None


def furniture_markers(nif):
    """[(x, y, z, heading, animation type, entry points)] of a NIF's BSFurnitureMarkerNode
    (Skyrim units and axes, model space)."""
    from nif import Reader
    out = []
    for i, t in enumerate(nif.block_types):
        if t != "BSFurnitureMarkerNode":
            continue
        r = Reader(nif.data, nif.block_offsets[i])
        r.i32()  # name
        for _ in range(r.u32()):
            x, y, z, heading = r.unpack("4f")
            anim, entry = r.unpack("HH")
            out.append((x, y, z, heading, anim, entry))
    return out


def schedule(world, npc, npc_gid, stats):
    """([(start minute, end minute, target)], default target or None)."""
    packages = []
    for t, v in npc.subrecords():
        if t != "PKID":
            continue
        pack = world.lo.record(world.lo.global_id(npc.plugin, _u32(v)), types=("PACK",))
        if pack is None:
            continue
        if any(st == "CTDA" for st, _ in pack.subrecords()):
            stats["packages with conditions (skipped)"] += 1
            continue
        psdt = pack.sub("PSDT")
        if not psdt or len(psdt) < 12:
            continue
        day_of_week, hour, minute = struct.unpack_from("<bbb", psdt, 1)[0], struct.unpack_from("<b", psdt, 3)[0], \
            struct.unpack_from("<b", psdt, 4)[0]
        duration = struct.unpack_from("<i", psdt, 8)[0]
        if day_of_week not in (-1,):
            continue  # weekly routines aren't modelled
        target = _package_target(world, npc_gid, pack)
        if target is None:
            stats["packages without a usable location"] += 1
            continue
        if _template(world.lo, pack) == "Sleep" and target[0] == INSIDE:
            bed = world.claim_bed(target[4], target[5], npc_gid)
            if bed is not None:  # asleep in it
                target = target[:5] + (bed[0], bed[1], 0.0, ACT_SLEEP, bed[2], target[10])
                stats["sleep packages in a bed"] += 1
            else:
                stats["sleep packages without a bed"] += 1
        if hour < 0 or duration <= 0:
            window = None  # any time
        else:
            start = hour * 60 + max(minute, 0)
            window = (start, start + duration)
        packages.append((window, target))

    default = world.spot(world.placement.get(npc_gid))
    slots = []
    for s in range(DAY_MINUTES // SLOT_MINUTES):
        m = s * SLOT_MINUTES + SLOT_MINUTES // 2
        chosen = None
        for window, target in packages:
            if window is None or any(window[0] <= mm < window[1] for mm in (m, m + DAY_MINUTES)):
                chosen = target
                break
        slots.append(chosen)
    entries = []
    for s, target in enumerate(slots):
        if target is None:
            continue
        start, end = s * SLOT_MINUTES, (s + 1) * SLOT_MINUTES
        if entries and entries[-1][2] == target and entries[-1][1] == start:
            entries[-1] = (entries[-1][0], end, target)
        else:
            entries.append((start, end, target))
    return entries, default


def _entry_bytes(start, end, target, interiors):
    kind, pos, heading, radius, cell, inside, inside_heading, inside_radius, activity, clip, world = target
    return struct.pack("<HHBBBB3fffi3fff", start, end, kind, activity, clip, world, *pos, heading, radius,
                       interiors.get(cell, -1), *inside, inside_heading, inside_radius)


# Skyrim's opening moves these to Helgen by script from elsewhere (Ulfric from Windhelm...):
# exported with no schedule, out of sight till the engine's opening places them
PLAYER_MEN = 5    # (the race menu: this many men's faces of each race - its donors, then Skyrim's named men...)
PLAYER_WOMEN = 5  # (...and this many women's after them; each face its own hair)
PLAYER_WOMEN_SKIP = ("lvl", "encounter", "dummy", "generic", "guard", "soldier", "bandit", "dlc", "vampire", "ghost",
                     "child", "corpse", "dead", "test", "mq", "cw", "dummy", "audience", "hag")
PART_PROP = 0x1000  # (a batch the engine shows only while a script says: actors_script_prop)
# Skyrim's AnimObjects a scene puts in someone's hands (model, the skeleton nodes to try)
HELD_PROPS = {"Hadvar": (("animobjects\\animobjectledger.nif", ("AnimObjectL", "SHIELD")),
                         ("animobjects\\animobjectquill.nif", ("AnimObjectR", "WEAPON")))}
FOES = ("MS13Arvel",)  # (exported with the creatures - creatures.py HOSTILE_PEOPLE: an enemy, not someone to talk to)
ARMED_CAST = ("MQ101Headsman",)  # (the opening: a weapon in hand all the while)
SCRIPTED_CAST = ("Ulfric", "GeneralTullius", "Elenwen", "MQ101Headsman", "MQ101ImperialPriest",
                 "EtienneRarnis",  # (Diplomatic Immunity: in the Embassy's dungeon)
                 "Igmund", "Maven", "Ondolemar")  # (... and guests at its party from beyond the map)
OFFSTAGE = (INSIDE, (0.0, 0.0, 0.0), 0.0, 0.0, None, (0.0, 0.0, 0.0), 0.0, 0.0, ACT_NONE, 0, 0)


class Exporter:
    def __init__(self, load_order, cells, models, find, cities=None):
        """models: extract_objects.ModelConverter (archives, texture conversion); cities:
        worlds.export's (their streets are spots too)."""
        self.lo = load_order
        self.models = models
        self.world = World(load_order, cells, cities)
        self.world.read_nif = lambda path: self.builder._nif(path)
        self.items = None  # items.Database: NPCs' pockets (pickpocketing)
        self.skin_marker = False  # (gear: skin shapes marked for the wearer's tone, not toned)
        self.custom_face = None  # (a made face: its texture name and the donor's tint to colour the bare head)
        self.body_tint = None  # (a player character: their stem and face tint, to bake their skin with)
        self.race_morphs = {}  # (model path: the race's morph offsets - chargen's heads and parts)
        self.texture_override = {}  # (model path: its part's own diffuse - chargen's eyes, brows, scars)
        # The character creator's (chargen.py): per model path, its chargen morphs ({name: (N,3) shape
        # offsets} - NoseLong, EyesType20...) carried to the batches; Skyrim's thin build (_0 of each _1
        # model) as the "weight" morph; and the look's roles - the face (its texture made by the engine),
        # bare skin and hair (coloured there: skin tone, hair colour) - left white here
        self.morph_sources = {}
        self.want_weight = False
        self.chargen_mode = False
        self.last_extras = []  # (_batches': per batch (role, {morph name: records}))
        self.builder = ActorBuilder(load_order, models.mesh_archives, find)
        self.skeletons = {}
        self.find = find
        self.faces_done = set()
        self.stats = Counter()

    def _thin_twin(self, path, shape):
        """The same shape in Skyrim's thin build of a model (its _0 beside the _1: weight 0), or None."""
        if not path.endswith("_1.nif"):
            return None
        nif = self.builder._nif(path[:-6] + "_0.nif")
        if nif is None:
            return None
        for s in skinned_shapes(nif):
            if s.name == shape.name and len(s.positions) == len(shape.positions):
                return s
        return None

    def _mouth_morph(self, part_name, vertex_count):
        """The open-mouth offsets (N,3, shape units) for a FaceGen head part (its shape is
        named after the head part record), or None if it has no lip-sync morphs."""
        if not hasattr(self, "_part_tris"):
            self._part_tris, self._tri_cache = {}, {}
            for plugin in self.lo.plugins:
                for rec in plugin.records_of_type("HDPT"):
                    kind = None
                    for t, d in rec.subrecords():
                        if t == "NAM0":
                            kind = _u32(d)
                        elif t == "NAM1" and kind == 1:
                            self._part_tris[rec.editor_id().lower()] = d.split(b"\0")[0].decode("ascii", "replace")
        path = self._part_tris.get(part_name.lower())
        if not path:
            return None
        if path not in self._tri_cache:
            try:
                data = self.find(self.models.mesh_archives, "meshes\\" + path.lower())
                self._tri_cache[path] = read_tri_morphs(data)
            except (KeyError, OSError, struct.error, ValueError):
                self._tri_cache[path] = (0, {})
        nv, morphs = self._tri_cache[path]
        if nv != vertex_count or not all(m in morphs for m, _w in MOUTH_MORPHS):
            return None
        return sum(morphs[m] * w for m, w in MOUTH_MORPHS)

    def _skeleton(self, female, path=None):
        """path: another skeleton (the first-person one) instead of the race's."""
        key = (female, path)
        if key not in self.skeletons:
            sk = Skeleton(self.builder._nif(path or SKELETONS[female]))
            world = sk.world()
            kept = [b for b in BONES if b in world]
            index = {b: i for i, b in enumerate(kept)}
            parents = []
            for b in kept:
                p = sk.parent.get(b)
                while p is not None and p not in index:
                    p = sk.parent.get(p)
                parents.append(index.get(p, -1) if p is not None else -1)

            def kept_for(name):
                while name is not None and name not in index:
                    name = sk.parent.get(name)
                return index.get(name, 0)

            joints = [to_engine(*world[b][:3, 3]) for b in kept]
            self.skeletons[key] = (world, kept, parents, joints, kept_for)
        return self.skeletons[key]

    def _face_texture(self, eid, diffuse, tint, flat=False):
        """The NPC's own face: the race's head texture x their baked face tint x 2 (what
        Skyrim's FaceGen shader does). Returns the pack texture name, or None.
        flat: the tint's skin colour alone, without its paint and shading (a made face)."""
        from io import BytesIO
        from PIL import Image
        name = f"facegen_{eid.lower()}"
        if name in self.faces_done:
            return name
        try:
            base = Image.open(BytesIO(self.find(self.models.tex_archives, diffuse))).convert("RGB")
            tint_img = Image.open(BytesIO(self.find(self.models.tex_archives, tint))).convert("RGB")
        except (KeyError, OSError):
            self.stats["faces without a tint"] += 1
            return None
        size = (FACE_TEXTURE_SIZE * 2, FACE_TEXTURE_SIZE * 2)
        b = np.asarray(base.resize(size, Image.LANCZOS), np.float32)
        t = np.asarray(tint_img.resize(size, Image.LANCZOS), np.float32)
        if flat:
            t = np.broadcast_to(np.median(t.reshape(-1, 3), axis=0), t.shape)
        face = np.clip(b * t * (2.0 / 255.0), 0, 255).astype(np.uint8)
        with open(os.path.join(self.models.pack, "textures", name + ".sktex"), "wb") as f:
            f.write(convert_image(Image.fromarray(face), max_size=FACE_TEXTURE_SIZE))
        self.faces_done.add(name)
        self.stats["faces"] += 1
        return name

    def _held_weapon(self, npc):
        """The first weapon the NPC carries or wears (CNTO, its outfit's INAM: a leveled
        list's first), rigid on the right hand's WEAPON node."""
        import gear
        u32 = lambda v, o=0: struct.unpack_from("<I", v, o)[0]
        cands = [self.lo.global_id(npc.plugin, u32(v)) for t, v in npc.subrecords() if t == "CNTO" and len(v) >= 4]
        doft = npc.sub("DOFT")
        outfit = self.lo.record(self.lo.global_id(npc.plugin, u32(doft)), types=("OTFT",)) if doft else None
        inam = (outfit.sub("INAM") or b"") if outfit is not None else b""
        cands += [self.lo.global_id(outfit.plugin, u32(inam, i * 4)) for i in range(len(inam) // 4)]

        def weapon(g, depth=0):
            w = self.lo.record(g, types=("WEAP", "LVLI"))
            if w is None or depth > 4:
                return None
            if w.type == "WEAP":
                return w if w.sub("MODL") else None
            for t, v in w.subrecords():
                if t == "LVLO" and len(v) >= 8:
                    found = weapon(self.lo.global_id(w.plugin, u32(v, 4)), depth + 1)
                    if found is not None:
                        return found
            return None

        # (the headsman's is given by MQ101's alias, not his own: Skyrim's iron battleaxe)
        fallback = [self.lo.global_id(p, r.form_id) for p in self.lo.plugins[:1]
                    for r in p.records_of_type("WEAP") if r.editor_id() == "IronBattleaxe"]
        for g in cands + fallback:
            w = weapon(g)
            if w is None:
                continue
            path = w.sub("MODL").split(b"\0")[0].decode("latin-1")
            try:
                nif = self.builder._nif("meshes\\" + path)
            except (KeyError, ValueError):
                continue
            held = gear.rigid_batches(self, False, 0, nif, "WEAPON")
            self.stats["held weapons" if held else f"held weapon unplaced: {path}"] += 1
            return [b + (None,) for b in held]
        self.stats[f"held weapon none of {len(cands)}"] += 1
        return []

    def _batches(self, npc, gid, parts=None, skeleton_path=None):
        """parts: (female, race, [(nif path, hide hair, part tag)]) instead of the NPC's own;
        skeleton_path: posed on that skeleton (the first-person one) instead."""
        res = parts or self.builder.parts(npc, gid)
        if res is not None and parts is None:
            # (a quest's piece - Ulfric's gag - a prop: shown only while the opening says, not in Windhelm)
            res = (res[0], res[1], [(p, h, PART_PROP if p in self.builder.worn_props else 0) for p, h in res[2]])
        if res is None:
            self.stats["not assembled"] += 1
            return None, None
        female, _race, parts = res
        race_skins = self.builder.race_skin_textures(_race, female)
        race_rec = self.lo.record(_race, types=("RACE",)) if _race is not None else None
        is_beast_race = bool(race_rec and (race_rec.editor_id() or "").lower().startswith(("argonian", "khajiit")))
        world, kept, parents, joints, kept_for = self._skeleton(female, skeleton_path)
        # A jaw for talking (Skyrim moves the mouth with morphs; a hinge is the PS2 way):
        # a bone under the head, hinged near the ears, carrying what is below the mouth
        head = kept.index(HEAD_BONE) if HEAD_BONE in kept else None
        jaw = len(kept)
        head_nif = self.builder._nif(parts[0][0])
        mouth_z = world[HEAD_BONE][2, 3] - 1.0 if head is not None else 0.0
        for s in skinned_shapes(head_nif) if head_nif is not None else []:
            if s.texture and "\\mouth\\" in s.texture:
                mouth_z = float(np.median(pose_shape(s, world)[0][:, 2]))
        hinge = (0.0, (world[HEAD_BONE][1, 3] if head is not None else 0.0) - JAW_HINGE_BACK, mouth_z + JAW_HINGE_UP)
        groups = {}
        for i, (path, hide_hair, tag) in enumerate(parts):
            nif = self.builder._nif(path)
            if nif is None:
                if i == 0:
                    self.stats["missing FaceGen head"] += 1
                    return None, None
                continue
            for s in skinned_shapes(nif):
                if not s.texture or (hide_hair and s.name.lower().startswith("hair")):
                    continue
                if self.texture_override.get(path): # (its part's own texture set)
                    import copy
                    s = copy.copy(s)
                    s.texture = self.texture_override[path]
                shader_type, textures, hair_tint = nif.lighting_shader(s.shader) or (0, [], None)
                # (a scar - its own texture set, "facedetails" - keeps its texture: given the body's, it was a pale
                # or grey patch on the face, the user's Nov 8 photos; it still takes the skin's tone, as a skin part)
                own_detail = bool(self.texture_override.get(path)) or "facedetails" in s.texture.lower()
                if (i > 0 or not path.startswith("meshes\\actors\\character\\facegendata\\")) and not own_detail:
                    if shader_type == SHADER_SKIN_TINT or "handsargonian" in s.texture.lower() or "malebody_1" in s.texture.lower() or "femalebody_1" in s.texture.lower() or "malehands_1" in s.texture.lower() or "femalehands_1" in s.texture.lower():
                        is_hand = bool(tag & SLOT_HANDS) or "hand" in s.texture.lower() or "hand" in path.lower()
                        skin_tex = race_skins.get(SLOT_HANDS if is_hand else SLOT_BODY)
                        if skin_tex and skin_tex != s.texture:
                            import copy
                            s = copy.copy(s)
                            s.texture = skin_tex
                face = None
                if shader_type == SHADER_FACEGEN and len(textures) > FACEGEN_TINT_SLOT and textures[FACEGEN_TINT_SLOT]:
                    face = self._face_texture(npc.editor_id(), s.texture, textures[FACEGEN_TINT_SLOT])
                elif shader_type == SHADER_FACEGEN and "head" in s.texture.lower() and self.custom_face and self.custom_face[1]:
                    face = self._face_texture(*self.custom_face[:1], s.texture, self.custom_face[1], flat=True)
                # Hair, brows and beards are grey: the shader tints them with the hair colour
                color = _hair_color(hair_tint) if shader_type == SHADER_HAIR_TINT and hair_tint is not None else None
                # Bare skin (bodies, hands, the skin under armour) takes the NPC's skin tone, as
                # Skyrim's skin-tint shader does: humans and elves share one body texture
                skin_tone = shader_type == SHADER_SKIN_TINT and not is_beast_race
                if skin_tone and not self.skin_marker:
                    color = skin_tone_color(npc)
                    if self.body_tint is not None and self.body_tint[1]:
                        # (a player character's: the skin baked with their face's own tint, as the face
                        # is - a vertex colour only darkens, and a High Elf's gold face sat on a pale body)
                        base = s.texture.replace("\\", "/").split("/")[-1].rsplit(".", 1)[0].lower()
                        face = self._face_texture(f"skin_{self.body_tint[0]}_{base}", s.texture, self.body_tint[1], flat=True)
                        if face is not None:
                            color = None
                # The character creator's look (chargen.py): the face's texture, bare skin's tone and
                # hair's colour are the engine's to make - white here, their batches marked
                role = 0
                if self.chargen_mode:
                    if shader_type == SHADER_FACEGEN and "head" in s.texture.lower():
                        role = ROLE_FACE
                    elif shader_type == SHADER_SKIN_TINT:
                        role, color = ROLE_SKIN, None
                        if face is not None and face.startswith("facegen_skin_"):
                            face = None
                    elif shader_type == SHADER_HAIR_TINT:
                        role, color = ROLE_HAIR, None
                morph_r = self.race_morphs.get(path) if self.race_morphs else None
                if morph_r is not None and len(morph_r) == len(s.positions): # (the race's skull: chargen.py)
                    saved_r = s.positions
                    s.positions = saved_r + morph_r
                    pos, nrm = pose_shape(s, world)
                    s.positions = saved_r
                else:
                    pos, nrm = pose_shape(s, world)
                if not np.isfinite(pos).all():
                    continue
                # Its chargen morphs and its thin build, as offsets of the posed vertices (posing is
                # linear: a morph's share of a slider scales them)
                ext = {}
                srcs = self.morph_sources.get(path) if self.morph_sources else None
                if srcs:
                    saved = s.positions
                    base_local = saved + (morph_r if morph_r is not None and len(morph_r) == len(saved) else 0.0)
                    for name, m in srcs.items():
                        if len(m) != len(saved):
                            continue
                        s.positions = base_local + m
                        moved, _ = pose_shape(s, world)
                        ext[name] = moved - pos
                    s.positions = saved
                if self.want_weight:
                    thin = self._thin_twin(path, s)
                    if thin is not None:
                        thin_pos, _ = pose_shape(thin, world)
                        off = thin_pos - pos
                        # (Skyrim's builds differ by a few centimetres; more is a twin that doesn't pose on this
                        # skeleton - a third-person model borrowed for the first-person arms: 75 cm out)
                        if np.isfinite(off).all() and np.abs(off).max() / UNITS_PER_METER <= WEIGHT_MAX_M:
                            ext["weight"] = off
                            self.stats["weight morphs"] += 1
                        else:
                            self.stats["weight twins left out"] += 1
                # Talking: the head part's own open-mouth morph, carried through the skin
                mouth = None
                morph = self._mouth_morph(s.name, len(s.positions)) if i == 0 else None
                if morph is not None:
                    saved = s.positions
                    s.positions = saved + morph + (morph_r if morph_r is not None and len(morph_r) == len(saved) else 0.0)
                    opened, _ = pose_shape(s, world)
                    s.positions = saved
                    mouth = opened - pos
                    self.stats["mouth morphs"] += 1
                # Two strongest bones after folding unanimated bones into their kept ancestor
                mapped = np.array([kept_for(n) for n in s.bone_names], np.int32)[s.bones]
                dense = np.zeros((len(pos), len(kept)))
                np.add.at(dense, (np.repeat(np.arange(len(pos)), 4), mapped.reshape(-1)), s.weights.reshape(-1))
                # The three strongest bones, renormalized
                order = np.argsort(-dense, axis=1)
                rows = np.arange(len(pos))
                b0, b1, b2 = order[:, 0], order[:, 1], order[:, 2]
                top = np.stack([dense[rows, b0], dense[rows, b1], dense[rows, b2]], 1)
                total = np.maximum(top.sum(1, keepdims=True), 1e-9)
                wts = np.where(top.sum(1, keepdims=True) > 0, top / total, np.array([[1.0, 0.0, 0.0]]))
                b1 = np.where(wts[:, 1] > 0, b1, b0)
                b2 = np.where(wts[:, 2] > 0, b2, b0)
                w0, w1 = wts[:, 0], wts[:, 1]
                if i == 0 and head is not None and mouth is None and "\\mouth\\" in s.texture:
                    # the FaceGen head: lower lip, chin, beard, teeth (not hair beside the cheeks)
                    j = _jaw_weight(pos, mouth_z, hinge[1], is_mouth="\\mouth\\" in s.texture)
                    moving = j > 0.0  # (the jaw and the head: head-only vertices here)
                    b1 = np.where(moving, b0, b1)
                    b2 = np.where(moving, b0, b2)
                    b0 = np.where(moving, jaw, b0)
                    w1 = np.where(moving, 1.0 - j, w1)
                    w0 = np.where(moving, j, w0)
                alpha = s.alpha_test
                shape_tag = tag | (SKIN_TONE_TAG if skin_tone and self.skin_marker else 0)
                if i == 0 and s.name.lower().startswith("hair"):
                    shape_tag = PART_SKIN | SLOT_HAIR  # (the player's: hidden under a helmet)
                key = (face or s.texture, alpha, face is not None, shape_tag, role)
                groups.setdefault(key, []).append((pos, s.uvs, nrm, (b0, b1, b2), (w0, w1), s.triangles, color, mouth, ext))
        if not groups:
            return None, None
        batches = []
        self.last_extras = []
        for (src, alpha, is_face, tag, role), shapes in sorted(groups.items()):
            if is_face:
                tex = src  # already written by _face_texture
            else:
                tex = texture_pack_name(src) + ("_a" if alpha else "")
                self.models._convert_texture({"texture": tex, "source": src, "flags": 1 if alpha else 0})
            verts, idx, base, morphs = [], [], 0, []
            extras = {}
            for pos, uv, nrm, bones3, weights2, tris, color, mouth, ext in shapes:
                n = len(pos)
                if base + n > 0xFFFF:
                    break
                v = np.zeros(n, VERTEX_DTYPE)
                v["pos"] = np.stack([pos[:, 0], pos[:, 2], -pos[:, 1]], 1) / UNITS_PER_METER
                v["uv"] = np.nan_to_num(uv)
                if nrm is not None:
                    v["n"] = np.clip(np.round(np.stack([nrm[:, 0], nrm[:, 2], -nrm[:, 1]], 1) * 127), -127, 127)
                else:
                    v["n"] = (0, 127, 0)
                v["col"] = 255
                if color is not None:
                    v["col"][:, :3] = color
                v["bone"][:, 0], v["bone"][:, 1], v["bone2"] = bones3
                w0q = np.clip(np.round(weights2[0] * 255), 0, 255)
                v["w"][:, 0] = w0q
                v["w"][:, 1] = np.clip(np.round(weights2[1] * 255), 0, 255 - w0q)
                verts.append(v)
                idx.append(tris.astype(np.uint32).reshape(-1) + base)
                if mouth is not None:
                    d = np.stack([mouth[:, 0], mouth[:, 2], -mouth[:, 1]], 1) / UNITS_PER_METER / MORPH_UNIT_M
                    moving = np.where(np.abs(d).max(1) >= 0.5)[0]
                    rec = np.zeros(len(moving), [("v", "<u2"), ("d", "<i2", 3)])
                    rec["v"] = moving + base
                    rec["d"] = np.clip(np.round(d[moving]), -32767, 32767)
                    morphs.append(rec)
                for name, off in ext.items():  # (the chargen morphs and the thin build: the same records)
                    d = np.stack([off[:, 0], off[:, 2], -off[:, 1]], 1) / UNITS_PER_METER / MORPH_UNIT_M
                    moving = np.where(np.abs(d).max(1) >= 0.5)[0]
                    if len(moving):
                        rec = np.zeros(len(moving), MORPH_REC)
                        rec["v"] = moving + base
                        rec["d"] = np.clip(np.round(d[moving]), -32767, 32767)
                        extras.setdefault(name, []).append(rec)
                base += n
            # Hair, brows and lashes are alpha-tested cards seen from both sides
            batches.append((tex, alpha, np.concatenate(verts), np.concatenate(idx).astype("<u2"), tag,
                            np.concatenate(morphs) if morphs else None))
            self.last_extras.append((role, {k: np.concatenate(v) for k, v in extras.items()}))
            self.stats["triangles"] += sum(len(i) for i in idx) // 3
        if head is not None:
            parents, joints = parents + [head], joints + [to_engine(*hinge)]
        return (parents, joints, female), batches

    def export(self, pack, bounds, interiors=None):
        """Writes every unique NPC whose schedule touches the converted region (or its
        converted interiors: interiors maps their cell ids to indices)."""
        interiors = interiors or {}
        min_x, min_y, max_x, max_y = bounds
        cell_m = 4096 / UNITS_PER_METER

        def in_region(pos):
            cx, cy = math.floor(pos[0] / cell_m), math.floor(-pos[2] / cell_m)
            return min_x <= cx <= max_x and min_y <= cy <= max_y

        out_dir = os.path.join(pack, "actors")
        os.makedirs(out_dir, exist_ok=True)
        for old in os.listdir(out_dir):
            if os.path.isfile(os.path.join(out_dir, old)):  # (not gear/)
                os.remove(os.path.join(out_dir, old))
        facegen = set()
        for a in self.models.mesh_archives:
            facegen.update(a.list("meshes\\actors\\character\\facegendata\\facegeom"))
        names = []
        self.exported = []
        talk = Dialogue(self.lo, self.world.cells, self.world, pack)
        self.talk = talk  # (the quests' lines use it too: quests.py)
        for npc, gid in humanoid_npcs(self.lo, facegen):
            eid = npc.editor_id()
            if "dead" in eid.lower() or eid in FOES:
                continue
            entries, default = schedule(self.world, npc, gid, self.stats)
            spots = [t for _s, _e, t in entries] + ([default] if default else [])
            if eid in SCRIPTED_CAST:  # (out of sight, till the opening places them: quests.py)
                entries, default = [], OFFSTAGE
                spots = []
            elif not any((t[0] == OUTDOORS and in_region(t[1])) or (t[0] == INSIDE and t[4] in interiors)
                       for t in spots):
                continue  # never anywhere the player can go
            if default is None:  # placed somewhere we can't reach: stay inside the first building
                default = next((t for t in spots if t[0] == INSIDE), spots[0])
            skeleton, batches = self._batches(npc, gid)
            if batches is None:
                continue
            if eid in ARMED_CAST:  # (the headsman's axe in his hand: the opening's block)
                batches = batches + self._held_weapon(npc)
            for path, nodes in HELD_PROPS.get(eid, ()):  # (a scene's things in hand, shown by its script: Hadvar's list)
                import gear
                try:
                    nif = self.builder._nif("meshes\\" + path)
                except (KeyError, ValueError):
                    nif = None
                for node in nodes:
                    held = gear.rigid_batches(self, False, PART_PROP, nif, node) if nif is not None else []
                    if held:
                        batches = batches + [b + (None,) for b in held]
                        self.stats["held props"] += 1
                        break
                else:
                    self.stats[f"held prop unplaced: {path}"] += 1
            fname = eid.lower() + ".sknpc"
            write_sknpc(os.path.join(out_dir, fname), eid, skeleton, entries, default, batches, interiors)
            if self.items is not None:
                self.items.npc_pockets(npc, eid)
                self.items.npc_vendor(npc, eid)
            names.append(fname)
            self.exported.append((eid, npc))  # (their crime factions: crime.py)
            if eid in SCRIPTED_CAST:
                self.stats["NPCs (the opening's cast)"] += 1
                talk.export_name(npc, os.path.join(out_dir, eid.lower()))  # ("Talk to Elenwen", not her id)
                continue
            talk.export(npc, gid, [t[1] for t in spots if t[0] == OUTDOORS], os.path.join(out_dir, eid.lower()),
                        [t[4] for t in spots if t[0] == INSIDE])
            self.stats["NPCs"] += 1
            self.stats["schedule entries"] += len(entries)
            self.stats["NPCs with a routine outdoors"] += any(t[0] == OUTDOORS for _s, _e, t in entries)
        with open(os.path.join(out_dir, "actors.txt"), "w") as f:
            f.write("".join(n + "\n" for n in names))
        print("NPCs:", dict(self.stats))
        print("dialogue:", dict(talk.stats))


FIRST_PERSON_SKELETON = "meshes\\actors\\character\\_1stperson\\skeleton.nif"
FIRST_PERSON_HANDS = {False: "meshes\\actors\\character\\character assets\\1stpersonmalehands_1.nif",
                      True: "meshes\\actors\\character\\character assets\\1stpersonfemalehands_1.nif"}
PLAYER_TORCH = ("Torch01", "weapons\\torch\\torch.nif")  # (LIGH editor id, model)
TORCH_NODE = "SHIELD"  # held where Skyrim holds torches and shields (a node on the left hand)


def _torch_batches(exporter, female, tag, skeleton_path=None):
    """The torch's handle (its textured shape; the flame is the engine's) as rigid
    batches on the left hand, in the bind pose, as items' batches are."""
    world, kept, _parents, _joints, kept_for = exporter._skeleton(female, skeleton_path)
    if TORCH_NODE not in world:
        return []
    try:
        nif = exporter.builder._nif("meshes\\" + PLAYER_TORCH[1])
    except (KeyError, ValueError):
        return []
    if nif is None:
        return []
    hand = kept_for(TORCH_NODE)
    m = world[TORCH_NODE]
    out = []
    for s in nif.shapes():
        if not s.texture:
            continue  # (the glow and heat-haze effect meshes)
        pos = s.positions.astype(np.float64) @ m[:3, :3].T + m[:3, 3]
        v = np.zeros(len(pos), VERTEX_DTYPE)
        v["pos"] = np.stack([pos[:, 0], pos[:, 2], -pos[:, 1]], 1) / UNITS_PER_METER
        v["uv"] = np.nan_to_num(s.uvs)
        if s.normals is not None:
            nrm = s.normals.astype(np.float64) @ m[:3, :3].T
            v["n"] = np.clip(np.round(np.stack([nrm[:, 0], nrm[:, 2], -nrm[:, 1]], 1) * 127), -127, 127)
        else:
            v["n"] = (0, 127, 0)
        v["col"] = 255
        v["bone"][:, 0] = v["bone"][:, 1] = v["bone2"] = hand
        v["w"][:, 0] = 255
        tex = texture_pack_name(s.texture) + ("_a" if s.alpha_test else "")
        exporter.models._convert_texture({"texture": tex, "source": s.texture, "flags": 1 if s.alpha_test else 0})
        out.append((tex, s.alpha_test, v, s.triangles.astype("<u2").reshape(-1), tag))
    return out


def export_player(exporter, pack, strings):
    """The player's characters (races.py): for each playable race and face, the face's
    FaceGen head on the race's body with PLAYER_ITEMS to wear (actors/player/<race>_<n>.sknpc
    and its first-person arms <race>_<n>_1st.sknpc); the gear for each body family
    (gear.py: actors/gear/<family>/, items/gear_<family>.txt); races.txt."""
    import races
    lo = exporter.lo
    npcs = {r.editor_id(): (r, lo.global_id(p, r.form_id)) for p in lo.plugins for r in p.records_of_type("NPC_")}
    armor = {r.editor_id(): r for p in lo.plugins for r in p.records_of_type("ARMO")}
    out_dir = os.path.join(pack, "actors", "player")
    os.makedirs(out_dir, exist_ok=True)
    for old in os.listdir(out_dir):
        os.remove(os.path.join(out_dir, old))
    # Bound Sword (Conjuration): the weapon the spell puts in your hand (gear for it)
    if exporter.items is not None:
        for p in lo.plugins:
            for r in p.records_of_type("WEAP"):
                if r.editor_id() in BOUND_WEAPONS:
                    exporter.items.item_for(lo.global_id(p, r.form_id), r)
            for r in p.records_of_type("AMMO"):  # (the Bound Bow's arrows)
                if (r.editor_id() or "").lower() == "boundarrow":
                    exporter.items.item_for(lo.global_id(p, r.form_id), r)
    faces, families_done = {}, set()
    creator_donors = {}  # {(race, female): (npc, gid, race id, family)}: someone of each, for the creator's parts
    # Women of each race too (the race menu's faces: their heads and hair, on a woman's body
    # with a woman's armour - its own gear family, <family>_f): Skyrim's named women with
    # FaceGen heads, PLAYER_WOMEN of each race
    from actors import humanoid_npcs
    facegen = set()
    for a in exporter.models.mesh_archives:
        facegen.update(a.list("meshes\\actors\\character\\facegendata\\facegeom"))
    women = {}
    for rec, g in humanoid_npcs(lo, facegen):
        acbs, rn, eid = rec.sub("ACBS"), rec.sub("RNAM"), rec.editor_id().lower()
        if not acbs or not rn or not rec.sub("FULL") or any(w in eid for w in PLAYER_WOMEN_SKIP):
            continue
        r = lo.record(lo.global_id(rec.plugin, _u32(rn)), types=("RACE",))
        if r is not None:
            women.setdefault((r.editor_id(), bool(_u32(acbs) & 1)), []).append((rec.editor_id(), rec, g))
    for race_eid, family, donors in races.PLAYABLE:
        men = [(d,) + npcs[d] for d in donors if d in npcs]
        men += [m for m in sorted(women.get((race_eid, False), []), key=lambda w: w[0])
                if m[0] not in donors][:PLAYER_MEN - len(men)]
        cast = men + sorted(women.get((race_eid, True), []), key=lambda w: w[0])[:PLAYER_WOMEN]
        for n, (donor, npc, gid) in enumerate(cast):
            stem = f"{race_eid[:-4].lower()}_{n}"
            res = _export_player_variant(exporter, npc, gid, armor, strings, out_dir, stem)
            if res is None:
                continue
            female, race_id, wardrobe, start = res
            creator_donors.setdefault((race_eid, female), (npc, gid, race_id, family + "_f" if female else family))
            face = npc.sub("FULL")
            face = strings.get(npc.plugin, _u32(face), "strings") if face and len(face) == 4 else donor
            tone = (255, 255, 255) if family in ("argonian", "khajiit") else (skin_tone_color(npc) or (255, 255, 255))
            fam = family + "_f" if female else family
            faces.setdefault(race_eid, []).append(
                (stem, f"{'Female: ' if female else 'Male: '}{face}\t{tone[0]} {tone[1]} {tone[2]}\t{fam}"))
            if fam not in families_done and exporter.items is not None:
                families_done.add(fam)
                family_was, family = family, fam
                if not os.path.exists(os.path.join(pack, "items", "start.txt")) or family == "human":
                    os.makedirs(os.path.join(pack, "items"), exist_ok=True)
                    with open(os.path.join(pack, "items", "start.txt"), "w") as f:
                        f.write("".join(f"{form:08x} {count} {int(worn)}\n" for form, count, worn in start))
                import gear
                exporter.skin_marker = True  # (the gear's skin takes the wearer's tone in the engine)
                gear.export(exporter, exporter.items, pack, npc, gid, female, race_id, wardrobe, family)
                exporter.skin_marker = False
                family = family_was
    # The character creator: each race's bald base head ("Custom" faces, after the presets) and
    # Skyrim's parts to put on it (chargen.py: hair, beards, eyes, brows, scars)
    import chargen
    heads = chargen.export(exporter, pack, creator_donors)
    for race_eid, family, _donors in races.PLAYABLE:
        for female in (False, True):
            if (race_eid, female) not in heads or (race_eid, female) not in creator_donors:
                continue
            npc, gid, _race_id, _fam = creator_donors[(race_eid, female)]
            stem = f"{race_eid[:-4].lower()}_c{'f' if female else 'm'}"
            exporter.race_morphs = exporter.head_parts.race_morphs(exporter.find, exporter.models.mesh_archives, race_eid,
                                                                    heads[(race_eid, female)])
            exporter.morph_sources = exporter.head_parts.chargen_morphs(exporter.find, exporter.models.mesh_archives,
                                                                        heads[(race_eid, female)])
            res = _export_player_variant(exporter, npc, gid, armor, strings, out_dir, stem, head=heads[(race_eid, female)])
            exporter.race_morphs, exporter.morph_sources = {}, {}
            if res is None:
                continue
            tone = (255, 255, 255) if family in ("argonian", "khajiit") else (skin_tone_color(npc) or (255, 255, 255))
            fam = family + "_f" if female else family
            faces.setdefault(race_eid, []).append(
                (stem, f"{'Female' if female else 'Male'}: Custom\t{tone[0]} {tone[1]} {tone[2]}\t{fam}"))
    races.export(lo, strings, pack, faces)


def _face_tint(builder, facegen_path):
    """The face tint texture a FaceGen head's shader names (None without one)."""
    nif = builder._nif(facegen_path)
    for sh in skinned_shapes(nif) if nif is not None else []:
        shader_type, textures, _ = nif.lighting_shader(sh.shader) or (0, [], None)
        if shader_type == SHADER_FACEGEN and len(textures) > FACEGEN_TINT_SLOT and textures[FACEGEN_TINT_SLOT]:
            return textures[FACEGEN_TINT_SLOT]
    return None


def _export_player_variant(exporter, npc, gid, armor, strings, out_dir, stem, head=None):
    """One playable character: npc's head on their race's body, the wardrobe and the torch;
    third person and the first-person arms. Returns (female, race, wardrobe {form: index},
    starting inventory [(form, count, worn)]) or None."""
    lo, b = exporter.lo, exporter.builder
    parts = b.parts(npc, gid)
    if parts is None:
        print("player: can't assemble", npc.editor_id())
        return None
    female, race_id, npc_parts = parts
    race = lo.record(race_id, types=("RACE",))
    is_beast = bool(race and (race.editor_id() or "").lower().startswith(("argonian", "khajiit")))
    skin = lo.record(lo.global_id(race.plugin, _u32(race.sub("WNAM"))), types=("ARMO",)) if race and race.sub("WNAM") else None
    # the head: theirs (FaceGen: hair, eyes and all), or a bald base head for the character creator's parts
    plan = [(p, False, 0) for p in head] if head else [(npc_parts[0][0], False, 0)]
    exporter.custom_face = None if is_beast else ((f"custom_{stem}", _face_tint(b, npc_parts[0][0])) if head else None)
    exporter.body_tint = None if is_beast else (stem, _face_tint(b, npc_parts[0][0]))
    # The character creator's (a bald base head): its look the engine's - the skin's tone and the
    # face's tint layers from the menu's sliders, its chargen morphs and Skyrim's thin build carried
    look = bool(head)
    if look:
        exporter.body_tint, exporter.chargen_mode, exporter.want_weight = None, True, True
    for slots, path in (b._armor_addons(skin, race_id, female) if skin else []):
        if slots & SKIN_SLOTS:
            plan.append((path, False, PART_SKIN | (slots & SKIN_SLOTS)))
    items, wardrobe, start, worn_eids = [], {}, [], []  # (the wardrobe's items as inventory items: form -> index)
    for eid, equipped in PLAYER_ITEMS:
        rec = armor.get(eid)
        if rec is None:
            continue
        addons = b._armor_addons(rec, race_id, female)
        if not addons:
            continue
        full = rec.sub("FULL")
        name = strings.get(rec.plugin, _u32(full), "strings") if full and len(full) == 4 else eid
        plan += [(path, False, len(items) + 1) for _slots, path in addons]
        form = lo.global_id(rec.plugin, rec.form_id)
        wardrobe[form] = len(items)
        start.append((form, 1, equipped))
        if exporter.items is not None:
            exporter.items.item_for(form, rec)
        items.append((name, b._armor_slots(rec), equipped))
        worn_eids.append(eid)
    skeleton, batches = exporter._batches(npc, gid, (female, race_id, plan))
    extras = list(exporter.last_extras) if look and batches else None
    exporter.custom_face = None
    if batches is None:
        print("player: not assembled", npc.editor_id())
        exporter.chargen_mode = exporter.want_weight = False
        return None
    # The torch (held in the left hand; the engine adds its flame and light)
    torch = _torch_batches(exporter, female, len(items) + 1)
    if torch:
        light = next((r for p in lo.plugins for r in p.records_of_type("LIGH") if r.editor_id() == PLAYER_TORCH[0]), None)
        full = light.sub("FULL") if light is not None else None
        name = strings.get(light.plugin, _u32(full), "strings") if full and len(full) == 4 else "Torch"
        batches += torch
        if light is not None:
            form = lo.global_id(light.plugin, light.form_id)
            wardrobe[form] = len(items)
            start.append((form, 1, False))
            if exporter.items is not None:
                exporter.items.item_for(form, light)
        items.append((name, 0, False))
    for eid, count in PLAYER_EXTRA_ITEMS:  # (carried, not worn: the gear map has them)
        rec = armor.get(eid)
        if rec is not None and exporter.items is not None:
            form = lo.global_id(rec.plugin, rec.form_id)
            exporter.items.item_for(form, rec)
            start.append((form, count, False))
    home = (0, (0.0, 0.0, 0.0), 0.0, 0.0, None, (0.0, 0.0, 0.0), 0.0, 0.0, ACT_NONE, 0, 0)
    if extras is not None:
        extras += [(0, {})] * (len(batches) - len(extras))  # (the torch's)
    write_sknpc(os.path.join(out_dir, stem + ".sknpc"), "Player", skeleton, [], home, batches, {}, items, extras)

    # First person: the arms in front of the camera (Skyrim's first-person skeleton, the
    # race's first-person hands and the first-person models of the same items, so the
    # wardrobe lines up item for item)
    hands = [p for sl, p in (b._armor_addons(skin, race_id, female, first_person=True) if skin else [])
             if sl & SLOT_HANDS and b._nif(p) is not None] or [FIRST_PERSON_HANDS[female]]
    plan1 = [(p, False, PART_SKIN | SLOT_HANDS) for p in hands]
    for i, eid in enumerate(worn_eids):
        # Its first-person models, or, where an item has none (Iron Gauntlets), the normal
        # ones: they're skinned to the same bones (else wearing it left the hands empty)
        first = [p for _s, p in b._armor_addons(armor[eid], race_id, female, first_person=True) if b._nif(p) is not None]
        if not first:
            first = [p for _s, p in b._armor_addons(armor[eid], race_id, female) if b._nif(p) is not None]
        plan1 += [(path, False, i + 1) for path in first]
    morphs_was, exporter.morph_sources = exporter.morph_sources, {}  # (the arms: no face)
    skeleton1, batches1 = exporter._batches(npc, gid, (female, race_id, plan1), skeleton_path=FIRST_PERSON_SKELETON)
    exporter.morph_sources = morphs_was
    extras1 = list(exporter.last_extras) if look and batches1 else None
    exporter.body_tint = None
    exporter.chargen_mode = exporter.want_weight = False
    if batches1 is None:
        print("player: no first-person arms for", npc.editor_id())
        return None
    if torch:
        batches1 += _torch_batches(exporter, female, len(items), skeleton_path=FIRST_PERSON_SKELETON)
    if extras1 is not None:
        extras1 += [(0, {})] * (len(batches1) - len(extras1))
    write_sknpc(os.path.join(out_dir, stem + "_1st.sknpc"), "Player1st", skeleton1, [], home, batches1, {}, items, extras1)
    print(f"player {stem} ({npc.editor_id()}): {len(batches)} batches, first person {len(batches1)}")
    return female, race_id, wardrobe, start


def skin_tone_color(npc):
    """Vertex colour (r, g, b 0..255) for an NPC's bare skin from their skin tone (QNAM):
    the shared body texture is a light skin, darkened toward the tone (a Nord's leaves it
    about as it is; a Redguard's makes it dark brown, a Dunmer's ash grey)."""
    q = npc.sub("QNAM") if npc is not None else None
    if not q or len(q) < 12:
        return None
    tone = np.array(struct.unpack_from("<3f", q), np.float64)
    return tuple(int(v) for v in np.clip((0.35 + 0.65 * tone / SKIN_TONE_LIGHT) * 255.0, 70, 255))


# Characters for the Vita: Skyrim's are PC detail (a crowd in Dragonsreach was 120k
# triangles a frame); each part keeps about this share of its triangles (simplify.py) - a
# talking face more, its mouth's morph remapped to the vertices kept
CHARACTER_DETAIL = 0.45
FACE_DETAIL = 0.65


def _lighter(verts, idx, mouth, extras=None):
    from simplify import simplify
    used, tri = simplify(verts["pos"], idx.reshape(-1, 3), FACE_DETAIL if mouth is not None else CHARACTER_DETAIL,
                         min_triangles=48)
    new_of = np.full(len(verts), -1, np.int64)
    new_of[used] = np.arange(len(used))

    def remap(recs):
        keep = new_of[recs["v"].astype(np.int64)] >= 0
        out = recs[keep].copy()
        out["v"] = new_of[out["v"].astype(np.int64)].astype(np.uint16)
        return out
    if mouth is not None:
        mouth = remap(mouth)
    if extras:
        extras = {k: remap(v) for k, v in extras.items()}
    return np.ascontiguousarray(verts[used]), tri.reshape(-1).astype("<u2"), mouth, extras


def _batch_bytes(batches, extras=None, extras_out=None):
    """extras: per batch (role, {morph name: records}) (Exporter.last_extras), carried through the
    simplifying into extras_out (a list) for the .skmorph beside the file."""
    data = bytearray()
    for k, (tex, alpha, verts, idx, tag, *mouth) in enumerate(batches):
        mouth = mouth[0] if mouth else None
        role, ext = extras[k] if extras and k < len(extras) else (0, {})
        # (tiny parts as they are; and hair, layered alpha cards that edge collapse crumples; and the
        # character creator's face - its morphs shape it close up, every vertex of it)
        shaped = any(name != "weight" for name in ext)
        if len(idx) >= 3 * 96 and not alpha and not shaped:
            verts, idx, mouth, ext = _lighter(verts, idx, mouth, ext)
        if extras_out is not None:
            extras_out.append((role, ext))
        name = tex.encode("ascii")
        data += struct.pack("<BBH", int(alpha), len(name), tag) + name + b"\0" * (-len(name) % 4)
        data += struct.pack("<II", len(verts), len(idx)) + verts.tobytes()
        raw = idx.tobytes()
        data += raw + b"\0" * (-len(raw) % 4)
        data += struct.pack("<I", 0 if mouth is None else len(mouth))
        if mouth is not None:
            data += mouth.tobytes()
    return bytes(data)


def write_skmorph(path, lists):
    """The character creator's per-vertex data for a model's batch lists (lists: [[(role, {name:
    records})] per batch] - third person, then a gear's first person): its batches' roles and its
    morphs. Nothing is written (an old file removed) if it has neither.

      "SKMO" u16 version 1, u16 role count; roles: u8 list, u8 batch, u8 role, u8 0
      u32 name count; names: char[24]
      u32 record count; records (sorted by list, batch, name, vertex): u8 list, u8 batch,
        u16 name, u16 vertex, i16 offset[3] (1/10000 m, model space)"""
    roles, names, recs = [], [], []
    for li, batches in enumerate(lists):
        for bi, (role, ext) in enumerate(batches):
            if role:
                roles.append((li, bi, role))
            for name in sorted(ext):
                if name not in names:
                    names.append(name)
                r = ext[name]
                if len(r):
                    recs.append((li, bi, names.index(name), r))
    if os.path.exists(path):
        os.remove(path)
    if not roles and not recs:
        return 0
    data = bytearray(struct.pack("<4sHH", b"SKMO", 1, len(roles)))
    for li, bi, role in roles:
        data += struct.pack("<BBBB", li, bi, role, 0)
    data += struct.pack("<I", len(names)) + b"".join(n.encode("ascii")[:23].ljust(24, b"\0") for n in names)
    total = sum(len(r) for *_x, r in recs)
    data += struct.pack("<I", total)
    out = np.zeros(total, [("l", "u1"), ("b", "u1"), ("n", "<u2"), ("v", "<u2"), ("d", "<i2", 3)])
    at = 0
    for li, bi, ni, r in sorted(recs, key=lambda x: (x[0], x[1], x[2])):
        r = np.sort(r, order="v")
        out["l"][at:at + len(r)], out["b"][at:at + len(r)], out["n"][at:at + len(r)] = li, bi, ni
        out["v"][at:at + len(r)], out["d"][at:at + len(r)] = r["v"], r["d"]
        at += len(r)
    data += out.tobytes()
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


def write_sknpc(path, eid, skeleton, entries, default, batches, interiors, items=(), extras=None):
    """items: [(name, body slots, equipped)] (the player's wardrobe); extras: the batches' look
    (Exporter.last_extras) for a .skmorph beside it (the player's)."""
    parents, joints, female = skeleton
    data = bytearray(struct.pack("<4sHH32s", b"SKNP", SKNP_VERSION, int(female), eid.encode("ascii", "replace")[:31]))
    data += struct.pack("<I", len(parents))
    for p, j in zip(parents, joints):
        data += struct.pack("<i3f", p, *j)
    data += struct.pack("<I", len(entries)) + b"".join(_entry_bytes(*e, interiors) for e in entries)
    data += _entry_bytes(0, DAY_MINUTES, default, interiors)
    final = []
    data += struct.pack("<I", len(batches)) + _batch_bytes(batches, extras, final)
    if extras is not None:
        write_skmorph(path[:-len(".sknpc")] + ".skmorph", [final])
    data += struct.pack("<I", len(items))
    for name, slots, equipped in items:
        n = name.encode("cp1252", "replace")[:255]
        rec = struct.pack("<IBB", slots, int(equipped), len(n)) + n
        data += rec + b"\0" * (-len(rec) % 4)
    with open(path, "wb") as f:
        f.write(data)


VERTEX_DTYPE = np.dtype([("pos", "<f4", 3), ("uv", "<f4", 2), ("n", "i1", 3), ("bone2", "u1"), ("col", "u1", 4),
                         ("bone", "u1", 2), ("w", "u1", 2)])
assert VERTEX_DTYPE.itemsize == 32
