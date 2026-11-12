#include "followers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "creatures.h"
#include "fileio.h"
#include "gamelog.h"
#include "items.h"
#include "navmesh.h"
#include "terrain.h"
#include "sfx.h"
#include "talk.h"
#include "ui.h"
#include "voice.h"

#define MAX_FOLLOWERS 64 // (Skyrim's potential followers in the converted area: 36 now - it held 32, the rest dropped)
#define MAX_LINES 1024 // (the whole map: 483)
#define BESIDE_M 1.4f        // brought through a door with you: this far to your side
#define LOST_M 45.0f         // further than this (a fast travel, a fall): beside you again
#define GONE_HOME_M 30.0f    // parted with: off home once this far from you (or you're elsewhere)
#define FIGHT_NEAR_M 14.0f   // a fight this near you: they're in it
#define STRIKE_S 1.6f        // a blow this often (the opening's Ralof and Hadvar fight so)...
#define STRIKE_REACH_M 2.8f  // ...at whoever is this near them...
#define STRIKE_DAMAGE 14.0f  // ...this hard
#define OFFSTAGE_Y -1000.0f  // (waiting somewhere else: kept out of sight)
#define ARRIVE_WAIT_S 0.6f   // through a door: placed once you've settled (out of High Hrothgar she was put at its height - in the air)
#define CATCH_UP_M 6.0f      // fallen far behind: brought up this far behind you, out of your view - not popped in at your side
#define ENGAGE_M 12.0f       // a foe this near them: they go for it
#define FOLLOWER_HEALTH 150.0f // a follower's health (Skyrim's scale with their level; a seasoned housecarl's)
#define BLEEDOUT_S 6.0f      // brought down (Skyrim's protected follower: your enemies can't kill them) - on
                             //  their knees till the fight's over and this long after, then up at half health
#define MEND_PER_S 2.0f      // out of a fight: their health back this fast

enum { T_RECRUIT, T_HIRE, T_SHORT, T_FOLLOW, T_WAIT, T_TRADE, T_DISMISS, T_COUNT };
static const char *const TOPIC_NAMES[T_COUNT] = {"recruit", "hire", "short", "follow", "wait", "trade", "dismiss"};

typedef struct {
	char id[48];
	int hireling, cost;
	int actor; // (actors.h's index once resolved; -1 not here)
} Follower;

typedef struct {
	int who, topic, part;
	char path[64], text[200];
} Line;

static Follower fol[MAX_FOLLOWERS];
static int fol_count, resolved;
static Line lines[MAX_LINES];
static int line_count;
static char prompts[T_COUNT][96];
static int current = -1;         // (fol index)
static int waiting, wait_place;  // told to wait: where
static float wait_at[3];
static uint64_t friends, paid;   // (bits by fol index: a friend now; a sellsword's fee paid)
#define BIT(f) ((uint64_t)1 << (f))
static int leaving = -1;         // parted with, still in sight (fol index)
static int last_place = -999;
static float strike_t, swing_left;
static int opt_kind[8], opt_n;
static unsigned rng = 0x9e3779b9u;

static int split(char *line, char **f, int max) {
	int n = 0;
	f[n++] = line;
	for (char *p = line; *p && n < max; p++)
		if (*p == '|')
			*p = 0, f[n++] = p + 1;
	return n;
}

static int topic_of(const char *name) {
	for (int t = 0; t < T_COUNT; t++)
		if (!strcmp(TOPIC_NAMES[t], name))
			return t;
	return -1;
}

static int find(const char *id) {
	for (int i = 0; i < fol_count; i++)
		if (!strcmp(fol[i].id, id))
			return i;
	return -1;
}

static char pack_path[128];

void followers_init(const char *pack_dir) {
	snprintf(pack_path, sizeof(pack_path), "%s", pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%sfollowers.txt", pack_dir);
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
		if (f[0][0] == 'F' && n >= 4 && fol_count < MAX_FOLLOWERS) {
			Follower *w = &fol[fol_count++];
			snprintf(w->id, sizeof(w->id), "%s", f[1]);
			w->hireling = atoi(f[2]), w->cost = atoi(f[3]), w->actor = -1;
		} else if (f[0][0] == 'P' && n >= 3 && topic_of(f[1]) >= 0) {
			snprintf(prompts[topic_of(f[1])], sizeof(prompts[0]), "%s", f[2]);
		} else if (f[0][0] == 'L' && n >= 6 && line_count < MAX_LINES && find(f[1]) >= 0 && topic_of(f[2]) >= 0) {
			Line *l = &lines[line_count++];
			l->who = find(f[1]), l->topic = topic_of(f[2]), l->part = atoi(f[3]);
			snprintf(l->path, sizeof(l->path), "%s", strcmp(f[4], "-") ? f[4] : "");
			snprintf(l->text, sizeof(l->text), "%s", f[5]);
		}
	}
	free(buf);
	game_log("followers: %d who could follow you, %d lines", fol_count, line_count);
}

