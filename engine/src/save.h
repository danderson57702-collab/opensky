#ifndef SAVE_H
#define SAVE_H
#include "chargen.h"

#include "followers.h"

// One saved game (ux0:data/opensky_save.dat): where you are (outdoors or which interior, and
// the way in), facing, the time of day, your vitals, what you wear, the camera, your skills,
// level and spells, your race and face. Version 1 saves (before skills) still load, with fresh
// skills; version 2 (before races), as a Nord.

#include <stddef.h>
#include <stdint.h>

#define SAVE_ITEMS 32

typedef struct {
	int interior; // -1 Tamriel, -2 - n walled city n (interior.h)
	float pos[3]; // feet
	float yaw, pitch;
	float entry[3], entry_yaw; // the interior's way in (interior_entry)
	float hours;
	float health, magicka, stamina;
	int third_person;
	int item_count;
	unsigned char equipped[SAVE_ITEMS];
	// (version 2)
	int skill_level[18];
	float skill_xp[18];
	int player_level, level_pending;
	float player_xp;
	float max_health, max_magicka, max_stamina;
	int spell;          // SAVE_SPELL_HANDS | left + 1 | (right + 1) << 4 (0: nothing)
	float candle_left;  // seconds of Candlelight
	// (version 3)
	int race, face;       // races.h (earlier saves: 0, the first Nord face)
	float power_cooldown; // game hours until the racial power is back
	// (version 4)
	int perk_points;          // unspent
	uint8_t perks[32];        // taken (perks.h bits)
	int dead_count;           // creatures killed (creatures.h: their spawn points)
	uint16_t dead[1024];
	uint8_t discovered[64];   // the map's places found (worldmap.h bits)
	uint32_t spells_known;    // (version 5) magic.h bits | SAVE_SPELLS_SET (earlier saves: all known)
	uint32_t books_read[96];  // skill books read (forms)
	char spells_other[24][32]; // spells learned from tomes not castable here yet (names)
	// (version 6)
	char spells_more[64][32];  // more spells learned from tomes (names: past spells_other's 24)
	char hand_spells[2][32];   // a tome spell in a hand (left, right: its name; "": see spell)
	float game_hours;          // game hours since it began (cells reset by it)
	float dead_at[1024];       // when each of dead[] was killed (game hours)
	int dragon_souls;          // absorbed (each unlocks a word of a shout)
	// (version 7)
	int quest_stage[16];       // quests.h: each quest's stage (0 not started); [14] the world's
	                           // switches (objects.h), [15] the opening (intro.h)
	// (version 8)
	char player_name[32];      // (name.h; earlier saves: "Prisoner")
	// (version 9)
	int8_t look[8];            // the character creator's parts (chargen.h: hair, beard, eyes, brows, scar; -1 none)
	// (version 10)
	int32_t bounty[16];        // crime.h: each hold's bounty on you
	uint8_t hold_hostile[16];  // ... its guards after you (you resisted)
	// (version 11)
	int32_t jailed_hold;       // the hold whose jail cell you're in (-1 none)
	int32_t followers[4];      // followers.h: who follows you (and how), saved
	// (version 12)
	float horse[6];            // riding.h: your horse - owned, where (place, x, y, z, heading)
	// (version 13)
	uint32_t walls_used[2];    // stones.h: the word walls that have taught you their word
	// (version 14)
	char interior_id[48];      // the interior's editor id ("" outdoors): its number changes as the map grows
	// (version 15)
	uint8_t shout_words[16];   // shouts.h: each shout's words (by its key - fifteen now, the first four in quest_stage[14] before)
	// (version 16)
	uint32_t people_dead[64];  // people.h: the townspeople killed (their editor ids' hashes)
	FollowersSave followers_by_id; // followers.h: who follows you, friends, paid - by name (followers[] above: older saves')
	uint32_t dead_keys[1024];  // creatures.h: the killed creatures' spawn points by key (dead[] above: their numbers, older saves')
	// (version 17)
	uint8_t shout_words_more[16]; // shouts.h: the shouts past the sixteenth (Storm Call, Dismay, Call of Valor...)
	// (version 18)
	int quest_stage_more[16];  // quests.h: the quests past the fourteenth (Elder Knowledge .. Dragonslayer)
	uint32_t vendors_invested[8]; // items.h: the stores Investor put gold into (a bit each)
	// (version 19)
	int companions[16];        // companions.h: the Companions' quests (Take Up Arms .. Glory of the Dead), what's done
	float beast[2];            // beast.h: Beast Form - the blood in you, hours till it can be called again
	// (version 20)
	int college[16];           // college.h: the College of Winterhold's quests (First Lessons .. The Eye of Magnus)
	int thieves[24];           // thieves.h: the Thieves Guild's
	// (version 21)
	ChargenLook look_full;     // chargen.h: the character's look - preset, sliders, colours (look[] above: its parts, older saves')
	// (version 22)
	uint32_t quests_inactive[32]; // quests.h: the quests set inactive in the journal (their names' hashes)
	uint32_t favorites[32];       // menu.h: the favourites - spells, shouts, weapons (their names' hashes)
	uint8_t favorite_kind[32];    // ... 1 a spell, 2 a shout or power, 3 a weapon (0: none)
} SaveGame;
#define SAVE_SPELLS_SET 0x80000000u

