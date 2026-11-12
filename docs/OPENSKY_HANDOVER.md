# OpenSKY handover — for an outside audit (31 October 2026)

Written by Claude (Opus 5.5) and audited & completed by Antigravity (31 October 2026),
so that the project is verified against Skyrim Special Edition's own files and ready for deployment.
Read `docs/STATUS.md` for the area-by-area progress table; this file says **where everything is,
how it is made, what was changed from Skyrim on purpose, and where the guesswork and truncation live**.

Last commit at writing: see `git log -1` (branch `main`). Last card build: **1031-1330**.

---

## 1. What OpenSKY is

A from-scratch PS Vita (homebrew, VitaSDK) game engine in C that plays Skyrim Special Edition's
content. **No Bethesda data is in the repo.** A Python converter reads the user's own Skyrim SE
install and writes a private, git-ignored `pack/` (~20 GB) that the engine loads from the Vita's
`ux0:data/opensky/`. The engine never reads `.esm`, `.bsa`, `.nif`, `.hkx` or Papyrus at run time:
everything is pre-digested into simple text tables and custom binary formats.

Legal/compliance rules the project follows (keep them): the pack is never distributed or
committed; no Bethesda assets in git; quest/dialogue text and voices come only from the user's
files at export time.

## 2. Paths

| What | Where |
|---|---|
| Repo | the repository's folder (published as `opensky`) |
| Skyrim SE data (source of truth) | the user's own install's `Data` folder (`SKYRIM_DATA`, or `converter/data_path.txt`; read by `converter/extract_textures.py:_game_data_dir`) |
| Load order used | `Skyrim.esm, Update.esm, Dawnguard.esm, HearthFires.esm, Dragonborn.esm` (`converter/esm.py: OFFICIAL_LOAD_ORDER`) |
| Archives read | `Skyrim - Meshes0/1.bsa, Textures*.bsa, Animations.bsa, Voices_en0.bsa, Sounds.bsa, Interface.bsa (strings), Misc.bsa (Papyrus .pex)` |
| Engine source | `engine/src/*.c|h` (74 C files), build `engine/build.sh` → `engine/build/opensky.vpk` (+ `eboot.bin`) |
| Converter | `converter/*.py` (Python 3.9, numpy, PyAV) |
| Generated pack | `pack/` (git-ignored). Big folders also archived into `*.skpak` by `converter/pack_archives.py` |
| Full re-export | `cd converter && python3 ../tools/reexport.py && python3 pack_archives.py ../pack` (~45–60 min, whole map) |
| Quick quest re-export | `cd converter && python3 ../tools/quest_lines.py ../pack <keys…> [--places]` (minutes) |
| Voice repair | `cd converter && python3 ../tools/voice_restore.py ../pack` |
| Papyrus reader | `python3 tools/pexdump.py <script>.pex [filter|vars]` (reads `Skyrim - Misc.bsa`) |
| Perk audit | `python3 tools/perk_audit.py` |
| Card deploy | `tools/deploy_card.sh` (card mounted at `/Volumes/Untitled`) |
| Vita3K (testing) | `~/Library/Application Support/Vita3K/Vita3K/fs/ux0/` — app `OSKY00001`, data `data/opensky/`, log `data/opensky_log.txt` |
| On-device log | `ux0:data/opensky_log.txt` (perf lines every 10 s) |
| Saves | `ux0:data/opensky_saves/NN.sav|.items|.thumb` (format `engine/src/save.h`, version 20) |
| Status / roadmap | `docs/STATUS.md`, `docs/ROADMAP.md` |

## 3. The pipeline (converter → pack → engine)

`tools/reexport.py` runs, in order: exterior cells (`extract_objects.py`) → interiors (`interiors.py`)
→ cell bakes (`bake_cells.py`) → navmesh (`navmesh.py`) → waygraph → people (`npcs.py`, `dialogue.py`)
→ people_combat → **quests** (`quests.py` + `companions.py`, `college.py`, `thieves.py`) → creatures
(`creatures.py`) → shouts → followers → topics → player/gear → stones/word walls → items → animations
(`animations.py`, `hkx.py`) → greybeard words → skills/perks → worldmap → weather → sounds → model table.
Terrain is a separate export (`extract_terrain.py`).

### Pack files (what to audit against the ESM)

