// Host test: a dragon's fight (real pack/creatures/creatures.txt; the world flat ground, the
// clips Skyrim's lengths): flying in, circling, swooping and breathing, landing, fighting on
// the ground, crashing when badly hurt in the air, dying and its soul streaming into you.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "collision.h"
#include "creatures.h"
#include "items.h"
#include "lighting.h"
#include "navmesh.h"
#include "projectiles.h"
#include "sfx.h"
#include "skills.h"
#include "spellfx.h"
#include "terrain.h"

#ifndef PACK
#define PACK "../../pack/"
#endif

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

// --- the world, flat; the dragon's clips by Skyrim's lengths ---
static const char *last_clip[24];
static float pose_y, pose_pitch, pose_roll, pose_fade;
static int beasts_used[24], breaths, souls_fx, bursts;
int actors_beast_kind(const char *name, const char *set) { (void)set; return strncmp(name, "dragon", 6) ? 1 : 0; }
void actors_beast_scale(int i, float scale) { (void)i, (void)scale; }
void actors_beast_fall(int b, float pitch, float roll) { if (b == 0) pose_pitch = pitch, pose_roll = roll; }
void actors_beast_look(int b, int far, float fade) { (void)far; if (b == 0) pose_fade = fade; }
int actors_beast_spawn(int kind) { (void)kind; for (int i = 0; i < 24; i++) if (!beasts_used[i]) { beasts_used[i] = 1; return i; } return -1; }
void actors_beast_remove(int b) { if (b >= 0 && b < 24) beasts_used[b] = 0; }
float actors_beast_clip(int kind, const char *clip, float *speed) {
	(void)kind;
	static const struct { const char *n; float s; } len[] = {
		{"idle", 5.0f}, {"combat", 2.0f}, {"walk", 1.33f}, {"run", 1.33f}, {"attack", 3.0f}, {"attack2", 2.67f},
		{"power", 2.0f}, {"recoil", 3.0f}, {"death", 2.5f}, {"glide", 1.33f}, {"glide_l", 1.33f}, {"glide_r", 1.33f},
		{"glide_hurt", 1.33f}, {"flap", 1.0f}, {"hover", 1.0f}, {"takeoff", 4.0f}, {"land", 1.33f}, {"crash", 5.27f},
		{"crash_up", 3.73f}, {"roar", 4.0f}, {"air_hit", 3.0f}, {"inhale", 1.0f}, {"breathe", 3.0f},
		{"fly_inhale", 1.0f}, {"fly_breathe", 3.0f}, {"hover_inhale", 1.0f}, {"hover_breathe", 3.0f},
		{"turn_l", 1.33f}, {"turn_r", 1.33f}};
	if (speed)
		*speed = !strcmp(clip, "walk") ? 3.0f : 0.0f;
	for (unsigned i = 0; i < sizeof(len) / sizeof(len[0]); i++)
		if (!strcmp(len[i].n, clip))
			return len[i].s;
	return 1.0f;
}
void actors_beast_pose(int b, float x, float y, float z, float heading, const char *clip, float t, const char *blend,
                       float blend_t, float w, float dt) {
	(void)x, (void)z, (void)heading, (void)t, (void)blend, (void)blend_t, (void)w, (void)dt;
	if (b >= 0 && b < 24)
		last_clip[b] = clip;
	if (b == 0)
		pose_y = y;
}
float collision_ray(const float a[3], const float b[3]) { (void)a, (void)b; return 1.0f; }
float collision_ray_ground(const float a[3], const float b[3]) { (void)a, (void)b; return 1.0f; }
float lighting_game_hours(void) { return 0.0f; }
int items_flight(int item, float *speed, float *gravity) { (void)item; *speed = 50.0f, *gravity = 0.3f; return -1; }
int items_open_container(uint32_t ref, int container) { (void)ref, (void)container; return 0; }
void items_container_add(int h, int item, int count) { (void)h, (void)item, (void)count; }
void items_forget_container(uint32_t ref) { (void)ref; }
int items_by_form(uint32_t form) { (void)form; return -1; }
int items_effects(int item, ItemEffect *out, int max) { (void)item, (void)out, (void)max; return 0; }
const MagicEffect *items_magic_effect(int mgef) { (void)mgef; return NULL; }
float items_poison_damage(int item) { (void)item; return 0.0f; }
int navmesh_locate(float x, float y, float z, NavRef *ref, float *height) { (void)x, (void)y, (void)z, (void)ref, (void)height; return 0; }
int navmesh_nearest(float x, float y, float z, float m, NavRef *ref, float out[3]) { (void)x, (void)y, (void)z, (void)m, (void)ref, (void)out; return 0; }
int navmesh_locate_level(float x, float y, float z, float dy, NavRef *ref, float *h) { (void)x, (void)y, (void)z, (void)dy, (void)ref, (void)h; return 0; }
int navmesh_track(NavRef *ref, float x, float z, float *h) { (void)ref, (void)x, (void)z, (void)h; return 0; }
int navmesh_path(NavRef from, const float f[3], NavRef to, const float t[3], float *p, int m) { (void)from, (void)f, (void)to, (void)t, (void)p, (void)m; return 0; }
int projectiles_fire(const float pos[3], const float dir[3], float speed, float gravity, int model, float damage, int from_player, int hits_player, int item) {
	(void)pos, (void)dir, (void)speed, (void)gravity, (void)model, (void)damage, (void)from_player, (void)hits_player, (void)item; return -1;
}
void projectiles_fire_spit(const float pos[3], const float dir[3], float speed, float gravity, float dps, float s) { (void)pos, (void)dir, (void)speed, (void)gravity, (void)dps, (void)s; }
int sfx_play(const char *e, float v, float p) { (void)e, (void)v, (void)p; return 0; }
int sfx_play_at(const char *e, const float pos[3], float m) { (void)e, (void)pos, (void)m; return 0; }
static SkillState skills;
SkillState *skills_state(void) { return &skills; }
int terrain_height_at(float x, float z, float *out) { (void)x, (void)z; *out = 0.0f; return 1; }
void spellfx_burst(const float pos[3], int element, float radius) { (void)pos, (void)element, (void)radius; bursts++; }
void spellfx_soul(const float from[3], float spread, const float to[3], float dt) { (void)from, (void)spread, (void)to, (void)dt; souls_fx++; }
void spellfx_wave(const float from[3], const float dir[3], float range, float spread, int element, float strength) {
	(void)from, (void)dir, (void)range, (void)spread, (void)element, (void)strength; breaths++;
}
void spellfx_soul_stream(const float from[3], float spread, const float to[3], float dt) { (void)from, (void)spread, (void)to, (void)dt; }
int activators_open_lids(const float pos[3], float radius) { (void)pos, (void)radius; return 0; }
int activators_web_down_near(const float pos[3], float radius) { (void)pos, (void)radius; return 0; }
void actors_beast_ghost(int b, int ghost) { (void)b, (void)ghost; }
int crime_guard_hello(int guard, int hold, const char *voice, int female, const char *race_eid) { (void)guard, (void)hold, (void)voice, (void)female, (void)race_eid; return 0; }
int crime_hold_index(const char *name) { (void)name; return 0; }
void game_log(const char *fmt, ...) { (void)fmt; }
const char *interior_title(int place) { (void)place; return ""; }
void music_special(const char *name) { (void)name; }
int navmesh_nearest_level(float x, float y, float z, float radius, float dy, NavRef *ref, float out[3]) { (void)x, (void)y, (void)z, (void)radius, (void)dy; if (ref) *ref = (NavRef){0,0,0}; return 0; }
int navmesh_indoors(void) { return 0; }
int perks_has_form(uint32_t form) { (void)form; return 0; }
int projectiles_fire_hostile_bolt(const float pos[3], const float dir[3], float speed, int element, float damage, float radius) { (void)pos, (void)dir, (void)speed, (void)element, (void)damage, (void)radius; return -1; }
int quests_busy(void) { return 0; }
int quests_stage(int quest) { (void)quest; return 0; }
float quests_thalmor_disguise_m(void) { return -1.0f; }
void quests_voice(const char *key) { (void)key; }
void ui_notice(const char *text, float seconds) { (void)text, (void)seconds; }
int voice_playing(void) { return 0; }