static void resolve(void) {
	if (resolved == actors_count())
		return;
	resolved = actors_count();
	for (int i = 0; i < fol_count; i++) {
		fol[i].actor = -1;
		for (int a = 0; a < actors_count(); a++)
			if (!strcmp(actors_id(a), fol[i].id))
				fol[i].actor = a;
	}
}

static int of_actor(int actor) {
	resolve();
	for (int i = 0; i < fol_count; i++)
		if (fol[i].actor == actor && actor >= 0)
			return i;
	return -1;
}

void followers_clear(void) {
	if (current >= 0)
		actors_script_release(fol[current].actor);
	current = -1, waiting = 0, friends = paid = 0, leaving = -1;
}

int followers_is(int actor) {
	return of_actor(actor) >= 0;
}

int followers_current(void) {
	return current >= 0 ? fol[current].actor : -1;
}

int followers_is_friend(int actor) {
	const int f = of_actor(actor);
	return f >= 0 && ((friends >> f) & 1u);
}

void followers_befriend(int actor) {
	const int f = of_actor(actor);
	if (f >= 0)
		friends |= BIT(f);
}

// Their line for a topic (their own, else the shared one), its parts together; voiced
static void say(int f, int topic) {
	int pick[64], n = 0;
	for (int i = 0; i < line_count && n < 64; i++)
		if (lines[i].who == f && lines[i].topic == topic && !lines[i].part)
			pick[n++] = i;
	if (!n)
		return;
	rng = rng * 1664525u + 1013904223u;
	const int first = pick[(rng >> 8) % n];
	char text[400] = "";
	for (int i = first; i < line_count && (i == first || lines[i].part); i++)
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s", i == first ? "" : " ", lines[i].text);
	if (lines[first].path[0])
		voice_play(lines[first].path);
	ui_subtitle(talk_name(fol[f].actor), text);
}

int followers_topics(int actor, const char **opts, int max) {
	const int f = of_actor(actor);
	opt_n = 0;
	if (f < 0)
		return 0;
	static char hire_text[96];
	int kinds[4], k = 0;
	if (f == current) {
		kinds[k++] = waiting ? T_FOLLOW : T_WAIT;
		kinds[k++] = T_TRADE;
		kinds[k++] = T_DISMISS;
	} else if (current < 0 && fol[f].hireling && !(paid & BIT(f))) {
		kinds[k++] = T_HIRE;
	} else if (current < 0 && ((friends | paid) & BIT(f))) {
		kinds[k++] = T_RECRUIT;
	}
	for (int i = 0; i < k && opt_n < max && opt_n < 8; i++) {
		const char *p = prompts[kinds[i]];
		if (kinds[i] == T_HIRE) {
			snprintf(hire_text, sizeof(hire_text), "%s", p[0] ? p : "Consider yourself hired.");
			p = hire_text;
		}
		opts[opt_n] = p[0] ? p : TOPIC_NAMES[kinds[i]];
		opt_kind[opt_n++] = kinds[i];
	}
	return opt_n;
}

static void start_following(int f) {
	if (leaving == f)
		leaving = -1;
	current = f, waiting = 0;
	actors_script(fol[f].actor, 1);
	actors_script_follow(fol[f].actor, 1);
	game_log("followers: %s follows you", fol[f].id);
}

