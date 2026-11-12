#!/usr/bin/env python3
"""The game's lines by number. OpenSKY's source names a line of dialogue by its record's form id
(INFO 000D1234: something said; DIAL 000D1230: something you can say) and never by its words -
none of the game's text is in the source. This reads the words from your own copy:

    python3 tools/lines.py 000D1234 000D1230       the words of these numbers
    python3 tools/lines.py --find "words to look for"   the numbers of lines with these words
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "converter"))


def index():
    """{form id: (kind, [texts])} of every INFO (its responses, its prompt) and DIAL (its prompt)."""
    from esm import LoadOrder
    from extract_textures import SKYRIM_DATA
    from dialogue import StringTables
    import struct
    lo = LoadOrder(SKYRIM_DATA)
    strings = StringTables(lo)
    u32 = lambda b: struct.unpack_from("<I", b)[0]
    out = {}
    for p in lo.plugins:
        for d in p.records_of_type("DIAL"):
            full = d.sub("FULL")
            text = strings.get(p, u32(full), "strings") if full and len(full) == 4 else ""
            if text:
                out[lo.global_id(p, d.form_id)] = ("DIAL", [text])
        for infos in p.topic_infos().values():
            for info in infos:
                texts = []
                for t, v in info.subrecords():
                    if t == "NAM1" and len(v) == 4:
                        texts.append(strings.get(info.plugin, u32(v), "ilstrings") or "")
                    elif t == "RNAM" and len(v) == 4:
                        texts.append("(you) " + (strings.get(info.plugin, u32(v), "strings") or ""))
                texts = [t for t in texts if t.strip() and t != "(you) "]
                if texts:
                    out[lo.global_id(info.plugin, info.form_id)] = ("INFO", texts)
    return out


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    lines = index()
    if args[0] == "--find":
        want = " ".join(args[1:]).lower()
        for gid, (kind, texts) in sorted(lines.items()):
            if any(want in t.lower() for t in texts):
                print(f"{kind} {gid:08X}  " + " / ".join(texts))
        return
    for a in args:
        gid = int(a.lstrip("#"), 16)
        kind, texts = lines.get(gid, ("?", ["(no such line)"]))
        print(f"{kind} {gid:08X}  " + " / ".join(texts))


if __name__ == "__main__":
    main()
