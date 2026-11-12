#include "racemenu.h"

#include <psp2/ctrl.h>
#include <stdio.h>
#include <string.h>

#include "gamelog.h"
#include "name.h"
#include "races.h"
#include "ui.h"
#include "gametext.h"

// What a slider sets
enum {
	K_SEX,          // male, female
	K_PRESET,       // the race's presets: the whole look
	K_WEIGHT,       // 0 .. 100
	K_COMPLEXION,   // the face's texture set
	K_TINT,         // a tint layer's colour (arg: its type - the race's one layer of it)
	K_LAYER,        // which layer of a type (arg: war paint, dirt - none, or one of its patterns)
	K_LAYER_COLOR,  // ... that layer's colour
	K_PART,         // a head part (arg: chargen.h CG_HAIR...)
	K_MORPH,        // a face slider (arg: its NAM9 index)
	K_TYPE,         // a type (arg: 0 nose, 2 eyes, 3 mouth)
	K_HAIR_COLOR
};

typedef struct {
	const char *label;
	unsigned char kind;
	signed char arg;
} Row;

#define END {NULL, 0, 0}
static const Row BODY[] = {{"Sex", K_SEX, 0}, {T_GMST_000D4F2D_DATA0, K_PRESET, 0}, {T_GMST_000D491A_DATA0, K_TINT, CG_TINT_SKIN},
                           {"Weight", K_WEIGHT, 0}, END};
static const Row HEAD[] = {{"Complexion", K_COMPLEXION, 0}, {"Dirt", K_LAYER, CG_TINT_DIRT},
                           {T_GMST_0010934D_DATA0, K_LAYER_COLOR, CG_TINT_DIRT}, {T_GMST_000D44BB_DATA0, K_PART, CG_SCAR},
                           {T_GMST_000D4482_DATA0, K_LAYER, CG_TINT_PAINT}, {T_GMST_0010934B_DATA0, K_LAYER_COLOR, CG_TINT_PAINT}, END};
static const Row FACE[] = {{T_GMST_000D4F70_DATA0, K_TYPE, 0}, {T_GMST_000D4F75_DATA0, K_MORPH, 1}, {T_GMST_000D4F8B_DATA0, K_MORPH, 0},
                           {T_GMST_000D4E1B_DATA0, K_TINT, CG_TINT_NOSE}, {T_GMST_000D4931_DATA0, K_MORPH, 3}, {T_GMST_000D4915_DATA0, K_MORPH, 2},
                           {T_GMST_000D4CF8_DATA0, K_MORPH, 4}, {T_GMST_000D46A2_DATA0, K_MORPH, 5}, {T_GMST_000D4494_DATA0, K_MORPH, 6},
                           {T_GMST_000D4E17_DATA0, K_TINT, CG_TINT_CHEEKS}, {T_GMST_000D4A49_DATA0, K_TINT, CG_TINT_CHEEKS_LOWER},
                           {T_GMST_000D4F87_DATA0, K_TINT, CG_TINT_LAUGH}, {T_GMST_000D4D02_DATA0, K_TINT, CG_TINT_NECK}, END};
static const Row EYES[] = {{T_GMST_000D447A_DATA0, K_TYPE, 2}, {T_GMST_000D4EAF_DATA0, K_PART, CG_EYES}, {T_GMST_000D48C5_DATA0, K_MORPH, 7},
                           {T_GMST_000D4F7A_DATA0, K_MORPH, 8}, {T_GMST_000D49CF_DATA0, K_MORPH, 17}, {T_GMST_000D48BA_DATA0, K_TINT, CG_TINT_EYELINER},
                           {T_GMST_000D449F_DATA0, K_TINT, CG_TINT_SOCKET_UPPER}, {T_GMST_000D4B73_DATA0, K_TINT, CG_TINT_SOCKET_LOWER}, END};
static const Row BROW[] = {{T_GMST_000D4BC8_DATA0, K_PART, CG_BROWS}, {T_GMST_000D4BF6_DATA0, K_MORPH, 9}, {T_GMST_000D4B9D_DATA0, K_MORPH, 10},
                           {T_GMST_000D4498_DATA0, K_MORPH, 11}, {T_GMST_000D4C14_DATA0, K_TINT, CG_TINT_FOREHEAD}, END};
