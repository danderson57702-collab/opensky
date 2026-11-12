"""Skyrim's character creation: for each playable race and sex its bald base head (its "face" part
and that part's extras: the mouth...), the head parts the player can pick - hair, beards, eyes,
brows, scars - as Skyrim's flags allow them (playable, the race's list, the sex), and everything
the race menu's sliders choose from in Skyrim's RACE record: its tint layers (skin tone, war paint,
dirt, the face's colours) and their colour presets, its presets (ten made characters a sex), hair
colours, complexions and the nose, eye and mouth types its chargen morphs allow.

Each part is written once per body (its file and the sex: human races share their skeleton) as
gear (converter/gear.py's .skgear: skinned to the head) in PACK_DIR/actors/head/, with its chargen
morphs and its look's roles (hair: coloured by the engine) in a .skmorph beside it (npcs.write_skmorph),
and listed in PACK_DIR/chargen.txt, one a line:
    P|race editor id|female (0/1)|kind (hair, beard, eyes, brows, scar)|part editor id|file stem
The engine puts the chosen ones on the race's "Custom" face (npcs.export_player: <race>_c<m/f>).

PACK_DIR/chargen/<race editor id, lower case>_<m/f>.txt, '|'-separated lines:
    X|base|detail            a complexion (FTSM/FTSF, in order; the first the race's default): the
                             face's diffuse (<base>.skraw) and its detail map (<detail>.skmask, or -)
    L|index|type|mask|default  a tint layer (TINI, TINP: 1 lips, 2 cheeks, 3 eyeliner, 4 eye socket
                             upper, 5 lower, 6 skin tone, 7 war paint, 8 laugh lines, 9 cheeks lower,
                             10 nose, 11 chin, 12 neck, 13 forehead, 14 dirt; <mask>.skmask; its default
                             colour, TIND: what a preset that doesn't list it has - the skin tone)
    C|index|rrggbb|value     ... one of its colour presets (TINC, TINV), in order
    H|rrggbb                 a hair colour (AHCM/AHCF)
    T|group|mask             the nose (0), eye (2) and mouth (3) types allowed (MPAV: bit n, type n)
    S|n|hair colour|complexion|weight|nose,eyes,mouth|18 morphs|kind:part,...|index:colour:value,...
                             a preset (RPRM/RPRF): its hair colour and complexion (indices above),
                             weight (0-100), types, face morphs (NAM9), head parts (editor ids) and
                             tint layers (colour: the layer's preset, value 0-100)
  PACK_DIR/chargen/<name>.skmask   "SKMK" u16 width, u16 height, then a byte a pixel
  PACK_DIR/chargen/<name>.skraw    "SKRW" u16 width, u16 height, then RGB
Skyrim makes the face from these as its FaceGen does (checked against its own face tints, to 1%):
the tint layers over grey, each blended in by its mask x value; the face = diffuse x detail x 4 (its blank is a quarter grey) x
tint x 2. The face's morphs: NAM9's 18 values, each a pair of the chargen .tri's morphs (below)
"""
import os
import struct

import numpy as np

import gear

KINDS = {2: "eyes", 3: "hair", 4: "beard", 5: "scar", 6: "brows"}
FACE = 1
FLAG_PLAYABLE, FLAG_MALE, FLAG_FEMALE = 1, 2, 4
TINT_SIZE = 256  # (the face texture's: npcs.FACE_TEXTURE_SIZE)


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