void followers_say(int actor, int k, int *trade_handle) {
	*trade_handle = -1;
	const int f = of_actor(actor);
	if (f < 0 || k < 0 || k >= opt_n)
		return;
	switch (opt_kind[k]) {
	case T_HIRE:
		if (items_gold() < fol[f].cost) {
			say(f, T_SHORT);
			ui_notice("You don't have enough gold", 3.0f);
			return;
		}
		items_add(items_find_gold(), -fol[f].cost);
		sfx_play("itm_gold", 0.7f, 0.0f);
		paid |= BIT(f);
		say(f, T_HIRE);
		start_following(f);
		break;
	case T_RECRUIT:
		say(f, T_RECRUIT);
		start_following(f);
		break;
	case T_FOLLOW:
		say(f, T_FOLLOW);
		waiting = 0;
		actors_script_follow(actor, 1);
		break;
	case T_WAIT: {
		say(f, T_WAIT);
		waiting = 1, wait_place = last_place;
		actors_script_follow(actor, 0);
		actors_where(actor, wait_at);
		break;
	}
	case T_TRADE: {
		say(f, T_TRADE);
		const int pockets = items_npc_pockets(fol[f].id);
		*trade_handle = items_open_container(0x80000000u | (uint32_t)actor, pockets);
		break;
	}
	case T_DISMISS:
		say(f, T_DISMISS);
		actors_script_follow(actor, 0);
		paid &= ~BIT(f); // (a sellsword's hired again next time)
		leaving = f, current = -1, waiting = 0;
		game_log("followers: %s parts with you", fol[f].id);
		break;
	}
}

// A spot onto the floor there (the navmesh's, else the land's): not left in the air
static void ground(float p[3]) {
	NavRef nr;
	float h;
	if (navmesh_locate_level(p[0], p[1], p[2], 6.0f, &nr, &h))
		p[1] = h;
	else if (terrain_height_at(p[0], p[2], &h) && h > -1e5f)
		p[1] = h;
}

