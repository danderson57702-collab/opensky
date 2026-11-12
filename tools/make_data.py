#!/usr/bin/env python3
"""Makes OpenSKY's data folder from your own installed copy of the game (Steam or GOG).

    python3 tools/make_data.py                (finds the game by itself)
    python3 tools/make_data.py --data "D:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data"
    python3 tools/make_data.py --out E:\\somewhere

What it does:
  1. finds the game's Data folder (Steam's libraries, GOG's registry entry, the usual places a
     Mac keeps a Windows Steam) and checks it is a store's install with the files needed;
  2. converts it - land, places, people, quests, voices, sounds, music - into a work folder
     (about 20 GB while it works; an hour or two);
  3. leaves the folder to copy to the Vita: <out>/opensky  (about 11 GB) -> ux0:data/opensky

Nothing is downloaded and nothing leaves your machine. The folder it makes is your own copy's
data in another form: it is for your own Vita, not for passing on.

A run that stopped can be started again: finished steps are skipped (--fresh starts over).
"""
import argparse
import builtins
import glob
import os
import re
import shutil
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
FROZEN = getattr(sys, "frozen", False)  # (the one-file program made for testers: the converter is inside it)
CONVERTER = getattr(sys, "_MEIPASS", None) or os.path.join(HERE, "..", "converter")
GAME_FOLDER = "Skyrim Special Edition"
STEAM_APP = "489830"
NEEDED = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm",
          "Skyrim - Animations.bsa", "Skyrim - Interface.bsa", "Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa",
          "Skyrim - Misc.bsa", "Skyrim - Sounds.bsa", "Skyrim - Textures0.bsa", "Skyrim - Voices_en0.bsa"]
BOUNDS = ("-57", "-43", "61", "50")  # (the whole of Tamriel: tools/reexport.py's)
WORK_GB, OUT_GB = 22, 11
FOLDERS = ("actors/gear", "actors/head", "actors/player", "anims", "chargen", "creatures", "interiors", "items",
           "legacy", "map", "meshes", "music", "navmesh", "objects/baked", "sound", "terrain/_preview",
           "textures/_preview", "ui", "voice")


# The converter was written on a Mac: its text files are UTF-8 unless it says otherwise, with "\n"
# line ends. The same on Windows, whatever its defaults (the engine reads the files byte for byte).
_open = builtins.open


def _same_everywhere_open(file, mode="r", buffering=-1, encoding=None, errors=None, newline=None, *a, **k):
    if "b" not in mode:
        if encoding is None:
            encoding = "utf-8"
        if newline is None and any(c in mode for c in "wax+"):
            newline = "\n"
    return _open(file, mode, buffering, encoding, errors, newline, *a, **k)


def say(text=""):
    print(text, flush=True)


# ---------------------------------------------------------------- finding the game

def _steam_libraries(steamapps):
    """A Steam's own steamapps folder and the other libraries its libraryfolders.vdf names."""
    found = [steamapps]
    vdf = os.path.join(steamapps, "libraryfolders.vdf")
    if os.path.exists(vdf):
        for m in re.finditer(r'"path"\s+"([^"]+)"', _open(vdf, encoding="utf-8", errors="replace").read()):
            found.append(os.path.join(m.group(1).replace("\\\\", "\\"), "steamapps"))
    return found


