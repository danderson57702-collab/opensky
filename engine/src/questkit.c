#include "questkit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actors.h"
#include "creatures.h"
#include "gamelog.h"
#include "interior.h"

static const float OFFSTAGE[3] = {0.0f, -500.0f, 0.0f}; // (a script's offstage: actors.c draws no one there)

// --- People ---------------------------------------------------------------------------------

void qk_escort(QkEscort *e, const char *a, const char *b, const char *c) {
	e->who[0] = a, e->who[1] = b, e->who[2] = c;
	e->n = c ? 3 : b ? 2 : a ? 1 : 0, e->place = -999;
}

void qk_escort_end(QkEscort *e) {
	for (int k = 0; k < e->n; k++) {
		const int a = qk_actor(e->who[k]);
		if (a >= 0)
			actors_script_release(a);
	}
	e->n = 0;
}

void qk_escort_update(QkEscort *e, float dt, const float feet[3], int interior) {
	if (!e->n || qk_moving())
		return;
	if (interior != e->place) {
		static const float SIDE[3] = {1.2f, -1.2f, 2.4f};
		for (int k = 0; k < e->n; k++)
			qk_beside(e->who[k], feet, SIDE[k]);
		e->place = interior;
		game_log("quest: %d with you", e->n);
	}
	for (int k = 0; k < e->n; k++)
		qk_ally_fights(qk_actor(e->who[k]), dt, feet, &e->strike_t[k]);
}

int qk_put(const char *who, const char *mark, int interior) {
	const QPlace *q = qk_place(mark);
	const int a = qk_actor(who);
	float at[3];
	if (!q || a < 0 || !qk_place_here(q, interior))
		return 0;
	if (actors_where(a, at) && hypotf(at[0] - q->pos[0], at[2] - q->pos[2]) < 1.5f && fabsf(at[1] - q->pos[1]) < 2.0f)
		return 1;
	actors_script(a, 1);
	actors_script_place(a, q->pos, q->heading);
	actors_script_follow(a, 0);
	game_log("quest: %s at %s", who, mark);
	return 1;
}

void qk_gone(const char *who) {
	const int a = qk_actor(who);
	float at[3];
	if (a < 0 || (actors_where(a, at) && at[1] < -400.0f))
		return;
	actors_script(a, 1);
	actors_script_place(a, OFFSTAGE, 0.0f);
}

void qk_back(const char *who) {
	const int a = qk_actor(who);
	if (a >= 0)
		actors_script_release(a);
}

int qk_in(const char *cell, int interior) {
	return interior >= 0 && interior == interior_find(cell);
}

// --- Foes -----------------------------------------------------------------------------------

void qk_foes_clear(QkFoes *f) {
	f->n = 0, f->at = -999;
}

int qk_foes_one(QkFoes *f, const char *key, const char *kind, const char *name, int interior, int ghost) {
	const QPlace *q = qk_place(key);
	if (!q || !qk_place_here(q, interior) || f->n >= QK_FOES)
		return -1;
	const int i = creatures_place_foe(kind, q->pos, q->heading, ghost, interior);
	if (i >= 0) {
		f->spawn[f->n++] = creatures_live_spawn(i); // (by its spawn: asleep while you're off, still there)
		if (name)
			creatures_set_name(i, name);
	}
	return i;
}

void qk_foes_spawn(QkFoes *f, const char *prefix, int first, int last, const char *kind, const char *name, int interior) {
	for (int n = first; n <= last && f->n < QK_FOES; n++) {
		char key[32];
		snprintf(key, sizeof(key), "%s%d", prefix, n);
		if (!qk_place(key))
			break;
		qk_foes_one(f, key, kind, name, interior, 0);
	}
	f->at = interior;
}

int qk_foes_left(const QkFoes *f) {
	int n = 0;
	for (int k = 0; k < f->n; k++)
		n += creatures_spawn_alive(f->spawn[k]) == 1;
	return n;
}
