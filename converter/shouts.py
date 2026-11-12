"""Shouts (the Thu'um) and the racial powers' yells, from Skyrim's records, for the player.

A shout (SHOU) has three words (WOOP: the word in the dragon tongue and its meaning), each
with its spell (SPEL: what one, two or all three words do) and recovery (seconds before the
next shout). The engine models four for now - Unrelenting Force, Fire Breath, Frost Breath and
Whirlwind Sprint - from their spells' effects: the push (Skyrim's stagger magnitude), the
damage (all of it, over its seconds), the slow.

The player's voice: Skyrim's VoicePowers lines (topics Shout01a "Fus!" - one word; Shout01b
"Fus..." - the first of more; Shout02 "Ro!" - the second, ending; Shout03 "Ro...Dah!" - the
second and third), for each shout and each voice the player can have; the racial powers'
yells (Battle Cry's "Aaah!") are Shout01a lines too.

  PACK_DIR/shouts.txt   '|'-separated:
    S|key|name|word1|word2|word3|meaning1|meaning2|meaning3|recovery1|2|3|push1|2|3|damage1|2|3|
      slow %|seconds|duration1|2|3|magnitude1|2|3|summons   (summons: each word's creature kind,
      comma-separated, "-" none - Call of Valor's heroes; key: force, fire, frost, sprint, ethereal,
      disarm, iceform, marked, slowtime, kynespeace, animal, fury, aura, throwvoice, drain; damage:
      all of it; seconds: how long frost's damage and slow last; each word's spell: its longest
      duration and its first effect's magnitude)
    V|key|voice type|line|path   (line: 1a, 1b, 2, 3; path under voice/; key may be a power's:
      battlecry, emperor, beasttongue)
  PACK_DIR/voice/shout/<voice type>_<key>_<line>.skvoc   (dialogue.Dialogue._convert's format)
"""
import os
import struct

from dialogue import Dialogue, StringTables
from bsa import BSA
from extract_music import load_adpcm
from extract_textures import SKYRIM_DATA

SHOUTS = (("UnrelentingForceShout", "force"), ("FireBreathShout", "fire"), ("FrostBreathShout", "frost"),
          ("WhirlwindSprintShout", "sprint"),
          # (the rest whose word walls are in the converted region: Ustengrav's Become Ethereal...)
          ("BecomeEtherealShout", "ethereal"), ("DisarmShout", "disarm"), ("IceFormShout", "iceform"),
          ("MarkedforDeathShout", "marked"), ("SlowTimeShout", "slowtime"), ("KynesPeaceShout", "kynespeace"),
          ("animalAllegianceShout", "animal"), ("ElementalFuryShout", "fury"), ("AuraWhisperShout", "aura"),
          ("ThrowVoiceShout", "throwvoice"), ("DLC1DrainVitalityShout", "drain"),
          ("ClearSkiesShout", "clearskies"),  # (the Greybeards' gift on the Throat of the World: MQ204)
          # (29 Oct, the whole map: the last five - Storm Call's and Dismay's walls; Call of Valor
          # Sovngarde's, Dragonrend the Elder Scroll's, Call Dragon Odahviing's gift)
          ("StormCallShout", "stormcall"), ("Dismay", "dismay"), ("CallofValorShout", "valor"),
          ("CallDragonShout", "calldragon"), ("DragonrendShout", "dragonrend"))
POWERS = (("NordBattleCry", "battlecry"), ("ImperialVoiceOfTheEmperor", "emperor"), ("WoodElfBeastTongue", "beasttongue"))
TOPICS = {"Shout01a": "1a", "Shout01b": "1b", "Shout02": "2", "Shout03": "3"}
# The voices the player's characters speak with (races.PLAYABLE: humans even-toned, elves
# haughty, orcs, khajiit, argonians)
PLAYER_VOICES = ("maleeventoned", "femaleeventoned", "maleelfhaughty", "femaleelfhaughty", "maleorc", "femaleorc",
                 "malekhajiit", "femalekhajiit", "maleargonian", "femaleargonian", "malenord", "femalenord",
                 "maleeventonedaccented")  # (the last three: only Battle Cry's and the Emperor's yells)
