# OpenSKY (PS Vita homebrew engine)

OpenSKY is a standalone, original game engine for the PS Vita (C, VitaSDK + vitaGL), written
from scratch - no publisher code, decompiled or leaked. It plays nothing on its own: it runs
data that its converter makes from **your own legally purchased, installed** copy of Skyrim
Special Edition (Steam or GOG; the converter checks for one and refuses to run without it).

**No game data lives in this repository, and none may be distributed.** The converter
writes everything it produces to `pack/` (git-ignored) on your own machine; the VPK contains
only the engine and its own icon. Not affiliated with or endorsed by Bethesda Softworks or
ZeniMax Media; their trademarks belong to them.

**Testing the beta?** See [BETA.md](BETA.md): one program makes the data folder from your own
installed copy, and the VPK is on the Releases page.

From source, one command makes the whole data folder (it finds a Steam or GOG install by
itself; `--data` names one):

```bash
python3 -m pip install -r tools/requirements.txt
python3 tools/make_data.py
```

## Layout

| Path | What |
| --- | --- |
| `converter/` | Python tools: the game's Data (BSA/ESM/NIF/DDS/xWMA) -> `pack/` |
| `tools/` | `make_data.py` (the testers' data maker), `reexport.py`, `deploy_card.sh`, `lines.py`... |
| `engine/` | The Vita app: streaming world, dither LOD fades, collision, PS2-style post-processing, height fog, day/night, music |
| `engine/tests/` | Host-side tests (collision, real-data walks) |
| `patches/` | Changes applied to vitaGL |
| `third_party/vitaGL` | vitaGL submodule, pinned to 2bdbe89 |
| `m1_cube/` | Milestone 1 toolchain smoke test (fog cube) |

## Setup (macOS, Apple Silicon)

- VitaSDK in `~/vitasdk` (`VITASDK` and `$VITASDK/bin` on `PATH`), cmake (`pip3 install --user cmake`)
- Python 3 with the converter's libraries: `python3 -m pip install -r tools/requirements.txt`
- vitaGL, built once with our patch:

```bash
git submodule update --init
git -C third_party/vitaGL apply ../../patches/vitagl.patch
make -C third_party/vitaGL NO_SPLASHSCREEN=1 HAVE_SHADER_CACHE=1 libvitaGL.a
```

## Convert your data

`tools/make_data.py` (above) does all of it for a new pack: the land, then
`tools/reexport.py` (places, people, quests, voices, sounds), the game's words for the menus
(`converter/gametext.py`), the music, and the archives - into `OpenSKY-data/work`, leaving
`OpenSKY-data/opensky` to copy to the Vita. Working on the converter, the same parts run by
themselves on `pack/` (git-ignored); the converter reads the game's Data folder from
`SKYRIM_DATA` or `converter/data_path.txt` and checks it is a Steam or GOG install.

```bash
cd converter
python3 extract_terrain.py ../pack -57 -43 61 50
python3 ../tools/reexport.py ../pack
python3 extract_music.py ../pack
python3 pack_archives.py ../pack
```

No text of the game's is in this source or in the program: lines of dialogue are named by
their records' form ids (`tools/lines.py` shows a number's words from your own copy), and what
the engine shows - quest names, objectives, what you can say - is read from the pack's
`gametext.txt` (`engine/src/gametext.h`).

## Build, test, run

```bash
engine/build.sh                 # -> engine/build/opensky.vpk
engine/tests/run_tests.sh       # host tests
./deploy_pack_vita3k.sh         # pack -> Vita3K ux0:data/opensky/
```

On a real Vita: install the VPK with VitaShell and copy `pack/` to `ux0:data/opensky/`.
Custom shaders need `ur0:data/libshacccg.suprx` (extracted on your own console).

## Controls

(For now; the final layout waits for the real Vita.)

