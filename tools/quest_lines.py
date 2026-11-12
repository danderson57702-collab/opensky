"""Re-exports some of quests.txt's conversations and scenes (by key) without the full re-export:
their D / S / R lines replaced, the rest kept; their voices converted into pack/voice/.
   cd converter && python3 ../tools/quest_lines.py ../pack c02_induction c01_skjor ... [--places]"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "converter"))
from esm import LoadOrder
from extract_textures import SKYRIM_DATA
from dialogue import Dialogue
import quests

pack, keys = sys.argv[1], set(a for a in sys.argv[2:] if a != "--places")
places = "--places" in sys.argv  # (and every quest mark - Q lines - remade)
lo = LoadOrder(SKYRIM_DATA)
talk = Dialogue(lo, {}, None, pack, keep_voice=True)  # (never the voices wiped: the people's are there too)
by_eid = {}
for p in lo.plugins:
    for t in ("QUST", "NPC_"):
        for r in p.records_of_type(t):
            by_eid[(t, (r.editor_id() or "").lower())] = r
missing, new = [], []
quests.TREES = tuple(t for t in quests.TREES if t[0] in keys)
quests.SCENE_EXPORTS = tuple(t for t in quests.SCENE_EXPORTS if t[0] in keys)
new += quests._trees(lo, talk, by_eid, missing) + quests._scenes(lo, talk, by_eid, missing)
for key, speaker, parts in quests.SCENES:  # (single lines: as export() writes them)
    if key not in keys:
        continue
    npc = by_eid.get(("NPC_", speaker.lower()))
    voice = talk.voice_types.get(lo.global_id(npc.plugin, quests._u32(npc.sub("VTCK")))) if npc is not None and \
        npc.sub("VTCK") else "*"
    got = [r for topic, quest, opening in parts for r in quests._topic_responses(lo, talk, by_eid, topic, quest, opening, voice)]
    if got:
        new.append(f"S\t{key}\t{'-' if speaker == '*' else speaker.lower()}")
        new += [f"R\t{key}\t{path}\t" + text.replace("\t", " ").replace("\n", " ") for path, text in got]
if places:
    new += quests._quest_places(lo, missing)
path = os.path.join(pack, "quests.txt")
with open(path, encoding="cp1252") as f:
    old = f.read().split("\n")
ours = keys | {k + "_f" for k in keys}  # (and their women's versions)
kept = [l for l in old if not (l[:1] in "DSR" and len(l.split("\t")) > 1 and l.split("\t")[1] in ours)
        and not (places and l.startswith("Q\t"))]
with open(path, "w", encoding="cp1252", errors="replace") as f:
    f.write("\n".join([l for l in kept if l] + new) + "\n")
print(f"quest lines: {len(new)} for {sorted(keys)}; missing {missing}")
