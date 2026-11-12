"""Skyrim's crime: the holds' crime factions (what each crime costs - CRVA -, where the jail lets
you out - JAIL, the exterior jail marker), whose hold each exported person reports to (their
crime faction, CRIF), and what the guards say when they come for you (DialogueCrimeGuards: its
force-greet and the three answers, Skyrim's lines and voices).

  PACK_DIR/crime.txt, one a line:
    H|faction editor id|hold name|arrest|attack on sight|murder|assault|trespass|pickpocket|
      steal multiplier|escape|werewolf|release place|x|y|z|heading|
      cell place|x|y|z|heading (the prison marker in its jail; place -99: none here)|
      the player's things' chest (PLCN, ref hex)|the stolen goods' chest (STOL)|jail clothes (form hex, comma separated)
    A|actor editor id (lower case)|faction editor id
    P|topic|the player's words (Skyrim's: "<CrimeGold>" the bounty)
    L|topic|player's sex (0 either, 1 a man, 2 a woman)|faction editor id or -|voice type|
      part (0 a line's first, 1.. its next)|voice path (under voice/) or -|text
    G|holds (faction editor ids, comma separated; - any)|player's sex (0, 1, 2)|races (- any; else
      ;-separated groups each a comma list, any of which will do, of +race / -race editor ids)|
      voice type|part|voice path or -|text
      a guard's greeting (DialogueGuardsGeneral's Hellos: INFO 00020954)
  Topics: greet (the guard's stop), pay, jail, resist. Places are interior.h's (-1 Tamriel, -2 - n
  a walled city).
"""
import os
import struct
from collections import Counter

CRIME_TOPICS = {"DGCrimeForcegreetTopic": "greet", "DGCrimePayFine": "pay", "DGCrimeGoToJail": "jail",
                "DGCrimeResistArrest": "resist"}
CRIME_QUEST = "DialogueCrimeGuards"
# Lines about one hold's jail or crimes (Skyrim's conditions put them there): only that hold's
# guards say them; ones about things we don't keep track of (a frame-up, murders by conspiracy,
# the Jarl's thanes) are left out
HOLD_WORDS = {"dragonsreach": "CrimeFactionWhiterun", "cidhna": "CrimeFactionReach", "markarth": "CrimeFactionReach",
              "castle dour": "CrimeFactionHaafingar", "the chill": "CrimeFactionWinterhold",
              "forsworn": "CrimeFactionReach"}
LEFT_OUT = ("murder", "conspir", "thane", "framed", "suspicious behavior", "what in the")  # (the last: caught trespassing)
F_GET_PC_IS_SEX = 131
F_GET_IN_FACTION, F_GET_PC_IS_RACE = 71, 130
HELLO_QUEST, HELLO_SUBTYPE = "DialogueGuardsGeneral", 73  # (Skyrim's guards' Hellos)
# What each hold's guards are in (for the Hellos' conditions): their city's guard faction, the
# Civil War side their hold starts on (Whiterun neutral)
HOLD_FACTIONS = {
    "CrimeFactionWhiterun": ("GuardFactionWhiterun", "CWWhiterunGuardNeutralFaction"),
    "CrimeFactionEastmarch": ("GuardFactionWindhelm", "CrimeFactionSons"),
    "CrimeFactionRift": ("GuardFactionRiften", "CrimeFactionSons"),
    "CrimeFactionPale": ("GuardFactionDawnstar", "CrimeFactionSons"),
    "CrimeFactionWinterhold": ("GuardFactionWinterhold", "CrimeFactionSons"),
    "CrimeFactionHaafingar": ("GuardFactionSolitude", "CrimeFactionImperial"),
    "CrimeFactionReach": ("GuardFactionMarkarth", "CrimeFactionImperial"),
    "CrimeFactionHjaalmarch": ("GuardFactionMorthal", "CrimeFactionImperial"),
    "CrimeFactionFalkreath": ("GuardFactionFalkreath", "CrimeFactionImperial"),
}
CTDA = struct.Struct("<B3xfH2xIIIIi")


