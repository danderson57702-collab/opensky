#include "frametime.h"
#include "spellfx.h"

#include <math.h>
#include <string.h>

#include "objects.h"

#define PARTICLES 400
#define FLAME_SPEED_MS 9.0f      // fireballs flowing out of the hand
#define FLAME_RATE 80.0f         // per second and hand
#define FLAME_SPREAD 0.10f       // sideways, of the speed
#define FLAME_START_M 0.07f      // a fireball's size leaving the hand...
#define FLAME_END_M 0.42f        // ... and where the stream stops (plus a little per metre)
#define FLAME_FPS 30.0f
#define BURN_RATE 30.0f          // per second: fire where the stream hits
#define HEAL_BURST_S 0.9f // Fast Healing's burst of light about you, this long
#define HEAL_SPARK_RATE 26.0f    // per second: sparkles around the hands (and body)
#define RING_M 1.35f             // Healing's circle (third person), across
#define MAX_SPRITES 520

enum { P_FLAME, P_BURN, P_SPARK, P_PUFF }; // (P_PUFF: a soft glow of its own colour)

typedef struct {
	float pos[3], vel[3];
	float age, life, size0, size1, angle, spin;
	unsigned char kind, frame0;
	unsigned char rgb[3]; // (P_PUFF)
	float drag;           // (P_PUFF: slowing a second)
} Particle;

static Particle parts[PARTICLES];
// A shout's look (spellfx_shout): a glowing aura about the body, a ring rolling out over the
// ground, rings of force (or a breath) travelling out ahead, a flare at the mouth
#define MAX_SHOCKS 4
static struct {
	float t, life, at[3], dir[3], range, strength;
	int rgb[3];
} shocks[MAX_SHOCKS];
static int part_count;
static float flame_acc[2], burn_acc, spark_acc;
static ObjectsSprite world[MAX_SPRITES], hands[96];
static int nworld, nhands;
static void soul_streams(const float from[3], float spread, const float to[3], float t);
static struct { float at[3], strength; int on; } wall_ask; // (a word wall's word glowing: drawn in spellfx_update)
static struct { float at[3], strength; int on; } floor_ask; // (a Greybeard's word shouted onto the floor: lying there, glowing)
static struct { float from[3], to[3], spread, t; int on; } soul_ask; // (asked during the frame: drawn in spellfx_update)

static unsigned rng = 0x2545f491u;

static float frand(void) {
	rng = rng * 1664525u + 1013904223u;
	return (rng >> 8) * (1.0f / 16777216.0f);
}

static float srand1(void) {
	return frand() * 2.0f - 1.0f;
}

static int particle_cap = PARTICLES; // (the performance setting's)

void spellfx_set_particle_cap(int n) {
	particle_cap = n < 50 ? 50 : n > PARTICLES ? PARTICLES : n;
}

static Particle *spawn(int kind) {
	if (part_count >= particle_cap)
		return NULL;
	Particle *p = &parts[part_count++];
	memset(p, 0, sizeof(*p));
	p->kind = (unsigned char)kind;
	p->frame0 = (unsigned char)(frand() * 32.0f);
	p->angle = frand() * 6.2831853f;
	p->spin = srand1() * 2.5f;
	return p;
}

static ObjectsSprite *sprite(ObjectsSprite *list, int *n, int cap, int kind, const float pos[3], float size) {
	if (*n >= cap)
		return NULL;
	ObjectsSprite *s = &list[(*n)++];
	memset(s, 0, sizeof(*s));
	memcpy(s->pos, pos, sizeof(s->pos));
	s->size = size;
	s->kind = (uint8_t)kind;
	s->rgba[3] = 255;
	return s;
}

static void tint(ObjectsSprite *s, float r, float g, float b, float a) {
	if (!s)
		return;
	s->rgba[0] = (uint8_t)fminf(255.0f, r), s->rgba[1] = (uint8_t)fminf(255.0f, g);
	s->rgba[2] = (uint8_t)fminf(255.0f, b), s->rgba[3] = (uint8_t)fminf(255.0f, fmaxf(0.0f, a));
}

// Fireballs from a palm toward the stream's end, a little spread (wide: third person, where
// a stream seen from behind would shrink to a blob)
static void emit_flames(int h, const float from[3], const float to[3], float dt, int wide) {
	float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
	float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (dist < 0.05f)
		return;
	for (int k = 0; k < 3; k++)
		d[k] /= dist;
	flame_acc[h] += dt * FLAME_RATE;
	while (flame_acc[h] >= 1.0f) {
		flame_acc[h] -= 1.0f;
		Particle *p = spawn(P_FLAME);
		if (!p)
			break;
		const float back = frand() * dt; // (spread through the frame: no clumps)
		for (int k = 0; k < 3; k++) {
			p->vel[k] = (d[k] + srand1() * FLAME_SPREAD * (wide ? 1.8f : 1.0f)) * FLAME_SPEED_MS;
			p->pos[k] = from[k] + p->vel[k] * back;
		}
		p->life = dist / FLAME_SPEED_MS;
		p->age = back;
		p->size0 = FLAME_START_M;
		p->size1 = (FLAME_END_M + 0.06f * dist) * (wide ? 2.0f : 1.0f);
	}
}

static Particle *puff(const float pos[3], const float vel[3], float life, float size0, float size1, int r, int g, int b,
                      float drag);

// Frostbite: cold mist streaming from a palm to where it stops
static void emit_frost(int h, const float from[3], const float to[3], float dt) {
	static float acc[2];
	float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
	const float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (dist < 0.05f)
		return;
	for (int k = 0; k < 3; k++)
		d[k] /= dist;
	const float speed = 9.0f;
	acc[h] += dt * 70.0f;
	while (acc[h] >= 1.0f) {
		acc[h] -= 1.0f;
		float v[3];
		for (int k = 0; k < 3; k++)
			v[k] = (d[k] + srand1() * 0.09f) * speed;
		if (!puff(from, v, dist / speed, 0.05f, 0.3f + 0.07f * dist, 105, 140, 185, 0.0f))
			break;
	}
}

