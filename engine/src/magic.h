#ifndef MAGIC_H
#define MAGIC_H

#include "vitals.h"

// The player's spells (Skyrim's, with its costs): Candlelight (Alteration: a floating light
// for a minute), Healing (Restoration: held, heals while you're hurt), Flames (Destruction:
// held, a stream of fire), Sparks (Destruction: held, lightning), Fire Rune (Destruction: a
// rune on the ground that bursts when someone steps on it), Muffle (Illusion: silent steps
// for 3 minutes) and Bound Sword (Conjuration: a sword of light in your hand for 2 minutes). As in Skyrim each hand holds its own spell
// (or nothing: the left may hold the torch instead) and casts with its own button; both
// hands casting is twice the magicka for twice the effect - or, the same spell in both with its
// school's Dual Casting perk, both pressed together, one dual cast (Skyrim's: 2.2 times the
// power for 2.8 times the magicka; Impact: a Destruction bolt staggers). Spells are readied (hands up)
// or put away. Casting spends magicka and trains the spell's school.

enum { SPELL_CANDLELIGHT, SPELL_HEALING, SPELL_FLAMES, SPELL_SPARKS, SPELL_FIRERUNE, SPELL_MUFFLE, SPELL_BOUNDSWORD,
       SPELL_COUNT };
#define MAX_RUNES 3
#define MAGIC_OTHER_MAX 88 // spells learned from tomes (beyond the engine's own seven): by name
#define MAGIC_KEY_LEN 32
#define MAGIC_SCROLL_BASE 1000 // a hand's "spell" this and above: a scroll (its item + this)
enum { HAND_LEFT, HAND_RIGHT }; // (the palms' order, see actors_player_palms)
enum { MAGIC_READY, MAGIC_AIM, MAGIC_SELF }; // a casting arm's pose (actors_player_magic)

typedef struct {
	int spell;            // -1: nothing (or the spells are put away)
	int pose;             // MAGIC_*
	float cast_s;         // how long its current cast has gone on (effects grow in)
	int charged;          // a scroll held long enough to be read when let go
	int flames, healing;  // casting Flames / Healing this frame
	int sparks;           // casting Sparks
	int frost;            // casting Frostbite (its stream: to flame_to)
} MagicHand;

typedef struct {
	MagicHand hand[2];    // HAND_LEFT, HAND_RIGHT
	float flame_to[3];    // where Flames stop (aimed from the eye)
	int flame_hit;        // ... against something
	int candle;           // Candlelight is lit: its light floats at candle_pos
	float candle_pos[3];
	float spark_to[3];    // where Sparks' lightning stops
	int spark_hit;
	int runes;            // Fire Runes lying in wait: where, how long they've lain
	float rune_pos[MAX_RUNES][3], rune_age[MAX_RUNES];
	int bursts;           // runes that burst this frame (someone stepped on them)
	float burst_pos[MAX_RUNES][3];
	int burst_element[MAX_RUNES], rune_element[MAX_RUNES]; // (spellfx.h ELEMENT_*: fire, frost, lightning runes)
	int muffle_cast;      // Muffle cast this frame
	int rune_cast;        // a rune laid this frame
	int heal_burst;       // Fast Healing, Close Wounds cast this frame (Skyrim's golden burst about you)
	int dual;             // dual casting this frame (both hands, one stronger cast)
} MagicFx;

void magic_init(const char *pack_dir); // (pack/spells.txt: Skyrim's costs and magnitudes)
float magic_cost(int spell);         // with the player's perks (per second when held)
int magic_count(void);
const char *magic_name(int spell);
const char *magic_detail(int spell); // ("Alteration, 21 magicka")
int magic_hand(int hand);            // the spell in a hand, -1 none
int magic_dual_casting(void);
// A staff's blow (the staff's item; struck: whether it hit someone, creatures' last struck): its
// spell, for a charge - on the one struck, or as the swing goes (a summons...); 1 if it did
int magic_staff_blow(int staff, int struck, Vitals *v, const float eye[3], const float dir[3]);        // both hands casting one spell, overcharged (Skyrim's dual casting)
void magic_set_hand(int hand, int spell);
// A scroll into a hand (item; -1: out of it): held to charge, let go to read it (used up)
void magic_set_scroll(int hand, int item);
float magic_armour_bonus(void);
const char *magic_bound_name(void); // (the bound weapon now in hand: its item's name - "Bound Bow")
float magic_ward(void);             // a ward held up: its strength (0 none)
int magic_guiding(void);              // Clairvoyance held: the way to the objective shown
float magic_detect(int *undead);    // Detect Life / Dead held: its range (0 not); undead: the dead's
int magic_take_telekinesis(void);   // Telekinesis held this frame      // a scroll's armour (Ironflesh) while it lasts
int magic_readied(void);             // hands up
void magic_set_readied(int readied);
float magic_candle_left(void);       // (saving) seconds
// Oakflesh .. Ebonyflesh on you (its shell drawn over you): 1 oak, 2 stone, 3 iron, 4 ebony, 0 none
int magic_flesh(float *left);
int magic_muffled(void);             // Muffle on: your steps make no sound
// Bound Sword was cast (main puts the sword in your hand) / its time ran out (main takes it
// away): 1 once each
int magic_take_bound_cast(void);
int magic_take_bound_end(void);
void magic_set_candle(float seconds);
// Each frame: each hand's cast button (pressed this frame / held; pressing readies the
// spells), the caster's eye, palms (world; Candlelight starts there), where they look
// (dir: unit) and face (yaw, degrees)
void magic_update(float dt, const int pressed[2], const int held[2], Vitals *v, const float eye[3],
                  const float palms[2][3], const float dir[3], float yaw_deg, MagicFx *fx);

// Spells learned (Skyrim's spell tomes): the engine's own seven as bits, the rest by name (both
// saved); a new character knows Flames and Healing. The tome spells the engine can cast
// (spells.txt X lines: Firebolt, Ice Spike, Fireball, Frostbite, Frost and Lightning Runes,
// the cloaks, Fast Healing, the flesh spells, Invisibility, Waterbreathing, Magelight, Fear,
// Calm, Paralyze, Turn Undead...) follow the seven (magic_count). magic_learn: a tome read
// (the spell's editor id and name): 1 learned, 2 learned but not castable here yet, 0 known
// already
int magic_known(int spell);
unsigned magic_known_mask(void);
void magic_set_known_mask(unsigned mask);
void magic_new_character(void);
int magic_learn(const char *key, const char *name);
int magic_other_count(void);  // (the learned names, for saving: castable ones too)
const char *magic_other(int i);
int magic_castable_name(const char *name); // (a learned name the engine can cast)
void magic_set_other(const char names[][MAGIC_KEY_LEN], int n);
void magic_add_other(const char *name);

#endif
