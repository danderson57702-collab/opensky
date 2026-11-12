#include "projectiles.h"
#include "activators.h"

#include <math.h>
#include <string.h>

#include "collision.h"
#include "creatures.h"
#include "loot.h"
#include "objects.h"
#include "sfx.h"
#include "skills.h"
#include "spellfx.h"
#include "aim.h"
#include "items.h"
#include "people.h"
#include "perks.h"
#include "enchant.h"
#include <stdlib.h>
#include "terrain.h"
#include "traps.h"
#include "ui.h"
#include <stdio.h>

#define MAX_ARROWS 64
#define ARROW_LENGTH_M 0.83f // (Skyrim's arrows: 58 units, nock to tip)
#define GRAVITY_MS2 9.81f
#define STUCK_S 600.0f    // an arrow stuck in the world stays this long (to be picked up)
#define TAKE_REACH_M 2.6f
#define FLIGHT_MAX_S 6.0f // (Skyrim's range: 60000 units at 51 m/s is far beyond view)
#define ARROW_RADIUS_M 0.12f // (with the body's capsule: about Skyrim's hit shapes, arms and all)
#define PLAYER_RADIUS_M 0.4f
#define PLAYER_HEIGHT_M 1.8f
#define ARROW_ALERT_M 8.0f // an arrow striking this near rouses them
#define BOLT_MODEL -2  // a mage's fire bolt (drawn as flame)
#define SPIT_MODEL -3  // a frostbite spider's spit (drawn as a green glob)
#define PERK_BULLSEYE 0x00058f64u   // (Skyrim's perks: paralysis, stagger by an arrow)
#define PERK_POWER_SHOT 0x00058f62u
#define BULLSEYE_S 3.0f
#define SPIT_HIT_RADIUS_M 0.35f // (a glob: wider than an arrow)

typedef struct {
	int used, stuck, model, from_player, hits_player;
	int item; // the arrow it is (items.h), to pick up once it's stuck; -1 none
	float pos[3], vel[3], gravity, damage, age;
	float poison_dps, poison_s; // a spit (model SPIT_MODEL): the poison it leaves
	int element;                // a bolt's (BOLT_MODEL): ELEMENT_* (spellfx.h)
	float area;                 // ... bursting over this (m) where it strikes; 0: what it hits
	int poison;                 // a poisoned arrow: its poison's item + 1 (0 none), on what it strikes
	int bow;                    // the player's: the bow it came from (its enchantment), -1
	int stagger;                // a dual-cast bolt with Impact: what it strikes staggers
} Arrow;

static Arrow arrows[MAX_ARROWS];

static void burst(const Arrow *a, const float at[3]);

void projectiles_clear(void) {
	memset(arrows, 0, sizeof(arrows));
}

int projectiles_fire(const float pos[3], const float dir[3], float speed, float gravity, int model, float damage,
                     int from_player, int hits_player, int item) {
	int slot = 0;
	for (int i = 0; i < MAX_ARROWS; i++) { // a free one, else the one stuck longest
		if (!arrows[i].used) {
			slot = i;
			break;
		}
		if (arrows[i].stuck && arrows[i].age > arrows[slot].age)
			slot = i;
	}
	Arrow *a = &arrows[slot];
	memset(a, 0, sizeof(*a));
	a->used = 1, a->model = model, a->from_player = from_player, a->hits_player = hits_player;
	a->gravity = gravity, a->damage = damage, a->item = item, a->bow = -1;
	for (int k = 0; k < 3; k++)
		a->pos[k] = pos[k], a->vel[k] = dir[k] * speed;
	return slot;
}

