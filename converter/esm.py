"""Reader for Skyrim SE plugin files (ESM/ESP, form version 44).

Only what world conversion needs: an index of a worldspace's exterior cells
(grid position -> CELL and LAND records), plus random access to any record by
form ID for lookups such as LTEX -> TXST -> texture path.
"""
import math
import struct
import zlib

RECORD_COMPRESSED = 0x00040000
RECORD_DELETED = 0x00000020
RECORD_INITIALLY_DISABLED = 0x00000800
CELL_UNITS = 4096
GROUP_WORLD_CHILDREN = 1
GROUP_EXTERIOR_BLOCK = 4
GROUP_EXTERIOR_SUBBLOCK = 5
GROUP_CELL_CHILDREN = 6
GROUP_CELL_PERSISTENT = 8
GROUP_CELL_TEMPORARY = 9

HEADER = struct.Struct("<4sIIIIHH")  # record: type, data size, flags, form id, vc, version, unknown
GROUP = struct.Struct("<4sI4siHHHH")  # group: "GRUP", total size, label, type, stamp, ?, version, ?


class Record:
    __slots__ = ("type", "flags", "form_id", "_raw", "_compressed", "plugin", "switch")

    def __init__(self, rtype, flags, form_id, raw):
        self.type = rtype
        self.flags = flags
        self.form_id = form_id
        self._raw = raw
        self._compressed = bool(flags & RECORD_COMPRESSED)
        self.plugin = None  # set by ESM; LoadOrder uses it to globalize form ids
        self.switch = 0     # a placed reference's world switch (LoadOrder.exterior_cells: SWITCHES)

    @property
    def data(self):
        if self._compressed:
            size = struct.unpack_from("<I", self._raw)[0]
            data = zlib.decompress(self._raw[4:])
            if len(data) != size:
                raise ValueError(f"{self.type} {self.form_id:08X}: bad decompressed size")
            return data
        return bytes(self._raw)

    def subrecords(self):
        """Yields (type, bytes) pairs, resolving XXXX large-size subrecords."""
        d = self.data
        o, big = 0, None
        while o + 6 <= len(d):
            st, size = struct.unpack_from("<4sH", d, o)
            o += 6
            if big is not None:
                size, big = big, None
            if st == b"XXXX":
                big = struct.unpack_from("<I", d, o)[0]
                o += size
                continue
            yield st.decode("ascii"), d[o:o + size]
            o += size

    def sub(self, name):
        for st, payload in self.subrecords():
            if st == name:
                return payload
        return None

    def editor_id(self):
        v = self.sub("EDID")
        return v.rstrip(b"\0").decode("cp1252") if v else ""


