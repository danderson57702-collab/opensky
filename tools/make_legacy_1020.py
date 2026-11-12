# Writes pack/legacy/interiors_1020.txt and spawns_1020.txt: for saves from before 10-21's wider
# map (bounds (-14, -25, 19, 12)), each old interior's and creature spawn point's number now
# (line n: the old n's new number, -1 gone). Rebuilds the old orders - the interiors' and the
# creatures' - into a scratch folder and matches them to the pack's by name and place.
# Run from converter/:  python3 ../tools/make_legacy_1020.py SCRATCH_DIR
import os, sys
sys.path.insert(0, '.')
import extract_objects as E, interiors, worlds, items, npcs, creatures
from esm import LoadOrder
from dialogue import StringTables
from extract_textures import SKYRIM_DATA, find
T = sys.argv[1]
OLD_BOUNDS = (-14, -25, 19, 12)
os.makedirs(T, exist_ok=True)
lo = LoadOrder(SKYRIM_DATA)
cells = lo.exterior_cells("Tamriel")
models = E.ModelConverter('../pack')
models.resume()
inside = interiors.Exporter(lo, cells, models, StringTables(lo), E.entry_for_ref)
inside.items = items.Database(lo, StringTables(lo))
cities = {p: worlds.city_cells(lo, w) for w, p, t in worlds.CITIES if w == "WhiterunWorld"}  # (10-20's only city)
inside.cities = cities
order = inside.select(OLD_BOUNDS)
order += [cg for cg, c in inside.inner.items()  # (appended by the export: Labyrinthian's last)
          if (c["cell"].editor_id() or "").lower() == "labyrinthian03" and cg not in order]
old_ids = [(inside.inner[cg]["cell"].editor_id() or "").lower() for cg in order]
people = npcs.Exporter(lo, cells, models, find, cities)
people.items = inside.items
try:  # (the old spawn points: the creatures' export, into the scratch folder - it stops at crime's)
    creatures.export(lo, models, cells, T, OLD_BOUNDS, people, StringTables(lo), inside, {cg: i for i, cg in enumerate(order)})
except AttributeError:
    pass
new_ids = [l.split('\t')[0].strip() for l in open('../pack/interiors/interiors.txt') if l.strip()]
pos = {n: i for i, n in enumerate(new_ids)}
os.makedirs('../pack/legacy', exist_ok=True)
with open('../pack/legacy/interiors_1020.txt', 'w') as f:
    f.write(''.join(f"{pos.get(n, -1)}\n" for n in old_ids))


def spawns(path, ids):
    out = []
    for l in open(path, encoding='cp1252'):
        if l.startswith('S|'):
            f = l.split('|')
            p = int(f[6])
            out.append((round(float(f[2]), 1), round(float(f[3]), 1), round(float(f[4]), 1), ids[p] if p >= 0 else '-'))
    return out


old = spawns(os.path.join(T, 'creatures', 'creatures.txt'), old_ids)
new = spawns('../pack/creatures/creatures.txt', new_ids)
at = {}
for i, k in enumerate(new):
    at.setdefault(k, i)
with open('../pack/legacy/spawns_1020.txt', 'w') as f:
    f.write(''.join(f"{at.get(k, -1)}\n" for k in old))
print("legacy: interiors", len(old_ids), "spawns", len(old))
