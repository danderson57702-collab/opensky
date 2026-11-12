#include "chargen.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include "actors.h"
#include "fileio.h"
#include "gamelog.h"
#include "gametext.h"

#define MAX_PARTS 4096
#define SLOT_HAIR 0x2u // (Skyrim's slot 31, as a helmet or hood covers it: the hair goes under)
#define SLOT_HEAD 0x1u

typedef struct {
	char race[24], eid[48], stem[16];
	short female, kind;
} Part;

static Part parts[MAX_PARTS];
static int part_count;
static char pack[128];
static int worn[CG_KINDS] = {-1, -1, -1, -1, -1}; // (the player's wardrobe items the parts are)

static const char *const KIND_KEYS[CG_KINDS] = {"hair", "beard", "eyes", "brows", "scar"};
static const char *const KIND_NAMES[CG_KINDS] = {"Hair", "Beard", "Eyes", "Brows", T_GMST_000D44BB_DATA0};

void chargen_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%schargen.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	part_count = 0;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line && part_count < MAX_PARTS; line = strtok(NULL, "\n")) {
		Part *p = &parts[part_count];
		char kind[16];
		int female = 0;
		if (sscanf(line, "P|%23[^|]|%d|%15[^|]|%47[^|]|%15s", p->race, &female, kind, p->eid, p->stem) != 5)
			continue;
		p->female = (short)female, p->kind = -1;
		for (int k = 0; k < CG_KINDS; k++)
			if (!strcmp(kind, KIND_KEYS[k]))
				p->kind = (short)k;
		if (p->kind >= 0)
			part_count++;
	}
	free(buf);
	game_log("chargen: %d parts", part_count);
}

const char *chargen_kind_name(int kind) {
	return kind >= 0 && kind < CG_KINDS ? KIND_NAMES[kind] : "";
}

static const Part *nth(const char *race, int female, int kind, int n) {
	for (int i = 0; i < part_count; i++)
		if (parts[i].kind == kind && parts[i].female == female && !strcmp(parts[i].race, race) && n-- == 0)
			return &parts[i];
	return NULL;
}

// Its place among the race's and sex's parts of its kind (-1: not one of them)
static int part_index(const char *race, int female, int kind, const char *eid) {
	int n = 0;
	for (int i = 0; i < part_count; i++)
		if (parts[i].kind == kind && parts[i].female == female && !strcmp(parts[i].race, race)) {
			if (!strcmp(parts[i].eid, eid))
				return n;
			n++;
		}
	return -1;
}

int chargen_count(const char *race, int female, int kind) {
	int n = 0;
	for (int i = 0; i < part_count; i++)
		n += parts[i].kind == kind && parts[i].female == female && !strcmp(parts[i].race, race);
	return n;
}

void chargen_defaults(const char *race, int female, signed char pick[CG_KINDS]) {
	for (int k = 0; k < CG_KINDS; k++)
		pick[k] = (k == CG_BEARD || k == CG_SCAR || !chargen_count(race, female, k)) ? -1 : 0;
}

void chargen_clear(void) {
	for (int k = 0; k < CG_KINDS; k++)
		if (worn[k] >= 0) {
			actors_player_set_worn(worn[k], 0);
			actors_player_drop_gear(worn[k]);
			worn[k] = -1;
		}
}

int chargen_apply(const char *race, int female, signed char pick[CG_KINDS]) {
	chargen_clear();
	int any = 0;
	for (int k = 0; k < CG_KINDS; k++) {
		const int count = chargen_count(race, female, k); // (another race's pick: kept in range)
		if (pick[k] >= count)
			pick[k] = (signed char)(count - 1);
		const Part *p = pick[k] >= 0 ? nth(race, female, k, pick[k]) : NULL;
		if (!p)
			continue;
		char path[256];
		snprintf(path, sizeof(path), "%sactors/head/%s.skgear", pack, p->stem);
		const int item = actors_player_add_gear(path, KIND_NAMES[k], 0, 0);
		if (item < 0) {
			game_log("chargen: %s %s didn't load", KIND_NAMES[k], p->eid);
			continue;
		}
		// (the hair under a helmet or hood: Skyrim's head slots covering it)
		actors_player_set_part(item, k == CG_HAIR ? (SLOT_HAIR | SLOT_HEAD) : 0);
		worn[k] = item, any = 1;
	}
	return any;
}

