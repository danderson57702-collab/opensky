"""Skyrim's weather by region (REGN records' weather lists over Tamriel's region areas; WTHR
records' classification). The engine picks one of a region's weathers by its chance every
few game hours and changes to it gradually: clear, cloudy, rain, snow or fog (how overcast,
how far you see, what falls).

  PACK_DIR/weather.txt, lines:
    D <weathers: kind:chance ...>              the worldspace's climate (outside every region)
    R <priority> <weathers: kind:chance ...>   a region (kind: clear, cloudy, rain, snow, fog)
    P <x> <z> <x> <z> ...                      one of its areas: a polygon (engine metres)
"""
import os
import struct

UNITS_PER_METER = 70.0
WTHR_PLEASANT, WTHR_CLOUDY, WTHR_RAINY, WTHR_SNOW = 0x1, 0x2, 0x4, 0x8


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


def _kind(wthr):
    data = wthr.sub("DATA")
    flags = data[11] if data and len(data) > 11 else 0
    eid = wthr.editor_id().lower()
    if flags & WTHR_SNOW:
        return "snow"
    if flags & WTHR_RAINY:
        return "rain"
    if "fog" in eid:
        return "fog"
    if flags & WTHR_CLOUDY or "cloudy" in eid or "overcast" in eid:
        return "cloudy"
    return "clear"


def export(lo, pack, world_eid="Tamriel"):
    world, world_rec = None, None
    for p in lo.plugins:
        for r in p.records_of_type("WRLD"):
            if r.editor_id() == world_eid:
                world, world_rec = lo.global_id(p, r.form_id), r
    wthr = {lo.global_id(p, r.form_id): r for p in lo.plugins for r in p.records_of_type("WTHR")}
    lines = []
    cnam = world_rec.sub("CNAM") if world_rec is not None else None
    climate = lo.record(lo.global_id(world_rec.plugin, _u32(cnam)), types=("CLMT",)) if cnam else None
    wlst = climate.sub("WLST") if climate is not None else None
    if wlst:
        default = []
        for k in range(len(wlst) // 12):
            wid, chance = struct.unpack_from("<Ii", wlst, k * 12)
            w = wthr.get(lo.global_id(climate.plugin, wid))
            if w is not None and chance > 0:
                default.append((_kind(w), chance))
        if default:
            lines.append("D " + " ".join(f"{k}:{c}" for k, c in default))
    regions = {}
    for p in lo.plugins:
        for r in p.records_of_type("REGN"):
            regions[lo.global_id(p, r.form_id)] = r  # (overrides win)
    count = 0
    for rec in regions.values():
        wnam = rec.sub("WNAM")
        if not wnam or lo.global_id(rec.plugin, _u32(wnam)) != world:
            continue
        subs = list(rec.subrecords())
        weathers, priority = [], 0
        for i, (t, v) in enumerate(subs):
            if t == "RDAT" and len(v) >= 6 and _u32(v) == 3:  # (the weather entry's data)
                priority = v[5]
            if t == "RDWT":
                for k in range(len(v) // 12):
                    wid, chance = struct.unpack_from("<II", v, k * 12)
                    w = wthr.get(lo.global_id(rec.plugin, wid))
                    if w is not None and chance:
                        weathers.append((_kind(w), chance))
        areas = []
        for t, v in subs:
            if t == "RPLD" and len(v) >= 24:
                pts = struct.unpack_from(f"<{len(v) // 4}f", v)
                areas.append([(pts[i] / UNITS_PER_METER, -pts[i + 1] / UNITS_PER_METER) for i in range(0, len(pts) - 1, 2)])
        if not weathers or not areas:
            continue
        lines.append(f"R {priority} " + " ".join(f"{k}:{c}" for k, c in weathers))
        for a in areas:
            lines.append("P " + " ".join(f"{x:.1f} {z:.1f}" for x, z in a))
        count += 1
    with open(os.path.join(pack, "weather.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"weather: {count} regions")
