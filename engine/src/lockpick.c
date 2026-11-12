#include "lockpick.h"

#include "perks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "atmosphere.h"
#include "effects.h"
#include "fileio.h"
#include "items.h"
#include "objects.h"
#include "skills.h"
#include "skmesh.h"
#include "sfx.h"
#include "ui.h"
#include "gametext.h"

#define OVERLAY_DEPTH 0.002f
#define LOCKPICK_FORM 0x0000000Au
// Skyrim.esm / the game's settings
#define SWEET_SKILL_BASE 0.82f     // fLockpickSkillSweetSpotBase
#define SWEET_SKILL_MULT 0.006f    // fLockpickSkillSweetSpotMult
#define PARTIAL_SKILL_BASE 0.775f  // fLockpickSkillPartialPickBase
#define PARTIAL_SKILL_MULT 0.015f  // fLockpickSkillPartialPickMult
#define BREAK_SKILL_BASE 1.0f      // fLockpickBreakSkillBase
#define BREAK_SKILL_MULT 0.005f    // fLockpickBreakSkillMult
#define USAGE_BROKEN 0.25f         // fSkillUsageLockPickBroken
enum { NOVICE, APPRENTICE, ADEPT, EXPERT, MASTER, TIERS };
static const int tier_max[TIERS] = {1, 25, 50, 75, 100};             // iLockLevelMax*
static const float pick_life[TIERS] = {2.0f, 1.0f, 0.75f, 0.5f, 0.25f}; // fLockpickBreak*: seconds forcing
static const float usage[TIERS] = {2.0f, 3.0f, 5.0f, 8.0f, 13.0f};      // fSkillUsageLockPick*
static const float sweet_base_deg[TIERS] = {33.0f, 21.0f, 12.0f, 6.5f, 3.0f}; // (estimated, see lockpick.h)
static const char *tier_names[TIERS] = {"Novice", "Apprentice", "Adept", "Expert", "Master"};
#define PARTIAL_DEG 90.0f   // beyond the sweet spot the turn shrinks to nothing over this (x the partial factor)
#define ARC_DEG 90.0f       // the pick sweeps -90..90 (12 o'clock is 0)
#define PICK_SPEED 75.0f    // degrees per second at full stick
#define TURN_RATE 1.8f      // the lock turning: of a full turn per second
#define RETURN_RATE 3.0f    // springing back when let go
#define OPEN_HOLD_S 0.35f   // fully turned: a moment before it opens
#define BREAK_SHOW_S 0.5f
#define LOCKSMITH_NEAR_DEG 30.0f // (Locksmith: within this of the sweet spot to begin)   // the broken pick's moment
#define VIEW_DIST 4.6f // (the lock in the middle of the screen, all of it in view, as Skyrim's)
#define QUANT_M 3.0f

typedef struct {
	int model;
	SkmMesh mesh;
	unsigned tex[8];
	float uv_c[2], uv_e;
	ObjectVertex *verts[8];
	int ok;
} Part;

enum { P_STILL, P_TUMBLER, P_SHIV, P_PICK, PARTS };
static Part parts[PARTS];
static float keyhole[3], tip[3];
static int active, target = -1, opened = -1, tier;
static float sweet, sweet_half, partial_range, life, life_left;
static float pick, turn, open_t, broken_t, shake;
static unsigned rng = 0x51ed27u;
static unsigned prev_buttons;

static int lockpick_item(void) {
	for (int i = 0; i < items_count(); i++)
		if (!strcmp(items_name(i), T_MISC_0000000A_FULL0))
			return i;
	return -1;
}

static char pack[128];

void lockpick_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%sui/lockpick.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	static const char *keys[PARTS] = {"still", "tumbler", "shiv", "pick"};
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char key[16];
		float a, b, c;
		int idx;
		if (sscanf(line, "%15s %f %f %f", key, &a, &b, &c) == 4) {
			float *out = !strcmp(key, "keyhole") ? keyhole : !strcmp(key, "tip") ? tip : NULL;
			if (out)
				out[0] = a, out[1] = b, out[2] = c;
		} else if (sscanf(line, "%15s %d", key, &idx) == 2) {
			for (int k = 0; k < PARTS; k++)
				if (!strcmp(key, keys[k]) && idx >= 0)
					parts[k].model = idx;
		}
	}
	free(buf);
}