// Whether the segment from a to b passes through the player's body (an upright capsule)
static int through_player(const float a[3], const float b[3], const float feet[3], float extra) {
	for (int s = 0; s <= 8; s++) {
		const float f = s / 8.0f, p[3] = {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
		if (p[1] > feet[1] - extra && p[1] < feet[1] + PLAYER_HEIGHT_M + extra &&
		    hypotf(p[0] - feet[0], p[2] - feet[2]) < PLAYER_RADIUS_M + extra)
			return 1;
	}
	return 0;
}

static float magic_to_player, arrows_to_player; // (of what struck the player: the bolts', the arrows')

void projectiles_take_split(float *magic, float *arrows) {
	*magic = magic_to_player, *arrows = arrows_to_player;
	magic_to_player = arrows_to_player = 0.0f;
}

float projectiles_update(float dt, const float feet[3]) {
	float to_player = 0.0f;
	for (int i = 0; i < MAX_ARROWS; i++) {
		Arrow *a = &arrows[i];
		if (!a->used)
			continue;
		a->age += dt;
		if (a->stuck) {
			if (a->age > STUCK_S)
				a->used = 0;
			continue;
		}
		if (a->age > FLIGHT_MAX_S) {
			a->used = 0;
			continue;
		}
		a->vel[1] -= a->gravity * GRAVITY_MS2 * dt;
		const float to[3] = {a->pos[0] + a->vel[0] * dt, a->pos[1] + a->vel[1] * dt, a->pos[2] + a->vel[2] * dt};
		const float d[3] = {to[0] - a->pos[0], to[1] - a->pos[1], to[2] - a->pos[2]};
		const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		const float dir[3] = {d[0] / fmaxf(len, 1e-4f), d[1] / fmaxf(len, 1e-4f), d[2] / fmaxf(len, 1e-4f)};
		const float wall = collision_ray_ground(a->pos, to);
		if (!a->hits_player && a->damage > 0.0f) // (yours - an arrow, a firebolt: a spider's web in its way, down)
			activators_strike(a->pos, dir, len * wall + 0.3f);
		// A creature on the way, nearer than the world: struck
		const float struck = a->hits_player ? -1.0f : creatures_ray(a->pos, dir, len * wall, ARROW_RADIUS_M, 0.0f);
		if (struck < 0.0f && a->from_player && a->damage > 0.0f && // (a townsperson in its way: struck)
		    people_ray(a->pos, dir, len * wall, ARROW_RADIUS_M, a->damage) >= 0.0f) {
			sfx_play_at("arrow_flesh", a->pos, 30.0f);
			a->used = 0;
			continue;
		}
		if (struck >= 0.0f && a->area > 0.0f) { // (a Fireball: bursts on whoever it strikes)
			const float at[3] = {a->pos[0] + dir[0] * struck, a->pos[1] + dir[1] * struck, a->pos[2] + dir[2] * struck};
			burst(a, at);
			if (a->stagger) // (Impact: the one it struck)
				creatures_stagger_struck();
			a->used = 0;
			continue;
		}
		if (struck >= 0.0f) {
			creatures_set_ray_sneaks(a->from_player && a->item >= 0);
			creatures_set_spell_element(a->from_player && a->model == BOLT_MODEL ? a->element : -1); // (a Destruction bolt's)
			creatures_ray(a->pos, dir, len * wall, ARROW_RADIUS_M, a->damage);
			creatures_set_spell_element(-1);
			creatures_set_ray_sneaks(0);
			if (a->poison > 0)
				creatures_poison_struck(a->poison - 1);
			if (a->stagger)
				creatures_stagger_struck();
			if (a->item >= 0) // (it stays in the body: taken back when you search it)
				creatures_take_arrow(a->item);
			if (a->from_player && a->bow >= 0) // (the bow's enchantment: the Bow of Embers' fire)
				enchant_strike(a->bow);
			if (a->from_player && a->item >= 0) { // (the Archery perks: Bullseye's paralysis, Power Shot's stagger)
				if (perks_has_form(PERK_BULLSEYE) && rand() % 100 < 15)
					creatures_paralyze_struck(BULLSEYE_S);
				else if (perks_has_form(PERK_POWER_SHOT) && rand() % 100 < 50)
					creatures_stagger_struck();
			}
			const float at[3] = {a->pos[0] + dir[0] * struck, a->pos[1] + dir[1] * struck, a->pos[2] + dir[2] * struck};
			sfx_play_at("arrow_flesh", at, 30.0f);
			if (a->from_player)
				skills_use(SK_ARCHERY, a->damage);
			a->used = 0;
			continue;
		}
		if (a->hits_player && through_player(a->pos, to, feet, a->model == SPIT_MODEL ? SPIT_HIT_RADIUS_M : 0.0f)) {
			to_player += a->damage;
			if (a->model == BOLT_MODEL) // (a mage's bolt: magic, not an arrow)
				magic_to_player += a->damage;
			else if (a->model != SPIT_MODEL)
				arrows_to_player += a->damage;
			if (a->model == SPIT_MODEL) {
				creatures_poison_player(a->poison_dps, a->poison_s);
				sfx_play("spider_spit_hit", 0.8f, 0.0f);
			} else {
				sfx_play("arrow_player", 0.8f, 0.0f);
			}
			a->used = 0;
			continue;
		}
		if (wall < 1.0f && a->model == SPIT_MODEL) { // (a spit: splashed on the floor or a wall)
			sfx_play_at("spider_spit_hit", to, 20.0f);
			a->used = 0;
			continue;
		}
		if (wall < 1.0f && a->area > 0.0f) { // (a Fireball against a wall or the floor: bursts there)
			const float at[3] = {a->pos[0] + d[0] * wall, a->pos[1] + d[1] * wall, a->pos[2] + d[2] * wall};
			burst(a, at);
			a->used = 0;
			continue;
		}
		if (wall < 1.0f && a->model == BOLT_MODEL) { // (a fire bolt: spent)
			traps_fire_at(to, 1.0f);
			a->used = 0;
			continue;
		}
		if (wall < 1.0f) { // into the world: it sticks there
			for (int k = 0; k < 3; k++)
				a->pos[k] += d[k] * wall;
			a->stuck = 1, a->age = 0.0f;
			sfx_play_at("arrow_stick", a->pos, 25.0f);
			if (a->from_player) // (a miss: those near where it struck come looking)
				creatures_alert_at(a->pos, ARROW_ALERT_M, feet);
			continue;
		}
		memcpy(a->pos, to, sizeof(to));
	}
	return to_player;
}

int projectiles_fire_bolt(const float pos[3], const float dir[3], float speed, int element, float damage, float area) {
	const int shot = projectiles_fire(pos, dir, speed, 0.0f, BOLT_MODEL, damage, 1, 0, -1);
	arrows[shot].element = element, arrows[shot].area = area;
	return shot;
}

int projectiles_fire_hostile_bolt(const float pos[3], const float dir[3], float speed, int element, float damage, float area) {
	const int shot = projectiles_fire(pos, dir, speed, 0.0f, BOLT_MODEL, damage, 0, 1, -1);
	arrows[shot].element = element, arrows[shot].area = area;
	return shot;
}

void projectiles_set_stagger(int shot) {
	if (shot >= 0 && shot < MAX_ARROWS)
		arrows[shot].stagger = 1;
}

// A bolt with a burst (a Fireball's): everything within its area hurt, its fire or frost seen
static void burst(const Arrow *a, const float at[3]) {
	creatures_set_spell_element(a->from_player ? a->element : -1);
	creatures_area(at, a->area, a->damage);
	creatures_set_spell_element(-1);
	people_area(at, a->area, a->damage);
	spellfx_burst(at, a->element, a->area);
	if (a->element == ELEMENT_FIRE)
		traps_fire_at(at, a->area);
	sfx_play_at(a->element == ELEMENT_FIRE ? "mag_rune_impact" : a->element == ELEMENT_FROST ? "mag_frost_hit" : "mag_shock_hit",
	            at, 40.0f);
}

void projectiles_fire_spit(const float pos[3], const float dir[3], float speed, float gravity, float dps, float seconds) {
	Arrow *a = &arrows[projectiles_fire(pos, dir, speed, gravity, SPIT_MODEL, 0.0f, 0, 1, -1)];
	a->poison_dps = dps, a->poison_s = seconds, a->element = ELEMENT_POISON;
}

int projectiles_sprites(ObjectsSprite *out, int max) {
	int n = 0;
	for (int i = 0; i < MAX_ARROWS && n + 2 <= max; i++) {
		const Arrow *a = &arrows[i];
		if (!a->used || a->stuck || (a->model != BOLT_MODEL && a->model != SPIT_MODEL))
			continue;
		if (a->model == SPIT_MODEL) { // a spit: a pale green glob in a sickly glow
			ObjectsSprite *g = &out[n++];
			memset(g, 0, sizeof(*g));
			memcpy(g->pos, a->pos, sizeof(g->pos));
			g->size = 0.28f, g->kind = SPRITE_GLOW;
			g->rgba[0] = 200, g->rgba[1] = 255, g->rgba[2] = 150, g->rgba[3] = 255;
			ObjectsSprite *h = &out[n++];
			memset(h, 0, sizeof(*h));
			memcpy(h->pos, a->pos, sizeof(h->pos));
			h->size = 0.7f, h->kind = SPRITE_GLOW;
			h->rgba[0] = 60, h->rgba[1] = 140, h->rgba[2] = 30, h->rgba[3] = 255;
			continue;
		}
		if (a->element != ELEMENT_FIRE) { // a frost or lightning bolt: a cold or crackling glow
			static const uint8_t core[3][3] = {{0}, {210, 235, 255}, {230, 230, 255}}, halo[3][3] = {{0}, {50, 110, 170}, {90, 90, 200}};
			const int e = a->element == ELEMENT_SHOCK ? 2 : 1;
			ObjectsSprite *c = &out[n++];
			memset(c, 0, sizeof(*c));
			memcpy(c->pos, a->pos, sizeof(c->pos));
			c->size = 0.35f, c->kind = SPRITE_GLOW;
			memcpy(c->rgba, core[e], 3), c->rgba[3] = 255;
			ObjectsSprite *h = &out[n++];
			memset(h, 0, sizeof(*h));
			memcpy(h->pos, a->pos, sizeof(h->pos));
			h->size = 1.2f, h->kind = SPRITE_GLOW;
			memcpy(h->rgba, halo[e], 3), h->rgba[3] = 255;
			continue;
		}
		const float big = a->area > 0.0f ? 1.6f : 1.0f; // (a Fireball: bigger than a mage's bolt)
		ObjectsSprite *s = &out[n++]; // a fire bolt: a flame in a warm glow
		memset(s, 0, sizeof(*s));
		memcpy(s->pos, a->pos, sizeof(s->pos));
		s->size = 0.45f * big, s->kind = SPRITE_FIRE, s->frame = (uint8_t)((int)(a->age * 14.0f) & 7);
		s->rgba[0] = 255, s->rgba[1] = 200, s->rgba[2] = 140, s->rgba[3] = 255;
		ObjectsSprite *g = &out[n++];
		memset(g, 0, sizeof(*g));
		memcpy(g->pos, a->pos, sizeof(g->pos));
		g->size = 1.1f * big, g->kind = SPRITE_GLOW;
		g->rgba[0] = 160, g->rgba[1] = 80, g->rgba[2] = 30, g->rgba[3] = 255;
	}
	return n;
}

// The flight model laid along dir with its tip at tip (Skyrim's arrows fly along their -Z)
static void draw_along(int model, const float tip[3], const float f[3]) {
	static const uint8_t tint[3] = {200, 200, 200};
	const float z[3] = {-f[0], -f[1], -f[2]};
	float x[3] = {z[2], 0.0f, -z[0]}; // (up x z)
	const float xl = sqrtf(x[0] * x[0] + x[2] * x[2]);
	if (xl < 1e-4f)
		x[0] = 1.0f, x[2] = 0.0f;
	else
		x[0] /= xl, x[2] /= xl;
	const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
	const float m[9] = {x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2]};
	loot_draw_moving(model, m, tip, tint);
}

