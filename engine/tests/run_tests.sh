#!/bin/bash
# Host-side tests for engine logic that doesn't need the Vita:
#   collision_test  synthetic geometry (walls, steps, ledges, bridges, pits, jumps)
#   walk_test       real converted terrain + objects around the start, scripted walks
#   navmesh_test    real navmeshes around Riverwood: paths between NPC spots stay on them
#   vitals_test     health and stamina rules (falls, sprinting, regeneration)
#   skills_test     skill experience and levelling (real pack/skills.txt)
#   beds_test       NPCs' beds in Riverwood's houses: reachable on the navmesh
#   items_test      items, leveled loot in containers, potions, saving the inventory
#   perks_test      the perk trees (real pack/perks.txt): taking perks, their effects
#   alchemy_test    alchemy (real pack/items/items.txt): mixing, Skyrim's strengths, eating, saving
#   dragons_test    a dragon's fight (real pack/creatures): flight, breath, landing, crash, its soul
#   talk_test       the townspeople's lines (real pack/actors): hellos and greetings, Whiterun's too
#   shouts_test     the shouts (real pack/shouts.txt): words gathered by holding, recovery
set -e
cd "$(dirname "$0")"
mkdir -p build
cc -std=c11 -O1 -Wall -I../src -o build/collision_test collision_test.c ../src/collision.c -lm
./build/collision_test
cc -std=c11 -O1 -Istub -I../src -o build/walk_test walk_test.c ../src/collision.c ../src/skmesh.c ../src/fileio.c -lm
./build/walk_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/navmesh_test navmesh_test.c ../src/navmesh.c ../src/worlds.c ../src/fileio.c -lm
./build/navmesh_test
cc -std=c11 -O1 -Wall -I../src -o build/vitals_test vitals_test.c ../src/vitals.c -lm
./build/vitals_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/skills_test skills_test.c ../src/skills.c ../src/fileio.c -lm
./build/skills_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/beds_test beds_test.c ../src/navmesh.c ../src/worlds.c ../src/fileio.c -lm
./build/beds_test | tail -1
cc -std=c11 -O1 -Wall -Istub -I../src -o build/items_test items_test.c ../src/items.c ../src/effects.c ../src/skills.c ../src/vitals.c ../src/fileio.c ../src/gametext.c -lm
./build/items_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/perks_test perks_test.c ../src/perks.c ../src/items.c ../src/effects.c ../src/skills.c ../src/vitals.c ../src/fileio.c ../src/gametext.c -lm
./build/perks_test | tail -1
cc -std=c11 -O1 -Wall -Istub -I../src -o build/shouts_test shouts_test.c ../src/shouts.c ../src/fileio.c ../src/gametext.c -lm
./build/shouts_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/alchemy_test alchemy_test.c ../src/alchemy.c ../src/perks.c ../src/items.c ../src/effects.c ../src/skills.c ../src/vitals.c ../src/fileio.c ../src/gametext.c -lm
./build/alchemy_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/dragons_test dragons_test.c ../src/creatures.c ../src/fileio.c ../src/gametext.c -lm
./build/dragons_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/draugr_test draugr_test.c ../src/creatures.c ../src/fileio.c ../src/gametext.c -lm
./build/draugr_test
cc -std=c11 -O1 -Wall -Istub -I../src -o build/talk_test talk_test.c ../src/talk.c ../src/fileio.c -lm
./build/talk_test
cc -std=c11 -O1 -Wall -I../src -o build/dungeon_test dungeon_test.c ../src/collision.c -lm
./build/dungeon_test