| Pack file | Written by | Engine reader | Format |
|---|---|---|---|
| `quests.txt` | `quests.py` | `quests.c`, `dtree.c`, guild files | tab text: `S/R` scenes, `DT/DN/DV/DR/DL/DX` dialogue trees, `I` quest items, `Q` quest marks (`key cell x y z heading`; cell `-` outdoors, interior editor id, or `@prefix` own world) |
| `topics.txt` | `topics.py` | `topics.c` | NPC topic lines |
| `actors/*.sknpc|.sktalk` | `npcs.py`, `dialogue.py` | `actors.c`, `talk.c` | custom binary skinned mesh / voiced lines (`u8 len, path, u16 len, text`) |
| `creatures/creatures.txt`, `*.sknpc`, `*.skanim` | `creatures.py`, `animations.py` | `creatures.c`, `anim.c` | `K|` kinds, `S|` spawns (pos, place, ambush, leveling, side, ref), `R|` routes, `M|` mage spells |
| `items/items.txt`, `gear*.txt`, `inventory.txt` | `items.py`, `gear.py` | `items.c`, `equip.c` | `I` items, `L` leveled lists, `G` list globals |
| `perks.txt` | `perks.py` | `perks.c` | perk entries: `E` entry points, `A` actor values, CTDA conditions (subset) |
| `spells.txt`, `shouts.txt` | `items.py`/`perks.py`, `shouts.py` | `magic.c`, `shouts.c` | spell table, shouts + `V` voice lines |
| `beast.txt` | `companions.py:export_beast` | `beast.c` | werewolf race unarmed/reach/height, level bonuses, AbWerewolf, PlayerWerewolfQuest timings |
| `interiors/*.skint|.sknav`, `interiors.txt` | `interiors.py` | `interior.c`, `navmesh.c` | per-interior baked geometry, doors, loot, activators |
| `objects/`, `terrain/`, `meshes/`, `textures/` | extract_* | `objects.c`, `terrain.c`, `skmesh.c`, `sktex.c` | baked cell batches, LOD tiers, DXT/PVR-ish textures |
| `voice/<voicetype>/<plugin>_<formid>_<n>.skvoc` | `dialogue.py:_convert` | `voice.c` | `SKMU` header + ADPCM mono (from .fuz XWM) |
| `worlds.txt`, `map/` | `worlds.py`, `worldmap.py` | `worlds.c`, `worldmap.c` | walled cities and own-land worlds by prefix |
| `followers.txt`, `crime.txt`, `stones.txt`, `walls.txt`, `weather.txt`, `races.txt`, `skills.txt`, `horses.txt`, `chargen.txt` | matching .py | matching .c | tab/pipe text |

Coordinates: engine metres = Skyrim units / 70; engine (x, y, z) = (X, Z, −Y); heading = −rotZ.

## 4. Systems changed from the original game

**Engine is not Skyrim's Creation Engine.** No Papyrus VM, no AI packages, no Havok runtime, no
condition evaluator beyond a small subset. Behaviour is re-authored in C, guided by the records.

- **Quests are hand-coded state machines**, not Papyrus. Main quest in `quests.c` (3,000+ lines);
  guilds in `companions.c` (C00–C06), `college.c` (MG01–08), `thieves.c` (TG00–09, TG02B,
  TGLeadership); shared helpers `questkit.c/.h`. Stage numbers mirror Skyrim's where possible;
  stage ≥100 is reserved as `QUEST_DONE` (so Skyrim stages 100/110 were renumbered, e.g.
  Dampened Spirits 100→98, 110→99).
