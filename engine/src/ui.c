#include "frametime.h"
#include "sfx.h"
#include "ui.h"

#include <math.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include <psp2/kernel/clib.h>

#include "fileio.h"
#include <ctype.h>
#include "shader.h"

#define SKFT_VERSION 1
#define MAX_QUADS 6144
#define MAX_LINES 3
#define SUB_WIDTH_FRAC 0.82f  // subtitles wrap within this share of the screen width
#define SUB_BOTTOM 34.0f      // pixels from the bottom edge to the last line
#define BOX_PAD 10.0f
#define SOLID_CHAR 127        // a solid cell in the atlas (for the backing box)

static const char *UI_VS =
	"uniform float2 u_screen;\n"
	"void main(float2 position, float2 texcoord, float4 color,\n"
	"	float4 out vPosition : POSITION, float2 out vTexcoord : TEXCOORD0, float4 out vColor : COLOR) {\n"
	"	vPosition = float4(position.x / u_screen.x * 2.0f - 1.0f, 1.0f - position.y / u_screen.y * 2.0f, 0.0f, 1.0f);\n"
	"	vTexcoord = texcoord;\n"
	"	vColor = color;\n"
	"}\n";

static const char *UI_FS =
	"uniform sampler2D u_tex;\n"
	"float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR) {\n"
	"	return float4(vColor.rgb, vColor.a * tex2D(u_tex, vTexcoord).a);\n"
	"}\n";

// Pictures (a save's scene): the texture's colours
static const char *IMG_FS =
	"uniform sampler2D u_tex;\n"
	"float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR) {\n"
	"	return tex2D(u_tex, vTexcoord) * vColor;\n"
	"}\n";

typedef struct {
	float x, y, u, v;
	uint8_t r, g, b, a;
} UiVertex;

static GLuint prog, tex, img_prog, img_tex;
static GLint u_screen_loc, img_screen_loc;
static int img_shown;            // a picture this frame (the save list's)
static float img_rect[4];        // x0, y0, x1, y1
static int saves_open;           // the save list
static UiSaveList saves;
static char save_title[48], save_rows[32][80], save_lines[4][96], save_hints[96];
static int screen_w, screen_h;
static int atlas_w, atlas_h, cell_w, cell_h, line_h, first_char, char_count;
static uint8_t advance[256];
static char speaker_text[64], body_text[1024], prompt_text[96];
static float fade_alpha;
// A book open (ui_book_*): its words laid out in lines, the lines in pages
#define BOOK_MAX_LINES 2048
#define BOOK_MAX_PAGES 256
#define BOOK_W 760.0f
#define BOOK_H 470.0f
#define BOOK_MARGIN 48.0f
static int book_open, book_page, book_pages, book_lines;
static int local_open;
static UiLocalMap local;
static char book_title[64], *book_text;
static struct { int start, len; } book_line[BOOK_MAX_LINES];
static int book_first[BOOK_MAX_PAGES + 1]; // each page's first line
static uint8_t tint_rgba[4]; // (a shout's flash, poison's sickly edge: over the scene, under the HUD)
static int subtitles_on = 1;
#define MENU_MAX 12
static char menu_title[48], menu_items[MENU_MAX][200]; // (a conversation's answers run long)
static char menu_note[320]; // (under the list: Skyrim's words for the chosen shout or skill)
static int menu_count, menu_selected, menu_open;
static int tween_open; // the character menu: four directions
static int title_on;   // the title screen under the menu
static float bar_frac[3], bar_alpha; // magicka, health, stamina (0..1); how visible
static float breath_frac = 1.0f;     // under water: air left
static int crosshair_on, crosshair_hot; // the centre dot; brighter over something to use
#define INV_MAX 256
static int inv_open;
static int race_open; // the race menu
static int cg_open; // the character creator's (ui_chargen)
static UiChargen cg;
static char cg_tabs[8][16], cg_races[16][32], cg_title[32], cg_desc[480], cg_lines[8][96], cg_rows[16][48],
	cg_values[16][24], cg_race_name[32], cg_name[40], cg_hints[128];
static float cg_fills[16];
static void build_chargen(void);
static int perk_open; // a perk tree
static int map_open;  // the world map
static UiMap map_view;
static char map_text[3][96];
static struct {
	unsigned tex;
	float rect[4], uv[4];
	uint8_t col[4];
} unders[3]; // pictures under the menu (the map's backdrop and images)
static int under_count;
static UiPerkTree perk_view;
static char perk_text[6][200];
static char enemy_name[32];
static float enemy_frac = -1.0f; // the foe's health bar (< 0: none)
static UiRaceMenu race;
static char race_names[16][32], race_face[64], race_title[32], race_desc[480], race_lines[8][96], race_hints[96];
static UiInventory inv;
static char inv_categories[16][24], inv_names[INV_MAX][64], inv_marks[INV_MAX][16];
static char inv_title[64], inv_lines[4][32], inv_text[160], inv_hints[96], inv_status[96];
static char notice_text[96];
static char notice_sound[24]; // (the showing notice's sound, played as it's first seen: a quest's, after a talk)
static struct { char text[96]; float seconds; char sound[24]; } notice_wait[6]; // (the notices still to show, in turn)
static int notice_wait_n;
static float notice_shown; // (how long the one up now has been)
static char perf_text[12][64]; // the performance readout (perf.c), top left
static int perf_n;
static float notice_left;          // seconds
static UiVertex verts[MAX_QUADS * 6];
static int vert_count;

// The game's title, as the user's own data names it (pack title.txt: converter/title.py) - none
// of its own: "OpenSKY" without it
static char title_over[48] = "", title_name[32] = "OpenSKY";
static float title_card_alpha; // (the opening's cards: ui_opening_card)
static int title_card_kind;    // (1 the studio, 2 "Presents", 3 the line over the name, 4 the name)
static char title_over_plain[48]; // (the line over the name as written: INFO 020181BC)
// The game's makers, as the game credits them (pack legal.txt: converter/title.py, from the user's own interface\credits.txt):
// the studio, then its copyright and trademark notice - shown while it starts (ui_boot), the engine naming no one itself
static char legal_studio[64], legal_text[640];
static int boot_on;
static char boot_step[96];
static const char *credits_text; // (the title menu's Credits: the game's own, rolled up the screen - ui_credits)
static float credits_scroll;

static void load_legal(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%slegal.txt", pack_dir);
	long size = 0;
	char *t = (char *)file_read_all(path, &size);
	if (!t)
		return;
	char *buf = realloc(t, size + 1);
	if (!buf) {
		free(t);
		return;
	}
	buf[size] = 0;
	char *nl = strchr(buf, '\n');
	if (nl) {
		*nl = 0;
		char *rest = nl + 1, *end = strchr(rest, '\n');
		if (end)
			*end = 0;
		snprintf(legal_studio, sizeof(legal_studio), "%s", buf);
		snprintf(legal_text, sizeof(legal_text), "%s", rest);
	}
	free(buf);
}

static void load_title(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%stitle.txt", pack_dir);
	long size = 0;
	char *t = (char *)file_read_all(path, &size);
	if (!t)
		return;
	char *buf = realloc(t, size + 1);
	if (!buf) {
		free(t);
		return;
	}
	buf[size] = 0;
	char *nl = strchr(buf, '\n');
	if (nl) {
		*nl = 0;
		char *name = nl + 1, *end = strchr(name, '\n');
		if (end)
			*end = 0;
		if (*name) {
			snprintf(title_over, sizeof(title_over), "%s", buf);
			snprintf(title_over_plain, sizeof(title_over_plain), "%s", buf);
			snprintf(title_name, sizeof(title_name), "%s", name);
			for (char *c = title_over; *c; c++) // (Skyrim's: wide-set capitals)
				*c = (char)toupper((unsigned char)*c);
			for (char *c = title_name; *c; c++)
				*c = (char)toupper((unsigned char)*c);
		}
	}
	free(buf);
}

static float draw_text_big(const char *s, int n, float x, float y, float scale, float tracking, const uint8_t col[4]);
static float text_width_big(const char *s, float scale, float tracking);
static void build_boot(void);
static void build_credits(void);

void ui_boot(const char *step) {
	boot_on = step != NULL;
	snprintf(boot_step, sizeof(boot_step), "%s", step ? step : "");
}

void ui_credits(const char *text, float scroll) {
	credits_text = text, credits_scroll = scroll;
}

void ui_opening_card(int kind, float alpha) {
	title_card_kind = kind;
	title_card_alpha = !kind || alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
}

