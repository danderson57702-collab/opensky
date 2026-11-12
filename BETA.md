# OpenSKY closed beta: getting it running

OpenSKY is an engine for the PS Vita. It has no game in it: it plays the data that **you** make,
on your own computer, from **your own installed copy** of Skyrim Special Edition. The download
holds no game files, and the data you make is for your own Vita only - please don't share it.

## What you need

- A PS Vita or PS TV with HENkaku / Ensō and **VitaShell**.
- `ur0:data/libshacccg.suprx` on the Vita (the shader compiler most homebrew ports need; the
  ShaRKF00D app extracts it from your own console).
- About **11 GB free** on the Vita's storage (an SD2Vita card is the comfortable way).
- Skyrim Special Edition (or Anniversary Edition), **English**, installed from **Steam** or GOG,
  on a Windows PC or a Mac. Unmodded: the tool reads the game's own files, not mods.
- About **35 GB free** on that computer while the tool works (the finished folder is about 11 GB),
  and one to two hours.

## 1. Download

From this repository's **Releases** page:

- `opensky.vpk` - the program for the Vita
- the data maker for your computer: `opensky-data-windows.exe`, or `opensky-data-macos-arm64`
  (Apple Silicon Macs)

## 2. Make your data folder

**Windows:** put `opensky-data-windows.exe` in a folder on a disk with room and double-click it.
Windows may say it doesn't know the program (it isn't signed): *More info > Run anyway*.

**Mac:** open Terminal in the folder you downloaded to, then:

```bash
chmod +x opensky-data-macos-arm64 && xattr -d com.apple.quarantine opensky-data-macos-arm64; ./opensky-data-macos-arm64
```

The tool:

1. finds the game (Steam's libraries, GOG, and on a Mac the usual places CrossOver, Whisky and
   others keep a Windows Steam). If it can't, it asks: drag the game's `Data` folder into the window.
2. checks it is a Steam or GOG install with the files it needs;
3. converts it. This is the long part, and it says which step it is on. If it stops (or you stop
   it), run it again: finished steps are skipped.

When it's done it shows where the folder is: `OpenSKY-data/opensky`, beside the tool.

Other ways to run it: `--data "<the game's Data folder>"` to say where the game is,
`--out <folder>` to work on another disk, `--find` to only show what it finds.

## 3. Put it on the Vita

1. In VitaShell press SELECT (USB, or FTP) and copy the **`opensky`** folder to `ux0:data/`, so
   that you have `ux0:data/opensky/`. It is one big copy (about 11 GB).
2. Copy `opensky.vpk` anywhere on the Vita and install it from VitaShell.
3. Start **OpenSKY**.

The first start compiles its shaders (a minute of loading screens, once).

## 4. Tell us how it goes

What helps most:

- **The log:** `ux0:data/opensky_log.txt` (and `opensky_log_prev.txt`, the run before). It has
  frame times, any hitch over 150 ms with its cause, and - if the game freezes - where it was.
- **Options > Measure this place (15 s)** somewhere slow: the screen flickers while it draws the
  scene without one kind of thing at a time, and the cost of each goes into the log.
- What you were doing, where, and your settings (Options: resolution, quality, frame lock).

Saves are in `ux0:data/opensky_saves/`, settings in `ux0:data/opensky_settings.txt`. Updating:
install the new `opensky.vpk` over the old one; the release notes say when the data folder has
to be made again.

## Good to know

- This is a beta of an engine in progress: the main quest plays through, most side quests
  don't exist yet, and performance on the Vita is still being worked on.
- OpenSKY is not affiliated with or endorsed by Bethesda Softworks or ZeniMax Media. You need to
  own the game; nothing here gives it to you.