static const Row MOUTH[] = {{T_GMST_000D4D39_DATA0, K_TYPE, 3}, {T_GMST_000D4B33_DATA0, K_MORPH, 12}, {T_GMST_000D48FF_DATA0, K_MORPH, 13},
                            {T_GMST_000D4E26_DATA0, K_MORPH, 14}, {T_GMST_000D4D28_DATA0, K_MORPH, 15}, {T_GMST_000D4F84_DATA0, K_MORPH, 16},
                            {T_GMST_000D4F51_DATA0, K_TINT, CG_TINT_LIPS}, {T_GMST_000D4D2B_DATA0, K_TINT, CG_TINT_CHIN}, END};
static const Row HAIR[] = {{"Hair", K_PART, CG_HAIR}, {T_GMST_000D4C01_DATA0, K_PART, CG_BEARD}, {T_GMST_000D4EAD_DATA0, K_HAIR_COLOR, 0}, END};

enum { TAB_RACE, TAB_BODY, TAB_HEAD, TAB_FACE, TAB_EYES, TAB_BROW, TAB_MOUTH, TAB_HAIR, TABS };
static const char *const TAB_NAMES[TABS] = {"Race", "Body", "Head", "Face", "Eyes", "Brow", "Mouth", "Hair"};
static const Row *const TAB_ROWS[TABS] = {NULL, BODY, HEAD, FACE, EYES, BROW, MOUTH, HAIR};

#define MAX_ROWS 16
#define MAX_TYPES 40
#define REPEAT_AFTER_S 0.35f // (a held direction: steps again after this...)
#define REPEAT_EVERY_S 0.07f // (... then this often)

static struct {
	int open, race, female, tab, row[TABS];
	ChargenLook look;
	float hold_t, repeat_t;
	unsigned hold_dir;
	int body_wanted; // (race or sex changed: the body again)
} m;

static const char *race_eid(void) {
	const Race *r = races_get(m.race);
	return r ? r->eid : "";
}

// The races as Skyrim's menu lists them: by name (the numbers - saves keep them - as the pack has them)
static int race_order(int out[RACE_MAX]) {
	const int n = races_count() < RACE_MAX ? races_count() : RACE_MAX;
	for (int i = 0; i < n; i++)
		out[i] = i;
	for (int i = 1; i < n; i++) // (ten: in place)
		for (int j = i; j > 0 && strcmp(races_get(out[j - 1])->name, races_get(out[j])->name) > 0; j--) {
			const int s = out[j];
			out[j] = out[j - 1], out[j - 1] = s;
		}
	return n;
}

// A race's and sex's first preset (or, without the menu's data, the parts' defaults)
static void preset_look(int n) {
	ChargenLook l;
	if (chargen_preset(race_eid(), m.female, n, &l)) {
		m.look = l;
		return;
	}
	memset(&m.look, 0, sizeof(m.look));
	chargen_defaults(race_eid(), m.female, m.look.pick);
}

void racemenu_open(int race, int female, const ChargenLook *look) {
	memset(&m, 0, sizeof(m));
	m.open = 1, m.race = race, m.female = female, m.tab = TAB_RACE;
	if (look && look->version == 1)
		m.look = *look;
	else
		preset_look(0);
	game_log("race menu: opened on race %d (%s), %s, preset %d", race, race_eid(), female ? "female" : "male", m.look.preset);
}

void racemenu_close(void) {
	m.open = 0;
	ui_chargen(NULL);
}

int racemenu_race(void) {
	return m.race;
}

int racemenu_female(void) {
	return m.female;
}

const ChargenLook *racemenu_look(void) {
	return &m.look;
}

int racemenu_portrait(void) {
	return m.open && m.tab >= TAB_BODY;
}

int racemenu_close_on_face(void) {
	return m.open && m.tab >= TAB_HEAD;
}

// The race's layers of a type (tini values)
static int layers_of(int type, int *out, int max) {
	return chargen_layers(race_eid(), m.female, type, out, max);
}

// A single-layer type's layer: the look's, or the race's first of it
static int tint_layer(int type) {
	if (m.look.tint[type].layer)
		return m.look.tint[type].layer;
	int t[1];
	return layers_of(type, t, 1) > 0 ? t[0] : 0;
}

static int types_of(int group, uint8_t *out) {
	const int n = chargen_types(race_eid(), m.female, group, out + 1, MAX_TYPES - 1);
	out[0] = 0; // (none first: Skyrim's base shape)
	return n + 1;
}

