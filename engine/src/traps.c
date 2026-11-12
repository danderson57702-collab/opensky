#include "frametime.h"
#include "traps.h"

#include <math.h>
#include <string.h>

#include "collision.h"
#include "creatures.h"
#include "perks.h"
#include "doors.h"
#include "items.h"
#include "projectiles.h"
#include "sfx.h"
#include "skills.h"

#define MAX_TRAPS 256
#define MAX_LINKS 16
#define PLAYER_R 0.35f
#define BLADE_PERIOD_S 2.4f  // a swinging blade's swing there and back
#define MACE_PERIOD_S 2.0f
#define MACE_SETTLE_S 1.6f   // a mace's / ram's swings die away over about this
#define SPEAR_OUT_S 0.12f
#define SPEAR_HOLD_S 1.0f
#define SPEAR_BACK_S 1.0f
#define SPEAR_REACH_M 1.0f
#define WALL_OUT_S 0.35f
#define WALL_HOLD_S 1.2f
#define WALL_BACK_S 2.0f
#define DART_SHOTS 6
#define DART_EVERY_S 0.25f
#define DART_SPEED_MS 30.0f
#define FLAME_S 3.0f
#define FLAME_EVERY_S 0.08f
#define FLAME_SPEED_MS 9.0f
#define FLAME_REACH_M 4.0f
#define OIL_BURN_S 8.5f // (Skyrim's oil: burnt out in some 8-9 seconds - it burnt 20)
#define OIL_LABEL_M 6.0f // looked at this near, unlit oil is named
#define FALL_G 9.81f
#define REARM_S 1.0f       // a blow strikes once, then not again for this long
#define BONES_EVERY_S 3.0f
#define LAMP_SPLASH_M 5.0f // a falling lamp lights oil this near where it lands
#define IRON_ARROW_FORM 0x0001397du

enum {
	T_PLATE = 1, T_WIRE, T_BOX, T_LINKER, T_BEAR, T_BLADE, T_MACE, T_RAM, T_WALL, T_SPEAR, T_ROCKS, T_LAMP, T_OIL,
	T_DARTS, T_FLAME, T_BONES
};

typedef struct {
	uint32_t ref;
	int kind;
	float damage[6], threshold[6];
	float lo[3], hi[3], muzzle[3]; // local (model space): its bounds (a trigger box: its box), where it shoots from
	uint32_t parents[MAX_LINKS], children[MAX_LINKS];
	int parent_count, child_count;
	float m[9], t[3], centre[3];
	int door;
	int active, spent, pressed;
	float t_active, rearm, fire_t, fall_v, fall_at, fall_to, bones_t;
	int shots;
} Trap;

static Trap traps[MAX_TRAPS];
static int trap_count;
static float damage_out;
#define PERK_LIGHT_FOOT 0x0005820cu
static int depth; // (hubs passing a trigger on: a loop of links can't run away)
static int lever_pull; // (what's springing came from a lever or chain pulled: blades stop)

void traps_clear(void) {
	trap_count = 0;
}

const uint8_t *traps_append(const uint8_t *p, const uint8_t *end) {
	if (!p || p + 4 > end)
		return NULL;
	uint32_t n;
	memcpy(&n, p, 4);
	p += 4;
	for (uint32_t i = 0; i < n; i++) {
		if (p + 92 > end)
			return NULL;
		Trap tr;
		memset(&tr, 0, sizeof(tr));
		memcpy(&tr.ref, p, 4);
		tr.kind = p[4];
		const int np = p[5], nc = p[6];
		memcpy(tr.damage, p + 8, 24);
		memcpy(tr.threshold, p + 32, 24);
		memcpy(tr.lo, p + 56, 12);
		memcpy(tr.hi, p + 68, 12);
		memcpy(tr.muzzle, p + 80, 12);
		p += 92;
		if (p + (np + nc) * 4 + 64 > end)
			return NULL;
		for (int k = 0; k < np; k++, p += 4)
			if (tr.parent_count < MAX_LINKS)
				memcpy(&tr.parents[tr.parent_count++], p, 4);
		for (int k = 0; k < nc; k++, p += 4)
			if (tr.child_count < MAX_LINKS)
				memcpy(&tr.children[tr.child_count++], p, 4);
		memcpy(tr.m, p, 36);
		memcpy(tr.t, p + 36, 12);
		memcpy(tr.centre, p + 48, 12);
		p = doors_add_trap(p, end, &tr.door);
		if (!p)
			return NULL;
		tr.fall_to = -1.0f;
		if (trap_count < MAX_TRAPS)
			traps[trap_count++] = tr;
	}
	return p;
}

