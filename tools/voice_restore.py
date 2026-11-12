"""Converts again every voice file the pack refers to that isn't in pack/voice/ (its quests,
topics and the people's .sktalk files): from Skyrim - Voices_en0.bsa, as the export does.
   cd converter && python3 ../tools/voice_restore.py ../pack"""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "converter"))
from esm import LoadOrder
from extract_textures import SKYRIM_DATA
from dialogue import Dialogue

pack = sys.argv[1]
wanted = set()
pat = re.compile(rb"([a-z0-9_]+/[a-z0-9_]+\.skvoc)")
for root, _dirs, files in os.walk(pack):
    if os.path.basename(root) == "voice" or "/voice/" in root + "/":
        continue
    for f in files:
        if f.endswith((".txt", ".sktalk")):
            with open(os.path.join(root, f), "rb") as fh:
                wanted.update(m.decode() for m in pat.findall(fh.read()))
missing = sorted(w for w in wanted if not os.path.exists(os.path.join(pack, "voice", w)))
talk = Dialogue(LoadOrder(SKYRIM_DATA), {}, None, pack, keep_voice=True)
by_name = {}
for e in talk.voices.entries:  # (the same name _convert gives it)
    parts = e.split("\\")
    if len(parts) != 5 or not e.endswith(".fuz"):
        continue
    bits = parts[4].rsplit("_", 2)
    if len(bits) == 3:
        by_name[f"{parts[3]}/{parts[2].rsplit('.', 1)[0]}_{bits[1]}_{bits[2][:-4]}.skvoc"] = e
done = lost = 0
for w in missing:
    if w in by_name and talk._convert(by_name[w]):
        done += 1
    else:
        lost += 1
print(f"voices: {len(wanted)} referred to, {len(missing)} missing, {done} converted, {lost} not found")
