#ifndef LOCKPICK_H
#define LOCKPICK_H

#include <stdint.h>
#include <psp2/ctrl.h>

// Skyrim's lockpicking: a pick swept across the 180 degree arc above the keyhole (left
// stick / d-pad), the lock turned with the screwdriver (hold CROSS or R). Within the sweet
// spot the lock turns all the way and opens; nearer it, it turns further before jamming;
// forcing a jammed lock wears the pick down until it breaks (a new pick starts at 12
// o'clock; the sweet spot stays). Skyrim's numbers (its data and the game's built-in
// settings): pick life by lock level x (1 + 0.005 x skill), the sweet spot x (0.82 + 0.006
// x skill), the partial turn x (0.775 + 0.015 x skill), skill use by lock level (broken
// picks too). The sweet spot's base width per lock level is the one estimate (the game's
// code that holds it is encrypted): Novice 33, Apprentice 21, Adept 12, Expert 6.5,
// Master 3 degrees. Drawn with Skyrim's lockpicking models.

void lockpick_init(const char *pack_dir);
// Starts on a lock (Skyrim's lock level 1..100; 255: needs a key) for a target; 0 if it
// can't be picked (needs a key / no lockpicks: a notice says why)
int lockpick_start(int lock_level, int target);
int lockpick_active(void);
// Each frame while active: the pad (consumed)
void lockpick_update(const SceCtrlData *pad, float dt);
// The target just unlocked (once), or -1
int lockpick_take_opened(void);
// Over the game, while active
void lockpick_draw(void);

#endif
