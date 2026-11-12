#ifndef TRAPS_H
#define TRAPS_H

// Skyrim's traps (converter/interiors.py's trap records: in interiors and the outdoor cells):
// triggers - pressure plates, tripwires, trigger boxes, levers - spring what's linked to them
// (through hubs): swinging blades, maces and battering rams, spears, swinging spiked walls,
// falling rocks and oil lamps, dart ports and flamethrowers; bear traps spring when stepped
// on, oil pools burn once lit (a lamp falling on them, fire spells), bone chimes rattle.
// Damage is Skyrim's for the player's level. Creatures set them off and are hurt by them too.

#include <stdint.h>

#include "objects.h"

void traps_clear(void);
// A trap section (an interior's, an outdoor cell's); returns the position after it
const uint8_t *traps_append(const uint8_t *p, const uint8_t *end);
// Each frame: the player's feet (triggers, blows)
void traps_update(float dt, const float player_feet[3]);
// Something activated this reference (a lever): what listens to it springs
void traps_fire_ref(uint32_t ref);
// A lever or chain pulled (this reference): as traps_fire_ref, but swinging blades it reaches stop
void traps_pull_ref(uint32_t ref);
// Springs the trap with this reference itself (a puzzle's darts on a wrong answer); how many
int traps_spring_ref(uint32_t ref);
// Fire here (Flames, a rune's burst, a fire bolt): oil within radius catches
void traps_fire_at(const float pos[3], float radius);
// Damage the traps did the player since last asked
float traps_take_damage(void);
// Burning oil and flamethrowers' fire, to draw with the spells'; returns how many
int traps_sprites(ObjectsSprite *out, int max);

// Unlit oil under the crosshair (within a few metres): its label ("Oil (flammable)"), how far; -1
int traps_aim_oil(const float eye[3], const float dir[3], float *along, const char **label);

#endif
