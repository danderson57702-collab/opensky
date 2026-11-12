#ifndef MENU_H
#define MENU_H

#include <psp2/ctrl.h>
#include <stdint.h>

// The pause menu (START) and the options it edits. Settings are saved to
// ux0:data/opensky_settings.txt (outside the pack, which redeploys replace).

typedef struct {
	int music_volume; // 0..10
	int voice_volume; // 0..10
	int subtitles;    // 0/1
	int stair_bounce; // 0 off, 1 low, 2 normal, 3 high
	int look_speed;   // 1..10
	int invert_look;  // 0/1
	int post;         // 0/1: bloom and colour grading (off by default: it washed out dark interiors)
	int debug;        // 0/1: testing controls (L held: fast travel; SELECT: noclip; R + TRIANGLE
	                  // held: fast-forward time); off by default
	int third_person; // 0/1: the camera (SQUARE)
	int difficulty;   // Skyrim's: 0 Novice, 1 Apprentice, 2 Adept (normal), 3 Expert, 4 Master, 5 Legendary
	int performance;  // PERF_*: the Fidelity setting - view distance, people and creatures about, effects
	                  // (main.c's profiles; Ultra Low the fastest)
	int readout;      // 0/1: the performance readout (frame time by system, memory) top left
	int frame_rate;   // FPS_*: 60, 30 (every other refresh), or smoothed (as fast as it goes, 22..60)
	int resolution;   // RES_*: the most the world is drawn at (424x240 .. 720x448), scaled to the screen (menus stay sharp); in
	                  // heavy places it's drawn smaller to keep the frame rate (the dynamic resolution, main.c)
	int controls;     // CONTROLS_*: the hands on the shoulders (R right, L left; TRIANGLE sprint, SQUARE
	                  // sneak: aim with the right stick while you draw), or on the face buttons
	int rear_touch;   // 0/1: touching the back's left half is L, its right half R (a worn shoulder button)
	int compass;      // 0/1: Skyrim's compass bar across the top and the compass rose top right
	int fov_first, fov_third; // the field of view, first / third person (degrees, up and down: 50..90; 65)
	int hud_crosshair;        // 0/1: the crosshair
	int hud_markers;          // 0/1: the quests' markers on the compass (the map keeps them)
	int hud_bar_fade;         // 0/1: health, magicka and stamina fade away when full, as Skyrim's (off: always shown)
	int msaa;                 // 0/1: 2x MSAA - the Vita's own anti-aliasing (set as the graphics start: from the next start)
	int upscaler;             // POST_UPSCALE_*: the world scaled up to the screen - Off (its pixels as blocks), Bilinear, or FSR 1.0 (EASU)
	int motion_blur;          // 0/1: camera motion blur - the view smeared as you turn it
	int dynres;               // 0/1: the dynamic resolution - the world drawn smaller while the GPU's behind (off: the size set)
	int bind[12];             // the buttons each action is on (KEY_*; menu_bind_buttons): the Button mapping page
} Settings;
enum { PERF_ULTRA_LOW, PERF_LOW, PERF_MEDIUM, PERF_HIGH, PERF_LEVELS };
enum { FPS_60, FPS_30, FPS_SMOOTH };
enum { RES_424, RES_480, RES_640, RES_720, RES_544, RES_544_352, RES_640_352, RES_COUNT }; // (new sizes last: saved settings
// keep their index) (the world drawn this big - 544 x 352 and 640 x 352 taller than 16:9, scaled to the screen's shape)
enum { CONTROLS_SHOULDERS, CONTROLS_FACE };
// The actions a button can be mapped to (Options: Button mapping) - the game reads them as the face-button layout's
// buttons (right hand TRIANGLE, left hand SQUARE, sprint R, sneak L, activate CIRCLE, jump CROSS, shout SELECT, ready
// D-pad up, camera down, the hands' spells left / right); START stays the menu
enum { ACT_RIGHT_HAND, ACT_LEFT_HAND, ACT_SPRINT, ACT_SNEAK, ACT_ACTIVATE, ACT_JUMP, ACT_SHOUT, ACT_READY, ACT_CAMERA,
       ACT_CYCLE_LEFT, ACT_CYCLE_RIGHT, ACT_COUNT };
