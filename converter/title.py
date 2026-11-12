"""The game's title as the user's own data names it (Skyrim - Interface.bsa: the English
interface strings), for the title screen and the opening's title card - the engine carries no
name of its own (it shows "OpenSKY" without this file).

PACK_DIR/title.txt: two lines, the line over the name (INFO 020181BC) and the name
("Skyrim").

The game's makers credited as the game itself does (the user asked, Nov 8), from the same archive -
the engine names no one of its own:
PACK_DIR/legal.txt: the studio ("$Bethesda Game Studios" in the interface strings), then the game's
copyright and trademark notice (the credits' LEGAL paragraph, to its first "All Rights Reserved.").
(Only that: every name in the game's credits was too much at the start, the user said, Nov 8.)
"""
import os
import re
import unicodedata

from bsa import BSA


def export(data_dir, pack):
    try:
        b = BSA(os.path.join(data_dir, "Skyrim - Interface.bsa"))
        raw = b.read("interface\\translate_english.txt")
    except (OSError, KeyError):
        print("title: no interface strings")
        return False
    text = raw.decode("utf-16-le", errors="ignore") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("latin-1")
    for line in text.splitlines():
        key, _, value = line.partition("\t")
        m = re.match(r"\s*(The Elder Scrolls [IVX]+)\s*:\s*(\w+)", value)
        if key.startswith("$") and m:
            with open(os.path.join(pack, "title.txt"), "w") as f:
                f.write(f"{m.group(1)}\n{m.group(2)}\n")
            print(f"title: {m.group(1)}: {m.group(2)}")
            return True
    print("title: not found in the interface strings")
    return False


def _latin1(text):
    out = []
    for ch in text.replace("\u00a0", " "):
        try:
            ch.encode("latin-1")
            out.append(ch)
        except UnicodeEncodeError:  # (L-stroke and the like: their plain letter)
            out.append(unicodedata.normalize("NFKD", ch).encode("ascii", "ignore").decode() or "?")
    return "".join(out)


def export_credits(data_dir, pack):
    try:
        b = BSA(os.path.join(data_dir, "Skyrim - Interface.bsa"))
        raw = b.read("interface\\credits.txt")
        strings = b.read("interface\\translate_english.txt")
    except (OSError, KeyError):
        print("credits: not in the interface archive")
        return False
    text = raw.decode("utf-16-le", errors="ignore") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8", errors="replace")
    lines = []
    for line in text.splitlines():
        line = re.sub(r"<[^>]*>", "", line).strip()
        if line:
            lines.append(_latin1(line))
    st = strings.decode("utf-16-le", errors="ignore") if strings[:2] in (b"\xff\xfe", b"\xfe\xff") else strings.decode("latin-1")
    studio = ""
    for line in st.splitlines():
        key, _, value = line.partition("\t")
        if key.strip() == "$Bethesda Game Studios":
            studio = value.strip()
    legal = ""
    for i, line in enumerate(lines):
        if line == "LEGAL" and i + 1 < len(lines):
            para = lines[i + 1]
            end = para.find("All Rights Reserved.")
            legal = para[: end + len("All Rights Reserved.")] if end >= 0 else para
            break
    if not legal:
        print("credits: no legal notice found")
        return False
    with open(os.path.join(pack, "legal.txt"), "w", encoding="latin-1") as f:
        f.write(f"{studio}\n{legal}\n")
    print(f"credits: the studio ({studio}) and the legal notice: {legal[:60]}...")
    return True


if __name__ == "__main__":
    from extract_textures import SKYRIM_DATA
    export(SKYRIM_DATA, "../pack")
    export_credits(SKYRIM_DATA, "../pack")