static void update_particles(float dt) {
	int n = 0;
	for (int i = 0; i < part_count; i++) {
		Particle *p = &parts[i];
		p->age += dt;
		if (p->age >= p->life)
			continue;
		for (int k = 0; k < 3; k++)
			p->pos[k] += p->vel[k] * dt;
		if (p->kind == P_BURN) // (burning out: slows, rises)
			p->vel[0] *= 1.0f - 3.0f * dt, p->vel[2] *= 1.0f - 3.0f * dt, p->vel[1] += 1.2f * dt;
		if (p->kind == P_PUFF && p->drag > 0.0f)
			for (int k = 0; k < 3; k++)
				p->vel[k] *= fmaxf(0.0f, 1.0f - p->drag * dt);
		p->angle += p->spin * dt;
		parts[n++] = *p;
	}
	part_count = n;
}

// The readied spell in a hand: idle, or glowing while cast
static void hand_fx(ObjectsSprite *list, int *n, int cap, const MagicHand *fx, const float at[3], int h, float t) {
	const float flicker = 0.85f + 0.15f * sinf(t * 17.0f + h * 2.1f) * sinf(t * 9.3f + h);
	const float grow = fminf(1.0f, fx->cast_s / 0.25f);
	if (fx->spell == SPELL_FLAMES) {
		const int casting = fx->flames;
		float base[3] = {at[0], at[1] + (casting ? 0.02f : 0.05f), at[2]};
		ObjectsSprite *s = sprite(list, n, cap, SPRITE_FIRE, base, (casting ? 0.16f : 0.11f) * flicker);
		if (s) {
			s->frame = (uint8_t)((int)(t * 12.0f) + h * 3);
			tint(s, 135 * flicker, 115 * flicker, 85 * flicker, 255);
		}
		s = sprite(list, n, cap, SPRITE_FLAME, at, casting ? 0.17f : 0.09f);
		if (s) {
			s->frame = (uint8_t)((int)(t * FLAME_FPS) + h * 11);
			s->angle = t * 1.7f + h;
			tint(s, 130, 115, 95, casting ? 230 : 170);
		}
		tint(sprite(list, n, cap, SPRITE_GLOW, at, (casting ? 0.34f : 0.2f) * flicker), 70, 45, 22, 255);
	} else if (fx->spell == SPELL_HEALING) {
		if (fx->healing) {
			// A ball of golden light with gleams turning about it
			tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.26f * grow * flicker), 130, 112, 62, 255);
			tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.1f * grow), 128, 128, 110, 255);
			ObjectsSprite *r = sprite(list, n, cap, SPRITE_RAYS, at, 0.5f * grow * (0.9f + 0.1f * flicker));
			if (r) {
				r->angle = t * 0.7f + h;
				tint(r, 105, 90, 45, 200);
			}
			r = sprite(list, n, cap, SPRITE_RAYS, at, 0.36f * grow);
			if (r) {
				r->angle = -t * 1.1f + h * 2.0f;
				tint(r, 110, 100, 60, 170);
			}
		} else {
			tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.12f * flicker), 90, 78, 42, 255);
		}
	} else if (fx->spell == SPELL_CANDLELIGHT) {
		tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.13f * flicker), 78, 84, 105, 255);
		tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.05f), 120, 124, 130, 255);
	} else if (fx->spell == SPELL_SPARKS) {
		// Arcs crackling over the palm (more while it streams), a violet glow
		const float k = fx->sparks ? 1.0f : 0.65f;
		tint(sprite(list, n, cap, SPRITE_WHITE, at, (fx->sparks ? 0.24f : 0.13f) * flicker), 60 * k, 50 * k, 125 * k, 255);
		for (int a = 0; a < (fx->sparks ? 2 : 1); a++) {
			ObjectsSprite *s = sprite(list, n, cap, SPRITE_ARCS, at, (fx->sparks ? 0.26f : 0.17f) * (1.0f - 0.2f * a));
			if (s) {
				s->frame = (uint8_t)((int)(t * 24.0f) + h * 3 + a * 2);
				s->angle = t * 9.0f * (a ? -1.0f : 1.0f) + h;
				tint(s, 170 * k, 165 * k, 255 * k, 255);
			}
		}
	} else if (fx->spell == SPELL_FIRERUNE) {
		tint(sprite(list, n, cap, SPRITE_GLOW, at, 0.2f * flicker), 85, 40, 18, 255);
		ObjectsSprite *r = sprite(list, n, cap, SPRITE_RAYS, at, 0.18f);
		if (r) {
			r->angle = t * 1.3f + h;
			tint(r, 120, 60, 25, 200);
		}
	} else if (fx->spell == SPELL_MUFFLE) {
		tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.14f * flicker), 70, 40, 95, 255);
	} else if (fx->spell == SPELL_BOUNDSWORD) {
		tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.14f * flicker), 60, 85, 120, 255);
		tint(sprite(list, n, cap, SPRITE_WHITE, at, 0.05f), 120, 140, 170, 255);
	}
}

static float view_yaw, view_pitch; // (the camera: bolts are drawn along their segments)

void spellfx_set_view(float yaw_deg, float pitch_deg) {
	view_yaw = yaw_deg * (float)M_PI / 180.0f, view_pitch = pitch_deg * (float)M_PI / 180.0f;
}