void ui_init(const char *pack_dir, int w, int h) {
	screen_w = w, screen_h = h;
	load_title(pack_dir);
	load_legal(pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%sui/font.skfont", pack_dir);
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	if (!d || size < 20 || memcmp(d, "SKFT", 4) || *(uint16_t *)(d + 4) != SKFT_VERSION) {
		sceClibPrintf("ui: no font at %s\n", path);
		free(d);
		return;
	}
	uint16_t hdr[5];
	memcpy(hdr, d + 8, sizeof(hdr));
	atlas_w = hdr[0], atlas_h = hdr[1], cell_w = hdr[2], cell_h = hdr[3], line_h = hdr[4];
	first_char = d[18], char_count = d[19];
	if (size < 20 + char_count + (long)atlas_w * atlas_h) {
		sceClibPrintf("ui: bad font %s\n", path);
		free(d);
		return;
	}
	memcpy(advance + first_char, d + 20, char_count);
	// White texels with the glyph coverage as alpha
	uint8_t *rgba = malloc((size_t)atlas_w * atlas_h * 4);
	const uint8_t *cov = d + 20 + char_count;
	for (int i = 0; rgba && i < atlas_w * atlas_h; i++)
		rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255, rgba[i * 4 + 3] = cov[i];
	free(d);
	if (!rgba)
		return;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_w, atlas_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	free(rgba);
	static const char *const attribs[] = {"position", "texcoord", "color"};
	img_prog = shader_program("ui_image", UI_VS, IMG_FS, attribs, 3);
	if (img_prog)
		img_screen_loc = glGetUniformLocation(img_prog, "u_screen");
	prog = shader_program("ui", UI_VS, UI_FS, attribs, 3);
	if (prog) {
		u_screen_loc = glGetUniformLocation(prog, "u_screen");
		glUseProgram(prog);
		glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
		glUseProgram(0);
	}
}

void ui_subtitle(const char *speaker, const char *text) {
	snprintf(speaker_text, sizeof(speaker_text), "%s", speaker ? speaker : "");
	snprintf(body_text, sizeof(body_text), "%s", text ? text : "");
}

void ui_prompt(const char *text) {
	snprintf(prompt_text, sizeof(prompt_text), "%s", text ? text : "");
}

void ui_set_subtitles(int on) {
	subtitles_on = on;
}

void ui_menu(const char *title, const char *const *items, int count, int selected) {
	menu_open = title != NULL;
	if (!menu_open)
		return;
	snprintf(menu_title, sizeof(menu_title), "%s", title);
	menu_note[0] = 0; // (set after, for this list: ui_menu_note)
	menu_count = count < MENU_MAX ? count : MENU_MAX;
	for (int i = 0; i < menu_count; i++)
		snprintf(menu_items[i], sizeof(menu_items[0]), "%s", items[i]);
	menu_selected = selected;
}

void ui_menu_note(const char *text) {
	snprintf(menu_note, sizeof(menu_note), "%s", text ? text : "");
}

void ui_vitals(float health, float magicka, float stamina, float breath, float alpha) {
	bar_frac[0] = magicka, bar_frac[1] = health, bar_frac[2] = stamina;
	breath_frac = breath;
	bar_alpha = alpha;
}

void ui_inventory(const UiInventory *v) {
	inv_open = v != NULL;
	if (!v)
		return;
	inv = *v; // (a copy of the strings: the caller's may be temporary)
	inv.category_count = v->category_count < 16 ? v->category_count : 16;
	inv.count = v->count < INV_MAX ? v->count : INV_MAX;
	for (int i = 0; i < inv.category_count; i++)
		snprintf(inv_categories[i], sizeof(inv_categories[0]), "%s", v->categories[i]);
	for (int i = 0; i < inv.count; i++) {
		snprintf(inv_names[i], sizeof(inv_names[0]), "%s", v->names[i]);
		snprintf(inv_marks[i], sizeof(inv_marks[0]), "%s", v->marks[i] ? v->marks[i] : "");
	}
	snprintf(inv_title, sizeof(inv_title), "%s", v->card_title ? v->card_title : "");
	for (int k = 0; k < 4; k++)
		snprintf(inv_lines[k], sizeof(inv_lines[0]), "%s", v->card_lines[k] ? v->card_lines[k] : "");
	snprintf(inv_text, sizeof(inv_text), "%s", v->card_text ? v->card_text : "");
	snprintf(inv_hints, sizeof(inv_hints), "%s", v->hints ? v->hints : "");
	snprintf(inv_status, sizeof(inv_status), "%s", v->status ? v->status : "");
}

void ui_race_menu(const UiRaceMenu *v) {
	race_open = v != NULL;
	if (!v)
		return;
	race = *v; // (copies of the strings)
	race.count = v->count < 16 ? v->count : 16;
	for (int i = 0; i < race.count; i++)
		snprintf(race_names[i], sizeof(race_names[0]), "%s", v->races[i]);
	snprintf(race_face, sizeof(race_face), "%s", v->face ? v->face : "");
	snprintf(race_title, sizeof(race_title), "%s", v->title ? v->title : "");
	snprintf(race_desc, sizeof(race_desc), "%s", v->desc ? v->desc : "");
	for (int k = 0; k < 8; k++)
		snprintf(race_lines[k], sizeof(race_lines[0]), "%s", v->lines[k] ? v->lines[k] : "");
	snprintf(race_hints, sizeof(race_hints), "%s", v->hints ? v->hints : "");
}

void ui_chargen(const UiChargen *v) {
	cg_open = v != NULL;
	if (!v)
		return;
	cg = *v; // (copies of the strings)
	cg.tab_count = v->tab_count < 8 ? v->tab_count : 8;
	for (int i = 0; i < cg.tab_count; i++)
		snprintf(cg_tabs[i], sizeof(cg_tabs[0]), "%s", v->tabs[i]);
	cg.race_count = v->races ? (v->race_count < 16 ? v->race_count : 16) : 0;
	for (int i = 0; i < cg.race_count; i++)
		snprintf(cg_races[i], sizeof(cg_races[0]), "%s", v->races[i]);
	cg.row_count = v->rows ? (v->row_count < 16 ? v->row_count : 16) : 0;
	for (int i = 0; i < cg.row_count; i++) {
		snprintf(cg_rows[i], sizeof(cg_rows[0]), "%s", v->rows[i]);
		snprintf(cg_values[i], sizeof(cg_values[0]), "%s", v->values && v->values[i] ? v->values[i] : "");
		cg_fills[i] = v->fills ? v->fills[i] : 0.0f;
	}
	snprintf(cg_title, sizeof(cg_title), "%s", v->title ? v->title : "");
	snprintf(cg_desc, sizeof(cg_desc), "%s", v->desc ? v->desc : "");
	for (int k = 0; k < 8; k++)
		snprintf(cg_lines[k], sizeof(cg_lines[0]), "%s", v->lines[k] ? v->lines[k] : "");
	snprintf(cg_race_name, sizeof(cg_race_name), "%s", v->race_name ? v->race_name : "");
	snprintf(cg_name, sizeof(cg_name), "%s", v->name ? v->name : "");
	snprintf(cg_hints, sizeof(cg_hints), "%s", v->hints ? v->hints : "");
}

void ui_save_list(const UiSaveList *v) {
	saves_open = v != NULL;
	if (!v)
		return;
	const int picture = saves.has_picture; // (ui_set_save_picture's)
	saves = *v;
	saves.has_picture = picture;
	saves.count = v->count < 32 ? v->count : 32;
	snprintf(save_title, sizeof(save_title), "%s", v->title ? v->title : "");
	for (int i = 0; i < saves.count; i++)
		snprintf(save_rows[i], sizeof(save_rows[0]), "%s", v->rows[i]);
	for (int k = 0; k < 4; k++)
		snprintf(save_lines[k], sizeof(save_lines[0]), "%s", v->lines[k] ? v->lines[k] : "");
	snprintf(save_hints, sizeof(save_hints), "%s", v->hints ? v->hints : "");
}

void ui_set_save_picture(const uint16_t *rgb565, int w, int h) {
	if (!rgb565) {
		saves.has_picture = 0;
		return;
	}
	if (!img_tex)
		glGenTextures(1, &img_tex);
	glBindTexture(GL_TEXTURE_2D, img_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rgb565);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	saves.has_picture = 1;
}

void ui_perk_tree(const UiPerkTree *v) {
	perk_open = v != NULL;
	if (!v)
		return;
	perk_view = *v;
	const char *src[6] = {v->skill, v->prev, v->next, v->title, v->desc, v->need};
	for (int i = 0; i < 6; i++)
		snprintf(perk_text[i], sizeof(perk_text[i]), "%s", src[i] ? src[i] : "");
	static char points[64], hints[96];
	snprintf(points, sizeof(points), "%s", v->points ? v->points : "");
	snprintf(hints, sizeof(hints), "%s", v->hints ? v->hints : "");
	perk_view.points = points, perk_view.hints = hints;
}

void ui_map(const UiMap *v) {
	map_open = v != NULL;
	if (!v)
		return;
	map_view = *v;
	const char *src[3] = {v->title, v->line, v->hints};
	for (int i = 0; i < 3; i++)
		snprintf(map_text[i], sizeof(map_text[i]), "%s", src[i] ? src[i] : "");
}

void ui_enemy(const char *name, float health) {
	if (!name) {
		enemy_frac = -1.0f;
		return;
	}
	snprintf(enemy_name, sizeof(enemy_name), "%s", name);
	enemy_frac = health < 0.0f ? 0.0f : health > 1.0f ? 1.0f : health;
}

static int compass_on;
static float compass_yaw; // (degrees: 0 north, east clockwise)
static UiCompassMark compass_marks[UI_COMPASS_MARKS];
static int compass_n;

void ui_compass(int on, float yaw_deg, const UiCompassMark *marks, int n) {
	compass_on = on, compass_yaw = isfinite(yaw_deg) ? fmodf(yaw_deg, 360.0f) : 0.0f;
	compass_n = n < UI_COMPASS_MARKS ? n : UI_COMPASS_MARKS;
	if (compass_n > 0)
		memcpy(compass_marks, marks, compass_n * sizeof(UiCompassMark));
}

void ui_crosshair(int on, int over_something) {
	crosshair_on = on, crosshair_hot = over_something;
}

void ui_perf(const char lines[][64], int n) {
	perf_n = n < 12 ? n : 12;
	for (int i = 0; i < perf_n; i++)
		snprintf(perf_text[i], sizeof(perf_text[i]), "%s", lines[i]);
}

// Notices wait their turn (a quest's update and its reward's "100 Gold added" both seen, as
// Skyrim's), and only count down while the HUD is up: one set in a conversation shows after it
void ui_notice_now(const char *text, const char *kind, float seconds) {
	// (a change that supersedes the last of its kind - "Left hand: Flames": shown at once, replacing
	// it, not queued behind it - the hand had moved on two spells while the notice still said the first)
	for (int i = 0; i < notice_wait_n; i++)
		if (!strncmp(notice_wait[i].text, kind, strlen(kind)))
			memmove(&notice_wait[i], &notice_wait[i + 1], sizeof(notice_wait[0]) * (notice_wait_n - i - 1)), notice_wait_n--, i--;
	if (notice_left <= 0.0f || !strncmp(notice_text, kind, strlen(kind))) {
		snprintf(notice_text, sizeof(notice_text), "%s", text);
		notice_left = seconds, notice_shown = 0.0f;
		return;
	}
	if (notice_wait_n < 6) { // (something else showing: next, before the rest)
		memmove(&notice_wait[1], &notice_wait[0], sizeof(notice_wait[0]) * notice_wait_n);
		snprintf(notice_wait[0].text, sizeof(notice_wait[0].text), "%s", text);
		notice_wait[0].seconds = seconds, notice_wait[0].sound[0] = 0, notice_wait_n++;
		notice_left = fminf(notice_left, 0.4f);
	}
}

void ui_notice(const char *text, float seconds) {
	if (!text) {
		notice_left = 0.0f, notice_wait_n = 0;
		return;
	}
	if (notice_left > 0.0f && !strcmp(notice_text, text))
		return;
	for (int i = 0; i < notice_wait_n; i++)
		if (!strcmp(notice_wait[i].text, text))
			return;
	if (notice_left <= 0.0f) {
		snprintf(notice_text, sizeof(notice_text), "%s", text);
		notice_left = seconds, notice_shown = 0.0f, notice_sound[0] = 0;
		return;
	}
	if (notice_wait_n == 6) // (full: the oldest waiting dropped)
		memmove(&notice_wait[0], &notice_wait[1], sizeof(notice_wait[0]) * 5), notice_wait_n--;
	snprintf(notice_wait[notice_wait_n].text, sizeof(notice_wait[0].text), "%s", text);
	notice_wait[notice_wait_n].sound[0] = 0;
	notice_wait[notice_wait_n++].seconds = seconds;
}

// A notice with its sound, heard as it shows (Skyrim's new quest, objective: a talk holds both back - the sound
// was played at once, under Farengar's voice, the notice after he'd done)
void ui_notice_sound(const char *text, float seconds, const char *sound) {
	ui_notice(text, seconds);
	if (!text || !sound)
		return;
	if (notice_left > 0.0f && !strcmp(notice_text, text) && notice_shown <= 0.0f) {
		snprintf(notice_sound, sizeof(notice_sound), "%s", sound);
		return;
	}
	for (int i = 0; i < notice_wait_n; i++)
		if (!strcmp(notice_wait[i].text, text))
			snprintf(notice_wait[i].sound, sizeof(notice_wait[i].sound), "%s", sound);
}

void ui_tween(int on) {
	tween_open = on;
}

void ui_tint(int r, int g, int b, float alpha) {
	tint_rgba[0] = (uint8_t)r, tint_rgba[1] = (uint8_t)g, tint_rgba[2] = (uint8_t)b;
	tint_rgba[3] = (uint8_t)(alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 255.0f : alpha * 255.0f));
}

static char loading_text[96]; // (the loading screen over the black: where you're going, "" none)
static float loading_t;

void ui_loading(const char *place, float seconds) {
	snprintf(loading_text, sizeof(loading_text), "%s", place ? place : "");
	loading_t = seconds;
}

void ui_fade(float alpha) {
	fade_alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}

static int glyph_ok(int c) {
	return c >= first_char && c < first_char + char_count;
}

static float text_width(const char *s, int n) {
	float w = 0.0f;
	for (int i = 0; i < n; i++) {
		int c = (uint8_t)s[i];
		w += glyph_ok(c) ? advance[c] : advance[' '];
	}
	return w;
}

static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const uint8_t c[4]) {
	if (vert_count + 6 > MAX_QUADS * 6)
		return;
	const UiVertex q[4] = {{x0, y0, u0, v0, c[0], c[1], c[2], c[3]}, {x1, y0, u1, v0, c[0], c[1], c[2], c[3]},
	                       {x1, y1, u1, v1, c[0], c[1], c[2], c[3]}, {x0, y1, u0, v1, c[0], c[1], c[2], c[3]}};
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	for (int i = 0; i < 6; i++)
		verts[vert_count++] = q[order[i]];
}

static void cell_uv(int c, float *u0, float *v0, float *u1, float *v1) {
	int i = c - first_char;
	*u0 = (float)((i % 16) * cell_w) / atlas_w;
	*v0 = (float)((i / 16) * cell_h) / atlas_h;
	*u1 = *u0 + (float)cell_w / atlas_w;
	*v1 = *v0 + (float)cell_h / atlas_h;
}

static float draw_text(const char *s, int n, float x, float y, const uint8_t col[4]) {
	for (int i = 0; i < n; i++) {
		int c = (uint8_t)s[i];
		if (!glyph_ok(c))
			c = '?';
		if (c != ' ') {
			float u0, v0, u1, v1;
			cell_uv(c, &u0, &v0, &u1, &v1);
			quad(x, y, x + cell_w, y + cell_h, u0, v0, u1, v1, col);
		}
		x += advance[c];
	}
	return x;
}

static void build_subtitle(void);
static void build_prompt(void);
static void build_menu(void);
static void build_inventory(void);
static void build_race(void);
static void build_saves(void);
static void build_tween(void);
static void build_hud(void);
static void build_perks(void);
static void build_map(void);
static void build_book(void);
static void build_local(void);

static void quad4(const float p[4][2], float cu, float cv, const uint8_t c[4]);

// The loading screen's dragon: a silhouette flying back and forth along the bottom, beating its
// wings, banking round at each end
static void loading_dragon(float t, float cu, float cv) {
	const float period = 7.0f, phase = fmodf(t, period) / period; // (across and back)
	const float along = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
	const float ease = along * along * (3.0f - 2.0f * along);
	const float x = 120.0f + ease * (screen_w - 240.0f), y = screen_h * 0.66f + sinf(t * 1.7f) * 10.0f;
	const float dir = phase < 0.5f ? 1.0f : -1.0f;                      // (facing the way it flies)
	const float turn = fminf(1.0f, fminf(along, 1.0f - along) * 12.0f); // (narrow as it turns about)
	const float flap = sinf(t * 7.0f), sx = dir * turn * 2.3f, sy = 2.3f;
	const uint8_t body[4] = {176, 140, 92, 255}, wing[4] = {140, 108, 70, 240}, membrane[4] = {96, 74, 50, 220};
#define P(px, py) x + (px) * sx, y + (py) * sy
	// Wings: from the shoulders out to the tips, up and down with the beat (the far one behind)
	const float tip_y = -22.0f * flap, elbow_y = -12.0f * flap;
	{
		const float far_w[4][2] = {{P(-4, -2)}, {P(4, -2)}, {P(-2, elbow_y * 0.7f - 4)}, {P(-12, tip_y * 0.7f - 6)}};
		quad4(far_w, cu, cv, membrane);
	}
	{
		const float near_w[4][2] = {{P(-6, 0)}, {P(6, 0)}, {P(0, elbow_y + 6)}, {P(-14, tip_y + 10)}};
		quad4(near_w, cu, cv, wing);
		const float bone[4][2] = {{P(4, -1)}, {P(6, 0)}, {P(-14, tip_y + 11)}, {P(-15, tip_y + 9)}};
		quad4(bone, cu, cv, body);
	}
	// Body, neck and head forward, tail behind
	const float trunk[4][2] = {{P(-14, -1)}, {P(10, -3)}, {P(12, 2)}, {P(-12, 3)}};
	quad4(trunk, cu, cv, body);
	const float neck[4][2] = {{P(10, -3)}, {P(20, -8)}, {P(22, -6)}, {P(12, 2)}};
	quad4(neck, cu, cv, body);
	const float head[4][2] = {{P(19, -10)}, {P(28, -8)}, {P(27, -5)}, {P(21, -5)}};
	quad4(head, cu, cv, body);
	const float horn[4][2] = {{P(19, -10)}, {P(15, -14)}, {P(16, -14)}, {P(21, -10)}};
	quad4(horn, cu, cv, body);
	const float sway = sinf(t * 3.0f) * 3.0f;
	const float tail[4][2] = {{P(-12, -1)}, {P(-12, 3)}, {P(-26, 3 + sway)}, {P(-34, sway - 1)}};
	quad4(tail, cu, cv, body);
#undef P
}

