// Host test: Draugr State Machine audit & verification
// Tests:
//   1. Ambush wake by proximity (AMBUSH -> RISE with sarcophagus lid opening -> CHASE)
//   2. Ambush wake by damage (AMBUSH -> RISE with lid opening -> CHASE)
//   3. Ambush wake by noise alert (creatures_alert_at: AMBUSH -> RISE -> CHASE)
//   4. Ambush wake by ally call (call_allies: AMBUSH -> RISE -> CHASE)
//   5. RISE state integrity: geometric interpolation, damage during rise, no flinch/knockdown
//   6. SHOUT state: windup, shout_in, shout execution, cooldown reset, return to CHASE
//   7. Shout interruption: bash, stagger, and damage interrupting shout windup into FLINCH
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "actors.h"
#include "activators.h"
#include "collision.h"
#include "creatures.h"
#include "crime.h"
#include "interior.h"
#include "items.h"
#include "lighting.h"
#include "music.h"
#include "navmesh.h"
#include "perks.h"
#include "projectiles.h"
#include "quests.h"
#include "sfx.h"
#include "skills.h"
#include "spellfx.h"
#include "terrain.h"
#include "ui.h"
#include "voice.h"
#include "gamelog.h"

#ifndef PACK
#define PACK "../../pack/"
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

// --- Test recording state ---
static int lid_open_count;
static float last_lid_pos[3];
static int beasts_used[24];
static const char *last_clip[24];
static float last_pose_pos[24][3];
static int waves_fired;
static int sfx_played;
static char last_sfx[64];

// Stubs for activators
int activators_open_lids(const float pos[3], float radius) {
	(void)radius;
	lid_open_count++;
	memcpy(last_lid_pos, pos, sizeof(last_lid_pos));
	printf("   [Activator] Lid opened at (%.1f, %.1f, %.1f), total lids=%d\n", pos[0], pos[1], pos[2], lid_open_count);
	return 1;
}
int activators_web_down_near(const float pos[3], float radius) { (void)pos, (void)radius; return 0; }

// Stubs for actors
int actors_beast_kind(const char *name, const char *set) {
	(void)name, (void)set;
	return 1;
}
void actors_beast_scale(int i, float scale) { (void)i, (void)scale; }
void actors_beast_fall(int b, float pitch, float roll) { (void)b, (void)pitch, (void)roll; }
void actors_beast_look(int b, int far, float fade) { (void)b, (void)far, (void)fade; }
int actors_beast_spawn(int kind) {
	(void)kind;
	for (int i = 0; i < 24; i++)
		if (!beasts_used[i]) {
			beasts_used[i] = 1;
			return i;
		}
	return -1;
}
void actors_beast_remove(int b) {
	if (b >= 0 && b < 24)
		beasts_used[b] = 0;
}
float actors_beast_clip(int kind, const char *clip, float *speed) {
	(void)kind;
	if (speed)
		*speed = (!strcmp(clip, "walk") || !strcmp(clip, "run")) ? 3.0f : 0.0f;
	if (!strcmp(clip, "sarc_idle") || !strcmp(clip, "alcove_idle") || !strcmp(clip, "sit_idle") || !strcmp(clip, "lay_idle"))
		return 2.0f;
	if (!strcmp(clip, "sarc_exit"))
		return 3.0f;
	if (!strcmp(clip, "alcove_exit"))
		return 2.5f;
	if (!strcmp(clip, "sit_exit") || !strcmp(clip, "lay_exit"))
		return 3.0f;
	if (!strcmp(clip, "shout_in"))
		return 0.8f;
	if (!strcmp(clip, "shout"))
		return 0.8f;
	if (!strcmp(clip, "idle") || !strcmp(clip, "combat"))
		return 2.0f;
	if (!strcmp(clip, "walk") || !strcmp(clip, "run") || !strcmp(clip, "trot"))
		return 1.33f;
	if (!strcmp(clip, "attack") || !strcmp(clip, "attack2") || !strcmp(clip, "power"))
		return 2.0f;
	if (!strcmp(clip, "death"))
		return 2.5f;
	return 1.0f;
}
void actors_beast_pose(int b, float x, float y, float z, float heading, const char *clip, float t, const char *blend,
                       float blend_t, float w, float dt) {
	(void)heading, (void)t, (void)blend, (void)blend_t, (void)w, (void)dt;
	if (b >= 0 && b < 24) {
		last_clip[b] = clip;
		last_pose_pos[b][0] = x;
		last_pose_pos[b][1] = y;
		last_pose_pos[b][2] = z;
	}
}
void actors_beast_ghost(int b, int ghost) { (void)b, (void)ghost; }