void projectiles_draw_nocked(int model, const float nock[3], const float grip[3]) {
	float f[3] = {grip[0] - nock[0], grip[1] - nock[1], grip[2] - nock[2]};
	const float l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	if (model < 0 || l < 0.05f)
		return;
	for (int k = 0; k < 3; k++)
		f[k] /= l;
	const float tip[3] = {nock[0] + f[0] * ARROW_LENGTH_M, nock[1] + f[1] * ARROW_LENGTH_M, nock[2] + f[2] * ARROW_LENGTH_M};
	draw_along(model, tip, f);
}

void projectiles_draw(void) {
	static const uint8_t tint[3] = {200, 200, 200};
	for (int i = 0; i < MAX_ARROWS; i++) {
		const Arrow *a = &arrows[i];
		if (!a->used || a->model < 0)
			continue;
		if (!terrain_rect_in_view(a->pos[0] - 1.0f, a->pos[2] - 1.0f, a->pos[0] + 1.0f, a->pos[2] + 1.0f) ||
		    !objects_point_in_sight(a->pos[0], a->pos[1], a->pos[2]))
			continue;
		// The model flies along its -Z (Skyrim's +Y, tip first): its axes turned to the flight
		const float speed = sqrtf(a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] + a->vel[2] * a->vel[2]);
		if (speed < 1e-3f)
			continue;
		const float f[3] = {a->vel[0] / speed, a->vel[1] / speed, a->vel[2] / speed};
		const float z[3] = {-f[0], -f[1], -f[2]};
		float x[3] = {z[2], 0.0f, -z[0]}; // (up x z)
		const float xl = sqrtf(x[0] * x[0] + x[2] * x[2]);
		if (xl < 1e-4f)
			x[0] = 1.0f, x[2] = 0.0f;
		else
			x[0] /= xl, x[2] /= xl;
		const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
		const float m[9] = {x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2]};
		loot_draw_moving(a->model, m, a->pos, tint);
	}
}