GET_EQUIPPED_SHOUT = 579  # (the condition naming a line's shout)
ARCH_STAGGER, ARCH_SLOW, AV_HEALTH = 33, 34, 24
# The dragon tongue's letters for its long vowels, as the words are spelled ("D4" Dah)
DRAGON_VOWELS = {"1": "aa", "2": "ei", "3": "ii", "4": "ah", "6": "uu", "7": "ir", "8": "oo", "9": "ey"}  # (Kaan K1n, Feim F2m, Dur D6, Mey M9)


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _word(text):
    out = "".join(DRAGON_VOWELS.get(ch, ch) for ch in text or "")
    return out[:1].upper() + out[1:]


def _spell_numbers(lo, spell):
    """(push, damage, slow %, seconds) of a shout word's spell."""
    push = damage = slow = seconds = 0.0
    subs = list(spell.subrecords())
    for i, (t, v) in enumerate(subs):
        if t != "EFID" or i + 1 >= len(subs) or subs[i + 1][0] != "EFIT":
            continue
        mgef = lo.record(lo.global_id(spell.plugin, _u32(v)), types=("MGEF",))
        mag, _area, dur = struct.unpack_from("<fII", subs[i + 1][1])
        d = mgef.sub("DATA") if mgef is not None else None
        if not d or len(d) < 0x48:
            continue
        arch, av = _u32(d, 0x40), struct.unpack_from("<i", d, 0x44)[0]
        if arch == ARCH_STAGGER:
            push = max(push, mag)
        elif arch == ARCH_SLOW:
            slow, seconds = max(slow, mag), max(seconds, float(dur))
        elif av == AV_HEALTH:
            damage += mag * max(1, dur) if dur > 1 else mag
            seconds = max(seconds, float(dur) if dur > 1 else seconds)
    return push, damage, slow, seconds


def _word_summon(lo, spell):
    """A word that summons (Call of Valor's heroes of Sovngarde): its creature kind
    (creatures.SUMMON_KINDS: run after the creatures' export), else "" """
    import creatures
    for t, v in spell.subrecords():
        if t == "EFID":
            m = lo.record(lo.global_id(spell.plugin, _u32(v)), types=("MGEF",))
            d = m.sub("DATA") if m is not None else None
            if d and len(d) >= 72 and _u32(d, 64) == 18 and _u32(d, 8):  # (archetype Summon Creature)
                return creatures.SUMMON_KINDS.get(lo.global_id(m.plugin, _u32(d, 8)), "")
            return ""
    return ""


def _word_effect(lo, spell):
    """(magnitude, seconds) of a shout word's spell: its first effect's magnitude, its longest
    duration (Ice Form's frozen seconds, Slow Time's, Become Ethereal's...)."""
    mag = dur = None
    subs = list(spell.subrecords())
    for i, (t, v) in enumerate(subs):
        if t == "EFID" and i + 1 < len(subs) and subs[i + 1][0] == "EFIT":
            m, _area, d = struct.unpack_from("<fII", subs[i + 1][1])
            mag = m if mag is None else mag
            dur = max(dur or 0, d)
    return mag or 0.0, float(dur or 0)