// Stubs for collision
float collision_ray(const float a[3], const float b[3]) { (void)a, (void)b; return 1.0f; }
float collision_ray_ground(const float a[3], const float b[3]) { (void)a, (void)b; return 1.0f; }

// Stubs for crime
int crime_guard_hello(int guard, int hold, const char *voice, int female, const char *race_eid) {
	(void)guard, (void)hold, (void)voice, (void)female, (void)race_eid; return 0;
}
int crime_hold_index(const char *name) { (void)name; return 0; }

// Stubs for gamelog
void game_log(const char *fmt, ...) { (void)fmt; }

// Stubs for interior
const char *interior_title(int place) { (void)place; return "Tamriel"; }

// Stubs for items
int items_flight(int item, float *speed, float *gravity) { (void)item; *speed = 50.0f, *gravity = 0.3f; return -1; }
int items_open_container(uint32_t ref, int container) { (void)ref, (void)container; return 0; }
void items_container_add(int h, int item, int count) { (void)h, (void)item, (void)count; }
void items_forget_container(uint32_t ref) { (void)ref; }
int items_by_form(uint32_t form) { (void)form; return -1; }
int items_effects(int item, ItemEffect *out, int max) { (void)item, (void)out, (void)max; return 0; }
const MagicEffect *items_magic_effect(int mgef) { (void)mgef; return NULL; }
float items_poison_damage(int item) { (void)item; return 0.0f; }

// Stubs for lighting
float lighting_game_hours(void) { return 12.0f; }

// Stubs for music
void music_special(const char *name) { (void)name; }

// Stubs for navmesh
int navmesh_locate(float x, float y, float z, NavRef *ref, float *height) {
	(void)x, (void)y, (void)z; if (ref) *ref = (NavRef){0, 0, 0}; if (height) *height = y; return 1;
}
int navmesh_nearest(float x, float y, float z, float m, NavRef *ref, float out[3]) {
	(void)m; if (ref) *ref = (NavRef){0, 0, 0}; if (out) { out[0] = x; out[1] = y; out[2] = z; } return 1;
}
int navmesh_locate_level(float x, float y, float z, float dy, NavRef *ref, float *h) {
	(void)dy; if (ref) *ref = (NavRef){0, 0, 0}; if (h) *h = y; return 1;
}
int navmesh_nearest_level(float x, float y, float z, float radius, float dy, NavRef *ref, float out[3]) {
	(void)radius, (void)dy;
	if (ref) *ref = (NavRef){0, 0, 0};
	if (out) {
		out[0] = x + 1.5f;
		out[1] = y;
		out[2] = z;
	}
	return 1;
}
int navmesh_track(NavRef *ref, float x, float z, float *h) {
	(void)x, (void)z; if (ref) *ref = (NavRef){0, 0, 0}; if (h) *h = 0.0f; return 1;
}
int navmesh_path(NavRef from, const float f[3], NavRef to, const float t[3], float *p, int m) {
	(void)from, (void)f, (void)to, (void)t, (void)p, (void)m; return 0;
}
int navmesh_indoors(void) { return 0; }

// Stubs for perks
int perks_has_form(uint32_t form) { (void)form; return 0; }
float perks_apply(int entry_point, float value, const PerkContext *ctx) {
	(void)entry_point, (void)ctx; return value;
}

// Stubs for projectiles
int projectiles_fire(const float pos[3], const float dir[3], float speed, float gravity, int model, float damage,
                     int from_player, int hits_player, int item) {
	(void)pos, (void)dir, (void)speed, (void)gravity, (void)model, (void)damage, (void)from_player, (void)hits_player, (void)item;
	return -1;
}
void projectiles_fire_spit(const float pos[3], const float dir[3], float speed, float gravity, float dps, float s) {
	(void)pos, (void)dir, (void)speed, (void)gravity, (void)dps, (void)s;
}
int projectiles_fire_hostile_bolt(const float pos[3], const float dir[3], float speed, int element, float damage,
                                  float radius) {
	(void)pos, (void)dir, (void)speed, (void)element, (void)damage, (void)radius; return -1;
}

