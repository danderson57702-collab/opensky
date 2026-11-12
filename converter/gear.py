"""Gear the player can wear and wield: every armour and weapon that can turn up in the region
(items.Database), fitted to the player's character, for equipping from the inventory.

  armour   the armour's addons (Skyrim's ARMA: the body parts it covers) skinned to the
           player's skeleton, third person and first person (its first-person models, or
           its normal ones where it has none), like the player's own clothes
  weapons  the weapon's model as rigid parts on Skyrim's weapon nodes: in the right hand
           (WEAPON; bows in the left, SHIELD), and sheathed by its kind (WeaponSword at the hip, WeaponBack for
           two-handed ones, WeaponBow...); the scabbard stays sheathed while it's drawn

Many items share a model (enchanted variants), so files are per model set.

  PACK_DIR/actors/gear/<family>/<name>.skgear   char[4] "SKGR", u16 version, u16 kind (0 armour,
      1 weapon), u32 body slots (<name>l.skgear: a one-handed weapon's left-hand twin, mirrored), then two batch lists (third person, first person): u32
      count, batches as in .sknpc (npcs.write_sknpc) with the batch tag: armour 1; weapons
      1 in the hand, 2 sheathed (the weapon in its scabbard), 3 the scabbard alone; plus
      0x4000 on bare skin, which takes the wearer's skin tone (npcs.SKIN_TONE_TAG); 0x2000 on a
      bound weapon's (drawn as ghostly light)
  PACK_DIR/items/gear_<family>.txt   per wearable item: form id (hex), what it is ("gear <name>",
      or "wardrobe <index>": one of the player's own clothes in player.sknpc), kind
      (armour, shield, weapon, quiver: arrows' quiver on the back), body slots, weapon kind
      (Skyrim's animation type; -1)
"""
import os
import struct

import numpy as np

from npcs import (FIRST_PERSON_SKELETON, UNITS_PER_METER, VERTEX_DTYPE, _batch_bytes, texture_pack_name, write_skmorph)

SKGR_VERSION = 1
SHIELD_SLOT = 1 << 9  # body slot 39
ARM_SLOTS = 0x8 | 0x10  # body slots 33 hands, 34 forearms
# Skyrim's weapon animation types -> where it's sheathed
SHEATH = {1: "WeaponSword", 2: "WeaponDagger", 3: "WeaponAxe", 4: "WeaponMace", 5: "WeaponBack", 6: "WeaponBack",
          7: "WeaponBow", 8: "WeaponBack", 9: "WeaponBow"}
TAG_HAND, TAG_SHEATHED, TAG_SCABBARD = 1, 2, 3
BOUND_TAG = 0x2000  # (batches drawn as a bound weapon's ghostly light: additive, blue)
BOUND_STAND_IN = {1: "meshes\\weapons\\glass\\glasssword.nif", 2: "meshes\\weapons\\glass\\glassdagger.nif",
                  6: "meshes\\weapons\\glass\\glassbattleaxe.nif", 7: "meshes\\weapons\\glass\\glassbow.nif"}  # (a bound weapon's look: a glass sword)
# Skyrim's back sheath sits inside the torso (its node is 8 units behind the spine; the back's
# surface 11-15, and armour on top): two-handed weapons went through the body. Moved back
# (-Y: behind the character) clear of it.
SHEATH_OFFSET = {"WeaponBack": (0.0, -10.0, 0.0)}
# Rigid models' skeleton nodes (Skyrim's "Prn" for them): a shield on the left forearm, the
# quiver at the back (the arrows' model: its ArrowQuiver part; the lone arrow is the dropped look)
SHIELD_NODE, QUIVER_NODE, QUIVER_PART = "SHIELD", "QUIVER", "ArrowQuiver"
ONE_HANDED = (1, 2, 3, 4)  # swords, daggers, war axes, maces: held in either hand
BOWS = (7,)  # held in the left hand (Skyrim's SHIELD node); everything else in the right (WEAPON)


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _mirror_name(name):
    """A bone's other-side twin ("NPC R Hand [RHnd]" <-> "NPC L Hand [LHnd]")."""
    out = name
    for a, b in ((" R ", " L "), ("[R", "[L"), ("Right", "Left")):
        out = out.replace(a, "\0").replace(b, a).replace("\0", b)
    return out


def mirrored_batches(exporter, female, batches, skeleton_path=None):
    """Batches mirrored left for right (across the body's middle: the bind pose is symmetric),
    on the other side's bones: a weapon in the left hand as the right holds it, Skyrim's way
    (its left-hand weapons and swings are the right's, mirrored)."""
    _world, kept, _parents, _joints, _kept_for = exporter._skeleton(female, skeleton_path)
    twin = np.array([kept.index(_mirror_name(n)) if _mirror_name(n) in kept else i for i, n in enumerate(kept)])
    out = []
    for tex, alpha, v, idx, tag in batches:
        v = v.copy()
        v["pos"][:, 0] *= -1.0
        v["n"][:, 0] = -v["n"][:, 0]
        v["bone"][:, 0], v["bone"][:, 1] = twin[v["bone"][:, 0]], twin[v["bone"][:, 1]]
        v["bone2"] = twin[v["bone2"]]
        tri = idx.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1)  # (mirrored: the winding turns over)
        out.append((tex, alpha, v, tri.astype(idx.dtype), tag))
    return out