// Its damage at the player's level (Skyrim's table)
static float trap_damage(const Trap *tr) {
	const int level = skills_state()->player_level;
	for (int i = 0; i < 6; i++)
		if (level < tr->threshold[i] || i == 5)
			return tr->damage[i];
	return tr->damage[5];
}

// World <-> the trap's model space
static void to_world(const Trap *tr, const float l[3], float w[3]) {
	for (int i = 0; i < 3; i++)
		w[i] = tr->m[i * 3] * l[0] + tr->m[i * 3 + 1] * l[1] + tr->m[i * 3 + 2] * l[2] + tr->t[i];
}

static void to_local(const Trap *tr, const float w[3], float l[3]) {
	const float d[3] = {w[0] - tr->t[0], w[1] - tr->t[1], w[2] - tr->t[2]};
	for (int i = 0; i < 3; i++) { // (the placement's rotation x scale: its columns' lengths undo the scale)
		const float c[3] = {tr->m[i], tr->m[3 + i], tr->m[6 + i]};
		const float s2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
		l[i] = (c[0] * d[0] + c[1] * d[1] + c[2] * d[2]) / (s2 > 1e-8f ? s2 : 1.0f);
	}
}

static void spring(Trap *tr);

void traps_pull_ref(uint32_t ref) {
	lever_pull = 1;
	traps_fire_ref(ref);
	lever_pull = 0;
}

void traps_fire_ref(uint32_t ref) {
	if (++depth > 6) {
		depth--;
		return;
	}
	for (int i = 0; i < trap_count; i++)
		for (int k = 0; k < traps[i].parent_count; k++)
			if (traps[i].parents[k] == ref) {
				spring(&traps[i]);
				break;
			}
	depth--;
}

static void fire_children(const Trap *tr) {
	for (int k = 0; k < tr->child_count; k++)
		for (int i = 0; i < trap_count; i++)
			if (traps[i].ref == tr->children[k] && &traps[i] != tr)
				spring(&traps[i]);
}

// A trap set off (by its trigger, a hub, a lever)
static void spring(Trap *tr) {
	switch (tr->kind) {
	case T_LINKER:
	case T_PLATE:
	case T_WIRE:
	case T_BOX:
		traps_fire_ref(tr->ref); // (passed on to what listens)
		break;
	case T_BLADE: // (set swinging; a lever or chain pulled stops them at the end of a swing)
		if (lever_pull && tr->active) {
			tr->spent = 1; // (stopping)
			break;
		}
		if (!tr->active)
			sfx_play_at("trp_windup", tr->centre, 30.0f), tr->t_active = 0.0f;
		tr->active = 1, tr->spent = 0;
		break;
	case T_MACE:
	case T_RAM:
	case T_WALL:
	case T_SPEAR:
		if (tr->active || (tr->spent && tr->kind != T_SPEAR && tr->kind != T_WALL))
			break;
		tr->active = 1, tr->t_active = 0.0f, tr->rearm = 0.0f;
		sfx_play_at(tr->kind == T_SPEAR ? "trp_spear" : tr->kind == T_MACE ? "trp_mace" : tr->kind == T_RAM ? "trp_ram"
		            : "trp_swing", tr->centre, 35.0f);
		break;
	case T_ROCKS:
	case T_LAMP:
		if (tr->spent)
			break;
		tr->spent = tr->active = 1, tr->fall_at = 0.0f, tr->fall_v = 0.0f;
		{ // (down to whatever's below)
			const float below[3] = {tr->centre[0], tr->centre[1] - 30.0f, tr->centre[2]};
			const float t = collision_ray_ground(tr->centre, below);
			tr->fall_to = t < 1.0f ? 30.0f * t : 3.0f;
		}
		sfx_play_at(tr->kind == T_ROCKS ? "trp_rockfall" : "trp_oil", tr->centre, 35.0f);
		break;
	case T_DARTS:
	case T_FLAME:
		if (tr->active)
			break;
		tr->active = 1, tr->t_active = 0.0f, tr->shots = 0, tr->fire_t = 0.0f;
		fire_children(tr);
		if (tr->kind == T_FLAME)
			sfx_play_at("trp_fire_lp", tr->centre, 30.0f);
		break;
	case T_OIL:
		if (!tr->active)
			tr->active = 1, tr->t_active = 0.0f, sfx_play_at("trp_oil", tr->centre, 35.0f);
		break;
	}
}