void ui_draw(void) {
	const int hud_hidden = menu_open || inv_open || race_open || cg_open || saves_open || perk_open || map_open || book_open ||
	                       local_open || fade_alpha > 0.5f || loading_t > 0.0f;
	if (notice_left > 0.0f && !hud_hidden && !menu_open && !inv_open) { // (not running out behind a menu - a talk's topics:
		// "Completed: Bleak Falls Barrow" came and went behind Farengar's)
		if (notice_sound[0]) // (its sound as it's seen)
			sfx_play(notice_sound, 0.8f, 0.0f), notice_sound[0] = 0;
		notice_left -= frame_seconds(), notice_shown += frame_seconds();
		if (notice_wait_n > 0 && notice_shown > 2.0f && notice_left > 0.4f) // (others waiting: on after 2 s)
			notice_left = 0.4f;
	}
	if (notice_left <= 0.0f && notice_wait_n > 0) {
		snprintf(notice_text, sizeof(notice_text), "%s", notice_wait[0].text);
		snprintf(notice_sound, sizeof(notice_sound), "%s", notice_wait[0].sound);
		notice_left = notice_wait[0].seconds, notice_shown = 0.0f;
		memmove(&notice_wait[0], &notice_wait[1], sizeof(notice_wait[0]) * (notice_wait_n - 1)), notice_wait_n--;
	}
	int hud = (bar_alpha > 0.0f || notice_left > 0.0f || crosshair_on || enemy_frac >= 0.0f || compass_on) && !menu_open;
	if (inv_open || race_open || cg_open || saves_open || perk_open || map_open || book_open || local_open)
		hud = 0;
	if (!prog || !tex || (!body_text[0] && !prompt_text[0] && fade_alpha <= 0.0f && !tint_rgba[3] && !menu_open && !tween_open && !hud && !perf_n &&
	                      !boot_on && !credits_text &&
	                      !inv_open && !race_open && !cg_open && !saves_open && !perk_open && !map_open && !book_open && !local_open))
		return;
	vert_count = 0;
	img_shown = 0;
	under_count = 0;
	if (boot_on || credits_text) { // (starting up: the makers' credit; the title's Credits roll - nothing else)
		if (boot_on)
			build_boot();
		else
			build_credits();
		goto drawn;
	}
	if (hud)
		build_hud();
	if (body_text[0] && subtitles_on && !menu_open)
		build_subtitle();
	if (prompt_text[0] && fade_alpha <= 0.0f && !menu_open)
		build_prompt();
	if (book_open)
		build_book();
	else if (local_open)
		build_local();
	else if (map_open)
		build_map();
	else if (perk_open)
		build_perks();
	else if (saves_open)
		build_saves();
	else if (race_open && !menu_open)
		build_race();
	else if (cg_open && !menu_open)
		build_chargen();
	else if (inv_open)
		build_inventory();
	else if (menu_open)
		build_menu();
	else if (tween_open)
		build_tween();
	if (tint_rgba[3] && !menu_open) {
		float u0, v0, u1, v1;
		cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
		quad(0, 0, (float)screen_w, (float)screen_h, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f, (u0 + u1) * 0.5f,
		     (v0 + v1) * 0.5f, tint_rgba);
	}
	if (fade_alpha > 0.0f) {
		float u0, v0, u1, v1;
		cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
		const uint8_t black[4] = {0, 0, 0, (uint8_t)(fade_alpha * 255.0f)};
		quad(0, 0, (float)screen_w, (float)screen_h, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f, (u0 + u1) * 0.5f,
		     (v0 + v1) * 0.5f, black);
		if (loading_text[0] && fade_alpha >= 0.99f) { // Skyrim's: the place's name, a turning mark, "Loading"
			const uint8_t gold[4] = {222, 200, 150, 255}, grey[4] = {170, 170, 170, 220};
			const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
			const float nw = text_width(loading_text, (int)strlen(loading_text));
			draw_text(loading_text, (int)strlen(loading_text), (screen_w - nw) * 0.5f, screen_h * 0.42f, gold);
			const float a = loading_t * 2.4f, r = 9.0f, cx = screen_w - 46.0f, cy = screen_h - 34.0f;
			const float p[4][2] = {{cx + cosf(a) * r, cy + sinf(a) * r}, {cx - sinf(a) * r, cy + cosf(a) * r},
			                       {cx - cosf(a) * r, cy - sinf(a) * r}, {cx + sinf(a) * r, cy - cosf(a) * r}};
			quad4(p, cu, cv, gold);
			loading_dragon(loading_t, cu, cv);
			static const char *const dots[4] = {"Loading", "Loading.", "Loading..", "Loading..."};
			const char *l = dots[(int)(loading_t * 2.5f) & 3];
			draw_text(l, (int)strlen(l), cx - 26.0f - text_width("Loading...", 10), cy - line_h * 0.5f, grey);
		}
	}
	if (title_card_alpha > 0.0f && !menu_open) { // (the opening's cards, one at a time in the middle of the screen, fading in
		// and out - Skyrim's: its studio, "Presents" on black, then the game's two lines over the ride as it fades in)
		const float W = (float)screen_w;
		const uint8_t a8 = (uint8_t)(title_card_alpha * 255.0f);
		const uint8_t soft[4] = {200, 200, 200, a8}, silver[4] = {222, 222, 222, a8};
		const float mid = screen_h * 0.5f;
		if (title_card_kind == 4) { // (the game's name: large, wide-set)
			const float big = 3.0f;
			draw_text_big(title_name, (int)strlen(title_name), (W - text_width_big(title_name, big, 14.0f)) * 0.5f, mid - cell_h * big * 0.5f,
			              big, 14.0f, silver);
		} else {
			const char *t = title_card_kind == 1 ? (legal_studio[0] ? legal_studio : "") : title_card_kind == 2 ? "Presents" : title_over_plain;
			if (t[0])
				draw_text_big(t, (int)strlen(t), (W - text_width_big(t, 1.15f, 2.0f)) * 0.5f, mid - line_h * 0.6f, 1.15f, 2.0f, soft);
		}
	}
	if (perf_n) { // the performance readout: top left (below Vita3K's own counter), over everything
		float u0, v0, u1, v1;
		cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
		const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
		const uint8_t box[4] = {0, 0, 0, 170}, green[4] = {170, 235, 150, 255};
		float w = 0.0f;
		for (int i = 0; i < perf_n; i++) {
			const float tw = text_width(perf_text[i], (int)strlen(perf_text[i]));
			w = tw > w ? tw : w;
		}
		const float x0 = 12.0f, y0 = 48.0f;
		quad(x0, y0, x0 + w + 16.0f, y0 + perf_n * line_h + 12.0f, cu, cv, cu, cv, box);
		for (int i = 0; i < perf_n; i++)
			draw_text(perf_text[i], (int)strlen(perf_text[i]), x0 + 8.0f, y0 + 6.0f + i * line_h, green);
	}
drawn:
	if (!vert_count)
		return;
	glUseProgram(prog);
	glUniform2f(u_screen_loc, (float)screen_w, (float)screen_h);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	for (int a = 0; a < 3; a++)
		glEnableVertexAttribArray(a);
	if (under_count && img_prog) { // (the map's images, under its markers and text)
		glUseProgram(img_prog);
		glUniform2f(img_screen_loc, (float)screen_w, (float)screen_h);
		for (int i = 0; i < under_count; i++) {
			const float *r = unders[i].rect, *uv = unders[i].uv;
			const uint8_t *k = unders[i].col;
			UiVertex q[6] = {{r[0], r[1], uv[0], uv[1], k[0], k[1], k[2], k[3]}, {r[2], r[1], uv[2], uv[1], k[0], k[1], k[2], k[3]},
			                 {r[2], r[3], uv[2], uv[3], k[0], k[1], k[2], k[3]}, {r[0], r[1], uv[0], uv[1], k[0], k[1], k[2], k[3]},
			                 {r[2], r[3], uv[2], uv[3], k[0], k[1], k[2], k[3]}, {r[0], r[3], uv[0], uv[3], k[0], k[1], k[2], k[3]}};
			glBindTexture(GL_TEXTURE_2D, unders[i].tex);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), &q[0].x);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), &q[0].u);
			glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex), &q[0].r);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		glUseProgram(prog);
	}
	glBindTexture(GL_TEXTURE_2D, tex);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), &verts[0].x);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), &verts[0].u);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex), &verts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, vert_count);
	// The picture over its frame (the save list's scene)
	if (img_shown && img_prog && img_tex) {
		const float *r = img_rect;
		UiVertex q[6] = {{r[0], r[1], 0, 0, 255, 255, 255, 255}, {r[2], r[1], 1, 0, 255, 255, 255, 255},
		                 {r[2], r[3], 1, 1, 255, 255, 255, 255}, {r[0], r[1], 0, 0, 255, 255, 255, 255},
		                 {r[2], r[3], 1, 1, 255, 255, 255, 255}, {r[0], r[3], 0, 1, 255, 255, 255, 255}};
		glUseProgram(img_prog);
		glUniform2f(img_screen_loc, (float)screen_w, (float)screen_h);
		glBindTexture(GL_TEXTURE_2D, img_tex);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), &q[0].x);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), &q[0].u);
		glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex), &q[0].r);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	for (int a = 0; a < 3; a++)
		glDisableVertexAttribArray(a);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glUseProgram(0);
}

// A quad with any four corners (lines, stars)
static void quad4(const float p[4][2], float cu, float cv, const uint8_t c[4]) {
	if (vert_count + 6 > MAX_QUADS * 6)
		return;
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	for (int i = 0; i < 6; i++) {
		UiVertex *v = &verts[vert_count++];
		v->x = p[order[i]][0], v->y = p[order[i]][1], v->u = cu, v->v = cv;
		v->r = c[0], v->g = c[1], v->b = c[2], v->a = c[3];
	}
}

static void line2(float x0, float y0, float x1, float y1, float w, float cu, float cv, const uint8_t c[4]) {
	float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
	if (len < 0.5f)
		return;
	const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
	const float p[4][2] = {{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}};
	quad4(p, cu, cv, c);
}

// A star: a diamond with a soft halo
static void star(float x, float y, float r, float cu, float cv, const uint8_t c[4]) {
	const uint8_t halo[4] = {c[0], c[1], c[2], (uint8_t)(c[3] / 5)};
	const float h[4][2] = {{x, y - r * 2.6f}, {x + r * 2.6f, y}, {x, y + r * 2.6f}, {x - r * 2.6f, y}};
	quad4(h, cu, cv, halo);
	const float d[4][2] = {{x, y - r}, {x + r, y}, {x, y + r}, {x - r, y}};
	quad4(d, cu, cv, c);
}

// Draws text wrapped to width w from (x, y), at most max_lines lines; returns the y after it
static float draw_wrapped(const char *s, float x, float y, float w, int max_lines, float row, const uint8_t col[4]) {
	for (int line = 0; *s && line < max_lines; line++) {
		while (*s == ' ')
			s++;
		int n = (int)strlen(s), fit = n;
		if (text_width(s, n) > w) { // the most whole words that fit
			fit = 0;
			for (int k = 1; k <= n; k++)
				if ((s[k] == ' ' || s[k] == 0) && text_width(s, k) <= w)
					fit = k;
			if (!fit)
				for (fit = n; fit > 1 && text_width(s, fit) > w; fit--)
					;
		}
		draw_text(s, fit, x, y, col);
		s += fit;
		y += row;
	}
	return y;
}

