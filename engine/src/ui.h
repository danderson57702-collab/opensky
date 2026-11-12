#ifndef UI_H
#define UI_H

#include <stdint.h>

// Screen overlay: subtitles for NPC speech (font: pack/ui/font.skfont, see
// converter/dialogue.py). Drawn after post-processing, at screen resolution.

void ui_init(const char *pack_dir, int screen_w, int screen_h);
// Shows "speaker: text" at the bottom of the screen (NULL/empty text hides it).
// Text is cp1252, as in Skyrim's string tables.
void ui_subtitle(const char *speaker, const char *text);
// A prompt above the subtitles (what CIRCLE would do here), NULL for none
void ui_prompt(const char *text);
// Black over the whole screen (0 clear .. 1 black), for door transitions
void ui_fade(float alpha);
// A colour over the scene (a shout's flash; 0 alpha: none), under the HUD
void ui_tint(int r, int g, int b, float alpha);
// Subtitles on/off (the player's setting)
void ui_set_subtitles(int on);
// A menu panel: title and items (an item "Label|Value" shows the value on the right);
// NULL title closes it. Drawn over a dimmed screen.
void ui_menu(const char *title, const char *const *items, int count, int selected);
void ui_menu_note(const char *text);
void ui_set_prompt_button(const char *glyph); // (the "(O)" before a prompt: the Activate button as mapped) // (after ui_menu: a line or three under its list - a shout's, a skill's description)
// The title screen behind the menu (on: black, the game's name over it, the menu's items
// below without a panel), or not
void ui_title(int on);
// The opening's title card over its black (the game's name from pack title.txt), 0 hidden .. 1
// The opening's cards, one at a time mid-screen: 1 the studio (the user's own files), 2 "Presents", 3 the line over the
// game's name, 4 the name; 0: none
void ui_opening_card(int kind, float alpha);
// While it starts: the game's name, its makers' credit from the user's own files (pack legal.txt) and OpenSKY's note,
// with the step it's on (NULL: off)
void ui_boot(const char *step);
// The title menu's Credits page: the game's makers and its legal notice (pack legal.txt) - text non-NULL: shown, NULL: off
void ui_credits(const char *text, float scroll);
// A line under the title screen's choices (the build; or that the data on the card is another build's)
void ui_title_note(const char *text, int warning);
// The character menu (four directions) over the game, or not
void ui_tween(int on);
// The health / magicka / stamina bars (fractions 0..1) and how visible they are (0: hidden)
// The HUD bars (0..1 each): breath is shown above them while it's below 1
void ui_vitals(float health, float magicka, float stamina, float breath, float alpha);
// A short message top left ("Game saved") for `seconds`
// Skyrim's inventory: categories down the left, the items in the middle, the selected
// item's card on the right (its 3D model is drawn above it: preview.c), hints and carry
// weight / gold along the bottom. NULL closes it.
typedef struct {
	const char *const *categories;
	int category_count, category;  // (the selected one)
	const char *const *names;      // the category's items
	const char *const *marks;      // per item: "Worn", "Equipped"... or ""
	int count, selected;
	int focus;                     // 0 the categories, 1 the items
	const char *card_title;        // the selected item (NULL: no card)
	const char *card_lines[4];     // "WEIGHT 0.5" ...
	const char *card_text;         // its effect (its words)
	const char *hints, *status;    // bottom left, bottom right
} UiInventory;
void ui_inventory(const UiInventory *view);
// The race menu (a new character): the races down the left, the chosen face's name above
// the character (drawn in the world, in the middle), the race's card on the right
// (description, skill bonuses, powers), hints along the bottom. NULL closes it.
typedef struct {
	const char *const *races;
	int count, selected;
	const char *face;         // "< Ulfric Stormcloak >"
	const char *title, *desc; // the race's name and description
	const char *lines[8];     // skill bonuses, powers ("" or NULL: none)
	const char *hints;
} UiRaceMenu;
void ui_race_menu(const UiRaceMenu *view);
// Skyrim's race menu (racemenu.c): its tabs along the top (the selected one bright), the race list
// and the race's card on the Race tab, else the tab's sliders down the left - each a name over a
// bar, the selected one bright (fills: where each sits, 0 .. 1; values: what it is, "" none) - the
// race's name and the hints along the bottom. NULL closes it.
typedef struct {
	const char *const *tabs;
	int tab_count, tab;
	const char *const *races; // (the Race tab)
	int race_count, race_selected;
	const char *title, *desc;
	const char *lines[8];
	const char *const *rows; // (the other tabs)
	const char *const *values;
	const float *fills;
	int row_count, row;
	const char *race_name, *name, *hints; // (along the bottom: the race, your name, the controls)
} UiChargen;
void ui_chargen(const UiChargen *view);
// The save / load list: the saves down the left (the selected one bright), its scene (a
// picture: ui_set_save_picture) and details on the right, hints along the bottom. NULL closes it.
typedef struct {
	const char *title;
	const char *const *rows; // "Whiterun  Level 4" ...
	int count, selected;
	const char *lines[4];    // the selected save's details
	const char *hints;
	int has_picture;         // (set by ui_set_save_picture)
} UiSaveList;
void ui_save_list(const UiSaveList *view);
// The selected save's picture (w x h RGB565), or NULL: none
void ui_set_save_picture(const uint16_t *rgb565, int w, int h);
// A skill's perk tree (Skyrim's constellation): the skill's name and level up top with its
// neighbours either side, the perks as stars joined by their links (taken ones bright),
// the selected perk's card along the bottom. NULL closes it.
#define UI_PERK_NODES 24
typedef struct {
	const char *skill, *prev, *next; // "One-handed 34", the skills either side
	float progress;                  // toward the skill's next level
	int count;                       // nodes
	float x[UI_PERK_NODES], y[UI_PERK_NODES]; // Skyrim's tree units (y up)
	int state[UI_PERK_NODES];        // -1 the root (not drawn), 0 out of reach, 1 can be taken, 2 some ranks taken,
	                                 // 3 all taken
	int link_count;
	int links[48][2];
	int selected;
	const char *title, *desc, *need, *points, *hints;
} UiPerkTree;
void ui_perk_tree(const UiPerkTree *view);
// Skyrim's world map: the province image (its cells' bounds), the region's over it when
// zoomed in, the markers (cell coordinates; kind: Skyrim's map marker kinds), the player's
// arrow, the selected marker's name and what CROSS does. NULL closes it.
#define UI_MAP_MARKERS 512
#define UI_MAP_QUESTS 8
typedef struct {
	unsigned province_tex, region_tex;
	float province[4], region[4]; // cells x0, y0, x1, y1 (y up)
	float centre[2], cells;       // the view: its centre (cells) and how many cells tall it is
	int count;
	float mx[UI_MAP_MARKERS], my[UI_MAP_MARKERS];
	uint8_t kind[UI_MAP_MARKERS], state[UI_MAP_MARKERS]; // state: 0 out of reach, 1 not found, 2 discovered
	int selected;
	float player[3]; // cells x, y, heading (radians, 0 north, clockwise)
	int quest_on;    // the active quests' objectives (Skyrim's gold markers): how many, cells x, y
	float quest[UI_MAP_QUESTS][2];
	const char *title, *line, *hints;
} UiMap;
void ui_map(const UiMap *view);
// The local map (a dungeon's, a house's - Skyrim's): its walkable floor from above (the navmesh;
// your floor bright, the others faint), its doors, your arrow, a title and the controls.
// NULL closes it
#define UI_LOCAL_DOORS 32
typedef struct {
	const float *verts;        // navmesh vertices (x, y, z)
	const uint16_t *tri_verts; // each triangle's vertex indices, tri_stride apart
	int tri_stride, tri_count;
	float centre[2], metres;   // the view: its centre (x, z) and how many metres tall it is
	float player[4];           // x, y (height), z, heading (radians, 0 north, clockwise)
	int doors;
	float door[UI_LOCAL_DOORS][3];
	const char *title, *hints;
} UiLocalMap;
void ui_local_map(const UiLocalMap *view);
// The crosshair (a dot in the middle), brighter when it's over something CIRCLE would use
void ui_crosshair(int on, int over_something);
// The compass (Skyrim's bar across the top, and a compass rose top right): the way you face
// (degrees: 0 north, east clockwise) and the places about you - their bearing (degrees, the
// same), map marker kind (worldmap.h; -1 a quest's target), discovered or not
typedef struct {
	float bearing;
	int kind, discovered;
} UiCompassMark;
#define UI_COMPASS_MARKS 48
void ui_compass(int on, float yaw_deg, const UiCompassMark *marks, int n);
// The foe you're fighting: its name and health (0..1) over a bar, top middle; NULL: none
void ui_enemy(const char *name, float health);
// A notice that replaces the last of its kind at once (kind: its opening words, INFO 0002EC2F)
void ui_notice_now(const char *text, const char *kind, float seconds);
void ui_notice(const char *text, float seconds);
void ui_notice_sound(const char *text, float seconds, const char *sound); // (its sound heard as it shows: held through a talk with it)
// The loading screen, over a full fade: the place's name, a turning mark and "Loading" (seconds:
// how long it's been up, for the turning); NULL or "" to take it away
void ui_loading(const char *place, float seconds);
// The performance readout's lines (perf.c), top left over everything; 0 lines: hidden
void ui_perf(const char lines[][64], int n);
void ui_draw(void);

// A book open over the game (Skyrim's reading): its title and words ('\n' lines, '\f'
// pages); returns its pages. ui_book_page shows one; ui_book_close puts it away
int ui_book_open(const char *title, const char *text);
void ui_book_page(int page);
void ui_book_close(void);

#endif