int traps_spring_ref(uint32_t ref) {
	int n = 0;
	for (int i = 0; i < trap_count; i++)
		if (traps[i].ref == ref)
			spring(&traps[i]), n++;
	return n;
}

void traps_fire_at(const float pos[3], float radius) {
	for (int i = 0; i < trap_count; i++) {
		Trap *tr = &traps[i];
		const float r = radius + 0.5f * hypotf(tr->hi[0] - tr->lo[0], tr->hi[2] - tr->lo[2]);
		if ((tr->kind == T_OIL || tr->kind == T_LAMP) && fabsf(tr->centre[1] - pos[1]) < 3.0f &&
		    hypotf(tr->centre[0] - pos[0], tr->centre[2] - pos[2]) < r)
			spring(tr);
	}
}

// Whether a point (world) lies over the trap's footprint, within dy above its top
static int over(const Trap *tr, const float w[3], float grow, float above) {
	float l[3];
	to_local(tr, w, l);
	return l[0] > tr->lo[0] - grow && l[0] < tr->hi[0] + grow && l[2] > tr->lo[2] - grow && l[2] < tr->hi[2] + grow &&
	       l[1] > tr->lo[1] - 0.5f && l[1] < tr->hi[1] + above;
}

static int in_pool(const float pos[3], void *tr) {
	return over((const Trap *)tr, pos, 0.1f, 1.0f);
}

// The tripwire: the player's legs across its line (its long side, at its height)
static int across(const Trap *tr, const float feet[3]) {
	float l[3];
	to_local(tr, feet, l);
	const int along_x = tr->hi[0] - tr->lo[0] > tr->hi[2] - tr->lo[2];
	const float mid_other = along_x ? (tr->lo[2] + tr->hi[2]) * 0.5f : (tr->lo[0] + tr->hi[0]) * 0.5f;
	const float a = along_x ? l[0] : l[2], b = along_x ? l[2] : l[0];
	const float a0 = along_x ? tr->lo[0] : tr->lo[2], a1 = along_x ? tr->hi[0] : tr->hi[2];
	return a > a0 && a < a1 && fabsf(b - mid_other) < PLAYER_R && l[1] < tr->hi[1] + 0.3f && l[1] > tr->lo[1] - 1.0f;
}

// A blow from its striking part: the player and creatures there are hurt (once per swing)
static void blow(Trap *tr, const float feet[3]) {
	float at[3], r;
	if (tr->rearm > 0.0f || !doors_part_blow(tr->door, at, &r))
		return;
	r = fminf(r, 0.9f);
	const float chest = feet[1] + 1.0f;
	const float d = hypotf(at[0] - feet[0], at[2] - feet[2]);
	const int hits = d < r + PLAYER_R && at[1] > feet[1] - r && at[1] < chest + 0.8f + r;
	if (hits) {
		damage_out += trap_damage(tr);
		sfx_play_at("trp_impact", at, 25.0f);
		tr->rearm = REARM_S;
	}
	if (creatures_area(at, r + 0.3f, trap_damage(tr)))
		tr->rearm = fmaxf(tr->rearm, REARM_S * 0.5f);
}