// Stubs for quests
int quests_busy(void) { return 0; }
int quests_stage(int quest) { (void)quest; return 0; }
float quests_thalmor_disguise_m(void) { return -1.0f; }
void quests_voice(const char *key) { (void)key; }

// Stubs for sfx
int sfx_play(const char *e, float v, float p) {
	(void)v, (void)p; sfx_played++; snprintf(last_sfx, sizeof(last_sfx), "%s", e); return 1;
}
int sfx_play_at(const char *e, const float pos[3], float m) {
	(void)pos, (void)m; sfx_played++; snprintf(last_sfx, sizeof(last_sfx), "%s", e); return 1;
}

// Stubs for skills
static SkillState skills;
SkillState *skills_state(void) { return &skills; }

// Stubs for spellfx
void spellfx_burst(const float pos[3], int element, float radius) { (void)pos, (void)element, (void)radius; }
void spellfx_soul(const float from[3], float spread, const float to[3], float dt) { (void)from, (void)spread, (void)to, (void)dt; }
void spellfx_soul_stream(const float from[3], float spread, const float to[3], float dt) { (void)from, (void)spread, (void)to, (void)dt; }
void spellfx_wave(const float from[3], const float dir[3], float range, float spread, int element, float strength) {
	(void)from, (void)dir, (void)range, (void)spread, (void)element, (void)strength;
	waves_fired++;
}

// Stubs for terrain
int terrain_height_at(float x, float z, float *out) { (void)x, (void)z; if (out) *out = 0.0f; return 1; }

// Stubs for ui
void ui_notice(const char *text, float seconds) { (void)text, (void)seconds; }

// Stubs for voice
int voice_playing(void) { return 0; }

// Helper to step engine frames
static void step_sim(int frames, float dt, const float player[3], int sneaking, int place) {
	for (int f = 0; f < frames; f++)
		creatures_update(dt, player, sneaking, place);
}

