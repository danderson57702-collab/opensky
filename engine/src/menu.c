#include "menu.h"
#include "beast.h"
#include "people.h"
#include "worlds.h"
#include "gamelog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

#include "actors.h"
#include "alchemy.h"
#include "effects.h"
#include "items.h"
#include "equip.h"
#include "preview.h"
#include "loot.h"
#include "magic.h"
#include "perks.h"
#include "quests.h"
#include "worldmap.h"
#include "navmesh.h"
#include "interior.h"
#include "races.h"
#include "shouts.h"
#include "save.h"
#include "sfx.h"
#include "skills.h"
#include "fileio.h"
#include "music.h"
#include "post.h"
#include "ui.h"
#include "voice.h"
#include "gametext.h"

#define SETTINGS_PATH "ux0:data/opensky_settings.txt"
#define REPEAT_DELAY 18 // frames before a held direction repeats...
#define REPEAT_EVERY 5  // ...and then every this many

// START: the pause menu. SELECT: wait. (The final button layout waits for the real Vita.)
enum { PAGE_MAIN, PAGE_OPTIONS, PAGE_INVENTORY, PAGE_WAIT, PAGE_MAGIC, PAGE_SKILLS, PAGE_CONTAINER, PAGE_POCKETS,
       PAGE_SAVE, PAGE_LOAD, PAGE_PERKS, PAGE_MAP, PAGE_ALCHEMY, PAGE_DIALOGUE, PAGE_BARTER, PAGE_QUESTS, PAGE_CRAFT, PAGE_ENCHANT, PAGE_FAVORITES, PAGE_KEYS };
#define PICKPOCKET_MAX 100.0f // (the perks can take it past Skyrim's 90: Keymaster's keys always)
#define NPC_PICKPOCKET_SKILL 15 // (NPCs' own skills aren't modelled: Skyrim's base for most)
enum { MAIN_RESUME, MAIN_INVENTORY, MAIN_MAGIC, MAIN_SKILLS, MAIN_MAP, MAIN_QUESTS, MAIN_WAIT, MAIN_SAVE, MAIN_LOAD, MAIN_OPTIONS, MAIN_CAMERA,
       MAIN_QUIT, MAIN_COUNT };
#define CARRIED_MAX 256
#define CONTAINER_LIST 128 // (a container's different items, as items.c holds)
#define MENU_ROWS 10 // lines shown at once; longer lists scroll with the selection
enum { OPT_MUSIC, OPT_VOICE, OPT_SUBTITLES, OPT_STAIRS, OPT_LOOK, OPT_INVERT, OPT_POST, OPT_FXAA, OPT_FSR, OPT_MOTION, OPT_DIFFICULTY, OPT_PERFORMANCE,
       OPT_FRAME_RATE, OPT_RESOLUTION, OPT_DYNRES, OPT_CONTROLS, OPT_REAR_TOUCH, OPT_KEYS, OPT_COMPASS, OPT_FOV_FIRST, OPT_FOV_THIRD, OPT_CROSSHAIR, OPT_MARKERS, OPT_BAR_FADE, OPT_READOUT, OPT_MEASURE, OPT_BACK,
       OPT_COUNT }; // (Debug controls and the crafting kit: not here - turned on by a test run only, autotest.c)

static Settings settings = {8, 10, 1, 2, 5, 0, 0, 0, 0, 2, PERF_LOW, 0, FPS_SMOOTH, RES_544_352, CONTROLS_SHOULDERS, 0, 1, 65, 65, 1, 1, 1, 0,
                            POST_UPSCALE_BILINEAR, 0, 0}; // (compass on; Low, smoothed: to start on the Vita; 544 x 352 at most, dynamic)

// Favourites (Skyrim's): spells, shouts and powers, weapons - marked with SQUARE in the Magic and Inventory menus; D-pad
// left / right step through those that hand can hold, held open a short list of them all. By name, kept with the game
#define FAV_MAX 32
static uint32_t fav_hash[FAV_MAX];
static uint8_t fav_kind[FAV_MAX];

