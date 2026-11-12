#ifndef RIDING_H
#define RIDING_H

#include <stdint.h>

// Skyrim's horses (pack/horses.txt, converter/creatures.py): the stables' horse, bought from the
// stablemaster (Skulvar Sable-Hilt at Whiterun's: DIAL 00068CB6), saddled and
// waiting by the stables. Yours to ride: Skyrim's rider in the saddle walking, trotting, at a run
// and a gallop; left where you get off (it waits outside while you're indoors), and along with
// you when you fast travel. Saved with the game.

#define RIDING_WALK_MS 2.6f   // the horse's paces
#define RIDING_RUN_MS 8.0f
#define RIDING_SPRINT_MS 11.5f
#define RIDING_EYE_LIFT_M 1.0f // (in the saddle: the view this much higher)

void riding_init(const char *pack_dir);
void riding_clear(void); // (a new game: no horse)

// The stablemaster: what you can say about a horse (appended to opts; how many); one said
int riding_topics(int actor, const char **opts, int max);
void riding_say(int actor, int k);
int riding_take_reopen(void); // (the stablemaster to talk to again: his offer, once he's said he has one)

// Your horse under the crosshair (within reach): 1, its distance along the ray and "Ride Horse"
int riding_aim(const float eye[3], const float dir[3], float *along, const char **label);
int riding_mounted(void);
// Into the saddle: where you are now (the horse's back) and which way you face (degrees)
int riding_mount(float feet[3], float *yaw_deg); // (1: in the saddle; 0: no horse of yours, feet untouched)
// Down from it: where you stand (beside it)
void riding_dismount(float feet[3]);

// Each frame: the horse drawn where it waits, or under you (its pace from yours: m/s); place is
// interior.h's (a different one: it waits where it was)
void riding_update(float dt, const float feet[3], float body_heading_deg, float speed, int place);
// Fast travel: your horse comes with you (if you're on it or it was near)
void riding_traveled(const float from[3], const float to[3], int place);

// Saving: owned, where it is (place, x, y, z), and ridden
void riding_save(float out[6]);
void riding_load(const float in[6]);

#endif