// (models load on first use: the model table is read after lockpick_init)
static int ensure(Part *p) {
	if (p->ok)
		return 1;
	if (p->model <= 0 || p->ok < 0)
		return 0;
	char path[400];
	snprintf(path, sizeof(path), "%smeshes/%s.skmesh", pack, objects_model_name(p->model));
	if (skmesh_load(path, &p->mesh) != 0) {
		p->ok = -1;
		return 0;
	}
	float umin[2] = {1e9f, 1e9f}, umax[2] = {-1e9f, -1e9f};
	for (int s = 0; s < p->mesh.submesh_count && s < 8; s++) {
		SkmSubmesh *sm = &p->mesh.submeshes[s];
		p->tex[s] = objects_texture(sm->texture);
		p->verts[s] = malloc(sm->vertex_count * sizeof(ObjectVertex));
		for (uint32_t v = 0; v < sm->vertex_count; v++)
			for (int k = 0; k < 2; k++)
				umin[k] = fminf(umin[k], sm->vertices[v].uv[k]), umax[k] = fmaxf(umax[k], sm->vertices[v].uv[k]);
	}
	p->uv_c[0] = (umin[0] + umax[0]) * 0.5f, p->uv_c[1] = (umin[1] + umax[1]) * 0.5f;
	p->uv_e = fmaxf(fmaxf(umax[0] - umin[0], umax[1] - umin[1]) * 0.5f, 1e-3f);
	p->ok = 1;
	return 1;
}

static float frand(void) {
	rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
	return (rng & 0xffffff) / 16777216.0f;
}

static int tier_of(int level) {
	for (int t = 0; t < TIERS; t++)
		if (level <= tier_max[t])
			return t;
	return -1;
}

static void new_pick(void) {
	pick = 0.0f; // (12 o'clock)
	life_left = life;
	turn = 0.0f;
}

int lockpick_start(int lock_level, int t) {
	const int tr = tier_of(lock_level);
	if (tr < 0) {
		ui_notice("This lock requires a key", 2.5f);
		return 0;
	}
	if (items_have(lockpick_item()) <= 0) {
		ui_notice("You have no lockpicks", 2.5f);
		return 0;
	}
	const float skill = skills_state()->level[SK_LOCKPICKING] * fmaxf(0.0f, 1.0f + effects_skill(SK_LOCKPICKING) / 100.0f); // (Fortified)
	tier = tr, target = t, opened = -1;
	PerkContext lock = {0}; // (and the Lockpicking perks: Novice Locks...)
	lock.lock_level = tier_max[tier], lock.lock_known = 1, lock.school = -1;
	sweet_half = perks_apply(EP_MOD_LOCKPICK_SWEET_SPOT,
	                         sweet_base_deg[tier] * (SWEET_SKILL_BASE + SWEET_SKILL_MULT * skill), &lock) * 0.5f;
	partial_range = PARTIAL_DEG * (PARTIAL_SKILL_BASE + PARTIAL_SKILL_MULT * skill);
	life = pick_life[tier] * (BREAK_SKILL_BASE + BREAK_SKILL_MULT * skill);
	rng ^= (unsigned)t * 2654435761u + (unsigned)(skill * 977.0f);
	sweet = -ARC_DEG + sweet_half + frand() * (2.0f * (ARC_DEG - sweet_half));
	new_pick();
	if (perks_apply(EP_SET_PROGRESSION_PICKING, 0.0f, NULL) > 0.0f) // (Locksmith: the pick starts near where it opens)
		pick = fmaxf(-ARC_DEG, fminf(ARC_DEG, sweet + (frand() - 0.5f) * LOCKSMITH_NEAR_DEG));
	open_t = broken_t = 0.0f;
	active = 1;
	sfx_play("lock_enter", 0.8f, 0.0f);
	prev_buttons = ~0u; // (the button that started it isn't a turn)
	return 1;
}

int lockpick_active(void) {
	return active;
}

int lockpick_take_opened(void) {
	int o = opened;
	opened = -1;
	return o;
}

