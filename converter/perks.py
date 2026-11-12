"""Skyrim's perk trees and the player's spells, for the engine's level-ups, skill trees and
magic.

Each skill's actor value record (AVIF) lays out its perk tree: nodes (PNAM the perk, XNAM /
YNAM its grid square, HNAM / VNAM a nudge within it, CNAM the nodes it leads to, INAM its
index); node 0 is the tree's root. A perk with ranks chains them through NNAM. A perk's own
conditions (CTDA) say what it takes: a skill level (GetBaseActorValue) and other perks
(HasPerk). Its effects (PRKE) are entry points (a value of the game's - attack damage, spell
cost, armour rating... - set, added to or multiplied when the entry's conditions hold) or
abilities (a spell whose effects raise actor values: carry weight, magicka regeneration...).

  PACK_DIR/perks.txt   lines (space-separated unless noted):
    N skill node x y links...   a tree node: grid position (x right, y up; the nudges added),
                                the nodes it leads to; its perks follow as R lines
    R skill node perk           the node's ranks, in order (perk: index into the P lines)
    P perk form_id level |name|description
                                level: the skill level it needs; the other perks it needs
                                are the tree's links (as in Skyrim's menu)
    Q perk other                it needs that perk too (its HasPerk conditions)
    E perk entry function value conditions...
                                an entry point: function 1 set, 2 add, 3 multiply; then per
                                condition "tab:func:op:value:param:or" (tab 0 the player,
                                1 the weapon / spell / item, 2 the target; op 0 ==, 1 !=,
                                2 >, 3 >=, 4 <, 5 <=; param a form id or actor value, hex)
    A perk av value             an ability: actor value av raised by value
  PACK_DIR/spells.txt  lines: key|name|school av|cost|magnitude|duration s|casting perk id|
                       keywords (hex, space-separated)|area m|dual
                       (the engine's own seven), then the rest the spell tomes teach:
                       X|key|name|school av|cost|magnitude|duration s|casting perk id|keywords|
                       area m|kind|charge s|extra|dual
                       dual: dual casting's "scale:what" (what: 1 magnitude, 2 duration - see _dual)
                       kind: how the engine casts it, from its first magic effect - bolt_fire /
                       bolt_frost / bolt_shock (aimed, fire-and-forget; area: a burst), stream_frost
                       (held), rune_frost / rune_shock, heal (yourself, at once), armour (the
                       flesh spells), invisibility, waterbreathing, light (Magelight: where it
                       strikes), cloak_fire / cloak_frost / cloak_shock (burning those about you),
                       storm_fire / storm_frost / storm_shock (a blast all about you),
                       fear, calm, paralyze, turn (the undead flee) - magnitude: the highest level
                       they work on (0: any); none: not castable here yet
"""
import os
import struct

UNITS_PER_METER = 70.0
SKILL_AVIF = range(0x44C, 0x44C + 18)  # Skyrim.esm's skill records, One-handed .. Enchanting
GET_BASE_AV, HAS_PERK = 277, 448
PLAYER_SPELLS = ("Candlelight", "Healing", "Flames", "Sparks", "FireRune", "Muffle", "BoundSword")


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _f32(b, o=0):
    return struct.unpack_from("<f", b, o)[0]


def _clean(s):
    return (s or "").replace("|", "/").replace("\n", " ").replace("\r", " ").strip()


def _ctda(v):
    """(op, compare value, function, param1, or flag) of a condition."""
    op_flags = v[0]
    return op_flags >> 5, _f32(v, 4), struct.unpack_from("<H", v, 8)[0], _u32(v, 12), op_flags & 1


