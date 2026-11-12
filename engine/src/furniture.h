#ifndef FURNITURE_H
#define FURNITURE_H

// Chairs, benches, stools and beds the player can use (their markers: converter/interiors.py
// "furniture", inside and in the outdoor cells around you): sit down on a seat (Skyrim's
// chair and stool clips), sleep in a bed (the Sleep menu).

#include <stdint.h>

enum { FURN_SEAT = 1, FURN_STOOL, FURN_BED, FURN_BEDROLL, FURN_ALCHEMY, FURN_CRAFT, FURN_RESOURCE }; // (an alchemy lab: the alchemy menu;
// a crafting station: its menu - furniture_station: 1 forge, 2 smelter, 3 tanning rack, 4 cooking pot, 5 grindstone,
// 6 armour workbench, 7 arcane enchanter)
int furniture_station(int index);
// Its reference (Skyrim's form id: an ore vein's or chopping block's, items_resource)
uint32_t furniture_ref(int index);

void furniture_clear(void);
// A furniture section (an interior's, an outdoor cell's); returns the position after it
const uint8_t *furniture_append(const uint8_t *p, const uint8_t *end);
// The piece under the crosshair within reach (-1: none): its distance along the ray, its label
int furniture_aim(const float eye[3], const float dir[3], float *along, const char **label);
int furniture_kind(int index);
int furniture_find(const char *label);
void furniture_set_taken(int index, int on); // (someone sat in it: not offered to you) // (the first piece about you labelled so - "Sit (Throne)" -, -1)
// Where to sit on a seat (or lie in a bed), for someone at (x, z): its free spot nearest them, facing, and the
// clips to get on / sit / get off by (Skyrim's: from the front, left or right); 0 if none
int furniture_seat(int index, float x, float z, float pos[3], float *heading, const char **in, const char **idle,
                   const char **out);

#endif
