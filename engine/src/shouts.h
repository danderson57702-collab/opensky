#ifndef SHOUTS_H
#define SHOUTS_H

#include <stdint.h>

// Shouts (the Thu'um), Skyrim's (pack/shouts.txt, converter/shouts.py): Unrelenting Force,
// Fire Breath, Frost Breath, Whirlwind Sprint and those whose walls are in the region (Become
// Ethereal, Disarm, Ice Form, Marked for Death, Slow Time, Kyne's Peace, Animal Allegiance,
// Elemental Fury, Aura Whisper, Throw Voice, Drain Vitality), each of three words. The voice slot (SELECT)
// holds a shout or the race's power, as Skyrim's one button does. Held, a shout gathers words
// (Skyrim's fShoutTime1 / fShoutTime2: a second word after 0.2 s, the third after 0.9 s, as
// many as you know); let go - or at the last word known - it's shouted, in your character's
// voice ("Fus... Ro Dah!"), and needs its words' recovery before the next. The racial powers'
// yells (Battle Cry's) are here too.

#define SHOUT_MAX 24
#define SHOUT_SAVE_SLOTS 32 // (each shout's words in the save, by key: SAVE_KEYS)

typedef struct {
	int shouted;          // a shout went off this frame: its words (1..3), else 0
	int kind;             // ... which (SHOUT_*)
	int dash;             // Whirlwind Sprint: dash along dash_dir (flat, unit) at dash_speed, for dash_s
	float dash_dir[2], dash_speed, dash_s;
	float ethereal_s;     // Become Ethereal: unharmed, and harming nothing, for this long
	float slow_s, slow_mult; // Slow Time: the world at slow_mult (you not), for slow_s
	float fury_s, fury_mult; // Elemental Fury: your blows this much quicker, for fury_s
	float clear_s;        // Clear Skies: the weather cleared, for this long
	float aura_s;         // Aura Whisper: the living and the dead about you on the compass, for this long
	float storm_s, storm_harm; // Storm Call: lightning on your foes about you, this hard, for this long (outdoors)
} ShoutFx;

enum { SHOUT_FORCE, SHOUT_FIRE, SHOUT_FROST, SHOUT_SPRINT, SHOUT_ETHEREAL, SHOUT_DISARM, SHOUT_ICEFORM, SHOUT_MARKED,
       SHOUT_SLOWTIME, SHOUT_KYNESPEACE, SHOUT_ANIMAL, SHOUT_FURY, SHOUT_AURA, SHOUT_THROWVOICE, SHOUT_DRAIN,
       SHOUT_CLEARSKIES, SHOUT_STORMCALL, SHOUT_DISMAY, SHOUT_VALOR, SHOUT_CALLDRAGON, SHOUT_DRAGONREND, SHOUT_KINDS };

void shouts_init(const char *pack_dir);
int shouts_count(void);
const char *shouts_name(int i);
const char *shouts_summon(int i, int word); // (a word's creature: Call of Valor's heroes - "" none)
const char *shouts_words(int i, int words); // its first words ("Fus Ro Dah"), with their meanings
int shouts_known(int i);                    // words of it the player knows (0..3)
// The words known: a new character knows the first of each (to try them: no word walls yet);
// the developer controls, all three
void shouts_set_known_all(int words);
const char *shouts_desc(int i);               // (Skyrim's words for it: the magic menu's)
const char *shouts_word(int i, int word);      // (its word: "Fus")
const char *shouts_meaning(int i, int word);   // (a word's meaning: Fus, "Force")
// Skyrim's way: a word is learned at a word wall (shouts_learn_word: its notice, or NULL if
// it's not one of these shouts' or known already), then unlocked with a dragon's soul
// (shouts_unlock: 1 if one was spent) - only unlocked words are shouted
void shouts_new_character(void);            // (no words, no souls)
const char *shouts_learn_word(const char *word);
const char *shouts_next_word(const char *word); // (the next of that word's shout you don't know; NULL)
int shouts_learned(int i);                  // words of it learned (unlocked or not)
int shouts_unlock(int i);
int shouts_souls(void);                     // dragon souls absorbed and not yet spent
void shouts_add_souls(int n);
unsigned shouts_state(void);                // (saving: the first four shouts', older saves' only)
void shouts_set_state(unsigned state, int souls);
void shouts_save_words(uint8_t out[SHOUT_SAVE_SLOTS]); // (saving: each shout's words - unlocked | learned << 2 - by its key's order)
void shouts_load_words(const uint8_t in[SHOUT_SAVE_SLOTS]);
// The words known by dragon souls absorbed: every shout's first, then each one's second in turn,
// then the third; the word a soul just unlocked ("Fire Breath - Yol"), NULL none
void shouts_set_known_souls(int souls);
const char *shouts_soul_unlocks(int souls_before, int souls_after);
int shouts_equipped(void);                  // the voice slot's shout, -1: the race's power
void shouts_equip(int i);
float shouts_recovery_left(void);
void shouts_reset_recovery(void);            // (a puzzle's gate shut: the next shout at once)           // seconds till the next shout
int shouts_holding(void);                   // the button's held: words gathered so far (0: not)
void shouts_set_recovery_mult(float m);    // Fortify Shouts: the recovery's share (1: as Skyrim's)
void shouts_set_fast_recovery(int on);      // (developer: a few seconds' recovery, to try them)
// The player's voice for the lines: their race (its editor id) and sex
void shouts_set_voice(const char *race_eid, int female);
const char *shouts_voice(void); // (the player's voice type: their grunts too)
// Each frame (unpaused): the shout button held (with a shout in the voice slot), the eye and
// where it looks (unit), the feet; the shout's effects on creatures and the scene happen here,
// the player's own (a sprint) are returned
void shouts_update(float dt, int held, const float eye[3], const float look[3], const float feet[3], ShoutFx *fx);
// A racial power's yell (key: battlecry, emperor, beasttongue) in the player's voice
void shouts_power_yell(const char *key);

#endif