def _hello_conditions(lo, info, holds):
    """(the holds whose guards may say it, sex 0/1/2, race groups) of a Hello, or None if its
    conditions go beyond the guard's factions and your race and sex."""
    groups, group = [], []
    for k, v in info.subrecords():
        if k != "CTDA" or len(v) < CTDA.size:
            continue
        flags, value, fn, p1, _p2, run_on, _ref, _x = CTDA.unpack_from(v)
        if fn not in (F_GET_IN_FACTION, F_GET_PC_IS_RACE, F_GET_PC_IS_SEX) or (fn == F_GET_IN_FACTION and run_on != 0):
            return None
        rec = lo.record(lo.global_id(info.plugin, p1), types=("FACT", "RACE")) if fn != F_GET_PC_IS_SEX else None
        group.append((flags >> 5, value, fn, (rec.editor_id() if rec is not None else "") if fn != F_GET_PC_IS_SEX else p1))
        if not flags & 1:  # (OR: joined to the next)
            groups.append(group)
            group = []
    if group:
        groups.append(group)

    def holds_true(op, value, held):
        r = 1.0 if held else 0.0
        return {0: r == value, 1: r != value, 2: r > value, 3: r >= value, 4: r < value, 5: r <= value}.get(op, False)
    ok_holds, sex, races = [], 0, []
    for hold in holds:
        inside = {hold, "GuardDialogueFaction", "IsGuardFaction"} | set(HOLD_FACTIONS.get(hold, ()))
        if all(any(holds_true(op, val, name in inside) for op, val, fn, name in g if fn == F_GET_IN_FACTION)
               for g in groups if all(fn == F_GET_IN_FACTION for _o, _v, fn, _n in g)):
            ok_holds.append(hold)
    for g in groups:
        kinds = {fn for _o, _v, fn, _n in g}
        if kinds == {F_GET_IN_FACTION}:
            continue
        if len(kinds) > 1:
            return None  # (a faction or your race: not ours to tell)
        if kinds == {F_GET_PC_IS_SEX}:
            if len(g) != 1 or g[0][0] not in (0, 1):
                return None
            op, val, _fn, who = g[0]
            female = who == 1 if (op == 0) == (val >= 0.5) else who != 1
            sex = 2 if female else 1
        else:
            races.append(",".join(("+" if (op == 0) == (val >= 0.5) else "-") + name.lower() for op, val, _fn, name in g))
    return ok_holds, sex, races
FLAG_USE_FACTIONS = 0x4


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def crime_factions(lo):
    """{faction gid: FACT} for the holds' crime factions (those with a jail)."""
    out = {}
    for p in lo.plugins:
        for f in p.records_of_type("FACT"):
            if (f.editor_id() or "").startswith("CrimeFaction") and f.sub("JAIL") and f.sub("CRVA"):
                out[lo.global_id(p, f.form_id)] = f
    return out


def crime_faction_of(records, npc, factions):
    """The crime faction an NPC reports to (CRIF: its own or, using its template's factions,
    the template's), or None."""
    t = records.template(npc, FLAG_USE_FACTIONS)
    c = t.sub("CRIF") or npc.sub("CRIF")
    g = records.lo.global_id(t.plugin, _u32(c)) if c else None
    return g if g in factions else None


def is_guard(records, npc):
    """In a guard faction (IsGuardFaction, or a hold's GuardFaction...): Skyrim's guards."""
    t = records.template(npc, FLAG_USE_FACTIONS)
    for k, v in t.subrecords():
        if k == "SNAM" and len(v) >= 4:
            f = records.lo.record(records.lo.global_id(t.plugin, _u32(v)), types=("FACT",))
            if f is not None and "guardfaction" in (f.editor_id() or "").lower():
                return True
    return False


def _player_sex(info):
    for k, v in info.subrecords():
        if k == "CTDA" and len(v) >= 16 and struct.unpack_from("<H", v, 8)[0] == F_GET_PC_IS_SEX:
            sex, value, op = _u32(v, 12), struct.unpack_from("<f", v, 4)[0], v[0] >> 5
            wants = (sex == 0) == (value != 0.0) if op == 0 else (sex == 0) != (value != 0.0)
            return 1 if wants else 2
    return 0


