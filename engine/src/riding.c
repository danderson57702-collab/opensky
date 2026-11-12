#include "riding.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "aim.h"
#include "fileio.h"
#include "gamelog.h"
#include "items.h"
#include "sfx.h"
#include "talk.h"
#include "topics.h"
#include "ui.h"
#include "gametext.h"

#define DRAW_M 150.0f       // the horse drawn within this of you
#define REACH_M 3.2f        // ...and mounted from this near
#define BESIDE_M 1.4f       // you get down this far to its side
#define TRAVEL_NEAR_M 60.0f // fast travel: it comes along if it was this near
#define SPRINT_FROM_MS 9.5f // (its gallop above this pace, a run above RUN_FROM)
#define RUN_FROM_MS 4.0f

static char kind_name[24], seller[48];
static int have_data, start_place;
static float start_pos[3], start_heading;
static int owned, mounted, place, beast = -1, beast_kind = -1;
static float pos[3], heading; // (where it is; heading in radians, the actors' convention)
static float gait_t;
static int reopen_actor = -1, offering;

void riding_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%shorses.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	char k[24], s[48];
	if (sscanf(buf, "H|%23[^|]|%47[^|]|%d|%f|%f|%f|%f", k, s, &start_place, &start_pos[0], &start_pos[1], &start_pos[2],
	           &start_heading) == 7) {
		snprintf(kind_name, sizeof(kind_name), "%s", k);
		snprintf(seller, sizeof(seller), "%s", s);
		have_data = 1;
	}
	free(buf);
	game_log("riding: %s", have_data ? "a horse at the stables" : "no horses");
}

static void unspawn(void) {
	if (beast >= 0)
		actors_beast_remove(beast), beast = -1;
}

void riding_clear(void) {
	unspawn();
	owned = mounted = 0, offering = 0, reopen_actor = -1;
	actors_player_mount(NULL, 0.0f);
}

int riding_mounted(void) {
	return mounted;
}

// --- Buying one -----------------------------------------------------------------------------

int riding_topics(int actor, const char **opts, int max) {
	if (!have_data || max < 1 || strcmp(actors_id(actor), seller))
		return 0;
	static char text[96];
	const char *p = offering && !owned ? topics_prompt("horse_bought") : topics_prompt("horse_buy");
	const char *tag = strstr(p, "<Global=");
	if (tag && strchr(tag, '>')) // (DIAL 00068CB6: Skyrim's HorseCost)
		snprintf(text, sizeof(text), "%.*s%d%s", (int)(tag - p), p, (int)topics_global("horsecost"), strchr(tag, '>') + 1);
	else
		snprintf(text, sizeof(text), "%s", p[0] ? p : T_DIAL_00068CBA_FULL0);
	opts[0] = text;
	return 1;
}

void riding_say(int actor, int k) {
	if (k != 0)
		return;
	if (owned) { // (INFO 00068CC0)
		topics_say("horse_sold", actor, talk_name(actor));
		return;
	}
	if (!offering) { // (INFO 00068CC2)
		topics_say("horse_buy", actor, talk_name(actor));
		offering = 1, reopen_actor = actor;
		return;
	}
	const int cost = (int)topics_global("horsecost");
	if (items_gold() < cost) { // (INFO 00068CF2)
		topics_say("horse_poor", actor, talk_name(actor));
		offering = 0;
		return;
	}
	items_add(items_find_gold(), -cost);
	sfx_play("itm_gold", 0.7f, 0.0f);
	topics_say("horse_bought", actor, talk_name(actor)); // (INFO 00068CF1)
	owned = 1, offering = 0, place = start_place, heading = start_heading;
	memcpy(pos, start_pos, sizeof(pos));
	game_log("riding: horse bought (%d gold), at place %d (%.1f %.1f %.1f)", cost, place, pos[0], pos[1], pos[2]);
}

int riding_take_reopen(void) {
	if (reopen_actor < 0 || topics_busy())
		return -1;
	const int a = reopen_actor;
	reopen_actor = -1;
	return a;
}

// --- Riding ---------------------------------------------------------------------------------