// A skill's perk tree (see ui_perk_tree): Skyrim's night sky
static void build_perks(void) {
	static const uint8_t sky[4] = {6, 12, 34, 238}, faint[4] = {150, 170, 230, 60}, white[4] = {235, 232, 222, 255},
	                     grey[4] = {150, 156, 176, 255}, gold[4] = {236, 206, 140, 255}, card[4] = {0, 0, 0, 170},
	                     edge[4] = {200, 196, 186, 120}, line_off[4] = {120, 140, 200, 90}, line_on[4] = {235, 225, 250, 220},
	                     bar_bg[4] = {0, 0, 0, 160}, bar_fill[4] = {120, 150, 230, 255};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w, H = (float)screen_h;
	const UiPerkTree *v = &perk_view;
	quad(0, 0, W, H, cu, cv, cu, cv, sky);
	// Faint stars scattered over the sky (the same every frame)
	unsigned s = 0x9e3779b9u;
	for (int i = 0; i < 140; i++) {
		s = s * 1664525u + 1013904223u;
		const float x = (s >> 8) % (unsigned)screen_w;
		s = s * 1664525u + 1013904223u;
		const float y = (s >> 8) % (unsigned)screen_h, r = 0.6f + (s & 3) * 0.4f;
		const uint8_t c[4] = {200, 210, 255, (uint8_t)(40 + (s >> 3 & 63))};
		quad(x - r, y - r, x + r, y + r, cu, cv, cu, cv, c);
	}
	// The skill's name and level, its progress, the neighbours
	const int sl = (int)strlen(perk_text[0]);
	draw_text(perk_text[0], sl, (W - text_width(perk_text[0], sl)) * 0.5f, 18.0f, white);
	const float bw = 220.0f, bx = (W - bw) * 0.5f, by = 22.0f + line_h;
	quad(bx - 1, by - 1, bx + bw + 1, by + 6, cu, cv, cu, cv, bar_bg);
	quad(bx, by, bx + bw * fmaxf(0.0f, fminf(1.0f, v->progress)), by + 5, cu, cv, cu, cv, bar_fill);
	draw_text(perk_text[1], (int)strlen(perk_text[1]), 30.0f, 18.0f, grey);
	draw_text(perk_text[2], (int)strlen(perk_text[2]), W - 30.0f - text_width(perk_text[2], (int)strlen(perk_text[2])),
	          18.0f, grey);
	const int pl = (int)strlen(v->points);
	draw_text(v->points, pl, (W - text_width(v->points, pl)) * 0.5f, by + 12.0f, gold);
	// The tree, fitted into the middle of the screen
	const float area[4] = {80.0f, by + 24.0f + line_h, W - 80.0f, H - 150.0f};
	float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
	for (int i = 0; i < v->count; i++) {
		if (v->state[i] < 0)
			continue;
		lo[0] = fminf(lo[0], v->x[i]), hi[0] = fmaxf(hi[0], v->x[i]);
		lo[1] = fminf(lo[1], v->y[i]), hi[1] = fmaxf(hi[1], v->y[i]);
	}
	const float span_x = fmaxf(hi[0] - lo[0], 1.0f), span_y = fmaxf(hi[1] - lo[1], 1.0f);
	const float scale = fminf((area[2] - area[0]) / span_x, (area[3] - area[1]) / span_y);
	const float ox = (area[0] + area[2]) * 0.5f - (lo[0] + hi[0]) * 0.5f * scale;
	const float oy = (area[1] + area[3]) * 0.5f + (lo[1] + hi[1]) * 0.5f * scale;
#define PX(i) (ox + v->x[i] * scale)
#define PY(i) (oy - v->y[i] * scale)
	for (int k = 0; k < v->link_count; k++) {
		const int a = v->links[k][0], b = v->links[k][1];
		if (a < 0 || b < 0 || a >= v->count || b >= v->count || v->state[a] < 0 || v->state[b] < 0)
			continue;
		const int on = v->state[a] >= 2 && v->state[b] >= 2;
		line2(PX(a), PY(a), PX(b), PY(b), on ? 2.0f : 1.2f, cu, cv, on ? line_on : line_off);
	}
	for (int i = 0; i < v->count; i++) {
		if (v->state[i] < 0)
			continue;
		const int st = v->state[i];
		const uint8_t col[4] = {(uint8_t)(st >= 2 ? 255 : st == 1 ? 225 : 120), (uint8_t)(st >= 2 ? 236 : st == 1 ? 225 : 130),
		                        (uint8_t)(st >= 2 ? 200 : st == 1 ? 245 : 170), (uint8_t)(st >= 1 ? 255 : 200)};
		star(PX(i), PY(i), st >= 2 ? 6.0f : 4.5f, cu, cv, col);
		if (i == v->selected) { // a ring of light about the selected star
			const float r = 15.0f;
			for (int k = 0; k < 12; k++) {
				const float a0 = k * 0.5236f, a1 = a0 + 0.42f;
				line2(PX(i) + cosf(a0) * r, PY(i) + sinf(a0) * r, PX(i) + cosf(a1) * r, PY(i) + sinf(a1) * r, 2.0f, cu, cv,
				      gold);
			}
		}
	}
#undef PX
#undef PY
	(void)faint;
	// The selected perk's card
	const float cy0 = H - 140.0f, cx0 = 60.0f, cx1 = W - 60.0f;
	quad(cx0 - 1, cy0 - 1, cx1 + 1, H - 40.0f + 1, cu, cv, cu, cv, edge);
	quad(cx0, cy0, cx1, H - 40.0f, cu, cv, cu, cv, card);
	const int tl = (int)strlen(perk_text[3]);
	draw_text(perk_text[3], tl, (W - text_width(perk_text[3], tl)) * 0.5f, cy0 + 8.0f, gold);
	float y = draw_wrapped(perk_text[4], cx0 + 24.0f, cy0 + 14.0f + line_h, cx1 - cx0 - 48.0f, 2, line_h + 2.0f, white);
	const int nl = (int)strlen(perk_text[5]);
	draw_text(perk_text[5], nl, (W - text_width(perk_text[5], nl)) * 0.5f, y + 2.0f, grey);
	const int hl = (int)strlen(v->hints);
	draw_text(v->hints, hl, 30.0f, H - 32.0f, grey);
}

// A map marker's icon (Skyrim's kinds, drawn simply): a castle for cities, a house for towns
// and farms, an arch for caves and mines, a tent for camps, a tower for forts, pillars for
// ruins, a diamond for the rest
static void map_icon_on(int kind, float x, float y, float s, float cu, float cv, const uint8_t c[4], int backed);

static void map_icon(int kind, float x, float y, float s, float cu, float cv, const uint8_t c[4]) {
	map_icon_on(kind, x, y, s, cu, cv, c, 1);
}

// (backed: on a dark square, as the map has them; the compass's go straight on its bar)
static void map_icon_on(int kind, float x, float y, float s, float cu, float cv, const uint8_t c[4], int backed) {
	const uint8_t dark[4] = {0, 0, 0, (uint8_t)(c[3] * 0.6f)};
	if (backed)
		quad(x - s - 1, y - s - 1, x + s + 1, y + s + 1, cu, cv, cu, cv, dark);
	switch (kind) {
	case 1: case 34: case 35: case 36: case 37: case 38: case 39: case 40: case 41: case 42: // city
		quad(x - s, y - s * 0.3f, x + s, y + s, cu, cv, cu, cv, c);
		for (int k = 0; k < 3; k++)
			quad(x - s + k * s * 0.8f, y - s, x - s + k * s * 0.8f + s * 0.4f, y - s * 0.3f, cu, cv, cu, cv, c);
		break;
	case 2: case 3: case 13: case 14: case 18: case 19: case 20: case 29: case 31: case 32: { // town, farm, mill
		quad(x - s * 0.8f, y, x + s * 0.8f, y + s, cu, cv, cu, cv, c);
		const float roof[4][2] = {{x - s, y + 0.5f}, {x, y - s}, {x, y - s}, {x + s, y + 0.5f}};
		quad4(roof, cu, cv, c);
		break;
	}
	case 4: case 15: case 9: // cave, mine, shipwreck
		for (int k = 0; k < 6; k++) {
			const float a0 = (float)M_PI * k / 6.0f, a1 = (float)M_PI * (k + 1) / 6.0f;
			line2(x - cosf(a0) * s, y + s * 0.6f - sinf(a0) * s * 1.4f, x - cosf(a1) * s, y + s * 0.6f - sinf(a1) * s * 1.4f,
			      2.2f, cu, cv, c);
		}
		quad(x - s, y + s * 0.5f, x + s, y + s * 0.8f, cu, cv, cu, cv, c);
		break;
	case 5: case 16: case 28: { // camps
		const float tent[4][2] = {{x - s, y + s}, {x, y - s}, {x, y - s}, {x + s, y + s}};
		quad4(tent, cu, cv, c);
		break;
	}
	case 6: case 21: case 30: case 26: // forts, towers, lighthouses
		quad(x - s * 0.55f, y - s * 0.6f, x + s * 0.55f, y + s, cu, cv, cu, cv, c);
		quad(x - s * 0.8f, y - s, x - s * 0.3f, y - s * 0.6f, cu, cv, cu, cv, c);
		quad(x + s * 0.3f, y - s, x + s * 0.8f, y - s * 0.6f, cu, cv, cu, cv, c);
		break;
	case 7: case 8: case 24: case 12: case 33: // ruins, altars, dragon lairs, shrines
		quad(x - s, y - s, x + s, y - s * 0.6f, cu, cv, cu, cv, c);
		quad(x - s * 0.8f, y - s * 0.6f, x - s * 0.4f, y + s, cu, cv, cu, cv, c);
		quad(x + s * 0.4f, y - s * 0.6f, x + s * 0.8f, y + s, cu, cv, cu, cv, c);
		break;
	default: {
		const float d[4][2] = {{x, y - s}, {x + s, y}, {x, y + s}, {x - s, y}};
		quad4(d, cu, cv, c);
	}
	}
}

// The world map (see ui_map)
static void build_map(void) {
	static const uint8_t back[4] = {20, 26, 34, 255}, frame[4] = {0, 0, 0, 200}, white[4] = {235, 232, 222, 255},
	                     grey[4] = {170, 166, 156, 255}, gold[4] = {236, 206, 140, 255}, edge[4] = {222, 196, 140, 150};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w, H = (float)screen_h;
	const UiMap *v = &map_view;
	{ // (the backdrop: the atlas's solid cell, under the images)
		unders[0].tex = tex;
		const float r[4] = {0, 0, W, H}, uv[4] = {cu, cv, cu, cv};
		memcpy(unders[0].rect, r, sizeof(r)), memcpy(unders[0].uv, uv, sizeof(uv)), memcpy(unders[0].col, back, 4);
		under_count = 1;
	}
	const float view_h = H - 50.0f, ppc = view_h / v->cells; // pixels a cell
	const float ox = W * 0.5f - v->centre[0] * ppc, oy = view_h * 0.5f + v->centre[1] * ppc;
#define SX(cx) (ox + (cx) * ppc)
#define SY(cy) (oy - (cy) * ppc)
	// The images: the province, and up close the region over it
	if (v->province_tex && under_count < 3) {
		memset(unders[under_count].col, 255, 4);
		unders[under_count].tex = v->province_tex;
		const float r[4] = {SX(v->province[0]), SY(v->province[3]), SX(v->province[2]), SY(v->province[1])};
		memcpy(unders[under_count].rect, r, sizeof(r));
		const float uv[4] = {0, 0, 1, 1};
		memcpy(unders[under_count++].uv, uv, sizeof(uv));
	}
	if (v->region_tex && v->cells < 60.0f && under_count < 3) {
		memset(unders[under_count].col, 255, 4);
		unders[under_count].tex = v->region_tex;
		const float r[4] = {SX(v->region[0]), SY(v->region[3]), SX(v->region[2]), SY(v->region[1])};
		memcpy(unders[under_count].rect, r, sizeof(r));
		const float uv[4] = {0, 0, 1, 1};
		memcpy(unders[under_count++].uv, uv, sizeof(uv));
	}
	// The region's edge (where you can go)
	{
		const float x0 = SX(v->region[0] + 2), x1 = SX(v->region[2] - 6), y0 = SY(v->region[3] - 4), y1 = SY(v->region[1] + 4);
		line2(x0, y0, x1, y0, 1.5f, cu, cv, edge), line2(x1, y0, x1, y1, 1.5f, cu, cv, edge);
		line2(x1, y1, x0, y1, 1.5f, cu, cv, edge), line2(x0, y1, x0, y0, 1.5f, cu, cv, edge);
	}
	const float s = v->cells < 40.0f ? 7.0f : v->cells < 80.0f ? 5.5f : 4.5f;
	for (int i = 0; i < v->count; i++) {
		const float x = SX(v->mx[i]), y = SY(v->my[i]);
		if (x < -10 || x > W + 10 || y < -10 || y > view_h + 10)
			continue;
		const int st = v->state[i];
		const uint8_t c[4] = {(uint8_t)(st == 2 ? 240 : st == 1 ? 150 : 95), (uint8_t)(st == 2 ? 225 : st == 1 ? 140 : 90),
		                      (uint8_t)(st == 2 ? 190 : st == 1 ? 125 : 85), (uint8_t)(st ? 255 : 170)};
		map_icon(v->kind[i], x, y, i == v->selected ? s * 1.4f : s, cu, cv, i == v->selected ? gold : c);
	}
	for (int q = 0; q < v->quest_on && q < UI_MAP_QUESTS; q++) { // The active quests' objectives: Skyrim's gold markers, pointed diamonds
		const float x = SX(v->quest[q][0]), y = SY(v->quest[q][1]);
		if (x > -20 && x < W + 20 && y > -20 && y < view_h + 20) {
			const uint8_t dark[4] = {40, 30, 10, 220};
			const float o[4][2] = {{x, y - 15}, {x + 9, y - 3}, {x, y + 6}, {x - 9, y - 3}};
			const float in[4][2] = {{x, y - 12}, {x + 6, y - 3}, {x, y + 3}, {x - 6, y - 3}};
			quad4(o, cu, cv, dark);
			quad4(in, cu, cv, gold);
		}
	}
	// You: an arrow the way you face
	{
		const float x = SX(v->player[0]), y = SY(v->player[1]), a = v->player[2], r = 9.0f;
		const float tip[2] = {x + sinf(a) * r, y - cosf(a) * r};
		const float l[2] = {x + sinf(a + 2.5f) * r * 0.8f, y - cosf(a + 2.5f) * r * 0.8f};
		const float rt[2] = {x + sinf(a - 2.5f) * r * 0.8f, y - cosf(a - 2.5f) * r * 0.8f};
		const float arrow[4][2] = {{tip[0], tip[1]}, {rt[0], rt[1]}, {x, y}, {l[0], l[1]}};
		const uint8_t red[4] = {230, 60, 40, 255};
		quad4(arrow, cu, cv, red);
	}
	// The crosshair (the view's middle) and the selected place's card
	const uint8_t cross[4] = {255, 255, 255, 140};
	quad(W * 0.5f - 8, view_h * 0.5f - 0.5f, W * 0.5f + 8, view_h * 0.5f + 0.5f, cu, cv, cu, cv, cross);
	quad(W * 0.5f - 0.5f, view_h * 0.5f - 8, W * 0.5f + 0.5f, view_h * 0.5f + 8, cu, cv, cu, cv, cross);
	if (map_text[0][0]) {
		const int n = (int)strlen(map_text[0]), m = (int)strlen(map_text[1]);
		const float w = fmaxf(text_width(map_text[0], n), text_width(map_text[1], m)) + 40.0f;
		const float x0 = (W - w) * 0.5f, y0 = 16.0f;
		quad(x0 - 1, y0 - 1, x0 + w + 1, y0 + line_h * 2 + 21, cu, cv, cu, cv, edge);
		quad(x0, y0, x0 + w, y0 + line_h * 2 + 20, cu, cv, cu, cv, frame);
		draw_text(map_text[0], n, (W - text_width(map_text[0], n)) * 0.5f, y0 + 6, gold);
		draw_text(map_text[1], m, (W - text_width(map_text[1], m)) * 0.5f, y0 + 10 + line_h, grey);
	}
	quad(0, view_h, W, H, cu, cv, cu, cv, frame);
	draw_text(map_text[2], (int)strlen(map_text[2]), 24.0f, view_h + 12.0f, white);
#undef SX
#undef SY
}

