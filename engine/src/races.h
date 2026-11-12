#ifndef RACES_H
#define RACES_H

// The ten playable races (pack/races.txt, see converter/races.py): Skyrim's skill bonuses,
// abilities (always on) and powers (once a day), and the faces to choose from in the race
// menu. The chosen race and face are the player's character (actors/player/<face>.sknpc).

#include "vitals.h"

#define RACE_MAX 10
#define RACE_FACES 12 // (ten presets and the two Custom faces: the creator's)
#define RACE_POWERS 6

typedef struct {
	char kind[8];    // "ability", "power" (once a day), "lesser" (any time)
	char name[32];
	char effect[16]; // health, magicka, stamina, health_rate, magicka_rate, stamina_rate,
	                 // waterbreathing, nighteye, none
	float magnitude;
	int seconds;
	char text[80];
} RacePower;

typedef struct {
	char eid[24], name[24], family[12];
	float height;
	char desc[480];
	int boost[18];                // skill bonuses (over Skyrim's base 15)
	RacePower powers[RACE_POWERS];
	int power_count;
	char face_stem[RACE_FACES][24], face_name[RACE_FACES][32];
	char face_family[RACE_FACES][16]; // (its body and gear: the race's family, or its women's - "human_f")
	unsigned char tone[RACE_FACES][3]; // skin tone (vertex colour) of each face
	int face_count;
} Race;

void races_init(const char *pack_dir);
int races_count(void);
const Race *races_get(int race);
// The player's: chosen in the race menu (and loaded from a save)
void races_set_player(int race, int face);
int races_player(void);
int races_player_face(void);
// A new character: racial skill bonuses and abilities' extra health / magicka / stamina
void races_new_character(Vitals *v);
// The race's power (once a day): its name, or NULL; whether it can be used now
const char *races_power_name(void);
int races_power_ready(void);
// Uses the power now; 0 if it isn't ready
int races_use_power(void);
// The game clock moved on (hours): a used power comes back after a day
void races_pass_hours(float hours);
// Each frame: abilities (water breathing) and a power's effect while it lasts, on the vitals
void races_update(float dt, Vitals *v);
const char *races_power_running(float *left, float *magnitude); // (the power in effect now: its name, or NULL)
// (saving) game hours until the power can be used again
float races_power_cooldown(void);
void races_set_power_cooldown(float hours);

#endif
