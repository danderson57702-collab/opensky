#include "frametime.h"
#include "weather.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmosphere.h"
#include "fileio.h"
#include "lighting.h"
#include "objects.h"

#define MAX_REGIONS 64
#define MAX_AREAS 160
#define MAX_POINTS 4096
#define MAX_WEATHERS 8
#define CHANGE_EVERY_H 6.0f // a new weather picked every this many game hours...
#define CHANGE_S 30.0f      // ...and the sky goes over to it in this many seconds
#define DROPS 300           // rain streaks / snowflakes around the camera
#define BOX_M 12.0f         // ...within this far of it

enum { W_CLEAR, W_CLOUDY, W_RAIN, W_SNOW, W_FOG, W_KINDS };
static const char *const NAMES[W_KINDS] = {"Clear", "Cloudy", "Rain", "Snow", "Fog"};
// Per kind: how overcast, how thick the haze, rain, snow
static const float LOOK[W_KINDS][4] = {
	{0.0f, 1.0f, 0.0f, 0.0f}, {0.45f, 1.25f, 0.0f, 0.0f}, {0.8f, 1.9f, 1.0f, 0.0f},
	{0.7f, 2.2f, 0.0f, 1.0f}, {0.55f, 4.0f, 0.0f, 0.0f}};

typedef struct {
	int priority, count;
	int kind[MAX_WEATHERS], chance[MAX_WEATHERS];
} Region;

typedef struct {
	int region, first, count; // its points in pts[]
	float lo[2], hi[2];
} Area;

static Region regions[MAX_REGIONS], fallback = {0, 1, {W_CLEAR}, {100}};
static int region_count;
static Area areas[MAX_AREAS];
static int area_count;
static float pts[MAX_POINTS][2];
static int pt_count;
static int current_region = -2, kind = W_CLEAR;
static float since_h = 1e9f, look[4] = {0.0f, 1.0f, 0.0f, 0.0f};
static unsigned rng = 0x9e3779b9u;

static int kind_of(const char *s) {
	for (int k = 0; k < W_KINDS; k++)
		if (!strncmp(s, NAMES[k], strlen(NAMES[k])) || (s[0] == NAMES[k][0] + 32 && !strncmp(s + 1, NAMES[k] + 1, strlen(NAMES[k]) - 1)))
			return k;
	return W_CLEAR;
}

// "kind:chance kind:chance ..." into a region
static void parse_weathers(const char *p, Region *r) {
	char word[24];
	int chance, used;
	r->count = 0;
	while (r->count < MAX_WEATHERS && sscanf(p, " %23[a-z]:%d%n", word, &chance, &used) == 2) {
		r->kind[r->count] = kind_of(word), r->chance[r->count] = chance, r->count++;
		p += used;
	}
}

void weather_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sweather.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		if (line[0] == 'D' && line[1] == ' ') {
			parse_weathers(line + 2, &fallback);
		} else if (line[0] == 'R' && line[1] == ' ' && region_count < MAX_REGIONS) {
			Region *r = &regions[region_count++];
			int used = 0;
			sscanf(line + 2, "%d%n", &r->priority, &used);
			parse_weathers(line + 2 + used, r);
		} else if (line[0] == 'P' && line[1] == ' ' && region_count && area_count < MAX_AREAS) {
			Area *a = &areas[area_count];
			a->region = region_count - 1, a->first = pt_count, a->count = 0;
			a->lo[0] = a->lo[1] = 1e9f, a->hi[0] = a->hi[1] = -1e9f;
			const char *p = line + 2;
			float x, z;
			int used;
			while (pt_count < MAX_POINTS && sscanf(p, " %f %f%n", &x, &z, &used) == 2) {
				pts[pt_count][0] = x, pts[pt_count][1] = z, pt_count++, a->count++;
				a->lo[0] = fminf(a->lo[0], x), a->hi[0] = fmaxf(a->hi[0], x);
				a->lo[1] = fminf(a->lo[1], z), a->hi[1] = fmaxf(a->hi[1], z);
				p += used;
			}
			if (a->count >= 3)
				area_count++;
			else
				pt_count = a->first;
		}
	}
	free(buf);
}

static int inside(const Area *a, float x, float z) {
	if (x < a->lo[0] || x > a->hi[0] || z < a->lo[1] || z > a->hi[1])
		return 0;
	int in = 0;
	for (int i = 0, j = a->count - 1; i < a->count; j = i++) {
		const float *pi = pts[a->first + i], *pj = pts[a->first + j];
		if ((pi[1] > z) != (pj[1] > z) && x < (pj[0] - pi[0]) * (z - pi[1]) / (pj[1] - pi[1]) + pi[0])
			in = !in;
	}
	return in;
}

static void pick(const Region *r) {
	int total = 0;
	for (int i = 0; i < r->count; i++)
		total += r->chance[i];
	rng = rng * 1664525u + 1013904223u;
	int roll = total > 0 ? (int)((rng >> 8) % (unsigned)total) : 0;
	for (int i = 0; i < r->count; i++) {
		if (roll < r->chance[i]) {
			kind = r->kind[i];
			return;
		}
		roll -= r->chance[i];
	}
	kind = r->count ? r->kind[0] : W_CLEAR;
}

