#ifndef COLLEGE_H
#define COLLEGE_H

// The College of Winterhold (Skyrim's MG01 .. MG08, in the game's order): First Lessons - Faralda's
// test at the bridge, Mirabelle's tour, Tolfdir's lesson on wards; Under Saarthal - the dig, the
// amulet, Nerien's vision, the Eye; Hitting the Books - Fellglow Keep and the Caller; Good
// Intentions - Ancano, Quaranir, the Augur of Dunlain; Revealing the Unseen - Mzulft's Oculory;
// Containment - Savos Aren dead, Winterhold's anomalies; The Staff of Magnus - Labyrinthian and
// Morokei; The Eye of Magnus - Ancano defeated, the Arch-Mage's chair. Skyrim's lines, scenes and
// marks (pack/quests.txt: converter/college.py), played as the main quest's are (questkit.h).

enum { CG_FIRST_LESSONS, CG_SAARTHAL, CG_BOOKS, CG_INTENTIONS, CG_UNSEEN, CG_CONTAINMENT, CG_STAFF, CG_EYE, CG_COUNT };

void college_new_game(void);
void college_set(int q, int stage); // (testing: a stage begun, the ones before done)
void college_update(float dt, const float feet[3], int interior, const char *reached);
int college_topics(int actor, const char **lines, int max);
void college_say(int actor, int topic);
void college_shouted(const float feet[3]); // (a Shout let go: Faralda hears the Dragonborn's Voice)
int college_marker(float out[2]);
int college_journal(const char **names, const char **objectives, int *done, int max);
int college_advanced(void);
void college_save(int out[16]);
void college_load(const int saved[16]);

#endif