def rigid_batches(exporter, female, tag, nif, node, skeleton_path=None, keep=lambda shape: True, offset=(0, 0, 0)):
    """A static model's shapes as rigid batches on a skeleton node (bind pose, model space);
    offset: moved by this much (Skyrim units, model space) after."""
    world, _kept, _parents, _joints, kept_for = exporter._skeleton(female, skeleton_path)
    if node not in world or nif is None:
        return []
    bone = kept_for(node)
    m = world[node]
    out = []
    for s in nif.shapes():
        if not s.texture or not keep(s):
            continue
        pos = s.positions.astype(np.float64) @ m[:3, :3].T + m[:3, 3] + np.asarray(offset, np.float64)
        v = np.zeros(len(pos), VERTEX_DTYPE)
        v["pos"] = np.stack([pos[:, 0], pos[:, 2], -pos[:, 1]], 1) / UNITS_PER_METER
        v["uv"] = np.nan_to_num(s.uvs)
        if s.normals is not None:
            nrm = s.normals.astype(np.float64) @ m[:3, :3].T
            v["n"] = np.clip(np.round(np.stack([nrm[:, 0], nrm[:, 2], -nrm[:, 1]], 1) * 127), -127, 127)
        else:
            v["n"] = (0, 127, 0)
        v["col"] = 255
        v["bone"][:, 0] = v["bone"][:, 1] = v["bone2"] = bone
        v["w"][:, 0] = 255
        tex = texture_pack_name(s.texture) + ("_a" if s.alpha_test else "")
        exporter.models._convert_texture({"texture": tex, "source": s.texture, "flags": 1 if s.alpha_test else 0})
        out.append((tex, s.alpha_test, v, s.triangles.astype("<u2").reshape(-1), tag))
    return out


def _write(path, kind, slots, third, first, extras=None):
    """extras: (third person's, first person's) batches' look (Exporter.last_extras: the thin
    build, a head part's chargen morphs and roles) for its .skmorph."""
    data = struct.pack("<4sHHI", b"SKGR", SKGR_VERSION, kind, slots)
    finals = []
    for k, batches in enumerate((third, first)):
        final = []
        data += struct.pack("<I", len(batches)) + _batch_bytes(batches, extras[k] if extras else None, final)
        finals.append(final)
    with open(path, "wb") as f:
        f.write(data)
    if extras is not None:
        write_skmorph(path[:-len(".skgear")] + ".skmorph", finals)