def _windows_places():
    places = []
    try:
        import winreg
    except ImportError:
        return places
    for hive, key, value in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                             (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
                             (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath")):
        try:
            with winreg.OpenKey(hive, key) as k:
                steam = winreg.QueryValueEx(k, value)[0]
            places += _steam_libraries(os.path.join(os.path.normpath(steam), "steamapps"))
        except OSError:
            pass
    for key in (r"SOFTWARE\WOW6432Node\GOG.com\Games", r"SOFTWARE\GOG.com\Games"):  # (GOG: each game's own folder)
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as games:
                for i in range(winreg.QueryInfoKey(games)[0]):
                    try:
                        with winreg.OpenKey(games, winreg.EnumKey(games, i)) as g:
                            places.append(("game", winreg.QueryValueEx(g, "path")[0]))
                    except OSError:
                        pass
        except OSError:
            pass
    for drive in "CDEFGHIJ":  # (libraries the registry didn't lead to)
        for sub in (r"Program Files (x86)\Steam", r"Program Files\Steam", "Steam", "SteamLibrary", r"Games\Steam", r"Games\SteamLibrary"):
            places.append(os.path.join(drive + ":\\", sub, "steamapps"))
        places.append(("game", os.path.join(drive + ":\\", "GOG Games", "Skyrim Special Edition")))
        places.append(("game", os.path.join(drive + ":\\", "GOG Games", "Skyrim Anniversary Edition")))
    return places


def _unix_places():
    home = os.path.expanduser("~")
    places = []
    c_steam = os.path.join("drive_c", "Program Files (x86)", "Steam", "steamapps")
    patterns = [
        # a Mac running the Windows Steam: CrossOver, Whisky, Kegworks / Wineskin, GameHub, Heroic, a plain prefix
        os.path.join(home, "Library", "Application Support", "CrossOver", "Bottles", "*", c_steam),
        os.path.join(home, "Library", "Containers", "com.isaacmarovitz.Whisky", "Bottles", "*", c_steam),
        os.path.join(home, "Library", "Containers", "*", "Data", "Library", "Application Support", "*", "Bottles", "*", c_steam),
        os.path.join(home, "Applications", "*", "*.app", "Contents", "SharedSupport", "prefix", c_steam),
        os.path.join(home, "Applications", "*.app", "Contents", "SharedSupport", "prefix", c_steam),
        os.path.join(home, "GameHub", "steamapps"),
        os.path.join(home, "Games", "*", c_steam),
        os.path.join(home, ".wine", c_steam),
        os.path.join(home, "Library", "Application Support", "Steam", "steamapps"),
        # Linux (Proton installs the same Windows game)
        os.path.join(home, ".steam", "steam", "steamapps"),
        os.path.join(home, ".local", "share", "Steam", "steamapps"),
        os.path.join(home, ".var", "app", "com.valvesoftware.Steam", ".local", "share", "Steam", "steamapps"),
        # other disks
        os.path.join("/Volumes", "*", "SteamLibrary", "steamapps"),
        os.path.join("/Volumes", "*", "steamapps"),
        os.path.join("/run", "media", "*", "*", "SteamLibrary", "steamapps"),
        os.path.join("/mnt", "*", "SteamLibrary", "steamapps"),
    ]
    for p in patterns:
        for steamapps in glob.glob(p):
            places += [s for s in _steam_libraries(steamapps) if os.path.isdir(s)] or [steamapps]
    return places


def check_data(data):
    """None if `data` is the Data folder of a Steam or GOG install with all that's needed, else why not."""
    if not os.path.isfile(os.path.join(data, "Skyrim.esm")):
        return "no Skyrim.esm in it"
    game = os.path.dirname(os.path.normpath(data))
    steam = os.path.exists(os.path.join(game, "steam_api64.dll")) and os.path.exists(os.path.join(game, "SkyrimSE.exe"))
    gog = bool(glob.glob(os.path.join(game, "goggame-*.info")))
    if not (steam or gog):
        return "not a Steam or GOG install (the store's own files beside the game are missing)"
    names = {n.lower() for n in os.listdir(data)}
    missing = [n for n in NEEDED if n.lower() not in names]
    if missing:
        return "files missing: " + ", ".join(missing) + " (the English Special Edition or Anniversary Edition is needed; in Steam: Properties > Installed Files > Verify)"
    return None


def find_data():
    """The Data folders found, best first, as [(path, problem or None)]."""
    seen, out = set(), []
    for place in (_windows_places() if sys.platform == "win32" else _unix_places()):
        if isinstance(place, tuple):
            data = os.path.join(place[1], "Data")
        else:
            data = os.path.join(place, "common", GAME_FOLDER, "Data")
        key = os.path.normcase(os.path.normpath(data))
        if key in seen or not os.path.isdir(data):
            continue
        seen.add(key)
        out.append((data, check_data(data)))
    out.sort(key=lambda e: e[1] is not None)
    return out


def ask_for_data():
    say("Where is the game? Drag its Data folder (in Steam: right-click the game > Manage > Browse local")
    say("files, then the Data folder inside) into this window and press Enter. An empty line gives up.")
    while True:
        try:
            line = input("> ").strip()
        except EOFError:
            line = ""
        if not line:
            sys.exit("No game folder given.")
        line = line.strip("\"'").replace("\\ ", " ") if sys.platform != "win32" else line.strip("\"'")
        for data in (line, os.path.join(line, "Data")):
            if os.path.isdir(data) and check_data(data) is None:
                return data
        say(f"  {line}: {check_data(line) if os.path.isdir(line) else 'no such folder'}")


# ---------------------------------------------------------------- the steps

def _terrain(pack):
    import extract_terrain
    sys.argv = ["extract_terrain.py", pack, *BOUNDS]
    extract_terrain.main()


def _world(pack):
    import reexport
    reexport.run(pack)


def _words(pack):
    import gametext
    from esm import LoadOrder
    from extract_textures import SKYRIM_DATA
    from dialogue import StringTables
    lo = LoadOrder(SKYRIM_DATA)
    if gametext.export(lo, StringTables(lo), pack) < len(gametext.KEYS):
        say("   (some of the game's words weren't found: the game shows blanks for those - please report it)")


def _music(pack):
    import extract_music
    sys.argv = ["extract_music.py", pack, "--all"]
    extract_music.main()


def _archives(pack):
    import pack_archives
    for folder in pack_archives.FOLDERS:
        pack_archives.pack_folder(pack, folder)


STEPS = [("land", "The land (about 10 minutes)", _terrain),
         ("world", "Places, people, quests, voices and sounds (the long one: an hour or more)", _world),
         ("words", "The game's words for the menus and quests", _words),
         ("music", "Music", _music),
         ("archives", "Packing for the Vita's card", _archives)]


def free_gb(path):
    while not os.path.exists(path):
        path = os.path.dirname(path) or "."
    return shutil.disk_usage(path).free / 1e9


def copy_out(pack, out):
    """The folder for the Vita: the archives in place of the folders they hold, and the rest."""
    import pack_archives
    packed = set(pack_archives.FOLDERS)
    os.makedirs(out, exist_ok=True)
    n = size = 0
    for root, dirs, files in os.walk(pack):
        rel = os.path.relpath(root, pack)
        if rel == ".":
            dirs[:] = [d for d in dirs if d not in packed]
        dirs[:] = [d for d in dirs if d != "_preview" and not d.startswith(".")]
        for f in files:
            if f.startswith(".") or f.endswith(".tmp") or f in ("build.txt", "autotest.txt"):
                continue
            src, dst = os.path.join(root, f), os.path.join(out, rel, f)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not (os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src) and os.path.getmtime(dst) >= os.path.getmtime(src)):
                shutil.copy2(src, dst)
            n, size = n + 1, size + os.path.getsize(src)
    return n, size


def wave_22k(np):
    return (np.sin(np.arange(22050, dtype=np.float32) / 22050 * 440 * 2 * np.pi) * 12000).astype(np.int16).tobytes()


def selftest():
    """Checks the program itself, with no game: every part of the converter loads, the sound coder
    and the texture coder work, and text is written the same on every system. (Run by the build.)"""
    os.environ["OPENSKY_SELFTEST"] = "1"
    import tempfile
    import numpy as np
    from PIL import Image
    import extract_objects, interiors, items, npcs, animations, esm, college, lighten_mids, relock_landscape  # noqa
    import bake_cells, navmesh, waygraph, people_combat, quests, weather, creatures, perks, worldmap  # noqa
    import extract_sounds, shouts, followers, legacy_followers, topics, stones, greybeard_words, skills  # noqa
    import title, extract_terrain, pack_archives, reexport, chargen, gear, races, crime, companions, thieves, gametext  # noqa
    import extract_music
    from textures import convert_image
    dll = extract_music.load_adpcm()
    t = np.arange(extract_music.RATE, dtype=np.float32) / extract_music.RATE
    wave = (np.sin(t * 440 * 2 * np.pi) * 12000).astype(np.int16)
    data, snr = extract_music.encode_skmus(dll, np.stack([wave, wave], axis=1).copy())
    assert data[:4] == b"SKMU" and snr > 15, ("sound coder", snr)
    import av, io, wave  # (the audio reader: a second of sound written as a .wav and read back)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        wave_bytes = wave_22k(np)
        w.setnchannels(1), w.setsampwidth(2), w.setframerate(22050), w.writeframes(wave_bytes)
    buf.seek(0)
    with av.open(buf) as c:
        heard = sum(f.samples for f in c.decode(audio=0))
    assert heard == len(wave_bytes) // 2, ("audio reader", heard)
    tex = convert_image(Image.fromarray((np.random.rand(64, 64, 3) * 255).astype(np.uint8)), max_size=64)
    assert len(tex) > 1000, "texture coder"
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "meshes"))
        for folder in pack_archives.FOLDERS:
            os.makedirs(os.path.join(d, folder), exist_ok=True)
        with open(os.path.join(d, "meshes", "a.txt"), "w") as f:
            f.write("caf\u00e9\nline\n")
        assert _open(os.path.join(d, "meshes", "a.txt"), "rb").read() == b"caf\xc3\xa9\nline\n", "text files"
        pack_archives.pack_folder(d, "meshes")
        assert _open(os.path.join(d, "meshes.skpak"), "rb").read(4) == b"SKPK"
        with tempfile.TemporaryDirectory() as out:
            n, _ = copy_out(d, out)
            assert n == 1 and os.path.exists(os.path.join(out, "meshes.skpak")), ("the Vita's folder", n)
    say(f"selftest: all parts load; sound coder {snr:.0f} dB; audio reader {heard} samples; texture {len(tex)} bytes; text and archives right")