void followers_update(float dt, const float feet[3], float yaw_deg, int place) {
	const int moved = place != last_place;
	last_place = place;
	if (leaving >= 0) { // (parted with: home once out of sight)
		float at[3];
		const int a = fol[leaving].actor;
		if (moved || !actors_where(a, at) || hypotf(at[0] - feet[0], at[2] - feet[2]) > GONE_HOME_M)
			actors_script_release(a), leaving = -1;
	}
	if (current < 0 || waiting) {
		creatures_set_friend(0, feet, 0);
		if (current < 0)
			return;
	}
	const int a = fol[current].actor;
	const float yr = (yaw_deg + 120.0f) * (float)M_PI / 180.0f; // (behind you, to one side)
	const float beside[3] = {feet[0] + sinf(yr) * BESIDE_M, feet[1], feet[2] - cosf(yr) * BESIDE_M};
	if (waiting) { // (where they were told to wait - out of sight while you're somewhere else)
		if (moved) {
			const float off[3] = {wait_at[0], OFFSTAGE_Y, wait_at[2]};
			actors_script_place(a, place == wait_place ? wait_at : off, 0.0f);
		}
		return;
	}
	static float arrive_t = -1.0f;
	if (moved) { // (out of sight till then: not left standing where she was, in the new place's space)
		const float off[3] = {feet[0], OFFSTAGE_Y, feet[2]};
		actors_script_place(a, off, 0.0f);
		arrive_t = ARRIVE_WAIT_S;
	}
	if (arrive_t > 0.0f) { // (through the door with you: beside you once you're where you're going, on its floor)
		if ((arrive_t -= dt) > 0.0f)
			return;
		arrive_t = -1.0f;
		float spot[3] = {beside[0], beside[1], beside[2]};
		ground(spot);
		actors_script_place(a, spot, yaw_deg * (float)M_PI / 180.0f);
		actors_script_follow(a, 1);
		return;
	}
	float at[3];
	if (!actors_where(a, at))
		return;
	if (hypotf(at[0] - feet[0], at[2] - feet[2]) > LOST_M || fabsf(at[1] - feet[1]) > 20.0f) {
		// (left far behind, or off in the air: caught up behind you, where you aren't looking)
		const float br = yaw_deg * (float)M_PI / 180.0f;
		float spot[3] = {feet[0] - sinf(br) * CATCH_UP_M, feet[1], feet[2] + cosf(br) * CATCH_UP_M};
		ground(spot);
		actors_script_place(a, spot, br);
		actors_script_follow(a, 1);
		return;
	}
	// Their health: the harm your enemies do them (creatures.c's friend 0); brought down, they
	// kneel in bleedout - Skyrim's protected followers - and get up once the fight's done
	static float health = FOLLOWER_HEALTH, down_t;
	const int fight = creatures_hostile_near(feet, FIGHT_NEAR_M);
	creatures_set_friend(0, at, down_t <= 0.0f);
	health -= creatures_take_friend_damage(0);
	if (down_t <= 0.0f && health <= 0.0f) {
		health = 0.0f, down_t = BLEEDOUT_S;
		actors_script_clip(a, "bleedout", 0);
		game_log("followers: %s is down (bleedout)", actors_id(a));
	}
	if (down_t > 0.0f) {
		if (!fight && (down_t -= dt) <= 0.0f) {
			health = FOLLOWER_HEALTH * 0.5f;
			actors_script_clip(a, "", 0);
			actors_script_follow(a, 1);
			game_log("followers: %s is up again", actors_id(a));
		}
		return;
	}
	if (!fight)
		health = fminf(FOLLOWER_HEALTH, health + MEND_PER_S * dt);
	if (swing_left > 0.0f && (swing_left -= dt) <= 0.0f)
		actors_script_clip(a, "", 0); // (the blow struck: walking with you again)
	// A fight about you: into it - to the nearest foe, striking it (Skyrim's follower), then back
	// at your side (she stood by you, swinging only at what came to her)
	static int engaged;
	if (creatures_hostile_near(feet, FIGHT_NEAR_M)) {
		const float eye[3] = {at[0], at[1] + 1.4f, at[2]}, ahead[3] = {0.0f, 0.0f, -1.0f};
		float foe[3];
		if (creatures_aim_assist(eye, ahead, -1.0f, ENGAGE_M, 0.0f, foe) &&
		    hypotf(foe[0] - at[0], foe[2] - at[2]) > STRIKE_REACH_M) {
			const float to[3] = {foe[0], at[1], foe[2]};
			actors_script_walk(a, to, 1);
			engaged = 1;
		}
	} else if (engaged) {
		actors_script_follow(a, 1);
		engaged = 0;
	}
	if (creatures_hostile_near(feet, FIGHT_NEAR_M) && (strike_t -= dt) <= 0.0f) {
		strike_t = STRIKE_S;
		const float eye[3] = {at[0], at[1] + 1.4f, at[2]}, ahead[3] = {0.0f, 0.0f, -1.0f};
		float foe[3];
		if (creatures_aim_assist(eye, ahead, -1.0f, STRIKE_REACH_M + 0.5f, 0.0f, foe)) {
			actors_script_face(a, atan2f(-(foe[0] - at[0]), -(foe[2] - at[2])));
			actors_script_clip(a, "w1h_power", 1);
			swing_left = 1.1f;
			creatures_blame_player(0);
			const int struck = creatures_area(at, STRIKE_REACH_M, STRIKE_DAMAGE);
			creatures_blame_player(1);
			if (struck > 0)
				sfx_play_at("wpn_hit_blade", at, 30.0f);
		}
	}
}

static uint32_t hash_of(const char *s) { // (FNV-1a of the editor id: kept in the save, whatever the list's order)
	uint32_t h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h ? h : 1;
}

static int of_hash(uint32_t h) {
	for (int i = 0; h && i < fol_count; i++)
		if (hash_of(fol[i].id) == h)
			return i;
	return -1;
}

void followers_save(FollowersSave *out) {
	memset(out, 0, sizeof(*out));
	out->now = current >= 0 ? hash_of(fol[current].id) : 0;
	out->waiting = (uint32_t)waiting;
	int nf = 0, np = 0;
	for (int i = 0; i < fol_count; i++) {
		if (((friends >> i) & 1u) && nf < FOLLOWERS_SAVED)
			out->friends[nf++] = hash_of(fol[i].id);
		if (((paid >> i) & 1u) && np < FOLLOWERS_SAVED / 2)
			out->paid[np++] = hash_of(fol[i].id);
	}
}

static void take_up(int now) { // (as loaded: the one with you at your side)
	if (current >= 0)
		actors_script_release(fol[current].actor);
	current = now, waiting = 0, leaving = -1;
	last_place = -999; // (beside you where the game's loaded)
	resolve();
	if (current >= 0 && fol[current].actor >= 0)
		start_following(current); // (one told to wait: with you again, as the game's loaded)
	else
		current = -1;
	if (current >= 0)
		game_log("followers: %s with you, as saved", fol[current].id);
}

