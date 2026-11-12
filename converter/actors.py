"""Unique NPCs as posed character models (Milestone III, stage 1).

For each unique humanoid NPC (no template, with a FaceGen head) this assembles a
character from Skyrim's parts and finds where they spend the day:

  parts     FaceGen head (head, eyes, mouth, brows, hair, beard), the race's skin
            (body, hands, feet) and the default outfit's armour; armour replaces the
            skin pieces whose body slots it covers, helmets hide the hair
  pose      Skyrim SE's bind pose (arms relaxed at the sides) on the race's skeleton
  location  the AI package running at midday with a "near reference" location in the
            converted region (Alvor at his forge, Sven by the inn...), or the actor's
            own placement if they are placed outdoors

The result is an ordinary model (posed shapes in NIF root space, feet at the origin)
plus a placement transform, so characters go through the object pipeline unchanged.
"""
import struct

import numpy as np

from nif import NIF, Shape
from skin import Skeleton, pose_shape, skinned_shapes

SLOT_HEAD, SLOT_HAIR, SLOT_BODY, SLOT_HANDS, SLOT_FEET = 1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 7
SKIN_SLOTS = SLOT_BODY | SLOT_HANDS | SLOT_FEET
LVL_USE_ALL = 0x04  # (LVLF: every entry, not one)
MIDDAY = 12
SKELETONS = {False: "meshes\\actors\\character\\character assets\\skeleton.nif",
             True: "meshes\\actors\\character\\character assets female\\skeleton_female.nif"}


# Worn besides their outfit, as Skyrim's quests put it on them: the opening's Ulfric is gagged
# (MQ101's alias gives him ArmorGag, Clothes\Gag\Gag.nif - "they gagged him", Ralof's own words)
WORN_TOO = {"Ulfric": (0xF9670,)}


def _u32(data, off=0):
    return struct.unpack_from("<I", data, off)[0]