static void hud(void) {
	char speaker[48], body[160];
	snprintf(speaker, sizeof(speaker), "%s lock", tier_names[tier]);
	snprintf(body, sizeof(body), "Lockpicks: %d    Left stick: the pick    Right stick (or X): turn    O: leave",
	         items_have(lockpick_item()));
	ui_subtitle(speaker, body);
}

static void leave(void) {
	active = 0;
	ui_subtitle(NULL, NULL);
}

void lockpick_update(const SceCtrlData *pad, float dt) {
	if (!active)
		return;
	const unsigned pressed = pad->buttons & ~prev_buttons;
	prev_buttons = pad->buttons;
	if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
		leave();
		return;
	}
	if (open_t > 0.0f) { // turned all the way: open in a moment
		if ((open_t += dt) > OPEN_HOLD_S) {
			skills_use(SK_LOCKPICKING, usage[tier]);
			opened = target;
			leave();
		}
		return;
	}
	if (broken_t > 0.0f) {
		if ((broken_t -= dt) <= 0.0f)
			broken_t = 0.0f;
		hud();
		return;
	}
	// The pick: left stick (or d-pad); it only moves while the lock isn't being turned
	// Turning the lock: the right stick pushed (Skyrim's controller), or X / R held
	const int stick_turn = pad->rx > 128 + 70 || pad->ry < 128 - 70;
	const int turning = stick_turn || (pad->buttons & (SCE_CTRL_CROSS | SCE_CTRL_RTRIGGER)) != 0;
	float stick = (pad->lx - 128) / 127.0f;
	if (fabsf(stick) < 0.15f)
		stick = 0.0f;
	if (pad->buttons & SCE_CTRL_LEFT)
		stick = -0.6f;
	if (pad->buttons & SCE_CTRL_RIGHT)
		stick = 0.6f;
	if (!turning || turn < 0.02f) {
		const float before = pick;
		pick = fmaxf(-ARC_DEG, fminf(ARC_DEG, pick + stick * PICK_SPEED * dt));
		static float moved; // (the pick scraping as it goes, now and then)
		moved += fabsf(pick - before);
		if (moved > 12.0f)
			sfx_play("lock_move", 0.5f, 0.0f), moved = 0.0f;
	}
	// How far the lock can turn from here: all the way in the sweet spot, less further off
	const float off = fabsf(pick - sweet);
	const float can_turn = off <= sweet_half ? 1.0f : fmaxf(0.0f, 1.0f - (off - sweet_half) / partial_range);
	shake = 0.0f;
	static int was_turning, was_jammed;
	if (turning && !was_turning)
		sfx_play("lock_turn", 0.7f, 0.0f);
	was_turning = turning;
	if (turning) {
		turn = fminf(can_turn, turn + TURN_RATE * dt);
		const int jammed = turn < 1.0f && turn >= can_turn - 1e-4f;
		if (jammed && !was_jammed)
			sfx_play("lock_stop", 0.7f, 0.0f);
		was_jammed = jammed;
		if (turn >= 1.0f) {
			open_t = 1e-4f; // open
			sfx_play("lock_unlock", 0.9f, 0.0f);
		} else if (turn >= can_turn - 1e-4f) {
			// Jammed: forcing it wears the pick
			shake = 1.0f;
			life_left -= dt;
			if (life_left <= 0.0f && perks_apply(EP_LOCKPICKS_UNBREAKABLE, 0.0f, NULL) > 0.0f) // (Unbreakable)
				life_left = life;
			if (life_left <= 0.0f) {
				items_add(lockpick_item(), -1);
				sfx_play("lock_break", 0.9f, 0.0f);
				skills_use(SK_LOCKPICKING, USAGE_BROKEN);
				ui_notice("Lockpick broke", 1.5f);
				broken_t = BREAK_SHOW_S;
				new_pick();
				if (items_have(lockpick_item()) <= 0) {
					ui_notice("You're out of lockpicks", 2.5f);
					leave();
					return;
				}
			}
		}
	} else {
		turn = fmaxf(0.0f, turn - RETURN_RATE * dt);
		was_jammed = 0;
	}
	hud();
}

