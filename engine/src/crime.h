#ifndef CRIME_H
#define CRIME_H

#include <stdint.h>

// Skyrim's crime (pack/crime.txt, converter/crime.py): each hold's bounty on you, what each
// crime adds (its crime faction's numbers: theft its value x 0.5, a pickpocketing caught 25,
// trespass 5, assault 40, murder 1000), reported only if someone saw it - the townspeople who
// report to that hold, its guards. A hold with a bounty on you sends its guards: they stop
// you (pay the fine, go to jail, or resist); at 1000 or more, or once you've resisted, they
// attack on sight. Saved with the game.

#define CRIME_HOLDS 16
enum { CRIME_THEFT, CRIME_PICKPOCKET, CRIME_TRESPASS, CRIME_ASSAULT, CRIME_MURDER };

void crime_init(const char *pack_dir);
void crime_clear(void); // (a new game)
int crime_hold_index(const char *faction_eid); // -1: not one
int crime_hold_count(void);
const char *crime_hold_name(int hold);
int crime_bounty(int hold);
int crime_actor_hold(int actor); // the hold a townsperson reports to (actors.h's index), -1

// Where you are, each frame (witnesses see you less well sneaking or unseen)
void crime_set_player(const float feet[3], int sneaking, float visibility);
// A crime at a place: reported to the hold of whoever saw it (victim_hold >= 0: that hold's
// own people's word - the one pickpocketed, the guard struck - counts as seen). value: theft's
// worth. 1 if it was reported
int crime_commit(int crime, int value, const float at[3], int victim_hold);
// Whether anyone who'd report it can see that place now (a lock being picked)
int crime_seen(const float at[3]);

// The guards: their holds' moods kept up (call each frame)
void crime_update(float dt);

// Arrest: a guard (creatures.h's live index) has stopped you. Says the guard's line, then
// (crime_update, once it's said) opens what you can say (menu.h's dialogue); crime_arrest_choice
// takes your answer
enum { ARREST_NONE, ARREST_PAID, ARREST_JAIL, ARREST_RESIST };
void crime_arrest_begin(int guard, int hold, int player_female);
// A guard's greeting in its own voice (Skyrim's: INFO 00020954), for
// its hold, your race and sex; 0 if it has none
int crime_guard_hello(int guard, int hold, const char *voice, int female, const char *race_eid);
int crime_arresting(void);
int crime_arrest_hold(void); // (the hold whose guard it is, -1)
int crime_arrest_choice(int choice); // ARREST_*: what to do (main: jail is its to carry out)
// Jail: into the hold's cell (Skyrim's prison marker: place, feet, heading) - your things in its
// evidence chest, but a lockpick; 0 if its jail isn't here. Then serving your time (sleeping in
// the cell; or at once with no cell here): the days, where you're let out (your things back,
// the bounty and the stolen goods gone). Out of the cell any other way is escaping: Skyrim's
// jailbreak bounty, your things left in the chest
int crime_jail_enter(int hold, int *place, float pos[3], float *heading);
int crime_jail(int hold, int *place, float pos[3], float *heading);
int crime_jailed(void);     // the hold whose cell you're in, -1
int crime_jail_place(void); // ... its place
void crime_escaped(void);

// Saving: the bounties and whether each hold's guards are after you
void crime_save(int32_t bounty[CRIME_HOLDS], uint8_t hostile[CRIME_HOLDS], int32_t *jailed_hold);
void crime_load(const int32_t bounty[CRIME_HOLDS], const uint8_t hostile[CRIME_HOLDS], int jailed_hold);

#endif