class HeadParts:
    def __init__(self, lo):
        self.lo = lo
        self.flists = {lo.global_id(p, r.form_id): r for p in lo.plugins for r in p.records_of_type("FLST")}
        self.races = {lo.global_id(p, r.form_id): r for p in lo.plugins for r in p.records_of_type("RACE")}
        self.parts = {}
        for p in lo.plugins:  # (the last plugin's wins)
            for h in p.records_of_type("HDPT"):
                self.parts[lo.global_id(p, h.form_id)] = h
        # Each model's race morphs (NAM0 0: a .tri of a morph per race - an elf's skull from the
        # one head Skyrim's humans and elves share; without it elf hair sat on a human head, its
        # crown showing through) and chargen morphs (NAM0 2: the race menu's - NoseLong, EyesType20...)
        self.race_tri, self.chargen_tri = {}, {}
        for h in self.parts.values():
            kind = None
            for t, v in h.subrecords():
                if t == "NAM0":
                    kind = _u32(v)
                elif t == "NAM1" and kind in (0, 2) and self._path(h):
                    tri = "meshes\\" + v.split(b"\0")[0].decode("latin-1").lower()
                    (self.race_tri if kind == 0 else self.chargen_tri).setdefault(self._path(h), tri)

    def _tri(self, find, archives, tri):
        from npcs import read_tri_morphs
        if tri not in self._tri_cache:
            try:
                self._tri_cache[tri] = read_tri_morphs(find(archives, tri))
            except (KeyError, OSError, ValueError, struct.error):
                self._tri_cache[tri] = (0, {})
        return self._tri_cache[tri]

    def race_morphs(self, find, archives, race_eid, models):
        """{model path: (N,3) offsets, shape units} - each model's morph for the race, where it has one."""
        out = {}
        for m in models:
            tri = self.race_tri.get(m)
            if not tri:
                continue
            nv, morphs = self._tri(find, archives, tri)
            if race_eid in morphs:
                out[m] = morphs[race_eid]
        return out

    def chargen_morphs(self, find, archives, models):
        """{model path: {morph name: (N,3) offsets}} - the race menu's morphs of each model that has them
        (VampireMorph left out: a vampire's, not a slider's)."""
        out = {}
        for m in models:
            tri = self.chargen_tri.get(m)
            if tri:
                _nv, morphs = self._tri(find, archives, tri)
                if morphs:
                    out[m] = {k: v for k, v in morphs.items() if k != "VampireMorph"}
        return out

    _tri_cache = {}

    def _races_of(self, h):
        rn = h.sub("RNAM")
        fl = self.flists.get(self.lo.global_id(h.plugin, _u32(rn))) if rn else None
        if fl is None:
            return set()
        out = set()
        for t, v in fl.subrecords():
            if t == "LNAM" and len(v) >= 4:
                r = self.races.get(self.lo.global_id(fl.plugin, _u32(v)))
                if r is not None:
                    out.add(r.editor_id())
        return out

    def _path(self, h):
        m = h.sub("MODL")
        return "meshes\\" + m.split(b"\0")[0].decode("latin-1").lower().replace("/", "\\") if m and len(m) > 4 else None

    def _with_extras(self, h, depth=0):
        """Its model and its extra parts' (HNAM: a face's mouth, a hair's hairline)."""
        out = [self._path(h)] if self._path(h) else []
        for t, v in h.subrecords():
            if t == "HNAM" and len(v) >= 4 and depth < 3:
                x = self.parts.get(self.lo.global_id(h.plugin, _u32(v)))
                if x is not None:
                    out += self._with_extras(x, depth + 1)
        return out

    def texture_of(self, eid):
        """A part's own diffuse (its texture set, TNAM: Skyrim's eye colours, brows and scars share
        a mesh and differ by it - all eight eyes were one), or None."""
        for h in self.parts.values():
            if h.editor_id() != eid:
                continue
            t = h.sub("TNAM")
            ts = self.lo.record(self.lo.global_id(h.plugin, _u32(t)), types=("TXST",)) if t else None
            tx = ts.sub("TX00") if ts is not None else None
            if tx and len(tx) > 4:
                return "textures\\" + tx.split(b"\0")[0].decode("latin-1").lower().replace("/", "\\")
        return None

    def race_defaults(self, race_eid, female):
        """The race's default head parts of no kind the player picks (HDPT type 0, Misc: the mouth -
        its teeth, tongue and dark inside), from the RACE's head data (MNAM / FNAM, then HEAD
        form ids): models. A Custom face without them showed the back of its head through its
        lips."""
        race = next((r for r in self.races.values() if r.editor_id() == race_eid), None)
        if race is None:
            return []
        out, sex = [], None
        for t, v in race.subrecords():
            if t == "MNAM":
                sex = False
            elif t == "FNAM":
                sex = True
            elif t == "HEAD" and len(v) >= 4 and sex == bool(female):
                h = self.parts.get(self.lo.global_id(race.plugin, _u32(v)))
                pn = h.sub("PNAM") if h is not None else None
                if h is not None and pn and _u32(pn) == 0:
                    out += [m for m in self._with_extras(h) if m not in out]
        return out

    def for_race(self, race_eid, female):
        """(base head models, [(kind, part editor id, models)]) for a race and sex."""
        face, picks = None, []
        for g in sorted(self.parts):
            h = self.parts[g]
            d, pn = h.sub("DATA"), h.sub("PNAM")
            if not d or not pn or not d[0] & FLAG_PLAYABLE:
                continue
            sexes = d[0] & (FLAG_MALE | FLAG_FEMALE)
            if sexes and not (sexes & (FLAG_FEMALE if female else FLAG_MALE)):
                continue
            if race_eid not in self._races_of(h):
                continue
            kind = _u32(pn)
            models = self._with_extras(h)
            if not models:
                continue
            if kind == FACE:
                face = face or models
            elif kind in KINDS:
                picks.append((KINDS[kind], h.editor_id(), models))
        return face, picks


