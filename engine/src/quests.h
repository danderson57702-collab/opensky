#ifndef QUESTS_H
#define QUESTS_H

#include <stdint.h>
#include "vitals.h"

// The main quest's opening, played by hand (Skyrim's are Papyrus scripts; their words and
// items come from pack/quests.txt, converter/quests.py): Before the Storm (tell Jarl
// Balgruuf about Helgen), Bleak Falls Barrow (Farengar's Dragonstone) and Dragon Rising (the
// dragon at the Western Watchtower). Each has stages; the journal shows the current
// objective and the compass a marker toward it.

enum { Q_BEFORE_THE_STORM, Q_BLEAK_FALLS_BARROW, Q_DRAGON_RISING, Q_UNBOUND, Q_WAY_OF_THE_VOICE, Q_HORN,
       Q_LOVELY_LETTER, Q_BRAWL, Q_GOLDEN_CLAW, Q_BLADE, Q_DIPLOMATIC, Q_RAT, Q_WALL, Q_THROAT,
       // (the save's quest_stage[] holds the fourteen above - its [14] is the world's -; the rest its quest_stage_more[])
       Q_ELDER, Q_BANE, Q_FALLEN, Q_SEASON, Q_PAARTHURNAX, Q_EYRIE, Q_SOVNGARDE, Q_DRAGONSLAYER,
       QUEST_COUNT };
#define QUESTS_IN_FIRST 14
#define QUEST_DONE 100

void quests_init(const char *pack_dir);
void quests_set_vitals(Vitals *v); // (yours: a quest may change you - the Companions' Beast Form)
// A new game: everything begun again (opening: Helgen first - else straight to the Jarl)
void quests_new_game(int opening);
// A stage reached (the opening's, intro.c): its notice, the journal's objective
void quests_set(int q, int stage);
int quests_stage(int q);
void quests_save(int stages[16], int more[16]);
void quests_load(const int stages[16], const int more[16]);
// Call Dragon shouted now: 1 if the quest has it (Dragonsreach's porch, The Fallen: Odahviing comes to its trap)
int quests_calls_dragon(void);
int quests_test_goto(const char *key); // (testing: you moved to a quest's place by its key - "time_wound"; 0 none)

// Each frame: where you are (feet; interior.h's current interior), the dragon souls you
// absorbed this frame
void quests_update(float dt, const float feet[3], int interior, int souls_absorbed);

// Talking to someone (actors.h's index): the quest topics they have for you now, as the
// player's lines (up to max); and saying one of them (its number in that list)
int quests_topics(int actor, const char **lines, int max);
void quests_say(int actor, int topic);

// A shout let go (shouts.h's kind): the Greybeards hear it; Clear Skies on the Throat of the World's path
void quests_shouted(int kind, int interior);
// A locked door pressed (locks.h's lock): 1 if a quest took it (Esbern's, in the Ratway Warrens: his voice through it)
int quests_door(int lock);
// A brawl's end (brawl.h's result)
void quests_brawl_over(int result);
// Someone to talk to again now (their follow-up topics: a favour's, a brawl's), -1
int quests_take_reopen(void);
int quests_busy(void); // (a scene being said)
// A quest moving you (the carriage to the Embassy's party): where to, once (interior.h's index)
int quests_take_move(int *interior, float pos[3], float *yaw_deg);
// An answer chosen in a menu of a quest's own (the Embassy's Solar guard): 1 if it was its
int quests_choice(int choice);
// Skyrim's conditions the conversations ask (dtree.c): a thing of a list worn (quests.txt's I
// lines named for the list: "tgamuletofarticulationlist"); someone's relationship with you
// (Skyrim's rank: 1 a friend); the Thalmor disguise's range (metres; -1 not disguised)
int quests_list_worn(const char *list);
int quests_relationship(int actor);
float quests_thalmor_disguise_m(void);
void quests_voice(const char *key);
int quests_take_rumble(void); // (the Greybeards' call: 1 once, to shake the view) // (a scene of quests.txt said: by its speaker if in sight, else heard)

// Opening a container (its handle, items.h) in the current interior: a quest may put its
// item in it first (the Dragonstone in Bleak Falls Barrow's last chest)
void quests_container_opened(int handle, int interior);
void quests_body_searched(int handle, int interior); // (a body searched: the Dragonstone on the Sanctum's last draugr)

// A quest's item by its name in quests.txt (the opening's gear: "rags", "sc_cuirass"...), -1
int quests_item(const char *name);

// Where the current objective is (world x, z; 0 if there's no marker): for the compass
int quests_marker(float out[2]);
// Skyrim's journal: quests set active / inactive (by name, any quest's - the guilds' too), and every active one's marker
#define QUESTS_INACTIVE_MAX 32
int quests_is_active(const char *name);
void quests_toggle_active(const char *name);
void quests_save_inactive(uint32_t out[QUESTS_INACTIVE_MAX]);
void quests_load_inactive(const uint32_t *in); // (NULL: every quest active)
int quests_markers(float out[][2], int max);
void quests_claw_learned(void); // (Arvel in the web found: the Golden Claw's quest begun, if it wasn't)

// The journal: per quest begun, its name, objective (or "Completed") and whether it's done
int quests_journal(const char **names, const char **objectives, int *done, int max);

#endif