typedef struct {
	int flew_high, banked, landed, ground_fight, crashed, died;
	float breath, bites;
} Fight;

// One dragon's fight: you shoot it from shoot_s on (arrows from below, 60 a second); its
// clips' timeline printed with TIMELINE set
static Fight fight(float shoot_s) {
	Fight r = {0};
	const float player[3] = {0.0f, 0.0f, 0.0f};
	CHECK(creatures_summon_dragon(player), "no dragon came");
	const float dt = 1.0f / 30.0f;
	char prev[24] = "";
	int element = 0;
	for (int f = 0; f < 30 * 240 && !r.died; f++) {
		creatures_update(dt, player, 0, -1);
		r.breath += creatures_take_breath(&element);
		r.bites += creatures_take_damage(0, 1.0f);
		const char *clip = last_clip[0] ? last_clip[0] : "";
		if (getenv("TIMELINE") && strcmp(prev, clip))
			printf("  %6.1fs %-14s y %.1f\n", f * dt, clip, pose_y);
		snprintf(prev, sizeof(prev), "%s", clip);
		r.flew_high |= pose_y > 15.0f;
		r.banked |= fabsf(pose_roll) > 0.2f;
		r.landed |= !strcmp(clip, "land");
		r.crashed |= !strcmp(clip, "crash");
		r.ground_fight |= !strcmp(clip, "attack") || !strcmp(clip, "attack2") || !strcmp(clip, "power") ||
		                  !strcmp(clip, "inhale") || !strcmp(clip, "turn_l") || !strcmp(clip, "turn_r");
		CHECK(isfinite(pose_y) && pose_y > -1.0f, "below the ground at %.1f s: %.2f", f * dt, pose_y);
		if (f % 15 == 0 && f * dt > shoot_s) {
			const float eye[3] = {0.0f, 1.6f, 0.0f};
			for (int a = 0; a < 16; a++)
				for (int e = 0; e < 6; e++) {
					const float ang = a * 0.3927f, el = 0.15f + e * 0.2f;
					const float dir[3] = {cosf(ang) * cosf(el), sinf(el), sinf(ang) * cosf(el)};
					if (creatures_ray(eye, dir, 300.0f, 0.3f, 30.0f) >= 0.0f)
						a = e = 99;
				}
		}
		if (creatures_take_dragon_souls())
			r.died = 1;
	}
	return r;
}

int main(void) {
	creatures_init(PACK);
	skills.player_level = 12;
	printf("a dragon shot once it has landed:\n");
	const Fight a = fight(70.0f);
	printf("  breath on you %.0f, its blows %.0f\n", a.breath, a.bites);
	CHECK(a.flew_high && a.banked, "it should fly high and bank into its turns");
	CHECK(a.breath > 0.0f, "it should breathe on you");
	CHECK(a.landed && a.ground_fight, "it should land and fight on the ground");
	CHECK(a.died, "dead, its soul should stream into you");
	printf("a dragon shot while it flies:\n");
	const Fight b = fight(25.0f);
	CHECK(b.crashed, "badly hurt in the air it should crash");
	CHECK(b.died, "the second dragon should die too");
	CHECK(souls_fx > 0 && bursts > 0, "its soul and its burning, its crash's dust");
	printf(failures ? "dragons_test: %d failures\n" : "dragons_test: all passed\n", failures);
	return failures != 0;
}
