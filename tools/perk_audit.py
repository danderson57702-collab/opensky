"""Which of Skyrim's perks the engine gives an effect to: each perk's entry points (E lines of
pack/perks.txt) and abilities (A lines) against what engine/src reads - perks_apply's entry
points, perks_av's actor values, perks checked by their form id (perks_has_form). A perk works
when every one of its effects is read; a few have none of their own (the tree's start: none).

python3 tools/perk_audit.py [pack] [--missing]
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def engine_reads():
    src = os.path.join(ROOT, "engine", "src")
    text = "".join(open(os.path.join(src, f), encoding="utf-8", errors="replace").read()
                   for f in os.listdir(src) if f.endswith((".c", ".h")))
    enum = dict((m.group(1), int(m.group(2))) for m in re.finditer(r"(EP_[A-Z_0-9]+)(?: =|\s+)\s*(\d+)", text))
    avs = dict((m.group(1), int(m.group(2))) for m in re.finditer(r"(AV_[A-Z_0-9]+)(?: =|\s+)\s*(\d+)", text))
    entries = {enum[n] for n in re.findall(r"perks_apply\((EP_[A-Z_]+)", text) if n in enum}
    entries |= {int(n) for n in re.findall(r"perks_apply\((\d+)", text)}
    entries |= {enum[n] for n in re.findall(r"perks_(?:entry|any|has_entry)\w*\((EP_[A-Z_]+)", text) if n in enum}
    av_read = {avs[n] for n in re.findall(r"perks_av\((AV_[A-Z_]+)", text) if n in avs}
    av_read |= {int(n) for n in re.findall(r"perks_av\((\d+)", text)}
    av_read |= {int(n) for n in re.findall(r"PERK_AV\((\d+)\)", text)}  # (read through a variable: marked so)
    forms = {int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{8})u?\b", text)}
    return entries, av_read, forms


def main(pack, missing_only):
    entries, av_read, forms = engine_reads()
    perks, eff = {}, collections.defaultdict(list)
    for line in open(os.path.join(pack, "perks.txt"), encoding="cp1252", errors="replace"):
        f = line.rstrip("\n").split(" ")
        if f[0] == "P":
            name = line.split("|")[1] if "|" in line else "?"
            perks[int(f[1])] = (int(f[2], 16), name)
        elif f[0] == "E":
            eff[int(f[1])].append(("E", int(f[2])))
        elif f[0] == "A":
            eff[int(f[1])].append(("A", int(f[2])))
    works, todo = [], []
    for p, (form, name) in sorted(perks.items()):
        e = eff.get(p, [])
        if form in forms:
            works.append((name, "by form"))
            continue
        if not e:
            todo.append((name, "no effect of its own in the pack"))
            continue
        bad = [f"{k}{v}" for k, v in e if (k == "E" and v not in entries) or (k == "A" and v not in av_read)]
        (todo if bad else works).append((name, " ".join(bad) if bad else "read"))
    print(f"perks: {len(perks)}; working {len(works)}; to do {len(todo)}")
    if missing_only:
        why = collections.Counter(w for _n, ws in todo for w in ws.split())
        print("missing effects, by how many perks need them:", why.most_common())
        for n, w in todo:
            print(f"  {n}: {w}")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(args[0] if args else os.path.join(ROOT, "pack"), "--missing" in sys.argv)