# --- The race menu's data (RACE: tint layers, presets, hair colours, complexions, types) ------------

class _Images:
    """The masks and diffuses the look is made from, written once each under PACK_DIR/chargen/."""

    def __init__(self, exporter, out_dir):
        self.exporter, self.out_dir, self.names, self.taken = exporter, out_dir, {}, set()

    def _name(self, path):
        stem = path.replace("/", "\\").split("\\")[-1].rsplit(".", 1)[0].lower()
        name, n = stem, 2
        while name in self.taken:
            name, n = f"{stem}_{n}", n + 1
        self.taken.add(name)
        return name

    def get(self, path, mode):
        """mode "L": a mask (.skmask); "RGB": a diffuse (.skraw). Returns its name, or None."""
        key = (path.lower(), mode)
        if key in self.names:
            return self.names[key]
        from io import BytesIO
        from PIL import Image
        try:
            im = Image.open(BytesIO(self.exporter.find(self.exporter.models.tex_archives, path))).convert(mode)
        except (KeyError, OSError, ValueError):
            self.names[key] = None
            return None
        im = im.resize((TINT_SIZE, TINT_SIZE), Image.LANCZOS)
        name = self._name(path)
        magic, ext = (b"SKMK", ".skmask") if mode == "L" else (b"SKRW", ".skraw")
        with open(os.path.join(self.out_dir, name + ext), "wb") as f:
            f.write(struct.pack("<4sHH", magic, im.width, im.height) + im.tobytes())
        self.names[key] = name
        return name


def _clfm_rgb(lo, plugin, form):
    rec = lo.record(lo.global_id(plugin, form), types=("CLFM",))
    c = rec.sub("CNAM") if rec is not None else None
    return tuple(c[:3]) if c and len(c) >= 3 else None


def _hex(rgb):
    return "%02x%02x%02x" % tuple(rgb)