class ActorBuilder:
    def __init__(self, load_order, mesh_archives, find):
        self.worn_props = set()  # (WORN_TOO's models: a quest's, worn only while its scene says - Ulfric's gag)
        self.lo = load_order
        self.archives = mesh_archives
        self.find = find
        self._skeletons = {}
        self._nifs = {}
        self.stats = {}

    def _count(self, key):
        self.stats[key] = self.stats.get(key, 0) + 1

    # --- records ---------------------------------------------------------------------

    def _rec(self, owner, local_id, types):
        g = self.lo.global_id(owner.plugin, local_id)
        return (self.lo.record(g, types=types), g) if g is not None else (None, None)

    def _armors(self, owner, local_id, depth=0):
        """The ARMOs an outfit entry gives: an ARMO, or a leveled item list's first - or all its
        entries', for a list flagged "use all" (a set: Hadvar's Imperial armour came as boots
        alone)."""
        rec, _g = self._rec(owner, local_id, ("ARMO", "LVLI"))
        if rec is None or depth > 4:
            return []
        if rec.type == "ARMO":
            return [rec]
        lvlf = rec.sub("LVLF")
        use_all = bool(lvlf and lvlf[0] & LVL_USE_ALL)
        out = []
        for t, v in rec.subrecords():
            if t == "LVLO" and len(v) >= 8:
                found = self._armors(rec, _u32(v, 4), depth + 1)
                if found:
                    out += found
                    if not use_all:
                        break
        return out

    def _arma_skin_texture(self, arma, female):
        """The skin diffuse texture path from an ARMA's NAM1 (female) or NAM0 (male) TXST record, or None."""
        nam = arma.sub("NAM1" if female else "NAM0") or arma.sub("NAM0")
        if not nam or len(nam) < 4:
            return None
        txst, _g = self._rec(arma, _u32(nam), ("TXST",))
        tx00 = txst.sub("TX00") if txst is not None else None
        if not tx00 or len(tx00) < 4:
            return None
        p = tx00.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\").lstrip("\\")
        return p if p.startswith("textures\\") else "textures\\" + p

    def race_skin_textures(self, race_id, female):
        """{slot_bit: diffuse_texture_path} from the race's WNAM (SkinNaked / SkinNakedBeast) ARMA records."""
        key = (race_id, bool(female))
        if not hasattr(self, "_race_skins"):
            self._race_skins = {}
        if key in self._race_skins:
            return self._race_skins[key]
        out = {}
        race = self.lo.record(race_id, types=("RACE",)) if race_id is not None else None
        wnam = race.sub("WNAM") if race is not None else None
        skin = self._rec(race, _u32(wnam), ("ARMO",))[0] if wnam and len(wnam) >= 4 else None
        if skin is not None:
            for t, v in skin.subrecords():
                if t != "MODL" or len(v) != 4:
                    continue
                arma, _g = self._rec(skin, _u32(v), ("ARMA",))
                if arma is None:
                    continue
                races = [self.lo.global_id(arma.plugin, _u32(sv)) for st, sv in arma.subrecords()
                         if st in ("RNAM", "MODL") and len(sv) == 4]
                default_race = self.lo.global_id(arma.plugin, 0x19)
                if race_id not in races and default_race not in races:
                    continue
                bod = arma.sub("BOD2") or arma.sub("BODT")
                tex = self._arma_skin_texture(arma, female)
                if bod and tex:
                    slots = _u32(bod)
                    for bit in (SLOT_BODY, SLOT_HANDS, SLOT_FEET):
                        if slots & bit:
                            out[bit] = tex
        self._race_skins[key] = out
        return out

    def _armor_addons(self, armo, race_id, female, first_person=False):
        """(slot mask, biped model path) of an ARMO's addons that fit the race (first_person:
        their first-person models, the arms and sleeves seen in front of the camera)."""
        out = []
        for t, v in armo.subrecords():
            if t != "MODL" or len(v) != 4:
                continue
            arma, _g = self._rec(armo, _u32(v), ("ARMA",))
            if arma is None:
                continue
            races = []
            for st, sv in arma.subrecords():
                if st in ("RNAM", "MODL") and len(sv) == 4:
                    races.append(self.lo.global_id(arma.plugin, _u32(sv)))
            default_race = self.lo.global_id(arma.plugin, 0x19)  # DefaultRace
            if race_id not in races and default_race not in races:
                continue
            if first_person:
                path = arma.sub("MOD5" if female else "MOD4") or arma.sub("MOD4")
            else:
                path = arma.sub("MOD3" if female else "MOD2") or arma.sub("MOD2")
            bod = arma.sub("BOD2") or arma.sub("BODT")
            if not path or not bod:
                continue
            out.append((_u32(bod), "meshes\\" + path.rstrip(b"\0").decode("cp1252").lower().replace("/", "\\")))
        return out

    def _armor_slots(self, armo):
        bod = armo.sub("BOD2") or armo.sub("BODT")
        return _u32(bod) if bod else 0

    # --- assembly --------------------------------------------------------------------

    def parts(self, npc, gid):
        """[(nif path, hide_hair)] for an NPC, or None if it can't be assembled."""
        acbs = npc.sub("ACBS")
        female = bool(acbs and _u32(acbs) & 1)
        rnam = npc.sub("RNAM")
        if not rnam:
            return None
        race, race_id = self._rec(npc, _u32(rnam), ("RACE",))
        if race is None:
            return None
        plugin = self.lo.plugins[gid >> 24].name.lower()
        head = f"meshes\\actors\\character\\facegendata\\facegeom\\{plugin}\\{gid & 0xFFFFFF:08x}.nif"

        worn, covered = [], 0
        doft = npc.sub("DOFT")
        outfit = self._rec(npc, _u32(doft), ("OTFT",))[0] if doft else None
        if outfit is not None:
            inam = outfit.sub("INAM") or b""
            for armo in (a for i in range(len(inam) // 4) for a in self._armors(outfit, _u32(inam, i * 4))):
                slots = self._armor_slots(armo)
                if slots & covered:
                    continue  # already wearing something there
                addons = self._armor_addons(armo, race_id, female)
                if addons:
                    worn.extend(path for _s, path in addons)
                    covered |= slots

        for local in WORN_TOO.get(npc.editor_id(), ()):  # (the quest's: Ulfric's gag)
            for armo in self._armors(npc, local):
                addons = self._armor_addons(armo, race_id, female)
                if addons and not self._armor_slots(armo) & covered:
                    worn.extend(path for _s, path in addons)
                    self.worn_props.update(path for _s, path in addons)  # (shown while the scene says: npcs.py PART_PROP)
                    covered |= self._armor_slots(armo)

        skin_ref = npc.sub("WNAM") or race.sub("WNAM")
        skin = self._rec(npc if npc.sub("WNAM") else race, _u32(skin_ref), ("ARMO",))[0] if skin_ref else None
        body = []
        if skin is not None:
            for slots, path in self._armor_addons(skin, race_id, female):
                if slots & SKIN_SLOTS and not slots & covered:
                    body.append(path)
        hide_hair = bool(covered & (SLOT_HAIR | SLOT_HEAD))
        return female, race_id, [(head, hide_hair)] + [(p, False) for p in body + worn]

    def _nif(self, path):
        if path not in self._nifs:
            try:
                self._nifs[path] = NIF(self.find(self.archives, path))
            except (KeyError, ValueError, IndexError, struct.error):
                self._nifs[path] = None
        return self._nifs[path]

    def _bones(self, female):
        if female not in self._skeletons:
            nif = self._nif(SKELETONS[female])
            self._skeletons[female] = Skeleton(nif).world() if nif else None
        return self._skeletons[female]

    def shapes(self, npc, gid):
        """Posed character shapes (nif.Shape, NIF root space), or None."""
        res = self.parts(npc, gid)
        if res is None:
            self._count("no race")
            return None
        female, race_id, parts = res
        bones = self._bones(female)
        if bones is None:
            return None
        race_skins = self.race_skin_textures(race_id, female)
        out = []
        for i, (path, hide_hair) in enumerate(parts):
            nif = self._nif(path)
            if nif is None:
                self._count("missing part" if i else "missing FaceGen head")
                if i == 0:
                    return None
                continue
            for s in skinned_shapes(nif):
                if hide_hair and s.name.lower().startswith("hair"):
                    continue
                if not s.texture:
                    continue
                tex = s.texture
                shader_type, _, _ = nif.lighting_shader(s.shader) or (0, [], None)
                if shader_type == 5 or "handsargonian" in tex.lower():
                    is_hand = "hand" in tex.lower() or "hand" in path.lower()
                    tex = race_skins.get(SLOT_HANDS if is_hand else SLOT_BODY, tex)
                pos, nrm = pose_shape(s, bones)
                if not np.isfinite(pos).all():
                    continue
                out.append(Shape(s.name, pos, s.uvs, nrm, None, s.triangles.astype(np.uint16), tex, s.alpha_test))
        return out or None

    # --- where they spend the day ------------------------------------------------------

    def day_reference(self, npc):
        """Global form id of the reference the NPC's midday package keeps them near."""
        best = None
        for t, v in npc.subrecords():
            if t != "PKID":
                continue
            pack, _g = self._rec(npc, _u32(v), ("PACK",))
            if pack is None:
                continue
            psdt = pack.sub("PSDT")
            if not psdt:
                continue
            hour, duration = struct.unpack_from("<b", psdt, 3)[0], struct.unpack_from("<i", psdt, 8)[0]
            if hour < 0 or duration <= 0:
                continue
            end = hour + duration / 60.0
            if not (hour <= MIDDAY < end or hour <= MIDDAY + 24 < end):
                continue
            for st, sv in pack.subrecords():
                if st == "PLDT" and len(sv) >= 8 and struct.unpack_from("<i", sv)[0] == 0:
                    ref = self.lo.global_id(pack.plugin, _u32(sv, 4))
                    if ref is not None and (best is None or duration > best[0]):
                        best = (duration, ref)
                    break
        return best[1] if best else None


def humanoid_npcs(load_order, facegen_names):
    """(record, global id) of unique NPCs with a FaceGen head, winning overrides only."""
    seen = {}
    for p in load_order.plugins:
        for rec in p.records_of_type("NPC_"):
            g = load_order.global_id(p, rec.form_id)
            if g is not None:
                seen[g] = rec
    out = []
    for g, rec in seen.items():
        if rec.sub("TPLT") or "corpse" in rec.editor_id().lower():
            continue
        plugin = load_order.plugins[g >> 24].name.lower()
        if f"meshes\\actors\\character\\facegendata\\facegeom\\{plugin}\\{g & 0xFFFFFF:08x}.nif" in facegen_names:
            out.append((rec, g))
    return out