// The save list (see ui_save_list)
static void build_saves(void) {
	static const uint8_t dim[4] = {0, 0, 0, 200}, panel[4] = {0, 0, 0, 170}, edge[4] = {200, 196, 186, 150},
	                     white[4] = {235, 232, 222, 255}, grey[4] = {150, 146, 138, 255}, gold[4] = {236, 206, 140, 255},
	                     bar[4] = {0, 0, 0, 210}, sel[4] = {255, 255, 255, 26}, black[4] = {0, 0, 0, 255};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w, H = (float)screen_h;
	const float bottom = H - 44.0f, row = line_h + 6.0f;
	quad(0, 0, W, H, cu, cv, cu, cv, dim);
	// The list (a window around the selection)
	const float lx0 = 18.0f, lx1 = 470.0f;
	quad(lx0, 0, lx1, bottom, cu, cv, cu, cv, panel);
	const int tl = (int)strlen(save_title);
	draw_text(save_title, tl, lx0 + 20.0f, 14.0f, gold);
	const int rows = (int)((bottom - 60.0f) / row);
	int first = saves.selected - rows / 2;
	if (first > saves.count - rows)
		first = saves.count - rows;
	if (first < 0)
		first = 0;
	float y = 56.0f;
	for (int i = first; i < saves.count && i < first + rows; i++, y += row) {
		if (i == saves.selected)
			quad(lx0, y - 3, lx1, y + line_h + 3, cu, cv, cu, cv, sel);
		int n = (int)strlen(save_rows[i]);
		while (n > 1 && text_width(save_rows[i], n) > lx1 - lx0 - 36.0f)
			n--;
		draw_text(save_rows[i], n, lx0 + 20.0f, y, i == saves.selected ? white : grey);
	}
	// The scene and the details
	const float px0 = 494.0f, px1 = W - 20.0f, py0 = 24.0f, py1 = py0 + (px1 - px0) * 9.0f / 16.0f;
	quad(px0 - 2, py0 - 2, px1 + 2, py1 + 2, cu, cv, cu, cv, edge);
	quad(px0, py0, px1, py1, cu, cv, cu, cv, black);
	if (saves.has_picture) {
		img_shown = 1;
		img_rect[0] = px0, img_rect[1] = py0, img_rect[2] = px1, img_rect[3] = py1;
	}
	y = py1 + 14.0f;
	for (int k = 0; k < 4; k++, y += row)
		if (save_lines[k][0])
			draw_wrapped(save_lines[k], px0, y, px1 - px0, 1, row, k ? grey : white);
	quad(0, bottom, W, H, cu, cv, cu, cv, bar);
	draw_text(save_hints, (int)strlen(save_hints), 24.0f, bottom + 8.0f, grey);
}

// The race menu (see ui_race_menu)
static void build_race(void) {
	static const uint8_t panel[4] = {0, 0, 0, 175}, card[4] = {0, 0, 0, 190}, edge[4] = {200, 196, 186, 150},
	                     white[4] = {235, 232, 222, 255}, grey[4] = {150, 146, 138, 255}, gold[4] = {236, 206, 140, 255},
	                     rule[4] = {150, 146, 138, 120}, bar[4] = {0, 0, 0, 200}, sel[4] = {255, 255, 255, 26};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w, H = (float)screen_h;
	const float bottom = H - 44.0f, row = line_h + 4.0f;
	// The races
	const float lx0 = 18.0f, lx1 = 235.0f;
	quad(lx0, 0, lx1, bottom, cu, cv, cu, cv, panel);
	float y = (bottom - race.count * row) * 0.5f;
	for (int i = 0; i < race.count; i++, y += row) {
		if (i == race.selected)
			quad(lx0, y - 2, lx1, y + line_h + 2, cu, cv, cu, cv, sel);
		draw_text(race_names[i], (int)strlen(race_names[i]), lx0 + 22.0f, y, i == race.selected ? white : grey);
	}
	// The face, over the character
	if (race_face[0]) {
		const int n = (int)strlen(race_face);
		draw_text(race_face, n, (lx1 + 590.0f - text_width(race_face, n)) * 0.5f, 18.0f, gold);
	}
	// The card
	const float kx0 = 590.0f, kx1 = W - 16.0f, ky0 = 12.0f, ky1 = bottom - 12.0f;
	quad(kx0 - 1, ky0 - 1, kx1 + 1, ky1 + 1, cu, cv, cu, cv, edge);
	quad(kx0, ky0, kx1, ky1, cu, cv, cu, cv, card);
	const int tn = (int)strlen(race_title);
	y = ky0 + 10.0f;
	draw_text(race_title, tn, (kx0 + kx1 - text_width(race_title, tn)) * 0.5f, y, white);
	y += row;
	quad(kx0 + 16.0f, y, kx1 - 16.0f, y + 1.0f, cu, cv, cu, cv, rule);
	y += 8.0f;
	y = draw_wrapped(race_desc, kx0 + 14.0f, y, kx1 - kx0 - 28.0f, 4, row, grey) + 6.0f;
	for (int k = 0; k < 8 && y < ky1 - row; k++)
		if (race_lines[k][0])
			y = draw_wrapped(race_lines[k], kx0 + 14.0f, y, kx1 - kx0 - 28.0f, (int)((ky1 - y) / row), row,
			                 race_lines[k][0] == '+' ? white : gold);
	// Hints
	quad(0, bottom, W, H, cu, cv, cu, cv, bar);
	draw_text(race_hints, (int)strlen(race_hints), 24.0f, bottom + 8.0f, grey);
}

// The character creator (see ui_chargen), as Skyrim's race menu lays it out (the user's reference
// video): the tabs along the top, the selected one in the middle; a narrow column left of the
// middle - the races, or the tab's sliders - the selected one large in the middle of the screen, the
// rest smaller and fading away from it; you to the right of the middle; the race's description over
// you lower down; Done, your name and race along the bottom
static void build_chargen(void) {
	static const uint8_t shade[4] = {0, 0, 0, 14}, card[4] = {0, 0, 0, 120}, edge[4] = {200, 196, 186, 150},
	                     white[4] = {240, 237, 228, 255}, gold[4] = {236, 206, 140, 255}, bar[4] = {0, 0, 0, 200},
	                     track[4] = {120, 117, 110, 210}, filled[4] = {215, 210, 198, 235};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w, H = (float)screen_h;
	const float bottom = H - 40.0f, mid = H * 0.5f;
	// (away from the selected one: greyer and fainter)
#define FADE(c, k) {(uint8_t)((c)[0] - 18 * (k)), (uint8_t)((c)[1] - 18 * (k)), (uint8_t)((c)[2] - 18 * (k)), \
                    (uint8_t)((k) >= 4 ? 70 : 230 - 40 * (k))}
	// The tabs
	const float ty0 = 22.0f, ty1 = ty0 + line_h + 12.0f;
	quad(130.0f, ty0, W - 130.0f, ty1, cu, cv, cu, cv, bar);
	quad(130.0f, ty1, W - 130.0f, ty1 + 1.0f, cu, cv, cu, cv, edge);
	draw_text("L", 1, 108.0f, ty0 + 6.0f, gold);
	draw_text("R", 1, W - 120.0f, ty0 + 6.0f, gold);
	const float slot = (W - 300.0f) / 5.0f;
	for (int d = -2; d <= 2; d++) {
		const int i = cg.tab + d;
		if (i < 0 || i >= cg.tab_count)
			continue;
		const int n = (int)strlen(cg_tabs[i]), k = d < 0 ? -d : d;
		const float cx = W * 0.5f + d * slot;
		const uint8_t col[4] = FADE(white, k * 2);
		if (!d) {
			const float tw = text_width_big(cg_tabs[i], 1.15f, 0.0f);
			draw_text("<", 1, cx - tw * 0.5f - 22.0f, ty0 + 6.0f, gold);
			draw_text(">", 1, cx + tw * 0.5f + 12.0f, ty0 + 6.0f, gold);
			draw_text_big(cg_tabs[i], n, cx - tw * 0.5f, ty0 + 4.0f, 1.15f, 0.0f, white);
		} else {
			draw_text(cg_tabs[i], n, cx - text_width(cg_tabs[i], n) * 0.5f, ty0 + 6.0f, col);
		}
	}
	// The column, left of the middle
	const float cx0 = 175.0f, cx1 = 395.0f, ccx = (cx0 + cx1) * 0.5f;
	(void)shade; // (no box behind it: Skyrim's has none - its words over the scene)
	if (cg.race_count) { // The races: the selected one large in the middle
		const float row = line_h + 8.0f;
		for (int i = 0; i < cg.race_count; i++) {
			const int k = i < cg.race_selected ? cg.race_selected - i : i - cg.race_selected;
			const float y = mid - line_h * 0.6f + (i - cg.race_selected) * row + (i < cg.race_selected ? -8.0f : i > cg.race_selected ? 8.0f : 0.0f);
			if (y < ty1 + 16.0f || y > bottom - line_h - 16.0f)
				continue;
			const int n = (int)strlen(cg_races[i]);
			if (!k) {
				const float tw = text_width_big(cg_races[i], 1.35f, 0.0f);
				draw_text_big(cg_races[i], n, ccx - tw * 0.5f, y - 4.0f, 1.35f, 0.0f, white);
			} else {
				const uint8_t col[4] = FADE(white, k + 1);
				draw_text(cg_races[i], n, ccx - text_width(cg_races[i], n) * 0.5f, y, col);
			}
		}
		// Its description over you, lower down (Skyrim's: over the chest); its skill bonuses under it.
		// Measured first (drawn clear), then drawn on its card
		const float dx0 = 404.0f, dx1 = W - 40.0f, row2 = line_h + 2.0f;
		static const uint8_t clear[4] = {0, 0, 0, 0};
		float end = draw_wrapped(cg_desc, dx0 + 12.0f, 0.0f, dx1 - dx0 - 24.0f, 8, row2, clear) + 4.0f;
		if (cg_lines[0][0])
			end = draw_wrapped(cg_lines[0], dx0 + 12.0f, end, dx1 - dx0 - 24.0f, 2, row2, clear);
		float y = bottom - 14.0f - (end + 18.0f);
		if (y < H * 0.5f)
			y = H * 0.5f;
		quad(dx0, y, dx1, bottom - 14.0f, cu, cv, cu, cv, card);
		y = draw_wrapped(cg_desc, dx0 + 12.0f, y + 9.0f, dx1 - dx0 - 24.0f, 8, row2, white) + 4.0f;
		if (cg_lines[0][0] && y < bottom - 14.0f - row2)
			draw_wrapped(cg_lines[0], dx0 + 12.0f, y, dx1 - dx0 - 24.0f, 2, row2, gold);
	} else { // The sliders: each its name over its bar, the selected one large in the middle
		const float item = line_h * 2.0f + 18.0f;
		for (int i = 0; i < cg.row_count; i++) {
			const int k = i < cg.row ? cg.row - i : i - cg.row, on = !k;
			const float y = mid - line_h + (i - cg.row) * item + (i < cg.row ? -6.0f : i > cg.row ? 6.0f : 0.0f);
			if (y < ty1 + 16.0f || y > bottom - item)
				continue;
			const int n = (int)strlen(cg_rows[i]);
			const uint8_t col[4] = FADE(white, k + 1), mark[4] = FADE(white, k);
			if (on) {
				const float tw = text_width_big(cg_rows[i], 1.25f, 0.0f);
				draw_text_big(cg_rows[i], n, ccx - tw * 0.5f, y - 6.0f, 1.25f, 0.0f, white);
			} else {
				draw_text(cg_rows[i], n, ccx - text_width(cg_rows[i], n) * 0.5f, y, col);
			}
			// Its bar: the track, filled to where it sits, a notch there
			const float bx0 = cx0 + 20.0f, bx1 = cx1 - 20.0f, by = y + line_h + (on ? 12.0f : 7.0f);
			const float f = cg_fills[i] < 0.0f ? 0.0f : cg_fills[i] > 1.0f ? 1.0f : cg_fills[i];
			const float at = bx0 + (bx1 - bx0) * f;
			const uint8_t tr[4] = {track[0], track[1], track[2], (uint8_t)(on ? 220 : 150 - 25 * (k > 4 ? 4 : k))};
			quad(bx0, by, bx1, by + 3.0f, cu, cv, cu, cv, tr);
			quad(bx0, by, at, by + 3.0f, cu, cv, cu, cv, on ? filled : tr);
			quad(at - 3.0f, by - 5.0f, at + 3.0f, by + 8.0f, cu, cv, cu, cv, on ? gold : mark);
			if (on) {
				draw_text("<", 1, bx0 - 16.0f, by - 7.0f, gold);
				draw_text(">", 1, bx1 + 7.0f, by - 7.0f, gold);
				if (cg_values[i][0]) {
					const int vn = (int)strlen(cg_values[i]);
					draw_text(cg_values[i], vn, ccx - text_width(cg_values[i], vn) * 0.5f, by + 10.0f, gold);
				}
			}
		}
	}
#undef FADE
	// Along the bottom: Done and the controls; your name and race
	quad(0, bottom, W, H, cu, cv, cu, cv, bar);
	draw_text(cg_hints, (int)strlen(cg_hints), 24.0f, bottom + 8.0f, white);
	static char right[96];
	snprintf(right, sizeof(right), "%s%s%sRACE %s", cg_name[0] ? "NAME " : "", cg_name, cg_name[0] ? "     " : "", cg_race_name);
	const int rn = (int)strlen(right);
	draw_text(right, rn, W - 24.0f - text_width(right, rn), bottom + 8.0f, white);
}

