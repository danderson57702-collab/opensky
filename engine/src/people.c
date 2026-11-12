#include "people.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "crime.h"
#include "fileio.h"
#include "gamelog.h"
#include "navmesh.h"
#include "quests.h"
#include "sfx.h"

#define MAX_PEOPLE 1200     // (actors.c's MAX_ACTORS)
#define PERSON_RADIUS_M 0.35f
#define PERSON_MID_M 1.1f   // a blow lands about here up them
#define FLEE_S 8.0f         // struck: running from you this long, then back to their day
#define FLEE_M 12.0f
#define BLEEDOUT_S 10.0f    // an essential one brought down: on their knees this long, then up at half
#define OFFSTAGE_Y -1000.0f // (the dead, not here: out of sight)

enum { P_ESSENTIAL = 1, P_PROTECTED = 2 };
#define QUEST_FLAGS_MAX 4

typedef struct {
	int known;               // (in combat.txt)
	int flags;
	float health, max_health;
	int quest[QUEST_FLAGS_MAX], quest_flags[QUEST_FLAGS_MAX]; // (Q_*, -1: their aliases' while it runs)
	float flee_t, down_t;
	int dead, dead_place, struck_once;
	float dead_at[3], dead_heading;
} Person;

static Person people[MAX_PEOPLE];
static int count;
static int last_place = -999;

// Skyrim's quests the engine plays, by editor id (an alias making someone essential while it runs)
static int quest_of(const char *eid) {
	static const struct { const char *eid; int q; } Q[] = {
		{"MQ101", Q_UNBOUND}, {"MQ102", Q_BEFORE_THE_STORM}, {"MQ103", Q_BLEAK_FALLS_BARROW}, {"MQ104", Q_DRAGON_RISING},
		{"MQ105", Q_WAY_OF_THE_VOICE}, {"MQ106", Q_BLADE}, {"MQ201", Q_DIPLOMATIC}, {"MQ202", Q_RAT}, {"MQ203", Q_WALL},
		{"MQ204", Q_THROAT}};
	for (size_t i = 0; i < sizeof(Q) / sizeof(Q[0]); i++)
		if (!strcmp(Q[i].eid, eid))
			return Q[i].q;
	return -1;
}

static uint32_t hash_of(const char *s) { // (FNV-1a: an editor id kept in the save)
	uint32_t h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h ? h : 1;
}

void people_init(const char *pack_dir) {
	memset(people, 0, sizeof(people));
	count = actors_count() < MAX_PEOPLE ? actors_count() : MAX_PEOPLE;
	char path[256];
	snprintf(path, sizeof(path), "%sactors/combat.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	int read = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char eid[64];
		int flags, n = 0;
		float health;
		if (sscanf(line, "%63s %d %f%n", eid, &flags, &health, &n) < 3)
			continue;
		for (int i = 0; i < count; i++) {
			if (strcmp(actors_id(i), eid))
				continue;
			Person *p = &people[i];
			p->known = 1, p->flags = flags, p->health = p->max_health = health;
			int nq = 0;
			for (char *tok = line + n; nq < QUEST_FLAGS_MAX && *tok;) {
				char qe[32];
				int qf, used;
				if (sscanf(tok, " %31[^:]:%d%n", qe, &qf, &used) < 2)
					break;
				const int q = quest_of(qe);
				if (q >= 0)
					p->quest[nq] = q, p->quest_flags[nq++] = qf;
				tok += used;
			}
			for (; nq < QUEST_FLAGS_MAX; nq++)
				p->quest[nq] = -1;
			read++;
			break;
		}
	}
	free(buf);
	game_log("people: %d lives read", read);
}

void people_clear(void) {
	for (int i = 0; i < count; i++) {
		Person *p = &people[i];
		if (p->dead || p->down_t > 0.0f || p->flee_t > 0.0f)
			actors_script_release(i);
		p->health = p->max_health, p->dead = 0, p->flee_t = p->down_t = 0.0f, p->struck_once = 0;
	}
}

int people_dead(int actor) {
	return actor >= 0 && actor < count && people[actor].dead;
}