// --- The race menu's data (converter/chargen.py: pack/chargen/<race>_<m/f>.txt) -------------

#define MAX_LAYERS 96
#define MAX_COLORS 2400
#define MAX_PRESETS 12
#define MAX_HAIR_COLORS 32
#define MAX_COMPLEXIONS 12
#define FACE_SIZE 256
#define SKIN_TONE_LIGHT 0.75f // (converter/npcs.py's: a fair skin, the body texture's own)
#define SKIN_TONE_LEAST (70.0f / 255.0f) // (the masks', the diffuses', the face texture's: converter/chargen.py TINT_SIZE)

typedef struct {
	uint8_t r, g, b, value; // (value 0 .. 100)
} Color;

typedef struct {
	int tini, type;
	char mask[40];
	int def;          // (its default colour: a preset without it has it - Skyrim's TIND, the skin tone)
	int first, count; // (its colours in menu.colors)
	uint8_t *pixels;  // (its mask, when first used)
} Layer;

typedef struct {
	char base[40], detail[40];
	uint8_t *base_px, *detail_px;
} Complexion;

static struct {
	char race[24];
	int female, tried, ok;
	Layer layers[MAX_LAYERS];
	int nlayers;
	Color colors[MAX_COLORS];
	int ncolors;
	uint8_t hair[MAX_HAIR_COLORS][3];
	int nhair;
	Complexion cx[MAX_COMPLEXIONS];
	int ncx;
	uint32_t types[4];
	ChargenLook presets[MAX_PRESETS];
	int npresets;
} menu;

static GLuint face_tex;

static void menu_free(void) {
	for (int i = 0; i < menu.nlayers; i++)
		free(menu.layers[i].pixels);
	for (int i = 0; i < menu.ncx; i++)
		free(menu.cx[i].base_px), free(menu.cx[i].detail_px);
	memset(&menu, 0, sizeof(menu));
}

static const Layer *layer_of(int tini) {
	for (int i = 0; i < menu.nlayers; i++)
		if (menu.layers[i].tini == tini)
			return &menu.layers[i];
	return NULL;
}

static int hex_rgb(const char *s, uint8_t out[3]) {
	unsigned v = 0;
	if (sscanf(s, "%6x", &v) != 1)
		return 0;
	out[0] = (uint8_t)(v >> 16), out[1] = (uint8_t)(v >> 8), out[2] = (uint8_t)v;
	return 1;
}