// A centred panel over the dimmed game: the title, then the items (the selected one gold,
// with arrows); "Label|Value" items put the value on the right
// Skyrim's inventory screen (see ui_inventory)
static void build_inventory(void) {
	static const uint8_t panel[4] = {0, 0, 0, 175}, panel2[4] = {6, 6, 6, 205}, rule[4] = {150, 146, 138, 120},
	                     white[4] = {235, 232, 222, 255}, grey[4] = {150, 146, 138, 255}, dimmed[4] = {110, 107, 100, 255},
	                     gold[4] = {236, 206, 140, 255}, card[4] = {0, 0, 0, 185}, edge[4] = {200, 196, 186, 150},
	                     bar[4] = {0, 0, 0, 200}, sel[4] = {255, 255, 255, 26};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w, H = (float)screen_h;
	const float bottom = H - 44.0f, row = line_h + 6.0f;
	// The two columns
	const float cx0 = 18.0f, cx1 = 205.0f, ix0 = 215.0f, ix1 = 545.0f;
	quad(cx0, 0, cx1, bottom, cu, cv, cu, cv, panel);
	quad(ix0, 0, ix1, bottom, cu, cv, cu, cv, panel2);
	quad(cx1 + 1, 0, cx1 + 2, bottom, cu, cv, cu, cv, rule);
	quad(ix1, 0, ix1 + 1, bottom, cu, cv, cu, cv, rule);
	// Categories, centred vertically, the selected one bright
	float y = (bottom - inv.category_count * row) * 0.5f;
	for (int i = 0; i < inv.category_count; i++, y += row) {
		const char *c = inv_categories[i];
		const uint8_t *col = i == inv.category ? (inv.focus == 0 ? white : gold) : grey;
		if (i == inv.category && inv.focus == 0)
			quad(cx0, y - 3, cx1, y + line_h + 3, cu, cv, cu, cv, sel);
		draw_text(c, (int)strlen(c), cx0 + 26.0f, y, col);
	}
	// Items: a window around the selection
	const int rows = (int)((bottom - 40.0f) / row);
	int first = inv.selected - rows / 2;
	if (first > inv.count - rows)
		first = inv.count - rows;
	if (first < 0)
		first = 0;
	y = 24.0f;
	for (int i = first; i < inv.count && i < first + rows; i++, y += row) {
		const int on = i == inv.selected && inv.focus == 1;
		if (on)
			quad(ix0, y - 3, ix1, y + line_h + 3, cu, cv, cu, cv, sel);
		const uint8_t *col = on ? white : inv.focus == 1 ? grey : dimmed;
		float maxw = ix1 - ix0 - 40.0f - (inv_marks[i][0] ? text_width(inv_marks[i], (int)strlen(inv_marks[i])) + 12.0f : 0);
		int n = (int)strlen(inv_names[i]);
		while (n > 1 && text_width(inv_names[i], n) > maxw)
			n--;
		draw_text(inv_names[i], n, ix0 + 20.0f, y, col);
		if (inv_marks[i][0]) {
			int m = (int)strlen(inv_marks[i]);
			draw_text(inv_marks[i], m, ix1 - 16.0f - text_width(inv_marks[i], m), y, gold);
		}
	}
	if (!inv.count) {
		const char *none = "Nothing here";
		draw_text(none, (int)strlen(none), ix0 + 20.0f, 24.0f, dimmed);
	}
	// The card, lower right (the 3D model is above it)
	if (inv_title[0]) {
		const float kx0 = 575.0f, kx1 = W - 22.0f, ky1 = bottom - 16.0f;
		int lines = 0;
		for (int k = 0; k < 4; k++)
			lines += inv_lines[k][0] != 0;
		const float ky0 = ky1 - (24.0f + row * 1.4f + (lines ? row : 0) + (inv_text[0] ? row * 2.2f : 0) + 10.0f);
		quad(kx0 - 1, ky0 - 1, kx1 + 1, ky1 + 1, cu, cv, cu, cv, edge);
		quad(kx0, ky0, kx1, ky1, cu, cv, cu, cv, card);
		int n = (int)strlen(inv_title);
		while (n > 1 && text_width(inv_title, n) > kx1 - kx0 - 20.0f)
			n--;
		float ty = ky0 + 12.0f;
		draw_text(inv_title, n, (kx0 + kx1 - text_width(inv_title, n)) * 0.5f, ty, white);
		ty += row;
		quad(kx0 + 20.0f, ty, kx1 - 20.0f, ty + 1.0f, cu, cv, cu, cv, rule);
		ty += 10.0f;
		// The stats side by side
		float total = 0.0f;
		for (int k = 0; k < 4; k++)
			total += inv_lines[k][0] ? text_width(inv_lines[k], (int)strlen(inv_lines[k])) + 26.0f : 0.0f;
		float tx = (kx0 + kx1 - total + 26.0f) * 0.5f;
		for (int k = 0; k < 4; k++) {
			if (!inv_lines[k][0])
				continue;
			int m = (int)strlen(inv_lines[k]);
			draw_text(inv_lines[k], m, tx, ty, grey);
			tx += text_width(inv_lines[k], m) + 26.0f;
		}
		if (lines)
			ty += row;
		if (inv_text[0]) { // the effect, wrapped over two lines at most
			const char *t = inv_text;
			for (int line = 0; line < 2 && *t; line++) {
				int len = (int)strlen(t), cut = len;
				while (cut > 0 && text_width(t, cut) > kx1 - kx0 - 30.0f) {
					cut--;
					while (cut > 0 && t[cut] != ' ')
						cut--;
				}
				if (cut <= 0)
					cut = len;
				draw_text(t, cut, (kx0 + kx1 - text_width(t, cut)) * 0.5f, ty, white);
				ty += row;
				t += cut;
				while (*t == ' ')
					t++;
			}
		}
	}
	// The bottom bar
	quad(0, bottom, W, H, cu, cv, cu, cv, bar);
	quad(0, bottom, W, bottom + 1.0f, cu, cv, cu, cv, rule);
	draw_text(inv_hints, (int)strlen(inv_hints), 24.0f, bottom + (H - bottom - line_h) * 0.5f, grey);
	int sn = (int)strlen(inv_status);
	draw_text(inv_status, sn, W - 24.0f - text_width(inv_status, sn), bottom + (H - bottom - line_h) * 0.5f, white);
}

void ui_title(int on) {
	title_on = on;
}

static char title_note[112];
static int title_note_warning;

void ui_title_note(const char *text, int warning) {
	snprintf(title_note, sizeof(title_note), "%s", text ? text : "");
	title_note_warning = warning;
}

// Text drawn bigger (the title): each glyph's cell scaled about its top left; tracking: extra
// space between letters
static float draw_text_big(const char *s, int n, float x, float y, float scale, float tracking, const uint8_t col[4]) {
	for (int i = 0; i < n; i++) {
		int c = (uint8_t)s[i];
		if (!glyph_ok(c))
			c = '?';
		if (c != ' ') {
			float u0, v0, u1, v1;
			cell_uv(c, &u0, &v0, &u1, &v1);
			quad(x, y, x + cell_w * scale, y + cell_h * scale, u0, v0, u1, v1, col);
		}
		x += advance[c] * scale + tracking;
	}
	return x;
}

static float text_width_big(const char *s, float scale, float tracking) {
	const int n = (int)strlen(s);
	return text_width(s, n) * scale + tracking * (n - 1);
}

// A paragraph centred in lines no wider than max_w (at scale); the y under it
static float draw_para(const char *t, float y, float max_w, float scale, const uint8_t col[4]) {
	const int len = (int)strlen(t);
	int start = 0;
	while (start < len) {
		int end = len, last_space = -1;
		for (int k = start; k <= len; k++) {
			if (k == len || t[k] == ' ') {
				if (text_width(t + start, k - start) * scale > max_w && last_space > start)
					break;
				last_space = k;
				if (k == len)
					break;
			}
		}
		end = last_space > start ? last_space : len;
		const float w = text_width(t + start, end - start) * scale;
		draw_text_big(t + start, end - start, (screen_w - w) * 0.5f, y, scale, 0.0f, col);
		y += line_h * scale + 2.0f;
		start = end + 1;
	}
	return y;
}

// While it starts: "<the studio> presents" and the game's name, as the user asked (Nov 8) - the studio's name from their own
// files (legal.txt); the step it's on at the bottom. (Its legal notice and OpenSKY's note: at the head of the title's Credits)
static void build_boot(void) {
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w;
	static const uint8_t black[4] = {0, 0, 0, 255}, dim[4] = {140, 140, 140, 255}, silver[4] = {214, 214, 214, 255},
	                     rule[4] = {170, 170, 170, 120}, gold[4] = {222, 200, 150, 255}, grey[4] = {150, 148, 142, 255};
	quad(0, 0, W, (float)screen_h, cu, cv, cu, cv, black);
	const float big = 2.6f;
	float y = screen_h * 0.2f;
	y += 60.0f; // (the studio's card is the opening's now - after New Game, as Skyrim's; the user, Nov 10)
	if (title_over[0]) {
		draw_text_big(title_over, (int)strlen(title_over), (W - text_width_big(title_over, 0.9f, 6.0f)) * 0.5f, y, 0.9f, 6.0f, dim);
		y += line_h * 0.9f + 8.0f;
	}
	draw_text_big(title_name, (int)strlen(title_name), (W - text_width_big(title_name, big, 12.0f)) * 0.5f, y, big, 12.0f, silver);
	y += cell_h * big + 6.0f;
	quad(W * 0.5f - 150.0f, y, W * 0.5f + 150.0f, y + 1.0f, cu, cv, cu, cv, rule);
	if (boot_step[0]) {
		char l[112];
		snprintf(l, sizeof(l), "Loading: %s", boot_step);
		const float w = text_width(l, (int)strlen(l)) * 0.7f;
		draw_text_big(l, (int)strlen(l), (W - w) * 0.5f, screen_h - 40.0f, 0.7f, 0.0f, dim);
	}
}

// The title menu's Credits: one page - the studio, the game's name, its copyright and trademark notice from the user's own
// files, and OpenSKY's note (not every name in the game's credits: too long, the user said, Nov 8)
static void build_credits(void) {
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w;
	static const uint8_t black[4] = {0, 0, 0, 255}, gold[4] = {222, 200, 150, 255}, silver[4] = {214, 214, 214, 255},
	                     grey[4] = {176, 174, 168, 255}, faint[4] = {128, 126, 120, 255}, hint[4] = {120, 118, 112, 255};
	quad(0, 0, W, (float)screen_h, cu, cv, cu, cv, black);
	float y = screen_h * 0.14f;
	if (legal_studio[0]) {
		draw_text_big(legal_studio, (int)strlen(legal_studio), (W - text_width_big(legal_studio, 0.9f, 2.0f)) * 0.5f, y, 0.9f, 2.0f, gold);
		y += line_h * 0.9f + 10.0f;
	}
	char name[96];
	snprintf(name, sizeof(name), "%s%s%s", title_over, title_over[0] ? ": " : "", title_name);
	draw_text_big(name, (int)strlen(name), (W - text_width_big(name, 1.0f, 3.0f)) * 0.5f, y, 1.0f, 3.0f, silver);
	y += line_h + 30.0f;
	if (legal_text[0])
		y = draw_para(legal_text, y, W - 150.0f, 0.66f, grey) + 24.0f;
	draw_para("OpenSKY is a free, unofficial engine - not made, endorsed or supported by the game's makers. It holds none of "
	          "their files: it plays only from your own copy of the game.", y, W - 190.0f, 0.66f, faint);
	static const char *const h = "Circle: back";
	draw_text_big(h, (int)strlen(h), W - text_width(h, (int)strlen(h)) * 0.6f - 18.0f, screen_h - 24.0f, 0.6f, 0.0f, hint);
	(void)credits_scroll;
}

// Skyrim's title: black, its name in wide-set capitals with a rule under, the choices below
static void build_title(void) {
	static const uint8_t black[4] = {0, 0, 0, 255}, silver[4] = {214, 214, 214, 255}, dim[4] = {140, 140, 140, 255},
	                     rule[4] = {170, 170, 170, 120}, white[4] = {240, 240, 240, 255}, grey[4] = {120, 120, 120, 255};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f, W = (float)screen_w;
	quad(0, 0, W, (float)screen_h, cu, cv, cu, cv, black);
	const char *const over = title_over, *const name = title_name;
	const float big = 3.2f, top = screen_h * 0.22f;
	draw_text_big(over, (int)strlen(over), (W - text_width_big(over, 1.0f, 6.0f)) * 0.5f, top, 1.0f, 6.0f, dim);
	const float ny = top + line_h + 10.0f;
	draw_text_big(name, (int)strlen(name), (W - text_width_big(name, big, 14.0f)) * 0.5f, ny, big, 14.0f, silver);
	const float ry = ny + cell_h * big + 6.0f;
	quad(W * 0.5f - 170.0f, ry, W * 0.5f + 170.0f, ry + 1.0f, cu, cv, cu, cv, rule);
	if (title_note[0]) { // (the build - or, in red, that the program and its data aren't a pair)
		static const uint8_t note[4] = {110, 110, 110, 255}, warn[4] = {235, 90, 70, 255};
		const int nn = (int)strlen(title_note);
		draw_text(title_note, nn, (W - text_width(title_note, nn)) * 0.5f, screen_h - line_h - 14.0f,
		          title_note_warning ? warn : note);
	}
	const float row = line_h + 14.0f, y0 = screen_h * 0.62f;
	for (int i = 0; i < menu_count; i++) {
		const int n = (int)strlen(menu_items[i]);
		draw_text(menu_items[i], n, (W - text_width(menu_items[i], n)) * 0.5f, y0 + i * row,
		          i == menu_selected ? white : grey);
		if (i == menu_selected) { // (Skyrim's marks either side of the chosen one)
			const float hw = text_width(menu_items[i], n) * 0.5f + 18.0f, my = y0 + i * row + line_h * 0.5f;
			quad(W * 0.5f - hw - 6.0f, my - 1.0f, W * 0.5f - hw, my + 1.0f, cu, cv, cu, cv, white);
			quad(W * 0.5f + hw, my - 1.0f, W * 0.5f + hw + 6.0f, my + 1.0f, cu, cv, cu, cv, white);
		}
	}
}