// A streak from a to b: a sprite of this width stretched along it, turned in the view to lie
// along the segment as the camera sees it
static ObjectsSprite *streak(int kind, const float a[3], const float b[3], float width) {
	const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
	const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len < 1e-3f)
		return NULL;
	const float cy = cosf(view_yaw), sy = sinf(view_yaw), sp = sinf(view_pitch), cp = cosf(view_pitch);
	const float dr = d[0] * cy + d[2] * sy, du = -d[0] * sy * sp + d[1] * cp + d[2] * cy * sp;
	const float mid[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
	ObjectsSprite *s = sprite(world, &nworld, MAX_SPRITES, kind, mid, width);
	if (s) {
		const float seen = sqrtf(dr * dr + du * du); // (its length as seen: not foreshortened away)
		s->angle = atan2f(-dr, du);
		s->stretch = fmaxf(1.0f, fmaxf(seen, len * 0.35f) / width);
	}
	return s;
}

// Sparks: Skyrim's lightning strips along a jagged path from the palm to where it strikes
// (the path jumps every few hundredths of a second), forks off it, the crackle where it hits
#define BOLT_POINTS 6
static void lightning(int h, const float from[3], const float to[3], int hit, int wide) {
	static float path[2][BOLT_POINTS][3], fork[2][2][2][3], age[2] = {1.0f, 1.0f};
	static unsigned char frame[2][BOLT_POINTS];
	float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
	const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len < 0.05f)
		return;
	age[h] += frame_seconds();
	if (age[h] > 0.06f) { // a new path
		age[h] = 0.0f;
		for (int i = 0; i < BOLT_POINTS; i++) {
			const float u = (float)i / (BOLT_POINTS - 1), sway = sinf(u * 3.14159f) * len * 0.09f;
			for (int k = 0; k < 3; k++)
				path[h][i][k] = from[k] + d[k] * u + (i && i < BOLT_POINTS - 1 ? srand1() * sway : 0.0f);
			frame[h][i] = (unsigned char)(frand() * 2.0f);
		}
		for (int f = 0; f < 2; f++) { // forks: off a middle point, a short way out
			const int at = 1 + (int)(frand() * (BOLT_POINTS - 2));
			const float l = (0.15f + frand() * 0.25f) * len;
			for (int k = 0; k < 3; k++) {
				fork[h][f][0][k] = path[h][at][k];
				fork[h][f][1][k] = path[h][at][k] + (d[k] / len * 0.5f + srand1() * 0.8f) * l;
			}
		}
	}
	const float w = wide ? 1.4f : 1.0f;
	for (int i = 0; i + 1 < BOLT_POINTS; i++) {
		ObjectsSprite *s = streak(SPRITE_BOLT, path[h][i], path[h][i + 1], 0.32f * w);
		if (s)
			s->frame = frame[h][i], tint(s, 175, 170, 255, 255);
		tint(streak(SPRITE_WHITE, path[h][i], path[h][i + 1], 0.16f * w), 70, 60, 140, 255); // (its glow)
	}
	for (int f = 0; f < 2; f++) {
		ObjectsSprite *s = streak(SPRITE_BOLT, fork[h][f][0], fork[h][f][1], 0.2f * w);
		if (s)
			s->frame = (uint8_t)f, tint(s, 140, 135, 230, 220);
	}
	if (hit) {
		ObjectsSprite *s = sprite(world, &nworld, MAX_SPRITES, SPRITE_ARCS, to, 0.7f + frand() * 0.3f);
		if (s) {
			s->frame = (uint8_t)(frand() * 4.0f), s->angle = frand() * 6.28f;
			tint(s, 170, 165, 255, 255);
		}
		tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_WHITE, to, 0.9f), 70, 55, 150, 255);
	}
}

static void guide_update(float dt);

