#include "skills.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>

#include "fileio.h"
#include "ui.h"

#define SKILL_MAX 100
#define LEVEL_UP_BASE 75.0f // Skyrim fXPLevelUpBase / fXPLevelUpMult
#define LEVEL_UP_MULT 25.0f

typedef struct {
	char name[32];
	float use_mult, use_offset, improve_mult, improve_offset;
	int start;
} SkillDef;

static SkillDef defs[SKILL_COUNT];
static SkillState state;
static int leveled;

static char keys[SKILL_COUNT][32];    // (their keys: OneHanded...)
static char descs[SKILL_COUNT][240];  // (Skyrim's words for each)

const char *skills_desc(int skill) {
	return skill >= 0 && skill < SKILL_COUNT ? descs[skill] : "";
}

void skills_init(const char *pack_dir) {
	for (int i = 0; i < SKILL_COUNT; i++) { // (if the file is missing: Skyrim's base)
		snprintf(defs[i].name, sizeof(defs[i].name), "Skill %d", i + 1);
		defs[i].use_mult = 1.0f, defs[i].improve_mult = 2.0f, defs[i].start = 15;
	}
	char path[256];
	snprintf(path, sizeof(path), "%sskills.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size);
	if (text) {
		char *buf = realloc(text, size + 1);
		if (buf) {
			buf[size] = 0;
			int i = 0;
			for (char *line = strtok(buf, "\n"); line && i < SKILL_COUNT; line = strtok(NULL, "\n"), i++) {
				SkillDef d;
				char key[32];
				if (sscanf(line, "%31[^|]|%31[^|]|%f|%f|%f|%f|%d", key, d.name, &d.use_mult, &d.use_offset,
				           &d.improve_mult, &d.improve_offset, &d.start) == 7)
					defs[i] = d, snprintf(keys[i], sizeof(keys[i]), "%s", key);
			}
			free(buf);
		} else {
			free(text);
		}
	} else {
		sceClibPrintf("skills: no %s\n", path);
	}
	// (Skyrim's words for each skill, AVIF DESC: the skills menu's - skills_desc.txt "key|text")
	memset(descs, 0, sizeof(descs));
	snprintf(path, sizeof(path), "%sskills_desc.txt", pack_dir);
	char *dt = file_read_all(path, &size), *db = dt ? realloc(dt, size + 1) : NULL;
	if (db) {
		db[size] = 0;
		for (char *line = strtok(db, "\n"); line; line = strtok(NULL, "\n")) {
			char *bar = strchr(line, '|');
			if (!bar)
				continue;
			*bar = 0;
			for (int k = 0; k < SKILL_COUNT; k++)
				if (!strcmp(keys[k], line))
					snprintf(descs[k], sizeof(descs[k]), "%s", bar + 1);
		}
		free(db);
	} else {
		free(dt);
	}
	memset(&state, 0, sizeof(state));
	for (int i = 0; i < SKILL_COUNT; i++)
		state.level[i] = defs[i].start;
	state.player_level = 1;
}

SkillState *skills_state(void) {
	return &state;
}

const char *skills_name(int skill) {
	return skill >= 0 && skill < SKILL_COUNT ? defs[skill].name : "";
}

static float skill_next(int skill) {
	return defs[skill].improve_mult * powf((float)state.level[skill], 1.95f) + defs[skill].improve_offset;
}

static float player_next(void) {
	return LEVEL_UP_BASE + LEVEL_UP_MULT * state.player_level;
}

static void settle(int skill);

static int stone_group = -1;

void skills_set_stone(int group) {
	stone_group = group;
}

static unsigned used_since; // (skills used since a quest last asked: a spell of a school cast - Faralda's test)

int skills_take_used(int skill) {
	const int was = skill >= 0 && skill < SKILL_COUNT && (used_since >> skill & 1u);
	if (skill >= 0 && skill < SKILL_COUNT)
		used_since &= ~(1u << skill);
	return was;
}

void skills_use(int skill, float usage) {
	if (skill >= 0 && skill < SKILL_COUNT && usage > 0.0f)
		used_since |= 1u << skill;
	if (skill < 0 || skill >= SKILL_COUNT || usage <= 0.0f || state.level[skill] >= SKILL_MAX)
		return;
	if (stone_group == skill / 6) // (the Warrior, the Thief, the Mage: Skyrim's 20%)
		usage *= 1.2f;
	state.xp[skill] += usage * defs[skill].use_mult + defs[skill].use_offset;
	settle(skill);
}

void skills_raise(int skill) {
	if (skill < 0 || skill >= SKILL_COUNT || state.level[skill] >= SKILL_MAX)
		return;
	state.xp[skill] = skill_next(skill); // (a whole level, its progress kept as Skyrim's books do: none)
	settle(skill);
}

// Levels earned: the skill's, then the player's
static void settle(int skill) {
	while (state.level[skill] < SKILL_MAX && state.xp[skill] >= skill_next(skill)) {
		state.xp[skill] -= skill_next(skill);
		state.level[skill]++;
		char text[64];
		snprintf(text, sizeof(text), "%s increased to %d", defs[skill].name, state.level[skill]);
		ui_notice(text, 3.0f);
		// The player's level: every skill level counts its new level
		state.player_xp += (float)state.level[skill];
		while (state.player_xp >= player_next()) {
			state.player_xp -= player_next();
			state.player_level++;
			state.pending++;
			state.perk_points++;
			leveled = 1;
			snprintf(text, sizeof(text), "Level %d! Choose a bonus and a perk in Skills", state.player_level);
			ui_notice(text, 5.0f);
		}
	}
	if (state.level[skill] >= SKILL_MAX)
		state.xp[skill] = 0.0f;
}

float skills_progress(int skill) {
	if (skill < 0 || skill >= SKILL_COUNT || state.level[skill] >= SKILL_MAX)
		return 1.0f;
	return fminf(1.0f, state.xp[skill] / skill_next(skill));
}

float skills_player_progress(void) {
	return fminf(1.0f, state.player_xp / player_next());
}

int skills_take_level_up(void) {
	if (state.pending <= 0)
		return 0;
	state.pending--;
	return 1;
}

int skills_take_leveled(void) {
	const int l = leveled;
	leveled = 0;
	return l;
}