static void build_menu(void) {
	if (title_on) {
		build_title();
		return;
	}
	static const uint8_t dim[4] = {0, 0, 0, 140}, box[4] = {12, 10, 8, 200}, edge[4] = {222, 196, 140, 160},
	                     white[4] = {225, 222, 212, 255}, gold[4] = {236, 206, 140, 255}, grey[4] = {150, 146, 138, 255},
	                     shadow[4] = {0, 0, 0, 200};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
	// (laid out first: a line too long for the box wrapped onto the next - a conversation's answers;
	// a name and what's said of it on the right too long for one row, that on a row of its own under
	// it - INFO 0002F7A2 ran into "Locked: needs a dragon soul")
	const float row = line_h + 10.0f, wmax = screen_w - 60.0f;
	float w = 520.0f;
	for (int i = 0; i < menu_count; i++) {
		const char *bar = strchr(menu_items[i], '|');
		const int ll = bar ? (int)(bar - menu_items[i]) : (int)strlen(menu_items[i]);
		const float need = text_width(menu_items[i], ll) + (bar ? text_width(bar + 1, (int)strlen(bar + 1)) + 40.0f : 0.0f) + 72.0f;
		w = fmaxf(w, fminf(need, wmax));
	}
	const float inner = w - 72.0f;
	int lines[MENU_MAX], split_at[MENU_MAX][3], below[MENU_MAX];
	float h = 70.0f + 20.0f;
	for (int i = 0; i < menu_count; i++) {
		const char *item = menu_items[i], *bar = strchr(item, '|');
		const int ll = bar ? (int)(bar - item) : (int)strlen(item);
		lines[i] = 1, below[i] = 0, split_at[i][0] = ll;
		if (bar) {
			below[i] = text_width(item, ll) + text_width(bar + 1, (int)strlen(bar + 1)) + 24.0f > inner;
		} else {
			int start = 0; // (word-wrapped: up to three lines)
			lines[i] = 0;
			while (start < ll && lines[i] < 3) {
				int end = ll, last_space = -1;
				for (int k = start; k <= ll; k++) {
					if (k == ll || item[k] == ' ') {
						if (text_width(item + start, k - start) > inner && last_space > start)
							break;
						last_space = k;
						if (k == ll)
							break;
					}
				}
				end = last_space > start ? last_space : ll;
				split_at[i][lines[i]++] = end;
				start = end + 1;
			}
			if (!lines[i])
				lines[i] = 1, split_at[i][0] = ll;
		}
		h += row * (lines[i] + below[i]) - (lines[i] + below[i] > 1 ? 4.0f * (lines[i] + below[i] - 1) : 0.0f);
	}
	int note_lines = 0; // (Skyrim's words for the chosen one, under the rows: up to three lines)
	if (menu_note[0]) {
		const float nw = text_width(menu_note, (int)strlen(menu_note));
		note_lines = nw > inner * 2.0f ? 3 : nw > inner ? 2 : 1;
		h += 14.0f + note_lines * (row - 4.0f);
	}
	const float x0 = (screen_w - w) * 0.5f, y0 = (screen_h - h) * 0.5f;
	quad(0, 0, (float)screen_w, (float)screen_h, cu, cv, cu, cv, dim);
	quad(x0 - 2, y0 - 2, x0 + w + 2, y0 + h + 2, cu, cv, cu, cv, edge);
	quad(x0, y0, x0 + w, y0 + h, cu, cv, cu, cv, box);
	int tl = (int)strlen(menu_title);
	float tx = (screen_w - text_width(menu_title, tl)) * 0.5f;
	draw_text(menu_title, tl, tx + 2.0f, y0 + 18.0f, shadow);
	draw_text(menu_title, tl, tx, y0 + 16.0f, gold);
	quad(x0 + 40, y0 + 56, x0 + w - 40, y0 + 58, cu, cv, cu, cv, edge);
	float y = y0 + 70.0f;
	for (int i = 0; i < menu_count; i++) {
		const char *item = menu_items[i], *bar = strchr(item, '|');
		int label_len = bar ? (int)(bar - item) : (int)strlen(item);
		const int rows = lines[i] + below[i];
		const float tall = row * rows - 4.0f * (rows - 1);
		const uint8_t *col = i == menu_selected ? gold : (bar ? white : grey);
		if (i == menu_selected)
			quad(x0 + 16, y - 4, x0 + w - 16, y + tall - row + line_h + 2, cu, cv, cu, cv, (const uint8_t[4]){222, 196, 140, 40});
		if (!bar) { // a plain item: centred, line by line
			int start = 0;
			for (int l = 0; l < lines[i]; l++) {
				const int n = split_at[i][l] - start;
				draw_text(item + start, n, (screen_w - text_width(item + start, n)) * 0.5f, y + l * (row - 4.0f),
				          i == menu_selected ? gold : white);
				start = split_at[i][l] + 1;
			}
		} else {
			draw_text(item, label_len, x0 + 36, y, col);
			const char *value = bar + 1;
			int vl = (int)strlen(value);
			draw_text(value, vl, x0 + w - 36 - text_width(value, vl), y + (below[i] ? row - 4.0f : 0.0f), col);
		}
		y += tall;
	}
	if (note_lines) {
		quad(x0 + 40, y + 2, x0 + w - 40, y + 4, cu, cv, cu, cv, edge);
		draw_wrapped(menu_note, x0 + 36, y + 12.0f, inner, note_lines, row - 4.0f, grey);
	}
}

// Skyrim's HUD: magicka (blue) bottom left, health (red) bottom centre, stamina (green)
// bottom right, each draining toward its middle; a notice line top left
// A triangle (a compass point's half)
static void tri(float ax, float ay, float bx, float by, float cx, float cy, float cu, float cv, const uint8_t c[4]) {
	const float p[4][2] = {{ax, ay}, {bx, by}, {cx, cy}, {cx, cy}};
	quad4(p, cu, cv, c);
}

// The compass: Skyrim's bar across the top (the way you face in the middle, N E S W, the places
// about you - discovered ones light, those not yet found dark, a quest's target gold); the rose
// top right that turned with you is gone (the bar's enough, as Skyrim's)
static void build_compass(float cu, float cv) {
	const float bw = 420.0f, bh = 24.0f, bx0 = (screen_w - bw) * 0.5f, bx1 = bx0 + bw, by = 10.0f, mid = screen_w * 0.5f;
	const uint8_t back[4] = {150, 146, 134, 120}, edge[4] = {222, 210, 180, 170}, light[4] = {250, 246, 236, 245},
	              faint[4] = {40, 38, 34, 140};
	quad(bx0, by, bx1, by + bh, cu, cv, cu, cv, back);
	quad(bx0, by - 1, bx1, by, cu, cv, cu, cv, edge);
	quad(bx0, by + bh, bx1, by + bh + 1, cu, cv, cu, cv, edge);
	for (int e = 0; e < 2; e++) { // (its knotted ends: a diamond each side)
		const float ex = e ? bx1 + 8.0f : bx0 - 8.0f, ey = by + bh * 0.5f, r = 10.0f;
		const float d[4][2] = {{ex, ey - r}, {ex + r, ey}, {ex, ey + r}, {ex - r, ey}};
		quad4(d, cu, cv, edge);
		const float in[4][2] = {{ex, ey - r + 3}, {ex + r - 3, ey}, {ex, ey + r - 3}, {ex - r + 3, ey}};
		quad4(in, cu, cv, back);
	}
	const float span = 100.0f; // (degrees either side shown)
	static const char *const dirs[8] = {"N", "", "E", "", "S", "", "W", ""};
	for (int k = 0; k < 24; k++) { // ticks every 15 degrees, letters at the four points
		float rel = fmodf(k * 15.0f - compass_yaw + 540.0f, 360.0f) - 180.0f;
		if (fabsf(rel) > span)
			continue;
		const float x = mid + rel / span * bw * 0.5f;
		if (k % 3 == 0 && dirs[k / 3][0]) {
			const char *l = dirs[k / 3];
			draw_text(l, 1, x - text_width(l, 1) * 0.5f, by + (bh - line_h) * 0.5f, light);
		} else {
			quad(x - 0.5f, by + bh - (k % 3 == 0 ? 9.0f : 5.0f), x + 0.5f, by + bh - 2.0f, cu, cv, cu, cv, faint);
		}
	}
	for (int i = 0; i < compass_n; i++) {
		const UiCompassMark *m = &compass_marks[i];
		float rel = fmodf(m->bearing - compass_yaw + 540.0f, 360.0f) - 180.0f;
		if (fabsf(rel) > span)
			continue;
		const float x = mid + rel / span * bw * 0.5f, edge_fade = fminf(1.0f, (span - fabsf(rel)) / 12.0f);
		if (m->kind < 0) { // (a quest's target: a gold arrowhead)
			const uint8_t gold[4] = {236, 196, 90, (uint8_t)(255 * edge_fade)};
			tri(x - 6.0f, by + 5.0f, x + 6.0f, by + 5.0f, x, by + bh - 4.0f, cu, cv, gold);
			continue;
		}
		const uint8_t found[4] = {250, 248, 240, (uint8_t)(250 * edge_fade)}, unknown[4] = {22, 20, 18, (uint8_t)(230 * edge_fade)},
		              outline[4] = {0, 0, 0, (uint8_t)(140 * edge_fade)};
		if (m->discovered) // (a found place: white, outlined dark so it reads on the bar)
			map_icon_on(m->kind, x + 1.0f, by + bh * 0.5f + 1.0f, 7.0f, cu, cv, outline, 0);
		map_icon_on(m->kind, x, by + bh * 0.5f, 7.0f, cu, cv, m->discovered ? found : unknown, 0);
	}
}

static void build_hud(void) {
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
	// The foe's health, top middle (Skyrim's): its name over a red bar draining to its middle
	if (enemy_frac >= 0.0f) {
		static const uint8_t frame[4] = {0, 0, 0, 170}, edge[4] = {222, 196, 140, 150}, red[4] = {178, 40, 34, 255},
		                     white[4] = {225, 222, 212, 255};
		const float w = 240.0f, h = 7.0f, y = compass_on ? 64.0f : 34.0f, x0 = (screen_w - w) * 0.5f, x1 = x0 + w;
		const int n = (int)strlen(enemy_name);
		draw_text(enemy_name, n, (screen_w - text_width(enemy_name, n)) * 0.5f, y - line_h - 2.0f, white);
		quad(x0 - 3, y - 3, x1 + 3, y + h + 3, cu, cv, cu, cv, edge);
		quad(x0 - 2, y - 2, x1 + 2, y + h + 2, cu, cv, cu, cv, frame);
		const float half = w * 0.5f * enemy_frac, mid = screen_w * 0.5f;
		quad(mid - half, y, mid + half, y + h, cu, cv, cu, cv, red);
	}
	if (bar_alpha > 0.0f) {
		static const uint8_t fill[3][3] = {{60, 96, 200}, {178, 40, 34}, {64, 150, 70}};
		const float w = 190.0f, h = 7.0f, y = screen_h - 30.0f;
		const float cx[3] = {40.0f + w * 0.5f, screen_w * 0.5f, screen_w - 40.0f - w * 0.5f};
		const uint8_t a = (uint8_t)(bar_alpha * 255.0f), frame[4] = {0, 0, 0, (uint8_t)(bar_alpha * 170.0f)},
		              edge[4] = {222, 196, 140, (uint8_t)(bar_alpha * 150.0f)};
		for (int i = 0; i < 3; i++) {
			float x0 = cx[i] - w * 0.5f, x1 = cx[i] + w * 0.5f;
			quad(x0 - 3, y - 3, x1 + 3, y + h + 3, cu, cv, cu, cv, edge);
			quad(x0 - 2, y - 2, x1 + 2, y + h + 2, cu, cv, cu, cv, frame);
			float half = w * 0.5f * fmaxf(0.0f, fminf(1.0f, bar_frac[i]));
			const uint8_t col[4] = {fill[i][0], fill[i][1], fill[i][2], a};
			quad(cx[i] - half, y, cx[i] + half, y + h, cu, cv, cu, cv, col);
		}
		// Breath: a pale blue bar above the health bar, shrinking while you're under water
		if (breath_frac < 0.999f) {
			const float bw = w * 0.6f, by = y - 20.0f, x0 = cx[1] - bw * 0.5f, x1 = cx[1] + bw * 0.5f;
			quad(x0 - 3, by - 3, x1 + 3, by + h + 3, cu, cv, cu, cv, edge);
			quad(x0 - 2, by - 2, x1 + 2, by + h + 2, cu, cv, cu, cv, frame);
			float half = bw * 0.5f * fmaxf(0.0f, fminf(1.0f, breath_frac));
			const uint8_t col[4] = {150, 205, 235, a};
			quad(cx[1] - half, by, cx[1] + half, by + h, cu, cv, cu, cv, col);
		}
	}
	if (compass_on)
		build_compass(cu, cv);
	if (crosshair_on) { // the crosshair: a small dot, brighter over something to use
		const float cx = screen_w * 0.5f, cy = screen_h * 0.5f, r = crosshair_hot ? 3.0f : 2.0f;
		const uint8_t shade[4] = {0, 0, 0, 120}, dot[4] = {236, 226, 200, (uint8_t)(crosshair_hot ? 235 : 150)};
		quad(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, cu, cv, cu, cv, shade);
		quad(cx - r, cy - r, cx + r, cy + r, cu, cv, cu, cv, dot);
	}
	if (notice_left > 0.0f && notice_text[0]) { // top right, in a box (the top left is under Vita3K's FPS counter)
		float a = fminf(1.0f, notice_left / 0.4f); // (fades out at the end)
		const uint8_t box[4] = {0, 0, 0, (uint8_t)(170 * a)}, edge[4] = {222, 196, 140, (uint8_t)(160 * a)},
		              gold[4] = {236, 206, 140, (uint8_t)(255 * a)};
		int n = (int)strlen(notice_text);
		float w = text_width(notice_text, n), x1 = screen_w - 24.0f, x0 = x1 - w - 28.0f,
		      y0 = compass_on ? 46.0f : 18.0f, y1 = y0 + line_h + 16.0f; // (under the compass bar)
		quad(x0 - 1, y0 - 1, x1 + 1, y1 + 1, cu, cv, cu, cv, edge);
		quad(x0, y0, x1, y1, cu, cv, cu, cv, box);
		draw_text(notice_text, n, x0 + 14.0f, y0 + 8.0f, gold);
	}
}

