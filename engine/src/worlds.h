#ifndef WORLDS_H
#define WORLDS_H

// Walled cities (pack/worlds.txt, see converter/worlds.py): Skyrim's cities are worldspaces
// laid over Tamriel's coordinates, using its land. In one, its cells' objects, navmesh and
// loot are the city's own (files <city>_X_Y); every other cell, and the terrain, Tamriel's.
// Other worldspaces behind the region's doors (Moss Mother Cavern, Bloated Man's Grotto,
// Labyrinthian) have land of their own: there everything, terrain too, is theirs.
// World 0 is Tamriel; 1.. the cities in worlds.txt order.

void worlds_init(const char *pack_dir);
int worlds_count(void);           // including Tamriel
const char *worlds_title(int w);  // "Whiterun"
const char *worlds_prefix_of(int w);
void worlds_set(int w);
int worlds_current(void);
int worlds_find(const char *prefix); // (its number by its prefix - "skuldafn" -; -1 none)
int worlds_own_land(void);          // (the current one is a land of its own - Sovngarde -, not Tamriel's cells)
// The file prefix for a cell in the current world ("tamriel", or the city's for its cells; a
// world with its own land: its own for every cell)
const char *worlds_prefix(int cell_x, int cell_y);
// The terrain's files: Tamriel's, or a world with its own land's
const char *worlds_terrain_prefix(void);

#endif