// A preset's line: S|n|hair colour|complexion|weight|nose,eyes,mouth|18 morphs|kind:part,...|index:colour:value,...
static void read_preset(char *line, const char *race, int female) {
	if (menu.npresets >= MAX_PRESETS)
		return;
	char *f[10] = {0};
	int n = 0;
	for (char *p = line; p && n < 10; n++) {
		f[n] = p;
		p = strchr(p, '|');
		if (p)
			*p++ = 0;
	}
	if (n < 9)
		return;
	ChargenLook *l = &menu.presets[menu.npresets];
	memset(l, 0, sizeof(*l));
	l->version = 1, l->preset = (uint8_t)menu.npresets;
	l->hair_color = (uint8_t)atoi(f[2]), l->complexion = (uint8_t)atoi(f[3]);
	const int w = atoi(f[4]);
	l->weight = (uint8_t)(w < 0 ? 0 : w > 100 ? 100 : w);
	int nose = 0, eyes = 0, mouth = 0;
	sscanf(f[5], "%d,%d,%d", &nose, &eyes, &mouth);
	l->nose = (uint8_t)nose, l->eyes = (uint8_t)eyes, l->mouth = (uint8_t)mouth;
	char *m = f[6];
	for (int k = 0; k < CG_MORPHS && m; k++) {
		const float v = strtof(m, NULL) * 100.0f;
		l->morph[k] = (int8_t)(v < -100.0f ? -100 : v > 100.0f ? 100 : (int)lroundf(v));
		m = strchr(m, ',');
		if (m)
			m++;
	}
	// Its head parts (the kinds it hasn't: the race's first eyes and brows, no hair or beard or scar)
	chargen_defaults(race, female, l->pick);
	l->pick[CG_HAIR] = -1;
	for (char *p = strtok(f[7], ","); p; p = strtok(NULL, ",")) {
		char *colon = strchr(p, ':');
		if (!colon)
			continue;
		*colon = 0;
		for (int k = 0; k < CG_KINDS; k++)
			if (!strcmp(p, KIND_KEYS[k])) {
				const int at = part_index(race, female, k, colon + 1);
				if (at >= 0)
					l->pick[k] = (int8_t)at;
			}
	}
	for (char *p = strtok(f[8], ","); p; p = strtok(NULL, ",")) {
		int tini = 0, color = 0, value = 0;
		if (sscanf(p, "%d:%d:%d", &tini, &color, &value) != 3)
			continue;
		const Layer *ly = layer_of(tini);
		if (!ly || ly->type <= 0 || ly->type >= CG_TINT_TYPES || color >= ly->count)
			continue;
		if (l->tint[ly->type].layer && l->tint[ly->type].value >= value)
			continue; // (two of a type: the stronger)
		l->tint[ly->type].layer = (uint8_t)tini, l->tint[ly->type].color = (uint8_t)color;
		l->tint[ly->type].value = (uint8_t)(value > 100 ? 100 : value);
	}
	// The layers it doesn't list: the race's defaults (its skin tone - a preset without one had the bare
	// body's; the rest are mostly "none")
	for (int i = 0; i < menu.nlayers; i++) {
		const Layer *ly = &menu.layers[i];
		if (ly->type <= 0 || ly->type >= CG_TINT_TYPES || ly->type == CG_TINT_PAINT || ly->type == CG_TINT_DIRT ||
		    l->tint[ly->type].layer || ly->def >= ly->count)
			continue;
		const Color *c = &menu.colors[ly->first + ly->def];
		if (!c->value)
			continue;
		l->tint[ly->type].layer = (uint8_t)ly->tini, l->tint[ly->type].color = (uint8_t)ly->def;
		l->tint[ly->type].value = c->value;
	}
	menu.npresets++;
}

static int menu_load(const char *race, int female) {
	if (menu.tried && menu.female == female && !strcmp(menu.race, race))
		return menu.ok;
	menu_free();
	snprintf(menu.race, sizeof(menu.race), "%s", race);
	menu.female = female, menu.tried = 1;
	char path[256], lower[24];
	int i = 0;
	for (; race[i] && i < 23; i++)
		lower[i] = (char)tolower((unsigned char)race[i]);
	lower[i] = 0;
	snprintf(path, sizeof(path), "%schargen/%s_%c.txt", pack, lower, female ? 'f' : 'm');
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		game_log("chargen: no race menu for %s (%s)", race, female ? "female" : "male");
		return 0;
	}
	buf[size] = 0;
	char *next = NULL;
	for (char *line = buf; line && *line; line = next) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		if (line[0] == 'X' && menu.ncx < MAX_COMPLEXIONS) {
			Complexion *c = &menu.cx[menu.ncx];
			if (sscanf(line, "X|%39[^|]|%39s", c->base, c->detail) == 2)
				menu.ncx++;
		} else if (line[0] == 'L' && menu.nlayers < MAX_LAYERS) {
			Layer *l = &menu.layers[menu.nlayers];
			l->def = 0;
			if (sscanf(line, "L|%d|%d|%39[^|]|%d", &l->tini, &l->type, l->mask, &l->def) >= 3)
				l->first = menu.ncolors, l->count = 0, menu.nlayers++;
		} else if (line[0] == 'C' && menu.nlayers && menu.ncolors < MAX_COLORS) {
			int tini = 0;
			char hex[8];
			float value = 0.0f;
			Color *c = &menu.colors[menu.ncolors];
			uint8_t rgb[3];
			Layer *l = &menu.layers[menu.nlayers - 1];
			if (sscanf(line, "C|%d|%7[^|]|%f", &tini, hex, &value) == 3 && tini == l->tini && hex_rgb(hex, rgb)) {
				c->r = rgb[0], c->g = rgb[1], c->b = rgb[2];
				c->value = (uint8_t)(value <= 0.0f ? 0 : value >= 1.0f ? 100 : lroundf(value * 100.0f));
				menu.ncolors++, l->count++;
			}
		} else if (line[0] == 'H' && menu.nhair < MAX_HAIR_COLORS) {
			if (hex_rgb(line + 2, menu.hair[menu.nhair]))
				menu.nhair++;
		} else if (line[0] == 'T') {
			int g = 0;
			unsigned m = 0;
			if (sscanf(line, "T|%d|%x", &g, &m) == 2 && g >= 0 && g < 4)
				menu.types[g] = m;
		} else if (line[0] == 'S') {
			read_preset(line, race, female);
		}
	}
	free(buf);
	menu.ok = menu.ncx > 0;
	game_log("chargen: %s %s - %d tint layers (%d colours), %d presets, %d hair colours, %d complexions", race,
	         female ? "female" : "male", menu.nlayers, menu.ncolors, menu.npresets, menu.nhair, menu.ncx);
	return menu.ok;
}