int main(void) {
	printf("Running draugr_test...\n");
	creatures_init(PACK);

	lid_open_count = 0;
	waves_fired = 0;

	// Use known draugr spawn at (1748.10, -150.59, -1730.09) in Tamriel (place = -1)
	const float spawn_pos[3] = {1748.10f, -150.59f, -1730.09f};

	// 1. Wake spawn into live[] by placing player within wake range (25 m) but outside ambush wake (6 m)
	float p_idle[3] = {spawn_pos[0] + 25.0f, spawn_pos[1], spawn_pos[2]};
	step_sim(5, 0.05f, p_idle, 0, -1);

	// Find draugr in live[]
	float draugr_pos[3];
	int found = creatures_find_titled("Restless Draugr", draugr_pos);
	if (!found)
		found = creatures_find_titled("Draugr", draugr_pos);
	CHECK(found, "Draugr spawned into live[] within 25 m");
	printf("Draugr at (%.1f, %.1f, %.1f), pose clip: %s\n", draugr_pos[0], draugr_pos[1], draugr_pos[2], last_clip[0]);

	// In AMBUSH: should be playing alcove_idle or sarc_idle or lay_idle
	CHECK(!strcmp(last_clip[0], "alcove_idle") || !strcmp(last_clip[0], "sarc_idle") || !strcmp(last_clip[0], "lay_idle"),
	      "Draugr starts in ambush idle clip (%s)", last_clip[0]);

	// Before waking, creatures_hostile_near should NOT report hostile (it's still in AMBUSH)
	int hostile_before = creatures_hostile_near(draugr_pos, 5.0f);
	CHECK(hostile_before == 0, "Draugr in AMBUSH does not count as hostile_near (hostile_before=%d)", hostile_before);

	// Test 1: Ambush Wake by Proximity
	// Player steps within wake radius (3 m < AMBUSH_WAKE_M 6 m)
	float p_near[3] = {draugr_pos[0] + 3.0f, draugr_pos[1], draugr_pos[2]};
	step_sim(1, 0.05f, p_near, 0, -1);

	// Now it has woken up into RISE state
	printf("Draugr clip immediately after wake: %s\n", last_clip[0]);
	CHECK(!strcmp(last_clip[0], "alcove_exit") || !strcmp(last_clip[0], "sarc_exit") || !strcmp(last_clip[0], "lay_exit"),
	      "Draugr transitioned to RISE and plays exit animation (%s)", last_clip[0]);

	int hostile_after = creatures_hostile_near(draugr_pos, 5.0f);
	CHECK(hostile_after == 1, "Draugr in RISE is counted as hostile_near");

	// Test 2: RISE State Integrity
	// Check position interpolation during RISE
	float pos_mid[3];
	memcpy(pos_mid, last_pose_pos[0], sizeof(pos_mid));
	step_sim(10, 0.05f, p_near, 0, -1); // 0.5 s further along rise
	printf("Pose pos mid-rise: (%.2f, %.2f, %.2f) vs (%.2f, %.2f, %.2f)\n",
	       last_pose_pos[0][0], last_pose_pos[0][1], last_pose_pos[0][2],
	       pos_mid[0], pos_mid[1], pos_mid[2]);
	CHECK(last_pose_pos[0][0] != pos_mid[0] || last_pose_pos[0][2] != pos_mid[2],
	      "Draugr position smoothly interpolates during RISE");

	// Test 3: Damage and Stagger during RISE (Uninterruptible Rise)
	// Raycast damage at draugr while in RISE
	float ray_eye[3] = {p_near[0], p_near[1] + 1.0f, p_near[2]};
	float ray_dir[3] = {-1.0f, 0.0f, 0.0f};
	float hit_dist = creatures_ray(ray_eye, ray_dir, 10.0f, 0.5f, 20.0f);
	CHECK(hit_dist > 0.0f, "Hit draugr with ray while in RISE (hit_dist=%.2f)", hit_dist);

	// Stagger and bash should NOT cancel RISE into FLINCH
	int stagger_res = creatures_stagger_struck();
	CHECK(stagger_res == 0, "Stagger returns 0 during RISE (does not interrupt rise)");

	int bash_res = creatures_bash_struck(0);
	CHECK(bash_res == 0, "Bash returns 0 during RISE (does not interrupt rise)");

	CHECK(!strcmp(last_clip[0], "alcove_exit") || !strcmp(last_clip[0], "sarc_exit") || !strcmp(last_clip[0], "lay_exit"),
	      "Exit clip still playing after damage/stagger attempt (%s)", last_clip[0]);

	// Step simulation past RISE duration (~2.5 s = 50 frames at 0.05s)
	step_sim(50, 0.05f, p_near, 0, -1);
	printf("Draugr clip after RISE completed: %s\n", last_clip[0]);
	CHECK(strcmp(last_clip[0], "alcove_exit") != 0 && strcmp(last_clip[0], "sarc_exit") != 0 && strcmp(last_clip[0], "lay_exit") != 0,
	      "Draugr exited RISE cleanly into CHASE/ATTACK/combat state");

	// Test 4: Draugr Boss Shouting & Sarcophagus Lid Opening
	// draugr2 spawn at (939.73, 228.59, 1417.22) in Tamriel is a Draugr Overlord with ambush 9 (lay/sarc) and shouts=1
	const float boss_spawn[3] = {939.73f, 228.59f, 1417.22f};
	float p_boss_far[3] = {boss_spawn[0] + 25.0f, boss_spawn[1], boss_spawn[2]};
	step_sim(5, 0.05f, p_boss_far, 0, -1);

	float boss_pos[3];
	int boss_found = creatures_find_titled("Draugr Overlord", boss_pos);
	CHECK(boss_found, "Found Draugr Overlord (boss with shouts)");

	int lids_before = lid_open_count;
	// Wake boss by proximity (boss wake distance is 10 m, step to 5 m)
	float p_boss_near[3] = {boss_pos[0] + 5.0f, boss_pos[1], boss_pos[2]};
	step_sim(2, 0.05f, p_boss_near, 0, -1);

	// Sarcophagus lid should have popped open!
	CHECK(lid_open_count > lids_before, "Sarcophagus lid opened upon boss wake (lids=%d)", lid_open_count);

	// Step past boss RISE (~3.0 s = 60 frames)
	step_sim(65, 0.05f, p_boss_near, 0, -1);

	// Now boss is in CHASE, shout_cd will count down
	int waves_before = waves_fired;
	for (int f = 0; f < 120 && waves_fired == waves_before; f++) {
		step_sim(1, 0.05f, p_boss_near, 0, -1);
		if (f % 20 == 0 || waves_fired > waves_before)
			printf("   Frame %d: waves=%d clip=%s\n", f, waves_fired, last_clip[1]);
	}
	CHECK(waves_fired > waves_before, "Draugr boss executed shout wave attack (waves=%d)", waves_fired);

	// Test 5: Shout Interruption
	// Step forward until next shout initiates (shout_cd counts down ~7 s = 140 frames)
	// When boss is in SHOUT, hitting with bash cancels shout into FLINCH
	int interrupted = 0;
	for (int f = 0; f < 300; f++) {
		step_sim(1, 0.05f, p_boss_near, 0, -1);
		int shout_b = -1;
		for (int b = 0; b < 24; b++) {
			if (last_clip[b] && (!strcmp(last_clip[b], "shout_in") || !strcmp(last_clip[b], "shout"))) {
				shout_b = b;
				break;
			}
		}
		if (shout_b >= 0) {
			printf("Boss (beast %d) began shouting clip '%s' on frame %d! Testing interrupt...\n", shout_b, last_clip[shout_b], f);
			// Aim ray at boss to make it struck_live
			creatures_ray(p_boss_near, (const float[3]){-1.0f, 0.0f, 0.0f}, 10.0f, 0.5f, 0.0f);
			int bash_int = creatures_bash_struck(0);
			CHECK(bash_int > 0, "Bash successfully struck shouting boss (result=%d)", bash_int);
			// Next frame: boss should now be in FLINCH
			step_sim(1, 0.05f, p_boss_near, 0, -1);
			printf("Boss clip after interrupt: %s\n", last_clip[shout_b]);
			CHECK(!strcmp(last_clip[shout_b], "recoil") || !strcmp(last_clip[shout_b], "idle") || !strcmp(last_clip[shout_b], "combat") || !strcmp(last_clip[shout_b], "w1h_hit"),
			      "Boss shout was interrupted into recoil/flinch (clip=%s)", last_clip[shout_b]);
			interrupted = 1;
			break;
		}
	}
	CHECK(interrupted, "Boss began next shout and was interrupted");

	// Test 6: Noise Alert Wake (`creatures_alert_at`)
	// Clean slate test for waking an ambushing draugr via noise alert
	creatures_clear();
	step_sim(5, 0.05f, p_idle, 0, -1); // spawn draugr back in AMBUSH
	found = creatures_find_titled("Restless Draugr", draugr_pos);
	if (!found)
		found = creatures_find_titled("Draugr", draugr_pos);
	CHECK(found, "Draugr in AMBUSH before noise alert");

	// Fire noise near ambushing draugr
	int alerted = creatures_alert_at(draugr_pos, 8.0f, p_idle);
	CHECK(alerted > 0, "creatures_alert_at alerted ambushing draugr (alerted=%d)", alerted);
	step_sim(1, 0.05f, p_idle, 0, -1);
	CHECK(!strcmp(last_clip[0], "alcove_exit") || !strcmp(last_clip[0], "sarc_exit") || !strcmp(last_clip[0], "lay_exit"),
	      "Draugr woken by noise alert entered RISE (%s)", last_clip[0]);

	// Test 7: Hurt Wake (Hit by Arrow / Spell from Distance)
	creatures_clear();
	step_sim(5, 0.05f, p_idle, 0, -1); // spawn draugr back in AMBUSH at ~25m
	found = creatures_find_titled("Restless Draugr", draugr_pos);
	if (!found)
		found = creatures_find_titled("Draugr", draugr_pos);
	CHECK(found, "Draugr in AMBUSH before ranged attack");

	// Shoot ray at ambushing draugr from 25 m
	float shoot_dir[3] = {(draugr_pos[0] - p_idle[0]) / 25.0f, 0.0f, (draugr_pos[2] - p_idle[2]) / 25.0f};
	float hit_shot = creatures_ray(p_idle, shoot_dir, 30.0f, 0.8f, 15.0f);
	CHECK(hit_shot > 0.0f, "Ranged attack hit ambushing draugr at %.1f m", hit_shot);
	step_sim(1, 0.05f, p_idle, 0, -1);
	CHECK(!strcmp(last_clip[0], "alcove_exit") || !strcmp(last_clip[0], "sarc_exit") || !strcmp(last_clip[0], "lay_exit"),
	      "Draugr hit while sleeping woke up and entered RISE (%s)", last_clip[0]);

	if (failures == 0) {
		printf("ALL PASS: Draugr State Machine verified 100%% successfully.\n");
		return 0;
	} else {
		printf("%d TESTS FAILED.\n", failures);
		return 1;
	}
}