def main():
    ap = argparse.ArgumentParser(description="Makes OpenSKY's data folder from your own installed copy of the game.")
    ap.add_argument("--data", help="the game's Data folder (found by itself if not given)")
    ap.add_argument("--out", help="where to work and leave the folder (default: OpenSKY-data beside this program)")
    ap.add_argument("--fresh", action="store_true", help="start over: forget the steps already done")
    ap.add_argument("--find", action="store_true", help="only say where the game was found")
    ap.add_argument("--selftest", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()

    builtins.open = _same_everywhere_open
    for stream in (sys.stdout, sys.stderr):  # (a console that can't show a letter shows "?" - it doesn't stop the work)
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    sys.path[:0] = [os.path.abspath(CONVERTER), HERE]
    if not FROZEN:
        os.chdir(CONVERTER)
    if args.selftest:
        return selftest()

    say("OpenSKY data maker")
    say("------------------")
    if args.data:
        data = args.data if os.path.isfile(os.path.join(args.data, "Skyrim.esm")) else os.path.join(args.data, "Data")
        problem = check_data(data) if os.path.isdir(data) else "no such folder"
        if problem:
            sys.exit(f"{args.data}: {problem}")
    else:
        found = find_data()
        for path, problem in found:
            say(f"found: {path}" + (f"\n   but: {problem}" if problem else ""))
        good = [p for p, problem in found if not problem]
        data = good[0] if good else None
        if args.find:
            return
        if not data:
            say("The game wasn't found in the usual places." if not found else "")
            data = ask_for_data()
    say(f"The game: {data}")

    base = os.path.dirname(sys.executable) if FROZEN else os.path.abspath(os.path.join(HERE, ".."))
    root = os.path.abspath(args.out or os.path.join(base, "OpenSKY-data"))
    work, out, done_dir = os.path.join(root, "work"), os.path.join(root, "opensky"), os.path.join(root, "work", ".done")
    if args.fresh and os.path.isdir(root):
        for d in (work, out):
            shutil.rmtree(d, ignore_errors=True)
    if sys.platform == "win32" and len(work) > 130:  # (Windows stops at 260 letters; the longest file's own are 106)
        sys.exit(f"{root}: too long a path for Windows - put the program in a short folder (C:\\OpenSKY) or give one with --out")
    todo = [s for s in STEPS if not os.path.exists(os.path.join(done_dir, s[0]))]
    need = OUT_GB + (WORK_GB if any(s[0] in ("land", "world") for s in todo) else 0)
    have = free_gb(root)
    say(f"Working in: {root}  ({have:.0f} GB free; about {need} GB needed)")
    if have < need and not os.path.exists(os.path.join(done_dir, "world")):
        sys.exit(f"Not enough room: free {need - have:.0f} GB more, or give another disk with --out")
    os.makedirs(done_dir, exist_ok=True)
    for folder in FOLDERS:  # (a pack's folders: the converter's parts each expect the others' to be there)
        os.makedirs(os.path.join(work, *folder.split("/")), exist_ok=True)

    os.environ["SKYRIM_DATA"] = data
    t0 = time.time()
    for i, (key, title, step) in enumerate(STEPS):
        if os.path.exists(os.path.join(done_dir, key)):
            say(f"[{i + 1}/{len(STEPS) + 1}] {title}: done before")
            continue
        say(f"\n[{i + 1}/{len(STEPS) + 1}] {title}")
        t = time.time()
        step(work)
        _open(os.path.join(done_dir, key), "w").write(time.strftime("%Y-%m-%d %H:%M\n"))
        say(f"   ({(time.time() - t) / 60:.0f} min)")
    say(f"\n[{len(STEPS) + 1}/{len(STEPS) + 1}] The folder for the Vita")
    n, size = copy_out(work, out)
    say(f"""
Done in {(time.time() - t0) / 60:.0f} minutes: {n} files, {size / 1e9:.1f} GB

   {out}

Copy that folder to the Vita as ux0:data/opensky (VitaShell: SELECT for USB or FTP), install
opensky.vpk, and start OpenSKY. The work folder ({work}) can be deleted.
This data is your own copy's: for your own Vita only - please don't pass it on.""")


if __name__ == "__main__":
    try:
        main()
    except SystemExit as e:
        if e.code not in (None, 0):
            say(str(e.code) if not isinstance(e.code, int) else "")
        code = e.code if isinstance(e.code, int) else (1 if e.code else 0)
    except KeyboardInterrupt:
        say("\nStopped. Run it again to go on from the last finished step.")
        code = 1
    except BaseException:
        traceback.print_exc()
        say("\nSomething went wrong. Please send the lines above with your report.")
        code = 1
    else:
        code = 0
    if FROZEN and sys.stdin and sys.stdin.isatty():  # (a double-clicked program: keep its window open)
        try:
            input("\nPress Enter to close.")
        except EOFError:
            pass
    sys.exit(code)