void spellfx_update(float dt, float t, const MagicFx *fx, const float palms[2][3], int third_person,
                    const float feet[3]) {
	nworld = nhands = 0;
	if (wall_ask.on) { // (a word wall's word: Skyrim's blue glow, pulsing)
		static float ph;
		ph += dt;
		const float pulse = 0.75f + 0.25f * sinf(ph * 2.2f), s = wall_ask.strength;
		for (int k = 0; k < 9; k++) { // (a soft cluster over the word's place: across and up the wall)
			const float ang = (float)k * 0.785f, r = k ? 0.7f : 0.0f;
			const float at[3] = {wall_ask.at[0] + cosf(ang) * r, wall_ask.at[1] + 2.0f + sinf(ang) * r * 0.8f,
			                     wall_ask.at[2] + sinf(ang) * r * 0.3f};
			tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_GLOW, at, (k ? 2.6f : 4.0f) * (0.8f + 0.6f * s) * pulse),
			     70.0f + 70.0f * s, 150.0f + 60.0f * s, 255.0f, 150.0f + 105.0f * s);
		}
		wall_ask.on = 0;
	}
	if (floor_ask.on) { // (a word on the floor: a flat ring of glow just over it, pulsing - not the wall's, up a wall)
		static float ph;
		ph += dt;
		const float pulse = 0.8f + 0.2f * sinf(ph * 2.6f), s = floor_ask.strength;
		for (int k = 0; k < 9; k++) {
			const float ang = (float)k * 0.785f + ph * 0.3f, r = k ? 0.45f : 0.0f;
			const float at[3] = {floor_ask.at[0] + cosf(ang) * r, floor_ask.at[1] + 0.08f, floor_ask.at[2] + sinf(ang) * r};
			tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_GLOW, at, (k ? 0.45f : 0.8f) * (0.8f + 0.4f * s) * pulse),
			     40.0f + 30.0f * s, 110.0f + 40.0f * s, 255.0f, 80.0f + 40.0f * s); // (a soft halo: the letters - wordfx - are the word)
		}
		floor_ask.on = 0;
	}
	if (soul_ask.on) // (a dragon's soul coming to you: its streams, this frame)
		soul_streams(soul_ask.from, soul_ask.spread, soul_ask.to, soul_ask.t), soul_ask.on = 0;
	update_particles(dt);
	guide_update(dt);
	for (int i = 0; i < MAX_SHOCKS; i++) { // shouts under way
		if (shocks[i].life <= 0.0f || (shocks[i].t += dt) >= shocks[i].life) {
			shocks[i].life = 0.0f;
			continue;
		}
		const float u = shocks[i].t / shocks[i].life, fade = (1.0f - u) * (1.0f - u), k = shocks[i].strength;
		const int *c = shocks[i].rgb;
		const float *at = shocks[i].at, *d = shocks[i].dir;
		// (the aura: a halo about the body, flaring then fading)
		const float chest[3] = {at[0], at[1] - 0.4f, at[2]};
		ObjectsSprite *h = sprite(world, &nworld, MAX_SPRITES, SPRITE_WHITE, chest, 1.4f + 0.9f * k + u * 0.8f);
		tint(h, c[0] * 0.5f, c[1] * 0.5f, c[2] * 0.5f, 255 * fade);
		h = sprite(world, &nworld, MAX_SPRITES, SPRITE_RAYS, at, (0.6f + 0.6f * k) * (0.5f + u));
		if (h)
			h->angle = t * 2.0f, tint(h, c[0], c[1], c[2], 255 * fade);
		// (a ring rolling out over the ground about you)
		const float feet_at[3] = {at[0], at[1] - 1.55f, at[2]};
		ObjectsSprite *r = sprite(world, &nworld, MAX_SPRITES, SPRITE_RING, feet_at, 0.8f + u * (3.0f + 3.0f * k));
		if (r)
			r->flat = 1, r->angle = t, tint(r, c[0] * 0.7f, c[1] * 0.7f, c[2] * 0.7f, 255 * fade);
		// (rings of force travelling out ahead, swelling as they go)
		for (int w = 0; w < 3; w++) {
			const float v = u * 1.4f - w * 0.18f;
			if (v <= 0.0f || v >= 1.0f)
				continue;
			const float p[3] = {at[0] + d[0] * shocks[i].range * v, at[1] + d[1] * shocks[i].range * v - 0.1f,
			                    at[2] + d[2] * shocks[i].range * v};
			ObjectsSprite *g = sprite(world, &nworld, MAX_SPRITES, SPRITE_RING, p, 0.6f + v * (2.0f + 2.5f * k));
			if (g)
				g->angle = t * 1.5f + w, tint(g, c[0] * 0.8f, c[1] * 0.8f, c[2] * 0.8f, 255 * (1.0f - v) * (1.0f - v));
		}
	}
	const MagicHand *hand = fx->hand;
	const int flaming = hand[0].flames + hand[1].flames, healing = hand[0].healing + hand[1].healing;
	float heal_cast_s = fmaxf(hand[0].healing ? hand[0].cast_s : 0.0f, hand[1].healing ? hand[1].cast_s : 0.0f);
	for (int h = 0; h < 2; h++) // (Frostbite's stream)
		if (hand[h].frost)
			emit_frost(h, palms[h], fx->flame_to, dt);
	// Flames: the stream from each casting hand, burning where it stops
	if (flaming) {
		for (int h = 0; h < 2; h++)
			if (hand[h].flames)
				emit_flames(h, palms[h], fx->flame_to, dt, third_person);
		if (fx->flame_hit) {
			burn_acc += dt * BURN_RATE * (flaming == 2 ? 1.5f : 1.0f);
			while (burn_acc >= 1.0f) {
				burn_acc -= 1.0f;
				Particle *p = spawn(P_BURN);
				if (!p)
					break;
				for (int k = 0; k < 3; k++)
					p->pos[k] = fx->flame_to[k] + srand1() * 0.12f, p->vel[k] = srand1() * 1.2f;
				p->vel[1] = 0.4f + frand() * 0.8f;
				p->life = 0.3f + frand() * 0.25f;
				p->size0 = 0.3f, p->size1 = 0.65f + frand() * 0.3f;
			}
		}
	}
	// Fast Healing's burst (Skyrim's: a golden light swelling about you, a ring spreading at your
	// feet, sparks rising - it was a flash in the hand)
	static float burst_t = -1.0f;
	if (fx->heal_burst)
		burst_t = 0.0f;
	if (burst_t >= 0.0f) {
		const float u = burst_t / HEAL_BURST_S, fade = 1.0f - u, grow = 1.0f - (1.0f - u) * (1.0f - u);
		const float chest[3] = {feet[0], feet[1] + 1.0f, feet[2]}, ground[3] = {feet[0], feet[1] + 0.05f, feet[2]};
		tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_WHITE, chest, 0.6f + 2.6f * grow), 150 * fade, 128 * fade, 60 * fade, 255);
		ObjectsSprite *s = sprite(world, &nworld, MAX_SPRITES, SPRITE_RING, ground, 0.5f + 2.2f * grow);
		if (s)
			s->flat = 1, s->angle = burst_t * 1.5f, tint(s, 160, 136, 64, 255 * fade);
		if (burst_t < 0.25f)
			for (int k = 0; k < 6; k++) { // (sparks rising all round)
				Particle *p = spawn(P_SPARK);
				if (!p)
					break;
				const float a = frand() * 6.2831853f, r = 0.3f + frand() * 0.6f;
				p->pos[0] = feet[0] + cosf(a) * r, p->pos[1] = feet[1] + 0.2f + frand() * 1.2f, p->pos[2] = feet[2] + sinf(a) * r;
				p->vel[0] = cosf(a) * 0.4f, p->vel[1] = 0.9f + frand() * 0.8f, p->vel[2] = sinf(a) * 0.4f;
				p->life = 0.8f + frand() * 0.6f, p->size0 = 0.1f, p->size1 = 0.03f;
				p->frame0 = (unsigned char)(frand() * 16.0f);
			}
		if ((burst_t += dt) > HEAL_BURST_S)
			burst_t = -1.0f;
	}
	// Healing: sparkles drifting off the hands; in third person rising around you too
	if (healing) {
		spark_acc += dt * HEAL_SPARK_RATE;
		while (spark_acc >= 1.0f) {
			spark_acc -= 1.0f;
			Particle *p = spawn(P_SPARK);
			if (!p)
				break;
			int body = third_person && frand() < 0.6f;
			if (body) {
				float a = frand() * 6.2831853f, r = RING_M * 0.42f * (0.8f + 0.2f * frand());
				p->pos[0] = feet[0] + cosf(a) * r, p->pos[1] = feet[1] + 0.05f, p->pos[2] = feet[2] + sinf(a) * r;
				p->vel[1] = 0.6f + frand() * 0.6f;
				p->life = 1.0f + frand() * 0.8f;
				p->size0 = 0.09f, p->size1 = 0.04f;
			} else {
				int h = healing == 2 ? (frand() < 0.5f) : hand[1].healing;
				for (int k = 0; k < 3; k++)
					p->pos[k] = palms[h][k] + srand1() * 0.05f, p->vel[k] = srand1() * 0.25f;
				p->vel[1] += 0.15f;
				p->life = 0.5f + frand() * 0.4f;
				p->size0 = 0.05f, p->size1 = 0.015f;
			}
			p->frame0 = (unsigned char)(frand() * 16.0f);
		}
	}
	// The particles
	for (int i = 0; i < part_count; i++) {
		const Particle *p = &parts[i];
		const float u = p->age / p->life;
		const float size = p->size0 + (p->size1 - p->size0) * u;
		if (p->kind == P_PUFF) { // (frost, a shout's wave, poison: soft, swelling, fading)
			ObjectsSprite *s = sprite(world, &nworld, MAX_SPRITES, SPRITE_GLOW, p->pos, size);
			if (s)
				tint(s, p->rgb[0], p->rgb[1], p->rgb[2], 255 * fminf(1.0f, u / 0.1f) * (1.0f - u));
			continue;
		}
		if (p->kind == P_SPARK) {
			ObjectsSprite *s = sprite(world, &nworld, MAX_SPRITES, SPRITE_SPARKS, p->pos, size);
			if (s) {
				s->frame = p->frame0;
				tint(s, 140, 122, 70, 255 * fminf(1.0f, (1.0f - u) * 3.0f));
			}
			continue;
		}
		// Fireballs: bright as they leave, redder and fading where they stop
		ObjectsSprite *s = sprite(world, &nworld, MAX_SPRITES, SPRITE_FLAME, p->pos, size);
		if (!s)
			break;
		s->frame = (uint8_t)(p->frame0 + (int)(p->age * FLAME_FPS));
		s->angle = p->angle;
		const float fade = fminf(1.0f, u / 0.08f) * fminf(1.0f, (1.0f - u) / (p->kind == P_BURN ? 0.6f : 0.3f));
		tint(s, 150 - 25 * u, 135 - 50 * u, 120 - 65 * u, 255 * fade);
	}
	// Healing: golden wisps circling you, each trailing a fading tail (third person)
	if (healing && third_person) {
		const float grow = fminf(1.0f, heal_cast_s / 0.6f);
		for (int w = 0; w < 3; w++)
			for (int k = 0; k < 5; k++) {
				const float a = t * 2.4f + w * 2.094f - k * 0.12f;
				const float at[3] = {feet[0] + cosf(a) * 0.55f, feet[1] + 0.9f + 0.35f * sinf(t * 1.7f + w * 1.3f) - k * 0.02f,
				                     feet[2] + sinf(a) * 0.55f};
				tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_WHITE, at, (0.14f - k * 0.02f) * grow), 140 - k * 18,
				     115 - k * 15, 50 - k * 6, 255 * grow);
			}
	}
	// Healing's circle at your feet (third person)
	if (healing && third_person) {
		const float grow = fminf(1.0f, heal_cast_s / 0.4f), pulse = 0.85f + 0.15f * sinf(t * 4.0f);
		const float at[3] = {feet[0], feet[1] + 0.04f, feet[2]};
		ObjectsSprite *s = sprite(world, &nworld, MAX_SPRITES, SPRITE_RING, at, RING_M * (0.6f + 0.4f * grow));
		if (s) {
			s->flat = 1, s->angle = t * 0.5f;
			tint(s, 125 * pulse, 106 * pulse, 55 * pulse, 255 * grow);
		}
		s = sprite(world, &nworld, MAX_SPRITES, SPRITE_RING, at, RING_M * 0.72f * grow);
		if (s) {
			s->flat = 1, s->angle = -t * 0.8f;
			tint(s, 110, 95, 50, 160 * grow);
		}
		tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_WHITE, (float[3]){feet[0], feet[1] + 0.9f, feet[2]}, 1.6f * grow),
		     40, 34, 18, 255);
	}
	// Sparks
	for (int h = 0; h < 2; h++)
		if (hand[h].sparks)
			lightning(h, palms[h], fx->spark_to, fx->spark_hit, third_person);
	// Fire Runes: a burning sigil on the ground, turning; a burst where one is set off
	for (int i = 0; i < fx->runes; i++) {
		const float grow = fminf(1.0f, fx->rune_age[i] / 0.5f), pulse = 0.85f + 0.15f * sinf(t * 5.0f + i);
		// Skyrim's fire sigil, burning and slowly turning, over a faint ring of heat
		// (its element's colour: fire, frost, lightning)
		static const float col[3][3] = {{200, 85, 25}, {80, 140, 210}, {140, 120, 240}};
		const float *c = col[fx->rune_element[i] >= 0 && fx->rune_element[i] <= ELEMENT_SHOCK ? fx->rune_element[i] : 0];
		ObjectsSprite *r = sprite(world, &nworld, MAX_SPRITES, SPRITE_RUNE, fx->rune_pos[i], 1.4f * grow);
		if (r) {
			r->flat = 1, r->angle = t * 0.25f + i;
			tint(r, c[0] * pulse, c[1] * pulse, c[2] * pulse, 255 * grow);
		}
		r = sprite(world, &nworld, MAX_SPRITES, SPRITE_RING, fx->rune_pos[i], 1.55f * grow);
		if (r) {
			r->flat = 1, r->angle = -t * 0.4f;
			tint(r, c[0] * 0.45f, c[1] * 0.4f, c[2] * 0.4f, 160 * grow);
		}
		const float up[3] = {fx->rune_pos[i][0], fx->rune_pos[i][1] + 0.15f, fx->rune_pos[i][2]};
		tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_GLOW, up, 0.5f * pulse * grow), c[0] * 0.4f, c[1] * 0.4f,
		     c[2] * 0.4f, 255);
	}
	for (int i = 0; i < fx->bursts; i++)
		if (fx->burst_element[i] != ELEMENT_FIRE) // (a frost or lightning rune's: its own burst)
			spellfx_burst(fx->burst_pos[i], fx->burst_element[i], 1.7f);
	for (int i = 0; i < fx->bursts; i++)
		for (int k = 0; k < 28 && fx->burst_element[i] == ELEMENT_FIRE; k++) {
			Particle *p = spawn(P_BURN);
			if (!p)
				break;
			for (int c = 0; c < 3; c++)
				p->pos[c] = fx->burst_pos[i][c] + srand1() * 0.3f, p->vel[c] = srand1() * 3.0f;
			p->vel[1] = 1.0f + frand() * 3.0f;
			p->life = 0.5f + frand() * 0.4f;
			p->size0 = 0.5f, p->size1 = 1.1f + frand() * 0.5f;
		}
	// Muffle: a dim violet ring drawn in about you as it's cast
	static float muffle_t;
	muffle_t = fx->muffle_cast ? 0.9f : fmaxf(0.0f, muffle_t - dt);
	if (muffle_t > 0.0f) {
		const float u = muffle_t / 0.9f;
		const float at[3] = {feet[0], feet[1] + 0.05f, feet[2]};
		ObjectsSprite *r = sprite(world, &nworld, MAX_SPRITES, SPRITE_RING, at, 0.6f + 1.2f * u);
		if (r) {
			r->flat = 1, r->angle = t;
			tint(r, 90, 50, 120, 255 * u);
		}
	}
	// Candlelight: a cool ball of light with faint rays
	if (fx->candle) {
		const float flicker = 0.9f + 0.1f * sinf(t * 5.0f);
		tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_WHITE, fx->candle_pos, 0.55f * flicker), 85, 90, 115, 255);
		tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_WHITE, fx->candle_pos, 0.16f), 128, 128, 128, 255);
		ObjectsSprite *r = sprite(world, &nworld, MAX_SPRITES, SPRITE_RAYS, fx->candle_pos, 0.7f);
		if (r) {
			r->angle = t * 0.3f;
			tint(r, 45, 48, 62, 255);
		}
	}
	// The readied spell in the hand(s): with the world in third person, over the arms in first
	for (int h = 0; h < 2; h++) {
		if (hand[h].spell < 0)
			continue;
		if (third_person)
			hand_fx(world, &nworld, MAX_SPRITES, &hand[h], palms[h], h, t);
		else
			hand_fx(hands, &nhands, (int)(sizeof(hands) / sizeof(hands[0])), &hand[h], palms[h], h, t);
	}
}