def export(lo, pack, strings, talk, records, ref_place, actors, guard_voices, ref_at=None):
    """talk: the people's dialogue.Dialogue (voice files and their conversion); ref_place(gid):
    (place, x, y, z, heading) in the engine's metres, or None; actors: [(editor id, NPC)];
    guard_voices: the guards' voice types."""
    stats = Counter()
    factions = crime_factions(lo)
    lines = []
    for g, f in sorted(factions.items()):
        eid = f.editor_id()
        cr = f.sub("CRVA")
        arrest, attack = cr[0], cr[1]
        murder, assault, trespass, pickpocket = struct.unpack_from("<4H", cr, 2)
        steal = struct.unpack_from("<f", cr, 12)[0] if len(cr) >= 16 else 1.0
        escape, werewolf = struct.unpack_from("<2H", cr, 16) if len(cr) >= 20 else (0, 0)
        at = ref_place(lo.global_id(f.plugin, _u32(f.sub("JAIL"))))
        place, x, y, z, h = at if at is not None else (-99, 0.0, 0.0, 0.0, 0.0)
        name = eid[len("CrimeFaction"):]
        # The jail: its prison marker (Skyrim's PrisonMarker in the cell where your things are kept)
        plcn = lo.global_id(f.plugin, _u32(f.sub("PLCN"))) if f.sub("PLCN") else 0
        stol = lo.global_id(f.plugin, _u32(f.sub("STOL"))) if f.sub("STOL") else 0
        cell = (-99, 0.0, 0.0, 0.0, 0.0)
        kept = ref_at.get(plcn) if ref_at and plcn else None
        if kept is not None and kept[0] >= 0:
            for gid, (pl, rr) in ref_at.items():
                if pl != kept[0] or not rr.sub("NAME"):
                    continue
                b = lo.record(lo.global_id(rr.plugin, _u32(rr.sub("NAME"))), types=("DOOR",))
                if b is not None and b.editor_id() == "PrisonMarker":
                    cell = ref_place(gid)
                    break
        outfit = []
        jout = lo.record(lo.global_id(f.plugin, _u32(f.sub("JOUT"))), types=("OTFT",)) if f.sub("JOUT") else None
        inam = (jout.sub("INAM") or b"") if jout is not None else b""
        for i in range(len(inam) // 4):
            for a in records.all_items(lo.global_id(jout.plugin, _u32(inam, i * 4)), ("ARMO",)):
                outfit.append(lo.global_id(a.plugin, a.form_id))
        lines.append(f"H|{eid}|{name}|{arrest}|{attack}|{murder}|{assault}|{trespass}|{pickpocket}|{steal:g}|"
                     f"{escape}|{werewolf}|{place}|{x:.2f}|{y:.2f}|{z:.2f}|{h:.3f}|"
                     f"{cell[0]}|{cell[1]:.2f}|{cell[2]:.2f}|{cell[3]:.2f}|{cell[4]:.3f}|{plcn:x}|{stol:x}|"
                     f"{','.join(f'{o:x}' for o in outfit) or '-'}")
        stats["holds with their jail cell here"] += cell[0] > -99
        stats["holds"] += 1
        stats["holds with their jail's way out here"] += at is not None
    for eid, npc in actors:
        g = crime_faction_of(records, npc, factions)
        if g is not None:
            lines.append(f"A|{eid.lower()}|{factions[g].editor_id()}")
            stats["people reporting crimes"] += 1
    # The guards' words: DialogueCrimeGuards' topics, every guard voice's take of each line
    for p in lo.plugins:
        topics = p.topic_infos()
        for dial in p.records_of_type("DIAL"):
            topic = CRIME_TOPICS.get(dial.editor_id())
            q = dial.sub("QNAM")
            quest = lo.record(lo.global_id(p, _u32(q)), types=("QUST",)) if q else None
            if topic is None or quest is None or quest.editor_id() != CRIME_QUEST:
                continue
            full = dial.sub("FULL")
            prompt = strings.get(p, _u32(full), "strings") if full and len(full) == 4 else ""
            if prompt and topic != "greet":
                lines.append(f"P|{topic}|{prompt.replace('|', '/')}")
            for info in topics.get(dial.form_id, []):
                sex = _player_sex(info)
                for voice in sorted(guard_voices):
                    responses = talk._responses(info, voice)
                    if not responses:
                        continue
                    text = " ".join(t for _p, t in responses)
                    low = text.lower()
                    if any(w in low for w in LEFT_OUT):
                        stats["lines left out"] += 1
                        break
                    hold = next((h for w, h in HOLD_WORDS.items() if w in low), "-")
                    for n, (vp, t) in enumerate(responses):  # (a line in parts: each with its own voice file)
                        path = talk._convert(vp)
                        lines.append(f"L|{topic}|{sex}|{hold}|{voice}|{n}|{path or '-'}|{t.replace('|', '/')}")
                    stats[f"{topic} lines"] += 1
    # The guards' greetings: DialogueGuardsGeneral's Hellos whose conditions we can tell - the
    # guard's hold and side, your race and sex (INFO 000AC12A)
    holds = [f.editor_id() for f in factions.values()]
    for p in lo.plugins:
        topics = p.topic_infos()
        for dial in p.records_of_type("DIAL"):
            q, data = dial.sub("QNAM"), dial.sub("DATA")
            quest = lo.record(lo.global_id(p, _u32(q)), types=("QUST",)) if q else None
            if quest is None or quest.editor_id() != HELLO_QUEST or not data or len(data) < 4 or \
                    struct.unpack_from("<H", data, 2)[0] != HELLO_SUBTYPE:
                continue
            for info in topics.get(dial.form_id, []):
                cond = _hello_conditions(lo, info, holds)
                if cond is None or not cond[0]:
                    stats["hellos left out"] += 1
                    continue
                ok_holds, sex, races = cond
                where = "-" if len(ok_holds) == len(holds) else ",".join(ok_holds)
                for voice in sorted(guard_voices):
                    responses = talk._responses(info, voice)
                    if not responses:
                        continue
                    for n, (vp, t) in enumerate(responses):
                        path = talk._convert(vp)
                        lines.append(f"G|{where}|{sex}|{';'.join(races) or '-'}|{voice}|{n}|{path or '-'}|{t.replace('|', '/')}")
                    stats["hello lines"] += 1
    with open(os.path.join(pack, "crime.txt"), "w", encoding="cp1252", errors="replace") as fh:
        fh.write("\n".join(lines) + "\n")
    print("crime:", dict(stats))