def export(lo, pack, strings):
    perks = {lo.global_id(p, r.form_id): r for p in lo.plugins for r in p.records_of_type("PERK")}
    avif = {r.form_id: r for r in lo.plugins[0].records_of_type("AVIF")}

    def name(r):
        f = r.sub("FULL")
        return strings.get(r.plugin, _u32(f), "strings") if f and len(f) == 4 else r.editor_id()

    def desc(r):
        d = r.sub("DESC")
        return strings.get(r.plugin, _u32(d), "dlstrings") if d and len(d) == 4 else ""

    lines, index = [], {}

    def perk_index(gid):
        if gid not in index:
            index[gid] = len(index)
        return index[gid]

    order = []  # perks in index order, for the P / E / A lines
    for skill, fid in enumerate(SKILL_AVIF):
        nodes, cur = {}, None
        for t, v in avif[fid].subrecords():
            if t == "PNAM":
                cur = {"perk": _u32(v), "links": []}
            elif cur is None:
                continue
            elif t == "XNAM":
                cur["gx"] = _u32(v)
            elif t == "YNAM":
                cur["gy"] = _u32(v)
            elif t == "HNAM":
                cur["h"] = _f32(v)
            elif t == "VNAM":
                cur["v"] = _f32(v)
            elif t == "CNAM":
                cur["links"].append(_u32(v))
            elif t == "INAM":
                nodes[_u32(v)] = cur
                cur = None
        for i, n in sorted(nodes.items()):
            x = n.get("gx", 0) + n.get("h", 0.0)
            y = n.get("gy", 0) + n.get("v", 0.0)
            lines.append(f"N {skill} {i} {x:.3f} {y:.3f} " + " ".join(str(l) for l in n["links"]))
            gid, seen = n["perk"], set()
            while gid and gid in perks and gid not in seen:
                seen.add(gid)
                idx = perk_index(gid)
                if idx == len(order):
                    order.append(gid)
                lines.append(f"R {skill} {i} {idx}")
                nn = perks[gid].sub("NNAM")
                gid = _u32(nn) if nn else 0
    effects = 0
    for idx, gid in enumerate(order):
        r = perks[gid]
        level, needs, in_effect, kind, entry = 0, [], False, None, None
        out = []
        for t, v in r.subrecords():
            if t == "PRKE":
                in_effect, kind, entry = True, v[0], None
            elif t == "PRKF":
                if entry is not None and entry["value"] is not None:
                    conds = " ".join(f"{c[0]}:{c[1]}:{c[2]}:{c[3]:g}:{c[4]:x}:{c[5]}" for c in entry["conds"])
                    out.append(f"E {idx} {entry['ep']} {entry['fn']} {entry['value']:g} {conds}".rstrip())
                    effects += 1
                in_effect, entry = False, None
            elif not in_effect:
                if t == "CTDA":
                    op, value, func, param, _or = _ctda(v)
                    if func == GET_BASE_AV:
                        level = max(level, int(value))
                    elif func == HAS_PERK and op == 0 and value >= 1.0:
                        g = lo.global_id(r.plugin, param)
                        if g in perks:
                            needs.append(g)
            elif kind == 2 and t == "DATA" and len(v) >= 3:
                entry = {"ep": v[0], "fn": v[1], "value": None, "conds": [], "tab": 0}
            elif kind == 2 and entry is not None and t == "PRKC":
                entry["tab"] = v[0]
            elif kind == 2 and entry is not None and t == "CTDA":
                op, value, func, param, orf = _ctda(v)
                entry["conds"].append((entry["tab"], func, op, value, param, orf))
            elif kind == 2 and entry is not None and t == "EPFD" and len(v) >= 4:
                entry["value"] = _f32(v)
            elif kind == 1 and t == "DATA" and len(v) == 4:
                spell = lo.record(lo.global_id(r.plugin, _u32(v)), types=("SPEL",))
                mgef = None
                for tt, vv in (spell.subrecords() if spell else ()):
                    if tt == "EFID":
                        mgef = lo.record(lo.global_id(spell.plugin, _u32(vv)), types=("MGEF",))
                    elif tt == "EFIT" and mgef is not None:
                        d = mgef.sub("DATA")
                        av = struct.unpack_from("<i", d, 0x44)[0] if d and len(d) >= 0x48 else -1
                        if av >= 0:
                            out.append(f"A {idx} {av} {_f32(vv):g}")
                        mgef = None
                        break  # (the first effect: the rest are its higher ranks' copies)
        lines.append(f"P {idx} {gid:08x} {level} |{_clean(name(r))}|{_clean(desc(r))}")
        lines += [f"Q {idx} {perk_index(g)}" for g in needs if g in index]
        lines += out
    with open(os.path.join(pack, "perks.txt"), "w", encoding="cp1252", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print(f"perks: {len(order)} perks in 18 trees, {effects} entry points")
    _export_spells(lo, pack, strings)


ARCH_CALM, ARCH_DEMORALIZE, ARCH_INVISIBILITY, ARCH_LIGHT, ARCH_PARALYSIS, ARCH_TURN_UNDEAD, ARCH_CLOAK = \
    6, 7, 11, 12, 21, 24, 35
CAST_CONCENTRATION = 2
DELIVERY_SELF, DELIVERY_AIMED, DELIVERY_LOCATION = 0, 2, 4


def _spell_kind(lo, spell, mgef, magnitude):
    """How the engine casts a spell (see spells.txt X lines), from its first effect."""
    spit = spell.sub("SPIT")
    if spit:
        cast, delivery = _u32(spit, 16), _u32(spit, 20)
    else:  # (an enchantment - a staff's: ENIT's cast type and target type)
        enit = spell.sub("ENIT")
        cast, delivery = _u32(enit, 8), _u32(enit, 16)
    d = mgef.sub("DATA")
    flags, archetype, av = _u32(d), _u32(d, 64), struct.unpack_from("<i", d, 68)[0]
    eid = (mgef.editor_id() or "").lower()
    element = "fire" if "fire" in eid or "flame" in eid else "frost" if "frost" in eid or "ice" in eid \
        else "shock" if "shock" in eid or "lightning" in eid else None
    if archetype == ARCH_CLOAK and element:
        return f"cloak_{element}"
    if archetype == ARCH_INVISIBILITY:
        return "invisibility"
    if archetype == ARCH_LIGHT:
        return "light"
    if archetype == ARCH_DEMORALIZE and delivery == DELIVERY_AIMED:
        return "fear"
    if archetype == ARCH_CALM and delivery == DELIVERY_AIMED:
        return "calm"
    if archetype == ARCH_PARALYSIS and delivery == DELIVERY_AIMED:
        return "paralyze"
    if archetype == ARCH_TURN_UNDEAD:
        return "turn"
    if av == 57 and delivery == DELIVERY_SELF:  # (WaterBreathing)
        return "waterbreathing"
    if av == 39 and delivery == DELIVERY_SELF and magnitude > 0:  # (DamageResist: Oakflesh .. Ebonyflesh)
        return "armour"
    if av == 24 and delivery == DELIVERY_SELF and not flags & 0x4 and cast != CAST_CONCENTRATION:
        return "heal"
    if element and "damage" in eid and delivery == DELIVERY_SELF and cast != CAST_CONCENTRATION:
        return f"storm_{element}"  # (Fire Storm: a blast all about you)
    if element and "rune" in eid and delivery == DELIVERY_LOCATION and element != "fire":
        return f"rune_{element}"
    if element and "damage" in eid and delivery == DELIVERY_AIMED and "wall" not in eid:
        if cast == CAST_CONCENTRATION:
            # (Flames and Sparks are the engine's own; another held stream of fire or lightning -
            # Lightning Storm - is cast as its bolt)
            return f"stream_{element}" if element == "frost" else \
                "none" if (spell.editor_id() or "").lower() in ("flames", "sparks") else f"bolt_{element}"
        return f"bolt_{element}"
    # The rest by what they are, cast as the engine's nearest (Skyrim's own effect where it has
    # one; else the effect closest to it - told apart here, so the tome isn't one you can't use)
    sid = (spell.editor_id() or "").lower() + " " + eid
    # Conjuration's and the rest the engine has its own way for (engine/src/magic.c)
    if archetype == 18:
        return "summon"        # (its creature: _spell_extra)
    if archetype == 22:
        return "reanimate"
    if archetype == 17:
        return "bound"         # (its weapon: _spell_extra)
    if archetype == 32:
        return "ward"
    if archetype == 19:
        return "detect"
    if archetype == 20:
        return "telekinesis"
    if archetype in (34, 38):
        return "rally"         # (Courage, Rally, Call to Arms)
    if archetype == 10:
        return "calm"          # (Command Daedra: it stops fighting you)
    if "transmute" in sid:
        return "transmute"
    if "soultrap" in sid:
        return "soultrap"
    if "ashguardian" in sid:
        return "summon"
    if "damagehealthconcself" in sid or "equilibrium" in sid:
        return "equilibrium"
    if archetype == 0 and av == 24 and delivery in (2, 3) and not flags & 0x4:
        return "heal_other"    # (Heal Other, Healing Hands, Heal Undead, Necromantic Healing)
    if archetype == ARCH_DEMORALIZE:
        return "fear"      # (Hysteria: all about you - here at what you aim at)
    if archetype == ARCH_CALM:
        return "calm"      # (Harmony)
    if archetype == ARCH_PARALYSIS:
        return "paralyze"  # (Mass Paralysis)
    if any(w in sid for w in ("frenzy", "fury", "mayhem")):
        return "rune_shock" if "rune" in sid else "frenzy"
    if "clairvoyance" in sid:
        return "clairvoyance"
    if "dragonhide" in sid:
        return "armour"
    if archetype == ARCH_CLOAK:
        return "cloak_fire" if "stendarr" in sid or "sun" in sid else "cloak_shock"  # (Stendarr's Aura, Whirlwind Cloak)
    if "rune" in sid and delivery == DELIVERY_LOCATION:
        return "rune_frost" if "frost" in sid else "rune_shock"  # (Poison Rune, Ash Rune)
    if any(w in sid for w in ("sunfire", "sun fire", "vampiresbane", "sundamage", "flamingfamiliar", "familiarexplo")):
        return "bolt_fire"
    if "spectralarrow" in sid:
        return "bolt_shock"
    if element and "wall" in sid:
        return f"bolt_{element}"  # (Wall of Flames / Frost / Storms: its fire thrown, not laid)
    if any(w in sid for w in ("baneoftheundead", "circleofprotection", "guardiancircle", "expeldaedra", "banishdaedra")):
        return "turn"
    return "none"


SUMMON_STAND_INS = (("atronach", "atronach_flame"), ("thrall", "atronach_flame"), ("dremora", "wrathman"),
                    ("seeker", "mistman"), ("ashguardian", "ashspawn"), ("ash", "ashspawn"))


def _summon_stand_in(effect_id, made):
    """A summons whose creature the converter can't yet make (the Storm and Frost Atronachs,
    the Dremora Lord, the Seeker: their clip sets aren't mapped): the nearest it can - told
    apart here so they're something of their own kind, not all the engine's wolf."""
    for word, kind in SUMMON_STAND_INS:
        if word in effect_id and kind in made:
            return kind
    return ""


def _spell_extra(lo, strings, mgef, kind):
    """What a spell's kind needs besides: a summons' creature kind (creatures.txt), a bound
    weapon's item name, "undead" for Detect Dead."""
    d = mgef.sub("DATA")
    assoc = lo.global_id(mgef.plugin, _u32(d, 8)) if _u32(d, 8) else None
    if kind == "summon":
        import creatures
        made = creatures.SUMMON_KINDS.get(assoc, "")
        for word, name in creatures.SUMMON_BY_WORD.items():
            if not made and word in (mgef.editor_id() or "").lower():
                made = name
        return made or _summon_stand_in((mgef.editor_id() or "").lower(), set(creatures.SUMMON_KINDS.values()))
    if kind == "bound" and assoc is not None:
        weapon = lo.record(assoc, types=("WEAP",))
        full = weapon.sub("FULL") if weapon is not None else None
        return _clean(strings.get(weapon.plugin, _u32(full), "strings")) if full and len(full) == 4 else ""
    if kind == "detect":
        return "undead" if "undead" in (mgef.editor_id() or "").lower() else ""
    return ""


def _dual(mgef_data):
    """Dual casting (Skyrim's: its school's Dual Casting perk, the same spell in both hands): the
    effect's dual casting scale and what its power raises - "scale:what", what 1 magnitude, 2
    duration (MGEF flags Power Affects Magnitude / Duration)."""
    flags = _u32(mgef_data)
    scale = _f32(mgef_data, 112) if len(mgef_data) >= 116 else 1.0
    return f"{scale:g}:{(1 if flags & 0x200000 else 0) | (2 if flags & 0x400000 else 0)}"


def _tome_spells(lo, strings):
    """X lines: every spell a spell tome teaches but the engine's own seven."""
    out, seen = [], set(PLAYER_SPELLS)
    for p in lo.plugins:
        for book in p.records_of_type("BOOK"):
            data = book.sub("DATA")
            if not data or len(data) < 8 or not data[0] & 0x4:  # (teaches a spell)
                continue
            spell = lo.record(lo.global_id(book.plugin, _u32(data, 4)), types=("SPEL",))
            if spell is None or spell.editor_id() in seen:
                continue
            seen.add(spell.editor_id())
            spit = spell.sub("SPIT")
            cost, charge, perk = _u32(spit), _f32(spit, 12), _u32(spit, 32)
            full = spell.sub("FULL")
            name = strings.get(spell.plugin, _u32(full), "strings") if full and len(full) == 4 else spell.editor_id()
            mgef, school, magnitude, duration, area, keywords, kind = None, -1, 0.0, 0, 0.0, [], "none"
            extra, dual = "", "1:1"
            for t, v in spell.subrecords():
                if t == "EFID" and mgef is None:
                    mgef = lo.record(lo.global_id(spell.plugin, _u32(v)), types=("MGEF",))
                elif t == "EFIT" and mgef is not None and school < 0:
                    magnitude, area_u, duration = struct.unpack_from("<fII", v)
                    d = mgef.sub("DATA")
                    school = struct.unpack_from("<i", d, 12)[0]
                    kw = mgef.sub("KWDA")
                    keywords = [lo.global_id(mgef.plugin, k) for k in struct.unpack(f"<{len(kw) // 4}I", kw)] if kw else []
                    area = area_u * 0.3048  # (feet)
                    kind = _spell_kind(lo, spell, mgef, magnitude)
                    extra = _spell_extra(lo, strings, mgef, kind)
                    dual = _dual(d)
            if kind.startswith("storm_"):  # (Fire Storm: its nearer rings, the same blast's - "75@19.81,50@7.62")
                eid = (mgef.editor_id() or "").lower().rstrip("0123456789")
                rings, m = [], None
                for t, v in spell.subrecords():
                    if t == "EFID":
                        m = lo.record(lo.global_id(spell.plugin, _u32(v)), types=("MGEF",))
                    elif t == "EFIT" and m is not None and m is not mgef and (m.editor_id() or "").lower().startswith(eid):
                        mg, a = struct.unpack_from("<fI", v)
                        if m.editor_id() != mgef.editor_id():
                            rings.append(f"{mg:g}@{a * 0.3048:.2f}")
                extra = ",".join(rings)
            perk_gid = lo.global_id(spell.plugin, perk) if perk else 0
            out.append(f"X|{spell.editor_id()}|{_clean(name)}|{school}|{cost}|{magnitude:g}|{duration}|{perk_gid:08x}|"
                       + " ".join(f"{k:08x}" for k in keywords) + f"|{area:.2f}|{kind}|{charge:g}|{extra}|{dual}")
    return out


def _export_spells(lo, pack, strings):
    by_eid = {r.editor_id(): r for p in lo.plugins for r in p.records_of_type("SPEL")}
    out = []
    for key in PLAYER_SPELLS:
        s = by_eid[key]
        spit = s.sub("SPIT")
        cost, perk = _u32(spit), _u32(spit, 32)
        full = s.sub("FULL")
        name = strings.get(s.plugin, _u32(full), "strings") if full and len(full) == 4 else key
        magnitude = duration = 0.0
        school, keywords, area, mgef, dual = -1, [], 0.0, None, "1:1"
        for t, v in s.subrecords():
            if t == "EFID" and mgef is None:
                mgef = lo.record(lo.global_id(s.plugin, _u32(v)), types=("MGEF",))
            elif t == "EFIT" and mgef is not None and school < 0:
                magnitude, area_u, duration = struct.unpack_from("<fII", v)
                d = mgef.sub("DATA")
                school = struct.unpack_from("<i", d, 12)[0]
                kw = mgef.sub("KWDA")
                keywords = [lo.global_id(mgef.plugin, k) for k in struct.unpack(f"<{len(kw) // 4}I", kw)] if kw else []
                area = area_u * 0.3048  # (feet)
                dual = _dual(d)
                # A rune's burst: its explosion's radius
                expl = _u32(d, 0x30) if len(d) >= 0x34 else 0
                if expl:
                    e = lo.record(lo.global_id(mgef.plugin, expl), types=("EXPL",))
                    if e is not None and e.sub("DATA") and len(e.sub("DATA")) >= 28:
                        area = _f32(e.sub("DATA"), 24) / UNITS_PER_METER
        perk_gid = lo.global_id(s.plugin, perk) if perk else 0
        out.append(f"{key}|{_clean(name)}|{school}|{cost}|{magnitude:g}|{duration}|{perk_gid:08x}|"
                   + " ".join(f"{k:08x}" for k in keywords) + f"|{area:.2f}|{dual}")
    out += _tome_spells(lo, strings)
    with open(os.path.join(pack, "spells.txt"), "w") as f:
        f.write("\n".join(out) + "\n")
    print("spells:", "; ".join(l.split("|")[1] + " " + l.split("|")[3] for l in out))
