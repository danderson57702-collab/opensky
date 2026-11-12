"""Walled cities: Skyrim's cities are worldspaces of their own (WhiterunWorld...), entered
through their gates, laid over Tamriel's own coordinates and using Tamriel's land (their
WRLD records say so: PNAM "use the parent's land, LOD, water, climate, sky"). Tamriel holds
only stand-ins in their place (wrhouse..lod, ..districtterrainlod) and the walls.

So a city is a set of cells that replace Tamriel's while you're in it: the city's own
references plus Tamriel's that aren't stand-ins (the stables and farms outside the walls)
nor duplicates of the city's (the walls), baked like any cell, with the city's navmesh,
containers and loose items. The engine keeps Tamriel's terrain and the cells around.

  PACK_DIR/objects/<city>_X_Y.skobj / .skloot, objects/baked/<city>_X_Y_G_L.skbat,
  PACK_DIR/navmesh/<city>_X_Y.sknav
  PACK_DIR/worlds.txt: "= <the open world's name>", then per city: "<name> <display name with _ for spaces> x,y x,y ..." (its cells)
"""
import os
import struct
from collections import Counter

import navmesh

CITIES = [("WhiterunWorld", "whiterun", "Whiterun"), ("SolitudeWorld", "solitude", "Solitude"),
          ("WindhelmWorld", "windhelm", "Windhelm"), ("RiftenWorld", "riften", "Riften"),
          ("MarkarthWorld", "markarth", "Markarth")]  # (worldspace, file prefix, name)
# Worldspaces of their own behind the region's doors (their land is theirs: grottos, the ruins
# of Labyrinthian): written whole, terrain too; worlds.txt marks them "!"
OWN_LAND = [("MossMotherCavernWorld", "mossmother", "Moss Mother Cavern"),
            ("BloatedMansGrottoWorld", "bloatedman", "Bloated Man's Grotto"),
            ("LabyrinthianWorld", "labyrinthian", "Labyrinthian"),
            ("LabyrinthianWorld04", "labyrinthian4", "Labyrinthian"),
            ("LabyrinthianMazeWorld", "labmaze", "Labyrinthian"),
            ("LabyrinthianWorld03", "labyrinthian3", "Labyrinthian"),
            # (29 Oct: the whole map - every base-game world behind a door: the main quest's Blackreach,
            # Skuldafn and Sovngarde, Alftand's glacier, Dragonsreach's porch for Odahviing; the rest a
            # quest's or a dungeon's own land. Skyrim's test worlds - CWTestHold, CWSiegeTestWorld,
            # FXLightWorldSpace - left out)
            ("Blackreach", "blackreach", "Blackreach"),
            ("SkuldafnWorld", "skuldafn", "Skuldafn"),
            ("Sovngarde", "sovngarde", "Sovngarde"),
            ("AlftandWorld", "alftand", "Alftand"),
            ("WhiterunDragonsreachWorld", "dragonsreach", "Dragonsreach"),
            ("FallowstoneCaveWorldStart", "fallowstone1", "Fallowstone Cave"),
            ("FallowstoneCaveWorldEnd", "fallowstone2", "Fallowstone Cave"),
            ("SouthfringeWorld", "southfringe", "Southfringe Sanctum"),
            ("BlindCliffCaveWorld", "blindcliff", "Blind Cliff Cave"),
            ("DeepwoodRedoubtWorld", "deepwood", "Deepwood Redoubt"),
            ("ShadowgreenCavernWorld", "shadowgreen", "Shadowgreen Cavern"),
            ("RedEagleRedoubtWorld", "redeagle", "Red Eagle Redoubt"),
            ("DarkwaterWorld", "darkwater", "Darkwater Pass"),
            ("BluePalaceWingWorld", "pelagiuswing", "Pelagius Wing"),
            ("KarthspireRedoubtWorld", "karthspire", "Karthspire"),
            ("EldergleamSanctuaryWorld", "eldergleam", "Eldergleam Sanctuary"),
            ("EastEmpireWarehouse", "eastempire", "East Empire Company Warehouse"),
            ("BrinewaterGrottoWorld", "brinewater", "Brinewater Grotto"),
            ("JaphetsFollyWorld", "japhetsfolly", "Japhet's Folly"),
            ("KatariahWorld", "katariah", "Katariah"),
            ("FrostmereCryptWorld", "frostmere", "Frostmere Crypt"),
            ("WindhelmPitWorldspace", "windhelmpit", "Windhelm")]
STAND_IN_MARKS = ("lod.nif", "\\lod\\")
SAME_PLACE_UNITS = 4.0


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _model_and_pos(lo, ref):
    name, data = ref.sub("NAME"), ref.sub("DATA")
    if not name or not data:
        return None, None
    base = lo.record(lo.global_id(ref.plugin, _u32(name)),
                     types=("STAT", "MSTT", "TREE", "FURN", "ACTI", "DOOR", "CONT", "LIGH", "FLOR", "SCOL", "MISC"))
    modl = base.sub("MODL") if base is not None else None
    model = modl.rstrip(b"\0").decode("cp1252", "replace").lower() if modl and len(modl) > 4 else None
    return model, struct.unpack_from("<3f", data)