Starting: with a saved game, the title offers Continue or New Game. A new game is Skyrim's
opening (Unbound): the cart ride into Helgen, the lineup - the race menu comes at Hadvar's "Who
are you?" (up / down picks one of Skyrim's ten races, with its skill bonuses, abilities and
power from the game's files; left / right one of its faces; CROSS on) - the execution, Alduin,
the escape through Helgen Keep with Ralof or Hadvar (whichever you run to), and on to Riverwood
(Before the Storm). There's no saving in Helgen until you're in the keep. Racial powers are used from the
Magic menu, once a day (Highborn, Adrenaline Rush and Histskin speed up regeneration; the
Argonians' water breathing works; the others are listed but have nothing to act on yet).

Left stick: move (tilt part way to walk). Right stick: look. R: sprint. CROSS: jump.
CIRCLE: talk to the person in front of you (waking them if they're asleep in bed), go through
the door you face, or open / close a door inside (NPCs open doors they walk through), open the
container in front of you (chests, barrels, sacks, urns...: its lid opens; CROSS takes one,
TRIANGLE takes everything; contents are Skyrim's leveled loot at your level) or take a loose
item, indoors and out. Owned things say "Steal" (no crime yet). Locked containers are picked:
left stick / d-pad moves the pick across the 180 degree arc, hold CROSS (or R) to turn the lock,
CIRCLE leaves (Skyrim's rules and numbers; a new game starts with 50 lockpicks). Sneaking (L),
CIRCLE on someone picks their pocket: each item shows its chance; caught, they turn on you
and keep an eye on you for a while.
L: sneak (crouch). D-pad down: first / third person.
Each hand holds its own thing, as in Skyrim: TRIANGLE uses the right hand, SQUARE the left (a
spell: Flames and Healing are held; pressing readies the spells first if put away). D-pad left /
right: what the left / right hand holds, in turn (nothing, the torch (left hand), each spell).
D-pad up: ready / put away the spells. The same spell in both hands casting together is twice
the magicka for twice the effect.
START: pause menu (inventory: Skyrim's categories, L / R or left / right to change: All,
Weapons, Apparel, Potions, Scrolls, Food, Ingredients, Books, Keys, Misc; gold and weight
against your carry weight (300, +5 per 10 stamina; over it you can't run); CROSS wears / takes
off armour and clothes, equips a weapon in the right hand (two-handed ones and bows take both
hands; D-pad up draws it), eats or drinks; magic: L / R equips
the spell in the left / right hand (again: takes it out), CROSS the right, and CROSS on your
race's power uses it; the torch and a spell can't share the left hand: the newer one stays;
skills: levels and progress, level-up bonuses; save / load game; options; camera; quit).
The game autosaves on arriving through a door (ux0:data/opensky_save.dat).
Health, magicka and stamina show as bars at the bottom while not full: sprinting costs stamina,
falls over 5 m hurt (20 m kills); under water a breath bar shows your 20 s of air, then you
drown (8% of your health a second); dying returns you to the last save.
Spells: Candlelight (21 magicka, a light that follows you for 60 s; Alteration), Healing
(12 magicka/s, 10 health/s; Restoration, only while hurt), Flames (14 magicka/s, 3.5 m;
Destruction, while the fire hits something), Sparks (16 magicka/s, lightning to 8 m;
Destruction), Fire Rune (51 magicka: a rune where you aim, bursting when someone steps on
it; Destruction), Muffle (144 magicka: silent steps for 3 minutes; Illusion), Bound Sword
(105 magicka: a sword in your hand for 2 minutes; Conjuration). A readied spell shows in the hands (Skyrim's casting
animations, with effects built from Skyrim's effect textures). Sneaking on the move near people
trains Sneak.
Skill and player levels work as in Skyrim; each player level asks for +10 health, magicka or
stamina in the Skills menu.
SELECT: wait (1-24 hours). CIRCLE on a chair, bench or stool sits down (Skyrim's clips; the
stick or CIRCLE gets up); on a bed or bedroll opens Sleep (rested: full health, magicka and
stamina). Levers, pull chains and puzzle pillars work as their scripts do (Bleak Falls
Barrow's pillars open its gate when they show the right faces; the wrong ones spring darts).
Save / load: a list of saves with where you were, your level and a picture; autosaves take
turns in three slots. Sound effects (footsteps by surface, swimming, weapons, spells,
lockpicking, items, doors, menus) are Skyrim's; their volume follows the voice volume.
Weather follows Skyrim's regions (clear, cloudy, rain, snow, fog), changing every few hours.
In deep water you swim (R: faster); look down and push forward to dive, look up to come back up, let go to float up.
Options: music and voice volume, subtitles, stair bounce, look speed and inversion, bloom, debug
controls; saved to ux0:data/opensky_settings.txt.
Debug controls (off by default): L held fast travel (instead of sneak), SELECT noclip (instead of
wait), R + TRIANGLE held fast-forward time.

## Diagnostics (sceClibPrintf / Vita3K log)

- `HITCH frame N: ...`: a frame over 40 ms, with update/stream timings
- `objects: cell X,Y gpu free: ...`: GPU memory after each cell change
- `STUCK at ...`: what blocked the player when pushing without moving