void spellfx_draw_world(float yaw_deg, float pitch_deg) {
	objects_draw_sprites(world, nworld, yaw_deg, pitch_deg);
}

void spellfx_draw_hands(float yaw_deg, float pitch_deg) {
	objects_draw_sprites(hands, nhands, yaw_deg, pitch_deg);
}

// A puff of colour (frost, a shout's wave): rgb, from size0 to size1 over life, slowing
static Particle *puff(const float pos[3], const float vel[3], float life, float size0, float size1, int r, int g, int b,
                      float drag) {
	Particle *p = spawn(P_PUFF);
	if (!p)
		return NULL;
	memcpy(p->pos, pos, sizeof(p->pos));
	memcpy(p->vel, vel, sizeof(p->vel));
	p->life = life, p->size0 = size0, p->size1 = size1, p->drag = drag;
	p->rgb[0] = (unsigned char)r, p->rgb[1] = (unsigned char)g, p->rgb[2] = (unsigned char)b;
	return p;
}

void spellfx_burst(const float pos[3], int element, float radius) {
	const int n = (int)fminf(48.0f, 16.0f + radius * 8.0f);
	for (int k = 0; k < n; k++) {
		float v[3] = {srand1(), srand1() * 0.6f + 0.4f, srand1()};
		const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) + 1e-4f, speed = radius * (1.2f + frand() * 1.4f);
		for (int c = 0; c < 3; c++)
			v[c] = v[c] / l * speed;
		if (element == ELEMENT_FIRE) {
			Particle *p = spawn(P_BURN);
			if (!p)
				break;
			for (int c = 0; c < 3; c++)
				p->pos[c] = pos[c] + srand1() * 0.2f, p->vel[c] = v[c];
			p->life = 0.5f + frand() * 0.4f;
			p->size0 = 0.5f, p->size1 = 0.9f + radius * 0.25f;
			continue;
		}
		static const int rgb[4][3] = {{0, 0, 0}, {120, 170, 220}, {150, 150, 230}, {90, 170, 50}};
		const int e = element >= 1 && element <= 3 ? element : 1;
		puff(pos, v, 0.5f + frand() * 0.5f, 0.3f, 0.8f + radius * 0.2f, rgb[e][0], rgb[e][1], rgb[e][2], 2.5f);
	}
}

