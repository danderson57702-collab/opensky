"""Skyrim's 18 skills, for the engine's skill and level system.

Each skill's actor value record (AVIF) holds how it improves (AVSK): experience for a use is
use_mult x usage + use_offset, and the next level takes improve_mult x level^1.95 +
improve_offset. The player's race (RACE DATA) starts some skills higher than the base 15.

  PACK_DIR/skills.txt   one line per skill, in actor value order (One-handed .. Enchanting):
                        key|name|use_mult|use_offset|improve_mult|improve_offset|start level
"""
import os
import struct

# Actor value indices of the skills (One-handed is 6 .. Enchanting 23)
FIRST_SKILL_AV, SKILL_COUNT = 6, 18
AVIF_IDS = range(0x44C, 0x44C + SKILL_COUNT)  # Skyrim.esm's skill AVIF form ids, in the same order
BASE_SKILL = 15
PLAYER_RACE = "NordRace"


def export(load_order, pack, strings, race_id=PLAYER_RACE):
    plugin = load_order.plugins[0]
    avif = {r.form_id: r for r in plugin.records_of_type("AVIF")}
    boosts = {}
    for r in plugin.records_of_type("RACE"):
        if r.editor_id() == race_id and r.sub("DATA"):
            pairs = struct.unpack_from("<14b", r.sub("DATA"), 0)
            for av, amount in zip(pairs[0::2], pairs[1::2]):
                if av >= 0:
                    boosts[av] = amount
    lines = []
    for i, form_id in enumerate(AVIF_IDS):
        r = avif.get(form_id)
        if r is None or not r.sub("AVSK"):
            raise ValueError(f"skill record {form_id:#x} missing")
        use_mult, use_offset, improve_mult, improve_offset = struct.unpack_from("<4f", r.sub("AVSK"))
        full = r.sub("FULL")
        name = strings.get(r.plugin, struct.unpack("<I", full)[0], "strings") if full and len(full) == 4 else r.editor_id()
        start = BASE_SKILL + boosts.get(FIRST_SKILL_AV + i, 0)
        key = r.editor_id()[2:] if r.editor_id().startswith("AV") else r.editor_id()
        lines.append(f"{key}|{name}|{use_mult:g}|{use_offset:g}|{improve_mult:g}|{improve_offset:g}|{start}")
    with open(os.path.join(pack, "skills.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    # Each skill's own words (AVIF DESC), shown in the skills menu as Skyrim's are: skills_desc.txt "key|text"
    # (apart: skills.txt is read line by line in its order)
    descs = []
    for form_id in AVIF_IDS:
        r = avif.get(form_id)
        desc = r.sub("DESC") if r is not None else None
        text = strings.get(r.plugin, struct.unpack("<I", desc)[0], "dlstrings") if desc and len(desc) == 4 else ""
        key = r.editor_id()[2:] if r.editor_id().startswith("AV") else r.editor_id()
        if text:
            descs.append(f"{key}|" + text.replace("|", "/").replace("\r", "").replace("\n", " "))
    with open(os.path.join(pack, "skills_desc.txt"), "w") as f:
        f.write("\n".join(descs) + "\n")
    print(f"skills: {len(lines)} ({race_id} starts: " +
          ", ".join(l.split('|')[1] + ' ' + l.split('|')[6] for l in lines if int(l.split('|')[6]) != BASE_SKILL) + ")")
