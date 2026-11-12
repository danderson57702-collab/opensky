#ifndef PEOPLE_H
#define PEOPLE_H

#include <stdint.h>

// The townspeople's lives (pack/actors/combat.txt, converter/people_combat.py): your blows and
// arrows hurt them as they do creatures. Struck, one runs from you and it's assault (crime.h);
// brought down, Skyrim's essential people (the story's - or while a quest's alias makes them so:
// Balgruuf in Before the Storm) kneel in bleedout and get up again, the rest die - murder - and
// stay dead (saved).

#define PEOPLE_DEAD_MAX 64

void people_init(const char *pack_dir); // (after actors_init)
void people_clear(void);                // (a new game: all alive)
// Your blow (as creatures_hit: eye, look, reach, cone): the person struck, 1 if one was
int people_hit(const float eye[3], const float dir[3], float reach, float cone_cos, float damage);
// An arrow's or a bolt's flight (as creatures_ray): how far along it struck someone, -1 none
float people_ray(const float eye[3], const float dir[3], float reach, float radius, float damage);
// A spell's burst (as creatures_area: a fireball, a rune, a spark's touch): how many it struck
int people_area(const float pos[3], float radius, float damage);
// Each frame: the fled come back, the downed get up, the dead lie where they fell (here) or are gone
void people_update(float dt, int place);
int people_dead(int actor);
int people_test_strike(int actor, float damage);
int people_poisoned(int actor, float damage); // (Poisoned: a poison slipped into their pockets - its harm, unseen) // (a test's blow: 1 if they could be struck)
// Saving: the dead (their editor ids' hashes)
void people_save(uint32_t dead[PEOPLE_DEAD_MAX]);
void people_load(const uint32_t dead[PEOPLE_DEAD_MAX]);

#endif