void spellfx_soul(const float from[3], float spread, const float to[3], float dt) {
	// (glowing wisps rising off the body and streaming into the player: Skyrim's soul absorption)
	static float acc;
	acc += dt * 70.0f;
	while (acc >= 1.0f) {
		acc -= 1.0f;
		float at[3] = {from[0] + srand1() * spread, from[1] + frand() * 2.0f, from[2] + srand1() * spread};
		float d[3] = {to[0] - at[0], to[1] - at[1], to[2] - at[2]};
		const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-3f, speed = 9.0f + frand() * 6.0f;
		float v[3];
		for (int c = 0; c < 3; c++)
			v[c] = d[c] / l * speed + srand1() * 1.5f;
		v[1] += 2.5f; // (curling up first)
		if (!puff(at, v, l / speed, 0.35f, 0.08f, 230, 130, 45, 0.0f))
			break;
	}
}

void spellfx_word(const float from[3], const float to[3], float dt) {
	static float acc;
	acc += dt * 30.0f; // (90 a second piled up white under a hall's warm light: a blaze, the word lost in it)
	while (acc >= 1.0f) {
		acc -= 1.0f;
		float at[3] = {from[0] + srand1() * 1.2f, from[1] + srand1() * 0.8f, from[2] + srand1() * 1.2f};
		float d[3] = {to[0] - at[0], to[1] - at[1], to[2] - at[2]};
		const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-3f, speed = 3.5f + frand() * 2.0f;
		float v[3];
		for (int c = 0; c < 3; c++)
			v[c] = d[c] / l * speed + srand1() * 0.6f;
		if (!puff(at, v, l / speed, 0.35f, 0.1f, 110, 170, 255, 0.0f))
			break;
	}
}

