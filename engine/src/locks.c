#include "locks.h"
#include "lighting.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "gamelog.h"
#include "items.h"
#include "lockpick.h"
#include "ui.h"

#define MAX_LOCKS 1024
#define MATCH_M 1.2f

typedef struct {
	int place, level, owned; // (owned: someone's - picking it seen is trespass)
	int night;               // (a home's: locked only at night, as its owners lock it when they turn in)
	float pos[3];
	uint32_t key, ref;
} Lock;

static Lock locks[MAX_LOCKS];
static int lock_count;

void locks_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sinteriors/locks.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	lock_count = 0;
	for (char *line = strtok(buf, "\n"); line && lock_count < MAX_LOCKS; line = strtok(NULL, "\n")) {
		Lock *l = &locks[lock_count];
		unsigned key, ref;
		l->owned = l->night = 0;
		if (sscanf(line, "%d %f %f %f %d %x %x %d %d", &l->place, &l->pos[0], &l->pos[1], &l->pos[2], &l->level, &key, &ref,
		           &l->owned, &l->night) >= 7)
			l->key = key, l->ref = ref, lock_count++;
	}
	free(buf);
	game_log("locks: %d locked doors", lock_count);
}

int locks_find(int place, const float pos[3]) {
	int best = -1;
	float best_d = MATCH_M;
	for (int i = 0; i < lock_count; i++) {
		if (locks[i].place != place || fabsf(locks[i].pos[1] - pos[1]) > 3.0f)
			continue;
		const float d = hypotf(locks[i].pos[0] - pos[0], locks[i].pos[2] - pos[2]);
		if (d < best_d)
			best = i, best_d = d;
	}
	return best;
}

static int have_key(const Lock *l) {
	const int k = l->key ? items_by_form(l->key) : -1;
	return k >= 0 && items_have(k) > 0;
}

#define NIGHT_FROM_H 20.0f // a home's door locked from its owners' bedtime...
#define NIGHT_TO_H 8.0f    // ...till they're up and about
#define NIGHT_OPEN_H 12.0f // (picked or keyed at night: open the rest of that night, locked again the next)
static float night_opened[MAX_LOCKS]; // (game hours it was opened, + 1: 0 never)

int locks_locked(int lock) {
	if (lock < 0 || lock >= lock_count)
		return 0;
	const Lock *l = &locks[lock];
	if (l->night) {
		const float h = lighting_sky()->hours;
		if (h >= NIGHT_TO_H && h < NIGHT_FROM_H)
			return 0; // (by day: open)
		return !(night_opened[lock] > 0.0f && lighting_game_hours() - (night_opened[lock] - 1.0f) < NIGHT_OPEN_H);
	}
	return !items_unlocked(l->ref);
}

static void open_lock(int lock) {
	if (locks[lock].night)
		night_opened[lock] = lighting_game_hours() + 1.0f;
	else
		items_set_unlocked(locks[lock].ref);
}

static const char *tier_name(int level) {
	return level >= 255 ? "Requires key" : level >= 100 ? "Master" : level >= 75 ? "Expert" : level >= 50 ? "Adept"
	     : level >= 25 ? "Apprentice" : "Novice";
}

const char *locks_label(int lock, const char *what) {
	static char label[96];
	if (!locks_locked(lock))
		return NULL;
	const Lock *l = &locks[lock];
	if (have_key(l))
		snprintf(label, sizeof(label), "Unlock %s (key)", what);
	else if (l->level >= 255)
		snprintf(label, sizeof(label), "%s (Requires key)", what);
	else
		snprintf(label, sizeof(label), "Pick lock: %s (%s)", what, tier_name(l->level));
	return label;
}

int locks_try(int lock) {
	if (!locks_locked(lock))
		return 1;
	Lock *l = &locks[lock];
	if (have_key(l)) {
		open_lock(lock);
		ui_notice("Unlocked with its key", 2.0f);
		return 1;
	}
	lockpick_start(l->level, LOCK_TARGET + lock); // ("This lock requires a key" for 255; no lockpicks: says so)
	return 0;
}

int locks_picked(int target) {
	const int lock = target - LOCK_TARGET;
	if (lock < 0 || lock >= lock_count)
		return 0;
	open_lock(lock);
	game_log("locks: %x picked", (unsigned)locks[lock].ref);
	return 1;
}

uint32_t locks_key(int target) {
	const int lock = target - LOCK_TARGET;
	return lock >= 0 && lock < lock_count ? locks[lock].key : 0;
}

int locks_owned(int lock) {
	return lock >= 0 && lock < lock_count && locks[lock].owned;
}

int locks_find_ref(uint32_t ref) {
	for (int i = 0; i < lock_count; i++)
		if (locks[i].ref == ref)
			return i;
	return -1;
}

void locks_set_level(int lock, int level) {
	if (lock >= 0 && lock < lock_count)
		locks[lock].level = level;
}

void locks_unlock(int lock) {
	if (lock >= 0 && lock < lock_count)
		open_lock(lock);
}
