"""The people's lives, for fighting them (engine/src/people.c): from each NPC's record, its health
and whether it can die - Skyrim's essential people (the story's: knocked down, never killed) and
protected ones (only you can kill them).

  PACK_DIR/actors/combat.txt   per person: "<editor id> <flags> <health> [<quest>:<flags> ...]"
    flags: 1 essential, 2 protected (ACBS, or an alias of a quest running from the game's start);
    health: the race's starting health, the record's offset and fNPCHealthLevelBonus (5) a level -
    at its level (a player-leveled one's at 10); then the quests whose aliases make them essential
    or protected while those run (Balgruuf, in Before the Storm: MQ102)

python3 people_combat.py ../pack
"""
import os
import struct
import sys

ESSENTIAL, PROTECTED, PC_LEVEL_MULT = 0x2, 0x800, 0x80
ALIAS_ESSENTIAL, ALIAS_PROTECTED = 0x40, 0x400  # (a QUST alias's flags: its FNAM)
QUEST_START_ENABLED = 0x1  # (QUST DNAM flags)
HEALTH_PER_LEVEL = 5.0  # (fNPCHealthLevelBonus)
LEVEL_FOR_MULT = 10     # (a player-leveled person: as at this level of yours)


def _alias_flags(lo):
    """({NPC form: flags}, {NPC form: {quest editor id: flags}}): a unique actor (ALUA) made
    essential or protected by a quest's alias - always, for a quest running from the game's start;
    else while that quest runs."""
    out, during = {}, {}
    for p in lo.plugins:
        for q in p.records_of_type("QUST"):
            dnam = q.sub("DNAM")
            always = bool(dnam and struct.unpack_from("<H", dnam)[0] & QUEST_START_ENABLED)
            flags, in_alias = None, False
            for t, v in q.subrecords():
                if t in ("ALST", "ALLS"):
                    flags, in_alias = None, True
                elif t == "FNAM" and in_alias and len(v) >= 4:
                    flags = struct.unpack_from("<I", v)[0]
                elif t == "ALUA" and flags is not None and len(v) >= 4:
                    f = (1 if flags & ALIAS_ESSENTIAL else 0) | (2 if flags & ALIAS_PROTECTED else 0)
                    if f:
                        g = lo.global_id(q.plugin, struct.unpack_from("<I", v)[0])
                        if always:
                            out[g] = out.get(g, 0) | f
                        else:
                            d = during.setdefault(g, {})
                            d[q.editor_id()] = d.get(q.editor_id(), 0) | f
    return out, during


def build(lo, pack):
    npcs = {(r.editor_id() or "").lower(): r for p in lo.plugins for r in p.records_of_type("NPC_")}
    names = [l.strip()[:-6] for l in open(os.path.join(pack, "actors", "actors.txt")) if l.strip().endswith(".sknpc")]
    by_alias, during = _alias_flags(lo)
    out, essential, protected = [], 0, 0
    for eid in names:
        npc = npcs.get(eid)
        acbs = npc.sub("ACBS") if npc is not None else None
        if not acbs or len(acbs) < 22:
            continue
        flags, level, offset = struct.unpack_from("<I", acbs)[0], struct.unpack_from("<h", acbs, 8)[0], \
            struct.unpack_from("<h", acbs, 20)[0]
        if flags & PC_LEVEL_MULT:
            level = max(1, round(level / 1000.0 * LEVEL_FOR_MULT))
        start = 50.0
        rnam = npc.sub("RNAM")
        race = lo.record(lo.global_id(npc.plugin, struct.unpack_from("<I", rnam)[0]), types=("RACE",)) if rnam else None
        rd = race.sub("DATA") if race is not None else None
        if rd and len(rd) >= 40:
            start = struct.unpack_from("<f", rd, 36)[0]
        health = max(10.0, start + offset + HEALTH_PER_LEVEL * level)
        gid = lo.global_id(npc.plugin, npc.form_id)
        f = (1 if flags & ESSENTIAL else 0) | (2 if flags & PROTECTED else 0) | by_alias.get(gid, 0)
        if f & 1:
            f &= 1
        essential += f & 1
        protected += (f & 2) >> 1
        quests = " ".join(f"{q}:{qf}" for q, qf in sorted(during.get(gid, {}).items()))
        out.append(f"{eid} {f} {health:.0f} {quests}".rstrip())
    with open(os.path.join(pack, "actors", "combat.txt"), "w") as fh:
        fh.write("\n".join(out) + "\n")
    print(f"people: {len(out)} lives, {essential} essential, {protected} protected")


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from esm import LoadOrder
    from extract_textures import SKYRIM_DATA
    build(LoadOrder(SKYRIM_DATA), sys.argv[1] if len(sys.argv) > 1 else "../pack")