// A dragon's soul as Skyrim draws it: streams of light out of the body, arcing up and over and
// down into you - each a bright head running along its curve, its tail fading behind it (the
// path there to see, not loose sparks). t: seconds since it began
#define SOUL_STREAMS 8
#define SOUL_STREAM_DOTS 34

void spellfx_soul_stream(const float from[3], float spread, const float to[3], float t) {
	memcpy(soul_ask.from, from, sizeof(soul_ask.from)), memcpy(soul_ask.to, to, sizeof(soul_ask.to));
	soul_ask.spread = spread, soul_ask.t = t, soul_ask.on = 1;
}

static void soul_streams(const float from[3], float spread, const float to[3], float t) {
	for (int k = 0; k < SOUL_STREAMS; k++) {
		// (each its own: where on the body it leaves, how high it arcs, which side it swings)
		const float a = (float)k * 2.39996f, along = sinf(a * 1.7f) * spread, side = cosf(a * 2.3f) * spread * 0.6f;
		const float p0[3] = {from[0] + along, from[1] + 0.5f + 0.4f * sinf(a), from[2] + side};
		const float lift = 5.0f + 3.0f * (0.5f + 0.5f * sinf(a * 3.1f));
		const float swing = 3.0f * cosf(a * 1.3f);
		const float dx = to[0] - p0[0], dz = to[2] - p0[2], dl = sqrtf(dx * dx + dz * dz) + 1e-3f;
		const float nx = -dz / dl, nz = dx / dl; // (across the way: the arc's swing)
		const float p1[3] = {p0[0] + dx * 0.25f + nx * swing, p0[1] + lift, p0[2] + dz * 0.25f + nz * swing};
		const float p2[3] = {to[0] - dx * 0.25f - nx * swing * 0.5f, to[1] + lift * 0.5f, to[2] - dz * 0.25f - nz * swing * 0.5f};
		const float speed = 0.38f + 0.06f * (float)(k % 3);
		for (int i = 0; i < SOUL_STREAM_DOTS; i++) {
			float u = t * speed + (float)k * 0.137f - (float)i * 0.016f;
			u -= floorf(u);
			const float v = 1.0f - u, b0 = v * v * v, b1 = 3.0f * v * v * u, b2 = 3.0f * v * u * u, b3 = u * u * u;
			float at[3];
			for (int c = 0; c < 3; c++)
				at[c] = b0 * p0[c] + b1 * p1[c] + b2 * p2[c] + b3 * to[c];
			const float head = 1.0f - (float)i / SOUL_STREAM_DOTS; // (1 at the head, fading down the tail)
			const float size = 0.16f + 0.3f * head * head;
			tint(sprite(world, &nworld, MAX_SPRITES, SPRITE_GLOW, at, size), 255.0f, 135.0f + 85.0f * head,
			     40.0f + 110.0f * head * head, 90.0f + 165.0f * head);
		}
	}
}

// Clairvoyance: Skyrim's guide (ClairvoyanceHazard, Magic\GuideHazard01.nif) - wisps running
// along the way to what you're after, leaving a glowing mist that hangs a moment and fades
#define GUIDE_WISPS 4
#define GUIDE_POINTS 64
#define GUIDE_SPEED 9.0f
static struct {
	float pts[GUIDE_POINTS][3];
	int n, i;
	float acc;
	int live;
} guides[GUIDE_WISPS];

void spellfx_guide_launch(const float *pts, int n) {
	for (int w = 0; w < GUIDE_WISPS; w++)
		if (!guides[w].live) {
			n = n < GUIDE_POINTS ? n : GUIDE_POINTS;
			memcpy(guides[w].pts, pts, (size_t)n * 3 * sizeof(float));
			guides[w].n = n, guides[w].i = 1, guides[w].acc = 0.0f, guides[w].live = n >= 2;
			return;
		}
}

