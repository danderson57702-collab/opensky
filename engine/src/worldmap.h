#ifndef WORLDMAP_H
#define WORLDMAP_H

// Skyrim's world map (pack/map, see converter/worldmap.py): the province from the game's
// distant-terrain textures, the converted region more closely, and the map markers (cities,
// towns, caves, forts...). Places in the region are discovered when you come near them (the
// cities show from the start); a discovered one can be fast travelled to.

#include <stdint.h>

#define MAP_MARKERS_MAX 512

void worldmap_init(const char *pack_dir);
// Each frame outdoors: places near the player are discovered (a notice says so)
void worldmap_update(float x, float z);
int worldmap_count(void);
// A marker: its kind (Skyrim's), name, position (engine), whether it's discovered / in the
// converted region (travel only there)
int worldmap_marker(int i, int *kind, const char **name, float *x, float *z, int *discovered, int *in_region);
float worldmap_marker_height(int i); // (Skyrim's marker's height: where fast travel lands; NAN none)
// The map images (GL textures, 0 if missing) and the cells they cover (x0, y0, x1, y1; y up)
int worldmap_marker_world(int i); // (the walled city it's in - fast travel there into it; 0 Tamriel)
unsigned worldmap_province(float bounds[4]);
unsigned worldmap_region(float bounds[4]);
// Saving: which markers are discovered (bits)
void worldmap_save(uint8_t bits[MAP_MARKERS_MAX / 8]);
void worldmap_load(const uint8_t bits[MAP_MARKERS_MAX / 8]);

#endif
