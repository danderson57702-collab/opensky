#include "races.h"

#include "perks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "skills.h"

#define POWER_COOLDOWN_H 24.0f // a greater power: once a day

static Race races[RACE_MAX];
static int race_count, player_race, player_face;
static float power_cooldown;     // game hours until the power can be used again
static float power_left;         // seconds of the power's effect still to run
static const RacePower *power_active;

// The next tab-separated field of a line (NUL-terminated in place)
static char *field(char **p) {
	char *start = *p, *tab = start ? strchr(start, '\t') : NULL;
	if (tab)
		*tab = 0, *p = tab + 1;
	else
		*p = NULL;
	return start ? start : "";
}

void races_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sraces.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	Race *r = NULL;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char *p = line + 2;
		if (line[0] == 'R' && line[1] == '\t' && race_count < RACE_MAX) {
			r = &races[race_count++];
			memset(r, 0, sizeof(*r));
			snprintf(r->eid, sizeof(r->eid), "%s", field(&p));
			snprintf(r->name, sizeof(r->name), "%s", field(&p));
			r->height = (float)atof(field(&p));
			snprintf(r->family, sizeof(r->family), "%s", field(&p));
		} else if (!r || line[1] != '\t') {
			continue;
		} else if (line[0] == 'D') {
			snprintf(r->desc, sizeof(r->desc), "%s", p);
		} else if (line[0] == 'B') {
			int skill, bonus, used;
			while (sscanf(p, " %d:%d%n", &skill, &bonus, &used) == 2) {
				if (skill >= 0 && skill < 18)
					r->boost[skill] = bonus;
				p += used;
			}
		} else if (line[0] == 'P' && r->power_count < RACE_POWERS) {
			RacePower *pw = &r->powers[r->power_count++];
			snprintf(pw->kind, sizeof(pw->kind), "%s", field(&p));
			snprintf(pw->name, sizeof(pw->name), "%s", field(&p));
			snprintf(pw->effect, sizeof(pw->effect), "%s", field(&p));
			pw->magnitude = (float)atof(field(&p));
			pw->seconds = atoi(field(&p));
			snprintf(pw->text, sizeof(pw->text), "%s", field(&p));
		} else if (line[0] == 'F' && r->face_count < RACE_FACES) {
			int f = r->face_count++, rgb[3] = {255, 255, 255};
			snprintf(r->face_stem[f], sizeof(r->face_stem[f]), "%s", field(&p));
			snprintf(r->face_name[f], sizeof(r->face_name[f]), "%s", field(&p));
			sscanf(field(&p), "%d %d %d", &rgb[0], &rgb[1], &rgb[2]);
			for (int k = 0; k < 3; k++)
				r->tone[f][k] = (unsigned char)(rgb[k] < 0 ? 0 : rgb[k] > 255 ? 255 : rgb[k]);
			const char *fam = field(&p);
			snprintf(r->face_family[f], sizeof(r->face_family[f]), "%s", fam && fam[0] ? fam : r->family);
		}
	}
	free(buf);
}

int races_count(void) {
	return race_count;
}

const Race *races_get(int race) {
	return race >= 0 && race < race_count ? &races[race] : NULL;
}

void races_set_player(int race, int face) {
	player_race = race >= 0 && race < race_count ? race : 0;
	const Race *r = races_get(player_race);
	player_face = r && face >= 0 && face < r->face_count ? face : 0;
	power_left = 0.0f, power_active = NULL;
}

int races_player(void) {
	return player_race;
}

int races_player_face(void) {
	return player_face;
}

void races_new_character(Vitals *v) {
	const Race *r = races_get(player_race);
	if (!r)
		return;
	SkillState *sk = skills_state();
	for (int i = 0; i < SKILL_COUNT && i < 18; i++)
		sk->level[i] += r->boost[i];
	for (int i = 0; i < r->power_count; i++) {
		const RacePower *pw = &r->powers[i];
		if (strcmp(pw->kind, "ability"))
			continue;
		float *max = !strcmp(pw->effect, "health")    ? &v->max_health
		             : !strcmp(pw->effect, "magicka") ? &v->max_magicka
		             : !strcmp(pw->effect, "stamina") ? &v->max_stamina
		                                              : NULL;
		if (max)
			*max += pw->magnitude;
	}
	v->health = v->max_health, v->magicka = v->max_magicka, v->stamina = v->max_stamina;
	power_cooldown = 0.0f;
}

static const RacePower *the_power(void) {
	const Race *r = races_get(player_race);
	for (int i = 0; r && i < r->power_count; i++)
		if (!strcmp(r->powers[i].kind, "power") || !strcmp(r->powers[i].kind, "lesser"))
			return &r->powers[i];
	return NULL;
}

const char *races_power_name(void) {
	const RacePower *pw = the_power();
	return pw ? pw->name : NULL;
}

int races_power_ready(void) {
	const RacePower *pw = the_power();
	return pw && (!strcmp(pw->kind, "lesser") || power_cooldown <= 0.0f);
}

int races_use_power(void) {
	if (!races_power_ready())
		return 0;
	const RacePower *pw = the_power();
	power_cooldown = strcmp(pw->kind, "lesser") ? POWER_COOLDOWN_H : 0.0f;
	power_active = pw;
	power_left = (float)pw->seconds;
	return 1;
}

void races_pass_hours(float hours) {
	if (hours > 0.0f)
		power_cooldown = power_cooldown > hours ? power_cooldown - hours : 0.0f;
}

void races_update(float dt, Vitals *v) {
	const Race *r = races_get(player_race);
	v->water_breathing = 0;
	for (int i = 0; r && i < r->power_count; i++)
		if (!strcmp(r->powers[i].kind, "ability") && !strcmp(r->powers[i].effect, "waterbreathing"))
			v->water_breathing = 1;
	v->rate_mult[0] = 1.0f; // (perks: Recovery, Wind Walker)
	v->rate_mult[1] = 1.0f + perks_av(AV_MAGICKA_RATE) / 100.0f, v->rate_mult[2] = 1.0f + perks_av(AV_STAMINA_RATE) / 100.0f;
	if (!power_active || power_left <= 0.0f)
		return;
	power_left -= dt;
	// Skyrim's numbers: Histskin heals 10x as fast; Highborn and Adrenaline Rush are percent
	const RacePower *pw = power_active;
	if (!strcmp(pw->effect, "health_rate"))
		v->rate_mult[0] = pw->magnitude;
	// (Skyrim's Highborn and Adrenaline Rush add to the rate itself - a share of the pool a second: magicka's is 3, stamina's
	// 5 - so +25 is some nine times as fast and +50 eleven, "10x faster" as the game says; read as percent they were +25%
	// and +50%, too little to notice - the user, Nov 10)
	else if (!strcmp(pw->effect, "magicka_rate"))
		v->rate_mult[1] += pw->magnitude / 3.0f;
	else if (!strcmp(pw->effect, "stamina_rate"))
		v->rate_mult[2] += pw->magnitude / 5.0f;
}

float races_power_cooldown(void) {
	return power_cooldown;
}

void races_set_power_cooldown(float hours) {
	power_cooldown = hours > 0.0f ? hours : 0.0f;
	power_left = 0.0f, power_active = NULL;
}

// The power running now (NULL: none): its name, how long it has left, its magnitude
const char *races_power_running(float *left, float *magnitude) {
	if (!power_active || power_left <= 0.0f)
		return NULL;
	if (left)
		*left = power_left;
	if (magnitude)
		*magnitude = power_active->magnitude;
	return power_active->name;
}