int chargen_presets(const char *race, int female) {
	return menu_load(race, female) ? menu.npresets : 0;
}

// (whether a race and sex has the menu's data, without loading it over the one in use: kept)
int chargen_has_menu(const char *race, int female) {
	static struct {
		char race[24];
		int female, has;
	} known[32];
	static int nknown;
	for (int i = 0; i < nknown; i++)
		if (known[i].female == female && !strcmp(known[i].race, race))
			return known[i].has;
	char path[256], lower[24];
	int i = 0;
	for (; race[i] && i < 23; i++)
		lower[i] = (char)tolower((unsigned char)race[i]);
	lower[i] = 0;
	snprintf(path, sizeof(path), "%schargen/%s_%c.txt", pack, lower, female ? 'f' : 'm');
	long size = 0;
	void *d = file_read_all(path, &size);
	const int has = d && size > 0;
	free(d);
	if (nknown < 32) {
		snprintf(known[nknown].race, sizeof(known[0].race), "%s", race);
		known[nknown].female = female, known[nknown].has = has, nknown++;
	}
	return has;
}

int chargen_preset(const char *race, int female, int n, ChargenLook *out) {
	if (!menu_load(race, female) || n < 0 || n >= menu.npresets)
		return 0;
	*out = menu.presets[n];
	return 1;
}

int chargen_hair_colors(const char *race, int female) {
	return menu_load(race, female) ? menu.nhair : 0;
}

int chargen_complexions(const char *race, int female) {
	return menu_load(race, female) ? menu.ncx : 0;
}

int chargen_layers(const char *race, int female, int type, int *tini_out, int max) {
	int n = 0;
	if (!menu_load(race, female))
		return 0;
	for (int i = 0; i < menu.nlayers; i++)
		if (menu.layers[i].type == type) {
			if (n < max && tini_out)
				tini_out[n] = menu.layers[i].tini;
			n++;
		}
	return n;
}

int chargen_layer_colors(const char *race, int female, int tini) {
	const Layer *l = menu_load(race, female) ? layer_of(tini) : NULL;
	return l ? l->count : 0;
}

int chargen_layer_color_value(const char *race, int female, int tini, int color) {
	const Layer *l = menu_load(race, female) ? layer_of(tini) : NULL;
	return l && color >= 0 && color < l->count ? menu.colors[l->first + color].value : 0;
}

int chargen_types(const char *race, int female, int group, uint8_t *out, int max) {
	int n = 0;
	if (!menu_load(race, female) || group < 0 || group > 3)
		return 0;
	for (int b = 1; b < 32; b++)
		if (menu.types[group] >> b & 1) {
			if (n < max && out)
				out[n] = (uint8_t)b;
			n++;
		}
	return n;
}

// --- Making the look --------------------------------------------------------------------

static uint8_t *read_image(const char *name, const char *ext, const char *magic, int channels) {
	char path[256];
	snprintf(path, sizeof(path), "%schargen/%s.%s", pack, name, ext);
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	if (!d)
		return NULL;
	uint16_t w = 0, h = 0;
	if (size >= 8)
		memcpy(&w, d + 4, 2), memcpy(&h, d + 6, 2);
	if (size < 8 || memcmp(d, magic, 4) || w != FACE_SIZE || h != FACE_SIZE ||
	    size < 8 + (long)FACE_SIZE * FACE_SIZE * channels) {
		free(d);
		return NULL;
	}
	memmove(d, d + 8, (size_t)FACE_SIZE * FACE_SIZE * channels);
	return d;
}