static void guide_update(float dt) {
	for (int w = 0; w < GUIDE_WISPS; w++) {
		if (!guides[w].live)
			continue;
		float step = GUIDE_SPEED * dt;
		float *at = guides[w].pts[0]; // (its head: the first point, moved along toward the next)
		while (step > 0.0f && guides[w].i < guides[w].n) {
			const float *to = guides[w].pts[guides[w].i];
			const float d[3] = {to[0] - at[0], to[1] - at[1], to[2] - at[2]};
			const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
			if (l <= step) {
				memcpy(at, to, 3 * sizeof(float));
				step -= l, guides[w].i++;
			} else {
				for (int c = 0; c < 3; c++)
					at[c] += d[c] / l * step;
				step = 0.0f;
			}
		}
		guides[w].acc += dt * 35.0f; // (the mist it leaves: a ribbon of it, hanging, fading - within the particle budget)
		while (guides[w].acc >= 1.0f) {
			guides[w].acc -= 1.0f;
			const float p[3] = {at[0] + srand1() * 0.12f, at[1] + srand1() * 0.1f, at[2] + srand1() * 0.12f};
			const float v[3] = {srand1() * 0.12f, 0.05f + frand() * 0.12f, srand1() * 0.12f};
			if (!puff(p, v, 1.8f + frand() * 0.8f, 0.12f, 0.4f, 160, 215, 255, 0.0f))
				break;
		}
		if (guides[w].i >= guides[w].n)
			guides[w].live = 0;
	}
}

void spellfx_wave(const float from[3], const float dir[3], float range, float spread, int element, float strength) {
	// (a cone of it rolling out from the mouth: Unrelenting Force's rippling air, a breath's
	// fire or frost)
	const int n = (int)(20.0f + 30.0f * strength);
	float side[3] = {-dir[2], 0.0f, dir[0]};
	const float sl = hypotf(side[0], side[2]) + 1e-4f;
	side[0] /= sl, side[2] /= sl;
	const float speed = range * 1.6f;
	for (int k = 0; k < n; k++) {
		const float a = srand1() * spread, b = srand1() * spread * 0.6f;
		float v[3];
		for (int c = 0; c < 3; c++)
			v[c] = (dir[c] + side[c] * a) * speed * (0.7f + frand() * 0.5f);
		v[1] += b * speed;
		float at[3];
		for (int c = 0; c < 3; c++)
			at[c] = from[c] + dir[c] * (0.3f + frand() * 0.4f);
		if (element == ELEMENT_FIRE) {
			Particle *p = spawn(P_FLAME);
			if (!p)
				break;
			memcpy(p->pos, at, sizeof(at));
			memcpy(p->vel, v, sizeof(v));
			p->life = range / speed * (0.8f + frand() * 0.4f);
			p->size0 = 0.25f, p->size1 = 0.9f + 0.5f * strength;
			continue;
		}
		const int r = element == ELEMENT_FROST ? 170 : 150, g = element == ELEMENT_FROST ? 210 : 160,
		          bl = element == ELEMENT_FROST ? 255 : 190;
		puff(at, v, range / speed * (0.8f + frand() * 0.4f), 0.2f, 0.9f + 0.6f * strength, r * 0.6f, g * 0.6f, bl * 0.6f,
		     0.6f);
	}
}

void spellfx_shout(const float mouth[3], const float dir[3], float range, int element, float strength) {
	static const int colours[5][3] = {{255, 150, 60}, {170, 215, 255}, {190, 190, 255}, {120, 200, 80}, {200, 220, 255}};
	int slot = 0; // (a free one, else the oldest)
	for (int i = 0; i < MAX_SHOCKS; i++) {
		if (shocks[i].life <= 0.0f) {
			slot = i;
			break;
		}
		if (shocks[i].t > shocks[slot].t)
			slot = i;
	}
	const int e = element >= 0 && element < 5 ? element : ELEMENT_FORCE;
	shocks[slot].t = 0.0f, shocks[slot].life = 0.9f + 0.4f * strength;
	memcpy(shocks[slot].at, mouth, sizeof(shocks[slot].at));
	memcpy(shocks[slot].dir, dir, sizeof(shocks[slot].dir));
	shocks[slot].range = range, shocks[slot].strength = strength;
	memcpy(shocks[slot].rgb, colours[e], sizeof(shocks[slot].rgb));
	// (the halo's swirl: motes thrown off around the body)
	for (int k = 0; k < 18; k++) {
		const float a = k * 6.2831853f / 18.0f;
		const float at[3] = {mouth[0] + cosf(a) * 0.5f, mouth[1] - 0.5f + srand1() * 0.4f, mouth[2] + sinf(a) * 0.5f};
		const float v[3] = {cosf(a) * (1.5f + 2.0f * strength) + dir[0] * 2.0f, 0.4f + frand() * 0.6f,
		                    sinf(a) * (1.5f + 2.0f * strength) + dir[2] * 2.0f};
		puff(at, v, 0.6f + frand() * 0.3f, 0.15f, 0.5f, colours[e][0] / 2, colours[e][1] / 2, colours[e][2] / 2, 2.0f);
	}
}

void spellfx_floor_word(const float at[3], float strength) {
	memcpy(floor_ask.at, at, sizeof(floor_ask.at));
	floor_ask.strength = strength, floor_ask.on = 1;
}

void spellfx_wall_glow(const float at[3], float strength) {
	memcpy(wall_ask.at, at, sizeof(wall_ask.at));
	wall_ask.strength = strength, wall_ask.on = 1;
}