void followers_load(const FollowersSave *in) {
	friends = paid = 0;
	for (int k = 0; k < FOLLOWERS_SAVED; k++) {
		const int f = of_hash(in->friends[k]), p = k < FOLLOWERS_SAVED / 2 ? of_hash(in->paid[k]) : -1;
		if (f >= 0)
			friends |= BIT(f);
		if (p >= 0)
			paid |= BIT(p);
	}
	take_up(of_hash(in->now));
}

// Saves before version 16 kept the follower as their line in followers.txt as it stood then: by
// the save's date, the list of that map (legacy/followers_eras.txt, tools/legacy_followers.py),
// else today's. A housecarl you can't have earned there (Lydia's the only one: Dragon Rising done)
// came from that misreading - the list one map back has who it was
static int era_lists(const char *date, char lists[2][64][48], int counts[2]) {
	char path[128];
	snprintf(path, sizeof(path), "%slegacy/followers_eras.txt", pack_path);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return 0;
	}
	buf[size] = 0;
	char eras[8][2048];
	char dates[8][24];
	int ne = 0;
	for (char *line = strtok(buf, "\n"); line && ne < 8; line = strtok(NULL, "\n")) {
		char *bar = strchr(line, '|');
		if (!bar)
			continue;
		*bar = 0;
		snprintf(dates[ne], sizeof(dates[ne]), "%s", line);
		snprintf(eras[ne++], sizeof(eras[0]), "%s", bar + 1);
	}
	free(buf);
	int at = ne; // (the era the save was made in: ne - today's list)
	for (int e = 0; e < ne; e++)
		if (strcmp(date, dates[e]) < 0) {
			at = e;
			break;
		}
	for (int w = 0; w < 2; w++) { // (0: its own era's list, 1: the one before)
		const int e = at - w;
		counts[w] = 0;
		if (e == ne) { // (today's)
			for (int i = 0; i < fol_count && i < 64; i++)
				snprintf(lists[w][counts[w]++], 48, "%s", fol[i].id);
		} else if (e >= 0) {
			char copy[2048];
			snprintf(copy, sizeof(copy), "%s", eras[e]);
			for (char *id = strtok(copy, ","); id && counts[w] < 64; id = strtok(NULL, ","))
				snprintf(lists[w][counts[w]++], 48, "%s", id);
		}
	}
	return 1;
}

static int earned(const char *id, int lydia_earned) {
	return strncmp(id, "housecarl", 9) || (lydia_earned && !strcmp(id, "housecarlwhiterun"));
}

static int old_number(int n, char lists[2][64][48], const int counts[2], int lydia_earned) {
	if (n < 0 || n >= counts[0])
		return -1;
	const char *id = lists[0][n];
	if (!earned(id, lydia_earned) && n < counts[1] && earned(lists[1][n], lydia_earned))
		id = lists[1][n];
	if (!earned(id, lydia_earned))
		return -1;
	return find(id);
}

void followers_load_old(const int32_t in[4], const char *save_date, int lydia_earned) {
	static char lists[2][64][48];
	int counts[2] = {0, 0};
	if (!era_lists(save_date ? save_date : "", lists, counts)) { // (no table: as they were read)
		counts[0] = 0;
		for (int i = 0; i < fol_count && i < 64; i++)
			snprintf(lists[0][counts[0]++], 48, "%s", fol[i].id);
	}
	friends = paid = 0;
	for (int b = 0; b < 32; b++) {
		const int f = old_number(b, lists, counts, lydia_earned);
		if (f >= 0 && (((uint32_t)in[2] >> b) & 1u))
			friends |= BIT(f);
		if (f >= 0 && (((uint32_t)in[3] >> b) & 1u))
			paid |= BIT(f);
	}
	const int now = old_number(in[0], lists, counts, lydia_earned);
	if (in[0] >= 0)
		game_log("followers: an old save's follower %d (made %s) is %s", in[0], save_date ? save_date : "?",
		         now >= 0 ? fol[now].id : "no one now");
	take_up(now);
}

void followers_assign(int actor) {
	const int f = of_actor(actor);
	if (f < 0)
		return;
	friends |= BIT(f);
	if (current >= 0 && current != f) { // (one at a time: the one with you goes home)
		actors_script_release(fol[current].actor);
		current = -1;
	}
	say(f, T_RECRUIT);
	start_following(f);
}
