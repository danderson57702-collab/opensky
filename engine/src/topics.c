#include "topics.h"

#include "dtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "fileio.h"
#include "gamelog.h"
#include "talk.h"
#include "ui.h"
#include "voice.h"

#define MAX_LINES 256
#define MAX_PROMPTS 64
#define MAX_TRAINERS 32
#define QUEUE 6

typedef struct {
	char key[32], speaker[40], path[64], text[220];
	int part;
} Line;

typedef struct {
	char id[40];
	int skill, max, actor;
	char prompt[80], path[64], text[160];
	char done_path[64], done_text[120];
} Trainer;

static Line lines[MAX_LINES];
static int line_count;
static struct { char key[32], text[120]; } prompts[MAX_PROMPTS];
static int prompt_count;
static Trainer trainers[MAX_TRAINERS];
static int trainer_count, resolved = -1;
static const Line *queue[QUEUE]; // (a speakerless answer's parts still to say)
static int queue_n, queue_i;
static char queue_title[48];
static unsigned rng = 0x7f4a7c15u;
static struct { char name[24]; float value; } globals[8]; // (G lines: Skyrim's prices - HorseCost...)
static int global_count;

static int split(char *line, char **f, int max) {
	int n = 0;
	f[n++] = line;
	for (char *p = line; *p && n < max; p++)
		if (*p == '|')
			*p = 0, f[n++] = p + 1;
	return n;
}

void topics_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%stopics.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[8];
		const int n = split(line, f, 8);
		if (f[0][0] == 'T' && n >= 7 && trainer_count < MAX_TRAINERS) {
			Trainer *t = &trainers[trainer_count++];
			memset(t, 0, sizeof(*t));
			snprintf(t->id, sizeof(t->id), "%s", f[1]);
			t->skill = atoi(f[2]), t->max = atoi(f[3]), t->actor = -1;
			snprintf(t->prompt, sizeof(t->prompt), "%s", strcmp(f[4], "-") ? f[4] : "");
			snprintf(t->path, sizeof(t->path), "%s", strcmp(f[5], "-") ? f[5] : "");
			snprintf(t->text, sizeof(t->text), "%s", strcmp(f[6], "-") ? f[6] : "");
		} else if (f[0][0] == 'N' && n >= 4) {
			for (int i = 0; i < trainer_count; i++)
				if (!strcmp(trainers[i].id, f[1]) && !trainers[i].done_text[0]) {
					snprintf(trainers[i].done_path, sizeof(trainers[i].done_path), "%s", strcmp(f[2], "-") ? f[2] : "");
					snprintf(trainers[i].done_text, sizeof(trainers[i].done_text), "%s", f[3]);
				}
		} else if (f[0][0] == 'G' && n >= 3 && global_count < 8) {
			snprintf(globals[global_count].name, sizeof(globals[0].name), "%s", f[1]);
			globals[global_count++].value = (float)atof(f[2]);
		} else if (f[0][0] == 'P' && n >= 3 && prompt_count < MAX_PROMPTS) {
			snprintf(prompts[prompt_count].key, sizeof(prompts[0].key), "%s", f[1]);
			snprintf(prompts[prompt_count++].text, sizeof(prompts[0].text), "%s", f[2]);
		} else if (f[0][0] == 'L' && n >= 6 && line_count < MAX_LINES) {
			Line *l = &lines[line_count++];
			snprintf(l->key, sizeof(l->key), "%s", f[1]);
			l->part = atoi(f[2]);
			snprintf(l->speaker, sizeof(l->speaker), "%s", f[3]);
			snprintf(l->path, sizeof(l->path), "%s", strcmp(f[4], "-") ? f[4] : "");
			snprintf(l->text, sizeof(l->text), "%s", f[5]);
		}
	}
	free(buf);
	game_log("topics: %d trainers, %d lines, %d prompts", trainer_count, line_count, prompt_count);
}

const char *topics_prompt(const char *key) {
	for (int i = prompt_count - 1; i >= 0; i--) // (the last: a later plugin's)
		if (!strcmp(prompts[i].key, key))
			return prompts[i].text;
	return "";
}

int topics_say(const char *key, int actor, const char *title) {
	if (actor >= 0 && dtree_start(dtree_find(key), actor)) // (a conversation of Skyrim's: your answers to choose)
		return 1;
	int pick[32], n = 0;
	for (int i = 0; i < line_count && n < 32; i++)
		if (!lines[i].part && !strcmp(lines[i].key, key))
			pick[n++] = i;
	if (!n)
		return 0;
	rng = rng * 1664525u + 1013904223u;
	const int first = pick[(rng >> 8) % n];
	static const char *paths[QUEUE], *texts[QUEUE]; // (talk_scene may keep them)
	int k = 0;
	for (int i = first; i < line_count && k < QUEUE && (i == first || lines[i].part); i++)
		paths[k] = lines[i].path, texts[k] = lines[i].text, queue[k++] = &lines[i];
	if (actor >= 0 && talk_scene(actor, paths, texts, k)) {
		queue_n = 0;
		return 1;
	}
	snprintf(queue_title, sizeof(queue_title), "%s", title ? title : "");
	queue_n = k, queue_i = 0;
	topics_update();
	return 1;
}

int topics_busy(void) {
	return queue_n > 0 || voice_playing() || talk_speaking() >= 0;
}

void topics_update(void) {
	if (queue_n <= 0 || (queue_i > 0 && voice_playing()))
		return;
	if (queue_i >= queue_n) {
		queue_n = 0;
		ui_subtitle(NULL, NULL); // (said: its words off the screen)
		return;
	}
	const Line *l = queue[queue_i++];
	if (l->path[0])
		voice_play(l->path);
	ui_subtitle(queue_title, l->text);
}

static void resolve(void) {
	if (resolved == actors_count())
		return;
	resolved = actors_count();
	for (int t = 0; t < trainer_count; t++) {
		trainers[t].actor = -1;
		for (int a = 0; a < actors_count(); a++)
			if (!strcmp(actors_id(a), trainers[t].id))
				trainers[t].actor = a;
	}
}

static Trainer *trainer_of(int actor) {
	resolve();
	for (int t = 0; t < trainer_count; t++)
		if (trainers[t].actor == actor && actor >= 0)
			return &trainers[t];
	return NULL;
}

int topics_trainer(int actor, int *skill, int *max) {
	const Trainer *t = trainer_of(actor);
	if (!t)
		return 0;
	*skill = t->skill, *max = t->max;
	return 1;
}

const char *topics_trainer_prompt(int actor) {
	const Trainer *t = trainer_of(actor);
	return t ? t->prompt : "";
}

void topics_trainer_say(int actor, int done) {
	const Trainer *t = trainer_of(actor);
	if (!t)
		return;
	const char *path = done ? t->done_path : t->path, *text = done ? t->done_text : t->text;
	if (!text[0])
		return;
	talk_scene(actor, &path, &text, 1);
}

float topics_global(const char *name) {
	for (int i = 0; i < global_count; i++)
		if (!strcmp(globals[i].name, name))
			return globals[i].value;
	return 0.0f;
}