// Essential now: their own record's, or a running quest's alias
static int essential(const Person *p) {
	if (p->flags & P_ESSENTIAL)
		return 1;
	for (int k = 0; k < QUEST_FLAGS_MAX; k++)
		if (p->quest[k] >= 0 && (p->quest_flags[k] & P_ESSENTIAL) && quests_stage(p->quest[k]) > 0 &&
		    quests_stage(p->quest[k]) < QUEST_DONE)
			return 1;
	return 0;
}

// Someone you can strike: alive, in the world, not in a scene or following you
static int strikable(int i, float pos[3]) {
	const Person *p = &people[i];
	return p->known && !p->dead && p->down_t <= 0.0f && actors_shown(i, pos) && (!actors_scripted(i) || p->flee_t > 0.0f);
}

// The harm landed: assault the first time (they run), murder if it kills them - or, essential,
// down on their knees
static void struck(int i, float damage) {
	Person *p = &people[i];
	float at[3];
	actors_where(i, at);
	p->health -= damage;
	const int hold = crime_actor_hold(i);
	if (p->health <= 0.0f && essential(p)) {
		p->health = 0.0f, p->down_t = BLEEDOUT_S, p->flee_t = 0.0f;
		actors_script(i, 1);
		actors_script_clip(i, "bleedout", 0);
		game_log("people: %s is down (essential)", actors_id(i));
		return;
	}
	if (p->health <= 0.0f) { // (protected or not: by your hand they die)
		p->dead = 1, p->dead_place = last_place, p->flee_t = 0.0f;
		memcpy(p->dead_at, at, sizeof(at));
		actors_script(i, 1);
		actors_script_clip(i, "death", 1);
		crime_commit(CRIME_MURDER, 0, at, -1);
		game_log("people: %s killed", actors_id(i));
		return;
	}
	if (!p->struck_once) {
		p->struck_once = 1;
		crime_commit(CRIME_ASSAULT, 0, at, hold); // (they saw who struck them)
	}
	if (p->flee_t <= 0.0f) { // (away from you, at a run)
		actors_script(i, 1);
		p->flee_t = FLEE_S;
	}
	if (damage >= 1.0f) // (a stream's frame by frame: not each)
		game_log("people: %s struck for %.0f (%.0f left)", actors_id(i), damage, p->health);
}

int people_poisoned(int actor, float damage) {
	float pos[3];
	if (actor < 0 || actor >= count || !strikable(actor, pos))
		return 0;
	Person *p = &people[actor];
	p->health -= damage; // (unseen: no flight, no crime - unless it kills them in front of others: not modelled)
	game_log("people: %s poisoned (%.0f left)", actors_id(actor), p->health);
	if (p->health <= 0.0f && !essential(p)) {
		p->dead = 1, p->dead_place = last_place, p->flee_t = 0.0f;
		memcpy(p->dead_at, pos, sizeof(pos));
		actors_script(actor, 1);
		actors_script_clip(actor, "death", 1);
	} else if (p->health <= 0.0f) {
		p->health = 1.0f;
	}
	return 1;
}

int people_test_strike(int actor, float damage) { // (a test's: a blow on them, wherever you are)
	float pos[3];
	if (actor < 0 || actor >= count || !strikable(actor, pos))
		return 0;
	struck(actor, damage);
	return 1;
}

int people_hit(const float eye[3], const float dir[3], float reach, float cone_cos, float damage) {
	int best = -1;
	float best_d = 1e9f;
	for (int i = 0; i < count; i++) {
		float pos[3];
		if (!strikable(i, pos))
			continue;
		const float d[3] = {pos[0] - eye[0], pos[1] + PERSON_MID_M - eye[1], pos[2] - eye[2]};
		const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), flat = hypotf(d[0], d[2]);
		if (len > reach + PERSON_RADIUS_M + 0.4f || len < 0.01f)
			continue;
		const float along = (d[0] * dir[0] + d[2] * dir[2]) / (fmaxf(flat, 0.01f) * fmaxf(hypotf(dir[0], dir[2]), 0.01f));
		if (along < cone_cos && flat > PERSON_RADIUS_M)
			continue;
		if (len < best_d)
			best = i, best_d = len;
	}
	if (best < 0)
		return 0;
	struck(best, damage);
	return 1;
}