static float cleared_s; // (Clear Skies: the sky held clear this long yet)

void weather_clear_for(float seconds) {
	cleared_s = seconds;
}

void weather_update(float x, float z, int outdoors, float dt_s, float hours_passed) {
	// The region here: the highest priority one whose area holds you
	int best = -1;
	for (int i = 0; i < area_count; i++)
		if ((best < 0 || regions[areas[i].region].priority > regions[best].priority) && inside(&areas[i], x, z))
			best = areas[i].region;
	since_h += hours_passed;
	const int first = current_region == -2;
	if (best != current_region || since_h >= CHANGE_EVERY_H) {
		pick(best >= 0 ? &regions[best] : &fallback);
		current_region = best, since_h = 0.0f;
		if (first)
			memcpy(look, LOOK[kind], sizeof(look));
	}
	// The sky going over to it (at once after a wait: the hours went by; quickly, Clear Skies')
	if (cleared_s > 0.0f)
		cleared_s -= dt_s;
	const int to = cleared_s > 0.0f ? W_CLEAR : kind;
	const float k = hours_passed > 0.5f ? 1.0f : fminf(1.0f, dt_s / (cleared_s > 0.0f ? 2.0f : CHANGE_S) * 3.0f);
	for (int i = 0; i < 4; i++)
		look[i] += (LOOK[to][i] - look[i]) * k;
	lighting_set_weather(outdoors ? look[0] : 0.0f);
	atmosphere_set_weather_fog(look[1]);
}

const char *weather_name(void) {
	return NAMES[kind];
}

static float density = 1.0f; // (the performance setting's share of the drops)

void weather_set_density(float share) {
	density = share < 0.1f ? 0.1f : share > 1.0f ? 1.0f : share;
}

void weather_draw(const float cam[3], float yaw_deg, float pitch_deg) {
	static ObjectsSprite drops[DROPS];
	static float seed[DROPS][4]; // x, y, z (in the box, metres), phase
	static int seeded;
	static float t;
	if (!seeded) {
		for (int i = 0; i < DROPS; i++)
			for (int k = 0; k < 4; k++) {
				rng = rng * 1664525u + 1013904223u;
				seed[i][k] = (rng >> 8) * (1.0f / 16777216.0f);
			}
		seeded = 1;
	}
	t += frame_seconds();
	const float rain = look[2], snow = look[3];
	if (rain < 0.05f && snow < 0.05f)
		return;
	const int n = (int)(DROPS * fmaxf(rain, snow) * density);
	const float tint = lighting_sky()->tint[1]; // (darker at night)
	for (int i = 0; i < n; i++) {
		ObjectsSprite *s = &drops[i];
		memset(s, 0, sizeof(*s));
		// Falling through a box that moves with the camera (wrapping round it)
		const float fall = snow > rain ? 1.1f : 11.0f;
		float y = fmodf(seed[i][1] * BOX_M * 2.0f - t * fall * (0.8f + 0.4f * seed[i][3]), BOX_M * 2.0f);
		if (y < 0.0f)
			y += BOX_M * 2.0f;
		float drift = snow > rain ? sinf(t * 0.7f + seed[i][3] * 6.28f) * 0.6f : 0.0f;
		float wx = fmodf(seed[i][0] * BOX_M * 2.0f + drift - cam[0], BOX_M * 2.0f);
		float wz = fmodf(seed[i][2] * BOX_M * 2.0f - cam[2], BOX_M * 2.0f);
		if (wx < 0.0f)
			wx += BOX_M * 2.0f;
		if (wz < 0.0f)
			wz += BOX_M * 2.0f;
		s->pos[0] = cam[0] + wx - BOX_M, s->pos[1] = cam[1] + y - BOX_M, s->pos[2] = cam[2] + wz - BOX_M;
		s->kind = SPRITE_WHITE;
		if (snow > rain) {
			s->size = 0.05f + 0.03f * seed[i][3];
			const float b = 150.0f * tint * snow;
			s->rgba[0] = s->rgba[1] = s->rgba[2] = (uint8_t)fminf(255.0f, b), s->rgba[3] = 255;
		} else {
			s->size = 0.015f, s->stretch = 28.0f;
			const float b = 70.0f * tint * rain;
			s->rgba[0] = (uint8_t)fminf(255.0f, b * 0.8f), s->rgba[1] = (uint8_t)fminf(255.0f, b * 0.85f);
			s->rgba[2] = (uint8_t)fminf(255.0f, b), s->rgba[3] = 255;
		}
	}
	objects_draw_sprites(drops, n, yaw_deg, pitch_deg);
}

const char *weather_cycle(void) {
	kind = (kind + 1) % W_KINDS;
	memcpy(look, LOOK[kind], sizeof(look));
	since_h = 0.0f; // (stays a while)
	return NAMES[kind];
}