class ESM:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = memoryview(f.read())
        self._form_index = None
        self.name = path.replace("\\", "/").rsplit("/", 1)[-1]
        header, _ = self._record_at(0)
        self.masters = [v.rstrip(b"\0").decode("cp1252") for st, v in header.subrecords() if st == "MAST"]

    def _record_at(self, o):
        rtype, size, flags, form_id, _vc, _ver, _unk = HEADER.unpack_from(self.d, o)
        rec = Record(rtype.decode("ascii"), flags, form_id, self.d[o + 24:o + 24 + size])
        rec.plugin = self
        return rec, o + 24 + size

    def _top_groups(self):
        o = 24 + HEADER.unpack_from(self.d, 0)[1]  # skip TES4
        while o < len(self.d):
            _g, size, label, _gtype, *_ = GROUP.unpack_from(self.d, o)
            yield bytes(label).decode("ascii"), o, size
            o += size

    def records_of_type(self, rtype):
        """All top-level records of one type (e.g. LTEX, TXST, STAT)."""
        for label, start, size in self._top_groups():
            if label != rtype:
                continue
            o, end = start + 24, start + size
            while o < end:
                if bytes(self.d[o:o + 4]) == b"GRUP":
                    o += GROUP.unpack_from(self.d, o)[1]
                    continue
                rec, o = self._record_at(o)
                yield rec

    def topic_infos(self):
        """{local DIAL form id: [INFO Record]} (dialogue responses under each topic)."""
        out = {}
        for label, start, size in self._top_groups():
            if label != "DIAL":
                continue
            o, end, topic = start + 24, start + size, None
            while o < end:
                if bytes(self.d[o:o + 4]) == b"GRUP":
                    _g, gsize, glabel, gtype, *_ = GROUP.unpack_from(self.d, o)
                    if gtype == 7:  # topic children
                        topic = struct.unpack("<I", glabel)[0]
                        o += 24
                    else:
                        o += gsize
                    continue
                rec, o = self._record_at(o)
                if rec.type == "INFO" and topic is not None:
                    out.setdefault(topic, []).append(rec)
        return out

    def record_by_form_id(self, form_id, types=("LTEX", "TXST")):
        """Looks up a top-level record; each type's group is indexed on first use."""
        if self._form_index is None:
            self._form_index = {}
            self._indexed_types = set()
        for t in types:
            if t not in self._indexed_types:
                for rec in self.records_of_type(t):
                    self._form_index[rec.form_id] = rec
                self._indexed_types.add(t)
        return self._form_index.get(form_id)

    def interior_cells(self):
        """{local interior cell form id: {"cell": Record, "refs": [Record], "navm": [Record]}}"""
        out = {}
        for label, start, size in self._top_groups():
            if label != "CELL":
                continue
            o, end, cur = start + 24, start + size, None
            while o < end:
                if bytes(self.d[o:o + 4]) == b"GRUP":
                    gsize, gtype = GROUP.unpack_from(self.d, o)[1], GROUP.unpack_from(self.d, o)[3]
                    o += 24 if gtype in (2, 3, GROUP_CELL_CHILDREN, GROUP_CELL_PERSISTENT,
                                         GROUP_CELL_TEMPORARY) else gsize
                    continue
                rec, o = self._record_at(o)
                if rec.type == "CELL":
                    cur = out.setdefault(rec.form_id, {"cell": None, "refs": [], "navm": []})
                    cur["cell"] = rec
                elif rec.type in ("REFR", "ACHR") and cur is not None:
                    cur["refs"].append(rec)
                elif rec.type == "NAVM" and cur is not None:
                    cur["navm"].append(rec)
        return out

    def refs_by_id(self, local_ids):
        """{local ref form id: ref Record} of the placed references (and people) whose object ids (the low 24
        bits) are these, wherever they are (worlds,
        interiors; disabled ones too: merchants' chests are kept out of sight so)."""
        out = {}
        for label, start, size in self._top_groups():
            if label not in ("WRLD", "CELL"):
                continue
            o, end = start + 24, start + size
            while o < end:
                if bytes(self.d[o:o + 4]) == b"GRUP":
                    o += 24  # (every group inside: descended into)
                    continue
                rtype, rsize, _f, form_id = HEADER.unpack_from(self.d, o)[:4]
                if rtype in (b"REFR", b"ACHR") and form_id & 0xFFFFFF in local_ids:  # (by object id: any master's; placed people too)
                    out[form_id] = self._record_at(o)[0]
                o += 24 + rsize
        return out

    def interior_refs(self):
        """{local ref form id: (local interior cell form id, ref Record)} for every reference
        (REFR/ACHR) placed in an interior cell."""
        return {r.form_id: (cid, r) for cid, c in self.interior_cells().items() for r in c["refs"]}

    def world_record(self, world_editor_id):
        """Finds and returns the WRLD Record for the given editor ID."""
        for label, start, size in self._top_groups():
            if label == "WRLD":
                break
        else:
            return None
        o, end = start + 24, start + size
        while o < end:
            if bytes(self.d[o:o + 4]) == b"GRUP":
                o += GROUP.unpack_from(self.d, o)[1]
                continue
            rec, o = self._record_at(o)
            if rec.type == "WRLD" and rec.editor_id().lower() == world_editor_id.lower():
                return rec
        return None

    def world_records(self):
        """All WRLD records in this plugin."""
        return list(self.records_of_type("WRLD"))

    def exterior_cells(self, world_editor_id, keep_removed=False):
        """{(x, y): {"cell": Record, "land": Record | None, "refs": [Record]}} for one worldspace.
        Cells with navmeshes also carry "navm": [Record].

        refs holds the cell's placed references (REFR objects, ACHR actors), excluding deleted and initially
        disabled ones. References from the world's persistent cell are assigned to the
        grid cell containing their position.
        """
        for label, start, size in self._top_groups():
            if label == "WRLD":
                break
        else:
            raise KeyError("no WRLD group")

        o, end = start + 24, start + size
        world_id = None
        while o < end:
            if bytes(self.d[o:o + 4]) == b"GRUP":
                _g, gsize, glabel, gtype, *_ = GROUP.unpack_from(self.d, o)
                if gtype == GROUP_WORLD_CHILDREN and struct.unpack("<I", glabel)[0] == world_id:
                    return self._scan_world_children(o + 24, o + gsize, keep_removed)
                o += gsize
                continue
            rec, o = self._record_at(o)
            if rec.type == "WRLD" and rec.editor_id().lower() == world_editor_id.lower():
                world_id = rec.form_id
        raise KeyError(f"worldspace {world_editor_id} not found")

    def _scan_world_children(self, o, end, keep_removed=False):
        cells = {}
        persistent = {"cell": None, "land": None, "refs": []}
        current = None
        stack = [end]
        while o < end:
            while o >= stack[-1]:
                stack.pop()
            if bytes(self.d[o:o + 4]) == b"GRUP":
                _g, gsize, _label, gtype, *_ = GROUP.unpack_from(self.d, o)
                if gtype in (GROUP_EXTERIOR_BLOCK, GROUP_EXTERIOR_SUBBLOCK, GROUP_CELL_CHILDREN,
                             GROUP_CELL_TEMPORARY, GROUP_CELL_PERSISTENT):
                    stack.append(o + gsize)
                    o += 24  # descend
                else:
                    o += gsize
                continue
            rec, o = self._record_at(o)
            if rec.type == "CELL":
                xclc = rec.sub("XCLC")
                # The world's persistent cell sits directly under the world-children group
                # (outside any exterior block) and may also carry XCLC 0,0 - never merge it
                # into the real cell at that position.
                if len(stack) == 1:
                    current = persistent
                elif xclc:
                    x, y = struct.unpack_from("<ii", xclc)
                    current = cells.setdefault((x, y), {"cell": None, "land": None, "refs": []})
                    current["cell"] = rec
                else:
                    current = persistent  # the world's persistent-references cell
            elif rec.type == "LAND" and current is not None:
                current["land"] = rec
            elif rec.type == "NAVM" and current is not None:  # walkable area (pathfinding)
                current.setdefault("navm", []).append(rec)
            elif rec.type in ("REFR", "ACHR") and current is not None:  # objects and actors
                if keep_removed or not rec.flags & (RECORD_DELETED | RECORD_INITIALLY_DISABLED):
                    current["refs"].append(rec)

        for ref in persistent["refs"]:
            data = ref.sub("DATA")
            if not data:
                continue
            px, py = struct.unpack_from("<ff", data)
            key = (math.floor(px / CELL_UNITS), math.floor(py / CELL_UNITS))
            cells.setdefault(key, {"cell": None, "land": None, "refs": []})["refs"].append(ref)
        return cells


