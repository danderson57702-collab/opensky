#include "stones.h"
#include "shouts.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "skills.h"

#define MAX_STONES 16
#define STONE_REACH_M 3.4f

typedef struct {
	char eid[32], name[48], prompt[64];
	float pos[3];
	int group; // 0 the Warrior's skills, 1 the Thief's, 2 the Mage's; -1 a stone not given here yet
} Stone;

static Stone stones[MAX_STONES];
static int stone_count, active = -1;

static void walls_load(const char *pack_dir);

void stones_init(const char *pack_dir) {
	walls_load(pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%sstones.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	stone_count = 0;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line && stone_count < MAX_STONES; line = strtok(NULL, "\n")) {
		Stone *s = &stones[stone_count];
		if (sscanf(line, "%31[^\t]\t%47[^\t]\t%f\t%f\t%f", s->eid, s->name, &s->pos[0], &s->pos[1], &s->pos[2]) != 5)
			continue;
		s->group = strstr(s->eid, "Warrior") ? 0 : strstr(s->eid, "Thief") ? 1 : strstr(s->eid, "Mage") ? 2 : -1;
		snprintf(s->prompt, sizeof(s->prompt), "Activate %s", s->name);
		stone_count++;
	}
	free(buf);
}

#define MAX_WALLS 192 // (the whole map: 119 words on its walls)
#define WALL_REACH_M 6.0f
#define WALL_GLOW_M 25.0f // (its word glows blue for you from this near, till learned)
// A wall: its shout's words as walls.txt lists them (one line each, at the wall's place); Skyrim's
// teaches one - the next of its shout you don't know - and is spent
static struct { char word[3][16]; int n, place; float pos[3]; } walls[MAX_WALLS];
static int wall_count;
static uint64_t walls_spent;

static void walls_load(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%swalls.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	wall_count = 0;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char word[16];
		int place;
		float p[3];
		if (sscanf(line, "%15[^\t]\t%d\t%f\t%f\t%f", word, &place, &p[0], &p[1], &p[2]) != 5)
			continue;
		int w = 0;
		while (w < wall_count && !(walls[w].place == place && fabsf(walls[w].pos[0] - p[0]) < 1.0f &&
		                           fabsf(walls[w].pos[1] - p[1]) < 1.0f && fabsf(walls[w].pos[2] - p[2]) < 1.0f))
			w++;
		if (w == wall_count) {
			if (wall_count == MAX_WALLS)
				continue;
			memset(&walls[w], 0, sizeof(walls[w]));
			walls[w].place = place, memcpy(walls[w].pos, p, sizeof(p));
			wall_count++;
		}
		if (walls[w].n < 3)
			snprintf(walls[w].word[walls[w].n++], sizeof(walls[0].word[0]), "%s", word);
	}
	free(buf);
}

int walls_near(const float feet[3], int place, const char **word) {
	for (int i = 0; i < wall_count; i++) {
		if ((walls_spent >> i & 1) || walls[i].place != place || fabsf(walls[i].pos[1] - feet[1]) > 8.0f ||
		    hypotf(walls[i].pos[0] - feet[0], walls[i].pos[2] - feet[2]) > WALL_REACH_M)
			continue;
		*word = shouts_next_word(walls[i].word[0]); // (its shout's next word for you: none, nothing to learn)
		if (*word)
			return i;
	}
	return -1;
}

int walls_glowing(const float feet[3], int place, float out[3]) {
	for (int i = 0; i < wall_count; i++) {
		if ((walls_spent >> i & 1) || walls[i].place != place || fabsf(walls[i].pos[1] - feet[1]) > 10.0f ||
		    hypotf(walls[i].pos[0] - feet[0], walls[i].pos[2] - feet[2]) > WALL_GLOW_M || !shouts_next_word(walls[i].word[0]))
			continue;
		memcpy(out, walls[i].pos, sizeof(walls[i].pos));
		return i;
	}
	return -1;
}

void walls_spend(int wall) {
	if (wall >= 0 && wall < 64)
		walls_spent |= 1ull << wall;
}

int walls_pos(int wall, float out[3]) {
	if (wall < 0 || wall >= wall_count)
		return 0;
	memcpy(out, walls[wall].pos, sizeof(walls[0].pos));
	return 1;
}

void walls_save(uint32_t out[2]) {
	out[0] = (uint32_t)walls_spent, out[1] = (uint32_t)(walls_spent >> 32);
}

void walls_restore(const uint32_t in[2]) {
	walls_spent = (uint64_t)in[0] | (uint64_t)in[1] << 32;
}

int stones_near(const float feet[3], const char **label) {
	for (int i = 0; i < stone_count; i++)
		if (fabsf(stones[i].pos[1] - feet[1]) < 8.0f &&
		    hypotf(stones[i].pos[0] - feet[0], stones[i].pos[2] - feet[2]) < STONE_REACH_M) {
			if (label)
				*label = stones[i].prompt;
			return i;
		}
	return -1;
}

int stones_take(int stone, const char **notice) {
	static char text[96];
	if (stone < 0 || stone >= stone_count)
		return 0;
	const Stone *s = &stones[stone];
	if (s->group < 0) {
		snprintf(text, sizeof(text), "%s is silent", s->name);
		*notice = text;
		return 0;
	}
	active = stone;
	skills_set_stone(s->group);
	static const char *const WHAT[3] = {"Combat", "Stealth", "Magic"};
	snprintf(text, sizeof(text), "%s: %s skills improve 20%% faster", s->name, WHAT[s->group]);
	*notice = text;
	return 1;
}

int stones_active(void) {
	return active + 1;
}

void stones_set_active(int v) {
	active = v > 0 && v <= stone_count ? v - 1 : -1;
	skills_set_stone(active >= 0 ? stones[active].group : -1);
}