// A row's choices and where it is among them (count 0: nothing to choose - hidden)
static void row_state(const Row *r, int *pos, int *count) {
	const char *eid = race_eid();
	*pos = 0, *count = 0;
	switch (r->kind) {
	case K_SEX:
		*count = chargen_has_menu(eid, !m.female) ? 2 : 1, *pos = m.female;
		break;
	case K_PRESET:
		*count = chargen_presets(eid, m.female), *pos = m.look.preset;
		break;
	case K_WEIGHT:
		*count = 21, *pos = (m.look.weight + 2) / 5;
		break;
	case K_COMPLEXION:
		*count = chargen_complexions(eid, m.female), *pos = m.look.complexion;
		break;
	case K_TINT: {
		const int tini = tint_layer(r->arg);
		*count = tini ? chargen_layer_colors(eid, m.female, tini) : 0;
		*pos = m.look.tint[r->arg].layer == tini && m.look.tint[r->arg].value ? m.look.tint[r->arg].color : 0;
		break;
	}
	case K_LAYER: {
		int t[MAX_TYPES];
		const int n = layers_of(r->arg, t, MAX_TYPES);
		*count = n ? n + 1 : 0;
		for (int i = 0; i < n && i < MAX_TYPES; i++)
			if (m.look.tint[r->arg].layer == t[i] && m.look.tint[r->arg].value)
				*pos = i + 1;
		break;
	}
	case K_LAYER_COLOR: {
		const int tini = m.look.tint[r->arg].value ? m.look.tint[r->arg].layer : 0;
		*count = tini ? chargen_layer_colors(eid, m.female, tini) : 0, *pos = m.look.tint[r->arg].color;
		break;
	}
	case K_PART: {
		const int n = chargen_count(eid, m.female, r->arg);
		const int none = r->arg == CG_HAIR || r->arg == CG_BEARD || r->arg == CG_SCAR;
		*count = n ? n + none : 0, *pos = m.look.pick[r->arg] + none;
		break;
	}
	case K_MORPH:
		*count = 21, *pos = (m.look.morph[r->arg] + 105) / 10;
		break;
	case K_TYPE: {
		uint8_t t[MAX_TYPES];
		const uint8_t now = r->arg == 0 ? m.look.nose : r->arg == 2 ? m.look.eyes : m.look.mouth;
		*count = types_of(r->arg, t);
		if (*count <= 1)
			*count = 0;
		for (int i = 0; i < *count; i++)
			if (t[i] == now)
				*pos = i;
		break;
	}
	case K_HAIR_COLOR:
		*count = chargen_hair_colors(eid, m.female), *pos = m.look.hair_color;
		break;
	}
	if (*pos >= *count)
		*pos = *count ? *count - 1 : 0;
}

enum { CHANGED_NONE, CHANGED_BODY, CHANGED_LOOK, CHANGED_PARTS, CHANGED_MORPHS, CHANGED_COLORS };

// A row moved to choice `pos`: the look changed (what of it)
static int row_set(const Row *r, int pos) {
	const char *eid = race_eid();
	switch (r->kind) {
	case K_SEX:
		m.female = pos;
		preset_look(0);
		return CHANGED_BODY;
	case K_PRESET:
		preset_look(pos);
		return CHANGED_LOOK;
	case K_WEIGHT:
		m.look.weight = (uint8_t)(pos * 5);
		return CHANGED_MORPHS;
	case K_COMPLEXION:
		m.look.complexion = (uint8_t)pos;
		return CHANGED_COLORS;
	case K_TINT: {
		const int tini = tint_layer(r->arg);
		m.look.tint[r->arg].layer = (uint8_t)tini, m.look.tint[r->arg].color = (uint8_t)pos;
		m.look.tint[r->arg].value = (uint8_t)chargen_layer_color_value(eid, m.female, tini, pos);
		return CHANGED_COLORS;
	}
	case K_LAYER: {
		int t[MAX_TYPES];
		const int n = layers_of(r->arg, t, MAX_TYPES);
		if (pos <= 0 || pos > n) {
			m.look.tint[r->arg].value = 0;
			return CHANGED_COLORS;
		}
		const int tini = t[pos - 1], colors = chargen_layer_colors(eid, m.female, tini);
		int c = m.look.tint[r->arg].color < colors ? m.look.tint[r->arg].color : 0;
		for (int k = 0; k < colors && !chargen_layer_color_value(eid, m.female, tini, c); k++)
			c = (c + 1) % colors; // (a colour that shows: the presets at no strength are its "none")
		m.look.tint[r->arg].layer = (uint8_t)tini, m.look.tint[r->arg].color = (uint8_t)c;
		m.look.tint[r->arg].value = (uint8_t)chargen_layer_color_value(eid, m.female, tini, c);
		if (!m.look.tint[r->arg].value)
			m.look.tint[r->arg].value = 100;
		return CHANGED_COLORS;
	}
	case K_LAYER_COLOR: {
		const int tini = m.look.tint[r->arg].layer;
		m.look.tint[r->arg].color = (uint8_t)pos;
		const int v = chargen_layer_color_value(eid, m.female, tini, pos);
		m.look.tint[r->arg].value = (uint8_t)(v ? v : 100);
		return CHANGED_COLORS;
	}
	case K_PART: {
		const int none = r->arg == CG_HAIR || r->arg == CG_BEARD || r->arg == CG_SCAR;
		m.look.pick[r->arg] = (int8_t)(pos - none);
		return CHANGED_PARTS;
	}
	case K_MORPH:
		m.look.morph[r->arg] = (int8_t)(pos * 10 - 100);
		return CHANGED_MORPHS;
	case K_TYPE: {
		uint8_t t[MAX_TYPES];
		const int n = types_of(r->arg, t);
		const uint8_t v = pos < n ? t[pos] : 0;
		if (r->arg == 0)
			m.look.nose = v;
		else if (r->arg == 2)
			m.look.eyes = v;
		else
			m.look.mouth = v;
		return CHANGED_MORPHS;
	}
	case K_HAIR_COLOR:
		m.look.hair_color = (uint8_t)pos;
		return CHANGED_COLORS;
	}
	return CHANGED_NONE;
}

