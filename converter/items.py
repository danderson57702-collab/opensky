"""Items: everything the player can carry, from Skyrim's records, for inventories, containers
and loose things lying about (Milestone: items and containers).

Items are registered as the exporters meet them (containers and their contents, loose items
in interiors) and written once at the end. Leveled item lists stay lists: the engine rolls
them (at the player's level) the first time a container is opened, as Skyrim does.

  PACK_DIR/items/items.txt   tab-separated, one record per line:
    I  index  form id (hex)  type  weight  value  name  extra  model
         (model: its model index in objects/models.txt, for the inventory's 3D view; -1)
         extra by type: ALCH "food", "potion" or "poison" then effects "av:magnitude:seconds:harmful"
         (av: 24 health, 25 magicka, 26 stamina...) then ":m", the magic effect (M line); INGR
         its four effects the same way ("av:magnitude:seconds:harmful:m"); BOOK the skill
         it teaches (actor value) or -1, then the spell (editor id, name) or "- -"; ARMO armour rating; WEAP damage; AMMO damage, flight
         speed (m/s), gravity, flight model (index, -1); SCRL its spell: charge seconds,
         delivery (0 self, 2 aimed, 4 a place), effect (fire_aimed, frost_aimed, shock_aimed,
         fire_self, frost_self, heal, rune_fire / frost / shock, muffle, armour; none), magnitude,
         area (m), seconds; SLGM its soul (0 empty .. 5 grand) and the largest it holds
    L  index  flags  chance none  entries "level:kind:index:count" (kind I item, L list)
    G  index  global              a list's chance of nothing set by the game's global (Skyrim's LVLG:
                                  PerkGoldenTouch, SpecialLootChance, PCDestructionAdept...): the engine's
         flags: 1 all levels up to the player's, 2 each of a count rolled separately, 4 all
    C  index  form id (hex)  name  entries "kind:index:count"
    H  index  an enchanted weapon's charge: "capacity cost" (Skyrim's EAMT and ENIT)
    K  index  keywords (hex, space-separated: perks ask about weapons' and armour's, merchants
         buy by them - VendorItemPotion...)
    T  index  a staff's spell (its enchantment), as a SCRL's: charge delivery kind magnitude area
         seconds extra - each blow of the staff delivers it
    E  index  its enchantment (a weapon's on each blow, armour's while worn): effects as a
         potion's then ":r", the actor value resisting it (41 fire, 42 shock, 43 frost, 44
         magic, -1) - the Bow of Embers' "24:10:0:1:57:41"
    M  index  editor id  flags  base cost  archetype  av  keywords  name  description
         Skyrim's magic effects (MGEF) that potions, food and ingredients carry: flags its DATA
         flags (4 detrimental, 2 recover, 0x200000 power affects magnitude, 0x400000 duration),
         keywords bits (1 restores health / magicka / stamina, 2 beneficial, 4 harmful: the
         alchemy perks ask), description with Skyrim's <mag> and <dur>
    N  editor id (lowercase)  container index   (an NPC's pockets: what they carry, for
         pickpocketing; their worn outfit isn't in it)
    R  station  item made  count  parts "item:count" ...  perks needed (hex; "-": none)
         Skyrim's recipes (COBJ): 1 forge, 2 smelter, 3 tanning rack, 4 cooking pot; 5 grindstone,
         6 workbench (tempering: the item made is the one improved)
    Z  reference (hex)  item  per use  total (0: never runs out)  tools (item indices; "-": none)
         an ore vein (mined with a pickaxe) or a chopping block (a woodcutter's axe)
    V  editor id (lowercase)  container index  start hour  end hour  flags  keywords (hex)
         a merchant (Skyrim's vendor faction): its chest, the hours it trades, what it buys -
         items with one of the keywords (flags 1: all but those), 2 stolen goods only
  Gold is the MISC Gold001 (weight 0, value 1).
  PACK_DIR/items/books.txt   per book: item index, TAB, its text (\\n lines, \\f pages)
"""
import os
import re
import struct

import numpy as np

ITEM_TYPES = ("MISC", "WEAP", "ARMO", "ALCH", "INGR", "BOOK", "KEYM", "AMMO", "SLGM", "SCRL", "LIGH")
LIGHT_CARRIED = 0x2  # LIGH DATA flags: can be carried (torches)
ALCH_FOOD = 0x2       # ENIT flags
ALCH_POISON = 0x20000 # ... (a poison: put on a weapon, not drunk)
BOOK_TEACHES_SKILL, BOOK_TEACHES_SPELL = 0x1, 0x4  # BOOK DATA flags
MGEF_HARMFUL = 0x4    # MGEF DATA flags: detrimental


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