#define SAVE_SPELL_DUAL 0x100  // (earlier: the spell | this, in both hands)
#define SAVE_SPELL_HANDS 0x200
#define SAVE_GAME_V1_SIZE (offsetof(SaveGame, skill_level)) // (version 1 saves end here)
#define SAVE_GAME_V2_SIZE (offsetof(SaveGame, race))        // (version 2 saves end here)
#define SAVE_GAME_V3_SIZE (offsetof(SaveGame, perk_points)) // (version 3 saves end here)
#define SAVE_GAME_V4_SIZE (offsetof(SaveGame, spells_known)) // (version 4 saves end here)
#define SAVE_GAME_V5_SIZE (offsetof(SaveGame, spells_more))  // (version 5 saves end here)
#define SAVE_GAME_V6_SIZE (offsetof(SaveGame, quest_stage))  // (version 6 saves end here)
#define SAVE_GAME_V7_SIZE (offsetof(SaveGame, player_name))  // (version 7 saves end here)
#define SAVE_GAME_V8_SIZE (offsetof(SaveGame, look))         // (version 8 saves end here)
#define SAVE_GAME_V9_SIZE (offsetof(SaveGame, bounty))       // (version 9 saves end here)
#define SAVE_GAME_V10_SIZE (offsetof(SaveGame, jailed_hold)) // (version 10 saves end here)
#define SAVE_GAME_V11_SIZE (offsetof(SaveGame, horse))       // (version 11 saves end here)
#define SAVE_GAME_V12_SIZE (offsetof(SaveGame, walls_used))  // (version 12 saves end here)
#define SAVE_GAME_V13_SIZE (offsetof(SaveGame, interior_id)) // (version 13 saves end here)
#define SAVE_GAME_V14_SIZE (offsetof(SaveGame, shout_words)) // (version 14 saves end here)
#define SAVE_GAME_V15_SIZE (offsetof(SaveGame, people_dead)) // (version 15 saves end here)
#define SAVE_GAME_V16_SIZE (offsetof(SaveGame, shout_words_more)) // (version 16 saves end here)
#define SAVE_GAME_V17_SIZE (offsetof(SaveGame, quest_stage_more)) // (version 17 saves end here)
#define SAVE_GAME_V18_SIZE (offsetof(SaveGame, companions)) // (version 18 saves end here)
#define SAVE_GAME_V19_SIZE (offsetof(SaveGame, college))    // (version 19 saves end here)
#define SAVE_GAME_V20_SIZE (offsetof(SaveGame, look_full))  // (version 20 saves end here)
#define SAVE_GAME_V21_SIZE (offsetof(SaveGame, quests_inactive)) // (version 21 saves end here)

// Save slots (ux0:data/opensky_saves/): SAVE_AUTOS autosaves taking turns (the oldest is
// replaced, so a bad one never costs you the rest), then the saves you make. Each has its
// game (NN.sav), inventory (NN.items, items.c) and a picture of the scene (NN.thumb:
// SAVE_THUMB_W x SAVE_THUMB_H RGB565). The save from before slots is moved into the first
// manual slot.
#define SAVE_AUTOS 3
#define SAVE_SLOTS 27 // (autosaves included)
#define SAVE_THUMB_W 160
#define SAVE_THUMB_H 90
enum { SAVE_NEW = -1, SAVE_AUTO = -2 }; // (save_write's slot: a new save, the next autosave)

typedef struct {
	int slot;
	unsigned seq;      // order saved (newest highest)
	int autosave;
	char place[48];    // INFO 00019F96, "Whiterun", "Skyrim"
	char race[24];
	int level;
	float hours;       // the time of day in the game
	char date[32];     // when it was saved ("2026-10-03 20:31")
} SaveInfo;

void save_init(void);
// The saves, newest first; returns how many
int save_list(SaveInfo *out, int max);
// Writes the game into slot (SAVE_NEW: a free slot, SAVE_AUTO: the next autosave's); its
// inventory is written by the caller to save_items_path(the slot returned). Returns the
// slot, -1 on failure
int save_write(int slot, const SaveGame *g, const SaveInfo *info, const uint16_t *thumb);
int save_read(int slot, SaveGame *g); // 1 if it's a valid save
int save_read_thumb(int slot, uint16_t *out); // SAVE_THUMB_W x SAVE_THUMB_H
const char *save_items_path(int slot);
int save_latest(void);
int save_read_version(void);
const char *save_read_date(void); // (when the save save_read last read was made: "2026-10-21 13:12") // the format version of the save save_read last read (1..4) // the newest save's slot, -1: none

#endif