// The pad's buttons as the game reads them, from the buttons pressed (play only: the menus read the pad as it is)
unsigned menu_bind_buttons(unsigned pad_buttons);
const char *menu_action_glyph(int action); // ("O", "X", "[]", "/\\", "L", "R"...: the button an action is on)
// The difficulty's multipliers (Skyrim's fDiffMultHPToPC* / fDiffMultHPByPC*): damage the
// player takes, and deals
float menu_damage_taken_mult(void);
float menu_damage_dealt_mult(void);


enum { MENU_PLAYING, MENU_PAUSED, MENU_QUIT };

void menu_init(void);
const Settings *menu_settings(void);
void menu_set_debug(int on); // (a test run's "debug": Debug controls, not offered in Options)
// Feeds this frame's buttons; returns MENU_PLAYING (the game runs), MENU_PAUSED (the
// menu is open: the game should stand still) or MENU_QUIT.
int menu_update(const SceCtrlData *pad);
// The camera's step smoothing for the stair bounce setting (see main.c)
float menu_step_keep(void);
// First / third person (SQUARE)
void menu_toggle_camera(void);
// Save / load chosen in the menu since the last call (0: neither)
enum { MENU_SAVE = 1, MENU_LOAD, MENU_TRAVEL }; // (travel: the slot is the map marker)
int menu_take_request(int *slot); // (and the save slot it's for: save.h)
// A level-up bonus chosen in Skills since the last call: 1 health, 2 magicka, 3 stamina (0: none)
int menu_take_level_choice(void);
// Something dropped from the inventory (-1 none): the item, how many
int menu_take_drop(int *count);
// Where the player is (the map shows it): engine x, z, yaw (degrees), outdoors or not
void menu_set_quest_markers(int n, const float (*at)[2]); // (the active quests' objectives: gold on the map)
// Favourites (Skyrim's): marked with SQUARE in the Magic and Inventory menus; D-pad left / right step through them
enum { FAV_NONE, FAV_SPELL, FAV_SHOUT, FAV_WEAPON };
int menu_is_favorite(int kind, const char *name);
void menu_toggle_favorite(int kind, const char *name);
void menu_save_favorites(uint32_t *hash, uint8_t *kind); // (32 of each)
void menu_load_favorites(const uint32_t *hash, const uint8_t *kind); // (NULL: none)
int menu_next_favorite_spell(int from, int *any); // (D-pad left / right: the favourite spell after from, -1 none; *any 0: none marked)
void menu_open_favorites(void);                   // (D-pad left / right held: the favourites list, time stopped)
void menu_set_player(float x, float z, float yaw_deg, int outdoors);
void menu_set_camera(int third_person);
// The player's carry weight (shown in the inventory)
void menu_set_carry_max(float weight, float carried); // (with the perks)
// An item chosen to eat or drink in the inventory since the last call (-1: none)
int menu_take_use(void);
// Pickpocketing: an NPC's pockets (items.h handle); caught by (NPC index, once; -1: no one)
// The Sleep page (a bed): how long to sleep
void menu_open_sleep(void);
void menu_open_load(void); // (the saves to load: the title's Continue)
int menu_is_open(void);
int menu_take_measure(void);        // (Options' Measure chosen: 1, once)
int menu_take_livearea(void);       // (Options' "Use this view for the LiveArea" chosen: 1, once)
void menu_set_msaa_running(int on); // (2x MSAA on as the graphics started)
void menu_open_pockets(int handle, int npc, const char *name);
// Talking to someone: what you can say; the chosen one's number once said (-2 goodbye, -1 none yet)
void menu_open_dialogue(const char *title, const char *const *options, int n);
int menu_take_dialogue(void);
// Buying and selling with a merchant (items.h's vendor)
void menu_open_barter(int vendor, const char *name);
// A crafting station (furniture.h's): the forge's, smelter's, tanning rack's, cooking pot's
// recipes; the arcane enchanter's disenchanting and enchanting
void menu_open_crafting(int station);
int menu_take_caught(void);
// Looks through a container (items.h handle): take one stack or all; closing it calls loot_close
void menu_open_container(int handle, const char *name);
// The alchemy lab's menu (an alchemy lab used: Skyrim's, mixing ingredients into potions)
void menu_open_alchemy(void);
// Hours chosen in the wait menu since the last call (0: none)
int menu_take_wait(int *was_sleep); // (hours to pass; was_sleep: in a bed)

// Inside (each frame): where you are and the place's name - the map opens on its local map
// (NULL place: outdoors, no local map)
void menu_set_local(float x, float y, float z, float yaw_deg, const char *place);

#endif
