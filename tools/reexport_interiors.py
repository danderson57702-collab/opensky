# Re-exports and re-bakes a few interiors in place (minutes, not reexport.py's hour): their .skint and
# .sknav; the names list, the doors outside and every other interior are left as they are. Run from
# converter/, then: python3 pack_archives.py ../pack
#   python3 ../tools/reexport_interiors.py ../pack ustengrav01 ustengrav02
import os, sys, time
from collections import Counter
sys.path.insert(0, '.')
import extract_objects as E, interiors, items
from esm import LoadOrder
from dialogue import StringTables
from extract_textures import SKYRIM_DATA


def main():
    pack, only = sys.argv[1], {a.lower() for a in sys.argv[2:]}
    t0 = time.time()
    bounds = (-57, -43, 61, 50)  # (as reexport.py's)
    import esm, college
    esm.ENABLED_ANYWAY |= college.ENABLED_ANYWAY
    lo = LoadOrder(SKYRIM_DATA)
    cells = lo.exterior_cells("Tamriel")
    models = E.ModelConverter(pack)
    models.resume()
    inside = interiors.Exporter(lo, cells, models, StringTables(lo), E.entry_for_ref)
    inside.items = items.Database(lo, StringTables(lo))
    import worlds
    inside.cities = worlds.cities_only(lo)
    have = [n.split("\t")[0] for n in open(os.path.join(pack, "interiors", "interiors.txt")).read().splitlines()]
    index = inside.export(pack, bounds, only)
    order = sorted(index, key=index.get)
    names = [(inside.inner[cg]["cell"].editor_id() or "").lower() for cg in order]
    if names != have:  # (the numbers must be the pack's: doors lead by them)
        sys.exit("the interiors' order differs from the pack's interiors.txt - run reexport.py")
    E.write_model_table(models, pack)
    inside.bake(pack)
    print("done", sorted(only), round(time.time() - t0), "s")


if __name__ == "__main__":
    main()