// The tab's rows that have something to choose
static int rows_of(int tab, const Row **out) {
	int n = 0;
	for (const Row *r = TAB_ROWS[tab]; r && r->label && n < MAX_ROWS; r++) {
		int pos, count;
		row_state(r, &pos, &count);
		if (count > 1 || r->kind == K_SEX)
			out[n++] = r;
	}
	return n;
}

static void put_on(int changed, RacemenuSetBody set_body) {
	const char *eid = race_eid();
	switch (changed) {
	case CHANGED_BODY:
		set_body(m.race, m.female, &m.look);
		break;
	case CHANGED_LOOK:
		chargen_apply_look(eid, m.female, &m.look);
		break;
	case CHANGED_PARTS:
		chargen_apply(eid, m.female, m.look.pick); // (the new parts take the morphs and colours as they come)
		break;
	case CHANGED_MORPHS:
		chargen_apply_morphs(&m.look);
		break;
	case CHANGED_COLORS:
		chargen_apply_colors(eid, m.female, &m.look);
		break;
	}
}

static void show(void) {
	static char rows_text[MAX_ROWS][48], values_text[MAX_ROWS][24];
	static const char *row_names[MAX_ROWS], *values[MAX_ROWS], *race_names[RACE_MAX];
	static float fills[MAX_ROWS];
	UiChargen v;
	memset(&v, 0, sizeof(v));
	v.tabs = TAB_NAMES, v.tab_count = TABS, v.tab = m.tab;
	const Race *r = races_get(m.race);
	if (m.tab == TAB_RACE) {
		int order[RACE_MAX];
		const int nr = race_order(order);
		for (int i = 0; i < nr; i++) {
			race_names[i] = races_get(order[i])->name;
			if (order[i] == m.race)
				v.race_selected = i;
		}
		v.races = race_names, v.race_count = nr;
		if (r) // (its description over you, as Skyrim's - no more: it rose over your face)
			v.title = r->name, v.desc = r->desc;
	} else {
		const Row *rows[MAX_ROWS];
		const int n = rows_of(m.tab, rows);
		for (int i = 0; i < n; i++) {
			int pos, count;
			row_state(rows[i], &pos, &count);
			snprintf(rows_text[i], sizeof(rows_text[i]), "%s", rows[i]->label);
			fills[i] = count > 1 ? (float)pos / (float)(count - 1) : 0.0f;
			const int none = (rows[i]->kind == K_PART && (rows[i]->arg == CG_HAIR || rows[i]->arg == CG_BEARD || rows[i]->arg == CG_SCAR) &&
			                  pos == 0) || (rows[i]->kind == K_LAYER && pos == 0);
			if (rows[i]->kind == K_SEX)
				snprintf(values_text[i], sizeof(values_text[i]), "%s", m.female ? "Female" : "Male");
			else if (none)
				snprintf(values_text[i], sizeof(values_text[i]), "None");
			else if (rows[i]->kind == K_MORPH || rows[i]->kind == K_WEIGHT)
				values_text[i][0] = 0; // (a slider: the bar says it)
			else
				snprintf(values_text[i], sizeof(values_text[i]), "%d / %d", pos + 1, count);
			row_names[i] = rows_text[i], values[i] = values_text[i];
		}
		v.rows = row_names, v.values = values, v.fills = fills, v.row_count = n;
		v.row = m.row[m.tab] < n ? m.row[m.tab] : n - 1;
	}
	v.race_name = r ? r->name : "", v.name = name_get();
	v.hints = m.tab == TAB_RACE ? "START Done    L/R Tab    X Next" : "START Done    L/R Tab";
	ui_chargen(&v);
}

