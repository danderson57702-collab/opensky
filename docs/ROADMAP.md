# Roadmap: what's in Skyrim's data, and what we use of it

A survey of Skyrim.esm's record types (118 kinds) against the engine. Sizes are the
records' share of Skyrim.esm; "in region" counts are for the converted area around
Riverwood, Whiterun and Bleak Falls Barrow.

## Already used

| Data | Records | What the engine does with it |
|---|---|---|
| World | WRLD, CELL, LAND, LTEX, STAT, SCOL, TREE, GRAS, MSTT | terrain, objects, LOD, roads |
| Interiors | CELL (interior), LIGH, LGTM | 79 buildings and dungeons, baked lighting, fog |
| Doors | DOOR | teleport doors; doors that open in place (Skyrim's Open sequences) |
| Water | WATR, fish effect models | rivers, lakes, swimming, diving, breath |
| People | NPC_, RACE, HDPT, OTFT, ARMO, ARMA | 88 unique NPCs, FaceGen faces, outfits |
| Routines | PACK (location, time, Sleep template), FURN (beds) | schedules, walking on NAVI navmeshes, sleeping |
| Speech | DIAL, INFO (greetings and idles), voice files | voiced lines with subtitles and lip sync |
| Animation | .hkx clips, behaviour-free | walking, running, swimming, sneaking, jumping, torch, casting, sleeping |
| Character | AVIF, RACE skill boosts | 18 skills, levelling |
| Magic | (3 spells by hand) | Candlelight, Healing, Flames with Skyrim's costs |
| Music | MUSC, MUST | exploration music by time of day |

## Next: interaction and items (your request)

The biggest gap. Everything below shares one new system: an **inventory of real items**.

| Feature | Data | Notes |
|---|---|---|
| Item records | MISC, WEAP, ARMO, AMMO, ALCH, INGR, BOOK, KEYM, SLGM, gold | name, weight, value, model, icon-free list UI |
| Containers | CONT (1,312 in region interiors), LVLI (leveled lists) | chests, barrels, sacks; contents rolled from leveled lists; open/close animation (same Open sequence code as doors); Take / Take all |
| Picking things up | item REFRs already drawn as clutter | "(O) Take" on loose items; the model leaves the world |
| Pickpocket | NPC CNTO + outfit, GMST fPickPocket*, FACT crime factions | Skyrim's success chance from Pickpocket skill, item weight and value; getting caught: the owner reacts, bounty later |
| Lockpicking | REFR XLOC (lock level, key), KEYM | the lockpick minigame on the touch screen or sticks; keys open their locks |
| Harvesting | FLOR (plants, nests) | ingredients into the inventory |
| Reading | BOOK text (DESC strings) | a reading page; skill books train skills |
| Ownership | XOWN on refs and cells | "StealINFO 000E89E4Take" |

## Then: sound

| Feature | Data |
|---|---|
| Footsteps by surface | FSTP, FSTS, MATT, IPDS (impact data sets) |
| Ambience | REGN sounds, SOUN, SNDR, ASPC (acoustic spaces) |
| Actions | magic, doors, containers, water, jumping and landing (SNDR) |

## Then: combat

| Feature | Data |
|---|---|
| Weapons and blocking | WEAP, ARMO (shields), AMMO, PROJ |
| Enemies | LVLN (leveled NPCs: bandits, draugr), creature races (wolves, skeevers), CSTY combat styles |
| Damage and effects | MGEF, SPEL (all of them), ENCH, EXPL, HAZD |
| Perks | PERK (with the skill trees) |
| Shouts | SHOU, WOOP, word walls |

## Later

| Feature | Data | Notes |
|---|---|---|
| Weather | WTHR, CLMT, IMAD, IMGS | rain, snow, fog, storms |
| Activators | ACTI, TACT | levers, pull chains, the Bleak Falls claw door |
| Traps | traps\\ models, HAZD | swinging blades, pressure plates |
| Crafting | COBJ, workbench FURN | smithing, cooking, alchemy, enchanting |
| Quests | QUST, SCEN, DIAL topics, MESG, LCTN | player dialogue choices; most quest logic is in Papyrus scripts (.pex), so quests would be reimplemented by hand, one at a time |
| Map and travel | LCTN, map markers | world map, fast travel |
| Character creation | RACE, HDPT, HAIR, EYES | choosing race and face |