def _race_look(lo, hp, race, female, images, picks):
    """The race menu's lines for a race and sex (the module's docstring), or None."""
    tex = lambda b: "textures\\" + b.split(b"\0")[0].decode("latin-1").lower().replace("/", "\\")
    layers, colours = [], {}  # [(index, type, mask)], {index: [(rgb, value, preset id, form)]}
    hair, complexions, default_cx, types, presets = [], [], None, {}, []
    sex, cur, last_c, mpai = None, None, None, None
    for t, v in race.subrecords():
        if t == "MNAM":
            sex = False
        elif t == "FNAM":
            sex = True
        if sex is not bool(female):
            continue
        if t == "TINI":
            cur = [struct.unpack("<H", v)[0], 0, None, None]
            layers.append(cur)
            colours[cur[0]] = []
        elif t == "TIND" and cur is not None:
            cur[3] = _u32(v)  # (its default colour: what a preset without the layer has - the skin tone)
        elif t == "TINT" and cur is not None:
            cur[2] = images.get(tex(v), "L")
        elif t == "TINP" and cur is not None:
            cur[1] = struct.unpack("<H", v)[0]
        elif t == "TINC" and cur is not None:
            last_c = [_clfm_rgb(lo, race.plugin, _u32(v)), 1.0, 0, _u32(v)]
            colours[cur[0]].append(last_c)
        elif t == "TINV" and last_c is not None:
            last_c[1] = struct.unpack("<f", v)[0]
        elif t == "TIRS" and last_c is not None:
            last_c[2] = struct.unpack("<H", v)[0]
        elif t in ("AHCM", "AHCF"):
            rgb = _clfm_rgb(lo, race.plugin, _u32(v))
            if rgb:
                hair.append((rgb, lo.global_id(race.plugin, _u32(v))))
        elif t in ("FTSM", "FTSF"):
            ts = lo.record(lo.global_id(race.plugin, _u32(v)), types=("TXST",))
            if ts is not None and ts.sub("TX00"):
                base = images.get(tex(ts.sub("TX00")), "RGB")
                detail = images.get(tex(ts.sub("TX04")), "L") if ts.sub("TX04") else None
                if base:
                    complexions.append((base, detail or "-", lo.global_id(race.plugin, _u32(v))))
        elif t in ("DFTM", "DFTF"):
            default_cx = lo.global_id(race.plugin, _u32(v))
        elif t == "MPAI":
            mpai = _u32(v)
        elif t == "MPAV" and mpai is not None:
            types[mpai] = _u32(v)
        elif t in ("RPRM", "RPRF"):
            npc = lo.record(lo.global_id(race.plugin, _u32(v)), types=("NPC_",))
            if npc is not None:
                presets.append(npc)
    if not complexions:
        return None
    if default_cx is not None:  # (the race's default first)
        complexions.sort(key=lambda c: c[2] != default_cx)
    lines = [f"X|{b}|{d}\n" for b, d, _g in complexions]
    layer_of = {}
    for index, kind, mask, default in layers:
        cs = [c for c in colours[index] if c[0] is not None]
        if not mask or not kind or not cs:
            continue
        layer_of[index] = (kind, cs)
        di = next((i for i, c in enumerate(cs) if c[3] == default), 0)
        lines.append(f"L|{index}|{kind}|{mask}|{di}\n")
        lines += [f"C|{index}|{_hex(rgb)}|{value:.3f}\n" for rgb, value, _p, _f in cs]
    lines += [f"H|{_hex(rgb)}\n" for rgb, _g in hair]
    lines += [f"T|{g}|{m:08x}\n" for g, m in sorted(types.items()) if g in (0, 2, 3)]
    part_eids = {eid for _kind, eid, _m in picks}
    for n, npc in enumerate(presets):
        lines.append(_preset_line(lo, hp, n, npc, layer_of, hair, complexions, part_eids))
    return "".join(lines)


def _preset_line(lo, hp, n, npc, layer_of, hair, complexions, part_eids):
    u = lambda v: _u32(v)
    hc = npc.sub("HCLF")
    hcol = _clfm_rgb(lo, npc.plugin, u(hc)) if hc else None
    hair_i = 0
    if hcol is not None and hair:  # (its colour among the race's: the nearest)
        hair_i = int(np.argmin([sum((a - b) ** 2 for a, b in zip(rgb, hcol)) for rgb, _g in hair]))
    ft = npc.sub("FTST")
    cx = 0
    if ft:
        g = lo.global_id(npc.plugin, u(ft))
        cx = next((i for i, c in enumerate(complexions) if c[2] == g), 0)
    w = npc.sub("NAM7")
    weight = int(round(struct.unpack("<f", w)[0])) if w else 50
    n9 = npc.sub("NAM9")
    morphs = struct.unpack("<18f", n9[:72]) if n9 and len(n9) >= 72 else (0.0,) * 18
    na = npc.sub("NAMA")
    nose, _u, eyes, mouth = struct.unpack("<4i", na[:16]) if na and len(na) >= 16 else (0, -1, 0, 0)
    parts = []
    for t, v in npc.subrecords():
        if t == "PNAM":
            h = hp.parts.get(lo.global_id(npc.plugin, u(v)))
            pn = h.sub("PNAM") if h is not None else None
            if h is not None and pn and u(pn) in KINDS and h.editor_id() in part_eids:
                parts.append(f"{KINDS[u(pn)]}:{h.editor_id()}")
    tints, cur = [], None
    for t, v in npc.subrecords():
        if t == "TINI":
            cur = [struct.unpack("<H", v)[0], None, 0, -1]
        elif t == "TINC" and cur is not None:
            cur[1] = tuple(v[:3])
        elif t == "TINV" and cur is not None:
            cur[2] = struct.unpack("<i", v)[0]
        elif t == "TIAS" and cur is not None:
            cur[3] = struct.unpack("<h", v)[0]
            tints.append(cur)
            cur = None
    tl = []
    for index, rgb, value, preset in tints:
        if index not in layer_of or value <= 0 or rgb is None:
            continue
        _kind, cs = layer_of[index]
        ci = next((i for i, c in enumerate(cs) if c[2] == preset), None)
        if ci is None:  # (its own colour: the layer's nearest)
            ci = int(np.argmin([sum((a - b) ** 2 for a, b in zip(c[0], rgb)) for c in cs]))
        tl.append(f"{index}:{ci}:{value}")
    m = ",".join(f"{x:.3f}" if abs(x) < 10 else "0" for x in morphs)
    return (f"S|{n}|{hair_i}|{cx}|{weight}|{max(nose, 0)},{max(eyes, 0)},{max(mouth, 0)}|{m}|"
            f"{','.join(parts)}|{','.join(tl)}\n")