void traps_update(float dt, const float feet[3]) {
	const float head[3] = {feet[0], feet[1] + 1.6f, feet[2]};
	for (int i = 0; i < trap_count; i++) {
		Trap *tr = &traps[i];
		tr->rearm -= dt;
		switch (tr->kind) {
		case T_PLATE: { // stepped on (by you or a creature): what's linked springs; up again after
			// (Light Foot: your weight never presses one down)
			const int on = (over(tr, feet, 0.05f, 0.6f) && !perks_has_form(PERK_LIGHT_FOOT)) || creatures_near(tr->centre, 0.4f);
			if (on && !tr->pressed) {
				sfx_play_at("trp_plate", tr->centre, 25.0f);
				spring(tr);
			}
			tr->pressed = on;
			doors_set_pose(tr->door, on ? 1.0f : 0.0f);
			break;
		}
		case T_WIRE:
			if (!tr->spent && (across(tr, feet) || creatures_near(tr->centre, 0.3f))) {
				tr->spent = 1;
				sfx_play_at("trp_tripwire", tr->centre, 25.0f);
				spring(tr);
			}
			break;
		case T_BOX: {
			const int in = over(tr, feet, 0.0f, 0.0f) || over(tr, head, 0.0f, 0.0f);
			if (in && !tr->pressed)
				spring(tr);
			tr->pressed = in;
			break;
		}
		case T_BEAR:
			if (!tr->spent && (hypotf(feet[0] - tr->centre[0], feet[2] - tr->centre[2]) < 0.45f &&
			                   fabsf(feet[1] - tr->centre[1]) < 0.5f)) {
				tr->spent = 1;
				damage_out += trap_damage(tr);
				sfx_play_at("trp_beartrap", tr->centre, 30.0f);
				doors_set_pose(tr->door, 1.0f);
			} else if (!tr->spent && creatures_near(tr->centre, 0.3f)) {
				tr->spent = 1;
				creatures_area(tr->centre, 0.8f, trap_damage(tr));
				sfx_play_at("trp_beartrap", tr->centre, 30.0f);
				doors_set_pose(tr->door, 1.0f);
			}
			break;
		case T_BLADE: // swinging from its cocked rest through the bottom to the other side and back
			if (!tr->active)
				break;
			tr->t_active += dt;
			{
				const float w = 2.0f * (float)M_PI / BLADE_PERIOD_S;
				float u = 1.0f - cosf(w * tr->t_active); // (0 rest .. 2 the far side)
				if (tr->spent && u < 0.05f) // (stopped by its chain: back at rest)
					tr->active = 0, u = 0.0f;
				doors_set_pose(tr->door, u);
				if (fabsf(sinf(w * tr->t_active)) > 0.3f) // (moving fast: through the bottom)
					blow(tr, feet);
				if (fmodf(tr->t_active, BLADE_PERIOD_S * 0.5f) < dt)
					sfx_play_at("trp_swing", tr->centre, 30.0f);
			}
			break;
		case T_MACE:
		case T_RAM: // one swing down that dies away, left hanging
			if (!tr->active)
				break;
			tr->t_active += dt;
			{
				const float w = 2.0f * (float)M_PI / MACE_PERIOD_S, e = expf(-tr->t_active / MACE_SETTLE_S);
				doors_set_pose(tr->door, 1.0f - e * cosf(w * tr->t_active));
				if (e > 0.3f)
					blow(tr, feet);
				if (tr->t_active > 8.0f)
					tr->active = 0, tr->spent = 1;
			}
			break;
		case T_WALL:
		case T_SPEAR: { // out fast, a moment, back slowly
			if (!tr->active)
				break;
			tr->t_active += dt;
			const float out_s = tr->kind == T_WALL ? WALL_OUT_S : SPEAR_OUT_S;
			const float hold_s = tr->kind == T_WALL ? WALL_HOLD_S : SPEAR_HOLD_S;
			const float back_s = tr->kind == T_WALL ? WALL_BACK_S : SPEAR_BACK_S;
			const float t = tr->t_active;
			float u = t < out_s ? t / out_s : t < out_s + hold_s ? 1.0f : 1.0f - (t - out_s - hold_s) / back_s;
			if (u <= 0.0f)
				u = 0.0f, tr->active = 0;
			doors_set_pose(tr->door, tr->kind == T_SPEAR ? u * SPEAR_REACH_M : u);
			if (t < out_s + 0.2f)
				blow(tr, feet);
			break;
		}
		case T_ROCKS:
		case T_LAMP: // falling (gravity) to what's below; rocks crush, a lamp lights the oil
			if (!tr->active)
				break;
			tr->fall_v += FALL_G * dt;
			tr->fall_at += tr->fall_v * dt;
			if (tr->fall_at >= tr->fall_to) {
				tr->fall_at = tr->fall_to, tr->active = 0;
				const float landed[3] = {tr->centre[0], tr->centre[1] - tr->fall_to, tr->centre[2]};
				if (tr->kind == T_ROCKS) {
					if (hypotf(feet[0] - landed[0], feet[2] - landed[2]) < 1.3f && fabsf(feet[1] - landed[1]) < 1.8f)
						damage_out += trap_damage(tr);
					creatures_area(landed, 1.3f, trap_damage(tr));
					sfx_play_at("trp_impact", landed, 35.0f);
				} else {
					traps_fire_at(landed, LAMP_SPLASH_M);
				}
			}
			doors_set_pose(tr->door, tr->fall_at);
			break;
		case T_OIL: // burning: hurts whoever stands in it
			if (!tr->active)
				break;
			tr->t_active += dt;
			if (over(tr, feet, 0.1f, 1.0f))
				damage_out += trap_damage(tr) * dt;
			creatures_hurt_where(in_pool, tr, trap_damage(tr) * dt); // (and anyone else standing in it)
			if (tr->t_active > OIL_BURN_S)
				tr->active = 0, tr->spent = 1;
			break;
		case T_DARTS:
		case T_FLAME: { // a burst of darts / a jet of fire from its port, the way it faces
			if (!tr->active)
				break;
			tr->t_active += dt, tr->fire_t -= dt;
			float from[3], ahead[3], dir[3];
			const float tip[3] = {tr->muzzle[0], tr->muzzle[1], tr->muzzle[2] - 1.0f};
			to_world(tr, tr->muzzle, from);
			to_world(tr, tip, ahead);
			for (int k = 0; k < 3; k++)
				dir[k] = ahead[k] - from[k];
			const float l = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
			for (int k = 0; k < 3; k++)
				dir[k] /= fmaxf(l, 1e-4f);
			if (tr->kind == T_DARTS) {
				if (tr->fire_t <= 0.0f && tr->shots < DART_SHOTS) {
					float speed, gravity;
					const int model = items_flight(items_by_form(IRON_ARROW_FORM), &speed, &gravity);
					projectiles_fire(from, dir, DART_SPEED_MS, 0.1f, model, trap_damage(tr), 0, 1, -1);
					sfx_play_at("trp_dart", from, 25.0f);
					tr->shots++, tr->fire_t = DART_EVERY_S;
				}
				if (tr->shots >= DART_SHOTS && tr->fire_t <= 0.0f)
					tr->active = 0;
			} else {
				if (tr->fire_t <= 0.0f) {
					projectiles_fire(from, dir, FLAME_SPEED_MS, 0.0f, -2, trap_damage(tr) * FLAME_EVERY_S, 0, 1, -1);
					tr->fire_t = FLAME_EVERY_S;
				}
				// (the jet burns what stands in it: creatures too)
				const float mid[3] = {from[0] + dir[0] * FLAME_REACH_M * 0.5f, from[1] + dir[1] * FLAME_REACH_M * 0.5f,
				                      from[2] + dir[2] * FLAME_REACH_M * 0.5f};
				creatures_area(mid, 1.0f, trap_damage(tr) * dt);
				traps_fire_at(mid, 1.0f);
				if (tr->t_active > FLAME_S)
					tr->active = 0;
			}
			break;
		}
		case T_BONES:
			tr->bones_t -= dt;
			if (tr->bones_t <= 0.0f && hypotf(feet[0] - tr->centre[0], feet[2] - tr->centre[2]) < 1.2f &&
			    fabsf(feet[1] + 1.0f - tr->centre[1]) < 1.5f) {
				sfx_play_at("trp_bones", tr->centre, 30.0f);
				tr->bones_t = BONES_EVERY_S;
			}
			break;
		}
	}
}

