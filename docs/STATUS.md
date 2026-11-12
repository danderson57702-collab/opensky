# Where OpenSKY stands (30 October 2026, evening)

An audit of the project against Skyrim's own data (Skyrim.esm, Update, Dawnguard, Hearthfire,
Dragonborn), read from the game files, the git history, the logs from the Vita
(`ux0:data/opensky_log.txt`) and the Vita3K test runs. (First written 17 October.)

Percentages are rough measures, not promises: they count records or places, not hours of play.

## The short version

| Area | Where we are (30 Oct, evening) | Measure | 30 Oct |
|---|---|---|---|
| Core systems (moving, fighting, magic, skills, items, crafting, crime, followers, menus, saving) | all of these work in Vita3K (audit below); 30 Oct: Beast Form (the werewolf: claws by level, feeding, 150 s, once a day) | ~87% | ~87% |
| Map locations | all of Tamriel (29 Oct: every land cell, -57..61 x -43..50) - Markarth, Morthal, Dawnstar, Winterhold, the Pale, Falkreath to the borders; the base game's worlds of their own: Blackreach, Skuldafn, Sovngarde, Alftand and 19 more | 340 of 340 map markers (100%); 11186 land cells | 340 of 340 map markers (100%) |
| Interiors | every door on the map leads to its interior | 561 | 561 |
| Dungeons in the area with no interior | none (only own worlds: Blackreach, Sovngarde... are outside) | 0 | 0 |
| Main quest | Unbound → Dragonslayer: Elder Knowledge, Alduin's Bane, The Fallen, Season Unending, Paarthurnax, The World-Eater's Eyrie, Sovngarde, Dragonslayer added and played through in Vita3K (29 Oct) | 19 of 19 (100%) | 19 of 19 (100%) |
| Other quests | The Golden Claw, A Lovely Letter, Uthgerd's brawl; the Companions (7, 30 Oct); the College of Winterhold (8: First Lessons .. The Eye of Magnus) and the Thieves Guild (13: A Chance Arrangement .. Under New Management), 30 Oct - each played through in Vita3K, a stage at a time | 31 of ~380 (~8%); three guilds' stories whole (their radiant jobs not yet) | 10 |
| Spells | every base-game spell tome castable; dual casting (28 Oct: the five Dual Casting perks, Impact) | 110 spells (≈ all 94 base tomes + some DLC) | 110 spells (≈ all 94 base tomes + some DLC) |
| Shouts | all 20 of the base game work in Vita3K: + Storm Call, Dismay, Call of Valor, Call Dragon, Dragonrend (the last three taught by the main quest) | 20 of 20 base (100%) + Drain Vitality | 20 of 20 base (100%) + Drain Vitality |
| Perks | all 251 can be taken; every one's effect now read by the engine (tools/perk_audit.py) - a sample seen working in Vita3K (Atronach, Reflect Blows...) | 251 with an effect (100%); ~25 seen in play | 251 with an effect (100%) |
| Items | weapons, armour, books, potions, keys, scrolls, soul gems | 6,123 items | 6,123 items |
| Ingredients | all of Skyrim's, DLC's too | 115 | 115 |
| Creatures | 343 kinds (142 creatures, 178 kinds of people, 22 guards): every base-game race - Falmer, Chaurus, automatons, Dragon Priests, Hagravens, werewolves, Wispmothers...; mages cast their own spells, conjure and heal | all base-game creature races (100%) | all base-game creature races (100%) |
| People | named NPCs with schedules, voices; models read only when in view (rooms within rooms too: the Ragged Flagon) | 662 | 662 |
| Combat | melee, power attacks, blocking, bashing, dual casting, staffs, bows, magic, shouts, sneak attacks; Skyrim's killmoves (ten, Vita-sized) and bodies flung by the killing blow; fighting from the saddle; enemy mages' own spells; spells hurt townspeople; enemies block, recoil, stagger you, flee | ~97%: crossbows come with Dawnguard's land | ~97% |

Overall: the **systems** are nearly all there; the **content** (quests, the rest of the map and
the special worlds) is where the remaining work is.

## Audit in Vita3K, Oct 25

Each system tried in Vita3K with a scripted run, checked by the log (a snapshot of the player
before and after: skills, vitals, gold, bounty) and screenshots:

| System | Result |
|---|---|
| Moving: walking, running, sneaking, jumping | works (Sneak rises sneaking near others) |
| Melee: swings, kills, power attacks; blocking | works (One-handed rises; wolves killed and searched) |
| Archery | works (Archery 15 → 16) |
| Magic: Flames, Healing | works (Destruction and Restoration rise; Healing only when it heals, as Skyrim's) |
| Shouts: all 15 | works - each fired, each taking on what it should (Disarm on a bandit, Kyne's Peace on wolves...) |
| Skills and levelling | works (level 4 from crafting, perk points given) |
| Items: loot, bodies, chests | works again (was broken by the 10-21 build: every container empty) |
| Crafting: forge, cooking, enchanting | works (Enchanting 15 → 36) |
| Barter | works (Belethor's goods and gold) |
| Crime: theft, the guard, jail, serving time | works end to end |
| Followers | load and follow (Lydia from your save); allies fight beside you (Ralof) |
| Menus: pause, inventory, map and fast travel | work |
| Saving and loading | works; your old saves load where they were (translated) |
| Riding | **was broken** (getting on with no horse left you nowhere - fixed); rides now |
| Dialogue and quests | Delphine, Malborn, Arngeir: voiced scenes, quest stages |
| Death | works; the cause is now logged |

## Compliance (not giving cause for a cease and desist)

Checked on 26 Oct against the user's DMCA compliance guide (its three golden rules and its
pre-release checklist):

| Checklist | State |
|---|---|
| Rule 1 - no proprietary assets in the binaries, packages or repository | Yes. The VPK holds only eboot.bin, param.sfo and our own icon0.png; the repository tracks no game file (no .bsa/.esm/.dds/.nif/.hkx/.xwm/.wav, no pack). Everything converted lives in git-ignored `pack/` on the user's own machine and card. |
| Setup mandates a valid Steam/GOG install | Yes, since 26 Oct. The converter takes the user's Data folder (`SKYRIM_DATA` or git-ignored `converter/data_path.txt`) and refuses to run unless it holds the master file and the Steam (SkyrimSE.exe, steam_api64.dll) or GOG (goggame-*.info) files beside the game. No install path is in the source any more. |
| Rule 2 - clean-room code, no leaked or decompiled source | Yes. Engine and converter are our own; third-party code is vitaGL (open source) and VitaSDK. The converter only reads the user's files. |
| Rule 3 - no publisher trademarks as the product's name | The app is "OpenSKY" with its own icon and, since 11 Nov, its own title ID (`OSKY00001`; its files on the card are `opensky_*`, the old ones renamed at the first start). The README says OpenSKY with a not-affiliated note. The engine has no hard-coded "Skyrim" text: the open world's name comes from the user's own data (the pack's `=` line in worlds.txt). The repository to publish is named `opensky` (the working folder on the developer's machine keeps its old name; nothing in the tree carries it). |
| Crediting the game's makers | Since 8 Nov: while it starts, the screen says "Bethesda Game Studios presents" over the game's name, and the title menu's Credits page shows the game's own copyright and trademark notice and OpenSKY's not-affiliated note. Both come from the user's own copy (`interface\credits.txt` and the interface strings, through converter/title.py into the pack's legal.txt); the engine names no publisher itself and ships no logo. |
| Funding tied to engine milestones | No funding or bounties so far. If any is ever taken, tie it to OpenSKY engine milestones, not to porting the game. |
| Proprietary scripts and text | Since 11 Nov none of the game's text is in the source or the program. The converter's selectors name lines by their INFO records' form ids (`dialogue.line_is`; 294 selectors changed, the quests' output checked identical before and after); comments and documents name lines the same way (`tools/lines.py` shows a number's words from the user's own copy). The 485 strings the engine showed from its own code - quest names, objectives, the player's choices, names of people, places and things - are empty buffers in the program, named for their records and filled at start from the pack's `gametext.txt`, which the converter writes from the user's own copy (`converter/gametext.py`, `engine/src/gametext.[ch]`; the built program was scanned against every string in the game's tables). Left in the source: single ordinary words the menus share with any game's (Inventory, Magic, Continue...), a few place and rank names in the converter's own tables (`worlds.py`, `creatures.py`), and the engine's own wording of some choices. The git history before 11 Nov still holds the quotes: publish from a new history (one commit), not this one. |

## Audit, Oct 24 (after the 10-21 build on the Vita)

Fixed and seen in Vita3K:
- Every container and body opened empty (enchantments' charge lines shared containers' C letter); saves from that build forget the empty ones.
- Helgen's gate swinging twice (doors reset whenever the cells about you reloaded); Helgen Keep's middle room (the opening's 21 offstage actors drawn under the floor; door collision tested in full by every step and camera ray).
- Word walls pointed at the wrong interiors (walls.txt and stones.txt were never written by the re-export); now in order and in reexport.py.
- Saves from before the wider map: interior, killed creatures and searched bodies translated (legacy/*_1020.txt); saves keep the interior by name (version 14).
- Lydia's hair: dark hair was lifted to a light grey (HAIR_MIN_LUMINANCE 0.55), and torch light skipped the hair tint - blonde in daylight and by a torch.
- Delphine and every quest-placed dragon were past the engine's limits (people 256, creature spawns 1400, kinds 256, pockets, ore veins).

Fixed, checked only by the log: nobody frozen mid-stride (posing turns starved the far half of a crowd); the captain behind the prisoner at the block (Skyrim's aoexecutionguardenter); walked round people off the cart; held still while a place loads (a fall through a floor not loaded yet); deaths logged with their cause.

Still to see on the device: the bow in third person, High Hrothgar "split up" on arrival, the compass going blank, Dark Elf hairs 9 and 13. Not yet in the engine: 16 shouts (Ustengrav's wall teaches Become Ethereal - shouts' save state holds only four), so several new walls teach nothing yet.

## What the logs say

- **The Vita** last ran build **1016-1632** (16 Oct, before crime). Played: Helgen, Helgen Keep,
  Embershard Mine, the wilds. Frames averaged **~37 ms (~27 fps)**; outdoors the GPU wait
  (`swap`) averaged ~18 ms. Nothing since — crime, jail, followers, trainers, the gate,
  brawls, Dragon Rising's new scenes, High Hrothgar, horses — has been played on the Vita;
  all of it was tested only in Vita3K.
- **Vita3K tests** each covered one feature at a time from a scripted start (`opening river`
  + teleports). They don't show how the systems behave together in a long play.
- **Known open problems** (from this session's notes and test runs):
  - word walls: a duplicate "Fus" entry (Bleak Falls, interiors 33 and 79) and a merged
    "GutMeyZul" entry in `walls.txt`;
  - the stables' horse has no saddle; Skulvar's horse is the plain model;
  - Lydia, Irileth and followers fight, but enemies never fight back at them;
  - the Way of the Voice skips the Greybeards' target and gate trials;
  - the letter and claw rewards are our own amounts (Skyrim's Papyrus scripts set them).

## System by system

### World and places

| | Have | Missing |
|---|---|---|
| Converted area | cells −14..19 × −25..12 of Tamriel; Whiterun's city world; Labyrinthian's world; Bloated Man's Grotto | the rest of Tamriel: Solitude, Windhelm, Riften, Markarth, Winterhold, Dawnstar, Morthal, the Reach, the Rift, Eastmarch, the Pale |
| Hold capitals | Whiterun, Falkreath | the other 7 |
| Interiors | 561: every door on the map leads to its interior (29 Oct) | — |
| Map | world map, local maps, compass, fast travel, discovery | carriages (the Whiterun driver isn't exported) |
| Weather, day and night, music | Skyrim's regions | — |
| Special worlds | — | Blackreach, Skuldafn, Sovngarde, Soul Cairn, Solstheim, Apocrypha |

### People

| Have | Missing |
|---|---|
| 187 named NPCs: schedules, sleep, navmesh walking, voiced greetings and chatter | general conversation topics (rumours, INFO 0003A8CC) |
| merchants (buying, selling, restocking, fences), trainers (11), pickpocketing | killing townspeople (they can't be hurt), essential / protected rules |
| guards for each hold, crime and bounties, jail cells, escape | trespass in homes after hours, horse theft, yielding to guards |
| followers: hire, friends, wait, trade, dismiss, housecarl | enemies attacking followers, follower commands, visible follower weapons, animal companions |
| brawls (Uthgerd) | other brawls (they use the same system: Njada, etc.) |
| speech checks: Persuade, Intimidate, Bribe (the Whiterun gate) | speech checks elsewhere (each needs its quest's topic) |
| — | marriage (Amulet of Mara), children (Hearthfire), house purchase (Breezehome), stewards |

### Fighting and creatures

| Have | Missing |
|---|---|
| one- and two-handed, dual wielding, power attacks, blocking, shields, bows, sneak attacks, poisons, enchanted weapons | killmoves, ragdolls, mounted combat, crossbows (Dawnguard) |
| 201 creature kinds: bandits, draugr, dragons (random and lairs, souls), giants, trolls, spiders, atronachs, summons | **Falmer** (32 placed in the area), **Chaurus**, **Slaughterfish**, **Ice Wraiths**, hares, chickens; Dragon Priests; Dwarven automatons (Irkngthand is in the area) |
| NPC enemies with leveled gear | NPCs fighting each other (guards against dragons, followers against bandits — one-sided) |

### Magic, shouts, skills

| Have | Missing |
|---|---|
| 110 spells: all base schools, summons, raised dead, bound weapons, wards, runes, cloaks, illusion | staffs, recharging enchanted weapons with soul gems, ritual spells (College) |
| enchanting, disenchanting, soul trap, soul gems | — |
| shouts: the area's walls' and the Greybeards' - 16 with Clear Skies (28 Oct); word walls; dragon souls | **16 base shouts**: Aura Whisper, Become Ethereal, Call Dragon, Call of Valor, Clear Skies, Disarm, Dismay, Dragonrend, Elemental Fury, Ice Form, Kyne's Peace, Marked for Death, Slow Time, Storm Call, Throw Voice, Animal Allegiance — the area's walls already teach words of 7 of them (Disarm, Animal Allegiance, Kyne's Peace, Elemental Fury, Aura Whisper, Slow Time, Throw Voice), which can be learned but not yet shouted |
| 18 skills, levelling, perk trees (251), 142 perk effects | 109 perk effects (mostly entry points not yet read: e.g. shout cooldowns, spell absorption, quick-shot, extra effects) |
| Guardian Stones and two more stones in the area | the other 8 standing stones (outside the area) |

### Items, crafting, economy

| Have | Missing |
|---|---|
| 5,845 items: 2,337 weapons, 2,190 armour, 500 books, 404 potions and foods, 115 ingredients, keys, scrolls, soul gems | the uniques that come from quests not yet done |
| smithing, tempering, smelting, tanning, cooking, alchemy, enchanting, mining, wood chopping, harvesting | Hearthfire building |
| containers with Skyrim's leveled loot, restocking, dropped things, stolen goods | — |
| horses (buy at Whiterun, ride, fast travel with you) | saddle; other stables; carriages |

### Survival and conditions

Not started: **diseases**, **vampirism**, **lycanthropy** (the Companions need it), bounty letters,
the Dark Brotherhood's assassins, Thalmor justiciars and other random encounters on the roads.

### Quests

| Line | Skyrim | Done |
|---|---|---|
| Main quest | 19 | Unbound, Before the Storm, Bleak Falls Barrow, Dragon Rising, The Way of the Voice, The Horn of Jurgen Windcaller, A Blade in the Dark, Diplomatic Immunity; (28 Oct) A Cornered Rat, Alduin's Wall, The Throat of the World. Next, Elder Knowledge, needs Alftand and Blackreach (Winterhold's hold, and a world of its own): the map is cells (-35,-27)..(45,30) |
| Companions | ~11 story + radiant | none — Jorrvaskr, Dustman's Cairn are in the area |
| College of Winterhold | ~11 | none (Winterhold outside) |
| Thieves Guild | ~13 + jobs | none — Riften, the Ratway and the Ragged Flagon are in the area now (28 Oct) |
| Dark Brotherhood | ~16 + contracts | none (its sanctuary is by Falkreath, at or just past the area's edge) |
| Civil War | ~10 | none (Whiterun's battle is in the area) |
| Daedric | 15–17 | none — *A Daedra's Best Friend* and *Ill Met by Moonlight* (Falkreath) are in the area |
| Side quests | 25 named | The Golden Claw — *In My Time of Need*, *Missing in Action* are in Whiterun |
| Favours and misc | ~190 (many radiant) | A Lovely Letter, Uthgerd's brawl |

Skyrim's quests run on Papyrus scripts, which aren't converted: each quest is rebuilt by hand
from its data (topics, scenes, aliases, markers). The last rounds showed the reusable pieces
work — scenes and voiced topics exported by name, follow-up topics, quest items, markers,
stages saved — so each new quest is mostly content work now.

## Open bugs (28 Oct)

| Bug | State |
|---|---|
| Old saves: Lydia came back as Jordis; bandit camps empty (29 Oct) | fixed: saves kept followers and killed creatures as numbers in lists each wider map renumbered. Version 16 keeps them by key; older saves' follower read with the list of their map (Lydia back), their kills all alive again |
| "Goodbye." played Skulvar's horse line in a man's voice | fixed: it fell in the stablemaster's range with no horse topics |
| The familiar barely visible, no white | fixed: drawn white (the tint only lit the brown fur) |
| A dead dragon's remains: an outline in the air | fixed: lies in Skyrim's ground pose (ground_bleedoutidle) as its ragdoll would lay it; the bones stay so |
| High Hrothgar: set down under the building | fixed: fast travel onto the navmesh nearest Skyrim's marker (it lies on the land under the courtyard's stonework) |
| The Greybeards scattered about High Hrothgar | fixed: held at their MQ105 marks in the hall from your arrival |
| The Word of Power on the floor a yellow blaze | fixed: its letters (fxgreybeardshout<word>), glowing blue |
| Solitude's coast without its sea | fixed: cells with water and none of their own take Tamriel's (-14000) - 1097 cells |
| Followers floating above you on slopes | fixed: off the navmesh outdoors, on the terrain |
| The Blue Palace at 11 fps (328 thousand triangles) | the governor steps in at once past 160 thousand (to ~140 thousand); interiors posed and textured as cities |
| Clairvoyance in a walled city | to its gate toward the objective |
| Scrolls (Blizzard did almost nothing to a dragon) | fixed (Vita3K, 28 Oct): every scroll now casts the spell of its main effect at the scroll's strength (16 of the 24 in the area did nothing). Blizzard is Skyrim's lasting storm (12 m, 20 frost a second for 10 s, its cold holding 5 s, slowing - a landed dragon lost ~300); Fire Storm's three rings hit nearer harder (the spell too); dragons now take burning and freezing over time; Fear no longer fell into the summons code |
| Clairvoyance's way wandered (High Hrothgar) | fixed (Vita3K, 28 Oct): it aimed 60 m along a straight line to the marker; now a graph of where every cell's walkable ground crosses into the next (pack/navmesh/ways.txt, from Skyrim's navmeshes; roads counted shorter) picks the crossing whose way on is shortest - up the 7000 Steps from Ivarstead |
| The Throat of the World's Arngeir | his refusal (DIAL 0003F876) moves on as his yes does; Skyrim has you ask again |
| Karthspire | Delphine and Esbern meet you by its camp; the bridge and pillar puzzles aren't played (the way to the temple's door is open) |
| Conversations with your answers | 12 so far (+ Elenwen's welcome, Razelan's thirst) (the main quest's, Gerdur's, Alvor's, Delphine's, Malborn's); the rest of the game's still said straight through - more to convert |
| Draugr that can't reach you | they now back off to their post after 4 s with no way up, as Skyrim's unable-to-reach (built; not yet seen in Vita3K - needs a ledge); no bow switch |
| Draugr still snag now and then (shorter than before) | needs where |
| Harvested plants stay drawn | they're baked with the scenery; can't be picked again till they grow back (10 days, saved) |
| Kyne's Peace / Animal Allegiance pick animals by kind name | Skyrim's ActorTypeAnimal keyword isn't exported yet |
| Diplomatic Immunity: the Embassy's Thalmor | done (Vita3K, 27 Oct): Skyrim's 15 placed Thalmor, at peace till you leave the party (stage 55, as MQ201's SetEnemy), the hall's guards at peace; the escape scene's two guards in the dungeon with the trapdoor keys; the Solar guard's challenge and persuasion (Speech 25 or an Amulet of Articulation). The party's guests (Skyrim's MQ201Party: 12 and the bard) about the hall; every distraction: Razelan, a friend (Balgruuf once you're his Thane; Vittoria, persuaded at Speech 50 - Skyrim's SpeechAverage), Erikur and Brelas (her freed from the dungeon after); the Thalmor disguise restored from Skyrim's cut script (an enchanted Elven helmet - its unfinished list - with Elven armour or Thalmor Robes; seen through within 18 m humans, 23 m beast races, 7 m Wood/Dark Elves, never High Elves). Not yet: the archers' "Intruder!" lines |
| The Ancient Nord Bow in third person, High Hrothgar's first arrival, Dark Elf hairs 9 and 13 | need photos |

## Locked-door audit (27 Oct)

Doors Skyrim locks with no key and opens by a quest script (found with tools/pexdump.py, by each
quest's fragment-to-stage table), checked against what we've played through:

| Door | Skyrim | Here now |
|---|---|---|
| High Hrothgar's front doors | MQ105 stage 5 unlocks them as the Greybeards summon you | never locked (as asked) |
| Thalmor Embassy: the bar's back room and its door out | MQ201 unlocks them as Malborn takes you out | unlocked at stage 55 |
| Thalmor Embassy: courtyard gate, barracks door | after the escape: a lock to pick (50), the barracks' easy | so after stage 90 |
| Helgen's north gate | MQ101's last stage (1000) unlocks it | unlocked once Unbound's done (it stayed locked for good) |
| Delphine's secret door (Sleeping Giant) | MQ106 stage 32 opens it; its key at the end | unlocked once A Blade in the Dark's begun; key given at its end if the pack has it |

Every other keyless lock belongs to a quest not built yet (Saarthal, Snow Veil Sanctum, the
College's gate...), locked as Skyrim leaves it till then.

## Milestones from here

1. **Play it on the Vita (one round).** Play from a new game through Riverwood, Whiterun and
   Bleak Falls on the device; send the log. Fix what breaks when systems meet (guards,
   followers, brawls, horses together), the wall duplicates, the saddle. Check the frame
   time with guards and followers about.
2. **Finish what's already on the map.** (Oct 25: no dungeon in the area without its interior;
   the area's 11 missing shouts done.) The missing creatures (Falmer, Chaurus, Slaughterfish,
   Ice Wraiths, Dwarven automatons, Dragon Priests, small animals); the last 6 shouts (Call
   Dragon, Call of Valor, Clear Skies, Dismay, Dragonrend, Storm Call: their walls or quests are
   outside the area); the
   remaining perk effects; staffs and recharging; diseases. Mostly converter and engine
   work, little new content.
3. **Whiterun Hold's stories.** The Companions to *Glory of the Dead* (needs lycanthropy),
   *In My Time of Need*, *Missing in Action*, *The Whispering Door*, *A Daedra's Best Friend*,
   *Ill Met by Moonlight*, the Battle for Whiterun if the Civil War is wanted, Breezehome and
   the steward, rumours, and a **radiant quest system** (one engine for "clear this dungeon",
   INFO 000DA242, "kill this bandit leader" — it multiplies content for the least work).
4. **Grow the map for the main quest.** (Oct 21: widened to Ustengrav and Kynesgrove; Oct 25: Haafingar - Solitude, the Embassy. Riften and the Ratway next, for Esbern.) The main quest after the Horn needs Ustengrav,
   Kynesgrove, Solitude (the Thalmor Embassy), Riften (the Ratway), Sky Haven Temple,
   Alftand and Blackreach, Skuldafn and Sovngarde. That means widening the converted region
   to most of Tamriel and the special worlds. The card has room (36 GB free; the pack is
   ~6 GB for 28% of the map), but streaming, memory and the GPU budget need watching.
5. **Fighting that goes both ways.** Enemies that fight followers and allies, townspeople
   who can be hurt (and the crime that follows), NPCs against dragons, killmoves.
6. **The other guilds, the Civil War, the Daedric quests, then the DLCs** — content rounds,
   one questline at a time, as the map reaches them.
