#include "autotest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "gamelog.h"

#define MAX_STEPS 128

typedef struct {
	float at, dur;
	char cmd[16], arg[64];
	float a, b;
	int done;
} Step;

static Step steps[MAX_STEPS];
static int step_count, active;

static unsigned button_of(const char *name) {
	static const struct { const char *name; unsigned bit; } names[] = {
		{"cross", SCE_CTRL_CROSS}, {"circle", SCE_CTRL_CIRCLE}, {"square", SCE_CTRL_SQUARE},
		{"triangle", SCE_CTRL_TRIANGLE}, {"up", SCE_CTRL_UP}, {"down", SCE_CTRL_DOWN}, {"left", SCE_CTRL_LEFT},
		{"right", SCE_CTRL_RIGHT}, {"l", SCE_CTRL_LTRIGGER}, {"r", SCE_CTRL_RTRIGGER}, {"start", SCE_CTRL_START},
		{"select", SCE_CTRL_SELECT}};
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (!strcmp(name, names[i].name))
			return names[i].bit;
	return 0;
}

int autotest_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sautotest.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size);
	if (!text)
		return 0;
	for (char *line = text, *end = text + size; line < end && step_count < MAX_STEPS;) {
		char *nl = memchr(line, '\n', end - line);
		if (!nl)
			nl = end;
		*nl = 0;
		Step *s = &steps[step_count];
		memset(s, 0, sizeof(*s));
		char rest[96] = "";
		if (line[0] != '#' && sscanf(line, "%f %15s %95[^\r]", &s->at, s->cmd, rest) >= 2) {
			if (!strcmp(s->cmd, "press") || !strcmp(s->cmd, "hold")) {
				s->dur = 0.1f;
				sscanf(rest, "%63s %f", s->arg, &s->dur);
			} else if (!strcmp(s->cmd, "stick") || !strcmp(s->cmd, "look")) {
				sscanf(rest, "%f %f %f", &s->a, &s->b, &s->dur);
			} else if (!strcmp(s->cmd, "city")) {
				sscanf(rest, "%63s %f %f", s->arg, &s->a, &s->b);
			} else if (!strcmp(s->cmd, "station")) {
				sscanf(rest, "%f", &s->a);
			} else if (!strcmp(s->cmd, "say")) {
				sscanf(rest, "%63s %f", s->arg, &s->a);
			} else if (!strcmp(s->cmd, "goto")) {
				sscanf(rest, "%f %f", &s->a, &s->b);
			} else {
				snprintf(s->arg, sizeof(s->arg), "%s", rest);
			}
			step_count++;
		}
		line = nl + 1;
	}
	free(text);
	active = step_count > 0;
	if (active)
		game_log("autotest: %d steps", step_count);
	return active;
}

int autotest_active(void) {
	return active;
}

int autotest_title(void) { // ("title" anywhere in the script: the run starts at the title menu, not a new game - its Credits)
	for (int i = 0; i < step_count; i++)
		if (!strcmp(steps[i].cmd, "title"))
			return 1;
	return 0;
}

int autotest_auto_race(void) { // ("autorace" anywhere in the script: the opening's race menu begun as it opens)
	for (int i = 0; i < step_count; i++)
		if (!strcmp(steps[i].cmd, "autorace"))
			return 1;
	return 0;
}

static unsigned char axis(float v) {
	const int a = 128 + (int)(v * 127.0f);
	return (unsigned char)(a < 0 ? 0 : a > 255 ? 255 : a);
}

void autotest_pad(float t, SceCtrlData *pad) {
	if (!active)
		return;
	pad->buttons = 0;
	pad->lx = pad->ly = pad->rx = pad->ry = 128;
	for (int i = 0; i < step_count; i++) {
		const Step *s = &steps[i];
		if (t < s->at || t >= s->at + s->dur)
			continue;
		if (!strcmp(s->cmd, "press") || !strcmp(s->cmd, "hold"))
			pad->buttons |= button_of(s->arg);
		else if (!strcmp(s->cmd, "stick"))
			pad->lx = axis(s->a), pad->ly = axis(s->b);
		else if (!strcmp(s->cmd, "look"))
			pad->rx = axis(s->a), pad->ry = axis(s->b);
	}
}

