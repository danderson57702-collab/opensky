# Re-exports everything but the terrain: the region's cells (loot split out), interiors,
# NPCs, the player and gear, items, animations, then the bakes; appends to the model table.
# Run from converter/ (the whole map: about an hour; a new pack: longer), then: python3 pack_archives.py ../pack
#   python3 ../tools/reexport.py [PACK_DIR]   (../pack if not given; tools/make_data.py runs it for a new pack)
import os, sys, time
from collections import Counter
sys.path.insert(0, '.')
import extract_objects as E, interiors, items, npcs, animations
from esm import LoadOrder
from dialogue import StringTables
from extract_textures import SKYRIM_DATA, find
from bake_cells import bake


def run(pack):
    t0 = time.time()
    bounds = (-57, -43, 61, 50)  # (29 Oct: the whole of Tamriel, every land cell. Grown a hold at a time for the main
                                 #  quest before: (-35, -27, 45, 30) - Ustengrav, Kynesgrove, Haafingar, Riften, the Reach)
    import esm, college
    esm.ENABLED_ANYWAY |= college.ENABLED_ANYWAY  # (the doors a quest opens, open from the start: Fellglow's upper floor)
    lo = LoadOrder(SKYRIM_DATA)
    cells = lo.exterior_cells("Tamriel")
    models = E.ModelConverter(pack)
    if os.path.exists(os.path.join(pack, 'objects', 'models.txt')):
        models.resume()  # (a pack being re-exported keeps its model numbers; a new one starts its table)
    inside = interiors.Exporter(lo, cells, models, StringTables(lo), E.entry_for_ref)
    inside.items = items.Database(lo, StringTables(lo))
    placed, cities = E.write_exterior_cells(lo, cells, models, inside, pack, bounds, Counter())
    inside.cities = cities
    print("cells done", round(time.time() - t0)); sys.stdout.flush()
    index = inside.export(pack, bounds)
    E.write_lockpick_models(models, pack)
    with open(os.path.join(pack, "objects", "models.txt"), "w") as f:
        f.write("".join(f"{n} {models.lods.get(i, '-')} {models.mids.get(i, '-')}\n" for i, n in enumerate(models.models)))
    with open(os.path.join(pack, "objects", "fx.txt"), "w") as f:
        for idx, points in sorted(models.fx.items()):
            for kind, x, y, z, w, h in points:
                f.write(f"{idx} {kind} {x:.3f} {y:.3f} {z:.3f} {w:.3f} {h:.3f}\n")
    print("interiors exported", round(time.time() - t0)); sys.stdout.flush()
    import lighten_mids, relock_landscape
    relock_landscape.main(pack)  # (rocks and mountains with their edges kept, once)
    lighten_mids.main(pack)  # (mid meshes to the current share, once)
    bake(pack)
    print("cells baked", round(time.time() - t0)); sys.stdout.flush()
    inside.bake(pack)
    import navmesh
    navmesh.export(lo, cells, pack, bounds)  # (before the quests: the opening's cart route is found on it)
    import waygraph
    waygraph.build(pack)  # (the coarse way across Tamriel, from those navmeshes: Clairvoyance's)
    people = npcs.Exporter(lo, cells, models, find, cities)
    people.items = inside.items
    people.export(pack, bounds, index)
    import people_combat
    people_combat.build(lo, pack)  # (their health, essential / protected: after actors.txt is written)
    import quests
    quests.export(lo, pack, people.talk, inside.items, models)  # (before the gear and the items: it registers the Dragonstone, the opening's kit)
    import weather, creatures, perks, worldmap, extract_sounds
    creatures.export(lo, models, cells, pack, bounds, people, StringTables(lo), inside, index)
    import shouts
    shouts.export(lo, pack)  # (after the people - their dialogue export clears voice/ - and the creatures: Call of Valor's heroes)
    import followers
    followers.export(lo, pack, StringTables(lo), people.talk, people.exported)
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
    import legacy_followers
    legacy_followers.main(pack)  # (old saves' followers: each earlier map's list, from this one)
    import topics
    topics.export(lo, pack, StringTables(lo), people.talk, people.exported)
    npcs.export_player(people, pack, StringTables(lo))
    import stones
    stones.export(lo, cells, pack, bounds, StringTables(lo))  # (the standing stones)
    stones.export_walls(lo, cells, inside, index, pack, bounds)  # (the word walls: by the interiors' numbers just made)
    inside.items.write(pack, models)
    E.write_model_table(models, pack)
    animations.export(pack, models.mesh_archives, find)
    import greybeard_words
    greybeard_words.build(pack, models.mesh_archives, find)  # (the words shouted onto the floor: their letters)
    import skills
    skills.export(lo, pack, StringTables(lo))
    perks.export(lo, pack, StringTables(lo))
    worldmap.export(lo, cells, pack, bounds, StringTables(lo))
    weather.export(lo, pack)
    extract_sounds.export(pack)
    E.write_model_table(models, pack)
    import gametext
    gametext.export(lo, StringTables(lo), pack)  # (the game's own words the engine shows: none are in the program)
    import title
    title.export(SKYRIM_DATA, pack)  # (the game's name, from the user's own interface strings)
    title.export_credits(SKYRIM_DATA, pack)  # (its makers' credits and legal notice, from the same)
    print("all done", round(time.time() - t0))


if __name__ == '__main__':
    run(sys.argv[1] if len(sys.argv) > 1 else '../pack')
