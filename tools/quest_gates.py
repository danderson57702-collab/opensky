#!/usr/bin/env python3
"""Quest-gated things in the interiors: the markers whose enable state Skyrim's quest scripts change
(Ustengrav's cave-in, MQ105EnableDungeonMarker) and what hangs on them (XESP enable parents). The
converter bakes interiors as they are at a new game, so a marker a quest switches leaves its
children in their first state forever - a cave-in never cleared, a way never opened.

    python3 tools/quest_gates.py [--quest MQ] [--cell ustengrav01]

Per marker: its cell, the quests whose scripts (or aliases) name it, which stage fragments
enable / disable it, and its children (normal: shown while it's enabled; inverted: while it's
disabled) by kind, with the solid statics counted."""
import os
import re
import struct
import subprocess
import sys
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "converter"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from esm import LoadOrder, RECORD_INITIALLY_DISABLED, _u32le  # noqa: E402
from extract_textures import SKYRIM_DATA  # noqa: E402
from audit_runner import parse_vmad  # noqa: E402

BASE_TYPES = ("STAT", "MSTT", "ACTI", "DOOR", "CONT", "FURN", "LIGH", "MISC", "TREE", "FLOR", "NPC_", "SCOL", "MOVS",
              "TACT", "IDLM", "SOUN", "WEAP", "ARMO", "BOOK", "LVLN", "ALCH", "INGR", "KEYM", "HAZD", "TXST", "LVLC",
              "EXPL", "SLGM", "ARMA", "AMMO", "SCRL", "APPA", "ADDN", "PROJ")


def quest_refs(lo):
    """{global ref id: [(quest editor id, property or alias name)]} and {quest eid: (qf script, {prop: gid})}"""
    named, scripts = defaultdict(list), {}
    for p in lo.plugins:
        for q in p.records_of_type("QUST"):
            eid = q.editor_id() or f"{q.form_id:08X}"
            alias = None
            for t, v in q.subrecords():
                if t == "ALID":
                    alias = bytes(v).rstrip(b"\0").decode("latin-1")
                elif t == "ALFR" and len(v) >= 4:
                    named[lo.global_id(p, _u32le(v))].append((eid, "alias " + (alias or "?")))
                elif t == "VMAD":
                    for sname, props in parse_vmad(bytes(v)).items():
                        if sname.startswith("_"):
                            continue
                        for pname, val in props.items():
                            for fid in (val if isinstance(val, list) else [val]):
                                if isinstance(fid, int) and fid > 0 and not isinstance(fid, bool):
                                    g = lo.global_id(p, fid)
                                    named[g].append((eid, pname))
                                    scripts.setdefault(eid, (f"QF_{eid}_{lo.global_id(p, q.form_id):08X}", {}))[1][pname] = g
    return named, scripts


_pex_cache = {}


def fragments(script, prop):
    """[(fragment, 'enable'|'disable')] of a quest's stage script on this property."""
    if script not in _pex_cache:
        out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "pexdump.py"), script + ".pex"],
                             capture_output=True, text=True).stdout
        _pex_cache[script] = out
    res, frag = [], None
    for line in _pex_cache[script].splitlines():
        if line and not line.startswith(" "):
            frag = line.strip()
        m = re.search(r"callmethod (enable|disable) ::(\w+)_var", line, re.I)
        if m and m.group(2).lower() == prop.lower():
            res.append((frag, m.group(1).lower()))
    return res


def main():
    args = sys.argv[1:]
    want_q = args[args.index("--quest") + 1].lower() if "--quest" in args else None
    want_c = args[args.index("--cell") + 1].lower() if "--cell" in args else None
    lo = LoadOrder(SKYRIM_DATA)
    named, scripts = quest_refs(lo)
    for p in lo.plugins:
        for local, c in p.interior_cells().items():
            if c["cell"] is None:
                continue
            ceid = (c["cell"].editor_id() or "").lower()
            if want_c and ceid != want_c:
                continue
            by_id = {lo.global_id(p, r.form_id): r for r in c["refs"]}
            kids = defaultdict(list)
            for g, r in by_id.items():
                x = r.sub("XESP")
                if x and len(x) >= 8:
                    kids[lo.global_id(p, _u32le(x))].append((r, bool(x[4] & 1)))
            for parent, children in kids.items():
                quests = named.get(parent, [])
                if not quests or (want_q and not any(q.lower().startswith(want_q) for q, _ in quests)):
                    continue
                pr = by_id.get(parent)
                peid = (pr.editor_id() if pr else None) or f"{parent:08X}"
                pdis = " (disabled at start)" if pr is not None and pr.flags & RECORD_INITIALLY_DISABLED else ""
                count = defaultdict(int)
                for r, inv in children:
                    base = lo.record(lo.global_id(r.plugin if hasattr(r, "plugin") else p, _u32le(r.sub("NAME"))), types=BASE_TYPES)
                    count[("inv " if inv else "") + (base.type if base else "?")] += 1
                print(f"{ceid}: {peid} {parent:08X}{pdis}  children {dict(count)}")
                seen = set()
                for q, prop in quests:
                    if (q, prop) in seen:
                        continue
                    seen.add((q, prop))
                    fr = fragments(scripts[q][0], prop) if q in scripts and not prop.startswith("alias") else []
                    print(f"    {q}.{prop}" + ("  " + ", ".join(f"{f}:{a}" for f, a in fr) if fr else ""))


if __name__ == "__main__":
    main()