def export(lo, pack):
    strings = StringTables(lo)
    shous = {}
    for p in lo.plugins:
        for r in p.records_of_type("SHOU"):
            shous[lo.global_id(p, r.form_id)] = r
    by_eid = {r.editor_id(): (gid, r) for gid, r in shous.items()}
    name = lambda r, sub="FULL": strings.get(r.plugin, _u32(r.sub(sub)), "strings") \
        if r is not None and r.sub(sub) and len(r.sub(sub)) == 4 else ""
    lines = []
    for eid, key in SHOUTS:
        if eid not in by_eid:
            continue
        _gid, r = by_eid[eid]
        words, meanings, recovery, numbers, effects, summons = [], [], [], [], [], []
        for t, v in r.subrecords():
            if t != "SNAM" or len(v) < 12:
                continue
            woop, spell, rec = struct.unpack_from("<IIf", v)
            w = lo.record(lo.global_id(r.plugin, woop), types=("WOOP",)) if woop else None
            s = lo.record(lo.global_id(r.plugin, spell), types=("SPEL",)) if spell else None
            words.append(_word(name(w)))
            meanings.append(name(w, "TNAM"))
            recovery.append(rec)
            numbers.append(_spell_numbers(lo, s) if s is not None else (0.0, 0.0, 0.0, 0.0))
            effects.append(_word_effect(lo, s) if s is not None else (0.0, 0.0))
            summons.append(_word_summon(lo, s) if s is not None else "")
        if len(words) != 3:
            continue
        clean = lambda s: s.replace("|", "/")
        desc = strings.get(r.plugin, _u32(r.sub("DESC")), "dlstrings") if r.sub("DESC") and len(r.sub("DESC")) == 4 else ""
        if desc:  # (Skyrim's own words for it, shown in the shout menu: a line of theirs)
            lines.append("|".join(["D", key, clean(desc.replace("\n", " ").replace("\r", ""))]))
        lines.append("|".join(["S", key, clean(name(r))] + [clean(x) for x in words + meanings] +
                              [f"{x:g}" for x in recovery] + [f"{n[0]:g}" for n in numbers] +
                              [f"{n[1]:g}" for n in numbers] + [f"{max(n[2] for n in numbers):g}",
                                                                 f"{max(n[3] for n in numbers):g}"] +
                              [f"{e[1]:g}" for e in effects] + [f"{e[0]:g}" for e in effects] +
                              [",".join(x or "-" for x in summons)]))
    # The voice lines: each topic's info naming the shout (or power), for each player voice
    d = object.__new__(Dialogue)  # (its .fuz conversion, without its export's set-up)
    d.lo, d.pack, d.converted, d.dll = lo, pack, {}, load_adpcm()
    from collections import Counter
    d.stats = Counter()
    d.voices = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Voices_en0.bsa"))
    keys = {by_eid[e][0]: k for e, k in SHOUTS + POWERS if e in by_eid}
    files = {}  # (plugin, voice, form id) -> its voice file
    for n in d.voices.entries:
        parts = n.split("\\")
        if len(parts) == 5 and parts[4].endswith("_1.fuz"):
            try:
                files[(parts[2], parts[3], int(parts[4][:-4].rsplit("_", 2)[1], 16))] = n
            except (ValueError, IndexError):
                pass
    out_dir = os.path.join(pack, "voice", "shout")
    os.makedirs(out_dir, exist_ok=True)
    count = 0
    for p in lo.plugins:
        topics = p.topic_infos()
        for dial in p.records_of_type("DIAL"):
            line = TOPICS.get(dial.editor_id())
            if line is None:
                continue
            for info in topics.get(dial.form_id, []):
                key = None
                for t, v in info.subrecords():
                    if t == "CTDA" and len(v) >= 16 and struct.unpack_from("<H", v, 8)[0] == GET_EQUIPPED_SHOUT:
                        key = key or keys.get(lo.global_id(p, _u32(v, 12)))
                if key is None:
                    continue
                gid = lo.global_id(p, info.form_id)
                for voice in PLAYER_VOICES:
                    src = files.get((lo.plugins[gid >> 24].name.lower(), voice, gid & 0xFFFFFF))
                    if src is None or any(l.startswith(f"V|{key}|{voice}|{line}|") for l in lines):
                        continue
                    rel = d._convert(src)
                    if rel is None:
                        continue
                    final = f"shout/{voice}_{key}_{line}.skvoc"
                    os.replace(os.path.join(pack, "voice", rel), os.path.join(pack, "voice", final))
                    lines.append(f"V|{key}|{voice}|{line}|{final}")
                    count += 1
    with open(os.path.join(pack, "shouts.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print(f"shouts: {sum(1 for l in lines if l.startswith('S|'))} shouts, {count} voice lines")

