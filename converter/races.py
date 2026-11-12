"""The ten playable races (Skyrim's RACE records) and the faces to choose from in the race
menu: unique NPCs of each race who don't live in the converted region (the character
creation presets have no FaceGen heads in the game's files; these do, beards included).

  PACK_DIR/races.txt, tab-separated lines, per race:
    R <race editor id> <name> <height> <body family> (the gear it wears: "human", "khajiit",
      "argonian")
    D <description>
    B <skill>:<bonus> ... (skills in actor value order from One-Handed = 0; Skyrim's racial
      bonuses over the base 15)
    V <health> <magicka> <stamina> (the race's starting values)
    P <kind> <name> <effect> <magnitude> <seconds> <text>: its abilities and powers (kind:
      ability, power (once a day), lesser (any time); effect: what the engine does with it:
      health, magicka, stamina (more of it), health_rate, magicka_rate, stamina_rate (faster
      regeneration, percent), waterbreathing, nighteye, or none (shown only))
    F <player file stem> <face name>: a face (actors/player/<stem>.sknpc and <stem>_1st.sknpc)
"""
import os
import struct

# (race, body family, faces: NPC editor ids)
PLAYABLE = [
    ("NordRace", "human", ("CWBattleUlfric", "CWBattleGalmar", "Roggvir")),
    ("ImperialRace", "human", ("AquilliusAeresius", "GavrosPlinius", "Rune")),
    ("BretonRace", "human", ("DelvinMallory", "EtienneRarnis", "Adeber")),
    ("RedguardRace", "human", ("Nazir", "Talib", "Razelan")),
    ("HighElfRace", "human", ("Ancano", "Ondolemar", "Nelacar")),
    ("DarkElfRace", "human", ("Erandur", "SavosAren", "RevynSadri")),
    ("WoodElfRace", "human", ("Niruin", "Enthir", "Valindor")),
    ("OrcRace", "human", ("UraggroShub", "Ghorbash", "Mauhulakh")),
    ("KhajiitRace", "khajiit", ("Jzargo", "Majhad", "Kharjo")),
    ("ArgonianRace", "argonian", ("Derkeethus", "Veezara", "TeebaEi")),
]
SPELL_KINDS = {2: "power", 3: "lesser", 4: "ability"}
# Actor values: what the engine models, and how each reads
AV_EFFECTS = {24: "health", 25: "magicka", 26: "stamina", 27: "health_rate", 28: "magicka_rate",
              29: "stamina_rate", 57: "waterbreathing"}
AV_TEXT = {24: "+{m:g} Health", 25: "+{m:g} Magicka", 26: "+{m:g} Stamina",
           27: "Health regenerates {m:g}x faster", 28: "Magicka regenerates {m:g}% faster",
           29: "Stamina regenerates {m:g}% faster", 35: "Unarmed attacks do {m:g} more damage",
           40: "{m:g}% resistance to poison", 41: "{m:g}% resistance to fire", 42: "{m:g}% resistance to shock",
           43: "{m:g}% resistance to frost", 44: "{m:g}% resistance to magic", 45: "{m:g}% resistance to disease",
           57: "Can breathe underwater"}
# Effects without an actor value, by their magic effect's editor id
EID_TEXT = {"NightEyeEffect": ("nighteye", "See in the dark"),
            "RaceNordBattleCryEffect": ("none", "Nearby enemies flee"),
            "PowerDarkElfFireCloakFFSelf": ("none", "Surrounded by fire that burns enemies"),
            "RaceBretonAbsorbSpellChance": ("none", "50% chance to absorb the magicka of hostile spells"),
            "RaceImperialPacify": ("none", "Calms nearby people"),
            "RaceWoodElfCommandAnimal": ("none", "Makes an animal an ally"),
            "RaceOrcBerserkEffect": ("none", "Deal double damage and take half damage"),
            "RaceImperialLuckEffect": ("none", "Finds more gold")}


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _effects(lo, strings, spell):
    """[(effect, magnitude, seconds, text)] of a spell's magic effects."""
    out, subs = [], list(spell.subrecords())
    for i, (t, v) in enumerate(subs):
        if t != "EFID" or i + 1 >= len(subs) or subs[i + 1][0] != "EFIT":
            continue
        mgef = lo.record(lo.global_id(spell.plugin, _u32(v)), types=("MGEF",))
        if mgef is None or not mgef.sub("DATA"):
            continue
        mag, _area, dur = struct.unpack_from("<fII", subs[i + 1][1])
        av = struct.unpack_from("<i", mgef.sub("DATA"), 68)[0]
        if mgef.editor_id() in EID_TEXT:
            effect, text = EID_TEXT[mgef.editor_id()]
        elif av in AV_TEXT:
            effect, text = AV_EFFECTS.get(av, "none"), AV_TEXT[av].format(m=mag)
        else:
            continue  # (staggers, sounds: parts of an effect already described)
        out.append((effect, mag, dur, text))
    return out


def export(lo, strings, pack, faces):
    """faces: {race editor id: [(player file stem, face name)]} as exported."""
    races = {r.editor_id(): r for p in lo.plugins for r in p.records_of_type("RACE")}
    name = lambda rec, field="FULL", table="strings": (
        strings.get(rec.plugin, _u32(rec.sub(field)), table) if rec.sub(field) and len(rec.sub(field)) == 4
        else rec.editor_id())
    clean = lambda s: " ".join(s.split())
    lines = []
    for eid, family, _donors in PLAYABLE:
        race = races.get(eid)
        if race is None or not faces.get(eid):
            continue
        d = race.sub("DATA")
        lines.append(f"R\t{eid}\t{name(race)}\t{struct.unpack_from('<f', d, 16)[0]:.3f}\t{family}")
        lines.append(f"D\t{clean(name(race, 'DESC', 'dlstrings'))}")
        lines.append("B\t" + " ".join(f"{d[i * 2] - 6}:{d[i * 2 + 1]}" for i in range(7) if 6 <= d[i * 2] <= 23))
        lines.append("V\t" + " ".join(f"{v:g}" for v in struct.unpack_from("<3f", d, 36)))
        for t, v in race.subrecords():
            if t != "SPLO":
                continue
            spell = lo.record(lo.global_id(race.plugin, _u32(v)), types=("SPEL",))
            if spell is None or not spell.sub("SPIT"):
                continue
            kind = SPELL_KINDS.get(_u32(spell.sub("SPIT"), 8))
            for effect, mag, dur, text in _effects(lo, strings, spell) if kind else []:
                lines.append(f"P\t{kind}\t{clean(name(spell))}\t{effect}\t{mag:g}\t{dur}\t{text}")
        for stem, face in faces[eid]:
            lines.append(f"F\t{stem}\t{face}")
    with open(os.path.join(pack, "races.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print(f"races: {sum(1 for l in lines if l[0] == 'R')} playable, {sum(1 for l in lines if l[0] == 'F')} faces")
