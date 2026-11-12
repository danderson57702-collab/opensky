#ifndef LOCKS_H
#define LOCKS_H
#include <stdint.h>

// Skyrim's locked doors (converter/interiors.py: interiors/locks.txt) - jail doors, houses,
// dungeons' ways on: picked (lockpick.c) or opened with their key. Chests' locks are loot.c's.

#define LOCK_TARGET 100000 // (a lockpick target this far up: a door's lock, its index past it)

void locks_init(const char *pack_dir);
// The lock on the door at pos in place (interior index; -1 Tamriel; -2 - n a walled city), or -1
int locks_find(int place, const float pos[3]);
// Still locked (not picked, not opened with its key)?
int locks_locked(int lock);
// The prompt for a locked door ("Pick lock: Door (Novice)", "Door (Requires key)"), or NULL
const char *locks_label(int lock, const char *what);
// Trying the door: 1 it opens (unlocked, or its key carried - then unlocked for good); 0 not
// (the lockpicking begun, or it needs its key)
int locks_try(int lock);
// A lockpicking's target (lockpick_take_opened) that was a door's: unlocked, 1; else 0
int locks_picked(int target);
int locks_owned(int lock);
uint32_t locks_key(int target); // (a picked door's key: its form id, 0 none - Wax Key copies it)
// A door's lock by its Skyrim reference (a quest's door), -1; its level set (a quest's
// SetLockLevel: 255 a key's, 1..100 to be picked)
int locks_find_ref(uint32_t ref);
void locks_set_level(int lock, int level);
void locks_unlock(int lock); // (opened for you: a quest's - Whiterun's gate) // (someone's: picking it where they'd see is trespass)

#endif