AutotestAction autotest_next(float t) {
	AutotestAction a = {AT_NONE, "", 0.0f, 0.0f};
	for (int i = 0; i < step_count && active; i++) {
		Step *s = &steps[i];
		if (s->done || t < s->at)
			continue;
		s->done = 1;
		if (!strcmp(s->cmd, "log")) {
			game_log("autotest: %s", s->arg);
			continue;
		}
		a.kind = !strcmp(s->cmd, "city") ? AT_CITY : !strcmp(s->cmd, "goto") ? AT_GOTO : !strcmp(s->cmd, "near") ? AT_NEAR
		       : !strcmp(s->cmd, "shot") ? AT_SHOT : !strcmp(s->cmd, "quit") ? AT_QUIT : !strcmp(s->cmd, "say") ? AT_SAY
		       : !strcmp(s->cmd, "barter") ? AT_BARTER
		       : !strcmp(s->cmd, "station") ? AT_STATION
		       : !strcmp(s->cmd, "kit") ? AT_KIT
		       : !strcmp(s->cmd, "enter") ? AT_ENTER : !strcmp(s->cmd, "opening") ? AT_OPENING : !strcmp(s->cmd, "wear") ? AT_WEAR : !strcmp(s->cmd, "spell") ? AT_SPELL : !strcmp(s->cmd, "warp") ? AT_WARP
		       : !strcmp(s->cmd, "give") ? AT_GIVE : !strcmp(s->cmd, "pull") ? AT_PULL : !strcmp(s->cmd, "left") ? AT_LEFT : !strcmp(s->cmd, "equip") ? AT_EQUIP : !strcmp(s->cmd, "measure") ? AT_MEASURE : !strcmp(s->cmd, "god") ? AT_GOD : !strcmp(s->cmd, "debug") ? AT_DEBUG : !strcmp(s->cmd, "slay") ? AT_SLAY : !strcmp(s->cmd, "take") ? AT_TAKE
		       : !strcmp(s->cmd, "befriend") ? AT_BEFRIEND : !strcmp(s->cmd, "sleep") ? AT_SLEEP : !strcmp(s->cmd, "talk") ? AT_TALK : !strcmp(s->cmd, "quest") ? AT_QUEST : !strcmp(s->cmd, "usedoor") ? AT_USEDOOR : !strcmp(s->cmd, "unequip") ? AT_UNEQUIP : !strcmp(s->cmd, "word") ? AT_WORD : !strcmp(s->cmd, "ride") ? AT_RIDE : !strcmp(s->cmd, "loadsave") ? AT_LOADSAVE : !strcmp(s->cmd, "travel") ? AT_TRAVEL : !strcmp(s->cmd, "dragon") ? AT_DRAGON : !strcmp(s->cmd, "hair") ? AT_HAIR : !strcmp(s->cmd, "search") ? AT_SEARCH : !strcmp(s->cmd, "watch") ? AT_WATCH : !strcmp(s->cmd, "state") ? AT_STATE : !strcmp(s->cmd, "lookat") ? AT_LOOKAT : !strcmp(s->cmd, "skill") ? AT_SKILL : !strcmp(s->cmd, "hp") ? AT_HP : !strcmp(s->cmd, "perk") ? AT_PERK : !strcmp(s->cmd, "follow") ? AT_FOLLOW : !strcmp(s->cmd, "strike") ? AT_STRIKE : !strcmp(s->cmd, "floorword") ? AT_FLOORWORD : !strcmp(s->cmd, "qgo") ? AT_QGO : AT_NONE;
		if (a.kind == AT_NONE)
			continue;
		snprintf(a.arg, sizeof(a.arg), "%s", s->arg);
		a.x = s->a, a.z = s->b;
		game_log("autotest: %.1f s %s %s", t, s->cmd, s->arg);
		return a;
	}
	return a;
}