int racemenu_update(unsigned pressed, unsigned held, float dt, RacemenuSetBody set_body) {
	if (!m.open)
		return RACEMENU_NONE;
	{ // (its frame rate, logged every few seconds)
		static float t;
		static int frames;
		t += dt, frames++;
		if (t >= 4.0f) {
			game_log("race menu: %.1f fps", frames / t);
			t = 0.0f, frames = 0;
		}
	}
	// A held direction steps again (the sliders' long runs)
	const unsigned dirs = SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN;
	unsigned step = pressed & dirs;
	if (held & m.hold_dir & dirs) {
		m.hold_t += dt;
		if (m.hold_t > REPEAT_AFTER_S && (m.repeat_t += dt) >= REPEAT_EVERY_S)
			m.repeat_t = 0.0f, step |= m.hold_dir & held;
	} else {
		m.hold_dir = pressed & dirs, m.hold_t = 0.0f, m.repeat_t = 0.0f;
	}
	if (pressed & dirs)
		m.hold_dir = pressed & dirs, m.hold_t = 0.0f;
	if (pressed & SCE_CTRL_LTRIGGER)
		m.tab = (m.tab + TABS - 1) % TABS;
	if (pressed & SCE_CTRL_RTRIGGER)
		m.tab = (m.tab + 1) % TABS;
	if (pressed & SCE_CTRL_CROSS && m.tab == TAB_RACE)
		m.tab = TAB_BODY;
	if (pressed & SCE_CTRL_CIRCLE && m.tab != TAB_RACE)
		m.tab--;
	if (pressed & (SCE_CTRL_START | SCE_CTRL_TRIANGLE)) {
		game_log("race menu: done - race %d, %s, preset %d, weight %d", m.race, m.female ? "female" : "male", m.look.preset,
		         m.look.weight);
		return RACEMENU_DONE;
	}
	if (m.tab == TAB_RACE) {
		int order[RACE_MAX];
		const int nr = race_order(order);
		const int was = m.race;
		int at = 0;
		for (int i = 0; i < nr; i++)
			if (order[i] == m.race)
				at = i;
		if (nr && (step & SCE_CTRL_UP))
			m.race = order[(at + nr - 1) % nr];
		if (nr && (step & SCE_CTRL_DOWN))
			m.race = order[(at + 1) % nr];
		if (m.race != was) {
			if (!chargen_has_menu(race_eid(), m.female) && chargen_has_menu(race_eid(), !m.female))
				m.female = !m.female;
			preset_look(0);
			put_on(CHANGED_BODY, set_body);
		}
	} else {
		const Row *rows[MAX_ROWS];
		const int n = rows_of(m.tab, rows);
		int *row = &m.row[m.tab];
		if (n) {
			if (step & SCE_CTRL_UP)
				*row = (*row + n - 1) % n;
			if (step & SCE_CTRL_DOWN)
				*row = (*row + 1) % n;
			if (*row >= n)
				*row = n - 1;
			const int dir = (step & SCE_CTRL_RIGHT) ? 1 : (step & SCE_CTRL_LEFT) ? -1 : 0;
			if (dir) {
				int pos, count;
				row_state(rows[*row], &pos, &count);
				int to = pos + dir;
				if (rows[*row]->kind == K_SEX || rows[*row]->kind == K_PRESET || rows[*row]->kind == K_PART ||
				    rows[*row]->kind == K_TYPE || rows[*row]->kind == K_HAIR_COLOR || rows[*row]->kind == K_COMPLEXION ||
				    rows[*row]->kind == K_LAYER || rows[*row]->kind == K_TINT || rows[*row]->kind == K_LAYER_COLOR)
					to = (to + count) % count; // (a list: round again)
				else if (to < 0 || to >= count)
					to = pos; // (a slider: stops at its ends)
				if (to != pos && count > 0)
					put_on(row_set(rows[*row], to), set_body);
			}
		}
	}
	show();
	return RACEMENU_NONE;
}