float traps_take_damage(void) {
	const float d = damage_out;
	damage_out = 0.0f;
	return d;
}

int traps_sprites(ObjectsSprite *out, int max) {
	int n = 0;
	static float t;
	t += frame_seconds();
	for (int i = 0; i < trap_count && n < max; i++) {
		const Trap *tr = &traps[i];
		if (tr->kind == T_OIL && !tr->active && !tr->spent) {
			// Unlit: Skyrim's oil has a sheen - a few soft glints drifting over it
			for (int k = 0; k < 3 && n < max; k++) {
				const float u = 0.5f + 0.35f * sinf(t * (0.31f + 0.07f * k) + k * 2.1f);
				const float v = 0.5f + 0.35f * sinf(t * (0.23f + 0.05f * k) + k * 1.3f + 0.7f);
				const float l[3] = {tr->lo[0] + (tr->hi[0] - tr->lo[0]) * u, tr->hi[1] + 0.02f,
				                    tr->lo[2] + (tr->hi[2] - tr->lo[2]) * v};
				ObjectsSprite *s = &out[n++];
				memset(s, 0, sizeof(*s));
				to_world(tr, l, s->pos);
				s->size = 0.5f + 0.15f * sinf(t * 1.7f + k), s->kind = SPRITE_GLOW;
				const float glint = 0.55f + 0.45f * sinf(t * 2.3f + k * 2.0f);
				s->rgba[0] = (uint8_t)(70 * glint), s->rgba[1] = (uint8_t)(64 * glint), s->rgba[2] = (uint8_t)(48 * glint);
				s->rgba[3] = 255;
			}
			continue;
		}
		if (tr->kind != T_OIL || !tr->active)
			continue;
		// Flames over the pool, flickering, dying down at the end
		const float left = fminf(1.0f, (OIL_BURN_S - tr->t_active) / 3.0f);
		for (int k = 0; k < 9 && n < max; k++) {
			const float fx = tr->lo[0] + (tr->hi[0] - tr->lo[0]) * (0.2f + 0.3f * (k % 3));
			const float fz = tr->lo[2] + (tr->hi[2] - tr->lo[2]) * (0.2f + 0.3f * (k / 3));
			const float l[3] = {fx, tr->hi[1] + 0.35f, fz};
			ObjectsSprite *s = &out[n++];
			memset(s, 0, sizeof(*s));
			to_world(tr, l, s->pos);
			s->size = (0.8f + 0.25f * sinf(t * 7.0f + k)) * left;
			s->kind = SPRITE_FIRE, s->frame = (uint8_t)((int)(t * 12.0f + k * 3) & 7);
			s->rgba[0] = 255, s->rgba[1] = 210, s->rgba[2] = 160, s->rgba[3] = 255;
		}
	}
	return n;
}

int traps_aim_oil(const float eye[3], const float dir[3], float *along, const char **label) {
	int best = -1;
	float best_t = OIL_LABEL_M;
	for (int i = 0; i < trap_count; i++) {
		const Trap *tr = &traps[i];
		if (tr->kind != T_OIL || tr->active || tr->spent || dir[1] > -0.05f)
			continue;
		// (where the look meets the pool's surface)
		const float top = tr->centre[1] + 0.05f, t = (top - eye[1]) / dir[1];
		if (t <= 0.0f || t > best_t)
			continue;
		const float at[3] = {eye[0] + dir[0] * t, top, eye[2] + dir[2] * t};
		if (over(tr, at, 0.0f, 1.0f))
			best = i, best_t = t;
	}
	if (best >= 0)
		*along = best_t, *label = "~Oil (flammable)";
	return best;
}