def city_cells(lo, world_eid):
    """(cells dict of the worldspace, the city's cells: those with a navmesh)."""
    cells = lo.exterior_cells(world_eid)
    return cells, {k for k, c in cells.items() if c.get("navm")}


def export(lo, tamriel, models, inside, pack, entry_for_ref, write_cell):
    """Writes the cities' cells (write_cell: extract_objects' cell writer, given a world
    prefix and the references), navmeshes and worlds.txt; returns {prefix: (cells dict,
    city cell keys)} for the interiors and NPCs."""
    out, lines = {}, []
    for world_eid, prefix, title in CITIES:
        cells, city = city_cells(lo, world_eid)
        stats = Counter()
        for key in sorted(city):
            own = cells[key]["refs"]
            placed = {}
            for r in own:
                m, p = _model_and_pos(lo, r)
                if m:
                    placed.setdefault(m, []).append(p)
            refs = list(own)
            for r in tamriel.get(key, {"refs": []})["refs"]:
                m, p = _model_and_pos(lo, r)
                if m and any(s in m for s in STAND_IN_MARKS):
                    stats["tamriel stand-ins left out"] += 1
                    continue
                if m and any(abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]) < SAME_PLACE_UNITS
                             for q in placed.get(m, [])):
                    stats["duplicates left out"] += 1
                    continue
                refs.append(r)
                stats["tamriel objects kept"] += 1
            write_cell(prefix, key, refs, cells[key].get("cell"))
            stats["city objects"] += len(own)
        xs, ys = [k[0] for k in city], [k[1] for k in city]
        navmesh.export(lo, cells, pack, (min(xs), min(ys), max(xs), max(ys)), prefix=prefix, only=city)
        lines.append(f"{prefix} {title.replace(' ', '_')} " + " ".join(f"{x},{y}" for x, y in sorted(city)))
        out[prefix] = (cells, city)
        print(f"city {title}: {len(city)} cells", dict(stats))
    # The worlds of their own: every cell with something in it, its land, its navmesh
    from extract_terrain import export_cells
    from terrain import LandTextures, world_water_height
    from extract_textures import find, open_texture_archives
    tex = open_texture_archives()
    land_textures = LandTextures(lo, lambda p: find(tex, p))
    for world_eid, prefix, title in OWN_LAND:
        try:
            cells = lo.exterior_cells(world_eid)
        except KeyError:
            continue
        keys = {k for k, c in cells.items() if c["refs"] or c.get("land") or c.get("navm")}
        if not keys:
            continue
        placed = sum(write_cell(prefix, key, cells[key]["refs"], cells[key].get("cell")) for key in sorted(keys))
        xs, ys = [k[0] for k in keys], [k[1] for k in keys]
        navmesh.export(lo, cells, pack, (min(xs), min(ys), max(xs), max(ys)), prefix=prefix, only=keys)
        land = export_cells(lo, cells, keys, prefix, pack, land_textures, water_default=world_water_height(lo, world_eid))
        lines.append(f"{prefix} {title.replace(' ', '_')} ! " + " ".join(f"{x},{y}" for x, y in sorted(keys)))
        out[prefix] = (cells, keys)
        print(f"world {title} ({world_eid}): {len(keys)} cells, {placed} objects, {len(land)} with land")
    with open(os.path.join(pack, "worlds.txt"), "w") as f:
        f.write(f"= {land_title(lo).replace(' ', '_')}\n" + "\n".join(lines) + "\n")
    return out


def cities_only(lo):
    """export's {prefix: (cells, keys)} without writing anything (tools/reexport_interiors.py: the
    interiors' order starts from these worlds' doors)."""
    out = {}
    for world_eid, prefix, _title in CITIES:
        cells, city = city_cells(lo, world_eid)
        out[prefix] = (cells, city)
    for world_eid, prefix, _title in OWN_LAND:
        try:
            cells = lo.exterior_cells(world_eid)
        except KeyError:
            continue
        keys = {k for k, c in cells.items() if c["refs"] or c.get("land") or c.get("navm")}
        if keys:
            out[prefix] = (cells, keys)
    return out


def land_title(lo):
    """The open world's own name, as the player's copy has it (its Tamriel worldspace's FULL):
    the engine's loading screens and saves name the outdoors by it, not by a name of its own."""
    from dialogue import StringTables
    strings = StringTables(lo)
    for p in lo.plugins:
        for r in p.records_of_type("WRLD"):
            if (r.editor_id() or "").lower() == "tamriel" and r.sub("FULL") and len(r.sub("FULL")) == 4:
                name = strings.get(r.plugin, struct.unpack("<I", r.sub("FULL"))[0], "strings")
                if name:
                    return name
    return "Outside"