class Database:
    def __init__(self, load_order, strings):
        self.lo, self.strings = load_order, strings
        self.items, self.item_index = [], {}   # [(gid, record)], gid -> index
        self.lists, self.list_index, self.list_globals = [], {}, {}
        self.containers, self.container_index = [], {}
        self.npcs = []  # (editor id, container index)
        self.vendor_npcs = []  # (editor id, vendor FACT): merchants, their chests found when written
        self.resources = {}  # ore veins, chopping blocks (interiors.py): ref -> (item, per use, total, tools)
        self._mgef = {}
        self.mgefs, self.mgef_index = [], {}  # [(gid, record)], gid -> index

    def resume(self, pack):
        """Loads existing item, leveled list and container indices from pack/items/items.txt
        so incremental re-exports (quests, interiors, player gear) keep exact indices."""
        path = os.path.join(pack, "items", "items.txt")
        if not os.path.isfile(path):
            return
        self.container_names = getattr(self, "container_names", {})
        self._resumed_vendor_lines = []
        mgefs_by_eid = {}
        for p in self.lo.plugins:
            for r in p.records_of_type("MGEF"):
                if r.editor_id():
                    mgefs_by_eid[r.editor_id().lower()] = (self.lo.global_id(p, r.form_id), r)
        with open(path, encoding="cp1252", errors="replace") as f:
            for line in f:
                raw_line = line.rstrip("\r\n")
                parts = raw_line.split("\t")
                if not parts:
                    continue
                if parts[0] == "I" and len(parts) >= 3:
                    idx = int(parts[1])
                    gid = int(parts[2], 16)
                    rec = self.record(gid)
                    while len(self.items) <= idx:
                        self.items.append((0, None))
                    self.items[idx] = (gid, rec)
                    self.item_index[gid] = idx
                elif parts[0] == "L" and len(parts) >= 4:
                    idx = int(parts[1])
                    flags, chance = int(parts[2]), int(parts[3])
                    entries = []
                    if len(parts) >= 5 and parts[4]:
                        for tok in parts[4].split():
                            lv, k, i, n = tok.split(":")
                            entries.append((int(lv), k, int(i), int(n)))
                    while len(self.lists) <= idx:
                        self.lists.append((0, 0, []))
                    self.lists[idx] = (flags, chance, entries)
                elif parts[0] == "G" and len(parts) >= 3:
                    self.list_globals[int(parts[1])] = parts[2]
                elif parts[0] == "C" and len(parts) >= 4:
                    idx = int(parts[1])
                    gid = int(parts[2], 16)
                    rec = self.lo.record(gid, types=("CONT", "FLOR", "NPC_")) if gid else None
                    entries = []
                    if len(parts) >= 5 and parts[4]:
                        for tok in parts[4].split():
                            k, i, n = tok.split(":")
                            entries.append((k, int(i), int(n)))
                    while len(self.containers) <= idx:
                        self.containers.append((0, None, []))
                    self.containers[idx] = (gid or None, rec, entries)
                    self.container_names[idx] = parts[3]
                    if gid:
                        self.container_index[gid] = idx
                elif parts[0] == "N" and len(parts) >= 3:
                    self.npcs.append((parts[1], int(parts[2])))
                elif parts[0] == "V":
                    self._resumed_vendor_lines.append(raw_line)
                elif parts[0] == "Z" and len(parts) >= 6:
                    ref = int(parts[1], 16)
                    tools = [] if parts[5] == "-" else [int(x) for x in parts[5].split()]
                    self.resources[ref] = (int(parts[2]), int(parts[3]), int(parts[4]), tools)
                elif parts[0] == "M" and len(parts) >= 3:
                    idx = int(parts[1])
                    eid = parts[2].lower()
                    pair = mgefs_by_eid.get(eid)
                    while len(self.mgefs) <= idx:
                        self.mgefs.append((0, None))
                    if pair:
                        self.mgefs[idx] = pair
                        self.mgef_index[pair[0]] = idx
        # Map existing LVLI records in load order to their resumed list indices by matching structure
        by_sig = {}
        for idx, (flags, chance, entries) in enumerate(self.lists):
            by_sig.setdefault((flags, chance, tuple(entries)), []).append(idx)
        for p in self.lo.plugins:
            for r in p.records_of_type("LVLI"):
                gid = self.lo.global_id(p, r.form_id)
                if gid is None or gid in self.list_index:
                    continue
                flags = r.sub("LVLF")[0] if r.sub("LVLF") else 0
                chance = r.sub("LVLD")[0] if r.sub("LVLD") else 0
                entries = []
                ok = True
                for t, v in r.subrecords():
                    if t == "LVLO" and len(v) >= 12:
                        level, ref, count = struct.unpack_from("<H2xIH", v)
                        egid = self.lo.global_id(r.plugin, ref)
                        if egid in self.item_index:
                            entries.append((level, "I", self.item_index[egid], max(count, 1)))
                        else:
                            ok = False
                            break
                if ok:
                    cands = by_sig.get((flags, chance, tuple(entries)))
                    if cands:
                        self.list_index[gid] = cands[0]

    # --- registration ---------------------------------------------------------------------

    def record(self, gid):
        return self.lo.record(gid, types=ITEM_TYPES + ("LVLI",))

    def is_item(self, rec):
        if rec is None or rec.type not in ITEM_TYPES:
            return False
        if rec.type == "LIGH":
            data = rec.sub("DATA")
            return bool(data and len(data) >= 16 and _u32(data, 12) & LIGHT_CARRIED)
        return bool(rec.sub("FULL"))

    def entry(self, gid):
        """('I', index) or ('L', index) for an item or leveled list (registering it), or None."""
        rec = self.record(gid)
        if rec is None:
            return None
        if rec.type == "LVLI":
            return ("L", self.list_for(gid, rec))
        if not self.is_item(rec):
            return None
        return ("I", self.item_for(gid, rec))

    def item_for(self, gid, rec=None):
        if gid not in self.item_index:
            rec = rec or self.record(gid)
            if not self.is_item(rec):
                return None
            self.item_index[gid] = len(self.items)
            self.items.append((gid, rec))
        return self.item_index[gid]

    def list_for(self, gid, rec):
        if gid in self.list_index:
            return self.list_index[gid]
        self.list_index[gid] = idx = len(self.lists)
        self.lists.append(None)  # (filled below: lists can contain themselves)
        entries = []
        for t, v in rec.subrecords():
            if t == "LVLO" and len(v) >= 12:
                level, ref, count = struct.unpack_from("<H2xIH", v)
                e = self.entry(self.lo.global_id(rec.plugin, ref))
                if e is not None:
                    entries.append((level, e[0], e[1], max(count, 1)))
        flags = rec.sub("LVLF")
        chance = rec.sub("LVLD")
        self.lists[idx] = (flags[0] if flags else 0, chance[0] if chance else 0, entries)
        g = rec.sub("LVLG")  # (its chance of nothing from a global the game sets: a perk's, a skill's, a race's)
        glob = self.lo.record(self.lo.global_id(rec.plugin, struct.unpack_from("<I", g)[0]), types=("GLOB",)) if g else None
        if glob is not None:
            self.list_globals[idx] = glob.editor_id()
        return idx

    def container_for(self, gid):
        """The container record's index (registering its contents), or None."""
        if gid in self.container_index:
            return self.container_index[gid]
        rec = self.lo.record(gid, types=("CONT",))
        if rec is None:
            return None
        entries = []
        for t, v in rec.subrecords():
            if t == "CNTO" and len(v) >= 8:
                ref, count = struct.unpack_from("<Ii", v)
                e = self.entry(self.lo.global_id(rec.plugin, ref))
                if e is not None and count > 0:
                    entries.append((e[0], e[1], count))
        self.container_index[gid] = len(self.containers)
        self.containers.append((gid, rec, entries))
        return self.container_index[gid]

    def flora_for(self, gid, rec):
        """A harvestable thing (Skyrim's flora: coin purses, plants) as a container holding
        what it gives (PFIG: an ingredient or a leveled list, e.g. a purse's gold), or None."""
        if gid in self.container_index:
            return self.container_index[gid]
        pfig = rec.sub("PFIG")
        e = self.entry(self.lo.global_id(rec.plugin, _u32(pfig))) if pfig and len(pfig) >= 4 else None
        if e is None:
            return None
        self.container_index[gid] = len(self.containers)
        self.containers.append((gid, rec, [(e[0], e[1], 1)]))
        return self.container_index[gid]

    def npc_pockets(self, npc, eid):
        """An NPC's inventory (its CNTO items and leveled lists) as a container."""
        entries = []
        for t, v in npc.subrecords():
            if t == "CNTO" and len(v) >= 8:
                ref, count = struct.unpack_from("<Ii", v)
                e = self.entry(self.lo.global_id(npc.plugin, ref))
                if e is not None and count > 0:
                    entries.append((e[0], e[1], count))
        idx = len(self.containers)
        self.containers.append((None, npc, entries))
        self.npcs.append((eid.lower(), idx))
        return idx

    def npc_vendor(self, npc, eid):
        """A merchant (an NPC in a vendor faction: its VENC chest): kept for write()."""
        for t, v in npc.subrecords():
            if t == "SNAM" and len(v) >= 4:
                fact = self.lo.record(self.lo.global_id(npc.plugin, _u32(v)), types=("FACT",))
                if fact is not None and fact.sub("VENC") and fact.sub("VEND"):
                    self.vendor_npcs.append((eid.lower(), fact))
                    return

    # Crafting stations by their keyword (interiors.py's STATIONS): the ones that make things
    RECIPE_STATIONS = {0x88105: 1, 0xF46CE: 1, 0xA5CCE: 2, 0x7866A: 3, 0xA5CB3: 4, 0x88108: 5, 0xADB78: 6}
    HAS_PERK = 448

    def _recipe_lines(self):
        """R lines: Skyrim's recipes (COBJ) at the forge, smelter, tanning rack and cooking pot."""
        lines = []
        for p in self.lo.plugins:
            for r in p.records_of_type("COBJ"):
                b, c = r.sub("BNAM"), r.sub("CNAM")
                station = self.RECIPE_STATIONS.get(self.lo.global_id(p, _u32(b))) if b and c else None
                if not station:
                    continue
                made = self.entry(self.lo.global_id(p, _u32(c)))
                if not made or made[0] != "I":
                    continue
                count = struct.unpack("<H", r.sub("NAM1"))[0] if r.sub("NAM1") else 1
                parts, perks, ok = [], [], True
                for t, v in r.subrecords():
                    if t == "CNTO" and len(v) >= 8:
                        e = self.entry(self.lo.global_id(p, _u32(v)))
                        ok &= bool(e) and e[0] == "I"
                        if ok:
                            parts.append(f"{e[1]}:{struct.unpack_from('<i', v, 4)[0]}")
                    elif t == "CTDA" and len(v) >= 16 and struct.unpack_from("<H", v, 8)[0] == self.HAS_PERK:
                        perk = self.lo.global_id(p, _u32(v, 12))
                        if perk is not None:
                            perks.append(f"{perk:08x}")
                if ok and parts:
                    lines.append(f"R\t{station}\t{made[1]}\t{count}\t{' '.join(parts)}\t{' '.join(perks) or '-'}")
        return lines

    def _vendor_lines(self):
        """V lines: per merchant its chest (a container), hours, flags and the keywords it trades."""
        if not self.vendor_npcs and getattr(self, "_resumed_vendor_lines", None):
            return list(self._resumed_vendor_lines)
        chests = {self.lo.global_id(f.plugin, _u32(f.sub("VENC"))): f for _e, f in self.vendor_npcs}
        refs = self.lo.refs_by_id(set(chests))
        lines = []
        for eid, fact in self.vendor_npcs:
            ref = refs.get(self.lo.global_id(fact.plugin, _u32(fact.sub("VENC"))))
            base = self.lo.global_id(ref.plugin, _u32(ref.sub("NAME"))) if ref is not None and ref.sub("NAME") else None
            cidx = self.container_for(base) if base is not None else None
            if cidx is None:
                continue
            venv = fact.sub("VENV") or b"\0" * 12
            start, end = struct.unpack_from("<HH", venv)
            stolen, inverted = (venv[8], venv[9]) if len(venv) >= 10 else (0, 0)
            flst = self.lo.record(self.lo.global_id(fact.plugin, _u32(fact.sub("VEND"))), types=("FLST",))
            kws = [self.lo.global_id(flst.plugin, _u32(v)) for t, v in flst.subrecords() if t == "LNAM"] \
                if flst is not None else []
            lines.append(f"V\t{eid}\t{cidx}\t{start}\t{end}\t{(1 if inverted else 0) | (2 if stolen else 0)}\t"
                         + " ".join(f"{k:08x}" for k in kws if k is not None))
        return lines

    # --- output ---------------------------------------------------------------------------

    def name(self, rec):
        full = rec.sub("FULL")
        if full and len(full) == 4:
            return self.strings.get(rec.plugin, _u32(full), "strings") or rec.editor_id()
        return rec.editor_id()

    def _mgef_for(self, gid, rec):
        if gid not in self.mgef_index:
            self.mgef_index[gid] = len(self.mgefs)
            self.mgefs.append((gid, rec))
        return self.mgef_index[gid]

    def _effects(self, rec, resist=False):
        """Its effects, "av:magnitude:seconds:harmful:m" each (resist: then ":r", the actor value
        that resists it - 41 fire, 42 shock, 43 frost, 44 magic; an enchantment's element)."""
        out, mgef = [], None
        for t, v in rec.subrecords():
            if t == "EFID":
                gid = self.lo.global_id(rec.plugin, _u32(v))
                mgef = self.lo.record(gid, types=("MGEF",))
                m = self._mgef_for(gid, mgef) if mgef is not None else -1
            elif t == "EFIT" and mgef is not None and len(v) >= 12:
                magnitude, _area, duration = struct.unpack_from("<fII", v)
                data = mgef.sub("DATA")
                if data and len(data) >= 72:
                    av = struct.unpack_from("<i", data, 68)[0]
                    harmful = int(bool(_u32(data) & MGEF_HARMFUL))
                    out.append(f"{av}:{magnitude:g}:{duration}:{harmful}:{m}" +
                               (f":{struct.unpack_from('<i', data, 16)[0]}" if resist else ""))
                mgef = None
        return out

    KW_RESTORE = (0x42503, 0x42508, 0x42504)  # MagicAlchRestoreHealth / Magicka / Stamina
    KW_BENEFICIAL, KW_HARMFUL = 0xf8a4e, 0x42509  # MagicAlchBeneficial, MagicAlchHarmful

    def _mgef_line(self, idx, rec):
        d = rec.sub("DATA") or b"\0" * 72
        flags, cost = struct.unpack_from("<If", d)
        archetype, av = struct.unpack_from("<Ii", d, 64) if len(d) >= 72 else (0, -1)
        kw = rec.sub("KWDA") or b""
        forms = {self.lo.global_id(rec.plugin, k) for k in struct.unpack(f"<{len(kw) // 4}I", kw)}
        bits = (1 if forms & set(self.KW_RESTORE) else 0) | (2 if self.KW_BENEFICIAL in forms else 0) | \
            (4 if self.KW_HARMFUL in forms else 0)
        dnam = rec.sub("DNAM")
        desc = (self.strings.get(rec.plugin, _u32(dnam), "strings") or "") if dnam and len(dnam) == 4 else ""
        clean = lambda s: s.replace("\t", " ").replace("\n", " ").replace("\r", " ")
        return (f"M\t{idx}\t{rec.editor_id()}\t{flags:x}\t{cost:g}\t{archetype}\t{av}\t{bits}\t"
                f"{clean(self.name(rec))}\t{clean(desc)}")

    # A scroll is its spell on paper: the engine casts it as the spell of its main effect
    # (perks.py's kinds - Blizzard's storm, Harmony's calm, a summons), at the scroll's own
    # strength. Its engine-own spells' kinds besides (Candlelight, Muffle, Fire Rune: magic.c's)
    def _scroll(self, rec, enit=False):
        """A scroll's "charge delivery kind magnitude area-m seconds extra": SPIT and its main
        effect - the first that isn't a stagger, a perk's or a slow (Blizzard's storm, not its
        chill) - with a blast's nearer rings (Fire Storm's three: "75@19.81,50@7.62") or a
        summons' creature for extra; "- 0 none 0 0 0 -" if the engine can't cast it."""
        import perks
        spit = rec.sub("SPIT")
        charge = struct.unpack_from("<f", spit, 12)[0] if spit and len(spit) >= 24 else 0.0
        delivery = struct.unpack_from("<I", spit, 20)[0] if spit and len(spit) >= 24 else 0
        if enit:  # (a staff's enchantment: ENIT's charge time and target type)
            e = rec.sub("ENIT") or b""
            charge = struct.unpack_from("<f", e, 24)[0] if len(e) >= 28 else 0.0
            delivery = struct.unpack_from("<I", e, 16)[0] if len(e) >= 20 else 0
        effects, mgef = [], None
        for t, v in rec.subrecords():
            if t == "EFID":
                mgef = self.lo.record(self.lo.global_id(rec.plugin, _u32(v)), types=("MGEF",))
            elif t == "EFIT" and mgef is not None and len(v) >= 12:
                effects.append((mgef,) + struct.unpack_from("<fII", v))
                mgef = None
        main = None
        for m, magnitude, area, seconds in effects:
            d = m.sub("DATA")
            archetype, av, eid = _u32(d, 64), struct.unpack_from("<i", d, 68)[0], (m.editor_id() or "").lower()
            if archetype == 33 or eid.startswith("perk") or (archetype == 34 and av == 30):
                continue  # (a stagger, a perk's, a slow: with the main one)
            main = main or (m, magnitude, area, seconds, archetype, av, eid)
        if main is None and effects:  # (Call to Arms: all fortifies)
            m, magnitude, area, seconds = effects[0]
            d = m.sub("DATA")
            main = (m, magnitude, area, seconds, _u32(d, 64), struct.unpack_from("<i", d, 68)[0], (m.editor_id() or "").lower())
        if main is None:
            return f"{charge:g} {delivery} none 0 0 0 -"
        m, magnitude, area, seconds, archetype, av, eid = main
        if archetype == 40 and not area:  # (a hazard laid about you - Guardian Circle: its own reach, feet)
            hazard = self.lo.record(self.lo.global_id(m.plugin, _u32(m.sub("DATA"), 8)), types=("HAZD",))
            area = struct.unpack_from("<f", hazard.sub("DATA"), 4)[0] if hazard is not None else 0
        if av == 92:
            kind = "muffle"
        elif archetype == 12 and delivery == 0:
            kind = "candlelight"
        elif "rune" in eid and "fire" in eid:
            kind = "rune_fire"
        else:
            kind = perks._spell_kind(self.lo, rec, m, magnitude)
        extra = perks._spell_extra(self.lo, self.strings, m, kind) if kind != "none" else ""
        if kind.startswith("storm_"):  # (its nearer rings: the same blast's, smaller, all at once)
            extra = ",".join(f"{mg:g}@{a * 0.3048:.2f}" for e, mg, a, _s in effects
                             if e is not m and e.editor_id() and e.editor_id().lower().startswith(eid.rstrip("0123456789")))
        return f"{charge:g} {delivery} {kind} {magnitude:g} {area * 0.3048:.2f} {seconds} {extra or '-'}"

    def _stats(self, rec):
        """(weight, value, extra) of an item record."""
        t, d = rec.type, rec.sub("DATA") or b""
        f = lambda o: struct.unpack_from("<f", d, o)[0] if len(d) >= o + 4 else 0.0
        i = lambda o: struct.unpack_from("<i", d, o)[0] if len(d) >= o + 4 else 0
        if t == "ALCH":
            enit = rec.sub("ENIT") or b"\0" * 8
            value, flags = struct.unpack_from("<iI", enit)
            kind = "food" if flags & ALCH_FOOD else "poison" if flags & ALCH_POISON else "potion"
            return f(0), value, " ".join([kind] + self._effects(rec))
        if t == "BOOK":  # the skill it teaches (actor value, -1), the spell (editor id and name, "-")
            flags = d[0] if d else 0
            skill = struct.unpack_from("<i", d, 4)[0] if len(d) >= 8 and flags & BOOK_TEACHES_SKILL else -1
            spell = self.lo.record(self.lo.global_id(rec.plugin, _u32(d, 4)), types=("SPEL",)) \
                if len(d) >= 8 and flags & BOOK_TEACHES_SPELL else None
            spell_text = f"{spell.editor_id()} {self.name(spell)}" if spell is not None else "- -"
            return f(12), i(8), f"{skill} {spell_text}"
        if t == "ARMO":
            dnam = rec.sub("DNAM")
            return f(4), i(0), str(struct.unpack("<i", dnam)[0] // 100 if dnam else 0)
        if t == "WEAP":
            return f(4), i(0), str(struct.unpack_from("<H", d, 8)[0] if len(d) >= 10 else 0)
        if t == "AMMO":  # damage, then its flight (the projectile's speed m/s, gravity, model)
            proj = self.lo.record(self.lo.global_id(rec.plugin, _u32(d)), types=("PROJ",)) if len(d) >= 4 else None
            pd = proj.sub("DATA") if proj is not None else None
            # (PROJ DATA: u16 flags, u16 type, f32 gravity, f32 speed, f32 range)
            speed, gravity = (struct.unpack_from("<f", pd, 8)[0] / 70.0, struct.unpack_from("<f", pd, 4)[0]) \
                if pd and len(pd) >= 16 else (50.0, 0.35)
            model = proj.sub("MODL") if proj is not None else None
            self._flight = model.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\") if model else None
            return f(16), i(12), f"{f(8):g} {speed:.1f} {gravity:g}"
        if t == "LIGH":
            return f(44), i(40), ""  # (after the flicker settings)
        if t == "SCRL":
            return f(4), i(0), self._scroll(rec)
        if t == "INGR":  # (its four effects, as a potion's)
            return f(4), i(0), " ".join(["ingredient"] + self._effects(rec))
        if t == "SLGM":  # (its soul: Skyrim's 0 empty .. 5 grand, and the largest it holds)
            soul, cap = rec.sub("SOUL"), rec.sub("SLCP")
            return f(4), i(0), f"{soul[0] if soul else 0} {cap[0] if cap else 0}"
        return f(4), i(0), ""  # MISC, KEYM: value, weight

    def model_path(self, rec):
        m = rec.sub("MOD2") if rec.type == "ARMO" else rec.sub("MODL")  # (armour: its ground model)
        if not m or len(m) < 5:
            return None
        return m.rstrip(b"\0").decode("cp1252", "replace").lower().replace("/", "\\")

    def book_text(self, rec):
        """A book's words as plain text: paragraphs on lines, pages split by form feeds (Skyrim's
        [pagebreak]); its markup (<p>, <font>, <img>...) left out."""
        desc = rec.sub("DESC")
        text = self.strings.get(rec.plugin, _u32(desc), "dlstrings") if desc and len(desc) == 4 else ""
        text = (text or "").replace("\r\n", "\n").replace("\r", "\n")
        text = re.sub(r"(?i)<br\s*/?>", "\n", text)
        text = re.sub(r"(?i)\[pagebreak\]", "\f", text)
        text = re.sub(r"<[^>]*>", "", text)
        for a, b in (("&lt;", "<"), ("&gt;", ">"), ("&quot;", '"'), ("&amp;", "&"), ("&nbsp;", " ")):
            text = text.replace(a, b)
        pages = [re.sub(r"\n{3,}", "\n\n", pg).strip("\n ") for pg in text.split("\f")]
        return "\f".join(pg for pg in pages if pg)

    def _write_books(self, out_dir):
        """items/books.txt: per book, "<item index><TAB><text>" (newlines as \\n, pages split by \\f)."""
        lines = []
        for idx, (gid, rec) in enumerate(self.items):
            if rec.type != "BOOK":
                continue
            text = self.book_text(rec).replace("\\", "/").replace("\t", " ")
            lines.append(f"{idx}\t" + text.replace("\n", "\\n").replace("\f", "\\f"))
        with open(os.path.join(out_dir, "books.txt"), "w", encoding="cp1252", errors="replace") as f:
            f.write("\n".join(lines) + "\n")
        print(f"items: {len(lines)} books")

    def _write_inventory_views(self, out_dir, models):
        """PACK_DIR/items/inventory.txt: "<model> m00 m01 .. m22" per item model with a
        BSInvMarker: how Skyrim's inventory turns it (engine space, from the front)."""
        from extract_textures import find
        from nif import NIF
        C = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], np.float64)  # Skyrim -> engine axes
        cw = lambda axis, a: np.array([[1, 0, 0], [0, np.cos(a), np.sin(a)], [0, -np.sin(a), np.cos(a)]]) if axis == 0 \
            else np.array([[np.cos(a), 0, -np.sin(a)], [0, 1, 0], [np.sin(a), 0, np.cos(a)]]) if axis == 1 \
            else np.array([[np.cos(a), np.sin(a), 0], [-np.sin(a), np.cos(a), 0], [0, 0, 1]])
        # (Gamebryo's clockwise X, Y, Z rotations; the inventory looks at the item from its
        # front, +Y: the engine's camera looks down -Z from +Z, so turned half round)
        turn = np.diag([-1.0, 1.0, -1.0])
        lines, done = [], set()
        for gid, rec in self.items:
            path = self.model_path(rec)
            idx = models.index_for(path) if path else None
            if idx is None or idx in done:
                continue
            done.add(idx)
            try:
                marker = NIF(find(models.mesh_archives, "meshes\\" + path)).inventory_marker()
            except (KeyError, ValueError, IndexError, struct.error):
                marker = None
            if marker is None:
                continue
            R = cw(0, marker[0]) @ cw(1, marker[1]) @ cw(2, marker[2])
            M = turn @ C @ R @ C.T
            lines.append(f"{idx} " + " ".join(f"{v:.4f}" for v in M.reshape(-1)))
        with open(os.path.join(out_dir, "inventory.txt"), "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"items: {len(lines)} inventory views")

    def write(self, pack, models=None):
        """models: extract_objects.ModelConverter, to give each item its model (converting
        those not converted yet; write the model table after this)."""
        out_dir = os.path.join(pack, "items")
        os.makedirs(out_dir, exist_ok=True)
        clean = lambda s: s.replace("\t", " ").replace("\n", " ")
        for p in self.lo.plugins:  # (every ingredient and potion: the alchemy menu lists what you carry,
            for t in ("INGR", "ALCH"):  # and quests, cooking and the kit hand out ones never placed)
                for rec in p.records_of_type(t):
                    gid = self.lo.global_id(p, rec.form_id)
                    if gid is not None and rec.sub("FULL"):
                        self.item_for(gid, self.lo.record(gid, types=(t,)))
            for rec in p.records_of_type("MISC"):  # (all dragon claw variants: needed for puzzle doors)
                edid = (rec.editor_id() or "").lower()
                if "claw" in edid and rec.sub("FULL"):
                    gid = self.lo.global_id(p, rec.form_id)
                    if gid is not None:
                        self.item_for(gid, self.lo.record(gid, types=("MISC",)))
        vendor_lines = self._vendor_lines() + self._recipe_lines()  # (first: they may bring items in)
        lines = []
        for idx, (gid, rec) in enumerate(self.items):
            self._flight = None
            weight, value, extra = self._stats(rec)
            if rec.type == "AMMO":  # (its flight model, converted)
                flight = models.index_for(self._flight) if models is not None and self._flight else None
                extra += f" {flight if flight is not None else -1}"
            path = self.model_path(rec) if models is not None else None
            model = models.index_for(path) if path else None
            lines.append(f"I\t{idx}\t{gid:08x}\t{rec.type}\t{weight:g}\t{value}\t{clean(self.name(rec))}\t{extra}"
                         f"\t{model if model is not None else -1}")
        for idx, (gid, rec) in enumerate(self.items):  # (keywords: perks ask; merchants buy by them)
            kw = rec.sub("KWDA")
            if kw:
                forms = [self.lo.global_id(rec.plugin, k) for k in struct.unpack(f"<{len(kw) // 4}I", kw)]
                lines.append(f"K\t{idx}\t" + " ".join(f"{k:08x}" for k in forms[:12]))
        for idx, (gid, rec) in enumerate(self.items):  # (enchanted weapons and armour: EITM, the ENCH)
            eitm = rec.sub("EITM") if rec.type in ("WEAP", "ARMO") else None
            ench = self.lo.record(self.lo.global_id(rec.plugin, _u32(eitm)), types=("ENCH",)) \
                if eitm and len(eitm) >= 4 else None
            effects = self._effects(ench, resist=True) if ench is not None else []
            if effects:
                lines.append(f"E\t{idx}\t" + " ".join(effects))
            dnam = rec.sub("DNAM") if rec.type == "WEAP" else None
            if ench is not None and dnam and dnam[0] == 8:  # (a staff: its spell, as a scroll's - each blow delivers it)
                lines.append(f"T\t{idx}\t" + self._scroll(ench, enit=True))
            eamt = rec.sub("EAMT") if rec.type == "WEAP" else None
            if effects and eamt and len(eamt) >= 2 and ench is not None:
                # (its charge: Skyrim's EAMT, and a strike's cost - the ENCH's ENIT cost, else its effects' own)
                capacity = struct.unpack_from("<H", eamt)[0]
                enit = ench.sub("ENIT") or b""
                cost = float(struct.unpack_from("<i", enit)[0]) if len(enit) >= 4 else 0.0
                if cost <= 0.0:
                    cost = max(1.0, capacity / 30.0)  # (an auto-calculated one: about thirty strikes)
                if capacity > 0:
                    lines.append(f"H\t{idx}\t{capacity} {cost:g}")
        lines += vendor_lines
        for ref, (item, per_use, total, tools) in sorted(self.resources.items()):
            lines.append(f"Z\t{ref:08x}\t{item}\t{per_use}\t{total}\t" + (" ".join(map(str, tools)) or "-"))
        for idx, (flags, chance, entries) in enumerate(self.lists):
            lines.append(f"L\t{idx}\t{flags}\t{chance}\t" + " ".join(f"{lv}:{k}:{i}:{n}" for lv, k, i, n in entries))
        for idx, glob in sorted(self.list_globals.items()):  # (Golden Touch's gold, Treasure Hunter's, the tomes a skill opens)
            lines.append(f"G\t{idx}\t{glob}")
        for idx, (gid, rec, entries) in enumerate(self.containers):
            name = getattr(self, "container_names", {}).get(idx) or self.name(rec)  # (a body: its kind's name)
            lines.append(f"C\t{idx}\t{(gid or 0):08x}\t{clean(name)}\t" + " ".join(f"{k}:{i}:{n}" for k, i, n in entries))
        for eid, idx in self.npcs:
            lines.append(f"N\t{eid}\t{idx}")
        for idx, (gid, rec) in enumerate(self.mgefs):
            lines.append(self._mgef_line(idx, rec))
        with open(os.path.join(out_dir, "items.txt"), "w", encoding="cp1252", errors="replace") as f:
            f.write("\n".join(lines) + "\n")
        self._write_books(out_dir)
        if models is not None:
            self._write_inventory_views(out_dir, models)
        print(f"items: {len(self.items)} items, {len(self.lists)} leveled lists, {len(self.containers)} containers")