def export(exporter, pack, donors):
    """donors: {(race editor id, female): (npc, gid, race global id, family)} - someone of that
    race and sex (their skeleton). Writes the parts, chargen.txt and the race menu's data; returns
    {(race, female): base head models} for the Custom faces."""
    out_dir = os.path.join(pack, "actors", "head")
    os.makedirs(out_dir, exist_ok=True)
    look_dir = os.path.join(pack, "chargen")
    os.makedirs(look_dir, exist_ok=True)
    for old in os.listdir(look_dir):
        os.remove(os.path.join(look_dir, old))
    hp = HeadParts(exporter.lo)
    exporter.head_parts = hp
    images = _Images(exporter, look_dir)
    lines, faces, done, looks = [], {}, {}, 0
    for (race_eid, female), (npc, gid, race_id, family) in sorted(donors.items()):
        face, picks = hp.for_race(race_eid, female)
        if face:
            faces[(race_eid, female)] = face + [m for m in hp.race_defaults(race_eid, female) if m not in face]
        for kind, eid, models in picks:
            morphs = hp.race_morphs(exporter.find, exporter.models.mesh_archives, race_eid, models)
            tex = hp.texture_of(eid)
            key = (tuple(models), female, family, race_eid if morphs else "", tex or "")
            if key not in done:
                stem = f"hp{len(done):04d}"
                exporter.race_morphs = morphs
                exporter.texture_override = {models[0]: tex} if tex else {}
                # (its chargen morphs, as the face's: a beard and brows follow the jaw and brow; hair
                # left white for the engine's hair colour)
                exporter.morph_sources = hp.chargen_morphs(exporter.find, exporter.models.mesh_archives, models)
                exporter.chargen_mode = True
                _sk, third = exporter._batches(npc, gid, (female, race_id, [(m, False, 1) for m in models]))
                extras = list(exporter.last_extras) if third else []
                exporter.race_morphs, exporter.texture_override, exporter.morph_sources = {}, {}, {}
                exporter.chargen_mode = False
                if third:
                    gear._write(os.path.join(out_dir, stem + ".skgear"), 0, 0, third, [], (extras, []))
                    done[key] = stem
                else:
                    done[key] = None
            if done[key]:
                lines.append(f"P|{race_eid}|{int(female)}|{kind}|{eid}|{done[key]}\n")
        race = hp.races.get(race_id)
        text = _race_look(exporter.lo, hp, race, female, images, picks) if race is not None else None
        if text:
            with open(os.path.join(look_dir, f"{race_eid.lower()}_{'f' if female else 'm'}.txt"), "w") as f:
                f.write(text)
            looks += 1
    with open(os.path.join(pack, "chargen.txt"), "w") as f:
        f.write("".join(lines))
    print(f"chargen: {len(lines)} choices, {sum(1 for v in done.values() if v)} part models, "
          f"{len(faces)} base heads, {looks} race menus, {len(images.taken)} masks and diffuses")
    return faces