static uint32_t fav_name_hash(const char *s) {
	uint32_t h = 2166136261u;
	for (; s && *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h ? h : 1;
}

int menu_is_favorite(int kind, const char *name) {
	const uint32_t h = fav_name_hash(name);
	for (int i = 0; i < FAV_MAX; i++)
		if (fav_kind[i] == kind && fav_hash[i] == h)
			return 1;
	return 0;
}

void menu_toggle_favorite(int kind, const char *name) {
	const uint32_t h = fav_name_hash(name);
	for (int i = 0; i < FAV_MAX; i++)
		if (fav_kind[i] == kind && fav_hash[i] == h) { // (off: the rest move up, keeping their order)
			memmove(&fav_hash[i], &fav_hash[i + 1], sizeof(fav_hash[0]) * (FAV_MAX - 1 - i));
			memmove(&fav_kind[i], &fav_kind[i + 1], sizeof(fav_kind[0]) * (FAV_MAX - 1 - i));
			fav_hash[FAV_MAX - 1] = 0, fav_kind[FAV_MAX - 1] = 0;
			ui_notice_now("Removed from favorites", "Removed from favorites", 1.5f);
			return;
		}
	for (int i = 0; i < FAV_MAX; i++)
		if (!fav_kind[i]) {
			fav_hash[i] = h, fav_kind[i] = (uint8_t)kind;
			ui_notice_now("Added to favorites", "Added to favorites", 1.5f);
			return;
		}
	ui_notice_now("Favorites are full", "Favorites are full", 1.5f);
}

// The favourites list (D-pad left / right held): each favourite you have now - a spell known, a shout with a word
// unlocked, the race's power - in the order marked
#define FAV_ROW_POWER -2
static int fav_row_kind[FAV_MAX + 1], fav_row_id[FAV_MAX + 1], fav_rows;

static void favorites_rows(void) {
	fav_rows = 0;
	for (int i = 0; i < FAV_MAX && fav_kind[i]; i++) {
		if (fav_kind[i] == FAV_SPELL) {
			for (int sp = 0; sp < magic_count(); sp++)
				if (magic_known(sp) && fav_name_hash(magic_name(sp)) == fav_hash[i]) {
					fav_row_kind[fav_rows] = FAV_SPELL, fav_row_id[fav_rows++] = sp;
					break;
				}
		} else if (fav_kind[i] == FAV_SHOUT) {
			if (fav_name_hash(races_power_name()) == fav_hash[i]) {
				fav_row_kind[fav_rows] = FAV_SHOUT, fav_row_id[fav_rows++] = FAV_ROW_POWER;
				continue;
			}
			for (int j = 0; j < shouts_count(); j++)
				if (shouts_known(j) > 0 && fav_name_hash(shouts_name(j)) == fav_hash[i]) {
					fav_row_kind[fav_rows] = FAV_SHOUT, fav_row_id[fav_rows++] = j;
					break;
				}
		}
	}
}

// D-pad left / right stepping a hand's spells: the next favourite spell after `from` (-1: the first), -1 none; any=0
// when no spell is a favourite (then every spell learned, as before)
int menu_next_favorite_spell(int from, int *any) {
	favorites_rows();
	int first = -1, after = -1;
	*any = 0;
	for (int r = 0; r < fav_rows; r++)
		if (fav_row_kind[r] == FAV_SPELL) {
			*any = 1;
			if (first < 0)
				first = fav_row_id[r];
			if (after < 0 && fav_row_id[r] > from)
				after = fav_row_id[r];
		}
	return from < 0 ? first : after;
}

static int open_page(int p);
void menu_open_favorites(void) {
	favorites_rows();
	game_log("menu: the favourites list (%d)", fav_rows);
	open_page(PAGE_FAVORITES);
}

void menu_save_favorites(uint32_t *hash, uint8_t *kind) {
	memcpy(hash, fav_hash, sizeof(fav_hash)), memcpy(kind, fav_kind, sizeof(fav_kind));
}

void menu_load_favorites(const uint32_t *hash, const uint8_t *kind) { // (NULL: none - a new game, an older save)
	if (hash && kind)
		memcpy(fav_hash, hash, sizeof(fav_hash)), memcpy(fav_kind, kind, sizeof(fav_kind));
	else
		memset(fav_hash, 0, sizeof(fav_hash)), memset(fav_kind, 0, sizeof(fav_kind));
}

// The buttons an action can be on, and what the game reads each action as
#define KEY_COUNT 11
static const unsigned KEY_BUTTON[KEY_COUNT] = {SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE, SCE_CTRL_LTRIGGER,
                                               SCE_CTRL_RTRIGGER, SCE_CTRL_SELECT, SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT};
static const char *const KEY_NAME[KEY_COUNT] = {"X", "O", "[]", "/\\", "L", "R", "SELECT", "D-pad up", "D-pad down", "D-pad left", "D-pad right"};
static const char *const KEY_GLYPH[KEY_COUNT] = {"X", "O", "[]", "/\\", "L", "R", "SELECT", "Up", "Down", "Left", "Right"};
enum { KEY_CROSS, KEY_CIRCLE, KEY_SQUARE, KEY_TRIANGLE, KEY_L, KEY_R, KEY_SELECT, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
static const unsigned ACTION_READ_AS[ACT_COUNT] = {SCE_CTRL_TRIANGLE, SCE_CTRL_SQUARE, SCE_CTRL_RTRIGGER, SCE_CTRL_LTRIGGER, SCE_CTRL_CIRCLE,
                                                   SCE_CTRL_CROSS, SCE_CTRL_SELECT, SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT};
static const char *const ACTION_NAME[ACT_COUNT] = {"Right hand (attack, cast)", "Left hand (block, cast)", "Sprint", "Sneak",
                                                   "Activate, search, talk", "Jump", "Shout / power", "Ready / put away",
                                                   "First / third person", "Left hand: next spell", "Right hand: next spell"};
static const char *const ACTION_KEY[ACT_COUNT] = {"right_hand", "left_hand", "sprint", "sneak", "activate", "jump", "shout",
                                                  "ready", "camera", "cycle_left", "cycle_right"};
// The two layouts (Options: Controls): the face buttons' - the game's own - and the shoulders' (the hands on R / L,
// sprint and sneak on TRIANGLE / SQUARE)
static const int PRESET_FACE[ACT_COUNT] = {KEY_TRIANGLE, KEY_SQUARE, KEY_R, KEY_L, KEY_CIRCLE, KEY_CROSS, KEY_SELECT, KEY_UP, KEY_DOWN,
                                           KEY_LEFT, KEY_RIGHT};
static const int PRESET_SHOULDERS[ACT_COUNT] = {KEY_R, KEY_L, KEY_TRIANGLE, KEY_SQUARE, KEY_CIRCLE, KEY_CROSS, KEY_SELECT, KEY_UP, KEY_DOWN,
                                                KEY_LEFT, KEY_RIGHT};

static void binds_preset(int controls) {
	for (int a = 0; a < ACT_COUNT; a++)
		settings.bind[a] = controls == CONTROLS_SHOULDERS ? PRESET_SHOULDERS[a] : PRESET_FACE[a];
}

unsigned menu_bind_buttons(unsigned b) {
	unsigned out = b & ~(unsigned)(SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE | SCE_CTRL_LTRIGGER |
	                               SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT | SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
	for (int a = 0; a < ACT_COUNT; a++)
		if (settings.bind[a] >= 0 && settings.bind[a] < KEY_COUNT && (b & KEY_BUTTON[settings.bind[a]]))
			out |= ACTION_READ_AS[a];
	return out;
}

const char *menu_action_glyph(int a) {
	return a >= 0 && a < ACT_COUNT && settings.bind[a] >= 0 && settings.bind[a] < KEY_COUNT ? KEY_GLYPH[settings.bind[a]] : "?";
}

// An action onto another button - the action that had it takes this one's old button (one button, one action)
static void bind_change(int a, int dir) {
	const int old = settings.bind[a], want = (old + dir + KEY_COUNT) % KEY_COUNT;
	for (int o = 0; o < ACT_COUNT; o++)
		if (o != a && settings.bind[o] == want)
			settings.bind[o] = old;
	settings.bind[a] = want;
}

static int selected; // (forward: the row chosen - defined with the menu's state below)
static void list_nav(unsigned pressed, int count) { // (up / down through a page's rows, round at the ends)
	if (count <= 0)
		return;
	if (pressed & SCE_CTRL_UP)
		selected = (selected + count - 1) % count;
	if (pressed & SCE_CTRL_DOWN)
		selected = (selected + 1) % count;
}

#define JOURNAL_ROWS 64
static char journal_quest[JOURNAL_ROWS][48]; // (the Quests page: the not-done quests' names, by their rows)
static int journal_row[JOURNAL_ROWS], journal_rows, journal_back;
static const char *const PERFORMANCE[PERF_LEVELS] = {"Ultra Low", "Low", "Medium", "High"};
static const char *const FRAME_RATES[3] = {"60", "30", "Smoothed 22-60"};
static const int RESOLUTIONS[RES_COUNT][2] = {{424, 240}, {480, 272}, {640, 368}, {720, 448}, {544, 304}, {544, 352}, {640, 352}};
static const char *const RESOLUTION_NAMES[RES_COUNT] = {"424 x 240", "480 x 272 (PSP)", "640 x 368", "720 x 448", "544 x 304",
                                                        "544 x 352", "640 x 352"};
static int measure_asked;  // (Options' Measure: taken by main.c - menu_take_measure)
static int msaa_running;   // (whether 2x MSAA was on as the graphics started: main.c - menu_set_msaa_running)
static const char *const UPSCALER_NAMES[2] = {"Off", "Bilinear"}; // (FSR 1.0: gone - 70 ms a frame on the Vita)
static const char *const DIFFICULTY[6] = {"Novice", "Apprentice", "Adept", "Expert", "Master", "Legendary"};
// Skyrim's difficulty multipliers: damage taken (fDiffMultHPToPC VE..L), dealt (fDiffMultHPByPC)
static const float TAKEN[6] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f}, DEALT[6] = {2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 0.25f};

float menu_damage_taken_mult(void) {
	return TAKEN[settings.difficulty];
}

float menu_damage_dealt_mult(void) {
	return DEALT[settings.difficulty];
}
static int wait_hours = 1, wait_pending;
static int request; // MENU_SAVE / MENU_LOAD, for main to carry out
static int request_slot; // ...in this save slot (save.h; SAVE_NEW: a new save)
static SaveInfo save_infos[SAVE_SLOTS]; // the save / load pages' saves, newest first
static int save_count, picture_for = -2; // (the slot whose picture is up)
static int level_choice; // a level-up's bonus chosen: 1 health, 2 magicka, 3 stamina
static int use_pending = -1; // an item to eat or drink, for main (it has the vitals)
static int sleeping, slept;   // the wait page is the bed's Sleep; the hours taken were sleep
static int inv_dirty;         // the inventory changed under the open menu (an item used)
static int container_handle = -1; // the container being looked through (items.h)
static char container_title[64];
static int carried_list[CARRIED_MAX], carried_n; // the inventory page's items (after the wardrobe)
static int inv_cat = CAT_ALL;   // the inventory's category (L / R)
static int inv_focus, inv_sel;  // the inventory: 0 the categories / 1 the items; the selected item
static float carry_max = 300.0f, carried_now; // (main passes the player's)
static int cont_items[CONTAINER_LIST], cont_counts[CONTAINER_LIST], cont_n;
static int cont_store;              // the container page shows your things, to put in (L / R)
// Talking to someone: what you can say (main.c's: quest topics, Barter, Goodbye) and what was chosen
#define DIALOGUE_MAX 12
static char dlg_title[64], dlg_opts[DIALOGUE_MAX][200];
static int dlg_n, dlg_choice = -1;
// Bartering: the merchant (items.h's vendor), their stock, buying (0) or selling (1)
static int barter_vendor = -1, barter_sell;
static char barter_name[64];
// Crafting at a station (furniture.h's: 1 forge .. 4 cooking pot) and enchanting (7)
static int craft_station, craft_rows_n;
static int craft_list[1200];
enum { ENCH_TOP, ENCH_DISENCHANT, ENCH_ITEM, ENCH_EFFECT, ENCH_GEM };
static int ench_step, ench_item, ench_effect;
static int ench_second, ench_effect2 = -1; // (Extra Effect: a second chosen - the same again: none)
static int ench_list[CARRIED_MAX];
static const char *const STATION_NAMES[8] = {"", "Forge", "Smelter", T_ACTI_000E2414_FULL0, T_FURN_001010B3_FULL0, "Grindstone",
                                              "Workbench", T_FURN_000BAD0D_FULL0};
static int store_list[CARRIED_MAX], store_n;
static int drop_item = -1, drop_count; // dropped from the inventory (main lays it on the ground)
static int pocket_npc = -1, caught = -1; // pickpocketing: whose pockets; caught by (for main)
static int open, page, selected;
static int perk_skill, perk_node; // the perk tree shown, its selected node
static float map_centre[2], map_cells = 40.0f; // the map's view: its middle (cells), how many cells tall
static int map_sel = -1;
static float player_at[3];  // where the player is (engine x, z, yaw degrees), for the map
static int quest_on;        // the active quests' objectives on the map (how many; engine x, z)
static float quest_at[UI_MAP_QUESTS][2];
// Inside: where you are in it (x, y, z, yaw degrees), its name - the local map (Skyrim's)
static float local_at[4], local_centre[2], local_metres = 60.0f;
static int local_known, map_local; // (map_local: the map shows the local one)
static char local_name[64];
static int player_outdoors;
#define CELL_M (4096.0f / 70.0f)
static unsigned prev_buttons;
static int held_frames;

static const char *const BOUNCE[4] = {"Off", "Low", "Normal", "High"};

static void apply(void) {
	music_set_volume(settings.music_volume / 10.0f);
	voice_set_volume(settings.voice_volume / 10.0f);
	sfx_set_volume(settings.voice_volume / 10.0f); // (sound effects follow the voice volume)
	ui_set_subtitles(settings.subtitles);
	post_set_enabled(settings.post);
	post_set_motion_blur(settings.motion_blur);
	post_set_upscaler(settings.upscaler);
	ui_set_prompt_button(menu_action_glyph(ACT_ACTIVATE)); // ("(O) Search", as Activate is mapped)
	post_set_resolution(RESOLUTIONS[settings.resolution][0], RESOLUTIONS[settings.resolution][1]);
}

static int clampi(int v, int lo, int hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static void save(void) {
	char text[1400];
	int n = snprintf(text, sizeof(text),
	                 "music_volume=%d\nvoice_volume=%d\nsubtitles=%d\nstair_bounce=%d\nlook_speed=%d\ninvert_look=%d\n"
	                 "bloom=%d\ndebug_controls=%d\nthird_person=%d\ndifficulty=%d\nperformance_level=%d\nreadout=%d\n"
	                 "frame_rate=%d\nresolution_width=%d\ncontrols=%d\nrear_touch=%d\ncompass=%d\n"
	                 "fov_first=%d\nfov_third=%d\ncrosshair=%d\nquest_markers=%d\nbar_fade=%d\nmsaa=%d\nupscaler=%d\nmotion_blur=%d\n"
	                 "resolution_height=%d\ndynamic_resolution=%d\n",
	                 settings.music_volume, settings.voice_volume, settings.subtitles, settings.stair_bounce,
	                 settings.look_speed, settings.invert_look, settings.post, settings.debug, settings.third_person,
	                 settings.difficulty, settings.performance, settings.readout, settings.frame_rate,
	                 RESOLUTIONS[settings.resolution][0], settings.controls, settings.rear_touch, settings.compass,
	                 settings.fov_first, settings.fov_third, settings.hud_crosshair, settings.hud_markers, settings.hud_bar_fade, settings.msaa, settings.upscaler, settings.motion_blur,
	                 RESOLUTIONS[settings.resolution][1], settings.dynres);
	for (int a = 0; a < ACT_COUNT && n < (int)sizeof(text) - 40; a++)
		n += snprintf(text + n, sizeof(text) - n, "bind_%s=%d\n", ACTION_KEY[a], settings.bind[a]);
	if (n > (int)sizeof(text) - 1)
		n = (int)sizeof(text) - 1;
	SceUID f = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (f < 0) {
		sceClibPrintf("menu: can't save %s (0x%08X)\n", SETTINGS_PATH, f);
		return;
	}
	sceIoWrite(f, text, n);
	sceIoClose(f);
}

static void load(void) {
	long size = 0;
	binds_preset(settings.controls); // (the layout's, unless the file says otherwise)
	char *text = file_read_all(SETTINGS_PATH, &size);
	if (!text)
		return;
	char *buf = realloc(text, size + 1);
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		int v;
		if (sscanf(line, "music_volume=%d", &v) == 1) settings.music_volume = clampi(v, 0, 10);
		else if (sscanf(line, "voice_volume=%d", &v) == 1) settings.voice_volume = clampi(v, 0, 10);
		else if (sscanf(line, "subtitles=%d", &v) == 1) settings.subtitles = !!v;
		else if (sscanf(line, "stair_bounce=%d", &v) == 1) settings.stair_bounce = clampi(v, 0, 3);
		else if (sscanf(line, "look_speed=%d", &v) == 1) settings.look_speed = clampi(v, 1, 10);
		else if (sscanf(line, "invert_look=%d", &v) == 1) settings.invert_look = !!v;
		else if (sscanf(line, "bloom=%d", &v) == 1) settings.post = !!v;
		// (debug_controls: no longer read - switched on by mistake, one row above Back, it gave
		// every shout; a test run turns it on: menu_set_debug)
		else if (sscanf(line, "third_person=%d", &v) == 1) settings.third_person = !!v;
		else if (sscanf(line, "difficulty=%d", &v) == 1) settings.difficulty = clampi(v, 0, 5);
		else if (sscanf(line, "performance_level=%d", &v) == 1) settings.performance = clampi(v, 0, PERF_LEVELS - 1);
		else if (sscanf(line, "frame_rate=%d", &v) == 1) settings.frame_rate = clampi(v, 0, 2);
		else if (sscanf(line, "controls=%d", &v) == 1) settings.controls = clampi(v, 0, 1), binds_preset(settings.controls);
		else if (!strncmp(line, "bind_", 5)) { // (a button mapped: "bind_sneak=4")
			for (int a = 0; a < ACT_COUNT; a++) {
				const size_t n = strlen(ACTION_KEY[a]);
				if (!strncmp(line + 5, ACTION_KEY[a], n) && line[5 + n] == '=')
					settings.bind[a] = clampi(atoi(line + 6 + n), 0, KEY_COUNT - 1);
			}
		}
		else if (sscanf(line, "rear_touch=%d", &v) == 1) settings.rear_touch = !!v;
		else if (sscanf(line, "compass=%d", &v) == 1) settings.compass = !!v;
		else if (sscanf(line, "fov_first=%d", &v) == 1) settings.fov_first = clampi(v, 50, 90);
		else if (sscanf(line, "fov_third=%d", &v) == 1) settings.fov_third = clampi(v, 50, 90);
		else if (sscanf(line, "crosshair=%d", &v) == 1) settings.hud_crosshair = !!v;
		else if (sscanf(line, "quest_markers=%d", &v) == 1) settings.hud_markers = !!v;
		else if (sscanf(line, "bar_fade=%d", &v) == 1) settings.hud_bar_fade = !!v;
		else if (sscanf(line, "msaa=%d", &v) == 1) settings.msaa = !!v;
		else if (sscanf(line, "dynamic_resolution=%d", &v) == 1) settings.dynres = !!v;
		else if (sscanf(line, "fsr=%d", &v) == 1) settings.upscaler = POST_UPSCALE_BILINEAR; // (older: FSR on / off)
		else if (sscanf(line, "upscaler=%d", &v) == 1) settings.upscaler = clampi(v, POST_UPSCALE_OFF, POST_UPSCALE_BILINEAR); // (2, FSR: bilinear)
		else if (sscanf(line, "resolution_height=%d", &v) == 1) { // (after its width: two sizes share 544, two 640)
			for (int r = 0; r < RES_COUNT; r++)
				if (RESOLUTIONS[r][0] == RESOLUTIONS[settings.resolution][0] && RESOLUTIONS[r][1] == v)
					settings.resolution = r;
		}
		else if (sscanf(line, "motion_blur=%d", &v) == 1) settings.motion_blur = !!v;
		else if (sscanf(line, "resolution_width=%d", &v) == 1) { // (by its width: new sizes don't shift it)
			settings.resolution = v > 720 ? RES_720 : RES_480; // (a size no longer offered: the nearest)
			for (int r = 0; r < RES_COUNT; r++)
				if (RESOLUTIONS[r][0] == v)
					settings.resolution = r;
		} else if (sscanf(line, "resolution=%d", &v) == 1) { // (older: 0 480, 1 720, 2 864, 3 960)
			static const int old[4] = {RES_480, RES_720, RES_720, RES_720}; // (the larger ones are gone)
			settings.resolution = old[clampi(v, 0, 3)];
		}
		else if (sscanf(line, "readout=%d", &v) == 1) settings.readout = !!v;

	}
	free(buf);
}

void menu_init(void) {
	load();
	apply();
}

const Settings *menu_settings(void) {
	return &settings;
}

void menu_set_debug(int on) { // (a test run's: not in Options)
	settings.debug = !!on;
}

float menu_step_keep(void) {
	static const float KEEP[4] = {0.0f, 0.2f, 0.35f, 0.6f};
	return KEEP[settings.stair_bounce];
}

// The inventory page: what's carried in the category
// The Magic page's rows: spells known (their index), then learned ones not castable here
// (ROW_OTHER - i), the race's power, the shouts (ROW_SHOUT - j), Back
#define MAGIC_ROWS_MAX (SPELL_COUNT + 2 * MAGIC_OTHER_MAX + 2 + SHOUT_MAX + 1)
enum { ROW_BACK = -1, ROW_POWER = -2, ROW_BEAST = -3, ROW_SHOUT = -10, ROW_OTHER = -100 };
static int mrows[MAGIC_ROWS_MAX];

static int magic_rows(void) {
	int n = 0;
	for (int s = 0; s < magic_count(); s++)
		if (magic_known(s))
			mrows[n++] = s;
	for (int i = 0; i < magic_other_count(); i++)
		if (!magic_castable_name(magic_other(i)))
			mrows[n++] = ROW_OTHER - i;
	if (races_power_name())
		mrows[n++] = ROW_POWER;
	if (beast_known()) // (the Companions' gift: Beast Form)
		mrows[n++] = ROW_BEAST;
	for (int j = 0; j < shouts_count(); j++)
		if (shouts_learned(j) > 0) // (a shout you've a word of: learned at a word wall)
			mrows[n++] = ROW_SHOUT - j;
	mrows[n++] = ROW_BACK;
	return n;
}

// A book read (Skyrim's): a spell tome teaches its spell and is gone (known already: nothing);
// any other opens to read, page by page - a skill book's skill rising the first time
static int book_item = -1, book_page, book_pages;

static void read_book(int item) {
	char key[MAGIC_KEY_LEN], text[96];
	const char *spell_name;
	if (items_book_spell(item, key, sizeof(key), &spell_name)) {
		const int learned = magic_learn(key, spell_name);
		if (!learned) {
			ui_notice("You already know that spell", 2.5f);
			return;
		}
		items_add(item, -1);
		snprintf(text, sizeof(text), learned == 1 ? "Learned %s" : "Learned %s (it can't be cast yet)", spell_name);
		ui_notice(text, 3.0f);
		sfx_play("ui_ok", 0.6f, 0.0f);
		inv_dirty = 1;
		return;
	}
	static char words[65536];
	if (!items_book_text(item, words, sizeof(words)) || !words[0])
		snprintf(words, sizeof(words), "(The pages are blank.)");
	book_pages = ui_book_open(items_name(item), words);
	book_page = 0;
	book_item = book_pages > 0 ? item : -1;
	const int skill = items_book_skill(item);
	if (skill >= 0 && items_book_first_read(item))
		skills_raise(skill); // ("<skill> increased to N")
}

// A scroll into a hand, ready to read (again: out of it)
static void hand_scroll(int h, int item) {
	const int on = magic_hand(h) != MAGIC_SCROLL_BASE + item;
	magic_set_scroll(h, on ? item : -1);
	if (on) {
		magic_set_readied(1);
		char text[64];
		snprintf(text, sizeof(text), "%s hand: %s", h == HAND_LEFT ? "Left" : "Right", items_name(item));
		ui_notice(text, 1.5f);
	}
}

static int inventory_rows(void) {
	int all[CARRIED_MAX], n = items_inventory(all, CARRIED_MAX);
	carried_n = 0;
	for (int i = 0; i < n; i++)
		if (inv_cat == CAT_ALL || items_category(all[i]) == inv_cat)
			carried_list[carried_n++] = all[i];
	return carried_n;
}

// The container page: Take all, its contents
static int container_rows(void) {
	cont_n = items_in_container(container_handle, cont_items, cont_counts, CONTAINER_LIST);
	return 1 + cont_n;
}

// Storing: what you carry (gold first), to put in the container
static int store_rows(void) {
	store_n = 0;
	if (items_gold() > 0)
		store_list[store_n++] = items_find_gold();
	store_n += items_inventory(store_list + store_n, CARRIED_MAX - store_n);
	return store_n;
}

// Bartering's rows: their stock (not their gold) or what you carry they'd buy (not worn: take it off)
static int barter_items[CARRIED_MAX + 64], barter_counts[CARRIED_MAX + 64];

static int barter_gold(void) {
	int its[CONTAINER_LIST], counts[CONTAINER_LIST];
	const int n = items_in_container(container_handle, its, counts, CONTAINER_LIST);
	for (int i = 0; i < n; i++)
		if (its[i] == items_find_gold())
			return counts[i];
	return 0;
}

static int barter_rows(void) {
	int n = 0;
	if (!barter_sell) {
		int its[CONTAINER_LIST], counts[CONTAINER_LIST];
		const int k = items_in_container(container_handle, its, counts, CONTAINER_LIST);
		for (int i = 0; i < k; i++)
			if (its[i] != items_find_gold())
				barter_items[n] = its[i], barter_counts[n++] = counts[i];
		return n;
	}
	static int list[CARRIED_MAX];
	const int k = items_inventory(list, CARRIED_MAX);
	for (int i = 0; i < k; i++) {
		// (Merchant: any kind of thing to any merchant; stolen goods: a fence's only - Fence: one you've invested in too)
		const int buys = items_vendor_buys(barter_vendor, list[i]) ||
		                 (perks_av(AV_MERCHANT) > 0.0f && !items_vendor_fence(barter_vendor) && list[i] != items_find_gold());
		if (buys && !equip_worn(list[i])) {
			const int fenced = perks_av(AV_FENCE) > 0.0f && items_vendor_invested(barter_vendor);
			const int count = items_vendor_fence(barter_vendor) ? items_stolen(list[i])
			                : fenced ? items_have(list[i]) : items_have(list[i]) - items_stolen(list[i]);
			if (count > 0)
				barter_items[n] = list[i], barter_counts[n++] = count;
		}
	}
	return n;
}

// Pickpocket, Fortified (a potion's, a ring's: that much more)
static int pickpocket_skill(void);

// What of their pockets can be taken: a weapon is the one they wield - only with Misdirection
// (Perfect Touch: anything they have on)
#define PERK_PERFECT_TOUCH 0x00058205u
static int pocket_items(void) {
	int n = items_in_container(container_handle, cont_items, cont_counts, CONTAINER_LIST), k = 0;
	for (int i = 0; i < n; i++) {
		PerkContext ctx = {0};
		ctx.school = -1, ctx.keyword_count = items_keywords(cont_items[i], &ctx.keywords);
		if (items_type(cont_items[i]) == IT_WEAP && !perks_has_form(PERK_PERFECT_TOUCH) &&
		    perks_apply(EP_CAN_PICKPOCKET_EQUIPPED, 0.0f, &ctx) <= 0.0f)
			continue;
		cont_items[k] = cont_items[i], cont_counts[k++] = cont_counts[i];
	}
	return k;
}

// Poisoned (Pickpocket): a poison of yours to slip into their pockets (-1 none, or not the perk)
#define PERK_POISONED 0x00105f28u
static int plant_poison(void) {
	if (pocket_npc < 0 || !perks_has_form(PERK_POISONED))
		return -1;
	static int list[CARRIED_MAX];
	const int n = items_inventory(list, CARRIED_MAX);
	for (int i = 0; i < n; i++)
		if (items_is_poison(list[i]))
			return list[i];
	return -1;
}

// Skyrim's pickpocket chance for one of your target's things, with the perks: Light Fingers, Keymaster
// (keys), Cutpurse (gold), Night Thief (asleep)
static int pickpocket_chance(int item, int count) {
	const int base = items_pickpocket_chance(item, count, pickpocket_skill(), NPC_PICKPOCKET_SKILL);
	PerkContext ctx = {0};
	ctx.school = -1, ctx.keyword_count = items_keywords(item, &ctx.keywords), ctx.item_form = (uint32_t)items_form(item);
	ctx.target_asleep = pocket_npc >= 0 && actors_asleep(pocket_npc);
	return (int)fmaxf(0.0f, fminf(PICKPOCKET_MAX, perks_apply(EP_MOD_PICKPOCKET_CHANCE, (float)base, &ctx)));
}

static int pickpocket_skill(void) {
	const int k = skills_state()->level[SK_PICKPOCKET];
	return (int)(k * fmaxf(0.0f, 1.0f + effects_skill(SK_PICKPOCKET) / 100.0f) + 0.5f);
}

// Fortify Barter: prices that much better
static float barter_fortify(void) {
	return fminf(0.5f, fmaxf(-0.5f, effects_skill(SK_SPEECH) / 100.0f));
}

static int buy_price(int item, int speech) { // (Haggling, Allure: the perks' multipliers)
	return items_buy_price(item, speech, perks_apply(EP_MOD_BUY_PRICES, 1.0f, NULL) * (1.0f - barter_fortify()));
}

static int sell_price(int item, int speech) {
	return items_sell_price(item, speech, perks_apply(EP_MOD_SELL_PRICES, 1.0f, NULL) * (1.0f + barter_fortify()));
}

// A window of a long list around the selection (MENU_ROWS lines)
static void show_list(const char *title, const char *const *items, int total) {
	int first = selected - MENU_ROWS / 2;
	if (first > total - MENU_ROWS)
		first = total - MENU_ROWS;
	if (first < 0)
		first = 0;
	static char titled[128]; // (longer than fits: where you are in it, "Options  3 / 16")
	if (total > MENU_ROWS && title) {
		snprintf(titled, sizeof(titled), "%.70s   %d / %d", title, selected + 1, total);
		title = titled;
	}
	ui_menu(title, items + first, total - first < MENU_ROWS ? total - first : MENU_ROWS, selected - first);
}

static int skills_rows(void) { // level-up choices (when one is pending), the skills, Back
	return (skills_state()->pending > 0 ? 3 : 0) + SKILL_COUNT + 1;
}

static void show_saves(void);

// The world map (see ui_map): every marker (discovered ones bright), you, the one under the
// view's middle
static void show_map(void) {
	static UiMap v;
	static char title[64], line[96];
	memset(&v, 0, sizeof(v));
	v.province_tex = worldmap_province(v.province);
	v.region_tex = worldmap_region(v.region);
	v.centre[0] = map_centre[0], v.centre[1] = map_centre[1], v.cells = map_cells;
	const float view_h = 544.0f - 50.0f, ppc = view_h / map_cells; // (the screen's pixels a cell)
	float best = 16.0f;
	map_sel = -1;
	for (int i = 0; i < worldmap_count() && v.count < UI_MAP_MARKERS; i++) {
		int kind, disc, in_region;
		const char *name;
		float x, z;
		worldmap_marker(i, &kind, &name, &x, &z, &disc, &in_region);
		const int k = v.count++;
		v.mx[k] = x / CELL_M, v.my[k] = -z / CELL_M, v.kind[k] = (uint8_t)kind;
		v.state[k] = (uint8_t)(!in_region ? 0 : disc ? 2 : 1);
		const float d = hypotf(v.mx[k] - map_centre[0], v.my[k] - map_centre[1]) * ppc;
		if (d < best)
			best = d, map_sel = i, v.selected = k;
	}
	if (map_sel < 0)
		v.selected = -1;
	v.player[0] = player_at[0] / CELL_M, v.player[1] = -player_at[1] / CELL_M;
	v.player[2] = player_at[2] * (float)M_PI / 180.0f;
	v.quest_on = quest_on;
	for (int q = 0; q < quest_on && q < UI_MAP_QUESTS; q++)
		v.quest[q][0] = quest_at[q][0] / CELL_M, v.quest[q][1] = -quest_at[q][1] / CELL_M;
	if (map_sel >= 0) {
		int kind, disc, in_region;
		const char *name;
		float x, z;
		worldmap_marker(map_sel, &kind, &name, &x, &z, &disc, &in_region);
		snprintf(title, sizeof(title), "%s", name);
		const float km = hypotf(x - player_at[0], z - player_at[1]) / 1000.0f;
		if (!in_region)
			snprintf(line, sizeof(line), "%.1f km away: not part of this world yet", km);
		else if (!disc)
			snprintf(line, sizeof(line), "%.1f km away: undiscovered", km);
		else
			snprintf(line, sizeof(line), "%.1f km away%s: X to fast travel", km,
			         interior_location_cleared(name) ? ", Cleared" : "");
		v.title = title, v.line = line;
	}
	v.hints = local_known ? "Move: stick / D-pad    L/R Zoom    X Fast travel    [] Local map    O Back"
	                      : "Move: stick / D-pad    L/R Zoom    X Fast travel    O Back";
	ui_map(&v);
}

// The local map: this interior's floor plan, centred where the view is, your arrow
static void show_local(void) {
	static UiLocalMap v;
	memset(&v, 0, sizeof(v));
	v.tri_count = navmesh_interior_mesh(&v.verts, &(int){0}, &v.tri_verts, &v.tri_stride);
	v.centre[0] = local_centre[0], v.centre[1] = local_centre[1], v.metres = local_metres;
	v.player[0] = local_at[0], v.player[1] = local_at[1], v.player[2] = local_at[2];
	v.player[3] = local_at[3] * (float)M_PI / 180.0f;
	const char *labels[UI_LOCAL_DOORS];
	v.doors = interior_doors(v.door, labels, UI_LOCAL_DOORS);
	v.title = local_name;
	v.hints = "Move: stick / D-pad    L/R Zoom    [] World map    O Back";
	ui_local_map(&v);
}

// The perk tree of perk_skill (see ui_perk_tree)
static void show_perks(void) {
	static UiPerkTree v;
	static char skill[48], prev[32], next[32], title[64], need[64], points[48];
	memset(&v, 0, sizeof(v));
	const SkillState *st = skills_state();
	snprintf(skill, sizeof(skill), "%s %d", skills_name(perk_skill), st->level[perk_skill]);
	snprintf(prev, sizeof(prev), "< %s", skills_name((perk_skill + SKILL_COUNT - 1) % SKILL_COUNT));
	snprintf(next, sizeof(next), "%s >", skills_name((perk_skill + 1) % SKILL_COUNT));
	snprintf(points, sizeof(points), "Perks to increase: %d", st->perk_points);
	v.skill = skill, v.prev = prev, v.next = next, v.points = points, v.progress = skills_progress(perk_skill);
	v.count = perks_node_count(perk_skill);
	if (v.count > UI_PERK_NODES)
		v.count = UI_PERK_NODES;
	for (int i = 0; i < v.count; i++) {
		const int *links, *ranks;
		int nl, nr;
		v.state[i] = -1;
		if (!perks_node(perk_skill, i, &v.x[i], &v.y[i], &links, &nl, &ranks, &nr))
			continue;
		for (int k = 0; k < nl && v.link_count < 48; k++)
			if (nr) // (the root's links aren't drawn)
				v.links[v.link_count][0] = i, v.links[v.link_count++][1] = links[k];
		if (!nr)
			continue;
		const int have = perks_node_taken(perk_skill, i);
		char why[64];
		v.state[i] = have >= nr ? 3 : have > 0 ? 2 : perks_can_take(perk_skill, i, why, sizeof(why)) ? 1 : 0;
	}
	v.selected = perk_node;
	const int *links, *ranks;
	int nl, nr;
	float x, y;
	if (perks_node(perk_skill, perk_node, &x, &y, &links, &nl, &ranks, &nr) && nr) {
		const int have = perks_node_taken(perk_skill, perk_node);
		const int shown = ranks[have < nr ? have : nr - 1];
		if (nr > 1)
			snprintf(title, sizeof(title), "%s (%d/%d)", perks_name(shown), have, nr);
		else
			snprintf(title, sizeof(title), "%s", perks_name(shown));
		char why[64];
		const int can = perks_can_take(perk_skill, perk_node, why, sizeof(why));
		if (can)
			snprintf(need, sizeof(need), "Requires %s %d: X to take it", skills_name(perk_skill), perks_level(shown));
		else
			snprintf(need, sizeof(need), "%s", why);
		v.title = title, v.desc = perks_description(shown), v.need = need;
	}
	v.hints = "X Take perk    L/R Skill    O Back";
	ui_perk_tree(&v);
}

// The perk tree: the node nearest in a direction from the selected one
static int perk_step(int dx, int dy) {
	const int *links, *ranks;
	int nl, nr, best = -1;
	float x0, y0, best_score = 1e9f;
	if (!perks_node(perk_skill, perk_node, &x0, &y0, &links, &nl, &ranks, &nr))
		return perk_node;
	for (int i = 0; i < perks_node_count(perk_skill); i++) {
		float x, y;
		if (i == perk_node || !perks_node(perk_skill, i, &x, &y, &links, &nl, &ranks, &nr) || !nr)
			continue;
		const float ex = x - x0, ey = y - y0, d = sqrtf(ex * ex + ey * ey);
		const float along = (ex * dx + ey * dy) / fmaxf(d, 0.001f);
		if (along < 0.35f)
			continue;
		const float score = d * (2.0f - along);
		if (score < best_score)
			best = i, best_score = score;
	}
	return best >= 0 ? best : perk_node;
}

// The first perk of a tree (a node the root leads to)
static int perk_first(int skill) {
	const int *links, *ranks;
	int nl, nr;
	float x, y;
	for (int i = 0; i < perks_node_count(skill); i++)
		if (perks_node(skill, i, &x, &y, &links, &nl, &ranks, &nr) && !nr && nl)
			return links[0];
	return 1;
}

// A poison chosen in the inventory (Skyrim's): which weapon takes it - the right hand's, the
// left's, the next arrow - the one in hand first; nothing to take it: "No weapon equipped"
static int poison_item = -1, poison_sel, poison_slots[EQUIP_POISON_SLOTS], poison_n;

static void choose_poison(int item) {
	poison_n = 0;
	for (int slot = 0; slot < EQUIP_POISON_SLOTS; slot++)
		if (equip_poison_target(slot))
			poison_slots[poison_n++] = slot;
	if (!poison_n) {
		ui_notice("No weapon equipped", 2.0f);
		return;
	}
	poison_item = item, poison_sel = 0;
}

// The alchemy lab (Skyrim's): the effects you know down the left (All, then each: only the
// ingredients with it), your ingredients in the middle (CROSS picks up to three), the card on
// the right - the ingredient's four effects, or with two or more picked what they'd make -
// and SQUARE makes it
#define ALCH_ROWS_MAX 256
#define ALCH_CATS_MAX 64
static int alch_rows[ALCH_ROWS_MAX], alch_row_n;
static int alch_cats[ALCH_CATS_MAX], alch_cat_n; // (magic effects known; 0th: All)
static int alch_cat, alch_focus, alch_sel;
static int alch_pick[3], alch_picked;

static int ingredient_knows(int item, int mgef) { // (the player knows it has that effect)
	ItemEffect e[ITEM_EFFECTS_MAX];
	const int n = items_effects(item, e, ITEM_EFFECTS_MAX);
	for (int k = 0; k < n; k++)
		if (e[k].mgef == mgef && (items_known(item) >> k & 1))
			return 1;
	return 0;
}

static void alchemy_lists(void) {
	alch_cat_n = 1, alch_cats[0] = -1;
	for (int i = 0; i < items_count(); i++) { // (the effects known of what you carry)
		if (items_type(i) != IT_INGR || items_have(i) <= 0)
			continue;
		ItemEffect e[ITEM_EFFECTS_MAX];
		const int n = items_effects(i, e, ITEM_EFFECTS_MAX);
		for (int k = 0; k < n; k++) {
			if (!(items_known(i) >> k & 1))
				continue;
			int seen = 0;
			for (int c = 1; c < alch_cat_n; c++)
				seen |= alch_cats[c] == e[k].mgef;
			if (seen || alch_cat_n >= ALCH_CATS_MAX)
				continue;
			int at = alch_cat_n++; // (by name)
			const char *name = items_magic_effect(e[k].mgef) ? items_magic_effect(e[k].mgef)->name : "";
			while (at > 1 && strcmp(items_magic_effect(alch_cats[at - 1])->name, name) > 0)
				alch_cats[at] = alch_cats[at - 1], at--;
			alch_cats[at] = e[k].mgef;
		}
	}
	if (alch_cat >= alch_cat_n)
		alch_cat = 0;
	alch_row_n = 0;
	for (int i = 0; i < items_count() && alch_row_n < ALCH_ROWS_MAX; i++) {
		if (items_type(i) != IT_INGR || items_have(i) <= 0 || (alch_cat > 0 && !ingredient_knows(i, alch_cats[alch_cat])))
			continue;
		int at = alch_row_n++;
		while (at > 0 && strcmp(items_name(alch_rows[at - 1]), items_name(i)) > 0)
			alch_rows[at] = alch_rows[at - 1], at--;
		alch_rows[at] = i;
	}
	if (alch_sel >= alch_row_n)
		alch_sel = alch_row_n ? alch_row_n - 1 : 0;
	// (picked ones used up: dropped)
	for (int k = 0; k < alch_picked; k++)
		if (items_have(alch_pick[k]) <= 0)
			alch_pick[k] = alch_pick[--alch_picked], k--;
}

static void show_alchemy(void) {
	alchemy_lists();
	static const char *cats[ALCH_CATS_MAX];
	static char names[ALCH_ROWS_MAX][64];
	static const char *name_ptrs[ALCH_ROWS_MAX], *marks[ALCH_ROWS_MAX];
	for (int c = 0; c < alch_cat_n; c++)
		cats[c] = c ? items_magic_effect(alch_cats[c])->name : "All";
	for (int i = 0; i < alch_row_n; i++) {
		const int it = alch_rows[i], count = items_have(it);
		snprintf(names[i], sizeof(names[0]), count > 1 ? "%s (%d)" : "%s", items_name(it), count);
		name_ptrs[i] = names[i];
		marks[i] = "";
		for (int k = 0; k < alch_picked; k++)
			if (alch_pick[k] == it)
				marks[i] = "Selected";
	}
	UiInventory v = {0};
	v.categories = cats, v.category_count = alch_cat_n, v.category = alch_cat;
	v.names = name_ptrs, v.marks = marks, v.count = alch_row_n, v.selected = alch_sel, v.focus = alch_focus;
	static char lines[4][48], text[400], title[64], status[128];
	const int it = alch_focus == 1 && alch_row_n ? alch_rows[alch_sel] : -1;
	AlchemyMix mix;
	if (alch_picked >= 2) { // (what they'd make)
		if (alchemy_mix(alch_pick, alch_picked, &mix)) {
			int n = 0;
			text[0] = 0;
			for (int k = 0; k < mix.n && n < (int)sizeof(text) - 1; k++) {
				char one[160];
				items_effect_text(mix.e[k].mgef, mix.e[k].magnitude, mix.e[k].seconds, one, sizeof(one));
				n += snprintf(text + n, sizeof(text) - n, "%s%s", n ? " " : "", one);
			}
			snprintf(title, sizeof(title), "%s", mix.name);
			snprintf(lines[3], sizeof(lines[0]), "VALUE %d", mix.value);
		} else {
			snprintf(title, sizeof(title), "No Effect");
			snprintf(text, sizeof(text), "These ingredients have no effect in common.");
			lines[3][0] = 0;
		}
		for (int k = 0; k < 3; k++)
			snprintf(lines[k], sizeof(lines[0]), "%s", k < alch_picked ? items_name(alch_pick[k]) : "");
		for (int j = 0; j < 4; j++)
			v.card_lines[j] = lines[j][0] ? lines[j] : NULL;
		v.card_title = title, v.card_text = text;
	} else if (it >= 0) {
		snprintf(lines[0], sizeof(lines[0]), "WEIGHT %g", items_weight(it));
		snprintf(lines[1], sizeof(lines[0]), "VALUE %d", items_value(it));
		v.card_lines[0] = lines[0], v.card_lines[1] = lines[1];
		items_describe(it, text, sizeof(text));
		v.card_title = items_name(it), v.card_text = text;
	}
	preview_set(it >= 0 ? items_model(it) : -1);
	v.hints = alch_focus == 1 ? (alch_picked >= 2 ? "X Select  [] Create  O Back  L/R Effect" : "X Select  O Back  L/R Effect")
	                          : "X Ingredients  O Exit  L/R Effect";
	snprintf(status, sizeof(status), "Alchemy %d    Ingredients %d/3", skills_state()->level[SK_ALCHEMY], alch_picked);
	v.status = status;
	ui_menu(NULL, NULL, 0, 0);
	ui_inventory(&v);
}

static int open_page(int p);

// Improving (a grindstone: weapons, a workbench: armour): what you carry its recipe improves
// (by the item it was made from), the recipe's parts
static int temper_item[CARRIED_MAX], temper_recipe[CARRIED_MAX];
#define PERK_ARCANE_BLACKSMITH 0x0005218eu

static int temper_rows(void) {
	static int carried_list[CARRIED_MAX];
	const int k = items_inventory(carried_list, CARRIED_MAX);
	int all[1200];
	const int nr = items_recipes(craft_station, all, 1200), want = craft_station == 5 ? IT_WEAP : IT_ARMO;
	int n = 0;
	for (int i = 0; i < k; i++) {
		const int it = carried_list[i];
		if (items_type(it) != want || items_temper_level(it) >= 6)
			continue;
		ItemEffect ench[4];
		int resist;
		if (items_enchantment(it, ench, &resist, 4) > 0 && !perks_has_form(PERK_ARCANE_BLACKSMITH))
			continue; // (enchanted: only with Arcane Blacksmith)
		int base = it;
		while (items_made_from(base) >= 0)
			base = items_made_from(base);
		for (int r = 0; r < nr; r++) {
			int made, count, nparts, nperks;
			const int *parts, *need;
			const uint32_t *perks;
			if (items_recipe(all[r], &made, &count, &parts, &need, &nparts, &perks, &nperks) && made == base) {
				temper_item[n] = it, temper_recipe[n++] = all[r];
				break;
			}
		}
	}
	return n;
}

// Skyrim's: the level an improvement reaches with your Smithing (Fine at 15 .. Legendary at the
// top) - twice the steps with the material's perk (Steel Smithing... their Mod Tempering entry on
// the item's material keyword)
static int temper_target(int recipe, int item) {
	(void)recipe;
	PerkContext ctx = {0};
	ctx.school = -1, ctx.keyword_count = items_keywords(item, &ctx.keywords);
	// (Fortify Smithing: as much more skill)
	const float steps = skills_state()->level[SK_SMITHING] * (1.0f + effects_skill(SK_SMITHING) / 100.0f) / 25.0f *
	                    perks_apply(EP_MOD_TEMPERING, 1.0f, &ctx);
	const int level = 1 + (int)steps;
	return level > 6 ? 6 : level;
}

static int can_make(int recipe);

// What a station can make for you now: the perks a recipe needs, taken
static int craft_rows(void) {
	if (craft_station == 5 || craft_station == 6)
		return craft_rows_n = temper_rows();
	int all[1200];
	const int n = items_recipes(craft_station, all, 1200);
	craft_rows_n = 0;
	for (int i = 0; i < n; i++) {
		int made, count, nparts, nperks;
		const int *parts, *need;
		const uint32_t *perks;
		if (!items_recipe(all[i], &made, &count, &parts, &need, &nparts, &perks, &nperks))
			continue;
		int ok = 1;
		for (int k = 0; k < nperks; k++)
			ok &= perks_has_form(perks[k]);
		if (ok)
			craft_list[craft_rows_n++] = all[i];
	}
	// (what you can make now first, then by name)
	for (int i = 1; i < craft_rows_n; i++)
		for (int j = i; j > 0; j--) {
			int ma, ca, mb, cb, na, nb, pa, pb;
			const int *xa, *xb, *ya, *yb;
			const uint32_t *qa, *qb;
			items_recipe(craft_list[j - 1], &ma, &ca, &xa, &ya, &na, &qa, &pa);
			items_recipe(craft_list[j], &mb, &cb, &xb, &yb, &nb, &qb, &pb);
			const int ka = can_make(craft_list[j - 1]), kb = can_make(craft_list[j]);
			if (ka > kb || (ka == kb && strcmp(items_name(ma), items_name(mb)) <= 0))
				break;
			const int t = craft_list[j];
			craft_list[j] = craft_list[j - 1], craft_list[j - 1] = t;
		}
	return craft_rows_n;
}

static int can_make(int recipe) {
	int made, count, nparts, nperks;
	const int *parts, *need;
	const uint32_t *perks;
	if (!items_recipe(recipe, &made, &count, &parts, &need, &nparts, &perks, &nperks))
		return 0;
	for (int k = 0; k < nparts; k++)
		if (items_have(parts[k]) < need[k])
			return 0;
	return 1;
}

// Enchanting's lists: enchanted things to disenchant; weapons and armour without one; the
// enchantments learned that suit the chosen thing (harm for weapons, the rest for armour); soul
// gems with a soul
static int ench_rows(void) {
	static int carried_list[CARRIED_MAX];
	const int k = items_inventory(carried_list, CARRIED_MAX);
	int n = 0;
	if (ench_step == ENCH_DISENCHANT || ench_step == ENCH_ITEM) {
		for (int i = 0; i < k; i++) {
			const int it = carried_list[i], t = items_type(it);
			ItemEffect e[ITEM_EFFECTS_MAX];
			const int has = items_enchantment(it, e, NULL, ITEM_EFFECTS_MAX) > 0;
			if ((t == IT_WEAP || t == IT_ARMO) && (ench_step == ENCH_DISENCHANT ? has : !has) && !equip_worn(it))
				ench_list[n++] = it;
		}
	} else if (ench_step == ENCH_EFFECT) {
		const int weapon = items_type(ench_item) == IT_WEAP;
		for (int i = 0; i < items_learned_count(); i++) {
			int mgef;
			float mag, secs;
			items_learned(i, &mgef, &mag, &secs);
			const MagicEffect *m = items_magic_effect(mgef);
			if (m && ((m->flags & MGEF_DETRIMENTAL) != 0) == weapon)
				ench_list[n++] = i;
		}
	} else if (ench_step == ENCH_GEM) {
		for (int i = 0; i < k; i++)
			if (items_soul(carried_list[i]) > 0)
				ench_list[n++] = carried_list[i];
	}
	return n;
}

// Skyrim's: the learned effect's strength by the soul (petty .. grand), Enchanting, the perks
static float ench_magnitude(float learned, int soul) {
	static const float by_soul[6] = {0.0f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f};
	const float skill = (float)skills_state()->level[SK_ENCHANTING];
	return learned * by_soul[soul < 0 ? 0 : soul > 5 ? 5 : soul] * (0.75f + skill / 200.0f) *
	       perks_apply(EP_MOD_ENCHANT_POWER, 1.0f, NULL) * fmaxf(0.0f, 1.0f + effects_skill(SK_ENCHANTING) / 100.0f);
}

void menu_open_crafting(int station) {
	craft_station = station;
	ench_step = ENCH_TOP;
	open_page(station == 7 ? PAGE_ENCHANT : PAGE_CRAFT);
}

void menu_open_alchemy(void) {
	alch_cat = alch_focus = alch_sel = alch_picked = 0;
	open_page(PAGE_ALCHEMY);
}

static void show(void) {
	static char lines[OPT_COUNT][64];
	static const char *items[OPT_COUNT];
	if (poison_item >= 0) { // (the weapon to put a poison on)
		static char rows[EQUIP_POISON_SLOTS + 1][72];
		static const char *row_items[EQUIP_POISON_SLOTS + 1];
		static const char *const where[EQUIP_POISON_SLOTS] = {"Right hand", "Left hand", "Next arrow"};
		for (int i = 0; i < poison_n; i++) {
			snprintf(rows[i], sizeof(rows[0]), "%s|%.40s", where[poison_slots[i]], equip_poison_target(poison_slots[i]));
			row_items[i] = rows[i];
		}
		snprintf(rows[poison_n], sizeof(rows[0]), "Cancel");
		row_items[poison_n] = rows[poison_n];
		static char title[80];
		snprintf(title, sizeof(title), "Apply %.40s to", items_name(poison_item));
		ui_inventory(NULL);
		preview_set(-1);
		ui_menu(title, row_items, poison_n + 1, poison_sel);
		return;
	}
	if (page != PAGE_SAVE && page != PAGE_LOAD)
		ui_save_list(NULL);
	if (page != PAGE_PERKS)
		ui_perk_tree(NULL);
	if (page != PAGE_MAP)
		ui_map(NULL);
	if (page != PAGE_MAP || !map_local)
		ui_local_map(NULL);
	if (page == PAGE_MAP && map_local) {
		ui_menu(NULL, NULL, 0, 0);
		ui_map(NULL);
		show_local();
		return;
	}
	if (page == PAGE_MAP) {
		ui_menu(NULL, NULL, 0, 0);
		show_map();
		return;
	}
	if (page == PAGE_PERKS) {
		ui_menu(NULL, NULL, 0, 0);
		show_perks();
		return;
	}
	if (page == PAGE_SAVE || page == PAGE_LOAD) {
		ui_menu(NULL, NULL, 0, 0);
		show_saves();
		return;
	}
	if (page == PAGE_MAIN) {
		static char camera[48];
		snprintf(camera, sizeof(camera), "Camera|< %s >", settings.third_person ? "Third person" : "First person");
		const char *main_items[MAIN_COUNT] = {"Resume", "Inventory", "Magic", "Skills", "Map", "Quests", "Wait", "Save game", "Load game",
		                                      "Options", camera, "Quit game"};
		ui_menu("Paused", main_items, MAIN_COUNT, selected);
		return;
	}
	if (page == PAGE_WAIT) {
		static char line[48];
		snprintf(line, sizeof(line), "%s|< %d hour%s >", sleeping ? "Sleep" : "Wait", wait_hours, wait_hours == 1 ? "" : "s");
		const char *wait_items[2] = {line, "Back"};
		ui_menu(sleeping ? "Sleep" : "Wait", wait_items, 2, selected);
		return;
	}
	if (page == PAGE_MAGIC) {
		// The spells known (L / R: a hand), those learned that can't be cast here yet, then the
		// voice slot's choices (Skyrim's powers and shouts: X readies one for SELECT)
		const int n = magic_rows();
		static char lines_m[MAGIC_ROWS_MAX][72];
		static const char *items_m[MAGIC_ROWS_MAX];
		for (int i = 0; i < n; i++) {
			const int r = mrows[i];
			if (r >= 0) {
				const int l = magic_hand(HAND_LEFT) == r, rh = magic_hand(HAND_RIGHT) == r;
				snprintf(lines_m[i], 72, "%s%s|%s", magic_name(r), menu_is_favorite(FAV_SPELL, magic_name(r)) ? " *" : "",
				         l && rh ? "Both hands" : l ? "Left hand" : rh ? "Right hand" : i == selected ? "L left, R right" : "");
			} else if (r <= ROW_OTHER) {
				snprintf(lines_m[i], 72, "%s|Can't be cast yet", magic_other(ROW_OTHER - r));
			} else if (r == ROW_POWER) {
				snprintf(lines_m[i], 72, "%s%s|%s", races_power_name(), menu_is_favorite(FAV_SHOUT, races_power_name()) ? " *" : "",
				         shouts_equipped() < 0 ? (races_power_ready() ? "Voice: SELECT" : "Voice: SELECT (used today)")
				                               : (races_power_ready() ? "" : "Used today"));
			} else if (r == ROW_BEAST) {
				const int on = beast_readied() && shouts_equipped() < 0;
				snprintf(lines_m[i], 72, "Beast Form|%s", on ? (beast_ready() ? "Voice: SELECT" : "Voice: SELECT (used today)")
				                                              : (beast_ready() ? "" : "Used today"));
			} else if (r <= ROW_SHOUT) {
				const int j = ROW_SHOUT - r;
				if (shouts_learned(j) > shouts_known(j))
					snprintf(lines_m[i], 72, "%s|%s%s", shouts_name(j), shouts_known(j) ? shouts_words(j, shouts_known(j)) : "",
					         shouts_souls() > 0 ? "  X: unlock a word (1 soul)" : "  a word locked: needs a dragon soul");
				else
					snprintf(lines_m[i], 72, "%s%s|%s%s", shouts_name(j), menu_is_favorite(FAV_SHOUT, shouts_name(j)) ? " *" : "",
					         shouts_words(j, shouts_known(j)), shouts_equipped() == j ? "  Voice: SELECT" : "");
			} else {
				snprintf(lines_m[i], 72, "Back");
			}
			items_m[i] = lines_m[i];
		}
		static char title_m[64];
		const int r = selected >= 0 && selected < n ? mrows[selected] : ROW_BACK;
		if (r >= 0)
			snprintf(title_m, sizeof(title_m), "Magic: %.38s", magic_detail(r));
		else if (r <= ROW_SHOUT && r > ROW_OTHER)
			snprintf(title_m, sizeof(title_m), "Shout: X to ready, hold SELECT for more words");
		else if (r == ROW_POWER || r == ROW_BEAST)
			snprintf(title_m, sizeof(title_m), "Power: once a day (X to ready, SELECT)");
		else
			snprintf(title_m, sizeof(title_m), "Magic");
		show_list(title_m, items_m, n);
		if (r >= 0 || r == ROW_POWER) // (the favourites: how they're marked and used - in the title it ran past the box)
			ui_menu_note("[] Favorite: D-pad left / right then steps through your favorites; hold it for the list");
		if (r <= ROW_SHOUT && r > ROW_OTHER) { // (Skyrim's words for the shout, and its words' meanings learned so far)
			const int j = ROW_SHOUT - r, w = shouts_learned(j);
			static char note[320];
			int at = snprintf(note, sizeof(note), "%s", shouts_desc(j));
			for (int k = 0; k < w && k < 3 && at < (int)sizeof(note) - 40; k++)
				at += snprintf(note + at, sizeof(note) - at, "%s%s (%s)", k ? ", " : "   ", shouts_word(j, k), shouts_meaning(j, k));
			ui_menu_note(note);
		}
		return;
	}
	if (page == PAGE_SKILLS) {
		static char lines_s[SKILL_COUNT + 4][64];
		static const char *items_s[SKILL_COUNT + 4];
		const SkillState *st = skills_state();
		int n = 0;
		if (st->pending > 0) {
			static const char *const bonus[3] = {"Level up: +10 Health", "Level up: +10 Magicka", "Level up: +10 Stamina"};
			for (int i = 0; i < 3; i++)
				items_s[n++] = bonus[i];
		}
		for (int i = 0; i < SKILL_COUNT; i++, n++) {
			snprintf(lines_s[n], 64, "%s|%d  (%d%%)", skills_name(i), st->level[i], (int)(skills_progress(i) * 100.0f));
			items_s[n] = lines_s[n];
		}
		items_s[n++] = "Back";
		static char title_s[48];
		if (st->perk_points > 0)
			snprintf(title_s, sizeof(title_s), "Level %d (%d%%): %d perk%s, X", st->player_level,
			         (int)(skills_player_progress() * 100.0f), st->perk_points, st->perk_points == 1 ? "" : "s");
		else
			snprintf(title_s, sizeof(title_s), "Skills: level %d (%d%%)", st->player_level,
			         (int)(skills_player_progress() * 100.0f));
		show_list(title_s, items_s, n);
		{ // (Skyrim's words for the chosen skill, under the list)
			const int skill = selected - (st->pending > 0 ? 3 : 0);
			if (skill >= 0 && skill < SKILL_COUNT)
				ui_menu_note(skills_desc(skill));
		}
		return;
	}
	if (page == PAGE_ALCHEMY) {
		show_alchemy();
		return;
	}
	if (page == PAGE_INVENTORY) {
		static const char *cats[CAT_COUNT];
		static char names[CARRIED_MAX][64];
		static const char *name_ptrs[CARRIED_MAX], *marks[CARRIED_MAX];
		for (int c = 0; c < CAT_COUNT; c++)
			cats[c] = items_category_name(c);
		const int n = inventory_rows();
		if (inv_sel >= n)
			inv_sel = n ? n - 1 : 0;
		for (int i = 0; i < n; i++) {
			const int it = carried_list[i], count = items_have(it);
			snprintf(names[i], sizeof(names[0]), count > 1 ? "%s (%d)" : "%s", items_name(it), count);
			name_ptrs[i] = names[i];
			int lk;
			const int in_left = equip_left_weapon(&lk) == it;
			int rk;
			const int in_right = items_category(it) == CAT_WEAPONS && equip_weapon(&rk) == it;
			marks[i] = in_left && in_right ? "Both hands" : in_left ? "Left hand" : in_right ? "Right hand"
			           : equip_worn(it) ? (items_category(it) == CAT_WEAPONS ? "Equipped" : "Worn") : "";
		}
		UiInventory v = {0};
		v.categories = cats, v.category_count = CAT_COUNT, v.category = inv_cat;
		v.names = name_ptrs, v.marks = marks, v.count = n, v.selected = inv_sel, v.focus = inv_focus;
		static char lines[4][32], text[160], status[96];
		const int it = inv_focus == 1 && n ? carried_list[inv_sel] : -1;
		if (it >= 0) {
			int k = 0;
			if (items_type(it) == IT_WEAP || items_type(it) == IT_AMMO) // (arrows too: Skyrim's 8 iron .. 24 daedric)
				snprintf(lines[k++], sizeof(lines[0]), "DAMAGE %d", items_stat(it));
			if (items_type(it) == IT_ARMO)
				snprintf(lines[k++], sizeof(lines[0]), "ARMOR %d", items_stat(it));
			snprintf(lines[k++], sizeof(lines[0]), "WEIGHT %g", items_weight(it));
			snprintf(lines[k++], sizeof(lines[0]), "VALUE %d", items_value(it));
			float left;
			if (items_charge(it, &left) && k < 4) // (an enchanted weapon's charge: Skyrim's bar, as a share)
				snprintf(lines[k++], sizeof(lines[0]), "CHARGE %d%%", (int)(left * 100.0f + 0.5f));
			for (int j = 0; j < 4; j++)
				v.card_lines[j] = j < k ? lines[j] : NULL;
			items_describe(it, text, sizeof(text));
			v.card_title = items_name(it), v.card_text = text;
		}
		preview_set(it >= 0 ? items_model(it) : -1);
		float charge_left = 1.0f;
		v.hints = preview_inspecting() ? "Stick / D-pad: turn it   O Back"
		          : inv_focus == 1 ? (it >= 0 && items_usable(it) ? "X Use  /\\ Drop  SELECT Drop all  O Back"
		                         : it >= 0 && items_type(it) == IT_BOOK ? "X Read  /\\ Drop  SELECT Drop all  O Back"
		                         : it >= 0 && items_is_poison(it) ? "X Apply to a weapon  /\\ Drop  O Back"
		                         : it >= 0 && items_type(it) == IT_WEAP && items_charge(it, &charge_left) && charge_left < 0.999f
		                           ? "X Right hand  [] Left hand  /\\ Drop  SELECT Recharge  O Back"
		                         : it >= 0 && (items_type(it) == IT_WEAP || items_scroll(it)) ? "X Right hand  [] Left hand  /\\ Drop  O Back"
		                         : it >= 0 && equip_wearable(it) ? (equip_worn(it) ? "X Unequip  /\\ Drop  SELECT Drop all  O Back"
		                                                                             : "X Equip  /\\ Drop  SELECT Drop all  O Back")
		                         : it >= 0 && items_model(it) >= 0 ? "[] Inspect  /\\ Drop one  SELECT Drop all  O Back"
		                         : it >= 0 ? "/\\ Drop one  SELECT Drop all  O Back  L/R Category" : "O Back    L/R Category")
		                         : "X Select    O Back    L/R Category";
		snprintf(status, sizeof(status), "Carry Weight %.0f/%.0f    Gold %d", carried_now, carry_max, items_gold());
		v.status = status;
		ui_menu(NULL, NULL, 0, 0);
		ui_inventory(&v);
		return;
	}
	ui_inventory(NULL);
	preview_place(0);
	preview_set(-1);
	if (page == PAGE_CONTAINER || page == PAGE_POCKETS) { // (the highlighted item's 3D view, beside the list)
		const int row = page == PAGE_CONTAINER && !cont_store ? selected - 1 : selected;
		const int it = page == PAGE_CONTAINER && cont_store ? (selected < store_rows() ? store_list[selected] : -1)
		             : row >= 0 && row < cont_n ? cont_items[row] : -1;
		preview_place(1);
		preview_set(it >= 0 ? items_model(it) : -1);
	}
	if (page == PAGE_CRAFT) {
		static char rows[1201][96];
		static const char *row_items[1201];
		const int n = craft_rows();
		const int tempering = craft_station == 5 || craft_station == 6;
		for (int i = 0; i < n; i++) {
			int made, count, nparts, nperks;
			const int *parts, *need;
			const uint32_t *perks;
			items_recipe(tempering ? temper_recipe[i] : craft_list[i], &made, &count, &parts, &need, &nparts, &perks, &nperks);
			if (tempering) {
				int ok = 1;
				for (int k = 0; k < nparts; k++)
					ok &= items_have(parts[k]) >= need[k];
				const int to = temper_target(temper_recipe[i], temper_item[i]);
				ok &= to > items_temper_level(temper_item[i]);
				snprintf(rows[i], sizeof(rows[0]), "%s%.30s|%d %.20s", ok ? "" : "- ", items_name(temper_item[i]),
				         nparts ? need[0] : 0, nparts ? items_name(parts[0]) : "");
				row_items[i] = rows[i];
				continue;
			}
			char what[48]; // (its main part, and how many more: the rest fit no column)
			if (nparts > 1)
				snprintf(what, sizeof(what), "%d %.14s +%d", need[0], items_name(parts[0]), nparts - 1);
			else
				snprintf(what, sizeof(what), "%d %.17s", need[0], items_name(parts[0]));
			if (count > 1)
				snprintf(rows[i], sizeof(rows[0]), "%s%.18s (%d)|%s", can_make(craft_list[i]) ? "" : "- ",
				         items_name(made), count, what);
			else
				snprintf(rows[i], sizeof(rows[0]), "%s%.22s|%s", can_make(craft_list[i]) ? "" : "- ",
				         items_name(made), what);
			row_items[i] = rows[i];
		}
		snprintf(rows[n], sizeof(rows[0]), n ? "Done" : tempering ? "(nothing you carry to improve)|Done"
		                                                          : "(nothing you can make here)|Done");
		row_items[n] = rows[n];
		show_list(STATION_NAMES[craft_station], row_items, n + 1);
		return;
	}
	if (page == PAGE_ENCHANT) {
		static char rows[CARRIED_MAX + 4][96];
		static const char *row_items[CARRIED_MAX + 4];
		if (ench_step == ENCH_TOP) {
			static const char *const top[3] = {"Disenchant|learn its magic", "Enchant|with a soul gem", "Done"};
			show_list(T_FURN_000BAD0D_FULL0, top, 3);
			return;
		}
		const int n = ench_rows();
		for (int i = 0; i < n; i++) {
			if (ench_step == ENCH_EFFECT) {
				int mgef;
				float mag, secs;
				items_learned(ench_list[i], &mgef, &mag, &secs);
				const MagicEffect *m = items_magic_effect(mgef);
				snprintf(rows[i], sizeof(rows[0]), "%.40s|up to %.0f", m ? m->name : "?", mag);
			} else {
				snprintf(rows[i], sizeof(rows[0]), "%.60s", items_name(ench_list[i]));
			}
			row_items[i] = rows[i];
		}
		snprintf(rows[n], sizeof(rows[0]), n ? "Back" : ench_step == ENCH_GEM ? "(no filled soul gems)|Back"
		                                     : ench_step == ENCH_EFFECT ? "(none learned for this)|Back" : "(nothing)|Back");
		row_items[n] = rows[n];
		static const char *const titles[5] = {"", "Disenchant", "Enchant: which?", "Enchant: with what?", "Enchant: which soul gem?"};
		const char *ench_title = ench_step == ENCH_EFFECT && ench_second ? "Enchant: a second? (the first again: none)" : titles[ench_step];
		show_list(ench_title, row_items, n + 1);
		return;
	}
	if (page == PAGE_KEYS) { // (Options: Button mapping - each action's button; left / right changes it)
		static char rows[ACT_COUNT + 2][72];
		static const char *row_items[ACT_COUNT + 2];
		for (int a = 0; a < ACT_COUNT; a++) {
			snprintf(rows[a], sizeof(rows[0]), "%s|< %s >", ACTION_NAME[a], KEY_NAME[settings.bind[a]]);
			row_items[a] = rows[a];
		}
		snprintf(rows[ACT_COUNT], sizeof(rows[0]), "Reset to the Controls layout|X");
		snprintf(rows[ACT_COUNT + 1], sizeof(rows[0]), "Back");
		row_items[ACT_COUNT] = rows[ACT_COUNT], row_items[ACT_COUNT + 1] = rows[ACT_COUNT + 1];
		show_list("Button mapping: left / right to change", row_items, ACT_COUNT + 2);
		ui_menu_note("A button taken by another action swaps with it. START is always the menu.");
		return;
	}
	if (page == PAGE_FAVORITES) { // (D-pad left / right held: Skyrim's favourites - X right hand or the voice, [] left hand)
		static char rows[FAV_MAX + 1][72];
		static const char *row_items[FAV_MAX + 1];
		for (int r = 0; r < fav_rows; r++) {
			const int id = fav_row_id[r];
			if (fav_row_kind[r] == FAV_SPELL) {
				const int l = magic_hand(HAND_LEFT) == id, rh = magic_hand(HAND_RIGHT) == id;
				snprintf(rows[r], sizeof(rows[0]), "%s|%s", magic_name(id), l && rh ? "Both hands" : l ? "Left hand" : rh ? "Right hand" : "");
			} else if (id == FAV_ROW_POWER) {
				snprintf(rows[r], sizeof(rows[0]), "%s|%s", races_power_name(), shouts_equipped() < 0 ? "Voice" : "");
			} else {
				snprintf(rows[r], sizeof(rows[0]), "%s|%s", shouts_name(id), shouts_equipped() == id ? "Voice" : "");
			}
			row_items[r] = rows[r];
		}
		snprintf(rows[fav_rows], sizeof(rows[0]), fav_rows ? "Back" : "No favorites: [] in the Magic menu marks them");
		row_items[fav_rows] = rows[fav_rows];
		show_list("Favorites: X right hand / voice, [] left hand", row_items, fav_rows + 1);
		return;
	}
	if (page == PAGE_QUESTS) { // the journal: each quest begun, what to do next
		// (a quest's name and how it stands on one row, what to do next on the row under it: on one
		// row a long objective ran over the name)
		static char rows[QUEST_COUNT * 2 + 1][128];
		static const char *row_items[QUEST_COUNT * 2 + 1];
		const char *names[QUEST_COUNT], *objectives[QUEST_COUNT];
		int done[QUEST_COUNT];
		const int n = quests_journal(names, objectives, done, QUEST_COUNT);
		int k = 0;
		journal_rows = 0;
		for (int i = 0; i < n; i++) {
			// (Skyrim's journal: X sets a quest active or inactive - only the active ones' markers shown)
			snprintf(rows[k], sizeof(rows[0]), "%s|%s", names[i], done[i] ? T_GMST_00017026_DATA0 : quests_is_active(names[i]) ? "Active" : "Inactive");
			if (journal_rows < JOURNAL_ROWS && !done[i])
				snprintf(journal_quest[journal_rows], sizeof(journal_quest[0]), "%s", names[i]), journal_row[journal_rows++] = k;
			row_items[k] = rows[k], k++;
			if (!done[i] && objectives[i] && objectives[i][0]) {
				snprintf(rows[k], sizeof(rows[0]), "- %s", objectives[i]);
				row_items[k] = rows[k], k++;
			}
		}
		snprintf(rows[k], sizeof(rows[0]), "Back");
		row_items[k] = rows[k];
		journal_back = k;
		show_list("Quests: X active / inactive", row_items, k + 1);
		return;
	}
	if (page == PAGE_DIALOGUE) {
		static const char *row_items[DIALOGUE_MAX];
		for (int i = 0; i < dlg_n; i++)
			row_items[i] = dlg_opts[i];
		show_list(dlg_title, row_items, dlg_n);
		return;
	}
	if (page == PAGE_BARTER) {
		static char rows[CARRIED_MAX + 2][64];
		static const char *row_items[CARRIED_MAX + 2];
		const int n = barter_rows(), speech = skills_state()->level[SK_SPEECH];
		for (int i = 0; i < n; i++) {
			const int it = barter_items[i], have = barter_counts[i];
			const int price = barter_sell ? sell_price(it, speech) : buy_price(it, speech);
			if (have > 1)
				snprintf(rows[i], sizeof(rows[0]), "%.36s (%d)|%d gold", items_name(it), have, price);
			else
				snprintf(rows[i], sizeof(rows[0]), "%.36s|%d gold", items_name(it), price);
			row_items[i] = rows[i];
		}
		snprintf(rows[n], sizeof(rows[0]), n ? "Done" : barter_sell ? "(nothing they'd buy)|Done" : "(sold out)|Done");
		row_items[n] = rows[n];
		static char title[128];
		snprintf(title, sizeof(title), "%s %.28s   You %d, %.20s %d gold   R/\\: %s", barter_sell ? "Sell to" : "Buy from",
		         barter_name, items_gold(), barter_name, barter_gold(), barter_sell ? "buy" : "sell");
		show_list(title, row_items, n + 1);
		return;
	}
	if (page == PAGE_POCKETS) {
		static char rows[CONTAINER_LIST + 1][64];
		static const char *row_items[65];
		cont_n = pocket_items();
		for (int i = 0; i < cont_n; i++) {
			char name[56];
			snprintf(name, sizeof(name), cont_counts[i] > 1 ? "%s (%d)" : "%s", items_name(cont_items[i]), cont_counts[i]);
			snprintf(rows[i], sizeof(rows[0]), "%.44s|%d%%", name,
			         pickpocket_chance(cont_items[i], cont_counts[i]));
		}
		const int plant = plant_poison();
		if (plant >= 0) // (Poisoned: one of yours slipped in)
			snprintf(rows[cont_n++], sizeof(rows[0]), "Plant %.36s|poison", items_name(plant));
		snprintf(rows[cont_n], sizeof(rows[0]), cont_n ? "Leave" : "(empty pockets)|Leave");
		for (int i = 0; i <= cont_n; i++)
			row_items[i] = rows[i];
		show_list(container_title, row_items, cont_n + 1);
		return;
	}
	if (page == PAGE_CONTAINER && cont_store) {
		static char rows[CARRIED_MAX + 1][64];
		static const char *row_items[CARRIED_MAX + 1];
		const int n = store_rows();
		for (int i = 0; i < n; i++) {
			const int it = store_list[i], have = items_have(it);
			if (have > 1)
				snprintf(rows[i], sizeof(rows[0]), "%.40s (%d)|%s", items_name(it), have, equip_worn(it) ? "Worn" : "");
			else
				snprintf(rows[i], sizeof(rows[0]), "%.40s|%s", items_name(it), equip_worn(it) ? "Worn" : "");
			row_items[i] = rows[i];
		}
		snprintf(rows[n], sizeof(rows[0]), "Close");
		row_items[n] = rows[n];
		static char title[80];
		snprintf(title, sizeof(title), "Store in %.40s: X all, [] one", container_title);
		show_list(title, row_items, n + 1);
		return;
	}
	if (page == PAGE_CONTAINER) {
		static char rows[CONTAINER_LIST + 2][64];
		static const char *row_items[66];
		const int n = container_rows();
		snprintf(rows[0], sizeof(rows[0]), n > 1 ? "Take all|TRIANGLE   L/R: store" : "(empty)|L/R: store");
		for (int i = 0; i < cont_n; i++) {
			char name[56];
			snprintf(name, sizeof(name), cont_counts[i] > 1 ? "%s (%d)" : "%s", items_name(cont_items[i]), cont_counts[i]);
			snprintf(rows[1 + i], sizeof(rows[0]), "%.44s|%d gold", name, items_value(cont_items[i]));
		}
		snprintf(rows[n], sizeof(rows[0]), "Close");
		for (int i = 0; i <= n; i++)
			row_items[i] = rows[i];
		show_list(container_title, row_items, n + 1);
		return;
	}
	snprintf(lines[OPT_MUSIC], 64, "Music volume|< %d >", settings.music_volume);
	snprintf(lines[OPT_VOICE], 64, "Voice volume|< %d >", settings.voice_volume);
	snprintf(lines[OPT_SUBTITLES], 64, "Subtitles|< %s >", settings.subtitles ? "On" : "Off");
	snprintf(lines[OPT_STAIRS], 64, "Stair bounce|< %s >", BOUNCE[settings.stair_bounce]);
	snprintf(lines[OPT_LOOK], 64, "Look speed|< %d >", settings.look_speed);
	snprintf(lines[OPT_INVERT], 64, "Invert look|< %s >", settings.invert_look ? "On" : "Off");
	snprintf(lines[OPT_POST], 64, "Bloom and colour grade|< %s >", settings.post ? "On" : "Off");
	// (2x MSAA: the Vita's own anti-aliasing, set as the graphics start - so from the next start of the game)
	snprintf(lines[OPT_FXAA], 64, "Smooth edges (2x MSAA)|< %s >", settings.msaa == msaa_running ? (settings.msaa ? "On" : "Off")
	                                                              : settings.msaa ? "On after a restart" : "Off after a restart");
	snprintf(lines[OPT_FSR], 64, "Upscaler|< %s >", UPSCALER_NAMES[settings.upscaler]);
	snprintf(lines[OPT_MOTION], 64, "Motion blur when turning|< %s >", settings.motion_blur ? "On" : "Off");
	snprintf(lines[OPT_DIFFICULTY], 64, "Difficulty|< %s >", DIFFICULTY[settings.difficulty]);
	snprintf(lines[OPT_PERFORMANCE], 64, "Detail & Draw Distance|< %s >", PERFORMANCE[settings.performance]);
	snprintf(lines[OPT_FRAME_RATE], 64, "Frame rate|< %s >", FRAME_RATES[settings.frame_rate]);
	snprintf(lines[OPT_RESOLUTION], 64, "Resolution|< %s >", RESOLUTION_NAMES[settings.resolution]);
	snprintf(lines[OPT_DYNRES], 64, "Dynamic resolution|< %s >", settings.dynres ? "On" : "Off");
	snprintf(lines[OPT_MEASURE], 64, "Measure this place (15 s)|X");
	snprintf(lines[OPT_CONTROLS], 64, "Controls|< %s >", settings.controls == CONTROLS_SHOULDERS ? "Shoulders (R/L hands)"
	                                                                                        : "Face buttons");
	snprintf(lines[OPT_REAR_TOUCH], 64, "Rear touchpad as L / R|< %s >", settings.rear_touch ? "On" : "Off");
	snprintf(lines[OPT_KEYS], 64, "Button mapping|X");
	snprintf(lines[OPT_COMPASS], 64, "Compass|< %s >", settings.compass ? "On" : "Off");
	snprintf(lines[OPT_FOV_FIRST], 64, "Field of view, first person|< %d >", settings.fov_first);
	snprintf(lines[OPT_FOV_THIRD], 64, "Field of view, third person|< %d >", settings.fov_third);
	snprintf(lines[OPT_CROSSHAIR], 64, "Crosshair|< %s >", settings.hud_crosshair ? "On" : "Off");
	snprintf(lines[OPT_MARKERS], 64, "Quest markers on the compass|< %s >", settings.hud_markers ? "On" : "Off");
	snprintf(lines[OPT_BAR_FADE], 64, "Bars fade when full|< %s >", settings.hud_bar_fade ? "On" : "Off");
	snprintf(lines[OPT_READOUT], 64, "Performance readout|< %s >", settings.readout ? "On" : "Off");
	snprintf(lines[OPT_BACK], 64, "Back");
	for (int i = 0; i < OPT_COUNT; i++)
		items[i] = lines[i];
	show_list("Options", items, OPT_COUNT); // (a window of them: more than fit)
}

// Left/right on an option (dir -1/+1; CROSS cycles forward)
static void change(int dir) {
	switch (selected) {
	case OPT_MUSIC: settings.music_volume = clampi(settings.music_volume + dir, 0, 10); break;
	case OPT_VOICE: settings.voice_volume = clampi(settings.voice_volume + dir, 0, 10); break;
	case OPT_SUBTITLES: settings.subtitles = !settings.subtitles; break;
	case OPT_STAIRS: settings.stair_bounce = (settings.stair_bounce + dir + 4) % 4; break;
	case OPT_LOOK: settings.look_speed = clampi(settings.look_speed + dir, 1, 10); break;
	case OPT_INVERT: settings.invert_look = !settings.invert_look; break;
	case OPT_POST: settings.post = !settings.post; break;
	case OPT_FXAA: settings.msaa = !settings.msaa; break;
	case OPT_FSR: settings.upscaler = !settings.upscaler; break;
	case OPT_MOTION: settings.motion_blur = !settings.motion_blur; break;
	case OPT_DIFFICULTY: settings.difficulty = (settings.difficulty + dir + 6) % 6; break;
	case OPT_PERFORMANCE: settings.performance = (settings.performance + dir + PERF_LEVELS) % PERF_LEVELS; break;
	case OPT_FRAME_RATE: settings.frame_rate = (settings.frame_rate + dir + 3) % 3; break;
	case OPT_RESOLUTION: { // (in size order: 424, 480, 544 x 304, 544 x 352, 640 x 352, 640 x 368, 720)
		static const int ORDER[RES_COUNT] = {RES_424, RES_480, RES_544, RES_544_352, RES_640_352, RES_640, RES_720};
		int at = 0;
		while (at < RES_COUNT && ORDER[at] != settings.resolution)
			at++;
		settings.resolution = ORDER[(at + dir + RES_COUNT) % RES_COUNT];
		break;
	}
	case OPT_FOV_FIRST: settings.fov_first = clampi(settings.fov_first + dir * 5, 50, 90); break;
	case OPT_FOV_THIRD: settings.fov_third = clampi(settings.fov_third + dir * 5, 50, 90); break;
	case OPT_CROSSHAIR: settings.hud_crosshair = !settings.hud_crosshair; break;
	case OPT_MARKERS: settings.hud_markers = !settings.hud_markers; break;
	case OPT_BAR_FADE: settings.hud_bar_fade = !settings.hud_bar_fade; break;
	case OPT_READOUT: settings.readout = !settings.readout; break;
	case OPT_DYNRES: settings.dynres = !settings.dynres; break;
	case OPT_CONTROLS: settings.controls = !settings.controls, binds_preset(settings.controls); break; // (the layout's buttons)
	case OPT_REAR_TOUCH: settings.rear_touch = !settings.rear_touch; break;
	case OPT_COMPASS: settings.compass = !settings.compass; break;
	default: return;
	}
	apply();
}

static void close_menu(void) {
	if (open)
		save();
	open = 0;
	items_set_theft(-1, 0); // (no one's container open now)
	ui_menu(NULL, NULL, 0, 0);
	ui_inventory(NULL);
	ui_save_list(NULL);
	ui_perk_tree(NULL);
	ui_map(NULL);
	preview_set(-1);
}

// The save / load pages: a save's row and its details
static void save_row(const SaveInfo *si, char *out, size_t n) {
	snprintf(out, n, "%s%s, level %d", si->autosave ? "Autosave: " : "", si->place[0] ? si->place : worlds_title(0), si->level);
}

static void show_saves(void) {
	static char rows[SAVE_SLOTS + 1][80], lines[4][96];
	static const char *row_ptrs[SAVE_SLOTS + 1];
	const int saving = page == PAGE_SAVE, extra = saving ? 1 : 0;
	int n = 0;
	if (saving)
		snprintf(rows[n], sizeof(rows[0]), "New save"), row_ptrs[n] = rows[n], n++;
	for (int i = 0; i < save_count; i++, n++)
		save_row(&save_infos[i], rows[n], sizeof(rows[0])), row_ptrs[n] = rows[n];
	if (!n)
		snprintf(rows[0], sizeof(rows[0]), "No saved games"), row_ptrs[0] = rows[0], n = 1;
	UiSaveList view = {0};
	view.title = saving ? "Save game" : "Load game";
	view.rows = row_ptrs, view.count = n, view.selected = selected;
	const int k = selected - extra;
	const SaveInfo *si = k >= 0 && k < save_count ? &save_infos[k] : NULL;
	if (si) {
		const int h = (int)si->hours, m = (int)((si->hours - h) * 60.0f);
		snprintf(lines[0], sizeof(lines[0]), "%s", si->place[0] ? si->place : worlds_title(0));
		snprintf(lines[1], sizeof(lines[1]), "%s%slevel %d", si->race, si->race[0] ? ", " : "", si->level);
		snprintf(lines[2], sizeof(lines[2]), "%02d:%02d in the game%s", h % 24, m, si->autosave ? "  (autosave)" : "");
		snprintf(lines[3], sizeof(lines[3]), "Saved %s", si->date[0] ? si->date : "");
		for (int i = 0; i < 4; i++)
			view.lines[i] = lines[i];
	} else if (saving) {
		view.lines[0] = "A new save";
		view.lines[1] = "(the save you pick from the list is replaced)";
	}
	view.hints = saving ? "X save   O back" : "X load   O back";
	const int slot = si ? si->slot : -1;
	if (slot != picture_for) {
		static uint16_t picture[SAVE_THUMB_W * SAVE_THUMB_H];
		ui_set_save_picture(slot >= 0 && save_read_thumb(slot, picture) ? picture : NULL, SAVE_THUMB_W, SAVE_THUMB_H);
		picture_for = slot;
	}
	ui_save_list(&view);
}

void menu_set_camera(int third_person) {
	settings.third_person = !!third_person;
}

void menu_toggle_camera(void) {
	settings.third_person = !settings.third_person;
	save();
}

void menu_set_local(float x, float y, float z, float yaw_deg, const char *place) {
	local_at[0] = x, local_at[1] = y, local_at[2] = z, local_at[3] = yaw_deg;
	snprintf(local_name, sizeof(local_name), "%s", place ? place : "");
	local_known = place != NULL;
}

void menu_set_quest_markers(int n, const float (*at)[2]) {
	quest_on = n < UI_MAP_QUESTS ? n : UI_MAP_QUESTS;
	for (int q = 0; q < quest_on; q++)
		quest_at[q][0] = at[q][0], quest_at[q][1] = at[q][1];
}

void menu_set_player(float x, float z, float yaw_deg, int outdoors) {
	player_at[0] = x, player_at[1] = z, player_at[2] = yaw_deg, player_outdoors = outdoors;
}

int menu_take_drop(int *count) {
	const int it = drop_item;
	*count = drop_count;
	drop_item = -1;
	return it;
}

int menu_take_level_choice(void) {
	int c = level_choice;
	level_choice = 0;
	return c;
}

int menu_take_request(int *slot) {
	int r = request;
	request = 0;
	if (slot)
		*slot = request_slot;
	return r;
}

void menu_set_carry_max(float weight, float carried) {
	carry_max = weight, carried_now = carried;
}

int menu_take_use(void) {
	int u = use_pending;
	use_pending = -1;
	return u;
}

int menu_take_wait(int *was_sleep) {
	int h = wait_pending;
	wait_pending = 0;
	if (was_sleep)
		*was_sleep = h && slept;
	return h;
}

static int open_page(int p) {
	open = 1, page = p, selected = 0;
	sleeping = 0;
	show();
	return MENU_PAUSED;
}

void menu_open_load(void) {
	open_page(PAGE_LOAD);
	save_count = save_list(save_infos, SAVE_SLOTS);
	selected = 0, picture_for = -2;
	show();
}

void menu_open_sleep(void) {
	open_page(PAGE_WAIT);
	sleeping = 1, wait_hours = wait_hours < 8 ? 8 : wait_hours;
	show();
}

void menu_open_pockets(int handle, int npc, const char *name) {
	container_handle = handle, pocket_npc = npc;
	items_set_theft(handle, 2); // (what you take is stolen; unnoticed, not reported)
	snprintf(container_title, sizeof(container_title), "Pickpocket: %s", name);
	open_page(PAGE_POCKETS);
}

int menu_take_caught(void) {
	int c = caught;
	caught = -1;
	return c;
}

void menu_open_dialogue(const char *title, const char *const *options, int n) {
	snprintf(dlg_title, sizeof(dlg_title), "%s", title ? title : "");
	dlg_n = n < DIALOGUE_MAX ? n : DIALOGUE_MAX;
	for (int i = 0; i < dlg_n; i++)
		snprintf(dlg_opts[i], sizeof(dlg_opts[0]), "%s", options[i]);
	dlg_choice = -1;
	open_page(PAGE_DIALOGUE);
}

int menu_take_dialogue(void) {
	const int c = dlg_choice;
	dlg_choice = -1;
	return c;
}

void menu_open_barter(int vendor, const char *name) {
	barter_vendor = vendor, barter_sell = 0;
	container_handle = items_vendor_stock(vendor);
	snprintf(barter_name, sizeof(barter_name), "%s", name ? name : "Merchant");
	game_log("barter: %s has %d gold", barter_name, barter_gold());
	open_page(PAGE_BARTER);
}

void menu_open_container(int handle, const char *name) {
	container_handle = handle, cont_store = 0;
	snprintf(container_title, sizeof(container_title), "%s", name && name[0] ? name : "Container");
	open_page(PAGE_CONTAINER);
}

int menu_update(const SceCtrlData *pad) {
	unsigned buttons = pad->buttons;
	// The left stick works like the d-pad here
	if (pad->ly < 50) buttons |= SCE_CTRL_UP;
	if (pad->ly > 206) buttons |= SCE_CTRL_DOWN;
	if (pad->lx < 50) buttons |= SCE_CTRL_LEFT;
	if (pad->lx > 206) buttons |= SCE_CTRL_RIGHT;
	unsigned pressed = buttons & ~prev_buttons;
	const unsigned dirs = SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT;
	// Held directions repeat
	if (buttons & dirs && (buttons & dirs) == (prev_buttons & dirs)) {
		if (++held_frames >= REPEAT_DELAY && (held_frames - REPEAT_DELAY) % REPEAT_EVERY == 0)
			pressed |= buttons & dirs;
	} else {
		held_frames = 0;
	}
	prev_buttons = buttons;

	if (!open) {
		if (pressed & SCE_CTRL_START) {
			sfx_play("ui_ok", 0.6f, 0.0f);
			return open_page(PAGE_MAIN);
		}
		// (SELECT is the power / shout button now; Wait is in the pause menu)
		return MENU_PLAYING;
	}
	// The menus' sounds: moving, choosing, going back
	if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))
		sfx_play("ui_focus", 0.35f, 0.0f);
	else if (pressed & SCE_CTRL_CROSS)
		sfx_play("ui_ok", 0.5f, 0.0f);
	else if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START))
		sfx_play("ui_cancel", 0.5f, 0.0f);
	if (inv_dirty && page == PAGE_INVENTORY && use_pending < 0 && drop_item < 0) { // (one was eaten: the list again)
		inv_dirty = 0;
		show();
	}
	if (poison_item >= 0) { // the poison's weapon: up / down, X puts it on, CIRCLE doesn't
		if (pressed & SCE_CTRL_UP)
			poison_sel = (poison_sel + poison_n) % (poison_n + 1);
		if (pressed & SCE_CTRL_DOWN)
			poison_sel = (poison_sel + 1) % (poison_n + 1);
		if ((pressed & SCE_CTRL_CROSS) && poison_sel < poison_n) {
			const int slot = poison_slots[poison_sel];
			char text[96];
			snprintf(text, sizeof(text), "%s poisoned", equip_poison_target(slot));
			if (equip_apply_poison(slot, poison_item)) {
				ui_notice(text, 2.5f);
				sfx_play("itm_poisonuse", 0.8f, 0.0f);
			}
			poison_item = -1, inv_dirty = 1;
		} else if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_START)) {
			poison_item = -1;
		}
		show();
		return MENU_PAUSED;
	}
	if (book_item >= 0) { // a book open: L / R (or left / right) turn its pages, CIRCLE puts it down
		if (pressed & (SCE_CTRL_RTRIGGER | SCE_CTRL_RIGHT))
			book_page++, sfx_play("ui_focus", 0.35f, 0.0f);
		if (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_LEFT))
			book_page = book_page > 0 ? book_page - 1 : 0;
		if (book_page >= book_pages)
			book_page = book_pages - 1;
		ui_book_page(book_page);
		if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START | SCE_CTRL_CROSS)) {
			ui_book_close();
			book_item = -1;
			show();
		}
		return MENU_PAUSED;
	}
	if (pressed & SCE_CTRL_START) {
		close_menu();
		return MENU_PLAYING;
	}
	if (page == PAGE_ALCHEMY) {
		alchemy_lists();
		const int n = alch_row_n;
		if (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
			alch_cat = (alch_cat + (pressed & SCE_CTRL_LTRIGGER ? alch_cat_n - 1 : 1)) % alch_cat_n;
			alch_sel = 0;
		} else if (alch_focus == 0) {
			if (pressed & SCE_CTRL_UP)
				alch_cat = (alch_cat + alch_cat_n - 1) % alch_cat_n, alch_sel = 0;
			if (pressed & SCE_CTRL_DOWN)
				alch_cat = (alch_cat + 1) % alch_cat_n, alch_sel = 0;
			if ((pressed & (SCE_CTRL_RIGHT | SCE_CTRL_CROSS)) && n)
				alch_focus = 1;
			if (pressed & SCE_CTRL_CIRCLE) {
				close_menu();
				return MENU_PLAYING;
			}
		} else {
			if ((pressed & SCE_CTRL_UP) && n)
				alch_sel = (alch_sel + n - 1) % n;
			if ((pressed & SCE_CTRL_DOWN) && n)
				alch_sel = (alch_sel + 1) % n;
			if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_CIRCLE))
				alch_focus = 0;
			if ((pressed & SCE_CTRL_CROSS) && alch_sel < n) { // (picked / put back; three at most)
				const int it = alch_rows[alch_sel];
				int had = -1;
				for (int k = 0; k < alch_picked; k++)
					if (alch_pick[k] == it)
						had = k;
				if (had >= 0)
					alch_pick[had] = alch_pick[--alch_picked];
				else if (alch_picked < 3)
					alch_pick[alch_picked++] = it;
				else
					ui_notice("You can only combine three ingredients", 2.0f);
			}
			if ((pressed & SCE_CTRL_SQUARE) && alch_picked >= 2) {
				int knew = 0;
				for (int k = 0; k < alch_picked; k++)
					knew += __builtin_popcount(items_known(alch_pick[k]));
				const int made = alchemy_make(alch_pick, alch_picked);
				int learned = -knew;
				for (int k = 0; k < alch_picked; k++)
					learned += __builtin_popcount(items_known(alch_pick[k]));
				if (made >= 0) {
					sfx_play(items_is_poison(made) ? "alchemy_poison" : "alchemy_potion", 0.8f, 0.0f);
					if (learned > 0)
						sfx_play("alchemy_learn", 0.7f, 0.0f);
					char text[96];
					snprintf(text, sizeof(text), "%s created", items_name(made));
					ui_notice(text, 2.5f);
					// (still enough of each for another: they stay picked, as in Skyrim; else not)
					for (int k = 0; k < alch_picked; k++)
						if (items_have(alch_pick[k]) <= 0) {
							alch_picked = 0;
							break;
						}
				} else {
					sfx_play("alchemy_fail", 0.8f, 0.0f);
					ui_notice("These ingredients have no effect in common", 2.0f);
				}
			}
		}
		show();
		return MENU_PAUSED;
	}
	// The inventory (Skyrim's): up / down in the focused column, left / right between the
	// categories and the items, L / R the category, CROSS uses / equips, CIRCLE steps back
	if (page == PAGE_INVENTORY) {
		const int n = inventory_rows();
		if (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
			inv_cat = (inv_cat + (pressed & SCE_CTRL_LTRIGGER ? CAT_COUNT - 1 : 1)) % CAT_COUNT;
			inv_sel = 0;
		} else if (inv_focus == 0) {
			if (pressed & SCE_CTRL_UP)
				inv_cat = (inv_cat + CAT_COUNT - 1) % CAT_COUNT, inv_sel = 0;
			if (pressed & SCE_CTRL_DOWN)
				inv_cat = (inv_cat + 1) % CAT_COUNT, inv_sel = 0;
			if ((pressed & (SCE_CTRL_RIGHT | SCE_CTRL_CROSS)) && n)
				inv_focus = 1;
			if (pressed & SCE_CTRL_CIRCLE) {
				page = PAGE_MAIN, selected = MAIN_INVENTORY;
				show();
				return MENU_PAUSED;
			}
		} else if (preview_inspecting()) { // Looking closely at it: the stick / D-pad turns it; O or [] back
			preview_turn(((buttons & SCE_CTRL_RIGHT) ? 0.06f : 0.0f) - ((buttons & SCE_CTRL_LEFT) ? 0.06f : 0.0f),
			             ((buttons & SCE_CTRL_DOWN) ? 0.04f : 0.0f) - ((buttons & SCE_CTRL_UP) ? 0.04f : 0.0f));
			if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE))
				preview_inspect(0);
			show();
			return MENU_PAUSED;
		} else {
			if ((pressed & SCE_CTRL_UP) && n)
				inv_sel = (inv_sel + n - 1) % n;
			if ((pressed & SCE_CTRL_DOWN) && n)
				inv_sel = (inv_sel + 1) % n;
			if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_CIRCLE))
				inv_focus = 0;
			if ((pressed & SCE_CTRL_CROSS) && inv_sel < n) {
				const int it = carried_list[inv_sel];
				if (items_is_poison(it)) { // (a poison: onto a weapon, not drunk)
					choose_poison(it);
					show();
					return MENU_PAUSED;
				}
				if (items_usable(it)) {
					use_pending = it; // (main eats / drinks it; the menu stays open: eat another, or leave)
					inv_dirty = 1;
					show();
					return MENU_PAUSED;
				}
				if (items_type(it) == IT_BOOK)
					read_book(it);
				else if (items_scroll(it)) // (a scroll: into the right hand, read like a spell)
					hand_scroll(HAND_RIGHT, it);
				else if (equip_wearable(it))
					equip_toggle(it);
				else if (items_type(it) == IT_ARMO || items_type(it) == IT_WEAP)
					ui_notice("Can't be equipped", 2.0f);
				else if (items_type(it) == IT_SCRL)
					ui_notice("That scroll can't be read yet", 2.0f);
			}
			// SQUARE: into the left hand (a one-handed weapon: dual wielding; a scroll)
			if ((pressed & SCE_CTRL_SQUARE) && inv_sel < n && items_scroll(carried_list[inv_sel]))
				hand_scroll(HAND_LEFT, carried_list[inv_sel]);
			else if ((pressed & SCE_CTRL_SQUARE) && inv_sel < n && items_type(carried_list[inv_sel]) == IT_WEAP)
				equip_left_toggle(carried_list[inv_sel]);
			else if ((pressed & SCE_CTRL_SQUARE) && inv_sel < n && items_model(carried_list[inv_sel]) >= 0)
				preview_inspect(1); // ([] on anything else: a close look - the Golden Claw's three signs)
			// SELECT on an enchanted weapon not full: recharged from the fullest soul gem carried
			float charge_left;
			if ((pressed & SCE_CTRL_SELECT) && inv_sel < n && items_charge(carried_list[inv_sel], &charge_left) &&
			    charge_left < 0.999f) {
				if (items_recharge(carried_list[inv_sel])) {
					sfx_play("mag_heal_lp", 0.6f, 0.0f);
					ui_notice("Recharged", 2.0f);
				} else {
					ui_notice("You have no filled soul gem", 2.0f);
				}
				pressed &= ~(unsigned)SCE_CTRL_SELECT;
			}
			// TRIANGLE drops one, SELECT the whole stack (main lays it at your feet)
			if ((pressed & (SCE_CTRL_TRIANGLE | SCE_CTRL_SELECT)) && inv_sel < n && drop_item < 0) {
				const int it = carried_list[inv_sel];
				const int all = (pressed & SCE_CTRL_SELECT) != 0;
				if (equip_worn(it) && items_have(it) <= (all ? items_have(it) : 1))
					equip_toggle(it);
				drop_item = it, drop_count = all ? items_have(it) : 1;
				inv_dirty = 1;
				show();
				return MENU_PAUSED;
			}
			if (!inventory_rows())
				inv_focus = 0;
		}
		show();
		return MENU_PAUSED;
	}
	// The map: the left stick or the directions move the view, L / R zoom, CROSS travels to
	// the place under the middle, CIRCLE goes back
	if (page == PAGE_MAP && (pressed & SCE_CTRL_SQUARE) && local_known) { // (local map <-> world map)
		map_local = !map_local;
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_MAP && map_local) {
		const float step = local_metres * 0.012f;
		if (buttons & SCE_CTRL_UP)
			local_centre[1] -= step;
		if (buttons & SCE_CTRL_DOWN)
			local_centre[1] += step;
		if (buttons & SCE_CTRL_LEFT)
			local_centre[0] -= step;
		if (buttons & SCE_CTRL_RIGHT)
			local_centre[0] += step;
		if (pressed & SCE_CTRL_LTRIGGER)
			local_metres = fminf(local_metres * 1.5f, 400.0f);
		if (pressed & SCE_CTRL_RTRIGGER)
			local_metres = fmaxf(local_metres / 1.5f, 15.0f);
		if (pressed & SCE_CTRL_CIRCLE)
			page = PAGE_MAIN, selected = MAIN_MAP;
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_MAP) {
		const float step = map_cells * 0.012f;
		if (buttons & SCE_CTRL_UP)
			map_centre[1] += step;
		if (buttons & SCE_CTRL_DOWN)
			map_centre[1] -= step;
		if (buttons & SCE_CTRL_LEFT)
			map_centre[0] -= step;
		if (buttons & SCE_CTRL_RIGHT)
			map_centre[0] += step;
		if (pressed & SCE_CTRL_LTRIGGER)
			map_cells = fminf(map_cells * 1.6f, 140.0f);
		if (pressed & SCE_CTRL_RTRIGGER)
			map_cells = fmaxf(map_cells / 1.6f, 10.0f);
		map_centre[0] = fmaxf(-64.0f, fminf(64.0f, map_centre[0]));
		map_centre[1] = fmaxf(-64.0f, fminf(64.0f, map_centre[1]));
		if ((pressed & SCE_CTRL_CROSS) && map_sel >= 0) {
			int kind, disc, in_region;
			const char *name;
			float x, z;
			worldmap_marker(map_sel, &kind, &name, &x, &z, &disc, &in_region);
			if (in_region && disc) {
				request = MENU_TRAVEL, request_slot = map_sel;
				close_menu();
				return MENU_PLAYING;
			}
			ui_notice(in_region ? "You haven't discovered it yet" : "That isn't part of this world yet", 2.5f);
		}
		if (pressed & SCE_CTRL_CIRCLE)
			page = PAGE_MAIN, selected = MAIN_MAP;
		show();
		return MENU_PAUSED;
	}
	// A perk tree: the directions move between the stars, L / R to the skills either side,
	// CROSS takes the perk, CIRCLE goes back to the skills
	if (page == PAGE_PERKS) {
		if (pressed & SCE_CTRL_UP)
			perk_node = perk_step(0, 1);
		if (pressed & SCE_CTRL_DOWN)
			perk_node = perk_step(0, -1);
		if (pressed & SCE_CTRL_LEFT)
			perk_node = perk_step(-1, 0);
		if (pressed & SCE_CTRL_RIGHT)
			perk_node = perk_step(1, 0);
		if (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
			perk_skill = (perk_skill + (pressed & SCE_CTRL_LTRIGGER ? SKILL_COUNT - 1 : 1)) % SKILL_COUNT;
			perk_node = perk_first(perk_skill);
		}
		if (pressed & SCE_CTRL_CROSS) {
			char why[64];
			if (perks_take(perk_skill, perk_node)) {
				const int *links, *ranks;
				int nl, nr;
				float x, y;
				perks_node(perk_skill, perk_node, &x, &y, &links, &nl, &ranks, &nr);
				char text[80];
				snprintf(text, sizeof(text), "Perk: %s", perks_name(ranks[perks_node_taken(perk_skill, perk_node) - 1]));
				ui_notice(text, 3.0f);
				sfx_play("ui_perk", 0.7f, 0.0f);
			} else if (!perks_can_take(perk_skill, perk_node, why, sizeof(why)) && why[0]) {
				ui_notice(why, 2.5f);
			}
		}
		if (pressed & SCE_CTRL_CIRCLE)
			page = PAGE_SKILLS, selected = (skills_state()->pending > 0 ? 3 : 0) + perk_skill;
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_CRAFT) { // (CROSS makes one, CIRCLE leaves)
		const int n = craft_rows();
		if (pressed & SCE_CTRL_UP)
			selected = (selected + n) % (n + 1);
		if (pressed & SCE_CTRL_DOWN)
			selected = (selected + 1) % (n + 1);
		if ((pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) || ((pressed & SCE_CTRL_CROSS) && selected >= n)) {
			close_menu();
			return MENU_PLAYING;
		}
		if ((pressed & SCE_CTRL_CROSS) && selected < n && (craft_station == 5 || craft_station == 6)) {
			int made, count, nparts, nperks;
			const int *parts, *need;
			const uint32_t *perks;
			items_recipe(temper_recipe[selected], &made, &count, &parts, &need, &nparts, &perks, &nperks);
			const int it = temper_item[selected], to = temper_target(temper_recipe[selected], temper_item[selected]);
			int ok = to > items_temper_level(it);
			for (int k = 0; k < nparts; k++)
				ok &= items_have(parts[k]) >= need[k];
			const int better = ok ? items_temper(it, to) : -1;
			if (better < 0) {
				ui_notice(to <= items_temper_level(it) ? "You can't improve it further yet" : "You don't have what it takes", 2.0f);
			} else {
				for (int k = 0; k < nparts; k++)
					items_add(parts[k], -need[k]);
				const int worn = equip_worn(it);
				if (worn)
					equip_toggle(it);
				items_add(it, -1), items_add(better, 1);
				if (worn)
					equip_toggle(better);
				char text[96];
				snprintf(text, sizeof(text), "%s made", items_name(better));
				ui_notice(text, 2.0f);
				sfx_play("itm_armor", 0.7f, 0.0f);
				skills_use(SK_SMITHING, 2.0f + items_value(better) * 0.05f);
			}
			show();
			return MENU_PAUSED;
		}
		if ((pressed & SCE_CTRL_CROSS) && selected < n) {
			int made, count, nparts, nperks;
			const int *parts, *need;
			const uint32_t *perks;
			items_recipe(craft_list[selected], &made, &count, &parts, &need, &nparts, &perks, &nperks);
			if (items_craft(craft_list[selected])) {
				char text[96];
				snprintf(text, sizeof(text), count > 1 ? "%s (%d) made" : "%s made", items_name(made), count);
				ui_notice(text, 2.0f);
				sfx_play("itm_armor", 0.7f, 0.0f);
				if (craft_station != 4) // (the forge, smelter, tanning rack train Smithing: by what it's worth)
					skills_use(SK_SMITHING, 1.0f + items_value(made) * count * 0.1f);
			} else {
				ui_notice("You don't have what it takes", 2.0f);
			}
		}
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_ENCHANT) {
		const int n = ench_step == ENCH_TOP ? 2 : ench_rows();
		if (pressed & SCE_CTRL_UP)
			selected = (selected + n) % (n + 1);
		if (pressed & SCE_CTRL_DOWN)
			selected = (selected + 1) % (n + 1);
		const int back = (pressed & SCE_CTRL_CIRCLE) || ((pressed & SCE_CTRL_CROSS) && selected >= n);
		if (back || (pressed & SCE_CTRL_START)) {
			if (ench_step == ENCH_TOP || (pressed & SCE_CTRL_START)) {
				close_menu();
				return MENU_PLAYING;
			}
			if (ench_step == ENCH_EFFECT && ench_second)
				ench_second = 0, selected = 0; // (back from the second: the first again)
			else
				ench_step = ench_step == ENCH_DISENCHANT || ench_step == ENCH_ITEM ? ENCH_TOP : ench_step - 1, selected = 0;
		} else if (pressed & SCE_CTRL_CROSS) {
			const int pick = ench_list[selected];
			if (ench_step == ENCH_TOP) {
				ench_step = selected == 0 ? ENCH_DISENCHANT : ENCH_ITEM, selected = 0;
			} else if (ench_step == ENCH_DISENCHANT) {
				char text[96];
				snprintf(text, sizeof(text), "%s disenchanted", items_name(pick));
				if (items_disenchant(pick)) {
					ui_notice(text, 2.5f);
					skills_use(SK_ENCHANTING, 10.0f + items_value(pick) * 0.05f);
					sfx_play("mag_rune_cast", 0.7f, 0.0f);
				}
			} else if (ench_step == ENCH_ITEM) {
				ench_item = pick, ench_step = ENCH_EFFECT, selected = 0, ench_second = 0, ench_effect2 = -1;
			} else if (ench_step == ENCH_EFFECT && !ench_second && perks_apply(EP_MOD_ENCHANTMENTS_ALLOWED, 1.0f, NULL) >= 2.0f) {
				ench_effect = pick, ench_second = 1, selected = 0; // (Extra Effect: a second to choose)
			} else if (ench_step == ENCH_EFFECT) {
				if (ench_second)
					ench_effect2 = pick == ench_effect ? -1 : pick;
				else
					ench_effect = pick;
				ench_step = ENCH_GEM, selected = 0, ench_second = 0;
			} else if (ench_step == ENCH_GEM) {
				int mgef;
				float mag, secs;
				items_learned(ench_effect, &mgef, &mag, &secs);
				const MagicEffect *m = items_magic_effect(mgef);
				ItemEffect e[2] = {{mgef, ench_magnitude(mag, items_soul(pick)), secs}};
				int ne = 1;
				if (ench_effect2 >= 0) { // (Extra Effect's second)
					int mgef2;
					float mag2, secs2;
					items_learned(ench_effect2, &mgef2, &mag2, &secs2);
					e[1] = (ItemEffect){mgef2, ench_magnitude(mag2, items_soul(pick)), secs2};
					ne = 2;
				}
				char name[48];
				snprintf(name, sizeof(name), "%.22s of %.22s", items_name(ench_item), m ? m->name : "Enchanting");
				const int made = items_create_enchanted(ench_item, name, items_value(ench_item) + (int)((e[0].magnitude + (ne > 1 ? e[1].magnitude : 0.0f)) * 8.0f), e, ne);
				if (made >= 0) {
					items_add(ench_item, -1), items_add(pick, -1), items_add(made, 1); // (the gem's soul spent: gone)
					char text[96];
					snprintf(text, sizeof(text), "%s made", name);
					ui_notice(text, 2.5f);
					skills_use(SK_ENCHANTING, 20.0f + e[0].magnitude);
					sfx_play("mag_rune_cast", 0.8f, 0.0f);
				}
				ench_step = ENCH_TOP, selected = 0;
			}
		}
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_KEYS) {
		list_nav(pressed, ACT_COUNT + 2);
		if ((pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) && selected < ACT_COUNT)
			bind_change(selected, pressed & SCE_CTRL_LEFT ? -1 : 1), apply();
		else if ((pressed & SCE_CTRL_CROSS) && selected < ACT_COUNT)
			bind_change(selected, 1), apply();
		else if ((pressed & SCE_CTRL_CROSS) && selected == ACT_COUNT)
			binds_preset(settings.controls), apply();
		else if ((pressed & SCE_CTRL_CIRCLE) || ((pressed & SCE_CTRL_CROSS) && selected == ACT_COUNT + 1)) {
			save();
			page = PAGE_OPTIONS, selected = OPT_KEYS;
		}
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_FAVORITES) { // (X: the right hand / the voice; []: the left hand; then back to the game)
		list_nav(pressed, fav_rows + 1);
		int chose = 0;
		if ((pressed & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE)) && selected < fav_rows) {
			const int id = fav_row_id[selected], left = (pressed & SCE_CTRL_SQUARE) != 0;
			char text[64];
			if (fav_row_kind[selected] == FAV_SPELL) {
				const int h = left ? HAND_LEFT : HAND_RIGHT;
				magic_set_hand(h, id), magic_set_readied(1);
				snprintf(text, sizeof(text), "%s hand: %s", left ? "Left" : "Right", magic_name(id));
			} else if (id == FAV_ROW_POWER) {
				shouts_equip(-1), beast_ready_it(0);
				snprintf(text, sizeof(text), "%s readied (SELECT)", races_power_name());
			} else {
				shouts_equip(id), beast_ready_it(0);
				snprintf(text, sizeof(text), "%s readied (hold SELECT)", shouts_name(id));
			}
			ui_notice_now(text, left ? "Left hand" : "Right hand", 1.5f);
			sfx_play("ui_ok", 0.6f, 0.0f);
			chose = 1;
		}
		if (chose || (pressed & SCE_CTRL_CIRCLE) || ((pressed & SCE_CTRL_CROSS) && selected >= fav_rows)) {
			close_menu();
			return MENU_PLAYING;
		}
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_QUESTS) { // (X on a quest: active / inactive, as Skyrim's journal; on Back, or O: back)
		list_nav(pressed, journal_back + 1);
		int toggled = 0;
		if (pressed & SCE_CTRL_CROSS)
			for (int r = 0; r < journal_rows; r++)
				if (journal_row[r] == selected || journal_row[r] + 1 == selected) { // (its name's row, or its objective's)
					quests_toggle_active(journal_quest[r]);
					sfx_play("ui_ok", 0.6f, 0.0f);
					toggled = 1;
					break;
				}
		if ((pressed & SCE_CTRL_CIRCLE) || ((pressed & SCE_CTRL_CROSS) && !toggled && selected == journal_back))
			page = PAGE_MAIN, selected = MAIN_QUESTS;
		show();
		return MENU_PAUSED;
	}
	// Talking: CROSS says the chosen line (main.c acts on it), CIRCLE says goodbye
	if (page == PAGE_DIALOGUE) {
		if (pressed & SCE_CTRL_UP)
			selected = (selected + dlg_n - 1) % dlg_n;
		if (pressed & SCE_CTRL_DOWN)
			selected = (selected + 1) % dlg_n;
		if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
			dlg_choice = (pressed & SCE_CTRL_CROSS) ? selected : -2;
			close_menu();
			return MENU_PLAYING;
		}
		show();
		return MENU_PAUSED;
	}
	// Bartering: L / R between buying and selling; CROSS one, SQUARE the whole stack; CIRCLE done
	if (page == PAGE_BARTER) {
		const int n = barter_rows(), speech = skills_state()->level[SK_SPEECH];
		if (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_TRIANGLE)) // (buying / selling: TRIANGLE too - not every L works)
			barter_sell = !barter_sell, selected = 0;
		else if (pressed & SCE_CTRL_UP)
			selected = (selected + n) % (n + 1);
		else if (pressed & SCE_CTRL_DOWN)
			selected = (selected + 1) % (n + 1);
		if ((pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) || ((pressed & SCE_CTRL_CROSS) && selected >= n)) {
			barter_vendor = -1, container_handle = -1;
			close_menu();
			return MENU_PLAYING;
		}
		if ((pressed & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE)) && selected < n) {
			const int it = barter_items[selected];
			int want = (pressed & SCE_CTRL_SQUARE) ? barter_counts[selected] : 1, done = 0, paid = 0;
			for (; done < want; done++) {
				if (barter_sell) {
					const int p = sell_price(it, speech);
					if (barter_gold() < p) {
						ui_notice("They can't afford it", 2.0f);
						break;
					}
					const int had = items_have(it), was_stolen = items_stolen(it);
					items_put(container_handle, it, 1);
					if (items_vendor_fence(barter_vendor) && items_have(it) < had) // (fenced: one less to answer for)
						items_mark_stolen(it, was_stolen - 1 - items_stolen(it));
					items_take(container_handle, items_find_gold(), p); // (their gold to you)
					paid += p;
				} else {
					const int p = buy_price(it, speech);
					if (items_gold() < p) {
						ui_notice("You don't have enough gold", 2.0f);
						break;
					}
					items_take(container_handle, it, 1);
					items_put(container_handle, items_find_gold(), p); // (your gold to them)
					paid += p;
				}
			}
			if (done) { // (Skyrim: Speech trains on the gold that changes hands)
				skills_use(SK_SPEECH, (float)paid);
				sfx_play(barter_sell ? "itm_gold_up" : "itm_up", 0.6f, 0.0f);
			}
			if (selected >= barter_rows())
				selected = barter_rows();
		}
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_POCKETS && (pressed & (SCE_CTRL_START | SCE_CTRL_CIRCLE))) {
		close_menu();
		return MENU_PLAYING;
	}
	// Pockets: CROSS tries to take a stack (the chance shown); caught, the menu closes
	if (page == PAGE_POCKETS && (pressed & SCE_CTRL_CROSS)) {
		cont_n = pocket_items();
		const int plant = plant_poison();
		if (plant >= 0 && selected == cont_n) { // (Poisoned: it does its harm on them, unseen)
			ItemEffect e[ITEM_EFFECTS_MAX];
			const int ne = items_effects(plant, e, ITEM_EFFECTS_MAX);
			float harm = 0.0f;
			for (int k = 0; k < ne; k++)
				harm += e[k].magnitude * fmaxf(1.0f, e[k].seconds);
			items_add(plant, -1);
			people_poisoned(pocket_npc, harm);
			ui_notice("Poison planted", 2.0f);
			close_menu();
			return MENU_PLAYING;
		}
		if (selected >= cont_n) {
			close_menu();
			return MENU_PLAYING;
		}
		const int item = cont_items[selected], count = cont_counts[selected];
		const int chance = pickpocket_chance(item, count);
		if ((int)(rand() % 100) < chance) {
			items_take(container_handle, item, count);
			// (Skyrim trains by what it was worth: value ^ fPickpocketSkillUsesCurve)
			skills_use(SK_PICKPOCKET, 1.0f + powf((float)(items_value(item) * count), 0.8f));
			if (selected >= items_in_container(container_handle, cont_items, cont_counts, CONTAINER_LIST))
				selected = selected > 0 ? selected - 1 : 0;
			show();
			return MENU_PAUSED;
		}
		caught = pocket_npc;
		close_menu();
		return MENU_PLAYING;
	}
	if (page == PAGE_CONTAINER && (pressed & (SCE_CTRL_START | SCE_CTRL_CIRCLE))) {
		loot_close();
		container_handle = -1;
		close_menu();
		return MENU_PLAYING;
	}
	// A container: L / R between its things (to take) and yours (to store: CROSS the stack,
	// SQUARE one; worn things come off first)
	if (page == PAGE_CONTAINER && (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))) {
		cont_store = !cont_store, selected = 0;
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_CONTAINER && cont_store) {
		const int n = store_rows();
		if (pressed & SCE_CTRL_UP)
			selected = (selected + n) % (n + 1);
		if (pressed & SCE_CTRL_DOWN)
			selected = (selected + 1) % (n + 1);
		if ((pressed & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE)) && selected < n) {
			const int it = store_list[selected];
			if (equip_worn(it))
				equip_toggle(it);
			items_put(container_handle, it, pressed & SCE_CTRL_SQUARE ? 1 : items_have(it));
			sfx_play("itm_up", 0.5f, 0.0f);
			if (selected >= store_rows())
				selected = store_n;
		} else if ((pressed & SCE_CTRL_CROSS) && selected == n) {
			loot_close();
			container_handle = -1, cont_store = 0;
			close_menu();
			return MENU_PLAYING;
		}
		show();
		return MENU_PAUSED;
	}
	int count = page == PAGE_POCKETS ? items_in_container(container_handle, cont_items, cont_counts, CONTAINER_LIST) + 1
	          : page == PAGE_MAIN ? MAIN_COUNT : page == PAGE_OPTIONS ? OPT_COUNT : page == PAGE_WAIT ? 2
	          : page == PAGE_MAGIC ? magic_rows() : page == PAGE_SKILLS ? skills_rows()
	          : page == PAGE_CONTAINER ? container_rows() + 1
	          : page == PAGE_SAVE ? save_count + 1 : page == PAGE_LOAD ? (save_count ? save_count : 1)
	                                                                  : inventory_rows() + 1;
	if (pressed & SCE_CTRL_UP)
		selected = (selected + count - 1) % count;
	if (pressed & SCE_CTRL_DOWN)
		selected = (selected + 1) % count;
	if (page == PAGE_OPTIONS && pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))
		change(pressed & SCE_CTRL_LEFT ? -1 : 1);
	if (page == PAGE_MAIN && selected == MAIN_CAMERA && pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))
		settings.third_person = !settings.third_person;
	// A container: TRIANGLE (or Take all) takes everything, CROSS one stack; Close leaves
	if (page == PAGE_CONTAINER && (pressed & (SCE_CTRL_TRIANGLE | SCE_CTRL_CROSS))) {
		if ((pressed & SCE_CTRL_TRIANGLE) || selected == 0) {
			for (int i = 0; i < cont_n; i++)
				items_take(container_handle, cont_items[i], cont_counts[i]);
		} else if (selected <= cont_n) {
			items_take(container_handle, cont_items[selected - 1], cont_counts[selected - 1]);
		}
		if (selected == count - 1 || (pressed & SCE_CTRL_TRIANGLE) || container_rows() == 1) {
			loot_close(); // (closes when emptied or left)
			container_handle = -1;
			close_menu();
			return MENU_PLAYING;
		}
		if (selected >= container_rows() + 1)
			selected = container_rows();
		show();
		return MENU_PAUSED;
	}
	// Magic: L / R equip the spell in that hand (again: take it out of the hand)
	if (page == PAGE_MAGIC && selected < magic_rows() && (pressed & SCE_CTRL_SQUARE)) { // ([]: a favourite, or not)
		const int r = mrows[selected];
		if (r >= 0)
			menu_toggle_favorite(FAV_SPELL, magic_name(r));
		else if (r == ROW_POWER)
			menu_toggle_favorite(FAV_SHOUT, races_power_name());
		else if (r <= ROW_SHOUT && r > ROW_OTHER && shouts_known(ROW_SHOUT - r) > 0)
			menu_toggle_favorite(FAV_SHOUT, shouts_name(ROW_SHOUT - r));
		show();
	}
	if (page == PAGE_MAGIC && selected < magic_rows() && mrows[selected] >= 0 &&
	    (pressed & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))) {
		const int h = pressed & SCE_CTRL_LTRIGGER ? HAND_LEFT : HAND_RIGHT, sp = mrows[selected];
		magic_set_hand(h, magic_hand(h) == sp ? -1 : sp);
		if (magic_hand(h) >= 0)
			magic_set_readied(1);
	}
	if (page == PAGE_WAIT && selected == 0 && pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))
		wait_hours = (wait_hours - 1 + (pressed & SCE_CTRL_LEFT ? 23 : 1)) % 24 + 1;
	int back = (pressed & SCE_CTRL_CIRCLE) || ((pressed & SCE_CTRL_CROSS) && selected == count - 1 && page != PAGE_MAIN &&
	                                          page != PAGE_OPTIONS && page != PAGE_SAVE && page != PAGE_LOAD);
	if (page == PAGE_OPTIONS && (pressed & SCE_CTRL_CROSS) && selected == OPT_KEYS) { // (Button mapping: its page)
		page = PAGE_KEYS, selected = 0;
		show();
		return MENU_PAUSED;
	}
	if (page == PAGE_OPTIONS && (pressed & SCE_CTRL_CROSS) && selected == OPT_MEASURE) { // (Measure: back to the game, and it runs)
		measure_asked = 1;
		close_menu();
		return MENU_PLAYING;
	}
	if (page == PAGE_OPTIONS && (pressed & SCE_CTRL_CROSS) && selected == OPT_BACK)
		back = 1;
	if (back) {
		if (page == PAGE_OPTIONS || page == PAGE_INVENTORY || page == PAGE_MAGIC || page == PAGE_SKILLS ||
		    page == PAGE_SAVE || page == PAGE_LOAD) {
			selected = page == PAGE_OPTIONS ? MAIN_OPTIONS : page == PAGE_INVENTORY ? MAIN_INVENTORY
			         : page == PAGE_MAGIC ? MAIN_MAGIC : page == PAGE_SAVE ? MAIN_SAVE : page == PAGE_LOAD ? MAIN_LOAD
			                                                                                           : MAIN_SKILLS;
			page = PAGE_MAIN;
		} else {
			close_menu();
			return MENU_PLAYING;
		}
		show();
		return MENU_PAUSED;
	}
	if (pressed & SCE_CTRL_CROSS) {
		if (page == PAGE_MAIN) {
			if (selected == MAIN_RESUME) {
				close_menu();
				return MENU_PLAYING;
			}
			if (selected == MAIN_QUIT) {
				close_menu();
				return MENU_QUIT;
			}
			if (selected == MAIN_SAVE || selected == MAIN_LOAD) { // the list of saves
				page = selected == MAIN_SAVE ? PAGE_SAVE : PAGE_LOAD;
				save_count = save_list(save_infos, SAVE_SLOTS);
				selected = 0, picture_for = -2;
				show();
				return MENU_PAUSED;
			}
			if (selected == MAIN_CAMERA)
				settings.third_person = !settings.third_person;
			else if (selected == MAIN_INVENTORY)
				page = PAGE_INVENTORY, inv_focus = 0, inv_sel = 0;
			else if (selected == MAIN_MAGIC)
				page = PAGE_MAGIC, selected = 0;
			else if (selected == MAIN_SKILLS)
				page = PAGE_SKILLS, selected = 0;
			else if (selected == MAIN_WAIT)
				page = PAGE_WAIT, selected = 0, sleeping = 0;
			else if (selected == MAIN_QUESTS)
				page = PAGE_QUESTS, selected = 0;
			else if (selected == MAIN_MAP) // (centred on you)
				page = PAGE_MAP, map_centre[0] = player_at[0] / CELL_M, map_centre[1] = -player_at[1] / CELL_M,
				map_local = local_known, local_centre[0] = local_at[0], local_centre[1] = local_at[2]; // (inside: its own map first)
			else
				page = PAGE_OPTIONS, selected = 0;
		} else if (page == PAGE_WAIT) {
			wait_pending = wait_hours, slept = sleeping;
			close_menu();
			return MENU_PLAYING;
		} else if (page == PAGE_OPTIONS) {
			change(1);
		} else if (page == PAGE_SAVE || (page == PAGE_LOAD && save_count > 0)) {
			const int k = selected - (page == PAGE_SAVE);
			request = page == PAGE_SAVE ? MENU_SAVE : MENU_LOAD;
			request_slot = k >= 0 && k < save_count ? save_infos[k].slot : SAVE_NEW;
			close_menu();
			return MENU_PLAYING;
		} else if (page == PAGE_MAGIC && selected < magic_rows()) {
			const int r = mrows[selected];
			char text[64];
			if (r == ROW_BEAST) { // Beast Form: SELECT's
				shouts_equip(-1);
				beast_ready_it(1);
				ui_notice("Beast Form readied (SELECT)", 2.0f);
			} else if (r == ROW_POWER) { // the race's power: SELECT's
				shouts_equip(-1);
				beast_ready_it(0);
				snprintf(text, sizeof(text), "%s readied (SELECT)", races_power_name());
				ui_notice(text, 2.0f);
			} else if (r <= ROW_SHOUT && r > ROW_OTHER) { // a shout: SELECT's
				const int sh = ROW_SHOUT - r;
				if (shouts_learned(sh) > shouts_known(sh) && shouts_unlock(sh)) { // (a dragon's soul for its next word)
					snprintf(text, sizeof(text), "Dragon soul spent: %s", shouts_words(sh, shouts_known(sh)));
					shouts_equip(sh);
				} else if (shouts_known(sh) <= 0) {
					snprintf(text, sizeof(text), "Its word is locked: you need a dragon's soul");
				} else {
					shouts_equip(sh);
					beast_ready_it(0);
					snprintf(text, sizeof(text), "%s readied (hold SELECT)", shouts_name(sh));
				}
				ui_notice(text, 3.0f);
				show();
			} else if (r <= ROW_OTHER) {
				ui_notice("That spell can't be cast yet", 2.0f);
			} else if (r >= 0) { // (CROSS: the right hand, as R)
				magic_set_hand(HAND_RIGHT, magic_hand(HAND_RIGHT) == r ? -1 : r);
				if (magic_hand(HAND_RIGHT) >= 0)
					magic_set_readied(1);
			}
		} else if (page == PAGE_SKILLS && skills_state()->pending > 0 && selected < 3) {
			if (skills_take_level_up())
				level_choice = selected + 1; // (main applies it to the vitals)
			selected = 0;
		} else if (page == PAGE_SKILLS) { // a skill: its perk tree
			const int skill = selected - (skills_state()->pending > 0 ? 3 : 0);
			if (skill >= 0 && skill < SKILL_COUNT && perks_node_count(skill) > 0) {
				page = PAGE_PERKS, perk_skill = skill, perk_node = perk_first(skill);
				ui_menu(NULL, NULL, 0, 0);
			}
		}
	}
	show();
	return MENU_PAUSED;
}

int menu_is_open(void) {
	return open;
}

int menu_take_measure(void) {
	const int m = measure_asked;
	measure_asked = 0;
	return m;
}

void menu_set_msaa_running(int on) {
	msaa_running = on;
}