static const uint8_t *mask_pixels(Layer *l) {
	if (!l->pixels)
		l->pixels = read_image(l->mask, "skmask", "SKMK", 1);
	return l->pixels;
}

// The face: its diffuse x detail x 4 x the tint layers (over grey, each blended in by its mask x
// value) x 2 - Skyrim's FaceGen, as its own face tints show (converter/chargen.py)
static int make_face(const ChargenLook *look, uint8_t *rgba) {
	const int cx = look->complexion < menu.ncx ? look->complexion : 0;
	Complexion *c = &menu.cx[cx];
	if (!c->base_px)
		c->base_px = read_image(c->base, "skraw", "SKRW", 3);
	if (!c->detail_px && strcmp(c->detail, "-"))
		c->detail_px = read_image(c->detail, "skmask", "SKMK", 1);
	if (!c->base_px)
		return 0;
	enum { N = FACE_SIZE * FACE_SIZE };
	static int16_t tint[N * 3];
	for (int p = 0; p < N * 3; p++)
		tint[p] = 128;
	for (int i = 0; i < menu.nlayers; i++) { // (in the race's order: the skin tone first)
		Layer *ly = &menu.layers[i];
		if (ly->type <= 0 || ly->type >= CG_TINT_TYPES || look->tint[ly->type].layer != ly->tini)
			continue;
		const int ci = look->tint[ly->type].color < ly->count ? look->tint[ly->type].color : 0;
		const Color *col = &menu.colors[ly->first + ci];
		const int a = look->tint[ly->type].value * 256 / 100; // (0 .. 256)
		const uint8_t *m = a > 0 ? mask_pixels(ly) : NULL;
		if (!m)
			continue;
		const int cr = col->r, cg = col->g, cb = col->b;
		for (int p = 0; p < N; p++) {
			const int k = (m[p] * a) >> 8;
			if (!k)
				continue;
			int16_t *t = &tint[p * 3];
			t[0] = (int16_t)(t[0] + (((cr - t[0]) * k) >> 8));
			t[1] = (int16_t)(t[1] + (((cg - t[1]) * k) >> 8));
			t[2] = (int16_t)(t[2] + (((cb - t[2]) * k) >> 8));
		}
	}
	// (the detail map's blank is a quarter grey - Skyrim's BlankDetailmap is 66 - and its FaceGen shader
	// takes it x4: x2 made every face half as bright)
	const uint8_t *b = c->base_px, *d = c->detail_px;
	for (int p = 0; p < N; p++) {
		const int det = d ? d[p] : 64;
		for (int j = 0; j < 3; j++) {
			const int v = ((b[p * 3 + j] * det >> 6) * tint[p * 3 + j]) >> 7;
			rgba[p * 4 + j] = (uint8_t)(v > 255 ? 255 : v);
		}
		rgba[p * 4 + 3] = 255;
	}
	return 1;
}