int projectiles_aim(const float eye[3], const float dir[3], float *along, const char **label) {
	static char text[64];
	int best = -1;
	float best_t = 1e9f;
	for (int i = 0; i < MAX_ARROWS; i++) {
		const Arrow *a = &arrows[i];
		float t;
		if (a->used && a->stuck && a->item >= 0 && aim_hits(eye, dir, a->pos, 0.3f, TAKE_REACH_M, &t) && t < best_t)
			best = i, best_t = t;
	}
	if (best >= 0) {
		*along = best_t;
		snprintf(text, sizeof(text), "Take %s", items_name(arrows[best].item));
		*label = text;
	}
	return best;
}

void projectiles_take(int i) {
	if (i < 0 || i >= MAX_ARROWS || !arrows[i].used || !arrows[i].stuck || arrows[i].item < 0)
		return;
	items_add(arrows[i].item, 1);
	char text[80];
	snprintf(text, sizeof(text), "%s added", items_name(arrows[i].item));
	ui_notice(text, 2.0f);
	sfx_play("itm_up", 0.6f, 0.0f);
	arrows[i].used = 0;
}

void projectiles_set_bow(int shot, int bow) {
	if (shot >= 0 && shot < MAX_ARROWS && arrows[shot].used)
		arrows[shot].bow = bow;
}

void projectiles_set_poison(int shot, int poison) {
	if (shot >= 0 && shot < MAX_ARROWS && arrows[shot].used)
		arrows[shot].poison = poison + 1;
}
