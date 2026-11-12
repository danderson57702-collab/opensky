#ifndef QUESTKIT_H
#define QUESTKIT_H

// What the quests' own files share (quests.c's): the side quests' files - the Companions'
// (companions.c) - play Skyrim's scenes and conversations, put people at Skyrim's marks and take
// them along, give and take quest things, the same way the main quest does.

// A mark of a quest (Q lines of quests.txt: Skyrim's references): its interior's editor id ("-"
// outdoors, "@prefix" a land of its own), where and which way it faces
typedef struct {
	char key[32], cell[48];
	float pos[3], heading;
} QPlace;
const QPlace *qk_place(const char *key);
int qk_place_here(const QPlace *q, int interior); // (you're in its place)
int qk_at(const char *key, const float feet[3], int interior, float m);
// Where a mark is for the compass: it, if you're in its place, else the map's marker of place
int qk_place_marker(const char *key, const char *place, float out[2]);
int qk_marker_named(const char *name, float out[2]); // (the map's marker by its name)
int qk_who_marker(const char *who, float out[2]);    // (someone, if they're about)

void qk_play(const char *key);     // a scene or conversation of quests.txt (its speaker's, if about)
int qk_busy(void);                 // (one being said)
int qk_actor(const char *id);      // (actors.h's index by editor id, -1)
// Someone put beside you and following (side: metres to your right), -1 if they're not in the pack
int qk_beside(const char *id, const float feet[3], float side_m);
void qk_ally_fights(int ally, float dt, const float feet[3], float *timer); // (at your side: striking what's at them)
void qk_move_to(const char *key); // (a quest moving you there - behind black)
int qk_moving(void);              // (... not there yet, or its people not placed)

int qk_item(const char *name);                 // a quest's thing by its name in quests.txt (items.h's), -1
int qk_has(const char *name);
void qk_give(const char *name, int count);     // (count < 0: taken)
void qk_reward(const char *heavy, const char *light); // (a leveled list's: the armour of your better skill)

// A stage reached: its notice ("Take Up Arms: Speak to Kodlak Whitemane"), the sounds (Skyrim's
// reward sting when done); was: the stage before (0: the quest new)
void qk_stage_notice(const char *quest, int was, int stage, const char *objective);
int qk_advance_count(void); // (stages reached so far, any quest's: the latest one's marker on the compass)

// People a quest directs (questkit.c): with you through every door, fighting at your side (up to
// three); at Skyrim's mark when you're where it is (there, not about their day); kept out of the
// world (dead, away); back to their day
typedef struct {
	const char *who[3];
	int n, place;
	float strike_t[3];
} QkEscort;
void qk_escort(QkEscort *e, const char *a, const char *b, const char *c);
void qk_escort_end(QkEscort *e);
void qk_escort_update(QkEscort *e, float dt, const float feet[3], int interior);
int qk_put(const char *who, const char *mark, int interior);
void qk_gone(const char *who);
void qk_back(const char *who);
int qk_in(const char *cell, int interior); // (you're in that interior: its editor id)

// A quest's foes (Skyrim's disabled ones its quest enables), by their spawn points: placed at marks
// "<prefix><n>" or one by its key, how many not yet killed (asleep away from you too)
#define QK_FOES 24
typedef struct {
	int spawn[QK_FOES], n, at; // (at: the place they were brought out in)
} QkFoes;
void qk_foes_clear(QkFoes *f);
void qk_foes_spawn(QkFoes *f, const char *prefix, int first, int last, const char *kind, const char *name, int interior);
int qk_foes_one(QkFoes *f, const char *key, const char *kind, const char *name, int interior, int ghost); // (its creature, -1)
int qk_foes_left(const QkFoes *f);

#endif