int riding_aim(const float eye[3], const float dir[3], float *along, const char **label) {
	if (!owned || mounted || beast < 0)
		return 0;
	const float body[3] = {pos[0], pos[1] + 1.3f, pos[2]};
	if (!aim_hits(eye, dir, body, 1.2f, REACH_M, along))
		return 0;
	*label = "Ride Horse";
	return 1;
}

int riding_mount(float feet[3], float *yaw_deg) {
	if (!owned || mounted)
		return 0;
	mounted = 1;
	memcpy(feet, pos, sizeof(pos));
	*yaw_deg = -heading * 180.0f / (float)M_PI; // (the view along its head)
	actors_player_mount("horse_idle", 0.0f);
	game_log("riding: in the saddle at %.1f %.1f %.1f", pos[0], pos[1], pos[2]);
	return 1;
}

void riding_dismount(float feet[3]) {
	if (!mounted)
		return;
	mounted = 0;
	// (down on its left: beside it, not in it)
	feet[0] = pos[0] - cosf(heading) * BESIDE_M, feet[1] = pos[1] + 0.1f, feet[2] = pos[2] + sinf(heading) * BESIDE_M;
	actors_player_mount(NULL, 0.0f);
	game_log("riding: down from the saddle at %.1f %.1f %.1f", pos[0], pos[1], pos[2]);
}

static const char *gait(float speed, const char *const names[4]) {
	return speed < 0.2f ? names[0] : speed < RUN_FROM_MS ? names[1] : speed < SPRINT_FROM_MS ? names[2] : names[3];
}

void riding_update(float dt, const float feet[3], float body_heading_deg, float speed, int now_place) {
	if (!owned)
		return;
	if (mounted) { // (under you: where you are, facing as your body does)
		memcpy(pos, feet, sizeof(pos));
		heading = -body_heading_deg * (float)M_PI / 180.0f, place = now_place; // (actors_player_heading_deg's: the view's way)
	}
	const int near = place == now_place && hypotf(pos[0] - feet[0], pos[2] - feet[2]) < DRAW_M;
	if (!near) {
		unspawn();
		return;
	}
	if (beast_kind < 0)
		beast_kind = actors_beast_kind(kind_name, "horse");
	if (beast < 0 && beast_kind >= 0)
		beast = actors_beast_spawn(beast_kind);
	if (beast < 0)
		return;
	static const char *const HORSE[4] = {"idle", "walk", "run", "sprint"};
	static const char *const RIDER[4] = {"horse_idle", "horse_walk", "horse_run", "horse_sprint"};
	const char *clip = gait(mounted ? speed : 0.0f, HORSE);
	float clip_speed = 0.0f;
	const float len = actors_beast_clip(beast_kind, clip, &clip_speed);
	if (len <= 0.0f && !strcmp(clip, "sprint")) // (no gallop of its own: its run)
		clip = "run";
	// (the hooves paced to the ground covered; the rider's clip at the same moment of the gait)
	gait_t += clip_speed > 0.5f && mounted ? dt * speed / clip_speed : dt;
	actors_beast_pose(beast, pos[0], pos[1], pos[2], heading, clip, gait_t, NULL, 0.0f, 0.0f, dt);
	if (mounted)
		actors_player_mount(gait(speed, RIDER), gait_t);
}

void riding_traveled(const float from[3], const float to[3], int now_place) {
	if (!owned || (!mounted && hypotf(pos[0] - from[0], pos[2] - from[2]) > TRAVEL_NEAR_M))
		return;
	memcpy(pos, to, sizeof(pos));
	if (!mounted)
		pos[0] += 2.0f; // (beside you)
	place = now_place;
	unspawn();
}

void riding_save(float out[6]) {
	out[0] = (float)owned, out[1] = (float)place, out[2] = pos[0], out[3] = pos[1], out[4] = pos[2], out[5] = heading;
}

void riding_load(const float in[6]) {
	unspawn();
	owned = in[0] > 0.5f, place = (int)in[1], pos[0] = in[2], pos[1] = in[3], pos[2] = in[4], heading = in[5];
	mounted = 0, offering = 0, reopen_actor = -1;
	actors_player_mount(NULL, 0.0f);
}