- **Dialogue**: Skyrim DIAL/INFO exported as trees (`TreeMaker` in `quests.py`). Conditions kept:
  GetIsID (72), GetPCIsSex (131), GetPCIsRace (130), GetItemCount (47), GetActorValue on player (14),
  GetEquipped list (182), relationship (403/638). **Everything else (GetStage, quest variables 629,
  GetIsAliasRef 566, faction, location…) is dropped**, so trees can offer lines out of context.
  Per-tree `assume` dicts fill quest aliases (e.g. Farkas as Observer) and `_stop` cuts branches the
  engine handles itself (Faralda's test).
- **Scenes**: SCEN actions flattened to an ordered line list, said as voices with subtitles; no
  phases, packages or timing. A `_f` copy exists when Skyrim has a female variant.
- **AI**: creatures/people use a small state machine (idle/wander/chase/attack/flee/ambush) in
  `creatures.c`; townspeople follow schedules from packages approximated in `npcs.py`.
- **Combat**: own damage model following Skyrim formulas where known (perk entry points applied via
  `perks.c`); killmoves are paired HKX clips; ragdoll replaced by a "flung body" push.
- **Beast Form** (`beast.c`): werewolf creature model drawn in place of the player, third person.
- **Magic**: 110 spells, own implementation per archetype; not every MGEF archetype.
- **Saves**: own binary format, versioned (v20), old versions still load.

## 5. Corners cut ON PURPOSE for the PS Vita (keep these)

The Vita has 512 MB RAM (~365 usable), a 4-core ARM Cortex-A9 @444 MHz, SGX543MP4+ GPU and slow
card I/O. These were deliberate and measured; an audit should **not** "fix" them back to Skyrim:

1. **No runtime ESM/BSA/NIF/HKX parsing** — all pre-baked into compact formats; load times and RAM.
2. **Cell batching and LOD tiers**: exterior objects baked into per-cell batches with full/mid/far
   meshes; simplified mid meshes (`lighten_mids.py`), distant land at reduced rings; fog ~120 m.
   Cities are GPU-bound ("swap" dominates frame time) — draw counts are the budget.
3. **Textures** downscaled and recompressed; no normal/specular maps on most objects; simplified
   lighting (no shadows, limited dynamic lights).
4. **Voices** converted to mono ADPCM at a reduced rate.
5. **Animations**: sampled/keyframe-reduced; procedural walk cycle for people when far; NPCs posed on
   a second core; only nearest few posed each frame in cities (`actors_set_detail`).
6. **People**: models loaded lazily, only in view; far NPCs placed by schedule, not simulated.
7. **Creatures**: capped awake count (`MAX_LIVE`), wake/sleep radii; a performance governor lowers
   detail when frames slip.
8. **Navmesh**: Skyrim's navmeshes exported, but pathing is simplified (straight-line fallbacks).
9. **Physics**: no Havok; capsule collision against baked geometry; "ragdolls" are a scripted push.
10. **Quests**: hand-coded rather than a Papyrus VM — a VM plus every script would not fit the CPU
    or memory budget. Puzzles are often replaced by "stand at Skyrim's mark" triggers.
11. **Dialogue conditions** reduced to the subset above (see §4) — full condition evaluation would
    need most of the game's runtime state.
12. **Radiant quests, the Story Manager, crime/faction depth** — largely absent or simplified.

## 6. Where precision was dropped or state truncated (audit item 1)

Known places where an architecture or size limit forced loss of fidelity:

- **Fixed caps that silently dropped data (now raised + logged, keep watching):**
  `dtree.c` MAX_TREES 1024 / NODES 6144 / VARIANTS 8192 / RESPONSES 12288 / LINKS 8192 (was 384 —
  guild trees were being dropped); `quests.c` MAX_SCENES 384, MAX_QPLACES 512 (265 used), MAX_QITEMS
  256; `TREE_MAX_NODES = 48` per tree in `quests.py` (deep conversations truncated); `SCENE_LINES 48`.
  Also check `creatures.c` MAX_LIVE/MAX_SPAWNS/MAX_FOES (`QK_FOES 24`), `actors.c` MAX_BEASTS,
  BEAST_KINDS/BEAST_SETS, `items.c` caps, `followers.h FOLLOWERS_SAVED 32`, save arrays
  (`dead_keys[1024]`, `quest_stage[16]`, `companions[16]`, `college[16]`, `thieves[24]`).
- **Quest state**: each guild saves only stage numbers + a flags int; mid-scene steps are not saved
  (on load, a stage restarts its scene). Thieves Guild objectives set by the_quests() are not
  restored after load (shows DIAL 000D4FAC). Topic-driven `step` is one int per guild file.
- **Dialogue**: dropped conditions (§4) mean variant choice = first passing; quest-variable branches
  (e.g. TG "good/bad job" lines) are picked by the exporter's `opening` string, not by state.
- **Scenes**: alias-only speakers resolved at export; unresolved aliases drop lines (e.g. scenes
  missing: `c00_fight`, `c01_post_ambush`, `c03_ritual`, `c06_extraction`, `mg04_augur`, `mg07_priest`,
  `mq303_portal`). Scene order is by action start index only.
- **Responses**: multi-response INFOs are exported in record order (correct per Skyrim), max one INFO
  per scene action.
- **Creatures killed**: tracked per spawn index for file spawns; quest-placed foes are `transient`
  and not saved — they respawn after loading.
- **Voice fallbacks**: lines whose speaker has no actor play as unattached voices; subtitles use
  `voice_title()` in `quests.c` (default "Whiterun Guard" — a known wart).

## 7. Hard-coded / guessed values to replace with record data (audit item 2 — "de-improv")

Grep targets. Each should be traced back to a record, global, or Papyrus property:

- `engine/src/companions.c`, `college.c`, `thieves.c`: every `#define *_M` proximity radius
  (TRAINING_NEAR_M, AMBUSH_NEAR_M, DOOR_NEAR_M, PICKUP_M, HIVES_M, PLANT_M, FLAME_M…) — Skyrim uses
  trigger volumes (`XPRM` primitives on triggers / `MG06CrystalTriggerRef`, `tg02hivesmarker`, …);
  export trigger boxes instead. `RAMPAGE_S 24`, `AMBUSH_WOLF_S 90`, `NIGHT_FROM_H 20/6`,
  `DEBT_GOLD 100`, `HIVE_EVERY_S`. Ancano/the Caller are stand-in kinds ("Thalmor-like" person,
  "Novice Conjurer").
- `quests.c` main quest: `BOSS_AT` (Bleak Falls Sanctum coordinates), `PARTY_DOOR_OUTSIDE`,
  `WATCHTOWER_M`, Western Watchtower fallback `(6.6, 209.0)` in `marker_named`, `TOWER_*`, `IRILETH_*`
  damage/timing, `ESCORT_S`, `GREYBEARDS_*` delays, Clear Skies/storm values, `MOVE_SETTLE_S`,
  `HEROES_NEEDED 3`, `ALDUIN_FLEES_AT 0.5`, `ODAHVIING_TRAPPED_AT 0.35`. Several are tuning, not data.
- `brawl.c`: `PUNCH_*`, health `50 + 5*level` (guesses; Skyrim's brawl uses actor stats).
- `beast.c`: `BEAST_PACE_MULT 1.35`, `BEAST_EYE_LIFT_M`, `STRIKE_S/POWER_S` clip lengths (should come
  from the werewolf race's movement types and the clip durations).
- `main.c`: `WALK/RUN/SPRINT_SPEED`, reach constants, `UNARMED_DAMAGE`, `POWER_HOLD_S`, killmove
  odds (Skyrim globals KillMove / KillMoveRandom are read, others not).
- `creatures.c`: ambush wake radii (`AMBUSH_WAKE_M 6`, `BOSS_WAKE_M 10`), dragon distances,
  ally follow distances, `DEATH_PUSH_*`. **Draugr "transitioning" (sarcophagus/alcove exit) uses
  fixed clips and a 6 m wake radius** rather than Skyrim's furniture markers + package conditions.
- `converter/creatures.py`: `ROLES` clip-name candidate lists (first match wins — guesswork),
  `PREY/NEUTRAL/HOSTILE_PEOPLE` lists, `MAX_REACH_M`, `NPC_HEALTH_LEVEL_BONUS`.
- `converter/companions.py/college.py/thieves.py`: `PLACES_ID` / `FOE_IDS` are real references
  (good), but `QUEST_FOE_CELLS` picks foes by *base name in a cell* (heuristic), and
  `ENABLED_ANYWAY` force-enables Fellglow Keep's doors.
- Coordinates in docs/test scripts only — the engine's quest marks all come from `Q` lines.

## 8. Simplifications in quests (accepted for now; candidates for faithful versions)

- Companions: C01 starts right after C00 (no radiant jobs); witches' head given on any witch death;
  Farkas's change is a summoned werewolf ally; Kodlak's ghost is his normal actor; Silver Hand in
  Gallows Rock spawned from quest marks; the Underforge door opened by proximity.
- College: Faralda's test = any spell of your best school near her; Oculory = any spell near the
  armillary; quest items handed over at Skyrim's spots (books, staff, crystal); MG07 ghosts/visions
  skipped; Arch-Mage robes & Torc not given (bad item ids in `college.py ITEMS`).
- Thieves Guild: ring planted by proximity (no reverse-pickpocket UI); hives by Destruction cast;
  vat/nest by proximity; no shadowing Gulum-Ei; Snow Veil = arrow + fade to Karliah; Under New
  Management right after TG09 (no radiant/city-influence requirement); Nocturnal's voice skipped.

## 9. Known structural risks for the pack audit (audit item 3)

Check these against SSE format rules first:

1. `esm.py` record/group walker: compressed records, localized strings, `XESP` enable-parent
   resolution (`_enable_state/_enabled`), persistent vs temporary cell children, `XTEL` door links.
2. `nif.py`/`meshes.py`: SSE BSTriShape vertex formats, skin partitions, `BSLightingShaderProperty`
   flags; meshes that fail fall back silently.
3. `hkx.py`: SSE 64-bit havok packfiles; spline-compressed tracks; killmove 201-track paired clips.
4. `textures.py`: BC7/BC5 DDS handling (SSE uses BC7) → engine format.
5. `dialogue.py`: `.fuz` → XWM decode via PyAV; lip data skipped; voice-type fallback `_any_voice`.
6. `items.py`: leveled list flags, `LVLG` globals, enchantment charge.
7. `perks.py`: perk entry point numbering (engine enum in `perks.h`) vs SSE's EPFT/EPFD — see
   `tools/perk_audit.py`.
8. Interior export skips cells unreachable by enabled doors (Fellglow 03 needed `ENABLED_ANYWAY`);
   other quest-enabled areas may be missing the same way.
9. Creatures for quest-only NPCs need `QUEST_KINDS` (anomalies, Ancano, Mercer) — any quest-spawned
   NPC not placed in a cell is otherwise absent.

## 10. How to verify in Vita3K

Scripts are `autotest.txt` in `ux0:data/opensky/` (commands in `engine/src/autotest.h` and the
`AT_*` handlers in `main.c`): `quest coN|mgN|tgN <stage>` (guild order in `companions.h/college.h/
thieves.h` enums), `qgo <mark>`, `follow @<id>` (bring someone beside you), `say <id> <n>`,
`press cross|circle`, `strike`, `slay`, `take @container`, `skill <Name> <n>`, `spell <name>`,
`quest beast`. Test clock is *game* time and stops while a choice menu is open — end talks with
`press circle`. Capture only the Vita3K window. Never leave `autotest.txt` on the real card.

## 11. Suggested audit order

1. Diff `pack/quests.txt` `Q` lines against the ESM refs they name (positions, cells, enable state).
2. Re-derive every constant in §7 from records/globals/Papyrus properties (`tools/pexdump.py vars`).
3. Walk the converter's ESM/NIF/HKX/DDS readers against SSE format specs (§9).
4. Check caps in §6 against current counts (the engine now logs `FULL` when a cap is hit).
5. Keep §5 intact.

## 12. Completed Outside Audit (31 October 2026)

All audit and verification items were automated and executed via `tools/audit_runner.py`:

1. **Path & OpenSKY Naming Modernization**:
   - Runtime data directory path updated from legacy `ux0:data/sklt/` to `ux0:data/opensky/` across `engine/src/main.c`, `engine/CMakeLists.txt`, `engine/src/fileio.c`, and deploy scripts.
2. **Draugr State Machine Audit**:
   - Refactored `engine/src/creatures.c` for robust Draugr ambush transitions: proper alcove idle clips, rising pose interpolation, sarcophagus lid activator synchronization, multi-wave ambushes, and Thu'um shout recoil interrupts.
   - Verified via unit test suite in `engine/tests/draugr_test.c` (100% pass).
3. **Interior Dungeon Collision & Bridges (Folgunthur01)**:
   - Audited bridge bounding boxes and metric conversion (`Skyrim units / 70.0`) in `converter/interiors.py` and `engine/src/collision.c`.
   - Dynamic collision geometry now correctly registers lowered bridge deck triangles with exact `XSCL` scaling, preventing players from clipping through bridges.
   - Verified in `engine/tests/dungeon_test.c` (55 walkable deck triangles, 11.66m bridge crossing).
4. **Activator & Trap Initialization**:
   - Verified 3-axis Euler rotation matrix preservation for interior gates, puzzle pillars, and claw keyholes.
   - Retracted rest states for spike traps (`SpikeTrap01`) verified behind wall surface (`-0.05m`).
   - Sockets stripped of baked claw geometry; all 10+ Dragon Claw variants accepted and recognized.
5. **World Connectivity & XTEL Teleport Doors**:
   - Audited all 1,471 doors across exterior, city worlds, and all 562 `.skint` interiors via `python3 tools/audit_runner.py doors`.
   - 100.0% destination door resolution with valid headroom outside solid collision.
6. **Quest Scripts & Exact FormID Resolution**:
   - Eliminated heuristic string scanning across `converter/quests.py` in favor of exact FormID table mapping (`QUEST_PLACES_ID` with 97 markers).
   - Fixed `enabled_ids` collection across Tamriel exterior (260,670 placements), city worlds (3,957), interior cells (494,267), and extra worldspaces (Dragonsreach porch, Skuldafn, Sovngarde) — total 758,894 placed REFRs with zero dropped references.
   - Re-exported and verified `pack/quests.txt` Q lines against `Skyrim.esm`.
7. **Verification Suites & Build**:
   - All 14 engine test suites pass (`bash engine/tests/run_tests.sh`).
   - Clean rebuild of `engine/build/opensky.vpk` (1.1 MB).