# --- Load order ------------------------------------------------------------------------

OFFICIAL_LOAD_ORDER = ("Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm")


# World switches (the engine turns them on: engine/src/objects.c objects_set_switch): the
# markers whose enable state Skyrim's scripts change, by editor id prefix (lowercase) -> number
SWITCHES = (("mq101alduinattackenablemarker1", 1),  # Helgen as Alduin's attack leaves it
            ("duncgrubble", 2), ("duncgpost", 2), ("duncgsmoke", 2))  # ... and after: the ruins
SWITCH_ON_SHOWS = 0x80
# ... and these references themselves, there only until their switch is on (Skyrim animates
# them away: the tower wall Alduin breaks open, DragonTowerHole01's collapse)
SWITCH_REFS = {"duncgtowerhole01ref": 1}
# Inside: the markers the main quest's scripts switch, kept with their own switch numbers (the
# engine turns them on by the quest's stage: engine/src/quests.c quests_world_switches). The
# interiors were baked as at a new game, so what hangs on these stayed in its first state for good
# A negative number: the switch on is the marker DISABLED (its children go, the inverted ones come)
SWITCHES_INSIDE = (("mq105enabledungeonmarker", -3),)  # Ustengrav's cave-in: MQ105 stage (its fragment
#   42, the Horn asked for) disables it - the rubble and boulder go, the dug-out way and lantern come


