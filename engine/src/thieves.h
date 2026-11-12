#ifndef THIEVES_H
#define THIEVES_H

// The Thieves Guild (Skyrim's TG00 .. TG09, TG02B, TGLeadership): A Chance Arrangement - Madesi's
// ring on Brand-Shei; Taking Care of Business - three debts, fists; Loud and Clear - Goldenglow's
// hives and safe; Meet the Family; Dampened Spirits - Honningbrew's vat; Scoundrel's Folly - Gulum-Ei
// and the Firebrand Wine; Speaking With Silence - Snow Veil Sanctum, Karliah; Hard Answers - Enthir,
// Calcelmo's stone; The Pursuit - Mercer's house; Trinity Restored - the Nightingales' oath;
// Blindsighted - Irkngthand, Mercer Frey; Darkness Returns - the Twilight Sepulcher; Under New
// Management. Skyrim's lines and marks (pack/quests.txt: converter/thieves.py), played as the main
// quest's are (questkit.h).

enum { TG_CHANCE, TG_BUSINESS, TG_LOUD, TG_FAMILY, TG_SPIRITS, TG_FOLLY, TG_SILENCE, TG_ANSWERS, TG_PURSUIT,
       TG_TRINITY, TG_BLIND, TG_DARKNESS, TG_LEADER, TG_COUNT };

void thieves_new_game(void);
void thieves_set(int q, int stage); // (testing: a stage begun, the ones before done)
void thieves_update(float dt, const float feet[3], int interior, const char *reached);
int thieves_topics(int actor, const char **lines, int max);
void thieves_say(int actor, int topic);
int thieves_brawl_over(int result); // (a debt collected with fists: 1 if it was one of theirs)
int thieves_marker(float out[2]);
int thieves_journal(const char **names, const char **objectives, int *done, int max);
int thieves_advanced(void);
void thieves_save(int out[24]);
void thieves_load(const int saved[24]);

#endif
