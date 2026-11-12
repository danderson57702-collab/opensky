"""NPC greetings and idle chatter: Skyrim's voiced lines with subtitles.

For each exported NPC (npcs.py) this picks the lines of the "Hello" and "Idle" topics of
quests that run from the start of the game whose conditions hold for that NPC. Only
conditions that can be decided ahead of time are evaluated (who is speaking, their race,
sex, voice type, factions, class, where they spend the day, global values, quest stages
at the start of the game); a line with any other condition (about the player, quest
aliases, combat...) is left out. Voice files (.fuz: lip data + xWMA) are converted to
mono IMA ADPCM; text comes from the game's string tables.

  PACK_DIR/actors/<editor id>.sktalk
  PACK_DIR/voice/<voice type>/<plugin>_<form id>_<n>.skvoc

.sktalk layout (little-endian), version 1:
  char[4] "SKTK", u16 version, u16 line count, u8 name length, name (cp1252)
  per line: u8 kind (0 hello in passing, 1 idle chatter, 2 greeting when talked to),
    u8 response count;
    per response: u8 path length, path (under voice/), u16 text length, text (cp1252)
.skvoc: the .skmus layout (extract_music.py) with one channel

Also the subtitle font, rendered from Pillow's bundled font (Aileron, SIL Open Font
License): PACK_DIR/ui/font.skfont
  char[4] "SKFT", u16 version, u16 0, u16 atlas width, height, u16 cell width, height,
  u16 line height, u8 first character, u8 count, u8 advance[count] (pixels),
  then width x height u8 coverage. Characters are cp1252 codes; cell i is at
  ((i % 16) * cell width, (i // 16) * cell height). Character 127 is a solid block.
"""
import ctypes
import io
import os
import random
import struct
from collections import Counter

import av
import numpy as np

from bsa import BSA
from extract_music import load_adpcm
from extract_textures import SKYRIM_DATA

SKTK_VERSION = 1
SKVOC_VERSION = 1
VOICE_RATE = 22050
VOICE_BLOCK_FRAMES = 1024
VOICE_PEAK = 0.85
TOPIC_GREETING, TOPIC_HELLO, TOPIC_IDLE = 73, 79, 88   # DIAL DATA subtype
KIND_HELLO, KIND_IDLE, KIND_GREETING = 0, 1, 2  # passing hello, chatter, when talked to
TOPIC_KINDS = {TOPIC_HELLO: KIND_HELLO, TOPIC_IDLE: KIND_IDLE, TOPIC_GREETING: KIND_GREETING}
MAX_LINES = {KIND_HELLO: 12, KIND_IDLE: 8, KIND_GREETING: 12}
PLAYER_FEMALE = 0  # the player isn't modelled; lines about them assume a man
QUEST_START_GAME_ENABLED = 0x0001
# Greetings that need a state we don't model (commanded dead, thralls, followers, spouses,
# vampires and werewolves, combat, crimes, guild members): their condition is the state
# the game puts the speaker in, not anything on the line
EXCLUDED = ("commanded", "vampire", "werewolf", "thrall", "creature", "follower", "hireling",
            "relationship", "marriage", "combat", "darkbrotherhood", "byoh", "bandit", "pickpocket",
            "soulcairn", "caravan", "cwdialogue", "da0", "da1", "ms0", "ms1", "mq0", "t01", "tgpre",
            "solitudeopening", "bardscollege")

# Condition functions (CTDA) decidable per NPC
F_GET_DISTANCE, F_GET_STAGE, F_GET_STAGE_DONE, F_GET_QUEST_RUNNING = 1, 58, 59, 56
F_GET_DEAD, F_GET_TALKED_TO_PC, F_GET_PC_IS_SEX = 46, 50, 131
F_GET_IS_CLASS, F_GET_IS_RACE, F_GET_IS_SEX, F_GET_IN_FACTION, F_GET_IS_ID = 68, 69, 70, 71, 72
F_GET_FACTION_RANK, F_GET_GLOBAL_VALUE, F_GET_RANDOM_PERCENT = 73, 74, 77
F_GET_IN_WORLDSPACE, F_GET_IN_CURRENT_LOC, F_GET_IS_VOICE_TYPE, F_LOCATION_HAS_KEYWORD = 310, 359, 426, 562
RUN_ON_SUBJECT = 0
CTDA_OR, CTDA_USE_GLOBAL = 0x01, 0x04
OPS = (lambda a, b: a == b, lambda a, b: a != b, lambda a, b: a > b, lambda a, b: a >= b,
       lambda a, b: a < b, lambda a, b: a <= b)