// Model space -> the view: rotated angle (radians) about the keyhole's axis (engine Z,
// toward the viewer), moved by off, then into camera space (the lock straight ahead)
static void place(const Part *p, float angle, const float off[3], float jitter) {
	const float c = cosf(angle), s = sinf(angle), q = 32767.0f / QUANT_M;
	for (int si = 0; si < p->mesh.submesh_count && si < 8; si++) {
		const SkmSubmesh *sm = &p->mesh.submeshes[si];
		for (uint32_t v = 0; v < sm->vertex_count; v++) {
			const SkmVertex *src = &sm->vertices[v];
			float x = src->pos[0] + off[0] - keyhole[0], y = src->pos[1] + off[1] - keyhole[1], z = src->pos[2] + off[2] - keyhole[2];
			float rx = x * c - y * s, ry = x * s + y * c;
			ObjectVertex *o = &p->verts[si][v];
			o->x = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, (rx + jitter) * q));
			o->y = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, ry * q));
			o->z = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, z * q));
			o->pad = 0;
			o->u = (int16_t)((src->uv[0] - p->uv_c[0]) / p->uv_e * 32767.0f);
			o->v = (int16_t)((src->uv[1] - p->uv_c[1]) / p->uv_e * 32767.0f);
			o->r = (uint8_t)(src->col[0] * 230 / 255), o->g = (uint8_t)(src->col[1] * 220 / 255);
			o->b = (uint8_t)(src->col[2] * 200 / 255), o->a = src->col[3];
		}
	}
}

void lockpick_draw(void) {
	if (!active)
		return;
	for (int k = 0; k < PARTS; k++)
		if (!ensure(&parts[k]))
			return;
	const float lock_angle = -turn * (float)M_PI * 0.5f; // (clockwise, as seen)
	const float jitter = shake ? (frand() - 0.5f) * 0.012f : 0.0f;
	const float zero[3] = {0, 0, 0};
	const float to_hole[3] = {keyhole[0] - tip[0], keyhole[1] - tip[1], keyhole[2] - tip[2]};
	place(&parts[P_STILL], 0.0f, zero, 0.0f);
	place(&parts[P_TUMBLER], lock_angle, zero, jitter);
	place(&parts[P_SHIV], lock_angle, zero, jitter);
	place(&parts[P_PICK], lock_angle - pick * (float)M_PI / 180.0f, to_hole, jitter);
	static ObjectsDynamic draws[PARTS * 8];
	int nd = 0;
	for (int k = 0; k < PARTS; k++) {
		if (k == P_PICK && broken_t > 0.0f)
			continue; // (the broken pick: gone for a moment)
		const Part *p = &parts[k];
		for (int s = 0; s < p->mesh.submesh_count && s < 8; s++) {
			const SkmSubmesh *sm = &p->mesh.submeshes[s];
			ObjectsDynamic *d = &draws[nd++];
			d->vertices = p->verts[s];
			d->vertex_count = (int)sm->vertex_count;
			d->indices = sm->indices;
			d->index_count = (int)sm->index_count;
			d->tex = p->tex[s];
			d->pos[0] = 0.0f, d->pos[1] = 0.0f, d->pos[2] = -VIEW_DIST, d->pos[3] = QUANT_M;
			d->uv[0] = p->uv_c[0], d->uv[1] = p->uv_c[1], d->uv[2] = p->uv_e;
			d->alpha = (sm->flags & SKMESH_ALPHA_TEST) != 0;
		}
	}
	// Its own view: the camera at the origin looking down -Z, no fog, no torch
	glClear(GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluPerspective(38.0, 960.0 / 544.0, 0.1, 20.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	atmosphere_set_camera(0.0f, 0.0f, 0.0f);
	atmosphere_set_torch(NULL, 0.0f, NULL);
	atmosphere_set_clear(1);
	// (in the front sliver of the depth range: always over the world, whatever its depth
	// buffer holds - in a cave the walls were nearer than the clear left them)
	glDepthRangef(0.0f, OVERLAY_DEPTH);
	objects_draw_dynamic(draws, nd);
	atmosphere_set_clear(0);
	glDepthRangef(0.0f, 1.0f);
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}
