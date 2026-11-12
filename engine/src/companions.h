#ifndef COMPANIONS_H
#define COMPANIONS_H

#include "vitals.h"

// The Companions of Jorrvaskr (Skyrim's C00 .. C06): Take Up Arms - Kodlak's word, Vilkas's test
// in the yard, Eorlund's favour, Aela's shield, Farkas showing you to the whelps' beds; Proving
// Honor - Skjor's errand to Dustman's Cairn with Farkas, the Silver Hand's ambush and what Farkas
// is; the Circle's induction at the Skyforge; The Silver Hand - the blood ritual in the
// Underforge, waking with Aela, Gallows Rock; Blood's Honor - the Glenmoril witches, Jorrvaskr
// attacked; Purity of Revenge - Driftshade Refuge with Vilkas; Glory of the Dead - Kodlak's
// funeral, Wuuthrad made whole, Ysgramor's Tomb, Kodlak's spirit freed. Their lines, scenes and
// marks are Skyrim's (pack/quests.txt: converter/companions.py); played here as the main quest's
// are (questkit.h).

enum { CO_TAKE_UP_ARMS, CO_PROVING_HONOR, CO_BROTHERHOOD, CO_SILVER_HAND, CO_BLOODS_HONOR, CO_PURITY, CO_GLORY,
       CO_COUNT };

void companions_new_game(void);
int companions_stage(int q);
void companions_set(int q, int stage); // (testing: a stage begun)

// Each frame (quests_update's): where you are; dt: seconds; hours: the clock (0..24); v: your
// vitals (the ritual's Beast Form); reached: a conversation's topic just reached, or NULL
void companions_update(float dt, const float feet[3], int interior, float hours, Vitals *v, const char *reached);

// Talking to someone (actors.h's index): what you can say to them now (appended; how many), saying one
int companions_topics(int actor, const char **lines, int max);
void companions_say(int actor, int topic);
int companions_brawl_over(int result); // (a spar ended: 1 if it was Vilkas's)
void companions_container_opened(int interior, const float feet[3]); // (Driftshade's box: the fragments)

int companions_marker(float out[2]);
int companions_journal(const char **names, const char **objectives, int *done, int max);
int companions_advanced(void); // (when one of theirs last moved on: qk_advance_count's then)

void companions_save(int out[16]);
void companions_load(const int in[16]);

#endif