def export(exporter, items, pack, npc, gid, female, race_id, wardrobe, family="human"):
    """wardrobe: {item form id: index} of the player's own clothes (the player's .sknpc);
    family: the bodies it fits (races.py: humans and elves share theirs)."""
    b = exporter.builder
    root = os.path.join(pack, "actors", "gear")
    out_dir = os.path.join(root, family)
    os.makedirs(out_dir, exist_ok=True)
    for old in os.listdir(root):  # (files of before body families)
        if os.path.isfile(os.path.join(root, old)):
            os.remove(os.path.join(root, old))
    for old in os.listdir(out_dir):
        os.remove(os.path.join(out_dir, old))
    done, lines, stats = {}, [], {"armour": 0, "weapons": 0, "items": 0}
    for form, rec in list(items.items):
        if form in wardrobe:
            slots = b._armor_slots(rec) if rec.type == "ARMO" else 0
            kind = "shield" if slots & SHIELD_SLOT else "armour" if rec.type == "ARMO" else "torch"
            lines.append(f"{form:08x}\twardrobe {wardrobe[form]}\t{kind}\t{slots}\t-1")
            continue
        if rec.type == "AMMO":  # (arrows: their quiver, worn on the back while they're equipped)
            modl = rec.sub("MODL")
            if not modl:
                continue
            path = "meshes\\" + modl.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")
            key = ("q", path)
            if key not in done:
                nif = b._nif(path)
                shapes, owners = nif.shapes_by_node([QUIVER_PART]) if nif is not None else ([], [])
                parts = {sh.name for sh, o in zip(shapes, owners) if o == QUIVER_PART}
                third = rigid_batches(exporter, female, TAG_HAND, nif, QUIVER_NODE, keep=lambda sh: sh.name in parts)
                done[key] = f"q{len(done):04d}" if third else None
                if third:
                    _write(os.path.join(out_dir, done[key] + ".skgear"), 0, 0, third, [])
                    stats["armour"] += 1
            if done[key]:
                lines.append(f"{form:08x}\tgear {done[key]}\tquiver\t0\t-1")
                stats["items"] += 1
        elif rec.type == "ARMO":
            addons = b._armor_addons(rec, race_id, female)
            if not addons:
                continue
            key = ("a",) + tuple(sorted(p for _s, p in addons))
            slots = b._armor_slots(rec)
            if key not in done:
                name = f"a{len(done):04d}"
                plan = [(p, False, 1) for _s, p in addons]
                exporter.want_weight = True  # (Skyrim's thin build of it: the character's weight)
                _sk, third = exporter._batches(npc, gid, (female, race_id, plan))
                ext3 = exporter.last_extras if third else []
                first = [p for _s, p in b._armor_addons(rec, race_id, female, first_person=True) if b._nif(p) is not None]
                # (no first-person model: Skyrim shows nothing - hoods, cloaks, a robe's cowl; their
                # third-person mesh on the first-person skeleton hung in front of the camera. Only
                # gloves and bracers borrow it, or the hands would be missing)
                if not first and slots & ARM_SLOTS and not slots & ~ARM_SLOTS:
                    first = [p for _s, p in addons if b._nif(p) is not None]
                _sk1, first_b = exporter._batches(npc, gid, (female, race_id, [(p, False, 1) for p in first]),
                                                  skeleton_path=FIRST_PERSON_SKELETON) if first else (None, [])
                ext1 = exporter.last_extras if first and first_b else []
                exporter.want_weight = False
                if not third and slots & SHIELD_SLOT:
                    # (a shield is a rigid model, not skinned: on the forearm's SHIELD node)
                    third = sum((rigid_batches(exporter, female, TAG_HAND, b._nif(p), SHIELD_NODE)
                                 for _s, p in addons), [])
                    # (a shield has no first-person model of its own: its one model on the first-person
                    # arm too - it wasn't drawn at all in first person)
                    first_b = sum((rigid_batches(exporter, female, TAG_HAND, b._nif(p), SHIELD_NODE,
                                                 FIRST_PERSON_SKELETON) for p in (first or [p for _s, p in addons])), [])
                    ext3, ext1 = [], []
                if not third:
                    done[key] = None
                    continue
                _write(os.path.join(out_dir, name + ".skgear"), 0, slots, third, first_b or [], (ext3, ext1))
                done[key] = name
                stats["armour"] += 1
            if done[key]:
                kind = "shield" if slots & SHIELD_SLOT else "armour"
                lines.append(f"{form:08x}\tgear {done[key]}\t{kind}\t{slots}\t-1")
                stats["items"] += 1
        elif rec.type == "WEAP":
            modl, dnam = rec.sub("MODL"), rec.sub("DNAM")
            if not modl or not dnam:
                continue
            wtype = dnam[0]
            if wtype not in SHEATH:
                continue
            path = "meshes\\" + modl.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")
            bound = "BoundWeapon" in rec.editor_id()
            if bound:
                # (a bound weapon's model is a placeholder: its blade is an effect Skyrim draws
                # at run time; a real one of its kind stands in, drawn as glowing light)
                path = BOUND_STAND_IN.get(wtype, path)
            key = ("w", path, bound)
            if key not in done:
                nif = b._nif(path)
                name = f"w{len(done):04d}"
                is_scb = lambda s: "scb" in s.name.lower()
                hand = "SHIELD" if wtype in BOWS else "WEAPON"
                back = SHEATH_OFFSET.get(SHEATH[wtype], (0, 0, 0))
                third = (rigid_batches(exporter, female, TAG_HAND, nif, hand, keep=lambda s: not is_scb(s)) +
                         rigid_batches(exporter, female, TAG_SHEATHED, nif, SHEATH[wtype], offset=back) +
                         rigid_batches(exporter, female, TAG_SCABBARD, nif, SHEATH[wtype], keep=is_scb, offset=back))
                first = rigid_batches(exporter, female, TAG_HAND, nif, hand, FIRST_PERSON_SKELETON,
                                      keep=lambda s: not is_scb(s))
                if not third:
                    done[key] = None
                    continue
                if bound:
                    third = [(t, a, v, i, tag | BOUND_TAG) for t, a, v, i, tag in third]
                    first = [(t, a, v, i, tag | BOUND_TAG) for t, a, v, i, tag in first]
                _write(os.path.join(out_dir, name + ".skgear"), 1, 0, third, first)
                if wtype in ONE_HANDED:  # (its left-hand twin, for dual wielding: <name>l)
                    _write(os.path.join(out_dir, name + "l.skgear"), 1, 0, mirrored_batches(exporter, female, third),
                           mirrored_batches(exporter, female, first, FIRST_PERSON_SKELETON))
                done[key] = name
                stats["weapons"] += 1
            if done[key]:
                lines.append(f"{form:08x}\tgear {done[key]}\tweapon\t0\t{wtype}")
                stats["items"] += 1
    with open(os.path.join(pack, "items", f"gear_{family}.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("gear:", stats)