float people_ray(const float eye[3], const float dir[3], float reach, float radius, float damage) {
	int best = -1;
	float best_t = reach;
	for (int i = 0; i < count; i++) {
		float pos[3];
		if (!strikable(i, pos))
			continue;
		const float r = PERSON_RADIUS_M + radius;
		for (int s = 0; s < 3; s++) { // (the body as spheres up an upright capsule, as creatures')
			const float d[3] = {pos[0] - eye[0], pos[1] + 0.45f + 0.45f * s - eye[1], pos[2] - eye[2]};
			const float along = d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2];
			if (along < 0.0f || along > best_t + r)
				continue;
			const float off2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - along * along;
			if (off2 < r * r) {
				const float t = fmaxf(0.0f, along - sqrtf(r * r - off2));
				if (t < best_t)
					best = i, best_t = t;
			}
		}
	}
	if (best < 0)
		return -1.0f;
	if (damage > 0.0f)
		struck(best, damage);
	return best_t;
}

int people_area(const float pos[3], float radius, float damage) {
	int n = 0;
	for (int i = 0; i < count; i++) {
		float at[3];
		if (strikable(i, at) && hypotf(at[0] - pos[0], at[2] - pos[2]) < radius + PERSON_RADIUS_M &&
		    fabsf(at[1] + 0.9f - pos[1]) < radius + 1.0f)
			struck(i, damage), n++;
	}
	return n;
}

void people_update(float dt, int place) {
	const int moved = place != last_place;
	last_place = place;
	for (int i = 0; i < count; i++) {
		Person *p = &people[i];
		if (p->dead) { // (lying where they fell while you're there; gone from anywhere else)
			if (moved) {
				const float off[3] = {p->dead_at[0], OFFSTAGE_Y, p->dead_at[2]};
				actors_script(i, 1);
				actors_script_place(i, p->dead_place == place ? p->dead_at : off, p->dead_heading);
				actors_script_clip(i, "death", 1);
			}
			continue;
		}
		if (p->down_t > 0.0f && (p->down_t -= dt) <= 0.0f) { // (up again, at half)
			p->health = p->max_health * 0.5f;
			actors_script_release(i);
			game_log("people: %s is up again", actors_id(i));
		}
		if (p->flee_t > 0.0f) {
			float at[3];
			if (actors_where(i, at) && !actors_script_walking(i)) { // (a run away over their floor - else cowering where they are)
				const float a0 = (float)rand() / (float)RAND_MAX * 6.2831853f;
				int went = 0;
				for (int k = 0; k < 8 && !went; k++) {
					const float a = a0 + k * 0.785f;
					NavRef nr;
					float h;
					const float px = at[0] + cosf(a) * FLEE_M, pz = at[2] + sinf(a) * FLEE_M;
					if (navmesh_locate_level(px, at[1] + 0.5f, pz, 1.5f, &nr, &h)) {
						const float to[3] = {px, h, pz};
						actors_script_walk(i, to, 1);
						went = 1;
					}
				}
				if (!went)
					actors_script_clip(i, "cower", 0);
			}
			if ((p->flee_t -= dt) <= 0.0f || moved)
				p->flee_t = 0.0f, actors_script_release(i);
		}
		if (!p->dead && p->down_t <= 0.0f && p->health < p->max_health) // (mending: Skyrim's people heal out of a fight)
			p->health = fminf(p->max_health, p->health + p->max_health * 0.02f * dt);
	}
}

void people_save(uint32_t dead[PEOPLE_DEAD_MAX]) {
	memset(dead, 0, PEOPLE_DEAD_MAX * sizeof(uint32_t));
	int n = 0;
	for (int i = 0; i < count && n < PEOPLE_DEAD_MAX; i++)
		if (people[i].dead)
			dead[n++] = hash_of(actors_id(i));
}

void people_load(const uint32_t dead[PEOPLE_DEAD_MAX]) {
	people_clear();
	for (int i = 0; i < count; i++) {
		const uint32_t h = hash_of(actors_id(i));
		for (int k = 0; k < PEOPLE_DEAD_MAX && dead[k]; k++)
			if (dead[k] == h) { // (dead before: gone - the body long since taken away)
				people[i].dead = 1, people[i].dead_place = -12345;
				float at[3] = {0.0f, 0.0f, 0.0f};
				actors_where(i, at);
				memcpy(people[i].dead_at, at, sizeof(at));
			}
	}
	last_place = -999;
}