// The face's texture (its mip chain - vitaGL's, from the first level: it's seen close in the menu,
// small across a room)
static GLuint upload_face(const uint8_t *rgba) {
	if (face_tex)
		glDeleteTextures(1, &face_tex);
	glGenTextures(1, &face_tex);
	glBindTexture(GL_TEXTURE_2D, face_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FACE_SIZE, FACE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	return face_tex;
}

// Skyrim's hair colour on grey strands: x2, as its hair shader; the darkest lifted so the strands
// still show (converter/npcs.py _hair_color)
static void hair_mul(const uint8_t rgb[3], float out[3]) {
	float c[3];
	for (int j = 0; j < 3; j++)
		c[j] = rgb[j] / 255.0f * 2.0f;
	const float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
	if (lum < 0.2f)
		for (int j = 0; j < 3; j++)
			c[j] = lum > 0.02f ? c[j] * (0.2f / lum) : 0.2f;
	for (int j = 0; j < 3; j++)
		out[j] = c[j] < 0.0f ? 0.0f : c[j] > 1.0f ? 1.0f : c[j];
}

void chargen_apply_colors(const char *race, int female, const ChargenLook *look) {
	if (!menu_load(race, female))
		return;
	const unsigned long long t0 = sceKernelGetProcessTimeWide();
	// The skin tone on the bare skin: as the converter tones the people's (npcs.py skin_tone_color - the
	// body texture a fair skin, darkened toward the tone), so you're lit as they are (Skyrim's x2 on it
	// washed your arms out to white here)
	float skin[3] = {1.0f, 1.0f, 1.0f}, hair[3] = {1.0f, 1.0f, 1.0f};
	const Layer *sl = look->tint[CG_TINT_SKIN].layer ? layer_of(look->tint[CG_TINT_SKIN].layer) : NULL;
	if (sl && sl->count) {
		const Color *c = &menu.colors[sl->first + (look->tint[CG_TINT_SKIN].color < sl->count ? look->tint[CG_TINT_SKIN].color : 0)];
		const float a = look->tint[CG_TINT_SKIN].value / 100.0f;
		const float rgb[3] = {c->r / 255.0f, c->g / 255.0f, c->b / 255.0f};
		for (int j = 0; j < 3; j++) {
			const float tone = 0.5f + (rgb[j] - 0.5f) * a; // (the layer over grey, as on the face)
			const float v = 0.35f + 0.65f * tone / SKIN_TONE_LIGHT;
			skin[j] = v < SKIN_TONE_LEAST ? SKIN_TONE_LEAST : v > 1.0f ? 1.0f : v;
		}
	}
	if (menu.nhair)
		hair_mul(menu.hair[look->hair_color < menu.nhair ? look->hair_color : 0], hair);
	actors_player_look_colors(skin, hair);
	static uint8_t rgba[FACE_SIZE * FACE_SIZE * 4];
	if (make_face(look, rgba))
		actors_player_look_face(upload_face(rgba));
	game_log("chargen: colours and face made in %u ms",
	         (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000));
}

// Skyrim's 18 face sliders (NPC NAM9): each a pair of chargen morphs, below zero and above
// (worked out from 150 of its NPCs' FaceGen heads: converter/chargen.py)
static const char *const MORPH_NEG[CG_MORPHS] = {
	"NoseShort", "NoseDown", "JawUp", "JawNarrow", "JawBack", "CheeksDown", "CheeksIn", "EyesMoveDown", "EyesMoveIn",
	"BrowDown", "BrowIn", "BrowBack", "LipMoveDown", "LipMoveIn", "ChinThin", "ChinMoveUp", "Overbite", "EyesBack"};
static const char *const MORPH_POS[CG_MORPHS] = {
	"NoseLong", "NoseUp", "JawDown", "JawWide", "JawForward", "CheeksUp", "CheeksOut", "EyesMoveUp", "EyesMoveOut",
	"BrowUp", "BrowOut", "BrowForward", "LipMoveUp", "LipMoveOut", "ChinWide", "ChinMoveDown", "Underbite",
	"EyesForward"};

void chargen_apply_morphs(const ChargenLook *look) {
	actors_player_look_morphs_clear();
	for (int i = 0; i < CG_MORPHS; i++) {
		const float v = look->morph[i] / 100.0f;
		if (v < 0.0f)
			actors_player_look_morph(MORPH_NEG[i], -v);
		else if (v > 0.0f)
			actors_player_look_morph(MORPH_POS[i], v);
	}
	char name[24];
	if (look->nose)
		snprintf(name, sizeof(name), "NoseType%d", look->nose), actors_player_look_morph(name, 1.0f);
	if (look->eyes)
		snprintf(name, sizeof(name), "EyesType%d", look->eyes), actors_player_look_morph(name, 1.0f);
	if (look->mouth)
		snprintf(name, sizeof(name), "LipType%d", look->mouth), actors_player_look_morph(name, 1.0f);
	actors_player_look_morph("weight", 1.0f - look->weight / 100.0f); // (the models are Skyrim's heaviest: _1)
	actors_player_look_apply();
}

int chargen_apply_look(const char *race, int female, ChargenLook *look) {
	if (!menu_load(race, female) || look->version != 1)
		return 0;
	const unsigned long long t0 = sceKernelGetProcessTimeWide();
	chargen_apply(race, female, look->pick);
	chargen_apply_morphs(look);
	chargen_apply_colors(race, female, look);
	game_log("chargen: the look on in %u ms (preset %d, %d roles)", (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000),
	         look->preset, actors_player_look_roles());
	return 1;
}