// Skyrim's character menu: Skills up, Items right, Magic left, Map down, around a centre
static void build_tween(void) {
	static const uint8_t dim[4] = {0, 0, 0, 150}, gold[4] = {236, 206, 140, 255}, white[4] = {225, 222, 212, 255},
	                     shadow[4] = {0, 0, 0, 220}, edge[4] = {222, 196, 140, 120};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
	quad(0, 0, (float)screen_w, (float)screen_h, cu, cv, cu, cv, dim);
	const float cx = screen_w * 0.5f, cy = screen_h * 0.5f, r = 150.0f;
	quad(cx - r, cy - 1, cx + r, cy + 1, cu, cv, cu, cv, edge); // the cross between them
	quad(cx - 1, cy - r * 0.75f, cx + 1, cy + r * 0.75f, cu, cv, cu, cv, edge);
	static const char *const labels[4] = {"Skills", "Items", "Magic", "Map"};
	const float at[4][2] = {{cx, cy - r * 0.75f - line_h - 8}, {cx + r + 16, cy - line_h * 0.5f},
	                        {cx - r - 16, cy - line_h * 0.5f}, {cx, cy + r * 0.75f + 8}};
	for (int i = 0; i < 4; i++) {
		int n = (int)strlen(labels[i]);
		float w = text_width(labels[i], n);
		float x = i == 1 ? at[i][0] : i == 2 ? at[i][0] - w : at[i][0] - w * 0.5f;
		draw_text(labels[i], n, x + 2.0f, at[i][1] + 2.0f, shadow);
		draw_text(labels[i], n, x, at[i][1], gold);
	}
	static const char *hint = "(O) Back";
	int hn = (int)strlen(hint);
	draw_text(hint, hn, cx - text_width(hint, hn) * 0.5f, screen_h - line_h - 24.0f, white);
}

static char prompt_button[12] = "O"; // (the button Activate is on: Button mapping)
void ui_set_prompt_button(const char *glyph) {
	snprintf(prompt_button, sizeof(prompt_button), "%s", glyph && glyph[0] ? glyph : "O");
}

// "(O) Sleeping Giant Inn", centred above the subtitle area ("~Oil": named only, no button)
static void build_prompt(void) {
	char text[128];
	int len = prompt_text[0] == '~' ? snprintf(text, sizeof(text), "%s", prompt_text + 1)
	                                : snprintf(text, sizeof(text), "(%s) %s", prompt_button, prompt_text);
	if (len > (int)sizeof(text) - 1)
		len = (int)sizeof(text) - 1;
	float w = text_width(text, len), x = (screen_w - w) * 0.5f, y = screen_h * 0.62f;
	static const uint8_t box[4] = {0, 0, 0, 110}, shadow[4] = {0, 0, 0, 200}, col[4] = {235, 232, 222, 255},
	                     gold[4] = {222, 196, 140, 255};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
	quad(x - BOX_PAD, y - BOX_PAD * 0.4f, x + w + BOX_PAD, y + line_h + BOX_PAD * 0.4f, cu, cv, cu, cv, box);
	draw_text(text, len, x + 2.0f, y + 2.0f, shadow);
	float after = draw_text(text, 3, x, y, gold);
	draw_text(text + 3, len - 3, after, y, col);
}

static void build_subtitle(void) {
	// Word-wrap "Speaker: text" into at most MAX_LINES lines
	char full[1100];
	int speaker_len = speaker_text[0] ? snprintf(full, sizeof(full), "%s: ", speaker_text) : 0;
	snprintf(full + speaker_len, sizeof(full) - speaker_len, "%s", body_text);
	const float max_w = screen_w * SUB_WIDTH_FRAC;
	int starts[MAX_LINES + 1], lens[MAX_LINES], lines = 0, pos = 0, len = (int)strlen(full);
	while (pos < len && lines < MAX_LINES) {
		while (full[pos] == ' ')
			pos++;
		int end = pos, last_space = -1;
		while (end < len && text_width(full + pos, end - pos + 1) <= max_w) {
			if (full[end] == ' ')
				last_space = end;
			end++;
		}
		if (end < len && last_space > pos)
			end = last_space;
		starts[lines] = pos, lens[lines] = end - pos;
		lines++;
		pos = end;
	}
	if (!lines)
		return;
	float widest = 0.0f;
	for (int i = 0; i < lines; i++) {
		float w = text_width(full + starts[i], lens[i]);
		widest = w > widest ? w : widest;
	}
	float top = screen_h - SUB_BOTTOM - lines * line_h;
	// Backing box, then shadowed text (the speaker's name in a warm tint)
	static const uint8_t box[4] = {0, 0, 0, 120}, shadow[4] = {0, 0, 0, 200}, white[4] = {235, 232, 222, 255},
	                     gold[4] = {222, 196, 140, 255};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
	quad((screen_w - widest) * 0.5f - BOX_PAD, top - BOX_PAD * 0.5f, (screen_w + widest) * 0.5f + BOX_PAD,
	     top + lines * line_h + BOX_PAD * 0.5f, cu, cv, cu, cv, box);
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < lines; i++) {
			float w = text_width(full + starts[i], lens[i]);
			float x = (screen_w - w) * 0.5f + (pass ? 0.0f : 2.0f), y = top + i * line_h + (pass ? 0.0f : 2.0f);
			if (!pass) {
				draw_text(full + starts[i], lens[i], x, y, shadow);
				continue;
			}
			// The speaker's name (first line only) in gold, the rest white
			int name_chars = i == 0 ? (speaker_len < lens[i] ? speaker_len : lens[i]) : 0;
			x = draw_text(full + starts[i], name_chars, x, y, gold);
			draw_text(full + starts[i] + name_chars, lens[i] - name_chars, x, y, white);
		}

}

int ui_book_open(const char *title, const char *text) {
	free(book_text);
	book_text = strdup(text ? text : "");
	snprintf(book_title, sizeof(book_title), "%s", title ? title : "");
	book_open = book_text != NULL, book_page = 0, book_lines = 0, book_pages = 0;
	if (!book_open)
		return 0;
	// Words wrapped to the page's width; a line break kept; a page full or a [pagebreak] ('\f')
	// starts the next page (the first page's top: the title)
	const float width = BOOK_W - 2.0f * BOOK_MARGIN;
	const int per_page = (int)((BOOK_H - 2.0f * BOOK_MARGIN - line_h) / (line_h + 2.0f));
	book_first[0] = 0;
	int on_page = 1; // (the title's line)
	const char *t = book_text;
	int i = 0;
	while (t[i] && book_lines < BOOK_MAX_LINES && book_pages < BOOK_MAX_PAGES) {
		if (t[i] == '\f') { // (a new page, unless this one's still empty)
			if (on_page > 0)
				book_first[++book_pages] = book_lines, on_page = 0;
			i++;
			continue;
		}
		// The longest run of words from i that fits (to the end of the paragraph at most)
		int end = i, fit = i;
		while (t[end] && t[end] != '\n' && t[end] != '\f') {
			int w = end;
			while (t[w] && t[w] != ' ' && t[w] != '\n' && t[w] != '\f')
				w++;
			if (text_width(t + i, w - i) > width && fit > i)
				break;
			fit = w, end = w;
			while (t[end] == ' ')
				end++;
		}
		if (fit == i && t[i] && t[i] != '\n' && t[i] != '\f') // (one word wider than the page)
			while (t[fit] && t[fit] != ' ' && t[fit] != '\n' && t[fit] != '\f')
				fit++;
		if (on_page >= per_page)
			book_first[++book_pages] = book_lines, on_page = 0;
		book_line[book_lines].start = i, book_line[book_lines].len = fit - i;
		book_lines++, on_page++;
		i = fit;
		while (t[i] == ' ')
			i++;
		if (t[i] == '\n')
			i++;
	}
	book_first[++book_pages] = book_lines;
	return book_pages;
}

void ui_book_page(int page) {
	book_page = page < 0 ? 0 : page >= book_pages ? book_pages - 1 : page;
}

void ui_book_close(void) {
	book_open = 0;
	free(book_text);
	book_text = NULL;
}

// The open book: a page of parchment, the words in ink; the page number and the controls under it
static void build_book(void) {
	static const uint8_t dim[4] = {0, 0, 0, 150}, paper[4] = {214, 196, 158, 250}, edge[4] = {120, 92, 58, 255},
	                     ink[4] = {48, 34, 22, 255}, faded[4] = {110, 88, 62, 255}, hint[4] = {225, 222, 212, 255};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
	const float x0 = (screen_w - BOOK_W) * 0.5f, y0 = (screen_h - BOOK_H) * 0.5f - 12.0f;
	quad(0, 0, (float)screen_w, (float)screen_h, cu, cv, cu, cv, dim);
	quad(x0 - 4, y0 - 4, x0 + BOOK_W + 4, y0 + BOOK_H + 4, cu, cv, cu, cv, edge);
	quad(x0, y0, x0 + BOOK_W, y0 + BOOK_H, cu, cv, cu, cv, paper);
	float y = y0 + BOOK_MARGIN;
	if (book_page == 0) {
		const int tl = (int)strlen(book_title);
		draw_text(book_title, tl, (screen_w - text_width(book_title, tl)) * 0.5f, y, faded);
		y += line_h + 2.0f;
	}
	for (int l = book_first[book_page]; l < book_first[book_page + 1]; l++) {
		draw_text(book_text + book_line[l].start, book_line[l].len, x0 + BOOK_MARGIN, y, ink);
		y += line_h + 2.0f;
	}
	char foot[64];
	const int fl = snprintf(foot, sizeof(foot), "%d / %d", book_page + 1, book_pages);
	draw_text(foot, fl, (screen_w - text_width(foot, fl)) * 0.5f, y0 + BOOK_H - BOOK_MARGIN * 0.5f - line_h * 0.5f, faded);
	static const char *const controls = "L / R  Turn the page     O  Close";
	const int cl = (int)strlen(controls);
	draw_text(controls, cl, (screen_w - text_width(controls, cl)) * 0.5f, y0 + BOOK_H + 14.0f, hint);
}

void ui_local_map(const UiLocalMap *v) {
	local_open = v != NULL;
	if (v)
		local = *v;
}

// The local map: the floor plan from above, as Skyrim's - your level's floor in parchment, the
// levels above and below fainter; doors as dots; you as an arrow in the middle
static void build_local(void) {
	static const uint8_t dim[4] = {0, 0, 0, 200}, frame[4] = {222, 196, 140, 160}, door_c[4] = {236, 206, 140, 255},
	                     me[4] = {255, 255, 255, 255}, white[4] = {225, 222, 212, 255};
	float u0, v0, u1, v1;
	cell_uv(SOLID_CHAR, &u0, &v0, &u1, &v1);
	const float cu = (u0 + u1) * 0.5f, cv = (v0 + v1) * 0.5f;
	const float top = 44.0f, bottom = (float)screen_h - 40.0f, h = bottom - top;
	const float scale = h / local.metres, cx = screen_w * 0.5f, cy = top + h * 0.5f;
	quad(0, 0, (float)screen_w, (float)screen_h, cu, cv, cu, cv, dim);
	quad(20, top - 2, (float)screen_w - 20, top, cu, cv, cu, cv, frame);
	quad(20, bottom, (float)screen_w - 20, bottom + 2, cu, cv, cu, cv, frame);
	// (north up: x right, -z up)
#define MAPX(x) (cx + ((x) - local.centre[0]) * scale)
#define MAPY(z) (cy + ((z) - local.centre[1]) * scale)
	for (int t = 0; t < local.tri_count; t++) {
		const uint16_t *tv = local.tri_verts + t * local.tri_stride;
		float p[4][2], y = 0.0f, lo = 1e9f, hi = -1e9f;
		for (int k = 0; k < 3; k++) {
			const float *vx = local.verts + tv[k] * 3;
			p[k][0] = MAPX(vx[0]), p[k][1] = MAPY(vx[2]), y += vx[1] / 3.0f;
			lo = fminf(lo, p[k][1]), hi = fmaxf(hi, p[k][1]);
		}
		if (hi < top || lo > bottom)
			continue;
		p[3][0] = p[2][0], p[3][1] = p[2][1];
		const float dy = fabsf(y - local.player[1]);
		const float k = dy < 2.0f ? 1.0f : dy < 6.0f ? 0.45f : 0.22f; // (your level bright, the others faint)
		const uint8_t c[4] = {(uint8_t)(196 * k + 20), (uint8_t)(178 * k + 18), (uint8_t)(140 * k + 14), 255};
		quad4(p, cu, cv, c);
	}
	for (int d = 0; d < local.doors; d++) {
		const float x = MAPX(local.door[d][0]), y = MAPY(local.door[d][2]);
		if (y > top && y < bottom)
			quad(x - 4, y - 4, x + 4, y + 4, cu, cv, cu, cv, door_c);
	}
	{ // (you: an arrow the way you face)
		const float x = MAPX(local.player[0]), y = MAPY(local.player[2]), a = local.player[3];
		const float s = sinf(a), c = cosf(a);
		float p[4][2] = {{x + s * 11.0f, y - c * 11.0f}, {x - c * 6.0f - s * 6.0f, y - s * 6.0f + c * 6.0f},
		                 {x, y}, {x + c * 6.0f - s * 6.0f, y + s * 6.0f + c * 6.0f}};
		quad4(p, cu, cv, me);
	}
#undef MAPX
#undef MAPY
	if (local.title) {
		const int n = (int)strlen(local.title);
		draw_text(local.title, n, (screen_w - text_width(local.title, n)) * 0.5f, 10.0f, door_c);
	}
	if (local.hints) {
		const int n = (int)strlen(local.hints);
		draw_text(local.hints, n, (screen_w - text_width(local.hints, n)) * 0.5f, bottom + 8.0f, white);
	}
}