# References a quest enables that the export keeps enabled from the start (their quest's way in:
# Fellglow Keep's doors up to the Caller, Hitting the Books's) - filled by the quests' modules
ENABLED_ANYWAY = set()


def _u32le(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


class LoadOrder:
    """Several plugins merged the way the game does: later plugins override earlier ones.

    Form ids inside a plugin are local (top byte = index into its own master list, or
    the plugin itself); global() maps them to load-order ids so overrides line up.
    """

    def __init__(self, data_dir, names=OFFICIAL_LOAD_ORDER):
        import os
        self.plugins = []
        for n in names:
            path = os.path.join(data_dir, n)
            if os.path.exists(path):
                self.plugins.append(ESM(path))
        by_name = {p.name.lower(): i for i, p in enumerate(self.plugins)}
        for i, p in enumerate(self.plugins):
            p.load_index = i
            p.master_map = [by_name.get(m.lower(), -1) for m in p.masters]
        self._records = {}

    def global_id(self, plugin, local_id):
        top = local_id >> 24
        idx = plugin.master_map[top] if top < len(plugin.master_map) else plugin.load_index
        return (idx << 24) | (local_id & 0xFFFFFF) if idx >= 0 else None

    def record(self, global_id, types=("LTEX", "TXST")):
        """Winning (last-loaded) top-level record for a global form id."""
        for t in types:
            if t not in self._records:
                index = {}
                for p in self.plugins:
                    for rec in p.records_of_type(t):
                        g = self.global_id(p, rec.form_id)
                        if g is not None:
                            index[g] = rec
                self._records[t] = index
            rec = self._records[t].get(global_id)
            if rec is not None:
                return rec
        return None

    def world_record(self, world_editor_id):
        """Winning WRLD Record for the given editor ID across plugins in load order."""
        rec = None
        for p in self.plugins:
            r = p.world_record(world_editor_id)
            if r is not None:
                rec = r
        return rec

    def _enable_state(self, plugin, ref, states, switches=None, table=None):
        """Notes a reference's own enable state and its enable parent (XESP) for
        _drop_disabled."""
        g = self.global_id(plugin, ref.form_id)
        xesp = ref.sub("XESP")
        parent = self.global_id(plugin, _u32le(xesp)) if xesp and len(xesp) >= 8 else None
        states[g] = (not ref.flags & RECORD_INITIALLY_DISABLED, parent, bool(xesp and xesp[4] & 1))
        if switches is not None:
            eid = (ref.editor_id() or "").lower()
            for prefix, number in table or SWITCHES:
                if eid.startswith(prefix):
                    switches[g] = number

    @staticmethod
    def _enabled(states, switches=None):
        """{ref: (enabled at a new game, world switch)}: its own flag, and its enable parent's
        (the opposite of it, for the inverted) - Skyrim's quest-gated objects: Helgen's rubble
        and burned clutter and the bandits who come later had been drawn over the intact town.
        A parent not here (another world) counts as enabled. Under one of the switches
        (SWITCHES: Helgen as Alduin's attack leaves it...) a reference is kept with its switch:
        its number, + SWITCH_ON_SHOWS if it's there once that's on (else only till then)."""
        switches = switches or {}
        memo = {}

        def on(g, depth=0):
            if g in memo:
                return memo[g]
            own, parent, inverted = states[g]
            result = (own, 0)
            if own and parent is not None and parent in switches:
                n = switches[parent]
                shows_on = (not inverted) if n > 0 else inverted  # (a negative one: the marker disabled when on)
                result = (True, abs(n) | (SWITCH_ON_SHOWS if shows_on else 0))
            elif own and parent is not None and parent in states and depth < 16:
                memo[g] = (True, 0)  # (a cycle: as enabled)
                pe, ptag = on(parent, depth + 1)
                result = (True, ptag ^ (SWITCH_ON_SHOWS if inverted else 0)) if ptag else (pe != inverted, 0)
            memo[g] = result
            return result

        return {g: on(g) for g in states}

    def exterior_cells(self, world_editor_id):
        """Merged {(x, y): {"cell", "land", "refs"}}: last CELL/LAND wins, references are
        overridden by global form id, and deleted ones are dropped, as are those disabled at a
        new game (their own flag, or their enable parent's: _enabled)."""
        merged = {}
        states, switches = {}, {}
        ref_slot = {}  # global ref id -> (x, y) where it currently lives
        for p in self.plugins:
            try:
                cells = p.exterior_cells(world_editor_id, keep_removed=True)
            except KeyError:
                continue
            for key, c in cells.items():
                m = merged.setdefault(key, {"cell": None, "land": None, "refs": {}, "navm": {}})
                if c["cell"] is not None:
                    m["cell"] = c["cell"]
                if c["land"] is not None:
                    m["land"] = c["land"]
                for nav in c.get("navm", []):  # overrides replace by form id
                    g = self.global_id(p, nav.form_id)
                    if nav.flags & RECORD_DELETED:
                        m["navm"].pop(g, None)
                    else:
                        m["navm"][g] = nav
                for ref in c["refs"]:
                    g = self.global_id(p, ref.form_id)
                    old = ref_slot.pop(g, None)
                    if old is not None:  # an override, possibly moved to another cell
                        merged[old]["refs"].pop(g, None)
                    states.pop(g, None)
                    if not ref.flags & RECORD_DELETED:
                        self._enable_state(p, ref, states, switches)
                        m["refs"][g] = ref
                        ref_slot[g] = key
        enabled = self._enabled(states, switches)
        for m in merged.values():
            refs = []
            for g, r in m["refs"].items():
                on, tag = enabled.get(g, (True, 0))
                if on:
                    r.switch = tag or SWITCH_REFS.get((r.editor_id() or "").lower(), 0)
                    refs.append(r)
            m["refs"] = refs
        return merged

    def interior_cells(self):
        """Merged {global cell id: {"cell", "refs", "navm"}}: the last CELL record wins,
        references and navmeshes are overridden by global form id, deleted and initially
        disabled references are dropped."""
        merged = {}
        states, switches = {}, {}
        for p in self.plugins:
            for local, c in p.interior_cells().items():
                g = self.global_id(p, local)
                if g is None:
                    continue
                m = merged.setdefault(g, {"cell": None, "refs": {}, "navm": {}})
                if c["cell"] is not None:
                    m["cell"] = c["cell"]
                for kind in ("refs", "navm"):
                    for r in c[kind]:
                        rg = self.global_id(p, r.form_id)
                        if r.flags & RECORD_DELETED:
                            m[kind].pop(rg, None)
                            states.pop(rg, None)
                        else:
                            m[kind][rg] = r
                            if kind == "refs":
                                self._enable_state(p, r, states, switches, SWITCHES_INSIDE)
        enabled = self._enabled(states, switches)  # (inside: as at a new game, but for SWITCHES_INSIDE)
        for m in merged.values():
            refs = []
            for g, r in m["refs"].items():
                on, tag = enabled.get(g, (True, 0))
                if on or g in ENABLED_ANYWAY:
                    r.switch = tag
                    refs.append(r)
            m["refs"] = refs
            m["navm"] = list(m["navm"].values())
        return merged

    def refs_by_id(self, global_ids):
        """{global ref id: ref Record} of these placed references (Plugin.refs_by_id), later
        plugins winning."""
        out, low = {}, {g & 0xFFFFFF for g in global_ids}
        for p in self.plugins:
            for lid, rec in p.refs_by_id(low).items():
                g = self.global_id(p, lid)
                if g in global_ids:
                    out[g] = rec
        return out

    def interior_refs(self):
        """{global ref id: (global interior cell id, ref Record)}, later plugins winning."""
        out = {}
        for p in self.plugins:
            for local, (cell, ref) in p.interior_refs().items():
                g, cg = self.global_id(p, local), self.global_id(p, cell)
                if g is not None and cg is not None:
                    out[g] = (cg, ref)
        return out
