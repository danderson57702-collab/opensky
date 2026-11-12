#include "wordfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "gamelog.h"
#include "objects.h"

#define MAX_WORDS 32
#define WORD_VERTS 64
#define WORD_TRIS 48
#define QUANT_M 2.0f       // (the letters' extent about their centre: well within this)
#define FADE_S 0.6f        // growing in, and fading at the end
#define LIFT_M 0.03f       // just over the floor
static const char *ALPHABET = "dungeons_dragonrunealphabet_a"; // (its pack name: the word walls' letters)

typedef struct {
	char name[12];
	int nv, nt;
	float pos[WORD_VERTS][3], uv[WORD_VERTS][2];
	uint16_t idx[WORD_TRIS * 3];
} Word;

static Word words[MAX_WORDS];
static int word_count;
static int showing = -1;
static float show_at[3], show_yaw, show_left, show_total, phase;
static unsigned int alphabet;
static ObjectVertex verts[WORD_VERTS];

void wordfx_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sobjects/words.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	Word *w = NULL;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		if (line[0] == 'W' && word_count < MAX_WORDS) {
			w = &words[word_count++];
			memset(w, 0, sizeof(*w));
			sscanf(line + 2, "%11s", w->name);
		} else if (line[0] == 'V' && w && w->nv < WORD_VERTS) {
			float *p = w->pos[w->nv], *t = w->uv[w->nv];
			if (sscanf(line + 2, "%f %f %f %f %f", &p[0], &p[1], &p[2], &t[0], &t[1]) == 5)
				w->nv++;
		} else if (line[0] == 'T' && w && w->nt < WORD_TRIS) {
			int a, b, c;
			if (sscanf(line + 2, "%d %d %d", &a, &b, &c) == 3)
				w->idx[w->nt * 3] = (uint16_t)a, w->idx[w->nt * 3 + 1] = (uint16_t)b, w->idx[w->nt * 3 + 2] = (uint16_t)c, w->nt++;
		}
	}
	free(buf);
	game_log("wordfx: %d words of power", word_count);
}

void wordfx_show(const char *word, const float at[3], float yaw, float seconds) {
	showing = -1;
	for (int i = 0; i < word_count; i++)
		if (!strcasecmp(words[i].name, word))
			showing = i;
	if (showing < 0)
		return;
	memcpy(show_at, at, sizeof(show_at));
	show_yaw = yaw, show_left = show_total = seconds, phase = 0.0f;
}

void wordfx_update(float dt) {
	if (showing < 0)
		return;
	phase += dt;
	if ((show_left -= dt) <= 0.0f)
		showing = -1;
}

void wordfx_draw(void) {
	if (showing < 0)
		return;
	if (!alphabet)
		alphabet = objects_texture(ALPHABET);
	const Word *w = &words[showing];
	// (in, then out: Skyrim's letters flare as they land, glow, and fade as you take them in)
	const float in = fminf(1.0f, (show_total - show_left) / FADE_S), out = fminf(1.0f, show_left / FADE_S);
	const float glow = in * out * (0.85f + 0.15f * sinf(phase * 3.0f));
	const float c = cosf(show_yaw), s = sinf(show_yaw), q = 32767.0f / QUANT_M;
	float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
	for (int i = 0; i < w->nv; i++)
		umin = fminf(umin, w->uv[i][0]), umax = fmaxf(umax, w->uv[i][0]), vmin = fminf(vmin, w->uv[i][1]), vmax = fmaxf(vmax, w->uv[i][1]);
	const float uc = (umin + umax) * 0.5f, vc = (vmin + vmax) * 0.5f, ue = fmaxf(fmaxf(umax - umin, vmax - vmin) * 0.5f, 1e-3f);
	for (int i = 0; i < w->nv; i++) {
		const float *p = w->pos[i];
		const float x = p[0] * c - p[2] * s, z = p[0] * s + p[2] * c;
		ObjectVertex *o = &verts[i];
		o->x = (int16_t)(x * q), o->y = (int16_t)((p[1] + LIFT_M) * q), o->z = (int16_t)(z * q), o->pad = 0;
		o->u = (int16_t)((w->uv[i][0] - uc) / ue * 32767.0f), o->v = (int16_t)((w->uv[i][1] - vc) / ue * 32767.0f);
		o->r = (uint8_t)(70.0f * glow), o->g = (uint8_t)(150.0f * glow), o->b = (uint8_t)(255.0f * glow), o->a = 255; // (the walls' blue)
	}
	ObjectsDynamic d;
	memset(&d, 0, sizeof(d));
	d.vertices = verts, d.vertex_count = w->nv, d.indices = w->idx, d.index_count = w->nt * 3;
	d.tex = alphabet, d.alpha = ALPHA_GLOW;
	d.pos[0] = show_at[0], d.pos[1] = show_at[1], d.pos[2] = show_at[2], d.pos[3] = QUANT_M;
	d.uv[0] = uc, d.uv[1] = vc, d.uv[2] = ue;
	d.tint_mul[0] = d.tint_mul[1] = d.tint_mul[2] = 1.0f;
	objects_draw_dynamic(&d, 1);
}