# Which of the game's lines a table means (quests.py's SCENES and TREES, topics.py's TOPICS...): by
# the line's own number - "#000D1234", its INFO record's form id in the official load order; several
# as "#000D1234+000D1235", either of two choices as "#...|#..." - never by its words: none of the
# game's text is in this source. (tools/lines.py shows a number's words, and finds the number for
# words, from your own copy.) Words still work, for trying a new line out before it's given its number.
LINES_SEEN = {} if os.environ.get("OPENSKY_RECORD_LINES") else None  # (words -> the numbers they chose: tools/lines.py)


def line_is(lo, info, text, which):
    """Whether the INFO (its first response's text) is the line `which` names; "": any line is."""
    if not which:
        return True
    gid = "%08X" % lo.global_id(info.plugin, info.form_id)
    for alt in (which if isinstance(which, tuple) else which.split("|")):
        if alt.startswith("#"):
            if gid in alt[1:].split("+"):
                return True
        elif text.startswith(alt):
            if LINES_SEEN is not None:
                LINES_SEEN.setdefault(alt, set()).add(gid)
            return True
    return False


def _u32(b, o=0):
    return struct.unpack_from("<I", b, o)[0]


SKFT_VERSION = 1
FONT_SIZE = 22


def write_font(pack):
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.load_default(size=FONT_SIZE)
    first, count, cols, cell_w, cell_h = 32, 224, 16, 26, 30
    width, height = 512, 512
    img = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(img)
    advances = []
    for i in range(count):
        code = first + i
        x, y = (i % cols) * cell_w, (i // cols) * cell_h
        if code == 127:
            draw.rectangle((x, y, x + cell_w - 1, y + cell_h - 1), fill=255)
            advances.append(cell_w)
            continue
        ch = bytes([code]).decode("cp1252", "replace")
        draw.text((x + 1, y + 2), ch, font=font, fill=255)
        advances.append(min(cell_w, int(round(font.getlength(ch)))))
    ascent, descent = font.getmetrics()
    os.makedirs(os.path.join(pack, "ui"), exist_ok=True)
    with open(os.path.join(pack, "ui", "font.skfont"), "wb") as f:
        f.write(struct.pack("<4sHHHHHHHBB", b"SKFT", SKFT_VERSION, 0, width, height, cell_w, cell_h,
                            ascent + descent + 4, first, count))
        f.write(bytes(advances))
        f.write(img.tobytes())


class StringTables:
    """Localized strings (the plugins store ids into strings/<plugin>_english.*)."""

    def __init__(self, load_order):
        archive = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Interface.bsa"))
        self.tables = {}
        for p in load_order.plugins:
            base = p.name.lower().rsplit(".", 1)[0]
            for ext in ("strings", "dlstrings", "ilstrings"):
                path = f"strings\\{base}_english.{ext}"
                if path in archive.entries:
                    self.tables[(p.name.lower(), ext)] = self._parse(archive.read(path), ext != "strings")

    @staticmethod
    def _parse(data, sized):
        count = _u32(data)
        base = 8 + count * 8
        out = {}
        for i in range(count):
            sid, off = struct.unpack_from("<II", data, 8 + i * 8)
            p = base + off
            if sized:
                n = _u32(data, p)
                raw = data[p + 4:p + 4 + n]
            else:
                raw = data[p:data.index(b"\0", p)]
            out[sid] = raw.rstrip(b"\0").decode("cp1252", "replace")
        return out

    def get(self, plugin, sid, ext):
        return self.tables.get((plugin.name.lower(), ext), {}).get(sid, "")


class Dialogue:
    def __init__(self, load_order, cells, world, pack, keep_voice=False):
        self.lo, self.cells, self.world, self.pack = load_order, cells, world, pack
        self.strings = StringTables(load_order)
        self.voices = BSA(os.path.join(SKYRIM_DATA, "Skyrim - Voices_en0.bsa"))
        self.voice_index = {}  # (plugin, voice type, local form id, response) -> path
        for n in self.voices.entries:
            parts = n.split("\\")
            if len(parts) != 5 or not n.endswith(".fuz"):
                continue
            bits = parts[4][:-4].rsplit("_", 2)
            try:
                self.voice_index[(parts[2], parts[3], int(bits[1], 16), int(bits[2]))] = n
            except (ValueError, IndexError):
                continue
        lo = load_order
        self.globals, self.quests, self.voice_types, self.locations = {}, {}, {}, {}
        for p in lo.plugins:
            for g in p.records_of_type("GLOB"):
                v = g.sub("FLTV")
                if v:
                    self.globals[lo.global_id(p, g.form_id)] = struct.unpack("<f", v)[0]
            for q in p.records_of_type("QUST"):
                d = q.sub("DNAM")
                self.quests[lo.global_id(p, q.form_id)] = bool(d and struct.unpack_from("<H", d)[0] & QUEST_START_GAME_ENABLED)
            for v in p.records_of_type("VTYP"):
                self.voice_types[lo.global_id(p, v.form_id)] = v.editor_id().lower()
            for loc in p.records_of_type("LCTN"):
                parent = loc.sub("PNAM")
                kw = loc.sub("KWDA") or b""
                self.locations[lo.global_id(p, loc.form_id)] = (
                    lo.global_id(p, _u32(parent)) if parent else None,
                    {lo.global_id(p, _u32(kw, i * 4)) for i in range(len(kw) // 4)})
        # Interior cells -> their location (the Sleeping Giant Inn, within Riverwood): where
        # someone who spends their days indoors is
        self.cell_locations = {}
        for p in lo.plugins:
            for cid, c in p.interior_cells().items():
                xlcn = c["cell"].sub("XLCN") if c["cell"] is not None else None
                if xlcn:
                    self.cell_locations[lo.global_id(p, cid)] = lo.global_id(c["cell"].plugin, _u32(xlcn))
        # Lines of the greeting and idle topics of quests running from the start
        self.lines = []
        self.infos = {}
        for p in lo.plugins:
            topics = p.topic_infos()
            for dial in p.records_of_type("DIAL"):
                for info in topics.get(dial.form_id, []):
                    self.infos[lo.global_id(p, info.form_id)] = info
                data, qnam = dial.sub("DATA"), dial.sub("QNAM")
                if not data or len(data) < 4 or not qnam:
                    continue
                subtype = struct.unpack_from("<H", data, 2)[0]
                quest = lo.global_id(p, _u32(qnam))
                if subtype not in TOPIC_KINDS or not self.quests.get(quest):
                    continue
                qrec = lo.record(quest, types=("QUST",))
                names = ((qrec.editor_id() if qrec else "") + " " + dial.editor_id()).lower()
                if any(x in names for x in EXCLUDED):
                    continue
                kind = TOPIC_KINDS[subtype]
                for info in topics.get(dial.form_id, []):
                    self.lines.append((kind, info))
        self.dll = load_adpcm()
        write_font(pack)
        self.converted = {}
        self.stats = Counter()
        voice_dir = os.path.join(pack, "voice")
        os.makedirs(voice_dir, exist_ok=True)
        for root, _dirs, files in os.walk(voice_dir if not keep_voice else os.devnull):  # converted again below (keep_voice:
                                                                                         #  a few re-made, the rest kept)
            for f in files:
                if f.endswith(".skvoc"):
                    os.remove(os.path.join(root, f))

    # --- conditions ----------------------------------------------------------------------

    def _form_list(self, gid):
        r = self.lo.record(gid, types=("FLST",))
        return {self.lo.global_id(r.plugin, _u32(v)) for t, v in r.subrecords() if t == "LNAM"} if r else set()

    def _in_location(self, loc, target):
        seen = 0
        while loc is not None and seen < 16:
            if loc == target:
                return True
            loc = self.locations.get(loc, (None, set()))[0]
            seen += 1
        return False

    def _facts(self, npc, gid, location):
        lo = self.lo
        ref = lambda t: lo.global_id(npc.plugin, _u32(npc.sub(t))) if npc.sub(t) else None
        return {
            "id": gid, "female": bool(_u32(npc.sub("ACBS")) & 1), "race": ref("RNAM"), "voice": ref("VTCK"),
            "class": ref("CNAM"), "location": location,
            "factions": {lo.global_id(npc.plugin, _u32(v)): struct.unpack_from("<b", v, 4)[0]
                         for t, v in npc.subrecords() if t == "SNAM"},
        }

    def _value(self, fn, param, f):
        """The condition function's value for the NPC, or None if it can't be known."""
        if fn == F_GET_IS_ID:
            return float(param == f["id"])
        if fn == F_GET_IS_RACE:
            return float(param == f["race"])
        if fn == F_GET_IS_SEX:
            return float(int(f["female"]) == (param & 0xFFFFFFFF))
        if fn == F_GET_IS_CLASS:
            return float(param == f["class"])
        if fn == F_GET_IN_FACTION:
            return float(param in f["factions"])
        if fn == F_GET_FACTION_RANK:
            return float(f["factions"].get(param, -1))
        if fn == F_GET_IS_VOICE_TYPE:
            return float(param == f["voice"] or f["voice"] in self._form_list(param))
        if fn == F_GET_GLOBAL_VALUE:
            return self.globals.get(param, 0.0)
        if fn == F_GET_RANDOM_PERCENT:
            return 50.0  # random gating: the line is in the pool
        if fn == F_GET_IN_WORLDSPACE:
            return 1.0 if param == self._tamriel else 0.0
        if fn == F_GET_IN_CURRENT_LOC:
            return float(f["location"] is not None and self._in_location(f["location"], param))
        if fn == F_LOCATION_HAS_KEYWORD:
            loc, found = f["location"], False
            while loc is not None and not found:
                parent, keywords = self.locations.get(loc, (None, set()))
                found, loc = param in keywords, parent
            return float(found)
        if fn == F_GET_TALKED_TO_PC:
            return 0.0  # a stranger
        if fn == F_GET_PC_IS_SEX:
            return float(PLAYER_FEMALE == param)
        if fn in (F_GET_STAGE, F_GET_STAGE_DONE):
            return 0.0  # the start of the game
        if fn == F_GET_QUEST_RUNNING:
            return float(self.quests.get(param, False))
        return None

    @staticmethod
    def _personal(info):
        """Written for a particular person or place (GetIsID / GetInCurrentLoc == 1). Generic
        greetings react to the player's state (magic in hand, disease, past fights)."""
        for t, v in info.subrecords():
            if t == "CTDA" and len(v) >= 24 and v[0] >> 5 == 0 and struct.unpack_from("<f", v, 4)[0] == 1.0 \
                    and struct.unpack_from("<H", v, 8)[0] in (F_GET_IS_ID, F_GET_IN_CURRENT_LOC):
                return True
        return False

    def _passes(self, info, facts):
        groups = [[]]
        for t, v in info.subrecords():
            if t != "CTDA" or len(v) < 24:
                continue
            op, flags = v[0] >> 5, v[0] & 0x1F
            fn, run_on = struct.unpack_from("<H", v, 8)[0], _u32(v, 20)
            if op >= len(OPS):
                return False
            if fn == F_GET_DEAD:
                value = 0.0  # everyone is alive at the start of the game
            elif run_on != RUN_ON_SUBJECT:
                return False
            else:
                param = (_u32(v, 12) if fn in (F_GET_IS_SEX, F_GET_PC_IS_SEX)
                         else self.lo.global_id(info.plugin, _u32(v, 12)))
                value = self._value(fn, param, facts)
            if value is None:
                self.stats[f"lines with undecidable conditions"] += 1
                return False
            comp = (self.globals.get(self.lo.global_id(info.plugin, _u32(v, 4)), 0.0) if flags & CTDA_USE_GLOBAL
                    else struct.unpack_from("<f", v, 4)[0])
            groups[-1].append(OPS[op](value, comp))
            if not flags & CTDA_OR:
                groups.append([])
        return all(any(g) for g in groups if g)

    # --- audio and text --------------------------------------------------------------------

    def _responses(self, info, voice_type):
        """[(voice file in the archive, text)] of an INFO, following shared responses."""
        shared = info.sub("DNAM")
        if shared:
            info = self.infos.get(self.lo.global_id(info.plugin, _u32(shared)), info)
        gid = self.lo.global_id(info.plugin, info.form_id)
        owner = self.lo.plugins[gid >> 24].name.lower()
        out, number = [], None
        for t, v in info.subrecords():
            if t == "TRDT" and len(v) >= 13:
                number = v[12]
            elif t == "NAM1" and number is not None:
                path = self.voice_index.get((owner, voice_type, gid & 0xFFFFFF, number))
                text = self.strings.get(info.plugin, _u32(v), "ilstrings") if len(v) == 4 else ""
                if not path or not text:
                    return None
                out.append((path, text))
                number = None
        return out or None

    def _convert(self, path):
        """Converts one .fuz; returns its path under voice/."""
        if path in self.converted:
            return self.converted[path]
        data = self.voices.read(path)
        if data[:4] != b"FUZE":
            return None
        lip = _u32(data, 8)
        container = av.open(io.BytesIO(data[12 + lip:]))
        resampler = av.AudioResampler(format="flt", layout="mono", rate=VOICE_RATE)
        chunks = []
        for frame in container.decode(container.streams.audio[0]):
            for out in resampler.resample(frame):
                chunks.append(out.to_ndarray().reshape(-1))
        for out in resampler.resample(None):
            chunks.append(out.to_ndarray().reshape(-1))
        pcm = np.concatenate(chunks) if chunks else np.zeros(1, np.float32)
        pcm = pcm * min(3.0, VOICE_PEAK / max(float(np.abs(pcm).max()), 1e-6))
        pcm = np.ascontiguousarray(np.clip(pcm * 32767.0, -32768, 32767).astype(np.int16))
        frames = len(pcm)
        blocks = (frames + VOICE_BLOCK_FRAMES - 1) // VOICE_BLOCK_FRAMES
        block_bytes = 4 + VOICE_BLOCK_FRAMES // 2
        body = ctypes.create_string_buffer(blocks * block_bytes)
        self.dll.encode_channel(pcm.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)), 1, frames,
                                VOICE_BLOCK_FRAMES, body, block_bytes)
        parts = path.split("\\")
        name = f"{parts[3]}/{parts[2].rsplit('.', 1)[0]}_{parts[4].rsplit('_', 2)[1]}_{parts[4].rsplit('_', 2)[2][:-4]}.skvoc"
        os.makedirs(os.path.join(self.pack, "voice", parts[3]), exist_ok=True)
        with open(os.path.join(self.pack, "voice", name), "wb") as f:
            f.write(struct.pack("<4sHHIII", b"SKMU", SKVOC_VERSION, 1, VOICE_RATE, frames, VOICE_BLOCK_FRAMES))
            f.write(body.raw)
        self.converted[path] = name
        self.stats["voice files"] += 1
        self.stats["voice seconds"] += frames // VOICE_RATE
        return name

    # --- per NPC ----------------------------------------------------------------------------

    @property
    def _tamriel(self):
        if not hasattr(self, "_tamriel_id"):
            self._tamriel_id = next((self.lo.global_id(p, w.form_id) for p in self.lo.plugins
                                     for w in p.records_of_type("WRLD") if w.editor_id() == "Tamriel"), None)
        return self._tamriel_id

    def location_of(self, pos):
        """Location (LCTN) of the exterior cell at an engine-space position."""
        cell_m = 4096 / 70.0
        key = (int(np.floor(pos[0] / cell_m)), int(np.floor(-pos[2] / cell_m)))
        cell = self.cells.get(key, {}).get("cell")
        xlcn = cell.sub("XLCN") if cell is not None else None
        return self.lo.global_id(cell.plugin, _u32(xlcn)) if xlcn else None

    def _name(self, npc):
        full = npc.sub("FULL")
        name = self.strings.get(npc.plugin, _u32(full), "strings") if full and len(full) == 4 else npc.editor_id()
        return name.encode("cp1252", "replace")[:255]

    def export_name(self, npc, file_base):
        """Only their name (a quest's cast: Elenwen, Etienne - their lines are the quest's)."""
        name_b = self._name(npc)
        with open(file_base + ".sktalk", "wb") as f:
            f.write(struct.pack("<4sHHB", b"SKTK", SKTK_VERSION, 0, len(name_b)) + name_b)

    def export(self, npc, gid, outdoor_spots, file_base, indoor_cells=()):
        voice = self.voice_types.get(self.lo.global_id(npc.plugin, _u32(npc.sub("VTCK")))) if npc.sub("VTCK") else None
        if not voice:
            return
        location = next((loc for loc in map(self.location_of, outdoor_spots) if loc), None)
        if location is None:  # (indoors all day: the building's location)
            location = next((self.cell_locations[c] for c in indoor_cells if c in self.cell_locations), None)
        facts = self._facts(npc, gid, location)
        picked = {KIND_HELLO: [], KIND_IDLE: [], KIND_GREETING: []}
        for kind, info in self.lines:
            if kind == KIND_GREETING and not self._personal(info):
                continue
            if self._passes(info, facts):
                responses = self._responses(info, voice)
                if responses:
                    picked[kind].append(responses)
        rng = random.Random(gid)
        lines = []
        for kind, options in picked.items():
            rng.shuffle(options)
            for responses in options[:MAX_LINES[kind]]:
                converted = [(self._convert(p), text) for p, text in responses]
                if all(c for c, _t in converted):
                    lines.append((kind, converted))
        name_b = self._name(npc)
        data = bytearray(struct.pack("<4sHHB", b"SKTK", SKTK_VERSION, len(lines), len(name_b)) + name_b)
        for kind, responses in lines:
            data += struct.pack("<BB", kind, len(responses))
            for path, text in responses:
                p = path.encode("ascii")
                t = text.encode("cp1252", "replace")[:1000]
                data += struct.pack("<B", len(p)) + p + struct.pack("<H", len(t)) + t
        with open(file_base + ".sktalk", "wb") as f:
            f.write(data)
        self.stats["NPCs with lines"] += bool(lines)
        self.stats["lines"] += len(lines)
