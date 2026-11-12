// OpenSKY: streams converted Tamriel terrain and placed objects around the
// player under a hazy gradient sky. All data comes from the converted pack in
// ux0:data/opensky/.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "atmosphere.h"
#include "collision.h"
#include "lighting.h"
#include "music.h"
#include "stream.h"
#include "objects.h"
#include "actors.h"
#include "navmesh.h"
#include "ways.h"
#include "interior.h"
#include "intro.h"
#include "name.h"
#include "menu.h"
#include "talk.h"
#include "ui.h"
#include "voice.h"
#include "post.h"
#include "sktex.h"
#include "terrain.h"
#include "water.h"
#include <string.h>
#include <strings.h>
#include "fish.h"
#include "magic.h"
#include "people.h"
#include "wordfx.h"
#include "perks.h"
#include "projectiles.h"
#include "traps.h"
#include "worldmap.h"
#include "spellfx.h"
#include "doors.h"
#include "dtree.h"
#include "items.h"
#include "loot.h"
#include "locks.h"
#include "lockpick.h"
#include "equip.h"
#include "preview.h"
#include "activators.h"
#include "furniture.h"
#include "sfx.h"
#include "weather.h"
#include "creatures.h"
#include "worlds.h"
#include "races.h"
#include "save.h"
#include "skills.h"
#include "stones.h"
#include "chargen.h"
#include "racemenu.h"
#include "buildstamp.h"
#include "vitals.h"
#include "shouts.h"
#include "alchemy.h"
#include "effects.h"
#include "perf.h"
#include "gamelog.h"
#include "enchant.h"
#include "autotest.h"
#include "quests.h"
#include "crime.h"
#include "followers.h"
#include "brawl.h"
#include "services.h"
#include "topics.h"
#include "riding.h"
#include "beast.h"
#include "companions.h"
#include "college.h"
#include "thieves.h"
#include "questkit.h"
#include "fileio.h"
#include "frametime.h"
#include "gametext.h"

// Newlib heap: model meshes, collision grids and batch building need more than the
// 128 MB default. vitaGL takes most of the remaining RAM at init.
int _newlib_heap_size_user = 192 * 1024 * 1024;


#define SCREEN_W 960
#define SCREEN_H 544
#define PACK_DIR "ux0:data/opensky/"
#define STREAM_BUDGET_US 3000 // per frame for finishing streamed loads (uploads)
#define WORLD "tamriel"

#define INTERIOR_BACKDROP 0.3f // indoors, the background: this much of the fog colour (see the clear)
#define DEATH_FADE_S 1.5f     // dying: the screen goes dark...
#define DEATH_RESPAWN_S 3.5f  // ...and you're back at the last save
#define EYE_HEIGHT 1.7f
#define SEATED_EYE_DROP_M 0.55f // sitting: the eyes this much lower
#define STEP_VOLUME 0.38f        // footsteps outdoors...
#define STEP_VOLUME_INSIDE 0.26f // ...and inside (smaller rooms: quieter)
#define POWER_HOLD_S 0.35f        // TRIANGLE held this long: a power attack...
#define POWER_STAMINA 25.0f       // ...for this much stamina
#define HIT_FRACTION 0.4f         // a swing lands this far into its clip
#define REACH_1H_M 2.0f           // Skyrim's melee reach (fCombatDistance 141 units, one-handed)...
#define REACH_2H_M 2.4f           // ...and a little more with a two-handed weapon
#define HIT_CONE_COS 0.55f        // what's within this of straight ahead is hit
#define UNARMED_DAMAGE 4.0f       // a fist (Skyrim's unarmed damage for most races)
#define AIM_ASSIST_COS 0.9945f   // arrows bend to a foe within about 6 degrees of the crosshair
#define BATTLE_CRY_FEAR_M 18.3f    // (Skyrim's RaceNordBattleCryEffect: 60 feet, 30 s)
#define BATTLE_CRY_FEAR_S 30.0f
#define BATTLE_CRY_STAGGER_M 12.2f // (its StaggerPushFFSelfArea: 40 feet)
#define AIM_FRICTION 0.55f
#define SWING_QUEUE_S 0.45f // a swing pressed this near the end of the last one follows it        // ... and the view turns this much slower over one (Skyrim's pad assist)
#define FAST_TRAVEL_KMH 6.0f      // fast travel takes this long a kilometre
#define HUNTING_BOW_FORM 0x00013985u
#define IRON_ARROW_FORM 0x0001397du
#define START_ARROWS 20
#define BOW_MIN_DRAW 0.35f       // a bow drawn less than this far doesn't loose
#define BOW_DRAW_SHARE 0.85f      // (fully drawn this far into Skyrim's draw clip)
#define BLOCK_SHIELD_SHARE 0.25f  // a block lets this much of a blow through with a shield...
#define BLOCK_WEAPON_SHARE 0.45f  // ...and this much with a weapon
#define BLOCK_STAMINA_PER_DAMAGE 0.6f
#define BLOCK_POWER_ATTACK_MULT 0.66f // a power attack: a guard holds back this much of what it would (fBlockPowerAttackMult)
#define WADE_M 0.15f             // feet this close to the water's surface: wading (splashes)
#define SNEAK_EYE_DROP_M 0.55f // crouched
// Step smoothing: walking on ground, the body snaps up or down each stair step, and the
// camera only shows part of that jump at once. menu_step_keep() of it lands instantly (a soft
// bob, so stairs still feel like stairs), the rest eases in over ~STEP_EASE_S.
// Slopes, jumps, falls and noclip move the camera directly.
#define STEP_EASE_S 0.08f
#define STEP_MAX_M 0.6f
#define DOOR_FADE_OUT_S 0.3f
#define WALK_SPEED_MS 1.4f   // m/s
#define RUN_SPEED_MS 3.2f   // (Skyrim's run clip is made for ~2.8 m/s: much faster and the legs blur)
#define SWIM_SPEED 1.6f      // m/s; stick part way: slower, R (as sprinting): faster
#define SWIM_SLOW_SPEED 0.9f
#define SNEAK_TRAIN_M 12.0f      // sneaking trains within this of someone...
#define SNEAK_USAGE_PER_S 0.6f   // ...this much use a second
#define CANDLE_REACH_M 12.0f // Candlelight's light
#define TORCH_REACH_M 8.0f   // the held torch's light: how far it reaches, and its colour
#define TORCH_R 1.05f
#define TORCH_G 0.72f
#define TORCH_B 0.38f
#define SWIM_DIVE_DEG 25.0f  // at the surface, looking down more than this dives
#define SWIM_LEVEL_DEG 8.0f  // under water, looking within this of level swims level
#define SWIM_FAST_SPEED 2.6f
#define SNEAK_WALK_MS 0.9f
#define SNEAK_RUN_MS 1.8f
#define SPRINT_SPEED 4.4f
#define BEAST_EYE_LIFT_M 0.45f // (Beast Form: the werewolf's eyes - Skyrim's 1.13 tall race - this much higher)
#define BEAST_PACE_MULT 1.35f  // ... and its stride   // (the sprint clip: ~3.3 m/s)
#define DEBUG_TRAVEL_SPEED 72.0f
#define WALK_TILT 0.75f      // the stick tilted less than this walks
#define OPENING_HURT 0.5f // (Helgen Keep's fights, Skyrim's tutorial: its spiders and soldiers hurt this much of their worth)
#define RACE_FRAME_X 0.6f         // (the race menu: your head this far across the screen - Skyrim's, the user's video)
#define RACE_FRAME_RACE_M 0.62f   // (the Race tab: the camera this near the head, the head's middle this far down...)
#define RACE_FRAME_RACE_Y 0.40f
#define RACE_FRAME_BODY_M 1.30f   // (... the Body tab: head to hips)
#define RACE_FRAME_BODY_Y 0.25f
#define RACE_FRAME_CLOSE_M 0.80f  // (... the face's tabs - Head .. Hair: head and shoulders)
#define RACE_FRAME_CLOSE_Y 0.37f
#define THIRD_PERSON_DIST_M 2.6f        // camera behind the head
#define THIRD_PERSON_DIST_INSIDE_M 1.6f // (closer indoors: rooms are small)
#define THIRD_PERSON_SHOULDER_M 0.35f   // over the right shoulder
#define THIRD_PERSON_DROP_M 0.15f
#define THIRD_PERSON_WALL_GAP_M 0.25f   // how far the camera stays in front of a wall
#define THIRD_PERSON_BODY_M 0.3f       // half-size of the camera, kept clear of walls
#define WALL_LEARN_S 5.0f               // a word wall's word drifting into you (Skyrim's few seconds)
#define THIRD_PERSON_NEAR_M 0.1f        // near clip in third person (first: NEAR_CLIP_M)
#define ARMS_NEAR_M 0.02f               // the first-person arms' near clip (drawn after the world)
#define NEAR_CLIP_M 0.15f               // first person: the view corners (~1.6x this) stay inside the 0.35 m body
#define THIRD_PERSON_EASE_IN 6.0f        // per second, of the full distance: gliding in past something beside the view
#define THIRD_PERSON_HIDE_M 0.6f        // camera closer than this to the head: the body isn't drawn
#define THIRD_PERSON_EASE_OUT 2.0f      // per second, of the full distance: the camera backing out again
#define DOOR_FADE_IN_S 0.5f // larger height changes in one frame aren't steps
#define FOV_Y 65.0f // (the first-person arms' own; the world's: the options' field of view, view_fov)
#define FOG_START 60.0f
#define FOG_END (TERRAIN_FAR_RADIUS * CELL_SIZE_M - 20.0f)
#define FAR_CLIP (FOG_END + 30.0f)
static float fog_end = FOG_END, far_clip = FAR_CLIP; // (the performance setting's view distance)

// The frame's game time (the frame rate setting): the real time a frame took, at least 1/60 s at
// 60, 1/30 at 30; smoothed, the real
// time a frame takes averaged over a few (between 1/60 and 1/22 s: slower than 22 fps, the game
// slows rather than jumping)
static float frame_s = 1.0f / 60.0f, clock_s;
static float real_frame_s; // (the last frame as it really took: not held to 1/30 - the dynamic resolution's)
#define SMOOTH_FRAMES 4
#define FRAME_30_US 33333
#define SLOWEST_S (1.0f / 22.0f)

float frame_seconds(void) {
	return frame_s;
}


// What killed you, where (the card's log: a death with no photo still leaves its cause)
static void log_death(const char *cause, float fell, float x, float y, float z) {
	game_log("death: %s at %.1f %.1f %.1f in %s", cause, x, y, z, interior_current() >= 0 ? interior_id() : T_LCTN_000130FF_FULL0);
	if (fell > 0.0f)
		game_log("death: fell %.1f m", fell);
}

static void frame_time(int rate) {
	static unsigned long long last;
	static float recent[SMOOTH_FRAMES];
	static int at;
	const unsigned long long now = stream_now_us();
	const float real = last ? (now - last) / 1e6f : 1.0f / 60.0f;
	last = now;
	real_frame_s = real;
	recent[at++ % SMOOTH_FRAMES] = fminf(fmaxf(real, 1.0f / 60.0f), SLOWEST_S);
	if (rate == FPS_60) { // (the real time taken, as at 30: a fixed 1/60 had the game at half speed
		                  //  whenever the Vita drew at 30 - Oakflesh's minute lasted two)
		frame_s = fminf(fmaxf(real, 1.0f / 60.0f), SLOWEST_S);
	} else if (rate == FPS_30) { // (held to 30 below; the real time taken, so a frame that came
		                             //  early can never run the game fast, nor a slow one slow it)
		frame_s = fminf(fmaxf(real, 1.0f / 30.0f), SLOWEST_S);
	} else {
		float sum = 0.0f;
		const int n = at < SMOOTH_FRAMES ? at : SMOOTH_FRAMES;
		for (int i = 0; i < n; i++)
			sum += recent[i];
		frame_s = sum / n;
	}
	clock_s += frame_s;
}

// The performance setting (Options: Low, Medium, High), and a step lower while memory runs
// short (heap or video memory under the budget: busy places get a little plainer rather than
// running out); Skyrim's own console versions cut the same things
typedef struct {
	int land_rings, object_rings; // view distance (cells): the land, the biggest objects
	int texture_dim;              // textures no larger than this (0: as they are; a city: half)
	float fog_m;                  // the fog's end (0: at the land's edge)
	int creatures;                // creatures about at once
	float sim_m;                  // how far off people walk their schedules
	int think_every, poses;       // people beyond 30 m think every Nth frame; posed a frame
	int particles;                // spell particles at once
	float rain;                   // share of the rain / snow drawn
	float near_m;                 // small things and people dissolve out by here
} Profile;
static const Profile PROFILES[PERF_LEVELS] = {
	{3, 2, 128, 120.0f, 5, 40.0f, 6, 2, 90, 0.22f, 34.0f}, // Ultra Low (half of Low's, 120 m; small textures)
	{5, 3, 512, 0.0f, 10, 70.0f, 3, 3, 180, 0.45f, 42.0f}, // Low
	{6, 4, 1024, 0.0f, 14, 90.0f, 2, 4, 280, 0.7f, 50.0f}, // Medium
	{7, 4, 0, 0.0f, 20, 110.0f, 1, 5, 400, 1.0f, 54.0f},   // High (the land a ring less, 54 m for small things: the
	// Vita's GPU ran just past 33 ms a frame at 30 - the user's micro-stutters, Nov 8; the fog hides the land's edge)
};
#define HEAP_TIGHT_KB (24 * 1024) // memory short below these (KB free) ...
#define VRAM_TIGHT_KB (12 * 1024)
#define HEAP_EASY_KB (40 * 1024)  // ... and fine again above these
#define VRAM_EASY_KB (24 * 1024)

// In a walled city (its walls hide what's beyond anyway: little past them drawn), by Fidelity:
// the fog's end, and the full detail's reach (props, people; houses beyond it plainer)
static const float CITY_FOG_M[PERF_LEVELS] = {50.0f, 60.0f, 70.0f, 90.0f};
static const float CITY_NEAR_M[PERF_LEVELS] = {20.0f, 25.0f, 30.0f, 38.0f};
// Inside, by Fidelity: how far is drawn (a great hall's far end in its fog: Dragonsreach in
// full was a quarter of a million triangles a frame)
static const float INTERIOR_REACH_M[PERF_LEVELS] = {32.0f, 40.0f, 50.0f, 60.0f};

// The frame governor: under 20 fps for a while (Dragonsreach, Whiterun's market), the reach of
// what's drawn comes in a step at a time; back out once there's room again
#define GOVERN_SLOW_S (1.0f / 20.0f)  // slower than this...
#define GOVERN_FAST_S (1.0f / 27.0f)  // ...and room to spare under this
#define GOVERN_WAIT_S 2.0f            // (judged over this long)
#define GOVERN_STEPS 4
#define TRI_BUDGET 160000u        // more of the world's triangles than this a frame - the Vita's GPU at its limit (the Blue
#define GOVERN_QUICK_S 0.3f       //  Palace sent 328 thousand: 11 fps) - and slow: a step in at once, not after 2 s each
static int govern_step; // 0: as the setting has it; each a step nearer
static int governor_live; // (last frame was play: not a load, a menu, the title)
static const float GOVERN_SCALE[GOVERN_STEPS + 1] = {1.0f, 0.85f, 0.72f, 0.62f, 0.54f};

#define RIDE_CLUTTER_M 22.0f // (on the cart: small things dissolve out by here - the reach kept for people; the user's OK, Nov 7)
static void apply_profile(int level, int city, int inside) {
	static int applied = -1;
	const int opening = intro_active() && city, ride = intro_on_cart();
	const int key = level * 4 + city + inside * 2 + govern_step * 100 + opening * 1000 + ride * 2000;
	if (key == applied)
		return;
	applied = key;
	const float g = GOVERN_SCALE[govern_step];
	Profile city_p = PROFILES[level];
	if (city) { // (Whiterun's streets: the GPU's busiest place - a step plainer, the fog nearer)
		city_p.fog_m = city_p.fog_m > 0.0f ? fminf(city_p.fog_m, CITY_FOG_M[level]) : CITY_FOG_M[level];
		city_p.near_m = CITY_NEAR_M[level];
		city_p.texture_dim = city_p.texture_dim ? (city_p.texture_dim > 128 ? city_p.texture_dim / 2 : 128) : 512;
		city_p.land_rings = 2, city_p.object_rings = 1; // (the fog's well inside them)
	}
	if (govern_step) { // (the governor's: nearer fog, a nearer reach for full detail and people)
		city_p.fog_m = (city_p.fog_m > 0.0f ? city_p.fog_m : terrain_far_radius() * CELL_SIZE_M - 20.0f) * g;
		city_p.near_m *= g;
		city_p.creatures = (int)(city_p.creatures * g + 0.5f) > 2 ? (int)(city_p.creatures * g + 0.5f) : 2;
	}
	if (inside) // (a great hall - the Blue Palace, Dragonsreach: textures a step smaller, as in a city)
		city_p.texture_dim = city_p.texture_dim ? (city_p.texture_dim > 128 ? city_p.texture_dim / 2 : 128) : 512;
	if (opening) // (Helgen in the opening: a step smaller again - video memory ran out twice on the ride and the frames after
		// took 100 ms; the user chose this, Nov 8)
		city_p.texture_dim = city_p.texture_dim > 512 ? city_p.texture_dim / 2 : 256; // (never under 256: with memory short
		// too it went to 128 - Helgen's walls a smear, and no more memory for it, Nov 10)
	objects_set_texture_cap(city_p.texture_dim);
	interior_set_draw_cap(INTERIOR_REACH_M[level] * g);
	objects_set_view_limit(city ? city_p.fog_m + 5.0f : 0.0f); // (past the fog: not even sent to the GPU)
	actors_set_city(city || inside); // (a crowd posed less often: the nearest every frame, the rest in turn)
	const Profile *p = &city_p;
	terrain_set_far_radius(p->land_rings);
	objects_set_far_radius(p->object_rings);
	creatures_set_max_awake(p->creatures);
	actors_set_detail(p->sim_m, p->think_every, p->poses);
	spellfx_set_particle_cap(p->particles);
	weather_set_density(p->rain);
	const float clutter_m = ride ? fminf(p->near_m, RIDE_CLUTTER_M) : p->near_m;
	objects_set_near_fade(clutter_m * 0.78f, clutter_m);
	actors_set_draw_radius(p->near_m + 2.0f);
	fog_end = p->fog_m > 0.0f ? p->fog_m : terrain_far_radius() * CELL_SIZE_M - 20.0f, far_clip = fog_end + 30.0f;
	atmosphere_set_edge(fog_end);
	glFogf(GL_FOG_END, fog_end);
	game_log("performance: level %d%s%s, governor step %d (land %d rings, fog %.0f m, near %.0f m%s, %d creatures, textures %d)", level,
	         city ? " in a city" : "", opening ? " (the opening)" : "", govern_step, p->land_rings, fog_end, p->near_m,
	         ride ? ", small things 22 m: the ride" : "", p->creatures, p->texture_dim);
}

// The dynamic resolution (the user's choice, Nov 8): the world drawn smaller in the moments the Vita's GPU can't finish a
// frame in time, back up as it can - up to the Resolution setting's size. A frame late while the game's own work fit (the
// rest waited on the GPU: in vglSwapBuffers) is the GPU's; a few of those in half a second and the size comes down a
// step; three seconds all in time and it goes up a little - unless that size was just too much (then not for a while).
// The governor's nearer reach (below) only once this is as small as it goes
#define DYN_FLOOR_PIXELS (448 * 256) // (the least: about 448 x 256, a little under the PSP's 480 x 272)
#define DYN_MIN_SCALE 0.55f
#define DYN_WINDOW 15        // (frames judged: half a second)
#define DYN_LATE_FRAMES 4    // (this many late in them: a step down)
#define DYN_DOWN 0.93f       // (a step down: 7% each way, 14% of the pixels)
#define DYN_UP 1.03f
#define DYN_UP_AFTER_S 3.0f
#define DYN_CEILING_S 20.0f  // (up, then straight back down: not up there again for this long)
static float dyn_scale = 1.0f, dyn_ceiling = 1.0f, dyn_ceiling_left, dyn_good_s, dyn_since_up = 99.0f;
static unsigned dyn_late; // (the last DYN_WINDOW frames: a bit each, late on the GPU)
static float swap_wait_s; // (last frame's wait in vglSwapBuffers: on the GPU)

// Measure (Options): what each kind of thing costs here, on this device - Vita3K can't say what the Vita's GPU spends, and
// the log's frame times can't say on what. For 15 s the game draws without one kind of thing at a time (it flickers), the
// screen's refresh wait off and each frame waited out on the GPU (glFinish), so a frame's time is the game's work plus the
// GPU's, nothing hidden behind the refresh; the log gets a line a kind: its frame time, and how much less than with all
enum { MS_ALL, MS_NO_PEOPLE, MS_NO_OBJECTS, MS_NO_TERRAIN, MS_NO_CARTS, MS_NO_SKY, MS_NO_WATER, MS_NO_FX, MS_NO_ARMS, MS_NO_HUD,
       MS_HALF_SIZE, MS_ALL_AGAIN, MS_COUNT };
static const char *const MS_NAME[MS_COUNT] = {"everything", "without people and creatures", "without buildings, trees and things",
                                              "without the land", "without carts, doors and moving things", "without the sky",
                                              "without water and fish", "without weather, fires and effects",
                                              "without your arms (first person)", "without the HUD", "at half the size each way",
                                              "everything, again"};
#define MS_WARM_FRAMES 8
#define MS_FRAMES 30
static int measure_on, measure_step, measure_frame;
static int test_measure; // (a test run's "measure")
static unsigned long long measure_cpu_us, measure_gpu_us, measure_from_us;
static float measure_ms[MS_COUNT][2]; // (each step's: the game's work, the GPU's wait)
static int ms_skip(int what) {
	return measure_on && measure_step == what;
}

static void measure_start(void) {
	measure_on = 1, measure_step = 0, measure_frame = 0, measure_cpu_us = measure_gpu_us = 0;
	vglWaitVblankStart(GL_FALSE);
	game_log("measure: at %s, %dx%d - a line a kind: frame ms (the game's work + the GPU's wait)", interior_place(), post_max_w(),
	         post_max_h());
}

// A measurement kept apart from the log (which holds two runs: a Whiterun measure was gone by the next
// evening's play): appended to ux0:data/opensky_measure.txt, the place, the settings and each kind's times
#define MEASURE_FILE "ux0:data/opensky_measure.txt"
static void measure_keep(void) {
	char text[2048];
	const Settings *st = menu_settings();
	int n = snprintf(text, sizeof(text), "== build %s, at %s, up to %dx%d, quality %d, frame rate %d, msaa %d, upscaler %d\n",
	                 BUILD_STAMP, interior_place(), post_max_w(), post_max_h(), st->performance, st->frame_rate,
	                 st->msaa, st->upscaler);
	for (int k = 0; k < MS_COUNT && n < (int)sizeof(text) - 96; k++)
		n += snprintf(text + n, sizeof(text) - n, "%-38s %5.1f ms (%4.1f + %4.1f)\n", MS_NAME[k],
		              measure_ms[k][0] + measure_ms[k][1], measure_ms[k][0], measure_ms[k][1]);
	SceUID f = sceIoOpen(MEASURE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (f >= 0) {
		sceIoWrite(f, text, n);
		sceIoClose(f);
	}
}

// At the frame's end, the swap done: this frame's two times in; the next kind, or the end
static void measure_frame_done(unsigned long long cpu_us, unsigned long long gpu_us) {
	if (++measure_frame > MS_WARM_FRAMES)
		measure_cpu_us += cpu_us, measure_gpu_us += gpu_us;
	if (measure_frame < MS_WARM_FRAMES + MS_FRAMES)
		return;
	const float cpu = measure_cpu_us / 1000.0f / MS_FRAMES, gpu = measure_gpu_us / 1000.0f / MS_FRAMES;
	measure_ms[measure_step][0] = cpu, measure_ms[measure_step][1] = gpu;
	if (measure_step == MS_ALL || measure_step == MS_ALL_AGAIN)
		game_log("measure: %-38s %5.1f ms (%4.1f + %4.1f)", MS_NAME[measure_step], cpu + gpu, cpu, gpu);
	else
		game_log("measure: %-38s %5.1f ms (%4.1f + %4.1f): %+5.1f ms (%+.1f the game's, %+.1f the GPU's)", MS_NAME[measure_step],
		         cpu + gpu, cpu, gpu, cpu + gpu - measure_ms[MS_ALL][0] - measure_ms[MS_ALL][1], cpu - measure_ms[MS_ALL][0],
		         gpu - measure_ms[MS_ALL][1]);
	measure_frame = 0, measure_cpu_us = measure_gpu_us = 0;
	if (++measure_step >= MS_COUNT) {
		measure_on = 0;
		vglWaitVblankStart(GL_TRUE);
		char note[96];
		snprintf(note, sizeof(note), "Measured: %.0f ms a frame here (in the log)", measure_ms[MS_ALL][0] + measure_ms[MS_ALL][1]);
		ui_notice(note, 5.0f);
		game_log("measure: done");
		measure_keep();
	}
}

static float dyn_floor(void) {
	const float px = (float)post_max_w() * post_max_h();
	const float f = px > DYN_FLOOR_PIXELS ? sqrtf(DYN_FLOOR_PIXELS / px) : 1.0f;
	return f < DYN_MIN_SCALE ? DYN_MIN_SCALE : f;
}

static int dyn_at_floor(void) {
	return dyn_scale <= dyn_floor() + 0.005f;
}

static void dyn_apply(void) {
	const float sc = ms_skip(MS_HALF_SIZE) ? 0.5f : measure_on || !menu_settings()->dynres ? 1.0f : dyn_scale;
	const int w = ((int)(post_max_w() * sc + 4.0f) / 8) * 8, h = ((int)(post_max_h() * sc + 4.0f) / 8) * 8;
	post_set_dynamic_size(w, h);
	perf_set_resolution(w, h);
}

static void dynres_step(float real_s, int live) {
	static int max_w, max_h;
	if (max_w != post_max_w() || max_h != post_max_h()) { // (the setting changed: from its top again)
		max_w = post_max_w(), max_h = post_max_h();
		dyn_scale = dyn_ceiling = 1.0f, dyn_ceiling_left = 0.0f, dyn_late = 0, dyn_good_s = 0.0f;
		dyn_apply();
	}
	static int was_fixed = -1; // (the option off, or a measure on: the size as set - Nov 10: it sat at its least through
	const int fixed = measure_on || !menu_settings()->dynres; // Helgen and bought no frames; the cost there isn't pixels)
	if (fixed || fixed != was_fixed || ms_skip(MS_HALF_SIZE)) {
		was_fixed = fixed;
		if (fixed)
			dyn_scale = dyn_ceiling = 1.0f, dyn_late = 0;
		dyn_apply();
		if (fixed)
			return;
	}
	if (!live || real_s <= 0.0f || real_s > 0.25f) // (menus, loads, a load's stall: as it was)
		return;
	const float budget = menu_settings()->frame_rate == FPS_60 ? 1.0f / 60.0f : 1.0f / 30.0f;
	const int late = real_s > budget * 1.12f && real_s - swap_wait_s < budget * 0.92f;
	dyn_late = ((dyn_late << 1) | (unsigned)late) & ((1u << DYN_WINDOW) - 1);
	dyn_since_up += real_s;
	if (dyn_ceiling_left > 0.0f && (dyn_ceiling_left -= real_s) <= 0.0f)
		dyn_ceiling = 1.0f;
	dyn_good_s = late ? 0.0f : dyn_good_s + real_s;
	if (__builtin_popcount(dyn_late) >= DYN_LATE_FRAMES && !dyn_at_floor()) {
		if (dyn_since_up < 1.5f) // (that step up was too much: not again for a while)
			dyn_ceiling = dyn_scale / DYN_UP, dyn_ceiling_left = DYN_CEILING_S;
		dyn_scale = fmaxf(dyn_floor(), dyn_scale * DYN_DOWN);
		dyn_late = 0, dyn_good_s = 0.0f;
		dyn_apply();
	} else if (dyn_good_s > DYN_UP_AFTER_S && dyn_scale < fminf(1.0f, dyn_ceiling) - 0.001f) {
		dyn_scale = fminf(fminf(1.0f, dyn_ceiling), dyn_scale * DYN_UP);
		dyn_good_s = 0.0f, dyn_since_up = 0.0f;
		dyn_apply();
	}
}

// Each frame: the setting, a step lower while memory is short (checked twice a second)
static int memory_short;
static void performance_step(int frame, float frame_s) {
	{ // (the governor: how the frames have gone lately)
		static float slow_t, fast_t;
		if (frame_s > 0.0f && frame_s < 0.5f) { // (not a load's stall)
			slow_t = frame_s > GOVERN_SLOW_S && dyn_at_floor() ? slow_t + frame_s : fmaxf(0.0f, slow_t - frame_s * 0.5f); // (the
			// dynamic resolution first: the reach brought in only once it's as small as it goes - each step in made the
			// world choose its detail again, the road's 963 ms frame)
			fast_t = frame_s < GOVERN_FAST_S ? fast_t + frame_s : 0.0f;
		}
		static float over_t;
		const unsigned tris = objects_frame_tris();
		over_t = tris > TRI_BUDGET ? over_t + frame_s : 0.0f; // (more than the Vita draws in a frame, whatever the clock says)
		if ((slow_t > GOVERN_WAIT_S || over_t > GOVERN_QUICK_S) && govern_step < GOVERN_STEPS)
			govern_step++, slow_t = fast_t = over_t = 0.0f;
		else if (fast_t > GOVERN_WAIT_S * 4.0f && govern_step > 0 && tris < TRI_BUDGET * 6 / 10)
			govern_step--, slow_t = fast_t = 0.0f;
	}
	if (frame % 30 == 0) {
		const unsigned heap = perf_heap_free_kb(), vram = perf_gpu_free_kb();
		const int short_now = heap < HEAP_TIGHT_KB || (vram && vram < VRAM_TIGHT_KB);
		const int easy = heap > HEAP_EASY_KB && (!vram || vram > VRAM_EASY_KB);
		if (short_now && !memory_short)
			sceClibPrintf("performance: memory short (heap %u KB, vram %u KB free): a step plainer\n", heap, vram);
		memory_short = short_now ? 1 : easy ? 0 : memory_short;
		static int swept_at = -100000;
		if (vram && vram < VRAM_TIGHT_KB && stream_pending() == 0 && frame - swept_at > 300) { // (textures nothing shows any more: freed)
			swept_at = frame;
			const int freed = objects_release(0);
			if (freed)
				game_log("video memory short (%u KB free): %d textures freed", vram, freed);
		}
	}
	// (in a walled city - or Helgen during the opening, its crowd and fires - a step plainer)
	const int city = (worlds_current() > 0 || (intro_active() && !intro_stage())) && interior_current() < 0;
	const int level = menu_settings()->performance - city;
	apply_profile(level - (memory_short && level > 0 ? 1 : 0) < 0 ? 0 : level - (memory_short && level > 0 ? 1 : 0), city,
	              interior_current() >= 0);
}

// Start on the tundra by the Western Watchtower, facing Whiterun to the north-east.
#define COMPASS_FOUND_M (20000.0f / 70.0f)   // Skyrim's fHUDCompassLocationMaxDist
#define COMPASS_UNFOUND_M (12500.0f / 70.0f) // ... iMapMarkerVisibleDistance
#define SETTLED_FRAME_S (1.0f / 15.0f) // the loading screen stays till this many frames in a row are quicker than this (no
// load's stall in them: 1/24 waited out Helgen's GPU-bound 40 ms frames to the 12 s limit - the user's black, Nov 8)
#define SETTLED_FRAMES 20
#define SETTLE_MIN_S 0.6f
#define SETTLE_MAX_S 8.0f
#define WALK_NAMED_M 6.0f // a walk-through way out (a cave's mouth) named this near
// A bigger place come into - outdoors, a city, a crowded hall: the loading screen held longer, the
// Vita given room to read it all in (its people's models too) before you're in it
#define SETTLE_BIG_MIN_S 2.0f
#define SETTLED_BIG_FRAMES 45
#define SETTLE_BIG_MAX_S 12.0f
#define BIG_CROWD 8
#define START_CELL_X 1
#define START_CELL_Y -4
#define START_YAW 60.0f
#define START_HOURS 13.0f   // early afternoon
#define AUTOSAVE_WAIT_S 5.0f // after a door: the autosave waits until you're standing (or gives up)
static int capture_pending; // take this frame's scene for a save's picture
static int bow_nocked = -1; // the arrow on the string while a bow is drawn (item)
static float zoom_fov = 1.0f;
static float view_fov = FOV_Y; // (the world's field of view: the options' - first or third person)   // the view's field narrowed by this (Eagle Eye: Block while aiming)
static float world_slow = 1.0f; // time's pace (Steady Hand: slowed while zoomed in)
static float walkto_at[2], walkto_left, walkto_still, walkto_last[2]; // (autotest "walkto")
static float walkto_pts[96 * 3], walkto_goal[2]; // (its way along the navmesh: corner points, the next one)
static int walkto_n, walkto_i;
static float dash_left, dash_dir[2], dash_speed; // Whirlwind Sprint: flung along the ground
static int test_fast_shouts;           // (a test run that learned a word: a few seconds' recovery)
static float ethereal_left;             // Become Ethereal: unharmed, harmless
static float slow_time_left, slow_time_mult = 1.0f; // Slow Time: the world at this pace (you not)
static float fury_left;                 // Elemental Fury: quicker blows
static float aura_left;                 // Aura Whisper: life on the compass
static float storm_left, storm_harm, storm_tick; // Storm Call: its lightning on your foes, outdoors
#define STORM_CALL_M 30.0f      // (VoiceStormCall: 100 feet about you)
#define STORM_CALL_EVERY_S 3.0f
// Killmoves (Skyrim's KillMove on, KillMoveRandom 50): the last foe about you killed by your blow -
// half the time (a sneak attack's, always: the backstab) - goes down through Skyrim's paired
// animation instead of its death: its clip and yours (anims/killmoves.txt), seen from outside
#define PERK_SHADOW_WARRIOR 0x00058214u
#define SHADOW_NEAR_M 8.0f   // (Shadow Warrior: those further off than this lose you)
#define SHADOW_EVERY_S 3.0f  // (... at most this often)
static int sneaking_now;
#define PERK_SILENT_ROLL 0x00105f23u
#define SILENT_ROLL_S 0.6f       // the roll's length...
#define SILENT_ROLL_MS 5.5f      // ... its pace
#define SILENT_ROLL_AGAIN_S 0.4f // ... and again only after this
#define SILENT_ROLL_STAMINA 15.0f
static float silent_roll_t = -1.0f; // (the main loop's sneaking, last frame: for what's set before it's known)
#define MOUNTED_REACH_M 0.9f
#define SWEEP_CONE_COS 0.0f      // (Sweep: a half circle before you)
#define REFLECT_CHANCE 10        // Reflect Blows: one blow in ten turned back on the one who struck (Skyrim's) // (a swing from the saddle reaches down and out this much further)
#define KILLMOVE_CHANCE 0.5f
#define KILLMOVE_LAST_M 15.0f // (no other foe this near: the last)
#define MAX_KILLMOVES 16
typedef struct {
	char name[16], kind[8]; // (kind: the weapon's - 1hm, 2hm, 2hw, h2h - or "back": a sneak attack's)
	float ahead, side, turn, duration;
} Killmove;
static Killmove killmoves[MAX_KILLMOVES];
static int killmove_count;
static float killmove_left, killmove_t;
static int killmove_victim = -1;
static int corpse_search = -1, corpse_handle = -1, body_search = -1, body_handle = -1; // (a scene's dead being searched: bare once it's taken
                                                  //  all they wore and carried)
static char killmove_clip[24];
static IntroFrame opening;
static int begin_opening, opening_race;
static int bound_kept;

static void killmoves_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sanims/killmoves.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line && killmove_count < MAX_KILLMOVES; line = strtok(NULL, "\n")) {
		Killmove *k = &killmoves[killmove_count];
		if (sscanf(line, "%15s %7s %f %f %f %f", k->name, k->kind, &k->ahead, &k->side, &k->turn, &k->duration) == 6)
			killmove_count++;
	}
	free(buf);
	game_log("killmoves: %d", killmove_count);
}

// Whether a killmove is wanted now: Skyrim's KillMove on, half the blows that would end the last foe (a sneak
// attack's, always), none from the saddle
#define KILLMOVES_ON 0 // (Nov 7: off - the user would rather lose them than have the clunky cut; 1 brings them back)
static int killmove_wanted(int unseen) {
	if (!KILLMOVES_ON || killmove_left > 0.0f || riding_mounted())
		return 0;
	const float chance = autotest_active() ? 1.0f : KILLMOVE_CHANCE; // (a test run: every time, to be seen)
	return unseen || (float)(rand() % 1000) / 1000.0f < chance;
}

// One of the killmoves for this weapon (and victim: it has its clip), -1 none
static int killmove_choose(int v, int unseen, int wkind) {
	const char *kind = unseen && wkind >= 0 && wkind < 5 ? "back" : wkind < 0 ? "h2h" : wkind == 5 ? "2hm" : wkind == 6 ? "2hw" : "1hm";
	int pick[MAX_KILLMOVES], n = 0;
	for (int k = 0; k < killmove_count; k++) {
		char clip[24];
		snprintf(clip, sizeof(clip), "km_%s_v", killmoves[k].name);
		if (!strcmp(killmoves[k].kind, kind) && creatures_killmove_clip_ok(v, clip))
			pick[n++] = k;
	}
	return n ? pick[rand() % n] : -1;
}

// Its paired clips begun: the victim set before you, yours played whole
static int killmove_begin(int v, int which, const float feet[3], float yaw_deg) {
	const Killmove *k = &killmoves[which];
	const float r = yaw_deg * (float)M_PI / 180.0f;
	const float at[3] = {feet[0] + sinf(r) * k->ahead + cosf(r) * k->side, feet[1],
	                     feet[2] - cosf(r) * k->ahead + sinf(r) * k->side};
	char clip[24];
	snprintf(clip, sizeof(clip), "km_%s_v", k->name);
	if (!creatures_killmove(v, clip, at, r + k->turn))
		return 0;
	killmove_victim = v;
	snprintf(killmove_clip, sizeof(killmove_clip), "km_%s_k", k->name);
	killmove_left = k->duration, killmove_t = 0.0f;
	game_log("killmove: %s", k->name);
	return 1;
}

// Your blow just struck (weapon: its kind, -1 bare hands): a killmove if it killed the last foe
static void try_killmove(int wkind, const float feet[3], float yaw_deg) {
	int unseen = 0;
	const int v = creatures_take_blow_kill(&unseen);
	if (v < 0 || killmove_left > 0.0f || riding_mounted()) // (from the saddle: none)
		return;
	if (creatures_hostile_near(feet, KILLMOVE_LAST_M) || !killmove_wanted(unseen)) {
		game_log("killmove: none (%s)", creatures_hostile_near(feet, KILLMOVE_LAST_M) ? "foes about" : "chance");
		return;
	}
	const int which = killmove_choose(v, unseen, wkind);
	if (which >= 0)
		killmove_begin(v, which, feet, yaw_deg);
}

// A swing begun that would bring down the last foe: Skyrim's killmove starts here - as the attack does, the foe
// still on its feet - and the blow is the clip's. 1: begun (no swing of its own)
static int killmove_on_swing(int wkind, float damage, const float eye[3], const float look[3], float reach,
                             const float feet[3], float yaw_deg) {
	int unseen = 0;
	const int v = creatures_killmove_pick(eye, look, reach, HIT_CONE_COS, damage, feet, KILLMOVE_LAST_M, &unseen);
	if (v < 0 || !killmove_wanted(unseen))
		return 0;
	const int which = killmove_choose(v, unseen, wkind);
	if (which < 0 || !creatures_killmove_strike(v))
		return 0;
	if (!killmove_begin(v, which, feet, yaw_deg))
		return 0;
	sfx_play(wkind < 0 ? "wpn_hit_unarmed" : wkind == 4 || wkind == 6 ? "wpn_hit_blunt" : "wpn_hit_blade", 0.8f, 0.0f);
	return 1;
}

// A racial power in effect (races_power_running): what it does to the fight - an Orc's Berserker Rage (Skyrim's: double
// the harm done, half the harm taken, a minute), a Dark Elf's Ancestor's Wrath (a cloak of fire: those in reach of your arm
// burn 8 points a second)
static int power_is(const char *name) {
	const char *on = races_power_running(NULL, NULL);
	return on && !strcmp(on, name);
}

// Harm taken: the difficulty's share - none while ethereal
static float taken_mult(void) {
	return ethereal_left > 0.0f ? 0.0f : menu_damage_taken_mult() * (power_is(T_SPEL_000AA026_FULL0) ? 0.5f : 1.0f);
}
static float tk_eye[3], tk_look[3]; // (the aim's ray this frame: Telekinesis reaches along it)
static int modal_before;
static int flames_voice = -1, heal_voice = -1, spark_voice = -1; // (the held spells' loops)
static int spell_sounds_ran;  // (their block ran this frame: if not, they're stopped - the healing loop ran on for ever) // (the opening's choice / your name up last frame: the pad not the menus')
static int race_zoom;    // (the race menu's Sex / Face rows: the view close on the face)
static int test_face_zoom; // (an autotest's hair view: the face close, whatever row the menu's on)
static int race_facial_zoom; // (detailed facial sub-menus: eyes, scars, brows, etc.)
static float shove[2]; // (flung by a shout: m/s, world x z)
#define FAVORITES_HOLD_S 0.4f // (D-pad left / right held this long: the favourites list)
static float shout_flash, shout_shake;           // a shout's flash (its colour) and the camera's shudder
static int shout_flash_rgb[3];
static float last_x_noise, last_z_noise; // (how fast you move: how loud)
static int levels_before = -1, player_before = -1, chime_reset; // (the level-up chimes)
static float last_view[3];  // where the camera was last frame (arrows fly to what it's on)
enum { STAGE_TITLE, STAGE_RACE, STAGE_PLAY }; // before playing: the title, the race menu
#define MAX_POCKET_NPCS 640 // (actors.c's MAX_ACTORS)
#define CAUGHT_WARY_S 120.0f    // caught pickpocketing: no second try on them for this long
#define TIME_FAST_FORWARD 60.0f // TRIANGLE held: a game hour in ~1.25 s

// Re-checksums all texture memory periodically and logs corruption (see sktex.h).
#ifndef DEBUG_TEXTURE_INTEGRITY
#define DEBUG_TEXTURE_INTEGRITY 0
#endif

static float stick(uint8_t raw) {
	float f = (raw - 128) / 127.0f;
	return fabsf(f) < 0.2f ? 0.0f : f;
}

// vita-elf-create needs a few KB free at the end of the code segment for the module info,
// and fails ("segment 1 overlaps") when the code happens to end just short of a page
// boundary. This read-only padding moves that boundary; drop it if the build fails the
// other way.
__attribute__((used)) static const char elf_layout_pad[4096] = {1};

// What's underfoot, for footsteps: a dungeon's stone or a house's boards inside, a city's
// cobbles, the countryside's dirt
static const char *underfoot(void) {
	if (interior_current() >= 0) {
		static const char *const stone[] = {"barrow", "mine", "cave", "ruin", "crypt", "catacomb", "jail", "basement",
		                                    "dungeon", "tomb", "keep", "sanctum", "hall", "temple", "dragonsreach"};
		const char *id = interior_id();
		for (unsigned k = 0; k < sizeof(stone) / sizeof(stone[0]); k++)
			if (strstr(id, stone[k]))
				return "stone";
		return "wood";
	}
	return worlds_current() ? "stone" : "dirt";
}

// The player's character: a race and one of its faces (the body, and its gear: races.h)
// The character creator's parts on a race's Custom face (chargen.h; saved with the game)
static ChargenLook player_look; // (version 0: none yet - the race's first preset, as it's put on)
static int look_old_picks;     // (an older save's: its parts kept over the race's first preset)
static int racemenu_on;        // (the race menu open: racemenu.c)
static int swallow_start;      // (its Done pressed: START held still - not the pause menu)
static int menu_body_race = -1, menu_body_face = -1; // (the race menu's new body this frame: menu_set_body)
static float race_turn;
static int test_god; // (an autotest's "god": health kept full)
static int test_sleep; // (an autotest's "sleep")
static int test_talk = -1; // (an autotest's "talk <id>")
static int test_door = -1; // (an autotest's "usedoor <city>")
static int test_ride; // (an autotest's "ride": on or off your horse)
static const char *test_door_label;
static int test_travel = -1; // (an autotest's "travel <place>": its map marker)
static float held_feet; // (where you came into a place: held there while it loads)
static char travel_title[64]; // (fast travel's destination, for its loading screen)
static float travel_marker_y = NAN; // (fast travelled to a marker of Skyrim's: its height - set down at it; NAN none)
static float travel_marker_x, travel_marker_z; // (... and where)
#define TRAVEL_NAVMESH_M 25.0f    // fast travel: set down on the navmesh nearest Skyrim's marker, this near...
#define TRAVEL_NAVMESH_DY_M 20.0f // ...this far up or down (as Skyrim's MoveTo finds its navmesh)
static int travel_landing; // (1: fast travelled - set down on the ground once it's streamed in, from above; 2: through a door, loaded - on the floor under you)
static int cast_this_frame; // (a spell cast this frame: brawl.c's "no magic")
static int jail_serve; // (slept in your jail cell: the time served - crime.c)
static int aim_lock = -1;                  // (the locked door aimed at: locks.c)
static int lock_door_kind, lock_door = -1; // (the door being picked, to go on through)
static int lock_go_door = -1;              // (a load door picked open: through it now) // (the race menu: degrees you're turned from facing the camera - the right stick)

static int face_custom(const Race *r, int face) {
	return r && face >= 0 && face < r->face_count && strstr(r->face_name[face], "Custom") != NULL;
}

static int face_female(const Race *r, int face) {
	const size_t fl = r ? strlen(r->face_family[face]) : 0;
	return fl > 2 && !strcmp(r->face_family[face] + fl - 2, "_f");
}

// A look for a race and sex that has none (its first preset; an older save's parts over it)
static void look_for(const char *race_eid, int woman) {
	ChargenLook l;
	if (player_look.version == 1 || !chargen_preset(race_eid, woman, 0, &l))
		return;
	if (look_old_picks)
		memcpy(l.pick, player_look.pick, sizeof(l.pick));
	player_look = l, look_old_picks = 0;
}

static void set_character(int race, int face) {
	const unsigned long long t0 = sceKernelGetProcessTimeWide();
	chargen_clear(); // (the last face's parts off before the body's changed under them)
	races_set_player(race, face);
	const Race *r = races_get(races_player());
	const int f = races_player_face(), custom = face_custom(r, f), woman = r ? face_female(r, f) : 0;
	// (a made face's look: its skin and hair coloured here, not baked - gear's bare skin too)
	const int looked = custom && chargen_presets(r->eid, woman) > 0;
	static const unsigned char WHITE[3] = {255, 255, 255};
	if (!r || !equip_change_body(r->face_family[f], r->face_stem[f], looked ? WHITE : r->tone[f]))
		sceClibPrintf("main: no character for race %d face %d\n", race, face);
	else if (looked) {
		look_for(r->eid, woman);
		if (!chargen_apply_look(r->eid, woman, &player_look))
			chargen_apply(r->eid, woman, player_look.pick);
	} else {
		actors_player_look_colors(NULL, NULL); // (a ready face: as it was made)
		if (custom)
			chargen_apply(r->eid, woman, player_look.pick);
	}
	game_log("race menu: race %d face %d made in %u ms", race, face, (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000));
}

// A race's made ("Custom") face for a sex (-1: none)
static int custom_face_of(const Race *r, int woman) {
	for (int f = 0; r && f < r->face_count; f++)
		if (face_custom(r, f) && face_female(r, f) == woman)
			return f;
	return -1;
}

// The race menu's: a race and sex chosen (or the menu opened) - its made face with the look on
static void menu_set_body(int race, int woman, ChargenLook *look) {
	const Race *r = races_get(race);
	const int f = custom_face_of(r, woman);
	if (f < 0)
		return;
	player_look = *look, look_old_picks = 0;
	set_character(race, f);
	*look = player_look; // (its parts kept in range)
	menu_body_race = race, menu_body_face = f;
}

static int dragon_souls; // absorbed (saved): each unlocks a word of a shout
static int naming;       // the system keyboard's up for your name (the frame shows it: vglSwapBuffers)

// Saves from before 10-21's wider map kept interiors and creatures by their numbers then: the
// numbers now (pack legacy/*_1020.txt: line n, the old n's new number; -1 gone)
#define LEGACY_BEFORE "2026-10-21 17" // (the wider map's pack was made that afternoon)
static int legacy_save(void) {
	return save_read_version() <= 13 && strcmp(save_read_date(), LEGACY_BEFORE) < 0;
}

static int legacy_number(const char *file, int old) {
	static struct { const char *file; int *map, n; } tables[2];
	for (int t = 0; t < 2; t++) {
		if (tables[t].file && strcmp(tables[t].file, file))
			continue;
		if (!tables[t].file) {
			tables[t].file = file;
			char path[128];
			snprintf(path, sizeof(path), PACK_DIR "legacy/%s", file);
			long size = 0;
			char *text = file_read_all(path, &size);
			for (char *l = text; text && l < text + size; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : text + size)
				tables[t].n++;
			tables[t].map = tables[t].n ? malloc(sizeof(int) * tables[t].n) : NULL;
			int k = 0;
			for (char *l = text; text && tables[t].map && l < text + size && k < tables[t].n;
			     l = strchr(l, '\n') ? strchr(l, '\n') + 1 : text + size)
				tables[t].map[k++] = atoi(l);
			free(text);
			game_log("save: %s - %d old numbers", file, tables[t].n);
		}
		return old >= 0 && old < tables[t].n && tables[t].map ? tables[t].map[old] : -1;
	}
	return -1;
}

// A save before version 16 kept the killed creatures (and their bodies) by their numbers in
// creatures.txt - right only for the map it was made with. Each wider map's numbers fell on others'
// spawn points (whole camps emptied), and a save made since carries those on: none can be trusted
static int no_spawn(int old) {
	(void)old;
	return -1;
}

// The game as it stands, for saving
#define GUIDE_REACH_M 110.0f // Clairvoyance: the way found this far on by the navmesh loaded about you

// The navmesh's way from feet to goal (Clairvoyance): its points (from feet), how long; 0 if none
// The navmesh nearest a spot that's dry - its own nearest, else the nearest of rings about it (off
// Dawnstar's beach the nearest lay under the sea: you were set down chest-deep by its ship)
static int dry_navmesh_near(float x, float y, float z, float out[3]) {
	NavRef nr;
	float best_d = 1e9f, cand[3], sea;
	int found = 0;
	for (int ring = 0; ring <= 4; ring++)
		for (int k = 0; k < (ring ? 8 : 1); k++) {
			const float a = k * 0.785f, r = ring * 5.0f;
			if (!navmesh_nearest_level(x + cosf(a) * r, y, z + sinf(a) * r, ring ? 6.0f : TRAVEL_NAVMESH_M, TRAVEL_NAVMESH_DY_M, &nr,
			                           cand))
				continue;
			if (interior_current() < 0 && terrain_water_at(cand[0], cand[2], &sea) && sea > cand[1] + 0.5f)
				continue; // (under the water)
			const float d = hypotf(cand[0] - x, cand[2] - z) + fabsf(cand[1] - y);
			if (d < best_d)
				best_d = d, memcpy(out, cand, sizeof(cand)), found = 1;
		}
	return found;
}

// Whether a walled city's navmesh reaches an objective (x, z): it's inside the walls
static int city_holds(const float goal[2], const float feet[3]) {
	NavRef r;
	float at[3];
	return navmesh_nearest(goal[0], feet[1], goal[1], 25.0f, &r, at);
}

static int guide_path(const float feet[3], const float goal[3], float *pts, float *len) {
	NavRef from, to;
	float h, at[3];
	memcpy(pts, feet, 3 * sizeof(float));
	int n = 1;
	if (navmesh_locate(feet[0], feet[1], feet[2], &from, &h) && navmesh_nearest(goal[0], goal[1], goal[2], 25.0f, &to, at))
		n += navmesh_path(from, feet, to, at, pts + 3, 63);
	*len = 0.0f;
	for (int k = 1; k < n; k++)
		*len += sqrtf((pts[k * 3] - pts[k * 3 - 3]) * (pts[k * 3] - pts[k * 3 - 3]) +
		              (pts[k * 3 + 1] - pts[k * 3 - 2]) * (pts[k * 3 + 1] - pts[k * 3 - 2]) +
		              (pts[k * 3 + 2] - pts[k * 3 - 1]) * (pts[k * 3 + 2] - pts[k * 3 - 1]));
	return n;
}

static void fill_save(SaveGame *g, const Player *p, float yaw, float pitch, const Vitals *v) {
	memset(g, 0, sizeof(*g));
	g->interior = interior_current();
	if (g->interior >= 0)
		snprintf(g->interior_id, sizeof(g->interior_id), "%s", interior_id());
	{
		uint8_t words[SHOUT_SAVE_SLOTS];
		shouts_save_words(words);
		memcpy(g->shout_words, words, 16), memcpy(g->shout_words_more, words + 16, 16);
	}
	people_save(g->people_dead);
	g->pos[0] = p->x, g->pos[1] = p->feet, g->pos[2] = p->z;
	g->yaw = yaw, g->pitch = pitch;
	interior_entry(g->entry, &g->entry_yaw);
	g->hours = lighting_sky()->hours;
	g->health = v->health, g->magicka = v->magicka, g->stamina = v->stamina;
	g->third_person = menu_settings()->third_person;
	const SkillState *sk = skills_state();
	memcpy(g->skill_level, sk->level, sizeof(g->skill_level));
	memcpy(g->skill_xp, sk->xp, sizeof(g->skill_xp));
	g->player_level = sk->player_level, g->player_xp = sk->player_xp, g->level_pending = sk->pending;
	Vitals base = *v; // (a potion's fortify isn't kept: the maximums without it)
	effects_unfortify(&base);
	beast_unbonus(&base); // (nor Beast Form's: saved as yourself)
	g->max_health = base.max_health, g->max_magicka = base.max_magicka, g->max_stamina = base.max_stamina;
	g->health = fminf(g->health, base.max_health), g->stamina = fminf(g->stamina, base.max_stamina);
	// (a scroll in a hand isn't kept: the hand's saved empty)
	const int ls = magic_hand(HAND_LEFT) < SPELL_COUNT ? magic_hand(HAND_LEFT) : -1;
	const int rs = magic_hand(HAND_RIGHT) < SPELL_COUNT ? magic_hand(HAND_RIGHT) : -1;
	g->spell = SAVE_SPELL_HANDS | (ls + 1) | (rs + 1) << 4;
	g->candle_left = magic_candle_left();
	g->race = races_player(), g->face = races_player_face();
	g->power_cooldown = races_power_cooldown();
	g->perk_points = sk->perk_points;
	perks_save(g->perks);
	g->spells_known = magic_known_mask() | SAVE_SPELLS_SET;
	items_books_read(g->books_read, 96);
	for (int i = 0; i < magic_other_count() && i < 24 + 64; i++)
		snprintf(i < 24 ? g->spells_other[i] : g->spells_more[i - 24], 32, "%s", magic_other(i));
	for (int h = 0; h < 2; h++) // (a tome spell in a hand: by name)
		if (magic_hand(h) >= SPELL_COUNT && magic_hand(h) < magic_count())
			snprintf(g->hand_spells[h], sizeof(g->hand_spells[h]), "%s", magic_name(magic_hand(h)));
	worldmap_save(g->discovered);
	{
		uint32_t dead[1024];
		g->dead_count = creatures_save(dead, g->dead_at, 1024);
		g->game_hours = lighting_game_hours();
		snprintf(g->player_name, sizeof(g->player_name), "%s", name_get());
		g->dragon_souls = shouts_souls(); // (those not yet spent)
		memcpy(g->look, player_look.pick, sizeof(player_look.pick));
		g->look_full = player_look;
		quests_save_inactive(g->quests_inactive);
		menu_save_favorites(g->favorites, g->favorite_kind);
		quests_save(g->quest_stage, g->quest_stage_more);
		companions_save(g->companions);
		college_save(g->college);
		thieves_save(g->thieves);
		beast_save(g->beast);
		items_vendors_invested(g->vendors_invested, 8, 1);
		crime_save(g->bounty, g->hold_hostile, &g->jailed_hold);
		g->quest_stage[12] = services_save(); // (the trainings this level, the Whiterun gate)
		riding_save(g->horse);
		walls_save(g->walls_used);
		followers_save(&g->followers_by_id);
		g->quest_stage[15] = intro_stage(); // (the opening: where in it, and with whom)
		g->quest_stage[14] = ((int)objects_switches() & 0xFF) | (int)shouts_state() << 8 | stones_active() << 24; // (+ the words learned / unlocked) // (the world's switches: Helgen burned...; the standing stone taken)
		for (int i = 0; i < g->dead_count; i++)
			g->dead[i] = (uint16_t)dead[i], g->dead_keys[i] = creatures_spawn_key((int)dead[i]);
	}
	g->item_count = actors_player_base_items() < SAVE_ITEMS ? actors_player_base_items() : SAVE_ITEMS;
	for (int i = 0; i < g->item_count; i++) {
		int worn = 0;
		actors_player_item(i, &worn);
		g->equipped[i] = (unsigned char)worn;
	}
}

// The scene for a save's picture (save.h): the frame as drawn, before the menus go over it
static uint16_t thumb[SAVE_THUMB_W * SAVE_THUMB_H];
static int thumb_ok;

static void capture_thumb(void) {
	uint8_t *px = malloc(SCREEN_W * SCREEN_H * 4);
	if (!px)
		return;
	glReadPixels(0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, px); // (bottom row first)
	for (int y = 0; y < SAVE_THUMB_H; y++)
		for (int x = 0; x < SAVE_THUMB_W; x++) {
			// The average of a few pixels around each picture pixel's middle
			const int sx = x * SCREEN_W / SAVE_THUMB_W, sy = SCREEN_H - 1 - (y * SCREEN_H / SAVE_THUMB_H + 2);
			int r = 0, g = 0, b = 0;
			for (int k = 0; k < 4; k++) {
				const uint8_t *p = &px[((sy - (k >> 1) * 2) * SCREEN_W + sx + (k & 1) * 2) * 4];
				r += p[0], g += p[1], b += p[2];
			}
			r /= 4, g /= 4, b /= 4;
			thumb[y * SAVE_THUMB_W + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
		}
	free(px);
	thumb_ok = 1;
}

// The game into a save slot (save.h; SAVE_NEW, SAVE_AUTO or one to replace), with its
// inventory / containers / taken things (items.c's file beside it) and the scene's picture
static int write_game(const SaveGame *g, int slot) {
	SaveInfo info;
	memset(&info, 0, sizeof(info));
	snprintf(info.place, sizeof(info.place), "%s", interior_place());
	const Race *r = races_get(races_player());
	snprintf(info.race, sizeof(info.race), "%s", r ? r->name : "");
	info.level = skills_state()->player_level > 0 ? skills_state()->player_level : 1;
	info.hours = g->hours;
	const int written = save_write(slot, g, &info, thumb_ok ? thumb : NULL);
	if (written < 0)
		return 0;
	uint32_t worn[64];
	items_set_worn(worn, equip_save(worn, 64));
	items_set_save_path(save_items_path(written));
	items_save();
	return 1;
}

static int apply_save(const SaveGame *g, Player *p, float *yaw, float *pitch, Vitals *v);
static int place_checked = -999; // (the place cells were last reset for)
#define RESET_HOURS 240.0f // Skyrim's iHoursToRespawnCell: containers refill after 10 game days

// A bow and arrows to try archery with (Skyrim's Hunting Bow and Iron Arrows)
static void grant_bow(void) {
	const int bow = items_by_form(HUNTING_BOW_FORM), arrows = items_by_form(IRON_ARROW_FORM);
	if (bow >= 0 && items_have(bow) <= 0)
		items_add(bow, 1);
	if (arrows >= 0)
		items_add(arrows, START_ARROWS);
}

// A save slot back (its inventory too); 0 if it isn't there
static int load_game(int slot, Player *p, float *yaw, float *pitch, Vitals *v) {
	SaveGame g;
	if (slot < 0 || !save_read(slot, &g))
		return 0;
	quests_load_inactive(save_read_version() >= 22 ? g.quests_inactive : NULL); // (older saves: every quest active)
	menu_load_favorites(save_read_version() >= 22 ? g.favorites : NULL, g.favorite_kind);
	begin_opening = 0;
	opening_race = 0;
	bound_kept = 0;
	memset(&opening, 0, sizeof(opening));
	opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
	items_set_save_path(save_items_path(slot));
	chime_reset = 1;
	if (!apply_save(&g, p, yaw, pitch, v))
		return 0;
	if (save_read_version() < 3)
		equip_grant_start_shields(); // (saved before them: the shields to try)
	if (save_read_version() < 4)
		grant_bow(); // (saved before bows: one to try)
	SaveInfo list[SAVE_SLOTS]; // (where it was: the save's own words for it)
	const int n = save_list(list, SAVE_SLOTS);
	for (int i = 0; i < n; i++)
		if (list[i].slot == slot && list[i].place[0] && strcmp(list[i].place, "Earlier save"))
			interior_set_place(list[i].place);
	return 1;
}

static int apply_save(const SaveGame *g, Player *p, float *yaw, float *pitch, Vitals *v) {
	begin_opening = 0;
	opening_race = 0;
	bound_kept = 0;
	memset(&opening, 0, sizeof(opening));
	opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
	if (g->race != races_player() || g->face != races_player_face())
		if (save_read_version() >= 9) { // (the creator's look: on before the body's loaded)
			player_look = g->look_full, look_old_picks = 0;
			if (player_look.version != 1) { // (an older save's: its parts, over the race's first preset)
				memset(&player_look, 0, sizeof(player_look));
				memcpy(player_look.pick, g->look, sizeof(player_look.pick));
				look_old_picks = 1;
			}
		}
		set_character(g->race, g->face); // (before the clothes go back on)
	races_set_power_cooldown(g->power_cooldown);
	lighting_set_game_hours(save_read_version() >= 6 ? g->game_hours : g->hours); // (older saves: from now)
	dragon_souls = save_read_version() >= 6 ? g->dragon_souls : 0;
	quests_load(g->quest_stage, g->quest_stage_more);
	companions_load(g->companions); // (a save from before them: zeros - not begun)
	college_load(g->college);
	thieves_load(g->thieves);
	beast_load(g->beast);
	items_vendors_invested((uint32_t *)g->vendors_invested, 8, 0); // (a save from before quests: zeros, they begin)
	services_load(g->quest_stage[12]);
	{
		static const float no_horse[6] = {0};
		riding_load(save_read_version() >= 12 ? g->horse : no_horse);
		walls_restore(g->walls_used); // (older saves: none spent - each wall teaches its shout's next word)
	}
	crime_load(g->bounty, g->hold_hostile, save_read_version() >= 11 ? g->jailed_hold : -1); // (a save from before crime: no bounties)
	{
		static const int32_t nobody[4] = {-1, 0, 0, 0};
		if (save_read_version() >= 16)
			followers_load(&g->followers_by_id);
		else // (by its number in the list as it stood then; Lydia's earned with Dragon Rising)
			followers_load_old(save_read_version() >= 11 ? g->followers : nobody, save_read_date(),
			                   quests_stage(Q_DRAGON_RISING) >= QUEST_DONE);
	}
	intro_load(g->quest_stage[15]);
	name_set(save_read_version() >= 8 ? g->player_name : NULL);
	objects_set_switches((uint32_t)g->quest_stage[14] & 0xFF);
	shouts_set_state((unsigned)g->quest_stage[14] >> 8 & 0xFFFF, dragon_souls);
	if (save_read_version() >= 15) { // (every shout's words; before, the first four's above; past sixteen: version 17)
		uint8_t words[SHOUT_SAVE_SLOTS] = {0};
		memcpy(words, g->shout_words, 16);
		if (save_read_version() >= 17)
			memcpy(words + 16, g->shout_words_more, 16);
		shouts_load_words(words);
	}
	{ // (the townspeople killed; a save from before: none)
		static const uint32_t nobody_dead[PEOPLE_DEAD_MAX];
		people_load(save_read_version() >= 16 ? g->people_dead : nobody_dead);
	}
	stones_set_active(g->quest_stage[14] >> 24 & 0xF);
	items_set_clock(lighting_game_hours());
	place_checked = -999; // (cells due to reset: looked at again)
	items_load_corpses_by(save_read_version() >= 16 ? NULL : no_spawn); // (an older save's bodies: gone)
	if (items_load() < 3) // (first: the interior hides what was taken; an older inventory gets the
		equip_new_game();  //  player's own clothes as items)
	int interior = g->interior; // (by its name now; a save from before the wider map: its number then)
	if (interior >= 0 && save_read_version() >= 14 && g->interior_id[0] && interior_find(g->interior_id) >= 0)
		interior = interior_find(g->interior_id);
	else if (interior >= 0 && legacy_save())
		interior = legacy_number("interiors_1020.txt", interior);
	if (interior != g->interior)
		game_log("save: the interior %d is %d now (%s)", g->interior, interior, interior >= 0 ? "found" : "gone - outdoors");
	if (!interior_go(interior, g->entry, g->entry_yaw))
		return 0;
	p->x = g->pos[0], p->feet = g->pos[1], p->z = g->pos[2], p->vy = 0.0f;
	*yaw = g->yaw, *pitch = g->pitch;
	if (g->interior < 0) {
		for (int pass = 0; pass < 2; pass++) {
			terrain_update(p->x, p->z);
			objects_update(p->x, p->z);
			navmesh_update(p->x, p->z);
			stream_flush();
		}
	}
	lighting_set_hours(g->hours);
	vitals_init(v);
	effects_reset(); // (the potions drunk before are gone)
	if (g->player_level > 0) { // (a version 2 save: skills, level, spells)
		SkillState *sk = skills_state();
		memcpy(sk->level, g->skill_level, sizeof(sk->level));
		memcpy(sk->xp, g->skill_xp, sizeof(sk->xp));
		sk->player_level = g->player_level, sk->player_xp = g->player_xp, sk->pending = g->level_pending;
		// Perks (a save from before them: a point for each level gained so far)
		perks_load(g->perks);
		if (save_read_version() >= 4)
			worldmap_load(g->discovered);
		// Spells learned and skill books read (a save from before tomes: every spell known)
		magic_set_known_mask(g->spells_known & SAVE_SPELLS_SET ? g->spells_known : ~0u);
		magic_set_other((const char (*)[MAGIC_KEY_LEN])g->spells_other, 24);
		for (int i = 0; i < 64; i++)
			magic_add_other(g->spells_more[i]);
		items_set_books_read(g->books_read, 96);
		{ // (the creatures killed stay dead)
			uint32_t dead[1024];
			const int n = save_read_version() >= 4 && g->dead_count > 0 && g->dead_count <= 1024 ? g->dead_count : 0;
			float dead_at[1024];
			int kept = 0;
			// (by their keys; a save from before kept their numbers, which wider maps put on others'
			// spawn points - whole camps emptied: all alive again, as after Skyrim's reset)
			const int by_key = save_read_version() >= 16;
			for (int i = 0; i < n; i++) {
				const int s = by_key ? creatures_spawn_by_key(g->dead_keys[i]) : -1;
				if (s >= 0)
					dead_at[kept] = g->dead_at[i], dead[kept++] = (uint32_t)s;
			}
			if (kept != n)
				game_log("save: %d of %d killed creatures kept (%s)", kept, n, by_key ? "by key" : "an older save's numbers: all alive again");
			creatures_load(dead, save_read_version() >= 6 ? dead_at : NULL, kept);
		}
		sk->perk_points = save_read_version() >= 4 ? g->perk_points : sk->player_level - 1;
		v->max_health = g->max_health, v->max_magicka = g->max_magicka, v->max_stamina = g->max_stamina;
		if (g->spell & SAVE_SPELL_HANDS) {
			magic_set_hand(HAND_LEFT, (g->spell & 15) - 1);
			magic_set_hand(HAND_RIGHT, (g->spell >> 4 & 15) - 1);
		} else { // (before hands: the spell, | SAVE_SPELL_DUAL in both)
			magic_set_hand(HAND_RIGHT, g->spell & 0xff);
			magic_set_hand(HAND_LEFT, g->spell & SAVE_SPELL_DUAL ? g->spell & 0xff : -1);
		}
		magic_set_candle(g->candle_left);
		for (int h = 0; h < 2; h++) // (a tome spell in a hand, by name)
			for (int sp = SPELL_COUNT; g->hand_spells[h][0] && sp < magic_count(); sp++)
				if (!strcmp(magic_name(sp), g->hand_spells[h]))
					magic_set_hand(h, sp);
	}
	v->health = g->health > 1.0f ? g->health : v->max_health;
	v->magicka = g->magicka, v->stamina = g->stamina;
	menu_set_camera(g->third_person);
	for (int i = 0; i < g->item_count && i < actors_player_base_items(); i++)
		actors_player_set_worn(i, g->equipped[i]);
	uint32_t worn[64];
	equip_restore(worn, items_worn(worn, 64));
	return 1;
}

// What the player carries, with perks (Conditioning: heavy armour worn weighs nothing...)
static float carried_weight(void) {
	float w = items_carried();
	if (!perks_taken_count())
		return w;
	uint32_t forms[32];
	const int n = equip_save(forms, 32);
	for (int i = 0; i < n; i++) {
		const int item = items_by_form(forms[i]);
		if (items_type(item) != IT_ARMO)
			continue;
		PerkContext ctx = {0};
		ctx.keyword_count = items_keywords(item, &ctx.keywords);
		w -= items_weight(item) - perks_apply(EP_MOD_ARMOR_WEIGHT, items_weight(item), &ctx);
	}
	return w;
}

// Skyrim's armour: each worn piece's rating raised by its skill (+0.4% a level from 20 below)
// and the perks (Juggernaut, Agile Defender...); every point turns aside 0.12% of a blow, at
// most 80%. Also which armour skill is worn most (hits train it)
#define KW_ARMOR_HEAVY 0x0006bbd2u
#define KW_ARMOR_LIGHT 0x0006bbd3u
static float armour_rating(int *trains) {
	uint32_t forms[32];
	const int n = equip_save(forms, 32);
	float total = 0.0f, heavy = 0.0f, light = 0.0f;
	for (int i = 0; i < n; i++) {
		const int item = items_by_form(forms[i]);
		if (items_type(item) != IT_ARMO || items_stat(item) <= 0)
			continue;
		PerkContext ctx = {0};
		ctx.keyword_count = items_keywords(item, &ctx.keywords);
		int skill = -1;
		for (int k = 0; k < ctx.keyword_count; k++)
			skill = ctx.keywords[k] == KW_ARMOR_HEAVY ? SK_HEAVY_ARMOR : ctx.keywords[k] == KW_ARMOR_LIGHT ? SK_LIGHT_ARMOR : skill;
		const float lvl = skill >= 0 ? (float)skills_state()->level[skill] : 0.0f;
		const float r = perks_apply(EP_MOD_ARMOR_RATING, items_stat(item) * (1.0f + 0.4f * (lvl + 20.0f) / 100.0f), &ctx) *
		                (skill >= 0 ? fmaxf(0.0f, 1.0f + effects_skill(skill) / 100.0f) : 1.0f); // (Fortify Heavy / Light Armor)
		total += r;
		if (skill == SK_HEAVY_ARMOR)
			heavy += r;
		else if (skill == SK_LIGHT_ARMOR)
			light += r;
	}
	*trains = heavy > light ? SK_HEAVY_ARMOR : light > 0.0f ? SK_LIGHT_ARMOR : -1;
	return total + magic_armour_bonus(); // (an Ironflesh scroll's)
}

// A bound weapon in hand (Bound Sword, Battleaxe, Dagger, Bow: magic.c's)
#define PERK_MYSTIC_BINDING 0x000640b3u
#define PERK_SOUL_STEALER 0x000d799eu
#define PERK_OBLIVION_BINDING 0x000d799cu
#define MYSTIC_BINDING_MULT 1.5f
#define SOUL_STEALER_S 5.0f
static int bound_in_hand(int weapon) {
	const char *b = magic_bound_name();
	return weapon >= 0 && b && b[0] && !strcmp(items_name(weapon), b);
}

// Avoid Death (Restoration): once a day, near death, 250 health back
#define PERK_AVOID_DEATH 0x000a3f64u
#define AVOID_DEATH_HEAL 250.0f
#define AVOID_DEATH_BELOW 0.1f
static int avoid_death_day = -1;
static void avoid_death(Vitals *v) {
	const int day = (int)(lighting_game_hours() / 24.0f);
	if (v->health > 0.0f && v->health < v->max_health * AVOID_DEATH_BELOW && day != avoid_death_day &&
	    perks_has_form(PERK_AVOID_DEATH)) {
		v->health = fminf(v->max_health, v->health + AVOID_DEATH_HEAL), v->shown_for = 0.0f;
		avoid_death_day = day;
		game_log("perks: Avoid Death");
	}
}

// Quick Hands: a lock picked under someone's nose and not noticed (Skyrim's lockpicking crime chance 0)
static int quick_hands(void) {
	return perks_apply(EP_MOD_LOCKPICK_CRIME, 1.0f, NULL) <= 0.0f;
}

// Fists of Steel: the heavy gauntlets worn - their base armour rating (0 none)
#define KW_ARMOR_GAUNTLETS 0x0006c0efu
static float heavy_gauntlets(void) {
	uint32_t forms[32];
	const int n = equip_save(forms, 32);
	for (int i = 0; i < n; i++) {
		const uint32_t *kw;
		const int item = items_by_form(forms[i]), nk = items_keywords(item, &kw);
		int gauntlets = 0, heavy = 0;
		for (int k = 0; k < nk; k++)
			gauntlets |= kw[k] == KW_ARMOR_GAUNTLETS, heavy |= kw[k] == KW_ARMOR_HEAVY;
		if (gauntlets && heavy)
			return (float)items_stat(item);
	}
	return 0.0f;
}

// The player's grunt (Skyrim's combat effort lines, in their voice): "hit", "death", "attack"
static void player_grunt(const char *what) {
	// (Skyrim's player only grunts - struck, dying: its "attack" lines are taunts, never the
	// player's - in their race's own voice: a Nord's for Nords)
	if (!strcmp(what, "attack"))
		return;
	const Race *race = races_get(races_player());
	const int female = actors_player_female();
	char voice[24];
	if (race && !strcmp(race->eid, "NordRace"))
		snprintf(voice, sizeof(voice), "%snord", female ? "female" : "male");
	else
		snprintf(voice, sizeof(voice), "%s", shouts_voice());
	char event[40];
	snprintf(event, sizeof(event), "v_%.14s_%s", voice, what);
	sfx_play(event, 0.8f, 0.0f);
}

// A racial power's effect where it's more than a buff: Battle Cry (Skyrim's: those about you
// up to level 99 flee for 30 s, a shove staggering those within 40 feet), its yell and its look
static void use_power_effect(const char *name, const float feet[3]) {
	if (!strcmp(name, T_MGEF_000E40C2_FULL0)) {
		shouts_power_yell("battlecry");
		sfx_play("power_battlecry", 1.0f, 0.0f);
		const float chest[3] = {feet[0], feet[1] + 1.2f, feet[2]}, ahead[3] = {0.0f, 0.0f, -1.0f};
		creatures_shout_push(chest, ahead, BATTLE_CRY_STAGGER_M, -1.0f, 0.05f, 0.0f, 0); // (all round)
		creatures_frighten(feet, BATTLE_CRY_FEAR_M, BATTLE_CRY_FEAR_S); // (after: staggered, then they run)
		spellfx_shout(chest, ahead, 1.0f, ELEMENT_FIRE, 0.7f); // (the roar's aura, fiery)
		for (int k = 0; k < 8; k++) { // (a ring of force rolling out about you)
			const float a = k * (float)M_PI / 4.0f, dir[3] = {sinf(a), 0.0f, cosf(a)};
			spellfx_wave(chest, dir, BATTLE_CRY_STAGGER_M, 0.45f, ELEMENT_FORCE, 0.6f);
		}
		shout_flash = 0.35f, shout_shake = 0.6f;
		shout_flash_rgb[0] = 200, shout_flash_rgb[1] = 60, shout_flash_rgb[2] = 30;
	} else if (!strcmp(name, T_SHOU_000E40CB_FULL0)) {
		shouts_power_yell("emperor");
	} else if (!strcmp(name, T_MGEF_000E40D0_FULL0)) {
		shouts_power_yell("beasttongue");
	} else if (!strcmp(name, T_SPEL_000AA026_FULL0)) { // (an Orc's: a roar, the view red)
		sfx_play("power_battlecry", 1.0f, 0.0f);
		shout_flash = 0.5f, shout_shake = 0.4f;
		shout_flash_rgb[0] = 220, shout_flash_rgb[1] = 30, shout_flash_rgb[2] = 20;
	} else if (!strcmp(name, T_MGEF_000B8F30_FULL0)) { // (a Dark Elf's: the fire bursts out about you)
		const float chest[3] = {feet[0], feet[1] + 1.0f, feet[2]};
		spellfx_burst(chest, ELEMENT_FIRE, 2.5f);
		sfx_play("mag_firebolt_cast", 0.9f, 0.0f);
		shout_flash = 0.4f;
		shout_flash_rgb[0] = 255, shout_flash_rgb[1] = 120, shout_flash_rgb[2] = 40;
	} else { // (the rest - Highborn, Dragonskin, Histskin, Adrenaline Rush: a pale flash, their hum)
		sfx_play("mag_heal_lp", 0.8f, 0.0f);
		shout_flash = 0.4f;
		shout_flash_rgb[0] = 200, shout_flash_rgb[1] = 220, shout_flash_rgb[2] = 255;
	}
}

// The left hand's weapon (item, -1)
static int lweapon_now(void) {
	int kind;
	return equip_left_weapon(&kind);
}

// A weapon's perk context (its keywords; a power attack's kind)
static PerkContext weapon_context(int weapon, uint32_t power) {
	PerkContext ctx = {0};
	ctx.keyword_count = items_keywords(weapon, &ctx.keywords);
	ctx.power_attack = power;
	ctx.school = -1;
	return ctx;
}

// Skyrim's perks the engine plays out itself (their entries ask about the one struck)
#define POWER_SIDEWAYS 0x000914e7u
#define POWER_BACKWARD 0x000914e8u
#define KW_WAR_AXE 0x0001e712u
#define KW_MACE 0x0001e714u
#define KW_BATTLEAXE 0x0006d932u
#define KW_WARHAMMER 0x0006d930u
static const uint32_t HACK_AND_SLASH[] = {0x0003fffa, 0x000c3678, 0x000c3679};
static const uint32_t LIMBSPLITTER[] = {0x000c5c05, 0x000c5c06, 0x000c5c07};
static const uint32_t BONE_BREAKER[] = {0x0005f592, 0x000c1e92, 0x000c1e93};
static const uint32_t SKULLCRUSHER[] = {0x0003af84, 0x000c1e96, 0x000c1e97};
#define PERK_PARALYZING_STRIKE 0x0003afa6u
#define PERK_WARMASTER 0x0003afa7u
#define PERK_STEADY_HAND_1 0x00103adau
#define PERK_STEADY_HAND_2 0x00103adbu
#define PERK_QUICK_SHOT 0x00105f19u
#define PERK_RANGER 0x00058f63u
#define PERK_BLOCK_RUNNER 0x00106253u
#define PERK_POWER_BASH 0x00058f67u
#define PERK_DISARMING_BASH 0x00058f66u
#define PERK_SHIELD_CHARGE 0x00058f6au
#define PERK_DUAL_FLURRY_1 0x00106256u
#define PERK_DUAL_FLURRY_2 0x00106257u
#define PERK_QUICK_REFLEXES 0x000d8c33u
#define BASH_MULT 0.5f           // a bash: this much of a blow's harm (Skyrim's bashStart attack data)...
#define BASH_POWER_MULT 1.5f     // ...a power bash's (bashPowerStart)
#define BASH_REACH_M 2.0f        // (fCombatBashReach: 141 units)
#define BASH_STAMINA 35.0f       // (fStaminaBashBase)
#define POWER_BASH_STAMINA 55.0f // (fStaminaPowerBashBase)
#define DISARM_BASH_CHANCE 50    // (iPerkBlockDisarmChance)
#define TORCH_BASH_FIRE 3.0f     // a torch's bash burns (TorchBashFireSpell: 3 fire)
#define QUICK_REFLEXES_MULT 0.05f // the world's pace while a power attack comes at your raised guard (PerkQuickReflexesEffect)...
#define QUICK_REFLEXES_S 1.0f     // ...this long
#define CHARGE_EVERY_S 0.3f      // Shield Charge: those ahead knocked down this often...
#define CHARGE_DAMAGE 4.0f       // ...this hard (shieldChargeKnockbackConcAimed)...
#define CHARGE_STAMINA 10.0f     // ...for this much stamina each (shieldChargeDamageStamina)
static int shield_charging;      // (sprinting behind a raised shield: Shield Charge's pace)
#define BLEED_S 3.0f      // (a bleeding wound's seconds; damage a second: 2, 3, 4 by rank)
#define PARALYZE_S 3.0f

// A skill's Fortify (an enchantment's, a potion's)
static float fortify(int skill) {
	return fmaxf(0.0f, 1.0f + effects_skill(skill) / 100.0f);
}

static int perk_rank(const uint32_t forms[3]) {
	int r = 0;
	while (r < 3 && perks_has_form(forms[r]))
		r++;
	return r;
}

static int weapon_is(int weapon, uint32_t kw) {
	const uint32_t *k;
	const int n = weapon >= 0 ? items_keywords(weapon, &k) : 0;
	for (int i = 0; i < n; i++)
		if (k[i] == kw)
			return 1;
	return 0;
}

// A blow's or an arrow's damage with the perks' critical hits (Bladesman, Critical Shot: their
// chance; the weapon's critical damage, about half its own, times theirs); and what of the
// target's armour it ignores (Bone Breaker, Skullcrusher) set for the blow about to land
static float with_criticals(int weapon, const PerkContext *ctx, float dmg) {
	const float chance = perks_apply(EP_CRIT_CHANCE, 0.0f, ctx);
	if (chance > 0.0f && (float)(rand() % 100) < chance && weapon >= 0) {
		dmg += perks_apply(EP_CRIT_DAMAGE, items_stat(weapon) * 0.5f, ctx);
		ui_notice("Critical strike", 1.0f);
	}
	const int pierce = weapon_is(weapon, KW_MACE) ? perk_rank(BONE_BREAKER) : weapon_is(weapon, KW_WARHAMMER)
	                   ? perk_rank(SKULLCRUSHER) : 0;
	creatures_set_armor_pierce(pierce * 0.25f);
	return dmg;
}

// After a blow landed: an axe's bleeding (Hack and Slash, Limbsplitter), a backward power
// attack's paralysis (Paralyzing Strike, Warmaster: a quarter of the time)
static void after_blow(int weapon, int two_handed, uint32_t power) {
	const int bleed = weapon_is(weapon, KW_WAR_AXE) ? perk_rank(HACK_AND_SLASH)
	                  : weapon_is(weapon, KW_BATTLEAXE) ? perk_rank(LIMBSPLITTER) : 0;
	if (bleed)
		creatures_bleed_struck(1.0f + bleed, BLEED_S);
	if (power == POWER_BACKWARD && perks_has_form(two_handed ? PERK_WARMASTER : PERK_PARALYZING_STRIKE) &&
	    rand() % 100 < 25)
		creatures_paralyze_struck(PARALYZE_S);
	creatures_set_armor_pierce(0.0f);
}

// The player's torch: its item index (-1: none) and whether it's held
static int find_torch(int *worn) {
	*worn = 0;
	for (int i = 0; i < actors_player_items(); i++)
		if (!strncmp(actors_player_item(i, worn), "Torch", 5))
			return i;
	*worn = 0;
	return -1;
}

// --- Starting up: a log on the card and something on the screen ----------------------------
// Each step is written to ux0:data/opensky_log.txt as it starts (opened, written and closed each
// time: a crash leaves the step it died in) with the time and the heap used; once the font is
// up, the step is shown on the screen. Without Sony's shader compiler (ur0:data/libshacccg.suprx,
// which ShaRKF00D extracts) nothing can be drawn: the screen is plain red instead (clearing it
// needs no shaders) until START closes the game.
static int boot_ui;

static void boot_log(const char *what) {
	game_log("heap %3u MB free  %s", perf_heap_free_kb() / 1024, what);
}

static void boot_step_show(const char *what) {
	if (!boot_ui)
		return;
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	ui_boot(what); // (the game's name and its makers' credit, from the user's own files, while it loads - the user, Nov 8)
	ui_draw();
	vglSwapBuffers(GL_FALSE);
}

static void boot_step(const char *what) {
	boot_log(what);
	boot_step_show(what);
}

// A trivial shader compiled: whether the shader compiler is there (the game's every shader
// is compiled as it starts)
static int shader_compiler_works(void) {
	static const char *src = "float4 main(uniform float4 u_c) : COLOR { return u_c; }\n";
	const GLuint sh = glCreateShader(GL_FRAGMENT_SHADER);
	if (!sh)
		return 0;
	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	glDeleteShader(sh);
	return ok != 0;
}

static int file_exists(const char *path) {
	SceIoStat st;
	return sceIoGetstat(path, &st) >= 0;
}

// The program's own files on the card were named for its first working name till Nov 11: a card that has those
// and none of today's gets them renamed, once (saves, settings and the stored items carry over by themselves)
static void old_names_step(void) {
	static const char *const names[] = { "saves", "settings.txt", "items.dat", "save.dat" };
	for (int i = 0; i < 4; i++) {
		char was[64], is[64];
		SceIoStat st;
		snprintf(was, sizeof(was), "ux0:data/sk" "lt_%s", names[i]);
		snprintf(is, sizeof(is), "ux0:data/opensky_%s", names[i]);
		if (sceIoGetstat(is, &st) < 0 && sceIoGetstat(was, &st) >= 0)
			sceIoRename(was, is);
	}
}

int main(void) {
	sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0); // (the game on core 0: posing 1, streaming and sound 2)
	old_names_step();
	boot_log("starting");
	{
		char text[64];
		snprintf(text, sizeof(text), "%d archives open", fileio_init(PACK_DIR));
		gametext_load(PACK_DIR); // (the game's own words, from the player's data: the program has none - gametext.h)
		autotest_init(PACK_DIR);
		boot_log(text);
	}
	// The Vita's fastest official clocks (retail games' 444 MHz; apps start at 333)
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	char clocks[64];
	snprintf(clocks, sizeof(clocks), "clocks: CPU %d MHz, GPU %d MHz", scePowerGetArmClockFrequency(),
	         scePowerGetGpuClockFrequency());
	boot_log(clocks);
	// The GPU sorts each frame's triangles into screen tiles in its parameter buffer; when a
	// frame has more than fits, it stops to draw what it has and starts again (a "partial
	// render", costly whatever the resolution). Whiterun's 200k triangles a frame were near
	// the default 16 MB. (Main memory, not video memory.)
	vglSetParamBufferSize(32 * 1024 * 1024);
	// The shaders, once compiled, kept on the card and read back at every start after (vitaGL's HAVE_SHADER_CACHE): the
	// game's every shader compiled at each start was much of its wait, 13 s of it the post-processing's (Nov 8)
	vglSetShaderCachePath("ux0:data/opensky_shaders");
	// (vitaGL returns true only when it had to fall back to a smaller screen size)
	// 2x MSAA (the option): the Vita's own anti-aliasing, the display's and every target's - chosen as the graphics start,
	// so read from the settings here, before the menu has them
	int msaa = 0;
	{
		SceUID fd = sceIoOpen("ux0:data/opensky_settings.txt", SCE_O_RDONLY, 0);
		if (fd >= 0) {
			static char text[2048];
			const int n = sceIoRead(fd, text, sizeof(text) - 1);
			sceIoClose(fd);
			text[n > 0 ? n : 0] = 0;
			const char *at = strstr(text, "msaa=");
			msaa = at && at[5] == '1';
		}
	}
	menu_set_msaa_running(msaa);
	const GLboolean fell_back = vglInitExtended(0, SCREEN_W, SCREEN_H, 8 * 1024 * 1024,
	                                            msaa ? SCE_GXM_MULTISAMPLE_2X : SCE_GXM_MULTISAMPLE_NONE);
	boot_log(msaa ? "2x MSAA on" : "2x MSAA off");
	boot_log(fell_back ? "graphics up (at a smaller size than asked)" : "graphics up");
	vglWaitVblankStart(GL_TRUE); // (frames shown on the screen's refresh: no tearing; 30 fps waits for two)
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	boot_log(file_exists("ur0:data/libshacccg.suprx") || file_exists("ur0:data/external/libshacccg.suprx")
	             ? "ur0:data/libshacccg.suprx is there" : "ur0:data/libshacccg.suprx is NOT there");
	if (!shader_compiler_works()) {
		boot_log("NO SHADER COMPILER: a test shader didn't compile (run ShaRKF00D) - red screen");
		for (;;) { // (red until START: the game can't draw anything without it)
			SceCtrlData pad;
			sceCtrlPeekBufferPositive(0, &pad, 1);
			glClearColor(0.6f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			vglSwapBuffers(GL_FALSE);
			if (pad.buttons & SCE_CTRL_START)
				sceKernelExitProcess(0);
		}
	}
	boot_log("shader compiler found");
	ui_init(PACK_DIR, SCREEN_W, SCREEN_H); // (first: the loading steps are shown)
	boot_ui = 1;
	boot_step("the world's light");

	lighting_init(START_HOURS);
	atmosphere_init(FOG_END);
	water_init();
	boot_step("fish");
	fish_init(PACK_DIR);
	boot_step("skills, magic and perks");
	skills_init(PACK_DIR);
	chargen_init(PACK_DIR);
	stones_init(PACK_DIR);
	magic_init(PACK_DIR);
	perks_init(PACK_DIR);
	boot_step("the map");
	worldmap_init(PACK_DIR);
	boot_step("the land");
	terrain_init(PACK_DIR, WORLD);
	stream_init();
	boot_step("objects");
	objects_init(PACK_DIR, WORLD);
	boot_step("paths");
	navmesh_init(PACK_DIR, WORLD);
	ways_init(PACK_DIR);
	boot_step("people");
	actors_init(PACK_DIR);
	boot_step("dialogue");
	talk_init(PACK_DIR);
	quests_init(PACK_DIR);
	intro_init(PACK_DIR); // (Skyrim's opening: Helgen)
	doors_init(PACK_DIR);
	boot_step("items");
	items_init(PACK_DIR);
	equip_init(PACK_DIR);
	items_new_game();
	effects_reset();
	equip_new_game();
	grant_bow();
	boot_step("races and shouts");
	races_init(PACK_DIR);
	shouts_init(PACK_DIR);
	boot_step("sounds");
	sfx_init(PACK_DIR);
	boot_step("weather");
	weather_init(PACK_DIR);
	boot_step("creatures");
	crime_init(PACK_DIR); // (before the creatures: the guards' holds)
	followers_init(PACK_DIR);
	people_init(PACK_DIR);
	wordfx_init(PACK_DIR);
	topics_init(PACK_DIR);
	riding_init(PACK_DIR);
	beast_init(PACK_DIR);
	creatures_init(PACK_DIR);
	killmoves_init(PACK_DIR);
	boot_step("your character");
	set_character(0, 0); // (the race menu changes it)
	boot_step("loot and lockpicks");
	loot_init(PACK_DIR);
	locks_init(PACK_DIR);
	lockpick_init(PACK_DIR);
	preview_init(PACK_DIR);
	boot_step("voices");
	voice_init(PACK_DIR);
	menu_init(); // (after music/voice/ui: it applies the saved settings to them)
	boot_step("interiors");
	interior_init(PACK_DIR);
	boot_step("music");
	music_init(PACK_DIR);
	boot_step("post-processing");
	post_init();

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);
	// Unit 0: texture x vertex color x 2 (vertex colors hold light at half strength)
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PRIMARY_COLOR);
	glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
	// alpha = texture x vertex alpha (Skyrim's vertex alpha fades foliage/decal edges)
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_PRIMARY_COLOR);
	glAlphaFunc(GL_GREATER, 0.5f);

	// Fixed-function fog: only for the fallback object path (if its shader fails); the world's
	// shaders share the atmosphere model (atmosphere.h)
	float haze[3];
	atmosphere_haze(haze);
	const GLfloat fog[4] = {haze[0], haze[1], haze[2], 1.0f};
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogfv(GL_FOG_COLOR, fog);
	glFogf(GL_FOG_START, FOG_START);
	glFogf(GL_FOG_END, fog_end);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	Player player = {(START_CELL_X + 0.5f) * CELL_SIZE_M, 0.0f, -(START_CELL_Y + 0.5f) * CELL_SIZE_M, 0.0f, 1};
	float yaw = START_YAW, pitch = 0.0f;
	int player_hidden = 0; // third-person camera right up against the body
	Vitals vitals;
	vitals_init(&vitals);
	quests_set_vitals(&vitals); // (a quest may change you: the Companions' blood ritual)
	enchant_init(&vitals); // (Absorb enchantments fill them)
	int can_sprint = 1;
	float dead_t = -1.0f; // seconds since dying (-1: alive)
	int airborne = 0;     // off the ground (not swimming): falling from air_peak
	float air_peak = 0.0f;
	int noclip = 0;
	unsigned buttons_before = 0;
	int stuck_frames = 0; // diagnostics: frames of pushing forward without moving
	// Load the starting area completely before the first frame (placements first, then
	// the object builds and collision they lead to)
	performance_step(0, 0.0f); // (the performance setting from the start: the first load is its size too)
	for (int pass = 0; pass < 4; pass++) {
		char step[64];
		snprintf(step, sizeof(step), "the land around you (%d of 4)", pass + 1);
		boot_step(step);
		terrain_update(player.x, player.z);
		objects_update(player.x, player.z);
		navmesh_update(player.x, player.z);
		// (finished as it loads, the pieces left shown and logged: slow and stuck look different)
		unsigned long long shown = 0, logged = stream_now_us();
		while (stream_pending() > 0) {
			stream_pump(100000);
			const unsigned long long now = stream_now_us();
			if (now - shown > 500000) {
				snprintf(step, sizeof(step), "the land around you (%d of 4): %d pieces left", pass + 1, stream_pending());
				if (now - logged > 5000000) { // (and what the loading is doing: memory, the worker)
					int waiting, worked, busy_ms, total;
					stream_stats(&waiting, &worked, &busy_ms, &total);
					char detail[240];
					snprintf(detail, sizeof(detail), "%s | waiting %d, worked %d, worker %d ms on %s (progress %d), "
					         "%d done | vram %u KB, ram %u KB, phycont %u KB free", step, waiting, worked, busy_ms,
					         stream_working_on(), stream_progress, total,
					         (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024),
					         (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024));
					boot_log(detail), logged = now;
				}
				boot_step_show(step);
				shown = now;
			}
			sceKernelDelayThread(2000);
		}
	}
	terrain_height_at(player.x, player.z, &player.feet);
	boot_log("ready");
	ui_boot(NULL);
	game_log_background(); // (from here the log's lines are written off the game's thread)
	ui_subtitle(NULL, NULL);
	unsigned long long last_frame_us = stream_now_us(), hitch_update_us = 0, hitch_stream_us = 0;

#if DEBUG_TEXTURE_INTEGRITY
	sktex_verify_all("after load");
#endif

	SceCtrlData pad;
	for (int frame = 1;; frame++) {
		perf_frame();
		frame_time(menu_settings()->frame_rate);
		perf_begin(PERF_GAME);
		perf_lap("perf_step");
		dynres_step(real_frame_s, governor_live || racemenu_on); // (in play - and the race menu, Helgen behind it: 26 fps in the
		// user's log - the GPU's late frames bring the world's size down)
		performance_step(frame, governor_live ? frame_seconds() : 0.0f); // (in play: the governor judges its frames)
		// Skyrim's cell resets: going somewhere (and outdoors, now and then), those killed 10 game
		// days ago come back and containers rolled as long ago refill - not ones you keep things in
		perf_lap("cell_resets");
		items_set_clock(lighting_game_hours());
		if (!menu_is_open() && (interior_current() != place_checked || (interior_current() == -1 && frame % 600 == 0))) {
			place_checked = interior_current();
			const float at[3] = {player.x, player.feet, player.z};
			const int back = creatures_respawn(place_checked, at), refilled = items_reset_containers(RESET_HOURS);
			if (back || refilled)
				sceClibPrintf("cells reset: %d creatures back, %d containers refilled\n", back, refilled);
		}
		perf_lap("readout");
		perf_begin(PERF_INPUT);
		if (menu_settings()->readout) {
			static char lines[14][64];
			int n = perf_lines(lines, 12);
			static const char *const level[PERF_LEVELS] = {"Ultra Low", "Low", "Medium", "High"};
			static const char *const rate[3] = {"60", "30", "smoothed"};
			snprintf(lines[n++], 64, "%s, %s fps, up to %dx%d", level[menu_settings()->performance],
			         rate[menu_settings()->frame_rate], post_max_w(), post_max_h());
			if (memory_short)
				snprintf(lines[n++], 64, "memory short: a step lower");
			ui_perf((const char (*)[64])lines, n);
		} else {
			ui_perf(NULL, 0);
		}
		perf_end(PERF_INPUT);
		perf_lap("pad");
		sceCtrlPeekBufferPositive(0, &pad, 1);
		autotest_pad(clock_s, &pad); // (a scripted test run: the script's pad)
		if (walkto_left > 0.0f) { // (autotest "walkto": turned toward the point and walked on)
			while (walkto_i < walkto_n - 1 && hypotf(walkto_pts[walkto_i * 3] - player.x, walkto_pts[walkto_i * 3 + 2] - player.z) < 0.7f)
				walkto_i++; // (a corner reached: on to the next)
			if (walkto_n)
				walkto_at[0] = walkto_pts[walkto_i * 3], walkto_at[1] = walkto_pts[walkto_i * 3 + 2];
			const float dx = walkto_at[0] - player.x, dz = walkto_at[1] - player.z,
			            d = walkto_i < walkto_n - 1 ? 9.0f : hypotf(walkto_goal[0] - player.x, walkto_goal[1] - player.z);
			walkto_left -= 1.0f / 30.0f;
			walkto_still = hypotf(player.x - walkto_last[0], player.z - walkto_last[1]) < 0.02f ? walkto_still + 1.0f / 30.0f : 0.0f;
			walkto_last[0] = player.x, walkto_last[1] = player.z;
			if (d < 0.8f || walkto_left <= 0.0f || walkto_still > 3.0f) {
				game_log("walkto: %s at %.1f %.1f %.1f (wanted %.1f %.1f, %.1f m off; corner %d of %d)", d < 0.8f ? "arrived" : "stuck",
				         player.x, player.feet, player.z, walkto_goal[0], walkto_goal[1], hypotf(walkto_goal[0] - player.x, walkto_goal[1] - player.z),
				         walkto_i, walkto_n);
				for (int k = walkto_i; k < walkto_n && k < walkto_i + 4 && d >= 0.8f; k++)
					game_log("walkto:   corner %d at %.1f %.1f %.1f", k, walkto_pts[k * 3], walkto_pts[k * 3 + 1], walkto_pts[k * 3 + 2]);
				walkto_left = 0.0f;
			} else {
				yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
				pad.lx = 128, pad.ly = 0;
			}
		}
		if (menu_settings()->rear_touch) { // (the back's left half is L, its right half R: menus too)
			static int sampling;
			if (!sampling)
				sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START), sampling = 1;
			SceTouchData touch;
			if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch, 1) > 0)
				for (unsigned k = 0; k < touch.reportNum; k++)
					pad.buttons |= touch.report[k].x < 960 ? SCE_CTRL_LTRIGGER : SCE_CTRL_RTRIGGER;
		}
		// START: pause menu. While it's open the world stands still (dt 0, no input)
		static int was_paused;
		// Before playing: the title (continue the saved game, or a new one) and, for a new
		// one, the race menu (Up / Down race, Left / Right face, CROSS begins). They have the
		// pad; the world stands still behind them.
		static int title_loading; // (the title's Continue: the list of saves open; backing out loads the newest)
		static int stage = -1, title_sel, race_sel, face_sel, front_load;
		static int dialogue_with = -1, dialogue_topics, dialogue_vendor = -1, dialogue_follow = 99, dialogue_service = 99,
		           dialogue_ride = 99, dialogue_end = 99, // (dialogue_end: "Goodbye." - nothing to say)
		           souls_this_frame; // (talking: to whom, its choices)
		static unsigned front_before;
		if (stage < 0) {
			save_init();
			stage = !autotest_active() || autotest_title() ? STAGE_TITLE : STAGE_RACE; // (a test run: a new game)
			title_sel = 0; // (the first of the list: Continue - or with no save yet, New Game)
			front_before = pad.buttons;
		}
		if (stage != STAGE_PLAY) {
			const unsigned now = pad.buttons & ~front_before;
			front_before = pad.buttons;
			static int credits; // (the title's Credits: one page - the game's makers, from the user's own files)
			if (stage == STAGE_TITLE && credits) {
				if (now & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_START)) {
					ui_credits(NULL, 0.0f);
					credits = 0;
				} else {
					ui_menu(NULL, NULL, 0, 0);
					ui_title(0);
					ui_credits("", 0.0f);
				}
			} else if (stage == STAGE_TITLE) {
				static const char *const title_items[3] = {"Continue", "New Game", "Credits"};
				const int has_save = save_latest() >= 0, items = has_save ? 3 : 2;
				if (title_sel >= items)
					title_sel = 0;
				if (now & SCE_CTRL_UP)
					title_sel = (title_sel + items - 1) % items;
				if (now & SCE_CTRL_DOWN)
					title_sel = (title_sel + 1) % items;
				const int pick = title_sel + (has_save ? 0 : 1); // (0 Continue, 1 New Game, 2 Credits)
				if ((now & SCE_CTRL_CROSS) && pick == 2) { // (the game's makers credited: one page)
					credits = 1;
				} else if (now & SCE_CTRL_CROSS) {
					title_sel = pick; // (Continue 0, New Game 1 - as it was)
					ui_menu(NULL, NULL, 0, 0);
					ui_title(0);
					if (title_sel != 0) // (a new character: the race's first preset - not a loaded save's look)
						memset(&player_look, 0, sizeof(player_look)), look_old_picks = 0;
					if (title_sel == 0)
						stage = STAGE_PLAY, title_loading = 2; // (Continue: your saves to pick from, once X is let go) // (Continue: your saves to pick from)
					else if (intro_ready())
						begin_opening = 1; // (Helgen: the race menu comes at INFO 0003F88B)
					else
						stage = STAGE_RACE;
				} else {
					ui_title(1); // (Skyrim's: its name on black; Continue only with a save)
					{ // (the build; and if the data on the card is another build's - the .vpk copied, not installed)
						static char note[112];
						static int checked, mismatch;
						if (!checked) {
							long size = 0;
							char *data = file_read_all(PACK_DIR "build.txt", &size);
							char have[32] = "";
							if (data && size > 0)
								snprintf(have, sizeof(have), "%.*s", (int)(size < 31 ? size : 31), data);
							free(data);
							have[strcspn(have, "\r\n")] = 0;
							mismatch = have[0] && strcmp(have, BUILD_STAMP) != 0;
							if (mismatch)
								snprintf(note, sizeof(note), "Program %s, game data %s: install opensky.vpk again in VitaShell", BUILD_STAMP, have);
							else
								snprintf(note, sizeof(note), "Build %s", BUILD_STAMP);
							game_log("build: program %s, data %s%s", BUILD_STAMP, have[0] ? have : "(none)", mismatch ? " - MISMATCH" : "");
							checked = 1;
						}
						ui_title_note(note, mismatch);
					}
					ui_menu("OpenSKY", title_items + (has_save ? 0 : 1), items, title_sel);
				}
			}
			if (begin_opening) { // A new game from the cart (the character chosen at the lineup)
				begin_opening = 0;
				{ // (the cart's prisoner: the race's made face, a man's, its first preset - Skyrim's)
					const int f = custom_face_of(races_get(race_sel), 0);
					if (f >= 0 && !face_custom(races_get(race_sel), face_sel))
						face_sel = f;
				}
				set_character(race_sel, face_sel);
				quests_new_game(1);
				objects_set_switches(0); // (Helgen as it was that morning)
				stones_set_active(0);
				magic_new_character(); // (Skyrim's first spells: Flames and Healing)
				shouts_new_character(); // (no words yet: the word walls', and a dragon's soul to unlock them)
				quests_load_inactive(NULL), menu_load_favorites(NULL, NULL); // (a new journal, no favourites)
				walls_restore((const uint32_t[2]){0, 0});
				dragon_souls = 0;
				items_set_books_read(NULL, 0);
				equip_strip(); // (a prisoner: no clothes but the rags the cart gives)
				stage = STAGE_PLAY;
				intro_start();
				memset(&opening, 0, sizeof(opening));
				opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
				intro_prime_view(&opening);
				if (opening.eye_set) {
					player.x = opening.pos[0], player.feet = opening.pos[1], player.z = opening.pos[2];
					yaw = opening.look_yaw, pitch = opening.look_pitch;
				}
			}
			if (stage == STAGE_RACE) {
				// Skyrim's race menu (racemenu.c): its tabs along the top - Race, Body, Head, Face, Eyes, Brow,
				// Mouth, Hair - their sliders down the left; START (or Triangle) when done
				if (!racemenu_on) {
					const Race *r0 = races_get(race_sel);
					const int woman = r0 && face_sel < r0->face_count ? face_female(r0, face_sel) : 0;
					racemenu_open(race_sel, woman, player_look.version == 1 ? &player_look : NULL);
					game_log("race menu: %s", opening_race ? "the opening's (\"Who are you?\")" : "a new game's");
					ChargenLook look = *racemenu_look();
					menu_set_body(race_sel, woman, &look);
					racemenu_on = 1;
				}
				menu_body_race = -1;
				int begin = racemenu_update(now, pad.buttons, frame_s, menu_set_body) == RACEMENU_DONE;
				if (menu_body_race >= 0) { // (another race or sex: its body, looked at)
					race_sel = menu_body_race, face_sel = menu_body_face;
					pitch = -8.0f; // (looking at the face: the body turns to the camera - actors_player_face_camera)
				}
				if (opening_race && autotest_active() && autotest_auto_race()) // (a test run past the opening's race menu: as it is)
					begin = 1;
				race_zoom = racemenu_portrait() || test_face_zoom; // (the race list: the whole of you; the rest close)
				race_facial_zoom = racemenu_close_on_face() || test_face_zoom;
				// (the right stick turns you round, to see yourself from every side: half a turn a second)
				race_turn = fmodf(race_turn + stick(pad.rx) * 180.0f * frame_s, 360.0f);
				if (begin) {
					swallow_start = 1; // (START was Done: not the pause menu too)
					player_look = *racemenu_look();
					const Race *r = races_get(race_sel);
					race_zoom = 0;
					race_facial_zoom = 0;
					racemenu_close(), racemenu_on = 0;
					races_new_character(&vitals);
					if (opening_race) { // (Hadvar's INFO 0003F88B answered: your name, then on with the lineup)
						opening_race = 0;
						naming = !autotest_active() && name_ask(T_DIAL_0001F4AC_FULL0); // (a test run: no typing)
						if (!naming)
							intro_race_chosen();
					} else {
						quests_new_game(0); // (Before the Storm: to the Jarl)
						magic_new_character(); // (Skyrim's first spells: Flames and Healing)
						shouts_new_character(), dragon_souls = 0;
						quests_load_inactive(NULL), menu_load_favorites(NULL, NULL);
						walls_restore((const uint32_t[2]){0, 0});
						items_set_books_read(NULL, 0);
					}
					stage = STAGE_PLAY;
					yaw += 180.0f, pitch = 0.0f; // (behind the character again)
					if (r) {
						char text[64];
						snprintf(text, sizeof(text), "You are %s %s", strchr("AEIOU", r->name[0]) ? "an" : "a", r->name);
						ui_notice(text, 4.0f);
					}
				}
			}
		}
		perf_lap("menu_update");
		const int front = stage != STAGE_PLAY;
		perf_begin(PERF_INPUT);
		SceCtrlData menu_pad = pad;
		if (modal_before) // (Up / Down in the keep's choice opened the menus under it: the flicker)
			menu_pad.buttons = 0;
		if (swallow_start && !(pad.buttons & (SCE_CTRL_START | SCE_CTRL_TRIANGLE))) // (the race menu's Done: let go first)
			swallow_start = 0;
		if (swallow_start)
			menu_pad.buttons &= ~(SCE_CTRL_START | SCE_CTRL_TRIANGLE);
		int menu = front ? MENU_PAUSED : menu_update(&menu_pad);
		perf_end(PERF_INPUT);
		if (menu == MENU_QUIT)
			break;
		// Picking a lock: it has the pad, the game stands still
		if (lockpick_active() && menu == MENU_PLAYING) {
			lockpick_update(&pad, frame_s);
			menu = MENU_PAUSED;
			int handle = -1;
			const char *cname = NULL;
			int target = lockpick_take_opened();
			if (target >= LOCK_TARGET && locks_key(target) && (float)(rand() % 100) < perks_apply(EP_MOD_KEY_REWARD, 0.0f, NULL)) {
				const int key = items_by_form(locks_key(target)); // (Wax Key: a copy of the door's key)
				if (key >= 0 && items_have(key) <= 0) {
					items_add(key, 1);
					char text[96];
					snprintf(text, sizeof(text), "%s added", items_name(key));
					ui_notice(text, 3.0f);
				}
			}
			if (target >= LOCK_TARGET && locks_picked(target)) { // (a door's: open, or through it)
				if (lock_door_kind == 3 /* AIM_SWING */ && !doors_is_open(lock_door))
					doors_toggle(lock_door);
				else if (lock_door_kind == 2 /* AIM_DOOR */)
					lock_go_door = lock_door;
				target = -1;
			}
			if (target >= 0 && loot_unlocked(target, &handle, &cname) == 2) {
				quests_container_opened(handle, interior_current());
				menu_open_container(handle, cname);
			}
		}
		const int paused = menu == MENU_PAUSED;
		const int closing = was_paused && !paused; // (the button that closed it isn't a jump)
		if (paused && !was_paused && !front)
			capture_pending = 1; // (the pause menu: its saves get this scene)
		if (paused != was_paused) {
			if (paused) {
				voice_stop();
				ui_subtitle(NULL, NULL);
			}
			music_duck(paused ? 0.5f : 1.0f);
			was_paused = paused;
		}
		// The buttons as mapped (Options: Controls - a layout - and Button mapping): the game reads them as the
		// face-button layout's (play only: the menus keep the pad as it is - L / R for their tabs)
		if (!paused)
			pad.buttons = menu_bind_buttons(pad.buttons);
		static unsigned swallowed; // (buttons held when a menu closed: ignored till let go)
		if (paused || closing) {
			swallowed = pad.buttons;
			pad.buttons = 0;
			pad.lx = pad.ly = pad.rx = pad.ry = 128;
		}
		swallowed &= pad.buttons | (paused || closing ? ~0u : 0u);
		pad.buttons &= ~swallowed;
		perf_lap("controls");
		const float dt = paused ? 0.0f : frame_s * world_slow; // (Steady Hand: slowed, zoomed in)
		const Settings *opts = menu_settings();
		if (effects_paralysed()) { // (a Paralysis: stiff, no control, till it wears off)
			pad.buttons = 0;
			pad.lx = pad.ly = pad.rx = pad.ry = 128;
		}
		if (killmove_left > 0.0f) {
			// Movement lock released instantly when victim HP hits 0 / ragdolls / death animation finishes,
			// or when the player inputs movement after the lethal blow has landed
			if ((killmove_victim >= 0 && creatures_killmove_done(killmove_victim)) ||
			    (killmove_t > 0.8f && (abs((int)pad.lx - 128) > 30 || abs((int)pad.ly - 128) > 30 ||
			     (pad.buttons & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT))))) {
				killmove_left = 0.0f, killmove_victim = -1;
				actors_player_scene(NULL, 0.0f);
			}
		}
		if (dead_t >= 0.0f || killmove_left > 0.0f) { // (dying, or in a killmove: no control - the view's
			pad.buttons = 0;                         //  still yours to turn about a killmove)
			pad.lx = pad.ly = 128;
			if (dead_t >= 0.0f)
				pad.rx = pad.ry = 128;
		}
		static int transition;     // 0 none, 1 fading out to the door, 2 fading in, 3 the loading screen
		static float settle_s;     // ... up this long
		static int settle_good;    // ... frames in a row quick enough
		// Skyrim's opening (intro.c): what it wants of this frame
		memset(&opening, 0, sizeof(opening));
		opening.enter = -1, opening.use_door = -1, opening.prompt_door = -1;
		creatures_hold_spawns(intro_active() && !intro_stage()); // (Helgen before the keep: only the opening's)
		creatures_set_opening_side(intro_side()); // (the keep: the other side's soldiers)
		static int choosing, choice_sel; // (the keep: who to follow - the opening's question)
		// (its own memory of the pad: the choice blanks the pad for the rest of the frame, and with
		// the game's memory blanked too a held Up / Down was a new press every frame)
		static unsigned opening_before;
		const unsigned opening_pressed = pad.buttons & ~opening_before;
		opening_before = pad.buttons;
		if (naming && !name_asking()) { // (your name given: on with the lineup)
			naming = 0;
			intro_race_chosen();
			char text[80];
			snprintf(text, sizeof(text), "You are %s", name_get());
			ui_notice(text, 4.0f);
		}
		if (stage == STAGE_PLAY && !paused && !transition && intro_active() && !naming && !choosing) {
			const float feet_now[3] = {player.x, player.feet, player.z};
			intro_update(frame_s, feet_now, interior_current(), opening_pressed, &opening);
			bound_kept = opening.bound;
		} else if (intro_active()) {
			opening.bound = bound_kept;
			// (a loading or fade-in on, so the opening isn't run: the cart's start still primed each frame - this view
			// is zeroed every frame, and without it the player fell off the seat to the ground and the eye was under the cart)
			if (stage == STAGE_PLAY && !intro_stage())
				intro_prime_view(&opening);
		}
		if (opening.choice)
			choosing = 1, choice_sel = 0;
		if (choosing) { // Who do you follow into the keep?
			static const char *const followers[2] = {"Follow Ralof (the Stormcloaks)", "Follow Hadvar (the Imperial Legion)"};
			if (opening_pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN))
				choice_sel ^= 1;
			if (opening_pressed & SCE_CTRL_CROSS) {
				choosing = 0;
				ui_menu(NULL, NULL, 0, 0);
				intro_choose(choice_sel == 0);
			} else {
				ui_menu("Into Helgen Keep: who will you follow?", followers, 2, choice_sel);
			}
		}
		modal_before = choosing || naming;
		equip_set_locked(opening.bound);
		actors_player_bound(opening.bound);
		if (opening.shake && shout_shake < 0.3f) // (Alduin's fire on Helgen: the view shudders)
			shout_shake = 0.5f;
		if (opening.bound) // (a prisoner's hands: tied)
			pad.buttons &= ~(SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE);
		if (opening.lock_move || naming || choosing) { // (riding, kneeling, a scene: looking around, the pause menu)
			pad.buttons &= naming || choosing ? 0 : SCE_CTRL_START;
			pad.lx = pad.ly = 128;
		}
		if (opening.face)
			yaw = opening.look_yaw, pitch = opening.look_pitch;
		yaw += opening.yaw_turn;
		actors_player_face_view(opening.look_on && opening.lock_move);
		if (opening.look_on) { // (turned toward what's happening: Lokir's run, the block, Alduin)
			// (from the eyes the view is seen from - kneeling at the block, riding the cart - not a standing man's: from 1.2 m
			// too high, Alduin on the tower sat over the top of the view)
			const float ox = opening.eye_set ? opening.eye[0] : player.x, oy = opening.eye_set ? opening.eye[1] : player.feet + EYE_HEIGHT,
			            oz = opening.eye_set ? opening.eye[2] : player.z;
			const float ex = opening.look_at[0] - ox, ey = opening.look_at[1] - oy, ez = opening.look_at[2] - oz;
			const float want_yaw = atan2f(ex, -ez) * 180.0f / (float)M_PI,
			            want_pitch = atan2f(ey, fmaxf(hypotf(ex, ez), 0.1f)) * 180.0f / (float)M_PI;
			float dy = want_yaw - yaw;
			while (dy > 180.0f)
				dy -= 360.0f;
			while (dy < -180.0f)
				dy += 360.0f;
			const float rate = 1.0f - expf(-frame_s * 4.0f);
			yaw += dy * rate, pitch += (fmaxf(-50.0f, fminf(50.0f, want_pitch)) - pitch) * rate;
			static unsigned long long view_told; // (the opening's view, now and then: where it looks, where it wants to)
			if (opening.roll != 0.0f && stream_now_us() - view_told > 3000000ull)
				view_told = stream_now_us(), game_log("view: yaw %.1f pitch %.1f (wants %.1f %.1f), roll %.1f, from %.2f %.2f %.2f", yaw,
				                                      pitch, want_yaw, want_pitch, opening.roll, ox, oy, oz);
		}
		if (opening.race_menu) {
			stage = STAGE_RACE, opening_race = 1;
			float hv[3]; // (the view from Hadvar's side: you face him as you're made - INFO 0003F88B)
			if (intro_face_point(hv))
				yaw = atan2f(player.x - hv[0], -(player.z - hv[2])) * 180.0f / (float)M_PI;
			else
				yaw += 180.0f;
			pitch = -8.0f;
		}

		// The controls (for now; the final layout waits for the real Vita): left stick moves,
		// right stick looks. R: sprint. CROSS: jump. CIRCLE: talk / open doors. SQUARE: first /
		// third person. TRIANGLE: cast the equipped spell (tap; hold for Healing and Flames).
		// D-pad left / right: change spell. D-pad down: raise / put away the torch. L: sneak.
		// START: pause menu (inventory, magic, skills, save, options). SELECT: wait. Debug
		// controls (an option): L held fast travel (instead of sneak), SELECT noclip, R +
		// TRIANGLE held fast-forward time.
		float mx = stick(pad.lx), mz = stick(pad.ly);
		const float look = opts->look_speed / 5.0f;
		float friction = 1.0f; // (aiming a bow or a spell over a foe: the view slows, easier to hold on it)
		if (magic_readied()) {
			const float yr0 = yaw * (float)M_PI / 180.0f, pr0 = pitch * (float)M_PI / 180.0f;
			const float look0[3] = {sinf(yr0) * cosf(pr0), sinf(pr0), -cosf(yr0) * cosf(pr0)};
			const float head0[3] = {player.x, player.feet + EYE_HEIGHT, player.z};
			float mark[3];
			if (creatures_aim_assist(opts->third_person ? last_view : head0, look0, 0.997f, 60.0f, 0.0f, mark))
				friction = AIM_FRICTION;
		}
		yaw += stick(pad.rx) * 2.5f * look * friction * zoom_fov; // (zoomed in: finer)
		pitch -= stick(pad.ry) * 1.8f * look * friction * zoom_fov * (opts->invert_look ? -1.0f : 1.0f);
		if (pitch > 60.0f) pitch = 60.0f;
		if (pitch < -60.0f) pitch = -60.0f;
		if (!isfinite(yaw) || !isfinite(pitch)) { // (gone bad - the compass had nothing on it: logged, put right)
			game_log("view: yaw %f pitch %f gone bad - reset", yaw, pitch);
			yaw = isfinite(yaw) ? yaw : 0.0f, pitch = isfinite(pitch) ? pitch : 0.0f;
		}
		yaw = fmodf(yaw, 360.0f); // (kept in a turn: never so large the compass's sums lose it)
		if (!spell_sounds_ran) { // (the spells' block didn't run last frame: their loops stop with it)
			if (flames_voice >= 0)
				sfx_stop(flames_voice), flames_voice = -1;
			if (heal_voice >= 0)
				sfx_stop(heal_voice), heal_voice = -1;
			if (spark_voice >= 0)
				sfx_stop(spark_voice), spark_voice = -1;
		}
		spell_sounds_ran = 0;
		perf_lap("seat_bed");
		unsigned pressed_now = pad.buttons & ~buttons_before;
		buttons_before = pad.buttons;
		// Sitting: the seat's clips move you; the stick, CIRCLE or CROSS gets you up
		float seat_pos[3], seat_yaw;
		const int seated = actors_player_seated(seat_pos, &seat_yaw);
		// In bed: lying down, the Sleep menu, then getting up once it's closed
		static int in_bed, bed_menu;
		if (seated == 2 && in_bed && !bed_menu) {
			menu_open_sleep();
			bed_menu = 1;
		} else if (seated == 2 && in_bed && bed_menu && !menu_is_open()) {
			actors_player_stand();
			in_bed = bed_menu = 0;
		}
		if (!seated)
			in_bed = bed_menu = 0;
		if (seated) {
			if (seated == 2 && !in_bed &&
			    (fabsf(mx) > 0.5f || fabsf(mz) > 0.5f || (pressed_now & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS))))
				actors_player_stand();
			mx = mz = 0.0f;
			pressed_now &= ~(unsigned)(SCE_CTRL_CIRCLE | SCE_CTRL_CROSS);
		}

		if ((pressed_now & SCE_CTRL_DOWN) && (pad.buttons & SCE_CTRL_RTRIGGER) && opts->debug)
			noclip = !noclip; // (debug: R + D-pad down)
		// SELECT: the power or shout in the voice slot (Skyrim's shout button). A shout is held
		// for its words (shouts_update, below); the race's power goes off at once
		perf_lap("shouts_power");
		creatures_set_player_damage_mult(menu_damage_dealt_mult() * (power_is(T_SPEL_000AA026_FULL0) ? 2.0f : 1.0f)); // (the difficulty's)
		{ // (a power's minute: Ancestor's Wrath's fire on those beside you, each second; and its end said)
#define WRATH_REACH_M 3.2f
#define WRATH_DPS 8.0f
			static char running[40];
			static float wrath_t;
			float left = 0.0f;
			const char *on = paused ? running : races_power_running(&left, NULL);
			if (on && on != running && strcmp(on, running))
				snprintf(running, sizeof(running), "%s", on), wrath_t = 0.0f;
			if (!on && running[0]) {
				char text[64];
				snprintf(text, sizeof(text), "%s has worn off", running);
				ui_notice(text, 3.0f);
				running[0] = 0;
			}
			if (on && !paused && !strcmp(on, T_MGEF_000B8F30_FULL0) && (wrath_t -= frame_s) <= 0.0f) {
				wrath_t = 1.0f;
				const float feet[3] = {player.x, player.feet, player.z}, chest[3] = {player.x, player.feet + 1.0f, player.z};
				const int hit = creatures_area(feet, WRATH_REACH_M, WRATH_DPS * menu_damage_dealt_mult());
				spellfx_burst(chest, ELEMENT_FIRE, 1.3f); // (the fire about you, seen)
				if (hit)
					sfx_play("mag_fire_hit", 0.5f, 0.0f);
			}
		}
		{ // (Fortify Sneak; sneaking, Stealth's 20% .. 40% harder to see)
			PerkContext sctx = {0};
			sctx.school = -1, sctx.sneaking = 1;
			const float stealth = sneaking_now ? perks_apply(EP_MOD_DETECTION_SNEAK, 0.0f, &sctx) / 100.0f : 0.0f;
			creatures_set_player_visibility(effects_invisible() ? 0.0f
			                                : fmaxf(0.1f, (1.0f - effects_skill(SK_SNEAK) / 100.0f) * (1.0f - fminf(0.9f, stealth))));
		}
		// (The words known: as learned at word walls and unlocked with dragon souls - debug controls
		// no longer give every word: switched on by mistake, they were kept in every save made after)
		shouts_set_fast_recovery(opts->debug || test_fast_shouts);
		shouts_set_recovery_mult(effects_shout_recovery_mult()); // (Fortify Shouts)
		if ((pressed_now & SCE_CTRL_SELECT) && dead_t < 0.0f && shouts_equipped() < 0 && beast_readied()) {
			if (beast_active()) // (Skyrim's: no going back till it wears off)
				ui_notice("You are already in beast form", 2.0f);
			else if (riding_mounted() || seated)
				ui_notice("You can't change here", 2.0f);
			else if (!beast_change(&vitals, 0))
				ui_notice("You can only use a power once a day", 3.0f);
		} else if ((pressed_now & SCE_CTRL_SELECT) && dead_t < 0.0f && shouts_equipped() < 0) {
			if (!races_power_name())
				ui_notice("No power or shout ready", 2.0f);
			else if (races_use_power()) {
				ui_notice(races_power_name(), 3.0f);
				const float at[3] = {player.x, player.feet, player.z};
				use_power_effect(races_power_name(), at);
			} else
				ui_notice("You can only use a power once a day", 3.0f);
		}
		if (!opts->debug)
			noclip = 0;
		if ((pressed_now & SCE_CTRL_CROSS) && !noclip)
			collision_jump(&player);
		const int cross_tap = pressed_now & SCE_CTRL_CIRCLE; // (CIRCLE: talk / doors)
		// Over the carry weight: no running (Skyrim's over-encumbered)
		perf_lap("worldmap_carry");
		if (interior_current() < 0) { // (the map: where you are; places near you discovered)
			menu_set_player(player.x, player.z, yaw, 1);
			if (!(intro_active() && !intro_stage())) // (Skyrim's opening: no places discovered from the cart or in Helgen - not till you're out)
				worldmap_update(player.x, player.z);
			menu_set_local(0.0f, 0.0f, 0.0f, 0.0f, NULL);
		} else { // (inside: the place's own map)
			menu_set_local(player.x, player.feet, player.z, yaw, interior_place());
		}
		perf_lap("sneak_view");
		const float carry_max = items_carry_max(&vitals) + perks_av(AV_CARRY_WEIGHT) + effects_av(AV_CARRY_WEIGHT);
		const float carried = carried_weight();
		const int encumbered = carried > carry_max;
		menu_set_carry_max(carry_max, carried);
		const int sprinting = (pad.buttons & SCE_CTRL_RTRIGGER) != 0 && can_sprint && !encumbered; // (stamina allowing)
		// L: sneak (debug controls: L held is fast travel instead)
		static int sneaking;
		{ // (Shadow Warrior: crouching mid-fight - the distant ones lose you for a moment, as Skyrim's)
			static int was_sneaking;
			static float shadow_t;
			shadow_t -= frame_s;
			if (sneaking && !was_sneaking && shadow_t <= 0.0f && perks_has_form(PERK_SHADOW_WARRIOR) &&
			    creatures_hostile_near((const float[3]){player.x, player.feet, player.z}, 40.0f)) {
				const int lost = creatures_lose_track((const float[3]){player.x, player.feet, player.z}, SHADOW_NEAR_M);
				shadow_t = SHADOW_EVERY_S;
				game_log("perks: Shadow Warrior - %d lose you", lost);
			}
			was_sneaking = sneaking;
		}
		sneaking_now = sneaking;
		{ // (Silent Roll: sprinting while sneaking - a roll forward, no sound of it)
			static int roll_held;
			silent_roll_t -= frame_s;
			const int sprint_down = (pad.buttons & SCE_CTRL_RTRIGGER) != 0;
			if (sneaking && sprint_down && !roll_held && silent_roll_t < -SILENT_ROLL_AGAIN_S && can_sprint &&
			    perks_has_form(PERK_SILENT_ROLL)) {
				silent_roll_t = SILENT_ROLL_S;
				vitals.stamina = fmaxf(0.0f, vitals.stamina - SILENT_ROLL_STAMINA), vitals.shown_for = 0.0f;
			}
			roll_held = sprint_down;
		}
		if ((pressed_now & SCE_CTRL_LTRIGGER) && !opts->debug)
			sneaking = !sneaking;
		// D-pad down: first / third person
		if ((pressed_now & SCE_CTRL_DOWN) && !(opts->debug && (pad.buttons & SCE_CTRL_RTRIGGER)))
			menu_toggle_camera();
		// D-pad left / right: what the left / right hand holds, in turn (nothing, the torch
		// (left hand only), each spell)
		perf_lap("torch_hands");
		int torch_worn = 0;
		const int torch_i = find_torch(&torch_worn);
		// (debug: R + D-pad left calls a dragon in, to try them)
		if ((pressed_now & SCE_CTRL_LEFT) && (pad.buttons & SCE_CTRL_RTRIGGER) && opts->debug) {
			const float at[3] = {player.x, player.feet, player.z};
			ui_notice(interior_current() == -1 && creatures_summon_dragon(at) ? "A dragon is coming" : "No dragon can come here",
			          2.5f);
			pressed_now &= ~(unsigned)SCE_CTRL_LEFT;
		}
		// (a tap steps the hand on - through your favourite spells, or every spell learned if none is; held, Skyrim's
		// favourites list opens, time stopped: a long list of spells no longer stepped through one at a time)
		static float dpad_held_t;
		static unsigned dpad_held;
		unsigned dpad_tap = 0;
		if (pressed_now & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))
			dpad_held = pressed_now & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT), dpad_held_t = 0.0f;
		if (dpad_held && (pad.buttons & dpad_held)) {
			dpad_held_t += frame_s;
			if (dpad_held_t >= FAVORITES_HOLD_S && !menu_is_open()) {
				menu_open_favorites();
				dpad_held = 0;
			}
		} else if (dpad_held) { // (let go: a tap)
			dpad_tap = dpad_held, dpad_held = 0;
		}
		if (dpad_tap) {
			const int h = dpad_tap & SCE_CTRL_LEFT ? HAND_LEFT : HAND_RIGHT;
			// -2 nothing, -1 the torch, 0.. a spell
			int now = h == HAND_LEFT && torch_worn ? -1 : magic_hand(h) >= 0 ? magic_hand(h) : -2;
			int any_fav = 0, next;
			const int first_fav = menu_next_favorite_spell(-1, &any_fav);
			if (any_fav) { // (nothing, the torch (left hand), then the favourites in turn, and round again)
				if (now == -2)
					next = h == HAND_LEFT && torch_i >= 0 ? -1 : first_fav;
				else if (now == -1)
					next = first_fav;
				else {
					next = menu_next_favorite_spell(now, &any_fav);
					if (next < 0)
						next = -2;
				}
			} else {
				next = now + 1 >= magic_count() ? -2 : now + 1;
				if (next == -1 && (h == HAND_RIGHT || torch_i < 0))
					next = 0;
				while (next >= 0 && next < magic_count() && !magic_known(next)) // (only spells learned)
					next = next + 1 >= magic_count() ? -2 : next + 1;
			}
			if (h == HAND_LEFT && torch_i >= 0) {
				actors_player_set_worn(torch_i, next == -1);
				if (next == -1)
					equip_free_left_hand(); // (no shield, bow or two-handed weapon with it)
			}
			torch_worn = h == HAND_LEFT ? next == -1 : torch_worn;
			magic_set_hand(h, next);
			if (next >= 0)
				magic_set_readied(1);
			char text[48];
			snprintf(text, sizeof(text), "%s hand: %s", h == HAND_LEFT ? "Left" : "Right",
			         next == -2 ? "nothing" : next == -1 ? "Torch" : magic_name(next));
			ui_notice_now(text, h == HAND_LEFT ? "Left hand" : "Right hand", 1.5f);
		}
		(void)dpad_tap;
		// (Skyrim's opening is seen in first person till you're asked who you are)
		const int third_person = (opts->third_person && !intro_first_person()) || stage == STAGE_RACE || killmove_left > 0.0f ||
		                         beast_active();
		view_fov = (float)(third_person ? opts->fov_third : opts->fov_first); // (the options' field of view)
		if (view_fov < 50.0f || view_fov > 90.0f)
			view_fov = FOV_Y; // (Beast Form: seen from behind, as Skyrim's)
		// TRIANGLE / SQUARE: the right / left hand casts (debug controls: R + TRIANGLE held
		// fast-forwards time instead)
		const int time_fast = opts->debug && sprinting && (pad.buttons & SCE_CTRL_TRIANGLE);
		// D-pad up: ready / put away the spell (in water it's put away). Debug controls: R +
		// D-pad up cycles the weather instead
		if ((pressed_now & SCE_CTRL_UP) && opts->debug && (pad.buttons & SCE_CTRL_RTRIGGER)) {
			char text[48];
			snprintf(text, sizeof(text), "Weather: %s", weather_cycle());
			ui_notice(text, 2.0f);
		} else if (pressed_now & SCE_CTRL_UP)
			magic_set_readied(!magic_readied());
		if (player.swimming)
			magic_set_readied(0);
		// The torch is held in the left hand: whichever went into it last (the torch or a
		// spell, from the menus) puts the other away
		{
			static int had_torch;
			torch_worn = 0;
			find_torch(&torch_worn);
			if (torch_worn && magic_hand(HAND_LEFT) >= 0) {
				if (!had_torch)
					magic_set_hand(HAND_LEFT, -1);
				else
					actors_player_set_worn(torch_i, 0), torch_worn = 0;
			}
			had_torch = torch_worn;
		}
		// A spell put in a hand that holds a weapon (D-pad / Magic menu) puts the weapon away
		{
			int kind, weapon = equip_weapon(&kind);
			if (weapon >= 0 && (magic_hand(HAND_RIGHT) >= 0 || (kind >= 5 && magic_hand(HAND_LEFT) >= 0)))
				equip_toggle(weapon);
			if (magic_hand(HAND_LEFT) >= 0 || torch_worn) { // (a spell or the torch in the left hand: its weapon goes,
				equip_free_left_weapon();                    //  and its shield - as Skyrim's one hand holds one thing)
				if (equip_shield_worn())
					equip_free_left_hand();
			}
			equip_drop_uncarried();
			equip_set_drawn(magic_readied());
			// Drawing / sheathing: the weapon's sound, or the readied spell's
			static int was_drawn;
			const int drawn = magic_readied();
			if (drawn != was_drawn && !front) {
				weapon = equip_weapon(&kind);
				static const char *const kinds[10] = {"unarmed", "blade1hand", "blade1hand", "axe1hand", "mace1hand",
				                                      "blade2hand", "blunt2hand", "bow", "unarmed", "bow"};
				char event[40];
				if (weapon >= 0 && kind >= 0 && kind < 10)
					snprintf(event, sizeof(event), "wpn_%s_%s", kinds[kind], drawn ? "draw" : "sheathe");
				else {
					const int sp = magic_hand(HAND_RIGHT) >= 0 ? magic_hand(HAND_RIGHT) : magic_hand(HAND_LEFT);
					snprintf(event, sizeof(event), "%s", sp == SPELL_CANDLELIGHT ? "mag_ready_alteration"
					         : sp == SPELL_HEALING ? "mag_ready_restoration" : sp >= 0 ? "mag_ready_destruction"
					         : drawn ? "wpn_unarmed_draw" : "wpn_unarmed_sheathe");
				}
				sfx_play(event, 0.7f, 0.0f);
			}
			was_drawn = drawn;
		}
		perf_lap("cast_input");
		const int can_cast = !time_fast && !player.swimming && ethereal_left <= 0.0f && !riding_mounted() && !beast_active(); // (nor as the beast) // (ethereal: harming nothing; no spells from the saddle, as Skyrim's)
		int cast_pressed[2] = {can_cast && (pressed_now & SCE_CTRL_SQUARE), can_cast && (pressed_now & SCE_CTRL_TRIANGLE)};
		if ((cast_pressed[0] && magic_hand(HAND_LEFT) >= 0) || (cast_pressed[1] && magic_hand(HAND_RIGHT) >= 0))
			cast_this_frame = 1; // (a spell cast: a brawl's off)
		int cast_held[2] = {can_cast && (pad.buttons & SCE_CTRL_SQUARE), can_cast && (pad.buttons & SCE_CTRL_TRIANGLE)};
		// Fighting: with the hands up and no spell in the right hand, TRIANGLE swings its weapon
		// (or a fist), held a moment a power attack; SQUARE held blocks (with a shield, the
		// weapon or the fists) unless a spell is in the left hand
		perf_lap("combat_bow");
		int blocking = 0;
		{
			int wkind;
			int weapon = equip_weapon(&wkind);
			if (beast_active()) // (Beast Form: the claws)
				weapon = -1, wkind = -1;
			// (from the saddle too - Skyrim's mounted combat: the right hand's weapon or bow, no guard, no off hand)
			const int mounted = riding_mounted();
			const int fight = (magic_readied() || beast_active()) && !player.swimming && !seated && dead_t < 0.0f && !paused && ethereal_left <= 0.0f &&
			                  (!mounted || weapon >= 0);
			static float held_s, swing_len;
			static int was_held, side, hit_pending, swing_power, swing_kind, swing_weapon;
			static uint32_t swing_power_kind;
			const int bow = weapon >= 0 && (wkind == 7 || wkind == 9);
			// The guard up with something to bash with (a shield, the weapon, the torch): TRIANGLE bashes
			const int shield_up = equip_shield_worn();
			const int guard_up = fight && !mounted && !beast_active() && (pad.buttons & SCE_CTRL_SQUARE) && magic_hand(HAND_LEFT) < 0 && lweapon_now() < 0 &&
			                     !bow && (weapon >= 0 || shield_up || torch_worn);
			const int held = fight && magic_hand(HAND_RIGHT) < 0 && (pad.buttons & SCE_CTRL_TRIANGLE) && !guard_up;
			int start = 0, power = 0;
			if (held && !bow) {
				held_s += frame_s;
				if (held_s >= POWER_HOLD_S && held_s - frame_s < POWER_HOLD_S)
					start = power = 1;
			} else {
				if (was_held && held_s < POWER_HOLD_S && fight && !bow && !guard_up)
					start = 1;
				held_s = 0.0f;
			}
			// A bow (Skyrim's): TRIANGLE held draws an arrow back (its nock and pull), let go
			// it flies - to what the crosshair is on - for the bow's and arrow's damage, less if
			// it wasn't drawn far; the perks (Overdraw...) and Archery add to it
			static float draw_s = -1.0f;
			if (bow && held) {
				if (draw_s < 0.0f) {
					if (equip_ammo() < 0) {
						if (!was_held)
							ui_notice("No arrows equipped", 2.0f);
					} else {
						draw_s = 0.0f;
						sfx_play("bow_nock", 0.6f, 0.0f);
						sfx_play("bow_pull", 0.5f, 0.0f);
					}
				} else {
					draw_s += frame_s;
				}
			} else if (draw_s >= 0.0f && (!bow || !fight)) {
				draw_s = -1.0f; // (put away or unequipped mid-draw: the arrow goes back, unshot)
			} else if (draw_s >= 0.0f) {
				const float full = actors_player_bow_draw_length() * BOW_DRAW_SHARE /
				                   (perks_has_form(PERK_QUICK_SHOT) ? 1.3f : 1.0f); // (Quick Shot: 30% faster)
				const float drawn = fminf(1.0f, draw_s / full);
				const int arrow = equip_ammo();
				if (drawn >= BOW_MIN_DRAW && arrow >= 0 && weapon >= 0) {
					float speed, gravity;
					const int model = items_flight(arrow, &speed, &gravity);
					const PerkContext bctx = weapon_context(weapon, 0);
					{ // (unseen: Skyrim's x2 an arrow - Deadly Aim's x3)
						PerkContext sctx = bctx;
						sctx.sneaking = 1;
						creatures_set_sneak_attack(perks_apply(EP_MOD_SNEAK_ATTACK_MULT, 3.0f, &sctx),
						                           perks_apply(EP_MOD_SNEAK_ATTACK_MULT, 2.0f, &sctx));
					}
					const float dmg = with_criticals(weapon, &bctx, perks_apply(EP_MOD_ATTACK_DAMAGE,
					                              (items_stat(weapon) + items_stat(arrow)) *
					                              (1.0f + skills_state()->level[SK_ARCHERY] / 200.0f) * drawn * fortify(SK_ARCHERY), &bctx));
					creatures_set_armor_pierce(0.0f);
					// Aimed where the view looks: at what's under the crosshair, from the eye
					const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
					const float look[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)};
					const float from[3] = {player.x + look[0] * 0.5f, player.feet + EYE_HEIGHT - 0.1f, player.z + look[2] * 0.5f};
					const float *cam = third_person ? last_view : from;
					const float far_pt[3] = {cam[0] + look[0] * 80.0f, cam[1] + look[1] * 80.0f, cam[2] + look[2] * 80.0f};
					const float t = collision_ray_ground(cam, far_pt);
					float dir[3] = {cam[0] + (far_pt[0] - cam[0]) * t - from[0], cam[1] + (far_pt[1] - cam[1]) * t - from[1],
					                cam[2] + (far_pt[2] - cam[2]) * t - from[2]};
					float mark[3]; // (aim assist: loosed this near something, the arrow goes to it, its drop allowed for)
					if (creatures_aim_assist(cam, look, AIM_ASSIST_COS, 60.0f, speed * (0.5f + 0.5f * drawn), mark)) {
						const float flight = sqrtf((mark[0] - from[0]) * (mark[0] - from[0]) + (mark[2] - from[2]) * (mark[2] - from[2])) /
						                     fmaxf(speed * (0.5f + 0.5f * drawn), 1.0f);
						dir[0] = mark[0] - from[0], dir[2] = mark[2] - from[2];
						dir[1] = mark[1] - from[1] + 0.5f * gravity * 9.81f * flight * flight * hypotf(dir[0], dir[2]) /
						         fmaxf(hypotf(dir[0], dir[2]), 0.01f);
					}
					const float dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
					for (int k = 0; k < 3; k++)
						dir[k] = dl > 0.5f ? dir[k] / dl : look[k];
					const int shot = projectiles_fire(from, dir, speed * (0.5f + 0.5f * drawn), gravity, model, dmg, 1, 0, arrow);
					projectiles_set_poison(shot, equip_take_poison(EQUIP_POISON_ARROW)); // (a poisoned arrow)
					projectiles_set_bow(shot, weapon);
					items_add(arrow, -1);
					actors_player_attack("bow_release");
					sfx_play("bow_fire", 0.7f, 0.0f);
				}
				draw_s = -1.0f;
			}
			// Bashing (Skyrim's): the guard up, TRIANGLE - a shove of the shield, the weapon, the
			// torch (a bow can't block: its SQUARE bashes) - a little harm (half a blow's) and a stagger
			// that breaks their blow. Power Bash: held, a power bash (1.5 times, a harder stagger;
			// Disarming Bash may take their weapon). Deadly Bash: five times the harm. Bashing trains Block
			static float bash_s = -1.0f, bash_len;
			static int bash_pending, bash_power;
			{
				const int ready = bow ? fight && draw_s < 0.0f : guard_up;
				const int down = (pad.buttons & (bow ? SCE_CTRL_SQUARE : SCE_CTRL_TRIANGLE)) != 0;
				const int can_power = perks_has_form(PERK_POWER_BASH);
				int go = 0, pw = 0;
				if (ready && down && actors_player_attack_time() < 0.0f) {
					bash_s = bash_s < 0.0f ? 0.0f : bash_s + frame_s;
					if (!can_power)
						go = bash_s == 0.0f; // (on the press)
					else if (bash_s >= POWER_HOLD_S && bash_s - frame_s < POWER_HOLD_S)
						go = pw = 1;
				} else if (!down) {
					if (ready && can_power && bash_s >= 0.0f && bash_s < POWER_HOLD_S) // (tapped: a bash)
						go = 1;
					bash_s = -1.0f;
				}
				if (go) {
					if (pw && vitals.stamina < POWER_BASH_STAMINA)
						pw = 0; // (too tired: a bash)
					const char *set = shield_up ? "shd" : bow ? "bow" : torch_worn ? "tor" : wkind == 5 ? "w2hm"
					                  : wkind == 6 || wkind == 8 ? "w2hw" : "w1h";
					char clip[24];
					snprintf(clip, sizeof(clip), pw ? "%s_bash_power" : "%s_bash", set);
					bash_len = actors_player_attack(clip);
					game_log("combat: %s (%.2f s), stamina %.0f", clip, bash_len, vitals.stamina);
					if (bash_len > 0.0f) {
						bash_pending = 1, bash_power = pw;
						vitals.stamina = fmaxf(0.0f, vitals.stamina - (pw ? POWER_BASH_STAMINA : BASH_STAMINA));
						vitals.shown_for = 0.0f;
						if (pw)
							player_grunt("attack");
						sfx_play("wpn_swing_unarmed", 0.6f, 0.0f);
					}
				}
				const float bt = actors_player_attack_time();
				if (bt < 0.0f)
					bash_pending = 0;
				if (bash_pending && bt >= bash_len * HIT_FRACTION) { // (it lands)
					bash_pending = 0;
					const float base = weapon >= 0 ? (float)items_stat(weapon) : UNARMED_DAMAGE;
					const float dmg = perks_apply(EP_MOD_BASH_DAMAGE, base * (bash_power ? BASH_POWER_MULT : BASH_MULT), NULL);
					const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
					const float look[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)};
					const float eye[3] = {player.x, player.feet + EYE_HEIGHT, player.z};
					if (creatures_hit(eye, look, BASH_REACH_M, HIT_CONE_COS, dmg)) {
						const int disarm = bash_power && perks_has_form(PERK_DISARMING_BASH) && rand() % 100 < DISARM_BASH_CHANCE;
						const int disarmed = creatures_bash_struck(disarm) == 2;
						if (torch_worn && !shield_up) { // (the torch's fire on them)
							float at[3];
							if (creatures_magic_struck(TORCH_BASH_FIRE, at))
								spellfx_burst(at, ELEMENT_FIRE, 0.4f);
						}
						effects_end_invisibility();
						skills_use(SK_BLOCK, dmg);
						sfx_play(shield_up ? "wpn_block_shield" : "wpn_hit_blunt", 0.8f, 0.0f);
						const char *who = NULL;
						float left = 0.0f;
						creatures_target(&who, &left);
						game_log("combat: %s for %.1f on %s (%.0f left)%s", bash_power ? "power bash" : "bash", dmg, who ? who : "?", left,
						         disarmed ? ", disarmed" : "");
					} else if (people_hit(eye, look, BASH_REACH_M, HIT_CONE_COS, dmg)) {
						sfx_play(shield_up ? "wpn_block_shield" : "wpn_hit_blunt", 0.8f, 0.0f);
					}
				}
			}
			actors_player_bow(bow && draw_s >= 0.0f ? draw_s : -1.0f);
			bow_nocked = bow && draw_s >= 0.0f ? equip_ammo() : -1;
			{ // Eagle Eye: Block held while drawn zooms in (Skyrim's 0.6 of the view); Steady Hand slows time
				const float want = bow && draw_s >= 0.0f && (pad.buttons & SCE_CTRL_SQUARE)
				                   ? perks_apply(EP_MOD_BOW_ZOOM, 1.0f, NULL) : 1.0f;
				zoom_fov += (want - zoom_fov) * fminf(1.0f, frame_s * 8.0f);
				world_slow = want < 1.0f ? perks_has_form(PERK_STEADY_HAND_2) ? 0.5f
				             : perks_has_form(PERK_STEADY_HAND_1) ? 0.75f : 1.0f : 1.0f;
			}
			was_held = held;
			const PerkContext wctx = weapon_context(weapon, sprinting || mz < -0.5f ? POWER_FORWARD : mz > 0.5f ? POWER_BACKWARD
			                                                : fabsf(mx) > 0.5f ? POWER_SIDEWAYS : POWER_STANDING);
			const float power_stamina = perks_apply(EP_MOD_POWER_ATTACK_STAMINA, POWER_STAMINA, &wctx);
			// (pressed during a swing: kept a moment and swung as soon as it ends - Skyrim chains
			// blows so; dropped, the next press felt lost)
			static float queued_s;
			static int queued_power;
			if (start && actors_player_attack_time() >= 0.0f)
				queued_s = SWING_QUEUE_S, queued_power = power;
			else if (!start && queued_s > 0.0f && actors_player_attack_time() < 0.0f && fight && !bow)
				start = 1, power = queued_power, queued_s = 0.0f;
			queued_s = fmaxf(0.0f, queued_s - frame_s);
			if (start && actors_player_attack_time() < 0.0f && !(lweapon_now() >= 0 && (pad.buttons & SCE_CTRL_SQUARE))) {
				if (power && vitals.stamina < power_stamina) {
					power = 0; // (too tired: a normal swing)
				}
				const char *set = weapon < 0 ? "h2h" : wkind == 5 ? "w2hm" : wkind == 6 || wkind == 8 ? "w2hw" : "w1h";
				char clip[24];
				if (mounted) { // (from the saddle: Skyrim's mounted swing, to one side then the other)
					snprintf(clip, sizeof(clip), "horse_atk_%c", (side ^= 1) ? 'r' : 'l');
					game_log("combat: a swing from the saddle (%s)", clip);
				} else if (power && weapon >= 0)
					snprintf(clip, sizeof(clip), "%s_power", set);
				else
					snprintf(clip, sizeof(clip), "%s_atk_%c", set, (side ^= 1) ? 'r' : 'l');
				int km_began = 0;
				if (!mounted && !beast_active() && !brawl_spar() && killmove_left <= 0.0f && (weapon < 0 || !items_staff(weapon))) {
					const int sk = wkind >= 5 ? SK_TWO_HANDED : SK_ONE_HANDED;
					const float base = weapon >= 0 ? (float)items_stat(weapon) : UNARMED_DAMAGE;
					const float pred = base * (1.0f + skills_state()->level[sk] / 200.0f) * (power ? 2.0f : 1.0f);
					const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
					const float look[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)};
					const float eye[3] = {player.x, player.feet + EYE_HEIGHT, player.z};
					const float feet_now[3] = {player.x, player.feet, player.z};
					km_began = killmove_on_swing(weapon < 0 ? -1 : wkind, pred, eye, look,
					                             (wkind >= 5 ? REACH_2H_M : REACH_1H_M), feet_now, yaw);
				}
				swing_len = km_began ? 0.0f : actors_player_attack(clip);
				if (swing_len > 0.0f && beast_active()) // (the werewolf's claws: its own clip drawn for yours)
					beast_strike(power);
				if (swing_len > 0.0f) {
					hit_pending = 1, swing_power = power, swing_kind = weapon < 0 ? -1 : wkind, swing_weapon = weapon;
					swing_power_kind = wctx.power_attack;
					if (power) {
						vitals.stamina -= power_stamina, vitals.shown_for = 0.0f;
						player_grunt("attack");
					}
					if (!beast_active())
						sfx_play(weapon < 0 ? "wpn_swing_unarmed" : wkind >= 5 ? "wpn_swing_2h" : wkind == 3 ? "wpn_swing_axe"
						         : "wpn_swing_blade", 0.6f, 0.0f);
				}
			}
			// The left hand's weapon (dual wielding): SQUARE swings it as TRIANGLE does the right's,
			// held a power attack; both together a dual attack (Skyrim's), both held a dual power one
			int lkind;
			const int lweapon = beast_active() ? -1 : equip_left_weapon(&lkind);
			static float lheld_s;
			static int lwas_held, lside, lhit_pending, lswing_power;
			const int lheld = fight && !mounted && lweapon >= 0 && (pad.buttons & SCE_CTRL_SQUARE);
			int lstart = 0, lpower = 0;
			if (lheld) {
				lheld_s += frame_s;
				if (lheld_s >= POWER_HOLD_S && lheld_s - frame_s < POWER_HOLD_S)
					lstart = lpower = 1;
			} else {
				if (lwas_held && lheld_s < POWER_HOLD_S && fight)
					lstart = 1;
				lheld_s = 0.0f;
			}
			lwas_held = lheld;
			if (lweapon >= 0 && actors_player_attack_time() < 0.0f && (lstart || (start && lheld))) {
				const int dual = weapon >= 0 && wkind <= 4 && (held || start);
				const int pw = (lpower || power) && vitals.stamina >= power_stamina;
				char clip[24];
				if (dual)
					snprintf(clip, sizeof(clip), "%s", pw ? "dw_power" : "dw_atk");
				else
					snprintf(clip, sizeof(clip), "%s", pw ? "w1h_power_lh" : (lside ^= 1) ? "w1h_atk_lh" : "w1h_atk_lh2");
				swing_len = actors_player_attack(clip);
				if (swing_len > 0.0f) {
					lhit_pending = 1, lswing_power = pw;
					if (dual) // (the right hand's blow too)
						hit_pending = 1, swing_power = pw, swing_kind = wkind, swing_weapon = weapon,
						swing_power_kind = wctx.power_attack;
					if (pw)
						vitals.stamina -= power_stamina * (dual ? 1.5f : 1.0f), vitals.shown_for = 0.0f;
					sfx_play(lkind == 3 ? "wpn_swing_axe" : "wpn_swing_blade", 0.6f, 0.0f);
				}
				start = 0;
			}
			const float at = actors_player_attack_time();
			if (at < 0.0f)
				hit_pending = lhit_pending = 0;
			if (lhit_pending && at >= swing_len * HIT_FRACTION) { // (the left hand's blow lands)
				lhit_pending = 0;
				const float base = (float)items_stat(lweapon);
				const PerkContext lctx = weapon_context(lweapon, 0);
				{ // (unseen: Skyrim's x3 a blow - Backstab's x6, Assassin's Blade's x15 a dagger)
					PerkContext sctx = lctx;
					sctx.sneaking = 1;
					creatures_set_sneak_attack(perks_apply(EP_MOD_SNEAK_ATTACK_MULT, 3.0f, &sctx), 2.0f);
				}
				float dmg = with_criticals(lweapon, &lctx, perks_apply(EP_MOD_ATTACK_DAMAGE,
				                           base * (1.0f + skills_state()->level[SK_ONE_HANDED] / 200.0f) * fortify(SK_ONE_HANDED), &lctx));
				if (lswing_power)
					dmg = perks_apply(EP_MOD_POWER_ATTACK_DAMAGE, dmg * 2.0f, &lctx);
				const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
				const float look[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)};
				const float eye[3] = {player.x, player.feet + EYE_HEIGHT, player.z};
				if (lweapon >= 0)
					activators_strike(eye, look, REACH_1H_M + 0.6f); // (a spider's web in the way: cut down)
				const int lstruck = creatures_hit(eye, look, REACH_1H_M, HIT_CONE_COS, dmg);
				if (!lstruck && people_hit(eye, look, REACH_1H_M, HIT_CONE_COS, dmg)) // (a townsperson)
					sfx_play(lkind == 4 ? "wpn_hit_blunt" : "wpn_hit_blade", 0.8f, 0.0f);
				if (items_staff(lweapon)) // (a staff in the left hand: its spell)
					magic_staff_blow(lweapon, lstruck, &vitals, eye, look);
				if (lstruck) {
					effects_end_invisibility();
					creatures_poison_struck(equip_take_poison(EQUIP_POISON_LEFT)); // (a poison on it: spent)
					after_blow(lweapon, 0, 0);
					if (!items_staff(lweapon))
						enchant_strike(lweapon);
					sfx_play(lkind == 4 ? "wpn_hit_blunt" : "wpn_hit_blade", 0.8f, 0.0f);
					skills_use(SK_ONE_HANDED, base);
				}
			}
			if (hit_pending && at >= swing_len * HIT_FRACTION) {
				// The blow lands: Skyrim's damage (the weapon's, its skill: + half a percent a
				// level; a power attack's double), on what's in front within reach
				hit_pending = 0;
				const int skill = swing_kind >= 5 ? SK_TWO_HANDED : SK_ONE_HANDED;
				// (bare hands: Fists of Steel adds the gauntlets' armour)
				const float base = swing_weapon >= 0 ? (float)items_stat(swing_weapon)
				                                     : beast_active() ? beast_claw_damage(skills_state()->player_level) // (the werewolf's)
				                                     : UNARMED_DAMAGE + (perks_av(AV_UNARMED_DAMAGE) > 0.0f ? heavy_gauntlets() : 0.0f);
				// (and the perks: Armsman, Barbarian, Savage Strike...)
				const PerkContext hctx = weapon_context(swing_weapon, swing_power ? swing_power_kind : 0);
				{ // (unseen: Skyrim's x3 a blow - Backstab's x6, Assassin's Blade's x15 a dagger)
					PerkContext sctx = hctx;
					sctx.sneaking = 1;
					creatures_set_sneak_attack(perks_apply(EP_MOD_SNEAK_ATTACK_MULT, 3.0f, &sctx), 2.0f);
				}
				float dmg = with_criticals(swing_weapon, &hctx, perks_apply(EP_MOD_ATTACK_DAMAGE,
				                           base * (1.0f + skills_state()->level[skill] / 200.0f) * fortify(skill), &hctx));
				if (swing_power)
					dmg = perks_apply(EP_MOD_POWER_ATTACK_DAMAGE, dmg * 2.0f, &hctx);
				if (bound_in_hand(swing_weapon) && perks_has_form(PERK_MYSTIC_BINDING))
					dmg *= MYSTIC_BINDING_MULT; // (Mystic Binding: Skyrim's stronger bound weapons)
				const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
				const float look[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)};
				const float eye[3] = {player.x, player.feet + EYE_HEIGHT, player.z};
				const float reach = beast_active() ? beast_reach_m() // (its claws' reach: the werewolf race's)
				                  : (swing_kind >= 5 ? REACH_2H_M : REACH_1H_M) + (riding_mounted() ? MOUNTED_REACH_M : 0.0f);
				if (swing_weapon >= 0)
					activators_strike(eye, look, reach + 0.6f); // (a spider's web in the way: cut down)
				int staff_struck = 0;
				if ((swing_weapon < 0 || brawl_spar()) && brawl_strike(eye, look, reach, dmg)) { // (a brawl: your fist on them; a spar, your blade)
				} else if (swing_power && swing_kind >= 5 && swing_power_kind == POWER_SIDEWAYS && // (Sweep: a sideways two-handed
				           perks_apply(EP_SET_SWEEP_ATTACK, 0.0f, &hctx) > 0.0f &&        //  power attack - all before you)
				           creatures_hit_all(eye, look, reach, SWEEP_CONE_COS, dmg)) {
					staff_struck = 1;
					after_blow(swing_weapon, 1, swing_power_kind);
					sfx_play(swing_kind == 6 ? "wpn_hit_blunt" : "wpn_hit_blade", 0.8f, 0.0f);
					skills_use(skill, base);
				} else if ((creatures_set_blow_power(swing_power), creatures_hit(eye, look, reach, HIT_CONE_COS, dmg))) {
					staff_struck = 1;
					effects_end_invisibility();
					creatures_poison_struck(equip_take_poison(EQUIP_POISON_RIGHT)); // (a poison on it: spent)
					after_blow(swing_weapon, swing_kind >= 5, swing_power ? swing_power_kind : 0);
					if (swing_weapon >= 0 && !items_staff(swing_weapon))
						enchant_strike(swing_weapon);
					if (bound_in_hand(swing_weapon)) { // (a bound weapon: Soul Stealer's soul trap, Oblivion Binding's banishing)
						if (perks_has_form(PERK_SOUL_STEALER))
							creatures_bewitch_struck(CSPELL_SOULTRAP, 0.0f, SOUL_STEALER_S);
						if (perks_has_form(PERK_OBLIVION_BINDING))
							creatures_banish_struck();
					}
					sfx_play(swing_kind < 0 ? "wpn_hit_unarmed" : swing_kind == 4 || swing_kind == 6 ? "wpn_hit_blunt"
					         : "wpn_hit_blade", 0.8f, 0.0f);
					if (swing_weapon >= 0)
						skills_use(skill, base);
					try_killmove(swing_kind, (const float[3]){player.x, player.feet, player.z}, yaw);
					const float soul = creatures_take_blow_soul(); // (Soul Siphon: a share of its soul into the weapon's charge)
					if (soul > 0.0f && swing_weapon >= 0)
						items_add_charge(swing_weapon, soul * perks_apply(EP_MOD_SOUL_TO_WEAPON, 0.0f, &hctx));
				} else if (people_hit(eye, look, reach, HIT_CONE_COS, dmg)) { // (a townsperson: assault, murder)
					sfx_play(swing_kind < 0 ? "wpn_hit_unarmed" : swing_kind == 4 || swing_kind == 6 ? "wpn_hit_blunt"
					         : "wpn_hit_blade", 0.8f, 0.0f);
				}
				if (swing_weapon >= 0 && items_staff(swing_weapon)) // (a staff: its spell, struck or as it swings)
					magic_staff_blow(swing_weapon, staff_struck, &vitals, eye, look);
			}
			blocking = fight && (pad.buttons & SCE_CTRL_SQUARE) && magic_hand(HAND_LEFT) < 0 && at < 0.0f &&
			           lweapon < 0 && !bow; // (dual wielding: no blocking, as in Skyrim; a bow can't: it bashes)
			const int shield = equip_shield_worn();
			// Shield Charge (Skyrim's perk): sprinting behind a raised shield, those ahead knocked down
			shield_charging = blocking && shield && sprinting && perks_has_form(PERK_SHIELD_CHARGE) && (mz < -0.5f);
			if (shield_charging) {
				static float charge_t;
				if ((charge_t -= frame_s) <= 0.0f) {
					charge_t = CHARGE_EVERY_S;
					const float yr = yaw * (float)M_PI / 180.0f;
					const float ahead[3] = {sinf(yr), 0.0f, -cosf(yr)}, from[3] = {player.x, player.feet + 1.0f, player.z};
					const int downed = creatures_shout_push(from, ahead, 1.8f, 0.5f, 6.0f, CHARGE_DAMAGE, 1);
					if (downed) {
						vitals.stamina = fmaxf(0.0f, vitals.stamina - CHARGE_STAMINA * downed), vitals.shown_for = 0.0f;
						sfx_play("wpn_block_shield", 0.9f, 0.0f);
					}
				}
			}
			actors_player_block(!blocking ? NULL : shield_charging ? "shd_charge" : shield ? "shd_block" : weapon < 0 ? "w1h_block"
			                    : wkind == 5 ? "w2hm_block" : wkind == 6 || wkind == 8 ? "w2hw_block" : "w1h_block");
			if (blocking) // (the guard up: the right hand's spell bashes, not casts - Skyrim's)
				cast_pressed[1] = cast_held[1] = 0;
			// Quick Reflexes: a power attack coming at your raised guard - the world slows a moment
			if (blocking && perks_has_form(PERK_QUICK_REFLEXES)) {
				const float feet_now[3] = {player.x, player.feet, player.z};
				if (creatures_power_attack_on_you(feet_now) && slow_time_mult >= 1.0f)
					slow_time_left = QUICK_REFLEXES_S, slow_time_mult = QUICK_REFLEXES_MULT;
			}
		}
		perf_lap("time_of_day");
		// The time of day (the wait menu jumps it; debug: R + TRIANGLE held fast-forwards)
		int hour_before = (int)lighting_sky()->hours;
		const float hours_before = lighting_sky()->hours;
		lighting_update(dt, time_fast ? TIME_FAST_FORWARD : 1.0f);
		int slept = 0;
		int waited = menu_take_wait(&slept);
		if (test_sleep)
			waited = 8, slept = 1, test_sleep = 0;
		{ // (Skyrim: no waiting or sleeping with enemies about)
			const float feet_now[3] = {player.x, player.feet, player.z};
			if (waited && creatures_hostile_near(feet_now, 60.0f)) {
				ui_notice(slept ? "You cannot sleep while enemies are nearby" : "You cannot wait while enemies are nearby",
				          3.0f);
				waited = slept = 0;
			}
		}
		if (waited)
			lighting_advance_hours((float)waited);
		if (test_god && dead_t < 0.0f) // (a test run's "god": kept alive, to see the way through)
			vitals.health = vitals.max_health;
		if (slept && crime_jailed() >= 0 && interior_current() == crime_jail_place())
			jail_serve = 1; // (Skyrim's: a sleep in the cell serves the sentence)
		if (slept) { // (a night in a bed: rested, as Skyrim's)
			vitals.health = vitals.max_health, vitals.magicka = vitals.max_magicka, vitals.stamina = vitals.max_stamina;
			ui_notice(waited >= 6 ? "You feel well rested" : "You feel rested", 3.0f);
		}
		{ // (a used racial power comes back after a day)
			float passed = lighting_sky()->hours - hours_before;
			if (passed < 0.0f)
				passed += 24.0f; // (past midnight)
			races_pass_hours(waited ? (float)waited : passed);
			beast_pass_hours(waited ? (float)waited : passed);
			weather_update(player.x, player.z, interior_current() < 0, dt, waited ? (float)waited : passed);
		}
		if ((int)lighting_sky()->hours != hour_before)
			sceClibPrintf("time: %02d:00\n", (int)lighting_sky()->hours);
		music_set_hours(lighting_sky()->hours);

		perf_lap("pace_input");
		// Two paces as in Skyrim: the stick tilted part way walks, all the way (or the d-pad)
		// runs; R sprints. Sneaking is slower.
		float tilt = hypotf(mx, mz);
		if (tilt > 1.0f)
			mx /= tilt, mz /= tilt, tilt = 1.0f;
		perf_lap("water");
		// Water here (rivers and ponds, or the cell's lake level): deep enough, you swim
		float river, lake;
		int has_river = objects_water_at_y(player.x, player.feet, player.z, &river), // (inside: the dungeon's own pools)
		    has_lake = interior_current() < 0 && terrain_water_at(player.x, player.z, &lake);
		player.in_water = has_river || has_lake;
		player.water = has_river && has_lake ? fmaxf(river, lake) : has_river ? river : lake;
		{ // (the log says where deep water was found, and whose: the cell's lake level or a model's)
			static int was_deep;
			const int deep = player.in_water && player.water > player.feet + 1.0f;
			if (deep && !was_deep) {
				float ground = NAN;
				terrain_height_at(player.x, player.z, &ground);
				game_log("water: deep at (%.0f, %.0f) feet %.1f ground %.1f | river %s %.1f lake %s %.1f",
				         player.x, player.z, player.feet, ground, has_river ? "yes" : "no", has_river ? river : 0.0f,
				         has_lake ? "yes" : "no", has_lake ? lake : 0.0f);
			}
			was_deep = deep;
		}
		perf_lap("movement");
		{ // (never nowhere: a position gone NaN - every check passes and walls taught across the map - back to the last good one)
			static float good[3] = {0.0f, 0.0f, 0.0f};
			static int have_good;
			if (isfinite(player.x) && isfinite(player.feet) && isfinite(player.z))
				good[0] = player.x, good[1] = player.feet, good[2] = player.z, have_good = 1;
			else if (have_good) {
				game_log("player: position lost (%f %f %f), put back at %.1f %.1f %.1f", player.x, player.feet, player.z, good[0],
				         good[1], good[2]);
				player.x = good[0], player.feet = good[1], player.z = good[2], player.vy = 0.0f;
			}
		}
		float pace = (pad.buttons & SCE_CTRL_LTRIGGER) && opts->debug ? DEBUG_TRAVEL_SPEED
		             : riding_mounted() ? (sprinting ? RIDING_SPRINT_MS : tilt < WALK_TILT ? RIDING_WALK_MS : RIDING_RUN_MS)
		             : player.swimming ? (sprinting ? SWIM_FAST_SPEED : tilt < WALK_TILT ? SWIM_SLOW_SPEED : SWIM_SPEED)
		             : sneaking && silent_roll_t > 0.0f ? SILENT_ROLL_MS // (Silent Roll: a quick roll forward, crouched)
		             : sneaking ? (tilt < WALK_TILT ? SNEAK_WALK_MS : SNEAK_RUN_MS)
		             : sprinting ? SPRINT_SPEED
		             : tilt < WALK_TILT || encumbered ? WALK_SPEED_MS : RUN_SPEED_MS;
		if (beast_active() && !player.swimming && !((pad.buttons & SCE_CTRL_LTRIGGER) && opts->debug))
			pace *= BEAST_PACE_MULT; // (the werewolf's stride)
		pace *= effects_speed_mult(); // (a Slow poison)
		// (Skyrim: a drawn bow or a raised guard keeps you to a walk - Ranger, Block Runner: not)
		if (!player.swimming && ((bow_nocked >= 0 && !perks_has_form(PERK_RANGER)) ||
		                         (blocking && !shield_charging && !(equip_shield_worn() && perks_has_form(PERK_BLOCK_RUNNER)))))
			pace = fminf(pace, WALK_SPEED_MS);
		float speed = tilt > 0.0f ? pace * dt / tilt : 0.0f; // (direction from the stick, length from the pace)
		float sy = sinf(yaw * (float)M_PI / 180.0f), cy = cosf(yaw * (float)M_PI / 180.0f);
		float dx = (mx * cy - mz * sy) * speed;
		float dz = (mx * sy + mz * cy) * speed;
		// Swimming: pushing forward goes where you look, as in Skyrim. At the surface only
		// a clear look down dives; under water any tilt steers up or down. Let go and you
		// float back up.
		player.swim_dy = 0.0f;
		if (player.swimming && -mz * speed > 0.0f) {
			const int under = player.feet < player.water - SWIM_DEPTH - 0.05f;
			if (under ? fabsf(pitch) > SWIM_LEVEL_DEG : pitch < -SWIM_DIVE_DEG) {
				const float pr = pitch * (float)M_PI / 180.0f;
				player.swim_dy = -mz * speed * sinf(pr);
				dx *= cosf(pr), dz *= cosf(pr);
			}
		}
		if (dash_left > 0.0f && !noclip && !player.swimming) { // (Whirlwind Sprint: flung along)
			dx = dash_dir[0] * dash_speed * dt, dz = dash_dir[1] * dash_speed * dt;
			dash_left -= dt;
		}
		if (noclip) {
			// Only move onto loaded ground (the converted region has an edge)
			float h;
			if (terrain_height_at(player.x + dx, player.z + dz, &h)) {
				player.x += dx, player.z += dz, player.feet = h, player.vy = 0.0f;
			}
		} else {
			float px = player.x, pz = player.z;
			if (interior_current() >= 0)
				objects_interior_update(player.x, player.z); // (the collision around you)
			if (opening.hold) // (the opening's cart, its block)
				player.x = opening.pos[0], player.feet = opening.pos[1], player.z = opening.pos[2], player.vy = 0.0f;
			else if (seated) // (on a seat: where its clip puts you, through its own collision)
				player.x = seat_pos[0], player.z = seat_pos[2], player.vy = 0.0f;
			else {
				perf_begin(PERF_MOVE);
				collision_move(&player, dx, dz, dt);
				if (shove[0] != 0.0f || shove[1] != 0.0f) { // (a draugr's Unrelenting Force: flung back, slowing)
					collision_move(&player, shove[0] * dt, shove[1] * dt, dt);
					const float slow = fmaxf(0.0f, 1.0f - 4.0f * dt);
					shove[0] *= slow, shove[1] *= slow;
					if (hypotf(shove[0], shove[1]) < 0.3f)
						shove[0] = shove[1] = 0.0f;
				}
				float bx = player.x, bz = player.z; // (and out of anyone you'd walk into - not through them)
				if (!noclip && actors_block_player(&bx, &bz, player.feet, PLAYER_RADIUS))
					collision_move(&player, bx - player.x, bz - player.z, 0.0f);
				float back[2]; // (and creatures', a familiar's: you stop at them, not push them along)
				creatures_take_block(back);
				if (!noclip && (back[0] != 0.0f || back[1] != 0.0f))
					collision_move(&player, back[0], back[1], 0.0f);
				perf_end(PERF_MOVE);
			}
			// Diagnostics: pushing but (almost) not moving for half a second -> log what blocks
			float want = sqrtf(dx * dx + dz * dz), got = hypotf(player.x - px, player.z - pz);
			stuck_frames = (want > 0.05f && got < want * 0.2f) ? stuck_frames + 1 : 0;
			if (stuck_frames == 30 || (stuck_frames > 30 && stuck_frames % 120 == 0)) {
				CollisionBlockers b = collision_last_blockers();
				float terrain = 0.0f;
				terrain_height_at(player.x, player.z, &terrain);
				sceClibPrintf("STUCK at x=%.2f z=%.2f feet=%.2f (terrain %.2f) cell (%d,%d) yaw=%.0f | wall: %s (contact y=%.2f) | "
				              "surface above step: %s (y=%.2f)\n",
				              player.x, player.z, player.feet, terrain, terrain_cell_x(player.x), terrain_cell_y(player.z), yaw,
				              b.wall_tag >= 0 ? objects_model_name(b.wall_tag) : "none", b.wall_y,
				              b.ledge_tag >= 0 ? objects_model_name(b.ledge_tag) : b.ledge_tag == -3 ? "TERRAIN" : "none",
				              b.ledge_y);
			}
		}
		perf_lap("listener_chimes");
		sfx_listener(player.x, player.z, yaw);
		// A skill or the player's level going up: Skyrim's chimes
		{
			if (chime_reset) // (a game just loaded: its levels aren't gains)
				levels_before = player_before = -1, chime_reset = 0;
			const SkillState *sk = skills_state();
			int levels = 0;
			for (int k = 0; k < SKILL_COUNT; k++)
				levels += sk->level[k];
			if (player_before >= 0 && sk->player_level > player_before) {
				static const char *const UP[3] = {"mus_levelup_01", "mus_levelup_02", "mus_levelup_03"};
				sfx_play("ui_levelup", 0.8f, 0.0f), music_special(UP[sk->player_level % 3]); // (and its sting)
			}
			else if (levels_before >= 0 && levels > levels_before && !front)
				sfx_play("ui_skill", 0.7f, 0.0f);
			levels_before = levels, player_before = sk->player_level;
		}
		perf_lap("footsteps");
		// Footsteps (Skyrim's, by what's underfoot), strokes while swimming
		{
			static float stepped, last_x, last_z, swim_t;
			const float moved = hypotf(player.x - last_x, player.z - last_z);
			last_x = player.x, last_z = player.z;
			const float pace = dt > 0.0f && moved < 2.0f ? moved / dt : 0.0f;
			if (player.swimming && dt > 0.0f) {
				swim_t += dt;
				if (swim_t > (pace > 0.5f ? 1.1f : 1.8f))
					sfx_play(pace > 0.5f ? "fst_swim" : "fst_tread", 0.45f, 0.0f), swim_t = 0.0f;
			} else if (player.grounded && !seated && !noclip && moved < 2.0f) {
				stepped += moved;
				const int run = pace > RUN_SPEED_MS * 0.8f;
				if (stepped > (pace > SPRINT_SPEED * 0.9f ? 1.5f : run ? 1.2f : 0.8f)) {
					stepped = 0.0f;
					// (wading only with your feet in it: not on a bridge or a dock over it)
					const int wading = player.in_water && player.feet < player.water + WADE_M;
					char event[32];
					snprintf(event, sizeof(event), "fst_%s_%s", underfoot(), run ? "run" : "walk");
					if (wading)
						snprintf(event, sizeof(event), "fst_water");
					const float loud = interior_current() >= 0 ? STEP_VOLUME_INSIDE : STEP_VOLUME;
					if (!magic_muffled()) // (Muffle: silent)
						sfx_play(event, sneaking ? loud * 0.45f : loud, 0.0f);
				}
			} else {
				stepped = 0.0f;
			}
			// Jumping off and landing: Skyrim's thuds, heavier for a longer drop
			static int was_grounded = 1;
			static float fall_from;
			if (!player.grounded && !player.swimming && !noclip && was_grounded) {
				fall_from = player.feet;
				if (player.vy > 1.0f && !magic_muffled()) {
					char event[32];
					snprintf(event, sizeof(event), "fst_%s_jump", underfoot());
					sfx_play(event, STEP_VOLUME, 0.0f);
				}
			}
			if (player.grounded && !was_grounded && !player.swimming && !noclip) {
				const float drop = fall_from - player.feet;
				if (drop > 0.4f) {
					char event[32];
					snprintf(event, sizeof(event), "fst_%s_land", underfoot());
					sfx_play(event, fminf(1.0f, STEP_VOLUME + drop * 0.12f), 0.0f);
				}
			}
			if (!player.grounded)
				fall_from = fmaxf(fall_from, player.feet);
			was_grounded = player.grounded || player.swimming;
		}
		perf_lap("races_noise");
		// Vitals: sprinting costs stamina, everything regenerates; long falls hurt
		{
			races_update(dt, &vitals); // (racial abilities and a power's effect)
			// Creatures about, and what they've bitten off you (a block takes most of it, and
			// stamina instead: Skyrim's)
			{
				const float feet_at[3] = {player.x, player.feet, player.z};
				const float moving = hypotf(player.x - last_x_noise, player.z - last_z_noise) / fmaxf(dt, 1e-3f);
				last_x_noise = player.x, last_z_noise = player.z;
				// (Muffled Movement: half the armour's noise; Silence: walking and running don't give you away)
				const float footfall = fmaxf(0.0f, 1.0f - perks_av(AV_MOVEMENT_NOISE));
				creatures_set_noise((sneaking ? 0.3f : moving > 4.0f ? 2.0f : moving > 0.5f ? 1.0f : 0.6f) *
				                    (moving > 0.5f ? fmaxf(0.3f, footfall) : 1.0f) *
				                    (actors_player_attack_time() >= 0.0f ||
				                     (magic_readied() && perks_apply(EP_MOD_CASTING_SOUND, 1.0f, NULL) > 0.0f) ? 1.5f : 1.0f) * // (Quiet Casting)
				                    (magic_muffled() || effects_muffled() ? 0.3f : 1.0f)); // (the spell, an enchantment)
				perf_end(PERF_GAME);
				perf_begin(PERF_AI);
				creatures_update(dt * slow_time_mult, feet_at, sneaking, interior_current()); // (Slow Time: the world slowed)
				// Crime (crime.c): what was stolen where someone saw, the guards after you
				crime_set_player(feet_at, sneaking, effects_invisible() ? 0.0f : 1.0f);
				followers_update(dt, feet_at, yaw, interior_current());
				people_update(dt, interior_current());
				wordfx_update(dt);
				const int theft = items_take_theft() + loot_take_theft();
				if (!intro_active() || intro_stage() > 0) { // (not in Helgen before the keep)
					if (theft > 0)
						crime_commit(CRIME_THEFT, theft, feet_at, -1);
					crime_update(dt);
					int hold = -1;
					const int guard = crime_arresting() || menu_is_open() || dead_t >= 0.0f ? -1 : creatures_take_arrest(&hold);
					float gpos[3];
					if (guard >= 0 && creatures_position(guard, gpos)) { // (turned to the guard, as Skyrim's talk does)
						yaw = atan2f(gpos[0] - player.x, -(gpos[2] - player.z)) * 180.0f / (float)M_PI, pitch = 0.0f;
						crime_arrest_begin(guard, hold, actors_player_female());
					}
				}
				perf_end(PERF_AI);
				perf_begin(PERF_GAME);
		perf_lap("traps");
				traps_update(dt, feet_at);
				if (interior_current() >= 0)
					activators_update(dt, feet_at); // (Ustengrav's dragon stones)
		perf_lap("projectiles");
				float shot = projectiles_update(dt * slow_time_mult, feet_at) * taken_mult(); // (arrows: the difficulty's)
				{ // A shield raised: arrows blocked as blows are (Deflect Arrows: all of them); bolts:
				  // Elemental Protection's half; Magic Resistance takes its share of a spell's
					float magic, arrows;
					projectiles_take_split(&magic, &arrows);
					const float m = menu_damage_taken_mult();
					PerkContext bctx = {0};
					bctx.school = -1, bctx.blocking = blocking;
					if (arrows > 0.0f && blocking && equip_shield_worn()) {
						const float deflect = perks_apply(EP_MOD_DEFLECT_ARROW, 0.0f, &bctx);
						const float kept = (float)(rand() % 100) < deflect ? 0.0f : 1.0f - BLOCK_SHIELD_SHARE;
						shot -= arrows * m * (1.0f - kept);
						sfx_play("wpn_block_shield", 0.5f, 0.0f);
					}
					if (magic > 0.0f && (float)(rand() % 100) < perks_av(AV_ABSORB_CHANCE)) { // (Atronach: the spell drunk in)
						shot -= magic * m;
						vitals.magicka = fminf(vitals.max_magicka, vitals.magicka + magic), vitals.shown_for = 0.0f;
						game_log("perks: a spell absorbed (%.0f magicka)", magic);
						magic = 0.0f;
					}
					if (magic > 0.0f && magic_ward() > 0.0f) { // (a ward up: half the spell turned - Ward Absorb: a share to magicka)
						shot -= magic * m * 0.5f;
						vitals.magicka = fminf(vitals.max_magicka, vitals.magicka + magic * perks_apply(EP_MOD_WARD_ABSORB, 0.0f, NULL));
						magic *= 0.5f;
					}
					if (magic > 0.0f) {
						static const uint32_t fire_kw = 0x0001ceadu; // (MagicDamageFire: a mage's bolt)
						bctx.keywords = &fire_kw, bctx.keyword_count = 1;
						const float resist = fminf(0.85f, (perks_av(AV_MAGIC_RESIST) + effects_av(AV_MAGIC_RESIST)) / 100.0f);
						const float kept = perks_apply(EP_MOD_INCOMING_SPELL_MAGNITUDE, 1.0f, &bctx) * (1.0f - fmaxf(resist, -1.0f));
						shot -= magic * m * (1.0f - kept);
					}
				}
				shot = fmaxf(0.0f, shot) + traps_take_damage() * (ethereal_left > 0.0f ? 0.0f : 1.0f); // (traps)
				if (shot > 0.0f && dead_t < 0.0f) {
					vitals.health -= shot, vitals.shown_for = 0.0f;
					if (vitals.health <= 0.0f) {
						vitals.health = 0.0f, dead_t = 0.0f;
						ui_notice("You died.", DEATH_RESPAWN_S);
						log_death("struck (arrows, spells, traps)", 0.0f, player.x, player.feet, player.z);
					}
				}
		perf_lap("damage_taken");
				avoid_death(&vitals);
				const int shield = equip_shield_worn();
				// (blocked: the rest; Shield Wall and the like block more)
				const float share = 1.0f - fminf(1.0f, perks_apply(EP_MOD_PERCENT_BLOCKED,
				                                                    1.0f - (shield ? BLOCK_SHIELD_SHARE : BLOCK_WEAPON_SHARE), NULL));
				// (a spider's: through armour; Resist Poison / Snakeblood take their share, as in Skyrim)
				const float resist = fminf(0.85f, fmaxf(-1.0f, (perks_av(AV_RESIST_POISON) + effects_av(AV_RESIST_POISON)) / 100.0f));
				const float poison = creatures_take_poison(frame_s) * (intro_side() ? OPENING_HURT : 1.0f) * taken_mult() * (1.0f - resist);
				// A dragon's breath: fire or frost (Resist Fire / Frost take their share; frost
				// chills your stamina too, as Skyrim's does)
				int element = 0;
				const float breath_in = creatures_take_breath(&element) * taken_mult();
				const float breath = breath_in * (magic_ward() > 0.0f ? 0.5f : 1.0f); // (a ward up: half of it turned)
				if (breath_in > 0.0f && magic_ward() > 0.0f) // (Ward Absorb: what it turned, a share to magicka)
					vitals.magicka = fminf(vitals.max_magicka, vitals.magicka + breath * perks_apply(EP_MOD_WARD_ABSORB, 0.0f, NULL));
				if (breath > 0.0f && dead_t < 0.0f) {
					const int av = element == ELEMENT_FROST ? 43 : 41;
					const float res = fminf(0.85f, fmaxf(-1.0f, (perks_av(av) + effects_av(av)) / 100.0f));
					vitals.health -= breath * (1.0f - res), vitals.shown_for = 0.0f;
					if (element == ELEMENT_FROST)
						vitals.stamina = fmaxf(0.0f, vitals.stamina - breath * (1.0f - res));
					if (vitals.health <= 0.0f) {
						vitals.health = 0.0f, dead_t = 0.0f;
						ui_notice("You died.", DEATH_RESPAWN_S);
						log_death("a dragon's breath", 0.0f, player.x, player.feet, player.z);
					}
				}
				{ // A draugr's shout: Unrelenting Force's push, Frost Breath's cold, Disarm
					float push[2];
					int el = 0, disarm = 0;
					const float hit = creatures_take_shout(push, &el, &disarm) * taken_mult() *
					                  (magic_ward() > 0.0f ? 0.5f : 1.0f);
					if ((hit > 0.0f || disarm) && dead_t < 0.0f) {
						const int av = el == ELEMENT_FROST ? 43 : el == ELEMENT_FIRE ? 41 : -1;
						const float res = av < 0 ? 0.0f : fminf(0.85f, fmaxf(-1.0f, (perks_av(av) + effects_av(av)) / 100.0f));
						vitals.health -= hit * (1.0f - res), vitals.shown_for = 0.0f;
						if (el == ELEMENT_FROST)
							vitals.stamina = fmaxf(0.0f, vitals.stamina - hit * (1.0f - res));
						shove[0] += push[0], shove[1] += push[1];
						shout_shake = fmaxf(shout_shake, el == ELEMENT_FORCE ? 0.7f : 0.3f);
						if (disarm) { // (your weapon out of your hand)
							int kind;
							const int w = equip_weapon(&kind);
							if (w >= 0 && equip_toggle(w))
								ui_notice("Disarmed!", 2.5f);
						}
						if (vitals.health <= 0.0f) {
							vitals.health = 0.0f, dead_t = 0.0f;
							ui_notice("You died.", DEATH_RESPAWN_S);
							log_death("a shout", 0.0f, player.x, player.feet, player.z);
						}
					}
				}
				{ // (a dragon's soul absorbed)
					const int souls = creatures_take_dragon_souls();
					souls_this_frame = souls;
					if (souls > 0) {
						dragon_souls += souls;
						shouts_add_souls(souls); // (to spend on a word you've learned: the Magic menu)
						char text[96];
						snprintf(text, sizeof(text), "Dragon Soul Absorbed (%d to spend on a word of power)", shouts_souls());
						ui_notice(text, 5.0f);
					}
				}
				if (poison > 0.0f && dead_t < 0.0f) {
					vitals.health -= poison, vitals.shown_for = 0.0f;
					if (vitals.health <= 0.0f) {
						vitals.health = 0.0f, dead_t = 0.0f;
						ui_notice("You died.", DEATH_RESPAWN_S);
						log_death("poison", 0.0f, player.x, player.feet, player.z);
					}
				}
				const float raw = creatures_take_damage(0, 1.0f) * (intro_side() ? OPENING_HURT : 1.0f) * taken_mult(); // (the difficulty's)
				const int power_blow = creatures_take_power_struck();
				if (raw > 0.0f && dead_t < 0.0f) {
					int armour_skill;
					const float armour = armour_rating(&armour_skill);
					// (a power attack: a guard holds back less of it - Skyrim's fBlockPowerAttackMult)
					const float through = power_blow ? 1.0f - (1.0f - share) * BLOCK_POWER_ATTACK_MULT : share;
					float hurt = (blocking ? raw * through : raw) * (1.0f - fminf(0.8f, armour * 0.0012f));
					if (blocking && !power_blow)
						creatures_recoil_striker(); // (its blow bounced off your guard)
					else if (power_blow && !blocking && actors_player_attack_time() < 0.0f &&
					         (float)(rand() % 100) < 100.0f * perks_apply(EP_MOD_INCOMING_STAGGER, 1.0f, NULL)) { // (Tower of Strength: half)
						actors_player_attack("w1h_hit"); // (staggered by it, as Skyrim's power attacks do)
						game_log("combat: staggered by a power attack");
					}
					hurt = perks_apply(EP_MOD_INCOMING_DAMAGE, hurt, NULL);
					if (perks_av(AV_REFLECT_DAMAGE) > 0.0f && !blocking && rand() % 100 < REFLECT_CHANCE &&
					    creatures_reflect_striker(hurt * perks_av(AV_REFLECT_DAMAGE) / 100.0f)) // (Reflect Blows)
						hurt = 0.0f;
					if (armour_skill >= 0)
						skills_use(armour_skill, raw); // (Skyrim: armour trains on the blows it takes)
					if (blocking)
						skills_use(SK_BLOCK, raw);
					// (the blow's sound: steel on your block - a shield's clang - or into you, and your
					// grunt, in your character's voice, as Skyrim's DCETHit)
					const int struck = creatures_take_struck();
					if (blocking) {
						sfx_play("wpn_block_hit", 0.9f, 0.0f);
					} else if (struck > -2) {
						sfx_play(struck >= 1 ? "wpn_hit_player" : "wpn_hit_unarmed", 0.8f, 0.0f);
						static unsigned grunt_frame;
						if (frame - grunt_frame > 40 && vitals.health - hurt > 0.0f) {
							player_grunt("hit");
							grunt_frame = frame;
						}
					}
					if (blocking) {
						sfx_play(shield ? "wpn_block_shield" : "wpn_block", 0.5f, 0.0f);
						vitals.stamina = fmaxf(0.0f, vitals.stamina - raw * BLOCK_STAMINA_PER_DAMAGE);
					}
					vitals.health -= hurt, vitals.shown_for = 0.0f;
					if (vitals.health <= 0.0f) {
						player_grunt("death");
						vitals.health = 0.0f, dead_t = 0.0f;
						ui_notice("You died.", DEATH_RESPAWN_S);
						log_death("a blow", 0.0f, player.x, player.feet, player.z);
					}
				}
				const char *foe;
				float foe_health;
				if (creatures_target(&foe, &foe_health))
					ui_enemy(foe, foe_health);
				else
					ui_enemy(NULL, 0.0f);
			}
			if (skills_take_leveled()) { // (Skyrim: a new level restores you)
				vitals.health = vitals.max_health, vitals.magicka = vitals.max_magicka;
				vitals.stamina = vitals.max_stamina, vitals.shown_for = 0.0f;
			}
			// (potions: Regenerate / Damage ... Regen scale the regeneration for this frame)
			const float rate_was[3] = {vitals.rate_mult[0], vitals.rate_mult[1], vitals.rate_mult[2]};
			for (int k = 0; k < 3; k++)
				vitals.rate_mult[k] *= effects_rate_mult(k);
			beast_rates(&vitals); // (Beast Form: health back only by feeding)
		perf_lap("vitals");
			can_sprint = vitals_update(&vitals, dt, sprinting && (dx != 0.0f || dz != 0.0f));
			memcpy(vitals.rate_mult, rate_was, sizeof(rate_was));
		perf_lap("effects_falls");
			effects_update(dt, &vitals); // (potions' over-time effects, fortifies ending)
			if (effects_take_cured_poison())
				creatures_cure_player_poison(); // (a spider's bite too)
			enchant_worn_update(&vitals); // (worn enchantments: Fortify Health, resistances...)
			if (vitals.health <= 0.0f && dead_t < 0.0f) { // (a poison eaten)
				vitals.health = 0.0f, dead_t = 0.0f;
				ui_notice("You died.", DEATH_RESPAWN_S);
				log_death("an effect (poison eaten)", 0.0f, player.x, player.feet, player.z);
			}
			if (!player.grounded && !player.swimming && !noclip) {
				air_peak = airborne ? fmaxf(air_peak, player.feet) : player.feet;
				airborne = 1;
			} else {
				if (airborne && player.grounded && travel_landing) // (set down after fast travel: no fall)
					airborne = 0;
				if (player.grounded && !transition)
					travel_landing = 0;
				if (airborne && player.grounded && dead_t < 0.0f && ethereal_left <= 0.0f) { // (Cushioned: half, in all heavy armour)
					const float before = vitals.health;
					vitals_fall(&vitals, air_peak - player.feet);
					if (vitals.health < before)
						vitals.health = before - (before - vitals.health) * perks_apply(EP_MOD_FALL_DAMAGE, 1.0f, NULL);
					if (vitals.health <= 0.0f) {
						vitals.health = 0.0f, dead_t = 0.0f;
						ui_notice("You died.", DEATH_RESPAWN_S);
						log_death("a fall", air_peak - player.feet, player.x, player.feet, player.z);
					}
				}
				airborne = 0;
			}
			// Sneak trains while you move crouched within sight of someone (Skyrim: unseen)
			if (sneaking && (dx != 0.0f || dz != 0.0f) && dt > 0.0f) {
				for (int i = 0; i < actors_count(); i++) {
					float at[3];
					if (actors_where(i, at) && hypotf(at[0] - player.x, at[2] - player.z) < SNEAK_TRAIN_M) {
						skills_use(SK_SNEAK, SNEAK_USAGE_PER_S * dt);
						break;
					}
				}
			}
			// Under water: holding your breath, then drowning
			const int head_under = player.swimming && !noclip && player.feet + EYE_HEIGHT < player.water;
			const int gills = vitals.water_breathing; // (a Waterbreathing potion: breathing like an Argonian)
			vitals.water_breathing |= effects_water_breathing();
			const int drowned = dt > 0.0f && dead_t < 0.0f && vitals_breathe(&vitals, dt, head_under);
			vitals.water_breathing = gills;
			if (drowned) {
				dead_t = 0.0f;
				ui_notice("You drowned.", DEATH_RESPAWN_S);
			}
			ui_vitals(vitals.health / vitals.max_health, vitals.magicka / vitals.max_magicka,
			          vitals.stamina / vitals.max_stamina, vitals.breath / vitals.max_breath,
			          menu_settings()->hud_bar_fade ? vitals_hud_alpha(&vitals) : 1.0f); // (the options': fading when full, or always shown)
		}
		perf_lap("loot_ext");
		float cam_x = opening.eye_set ? opening.eye[0] : player.x, cam_z = opening.eye_set ? opening.eye[2] : player.z;
		objects_set_time(clock_s);
		water_set_time(clock_s);
		unsigned long long t0 = stream_now_us();
		const int indoors = interior_current() >= 0;
		if (!indoors) // (outdoor containers and loose items around you)
			loot_exterior(terrain_cell_x(player.x), terrain_cell_y(player.z));
		perf_lap("stream_ai");
		perf_end(PERF_GAME);
		perf_begin(PERF_STREAM);
		if (!indoors) { // (the exterior stays loaded but idle while inside)
			terrain_update(cam_x, cam_z);
			objects_update(cam_x, cam_z);
			navmesh_update(cam_x, cam_z);
		}
		perf_end(PERF_STREAM);
		perf_begin(PERF_AI);
		actors_update(dt, lighting_sky()->hours, cam_x, cam_z);
		perf_end(PERF_AI);
		perf_begin(PERF_GAME);
		perf_lap("activate_prep");
		// CIRCLE: whatever the crosshair is on (Skyrim's: the nearest thing along where you
		// look, within reach): someone to talk to (sneaking: pick their pocket), a door to go
		// through, a door to open, a container to open, a thing to take
		static int pending_door = -1;
		static float fade;
		static int autosave;
		static float autosave_wait; // (see below)
		static int autosave_now;
		int pressed = cross_tap && !transition && !paused; // (CIRCLE: activate)
		static float caught_by_s[MAX_POCKET_NPCS]; // (caught: they're wary of you for a while)
		for (int i = 0; i < MAX_POCKET_NPCS; i++)
			caught_by_s[i] = fmaxf(0.0f, caught_by_s[i] - dt);
		enum { AIM_NONE, AIM_PERSON, AIM_DOOR, AIM_SWING, AIM_LOOT, AIM_ACTIVATOR, AIM_FURNITURE, AIM_BODY, AIM_ARROW, AIM_HORSE };
		int aim = AIM_NONE, aim_index = -1;
		const char *aim_label = NULL;
		static char person_label[96];
		perf_lap("aim");
		if (corpse_search >= 0) { // (all they wore taken: their clothes off - Skyrim's bodies lie in their underwear)
			int its[64], counts[64], gear = 0;
			const int n = items_in_container(corpse_handle, its, counts, 64);
			for (int k = 0; k < n; k++)
				gear |= items_category(its[k]) == CAT_WEAPONS || items_category(its[k]) == CAT_APPAREL;
			if (!gear)
				actors_script_strip(corpse_search), corpse_search = -1;
		}
		if (body_search >= 0) { // (a creature's - a person's body: the same)
			int its[64], counts[64], gear = 0;
			const int n = items_in_container(body_handle, its, counts, 64);
			for (int k = 0; k < n; k++)
				gear |= items_category(its[k]) == CAT_WEAPONS || items_category(its[k]) == CAT_APPAREL;
			if (!gear)
				creatures_strip(body_search), body_search = -1;
		}
		perf_begin(PERF_LOOK);
		if (!transition) {
			const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
			const float look[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)};
			const float eye[3] = {player.x, player.feet + EYE_HEIGHT, player.z};
			memcpy(tk_eye, eye, sizeof(tk_eye)), memcpy(tk_look, look, sizeof(tk_look)); // (Telekinesis: the same ray, further)
			float best = 1e9f, t;
			const char *label = NULL;
			int i;
			const int in_scene = intro_active() && !intro_stage(); // (Helgen's scenes: nobody to talk to)
			if (!in_scene && (i = talk_aim(eye, look, &t)) >= 0 && t < best) {
				best = t, aim = AIM_PERSON, aim_index = i;
				const int pockets = items_npc_pockets(actors_id(i));
				snprintf(person_label, sizeof(person_label), actors_corpse(i) ? "Search %s" : sneaking && pockets >= 0 ? "Pickpocket %s"
				         : actors_asleep(i) ? "Wake %s" : "Talk to %s", talk_name(i));
				aim_label = person_label;
			}
			if ((i = interior_door_aim(eye, look, &t, &label)) >= 0 && t < best)
				best = t, aim = AIM_DOOR, aim_index = i, aim_label = label;
			if ((i = doors_aim(eye, look, &t, &label)) >= 0 && t < best)
				best = t, aim = AIM_SWING, aim_index = i, aim_label = label;
			if ((i = loot_aim(eye, look, &t, &label)) >= 0 && t < best)
				best = t, aim = AIM_LOOT, aim_index = i, aim_label = label;
			if (aim == AIM_DOOR || aim == AIM_SWING) { // (a locked door: its lock - Skyrim's, picked or keyed)
				float dp[3];
				const int place = interior_current() >= 0 ? interior_current() : -1 - worlds_current();
				const int got = aim == AIM_DOOR ? interior_door_pos(aim_index, dp) : doors_position(aim_index, dp);
				aim_lock = got && !(aim == AIM_SWING && doors_is_open(aim_index)) ? locks_find(place, dp) : -1;
				if (aim == AIM_DOOR && aim_lock >= 0 && locks_locked(aim_lock) && services_gate_open(aim_label, place))
					locks_unlock(aim_lock); // (Whiterun's gate, yours to pass now: open as you look at it - it said
					                        //  "Requires key" back from the stables till pressed)
				const char *ll = locks_label(aim_lock, aim == AIM_DOOR ? aim_label : "Door");
				if (ll)
					aim_label = ll;
			} else {
				aim_lock = -1;
			}
			if ((i = activators_aim(eye, look, &t, &label)) >= 0 && t < best)
				best = t, aim = AIM_ACTIVATOR, aim_index = i, aim_label = label;
			if (!seated && (i = furniture_aim(eye, look, &t, &label)) >= 0 && t < best)
				best = t, aim = AIM_FURNITURE, aim_index = i, aim_label = label;
			if ((i = creatures_aim(eye, look, &t, &label)) >= 0 && t < best)
				best = t, aim = AIM_BODY, aim_index = i,
				aim_label = beast_active() && creatures_health_share(i) <= 0.0f ? "Feed" : label; // (the beast's: Skyrim's Feed)
			if ((i = projectiles_aim(eye, look, &t, &label)) >= 0 && t < best)
				best = t, aim = AIM_ARROW, aim_index = i, aim_label = label;
			if (riding_aim(eye, look, &t, &label) && t < best) // (your horse: "Ride Horse")
				best = t, aim = AIM_HORSE, aim_index = 0, aim_label = label;
			if (aim == AIM_NONE && traps_aim_oil(eye, look, &t, &label) >= 0) // (named only: nothing to do with it)
				aim_label = label;
			if (aim == AIM_NONE && !aim_label) { // (a cave's mouth, a walk-through way out, near: named, and (O) takes you
				// through - only aimed straight at, it said nothing as you came up to it)
				const float feet_w[3] = {player.x, player.feet, player.z};
				if ((i = interior_walk_near(feet_w, WALK_NAMED_M, &label)) >= 0 && label && label[0])
					aim = AIM_DOOR, aim_index = i, aim_label = label;
			}
		}
		perf_end(PERF_LOOK);
		perf_lap("talk");
		int talk_to = -1, talk_again = 0;
		services_update(); // (the training menu, the gate guard's choices: once they've had their say)
		if (test_talk < 0 && !menu_is_open() && !paused) { // (a favour's, a brawl's next words: talking again)
			int again = quests_take_reopen();
			if (again < 0)
				again = riding_take_reopen(); // (Skulvar's offer, once he's said he has a horse)
			if (again >= 0)
				test_talk = again, talk_again = 1;
		}
		if (test_talk >= 0) // (an autotest's "talk": as if you'd turned to them and pressed)
			pressed = 1, aim = AIM_PERSON, aim_index = test_talk, test_talk = -1;
		if (test_ride) // (an autotest's "ride": as if you'd walked up to your horse and pressed)
			pressed = 1, aim = riding_mounted() ? AIM_NONE : AIM_HORSE, test_ride = 0;
		if (pressed && beast_active() && aim != AIM_NONE && aim != AIM_DOOR && aim != AIM_SWING) { // (the beast: doors, and feeding)
			if (aim == AIM_BODY && creatures_health_share(aim_index) <= 0.0f && beast_fed_on(aim_index, &vitals))
				ui_notice("You feed", 2.0f);
			else
				ui_notice("You can't do that in beast form", 2.0f);
			pressed = 0;
		}
		if (pressed && aim == AIM_HORSE && !riding_mounted()) { // Into the saddle
			float at[3];
			if (riding_mount(at, &yaw)) // (no horse of yours: nothing - its position was left unset, and you were nowhere)
				player.x = at[0], player.feet = at[1] + 0.05f, player.z = at[2], player.vy = 0.0f;
			pressed = 0;
		} else if (pressed && riding_mounted() && aim != AIM_PERSON) { // Down (and through the door, if that's what it was)
			float at[3];
			riding_dismount(at);
			player.x = at[0], player.feet = at[1], player.z = at[2], player.vy = 0.0f;
			if (aim != AIM_DOOR)
				pressed = 0;
		}
		if (pressed && aim != AIM_NONE)
			effects_end_invisibility();
		if (pressed && aim == AIM_PERSON && actors_corpse(aim_index)) { // (a scene's dead: what they carried - then bare under it)
			const int pockets = items_npc_pockets(actors_id(aim_index));
			if (pockets >= 0) {
				corpse_search = aim_index;
				corpse_handle = items_open_container(0x40000000u | (uint32_t)aim_index, pockets);
				menu_open_container(corpse_handle, talk_name(aim_index));
			}
		} else if (pressed && aim == AIM_PERSON) {
			const int pockets = items_npc_pockets(actors_id(aim_index));
			if (sneaking && pockets >= 0) {
				if (aim_index < MAX_POCKET_NPCS && caught_by_s[aim_index] > 0.0f)
					ui_notice("They're keeping an eye on you", 2.0f);
				else
					menu_open_pockets(items_open_container(0x80000000u | (uint32_t)aim_index, pockets), aim_index,
					                  talk_name(aim_index));
			} else {
				// Someone with something for you (a quest's topic, a merchant's wares): their
				// greeting, then what you can say; anyone else just greets you
				const char *opts[12];
				int n = quests_topics(aim_index, opts, 6);
				const int vendor = items_vendor(actors_id(aim_index));
				const int trading = vendor >= 0 && items_vendor_open_now(vendor, lighting_sky()->hours);
				dialogue_topics = n, dialogue_vendor = trading ? vendor : -1;
				if (trading)
					opts[n++] = "Let me see what you have for sale.";
				dialogue_follow = n; // (following you, or asked to: Skyrim's follower topics)
				n += followers_topics(aim_index, opts + n, 11 - n);
				dialogue_service = n; // (a trainer: their skill)
				n += services_topics(aim_index, opts + n, 11 - n);
				dialogue_ride = n; // (the stablemaster: a horse)
				n += riding_topics(aim_index, opts + n, 11 - n);
				dialogue_end = n; // ("Goodbye.": past every range - it once fell in the stablemaster's, Skulvar's line in anyone's mouth)
				talk_to = talk_again ? -1 : aim_index; // (talking again: no greeting over what they've said)
				if (n > 0) {
					opts[n++] = T_DIAL_000D8BE8_FULL0;
					dialogue_with = aim_index;
					menu_open_dialogue(talk_name(aim_index), opts, n);
				}
			}
		}
		int caught = menu_take_caught();
		if (caught >= 0) {
			char text[96];
			snprintf(text, sizeof(text), "%s caught you! (the pickpocket failed)", talk_name(caught));
			ui_notice(text, 3.0f);
			const float feet_c[3] = {player.x, player.feet, player.z};
			crime_commit(CRIME_PICKPOCKET, 0, feet_c, crime_actor_hold(caught));
			actors_attend(caught, player.x, player.z, 4.0f);
			if (caught < MAX_POCKET_NPCS)
				caught_by_s[caught] = CAUGHT_WARY_S;
		}
		perf_lap("talk_update_use");
		if (!paused)
			talk_update(dt, cam_x, cam_z, yaw, talk_to);
		if (test_door >= 0) // (an autotest's "usedoor": as if you'd walked up to it and pressed)
			pressed = 1, aim = AIM_DOOR, aim_index = test_door, aim_label = test_door_label, test_door = -1;
		{ // Whiterun's gate: shut (Skyrim's key-only lock) INFO 000D197C till the guard
		  // lets you in - then open to you for good
			const int place = interior_current() >= 0 ? interior_current() : worlds_current() > 0 ? -2 : -1;
			if (pressed && aim == AIM_DOOR && services_gate_stop(aim_index, aim_label, place))
				pressed = 0; // (INFO 000D197C)
			else if (pressed && aim == AIM_DOOR && aim_lock >= 0 && services_gate_open(aim_label, place))
				locks_unlock(aim_lock);
		}
		if (pressed && (aim == AIM_DOOR || aim == AIM_SWING) && aim_lock >= 0 && locks_locked(aim_lock) && quests_door(aim_lock))
			pressed = 0; // (Esbern's door: his voice through it - "Go away!")
		if (pressed && (aim == AIM_DOOR || aim == AIM_SWING) && aim_lock >= 0 && locks_locked(aim_lock)) {
			lock_door_kind = aim, lock_door = aim_index; // (on through it once it's picked)
			if (!locks_try(aim_lock)) {
				pressed = 0;
				const float feet_l[3] = {player.x, player.feet, player.z};
				if (lockpick_active() && locks_owned(aim_lock) && !quick_hands()) // (someone's door picked where they'd see: trespass)
					crime_commit(CRIME_TRESPASS, 0, feet_l, -1);
			}
		}
		if (pressed && aim == AIM_DOOR)
			transition = 1, pending_door = aim_index, sfx_play("door_open", 0.7f, 0.0f);
		if (lock_go_door >= 0 && !transition && !paused) // (picked open: through)
			transition = 1, pending_door = lock_go_door, lock_go_door = -1, sfx_play("door_open", 0.7f, 0.0f);
		if (opening.use_door >= 0 && !transition && !paused) // (the caves' mouth: out as you walk into it)
			transition = 1, pending_door = opening.use_door;
		else if (pressed && opening.prompt_door >= 0 && opening.prompt && !transition) // ("(O) Skyrim" pressed: out - it did nothing)
			transition = 1, pending_door = opening.prompt_door, sfx_play("door_open", 0.7f, 0.0f);
		{ // (any dungeon's walk-through way out - Embershard's back door, a cave's mouth: through as you walk in)
			const float feet_now[3] = {player.x, player.feet, player.z};
			const int walk = !transition && !paused && !front ? interior_walk_door(feet_now) : -1;
			if (walk >= 0)
				transition = 1, pending_door = walk;
		}
		const float stone_feet[3] = {player.x, player.feet, player.z};
		const char *stone_label = NULL;
		const int stone = aim == AIM_NONE && interior_current() < 0 && worlds_current() == 0 ? stones_near(stone_feet, &stone_label) : -1;
		if (stone >= 0 && !aim_label)
			aim_label = stone_label; // ("(O) Activate The Warrior Stone")
		{ // A word wall come up to: as Skyrim's - the whisper, the Word of Power's music, its light
			// drifting into you for a few seconds, then the word learned (a dragon's soul unlocks it)
			static int wall = -1, whisper = -1;
			static float wall_t;
			static const char *wall_word;
			if (wall < 0 && !paused && !transition && fade <= 0.0f && (wall = walls_near(stone_feet, interior_current(), &wall_word)) >= 0) {
				wall_t = 0.0f;
				static const char *const PIECE[3] = {"mus_special_wordofpower_01", "mus_special_wordofpower_02",
				                                     "mus_special_wordofpower_03"};
				music_special(PIECE[(unsigned)rand() % 3u]);
				whisper = sfx_loop("wordwall_whisper", 0.9f);
				sfx_play("wordwall_glow", 0.8f, 0.0f);
				game_log("wall %d: learning %s", wall, wall_word);
			}
			{ // (its word glowing blue while there's one for you, as Skyrim's walls do; bright while it's learned)
				float wpos[3];
				if (wall >= 0 && walls_pos(wall, wpos))
					spellfx_wall_glow(wpos, 1.0f);
				else if (walls_glowing(stone_feet, interior_current(), wpos) >= 0)
					spellfx_wall_glow(wpos, 0.0f);
			}
			if (wall >= 0 && !paused) {
				wall_t += frame_s;
				float from[3];
				if (walls_pos(wall, from) && wall_t < WALL_LEARN_S - 0.5f) {
					from[1] += 1.6f;
					const float chest[3] = {player.x, player.feet + 1.2f, player.z};
					spellfx_word(from, chest, frame_s);
				}
				shout_flash = fmaxf(shout_flash, 0.12f), shout_flash_rgb[0] = 150, shout_flash_rgb[1] = 205,
				shout_flash_rgb[2] = 255;
				if (wall_t >= WALL_LEARN_S) {
					const char *learned = shouts_learn_word(wall_word);
					if (learned)
						ui_notice(learned, 7.0f);
					sfx_stop(whisper), whisper = -1;
					sfx_play("shout_learn", 1.0f, 0.0f);
					shout_flash = 0.5f;
					walls_spend(wall), wall = -1;
				}
			}
		}
		if (pressed && stone >= 0) { // (a standing stone's blessing: one at a time)
			const char *told = NULL;
			stones_take(stone, &told);
			if (told)
				ui_notice(told, 6.0f);
			sfx_play("mag_heal_lp", 0.8f, 0.0f);
		} // (activate at nothing: nothing - it sneaked too, the user's Nov 8: sneak is its own button, L by default)
		if (pressed && aim == AIM_SWING)
			doors_toggle(aim_index);
		if (pressed && aim == AIM_ACTIVATOR)
			activators_use(aim_index);
		if (pressed && aim == AIM_FURNITURE) {
			const int kind = furniture_kind(aim_index);
			float pos[3], heading;
			const char *in, *idle, *out;
			if (kind == FURN_ALCHEMY) {
				magic_set_readied(0);
				menu_open_alchemy();
			} else if (kind == FURN_CRAFT) { // (a forge, a cooking pot, an arcane enchanter...)
				magic_set_readied(0);
				menu_open_crafting(furniture_station(aim_index));
			} else if (kind == FURN_RESOURCE) { // (an ore vein, a chopping block: its tool in your pack)
				int item, per_use, total, ntools;
				const int *tools;
				const uint32_t ref = furniture_ref(aim_index);
				if (items_resource(ref, &item, &per_use, &total, &tools, &ntools)) {
					int has_tool = ntools == 0;
					for (int k = 0; k < ntools; k++)
						has_tool |= items_have(tools[k]) > 0;
					char text[96];
					if (!has_tool) {
						ui_notice(total ? "You need a pickaxe to mine this" : "You need a woodcutter's axe to chop wood", 2.5f);
					} else if (total && items_taken(ref)) {
						ui_notice("This vein is depleted", 2.0f);
					} else {
						const int got = total ? total : per_use;
						items_add(item, got);
						if (total)
							items_set_taken(ref); // (mined out: back with the cell's reset)
						sfx_play(total ? "wpn_hit_blunt" : "wpn_hit_blade", 0.8f, 0.0f);
						snprintf(text, sizeof(text), got > 1 ? "%s (%d) added" : "%s added", items_name(item), got);
						ui_notice(text, 2.0f);
					}
				}
			} else if (furniture_seat(aim_index, player.x, player.z, pos, &heading, &in, &idle, &out)) {
				magic_set_readied(0); // (hands down to sit)
				if (actors_player_seat(pos, heading, in, idle, out))
					in_bed = kind == FURN_BED || kind == FURN_BEDROLL, bed_menu = 0;
				else if (kind == FURN_BED || kind == FURN_BEDROLL)
					menu_open_sleep(); // (no clips for it: just the Sleep menu)
				else
					ui_notice("You can't sit there", 2.0f);
			}
		}
		{ // (a puzzle's darts)
			const float hurt = activators_take_damage();
			if (hurt > 0.0f && dead_t < 0.0f) {
				vitals.health -= hurt, vitals.shown_for = 0.0f;
				if (vitals.health <= 0.0f)
					vitals.health = 0.0f, dead_t = 0.0f;
			}
		}
		if (pressed && aim == AIM_ARROW)
			projectiles_take(aim_index);
		if (pressed && aim == AIM_BODY && creatures_cut_down(aim_index)) {
			// (Arvel out of the spider's web)
		} else if (pressed && aim == AIM_BODY && creatures_guard_talk(aim_index)) {
			// (a guard's word to you)
		} else if (pressed && aim == AIM_BODY) { // (a body: what it carried)
			const char *name = NULL;
			creatures_set_arrow_recovery(perks_apply(EP_MOD_RECOVER_ARROW, 33.0f, NULL) / 100.0f); // (Hunter's Discipline: 66)
			const int handle = creatures_search(aim_index, &name);
			if (handle >= 0) {
				body_search = aim_index, body_handle = handle;
				quests_body_searched(handle, interior_current()); // (the Dragonstone on Bleak Falls' last draugr)
				menu_open_container(handle, name);
			}
		}
		if (magic_take_telekinesis() && !paused) { // Telekinesis held: what you aim at, from afar, to your hand
			static float tk_wait;
			if ((tk_wait -= frame_s) <= 0.0f)
				for (float d = 2.0f; d <= 18.0f; d += 2.0f) {
					const float from[3] = {tk_eye[0] + tk_look[0] * d, tk_eye[1] + tk_look[1] * d, tk_eye[2] + tk_look[2] * d};
					float tt;
					const char *tl = NULL;
					const int far_i = loot_aim(from, tk_look, &tt, &tl);
					if (far_i < 0)
						continue;
					int handle = -1;
					const char *name = NULL;
					if (loot_activate(far_i, &handle, &name) != 2) {
						ui_notice("Drawn to your hand", 2.0f);
						sfx_play("itm_drop", 0.6f, 0.0f);
					}
					tk_wait = 0.6f;
					break;
				}
		}
		if (pressed && aim == AIM_LOOT) {
			int handle = -1;
			const char *name = NULL;
			if (loot_activate(aim_index, &handle, &name) == 2) {
				quests_container_opened(handle, interior_current()); // (the Dragonstone's chest)
				menu_open_container(handle, name);
			} else if (lockpick_active() && loot_owned(aim_index) && !quick_hands()) { // (someone's chest picked where they'd see: trespass)
				const float feet_l[3] = {player.x, player.feet, player.z};
				crime_commit(CRIME_TRESPASS, 0, feet_l, -1);
			}
		}
		if (!paused) {
			doors_keep_npc_open(intro_active() && interior_current() < 0); // (Helgen's gate: open for the carts, and left open)
			doors_update(dt);
		}
		ui_prompt(opening.prompt ? opening.prompt : aim_label);
		ui_crosshair(!paused && !transition && opts->hud_crosshair, aim != AIM_NONE);
		perf_lap("inventory_actions");
		// Eating and drinking (chosen in the inventory)
		{ // Dropped from the inventory: at your feet, a step ahead
			int drop_n;
			const int drop = menu_take_drop(&drop_n);
			if (drop >= 0) {
				const float yr = yaw * (float)M_PI / 180.0f;
				float at[3] = {player.x + sinf(yr) * 0.7f, player.feet, player.z - cosf(yr) * 0.7f};
				const float above[3] = {at[0], player.feet + 1.0f, at[2]}, below[3] = {at[0], player.feet - 2.0f, at[2]};
				const float t = collision_ray_ground(above, below);
				if (t < 1.0f)
					at[1] = above[1] + (below[1] - above[1]) * t;
				const int n = items_drop(drop, drop_n, at, -yr, interior_current());
				if (n > 0) {
					char text[80];
					snprintf(text, sizeof(text), n > 1 ? "%s (%d) dropped" : "%s dropped", items_name(drop), n);
					ui_notice(text, 2.0f);
					sfx_play("itm_drop", 0.6f, 0.0f);
					loot_refresh_dropped();
				}
			}
		}
		// A level-up's bonus, chosen in Skills (while the menu's still open): as in Skyrim, the
		// chosen one grows by 10 and all three are restored in full
		const int level_choice = menu_take_level_choice();
		if (level_choice) {
			vitals.max_health += level_choice == 1 ? 10.0f : 0.0f;
			vitals.max_magicka += level_choice == 2 ? 10.0f : 0.0f;
			vitals.max_stamina += level_choice == 3 ? 10.0f : 0.0f;
			vitals.health = vitals.max_health, vitals.magicka = vitals.max_magicka, vitals.stamina = vitals.max_stamina;
			vitals.shown_for = 0.0f;
		}
		int use = menu_take_use();
		if (use >= 0 && items_use(use, &vitals)) {
			sfx_play(items_category(use) == CAT_POTIONS ? "itm_drink" : "itm_eat", 0.8f, 0.0f);
			char text[80];
			snprintf(text, sizeof(text), "%s %s", items_name(use), "used");
			if (items_type(use) == IT_INGR) { // (eaten: its first effects learned - Experimenter, more)
				const int before = items_known(use);
				items_learn(use, (1 << alchemy_eat_reveals()) - 1);
				ItemEffect e[ITEM_EFFECTS_MAX];
				const int n = items_effects(use, e, ITEM_EFFECTS_MAX);
				for (int k = 0; k < n; k++)
					if ((items_known(use) & ~before) >> k & 1) {
						const MagicEffect *m = items_magic_effect(e[k].mgef);
						snprintf(text, sizeof(text), "%s: %s learned", items_name(use), m ? m->name : "an effect");
						break;
					}
			}
			ui_notice(text, 2.0f);
		}
		perf_lap("transitions");
		if (transition == 1 && (fade += 1.0f / (60.0f * DOOR_FADE_OUT_S)) >= 1.0f) {
			fade = 1.0f;
			float pos[3], arrive_yaw;
			projectiles_clear();
			interior_walk_disarm(); // (arriving by one: not straight back through its other side)
			if (interior_use_door(pending_door, pos, &arrive_yaw)) {
				autosave = 1; // (on arrival, as Skyrim saves at load doors)
				airborne = 0;
				player.x = pos[0], player.feet = pos[1], player.z = pos[2], player.vy = 0.0f;
				yaw = arrive_yaw, pitch = 0.0f;
				if (interior_current() < 0) { // back outside: load around the arrival first
					for (int pass = 0; pass < 2; pass++) {
						terrain_update(player.x, player.z);
						objects_update(player.x, player.z);
						navmesh_update(player.x, player.z);
						stream_flush();
					}
				}
			}
			transition = 3, settle_s = 0.0f, settle_good = 0, airborne = 0, travel_landing = 2;
		} else if (transition == 3) { // Black, loading, until the streaming's done and the frames come
			fade = 1.0f;              // steadily (the new place's first frames were the slowest), as Skyrim's
			if (settle_s == 0.0f)
				held_feet = player.feet;
			settle_s += frame_s;
			float ground; // (fast travelled: held on the ground as it streams in - the land wasn't there at first,
			              //  you were left at your old height, and fell from the sky)
			if (travel_landing == 2) { // (through a door, loaded, respawned: on the floor just under where you came in -
				// the fast travel's look down from 60 m above put you on Dragonsreach's roof, on Whiterun's gate)
				const float top[3] = {player.x, held_feet + 1.2f, player.z}, low[3] = {player.x, held_feet - 20.0f, player.z};
				const float t = collision_ray_ground(top, low);
				player.feet = t < 1.0f ? top[1] + (low[1] - top[1]) * t + 0.05f : held_feet;
				if (t >= 1.0f && interior_current() < 0 && terrain_height_at(player.x, player.z, &ground) && ground < held_feet && ground > held_feet - 20.0f)
					player.feet = ground + 0.05f;
				player.vy = 0.0f, airborne = 0;
			} else if (travel_landing == 1 && interior_current() < 0 && !isnan(travel_marker_y)) {
				// (onto what's at Skyrim's marker - High Hrothgar's steps - not the first thing down from
				// high over the land: its terrace by the roof. Skyrim moves you to the navmesh nearest it:
				// High Hrothgar's marker lies on the bare land under its courtyard's stonework, 10 m down -
				// you were set down underneath the building)
				NavRef nr;
				float on[3];
				if (dry_navmesh_near(travel_marker_x, travel_marker_y, travel_marker_z, on)) {
					player.x = on[0], player.feet = on[1] + 0.05f, player.z = on[2];
				} else {
					const float top[3] = {player.x, travel_marker_y + 3.0f, player.z}, low[3] = {player.x, travel_marker_y - 5.0f, player.z};
					const float t = collision_ray_ground(top, low);
					player.feet = (t < 1.0f ? top[1] + (low[1] - top[1]) * t : travel_marker_y) + 0.05f;
				}
				player.vy = 0.0f, airborne = 0;
			} else if (travel_landing && interior_current() < 0 && terrain_height_at(player.x, player.z, &ground)) {
				const float top[3] = {player.x, ground + 60.0f, player.z}, low[3] = {player.x, ground - 5.0f, player.z};
				const float t = collision_ray_ground(top, low);
				player.feet = (t < 1.0f ? top[1] + (low[1] - top[1]) * t : ground) + 0.05f;
				player.vy = 0.0f, airborne = 0;
			} else if (travel_landing) { // (indoors, or the land not there yet: held where you came in - not
				player.vy = 0.0f, airborne = 0; // falling through a floor still loading, a fall that killed you)
				player.feet = held_feet;
			}
			settle_good = frame_s < SETTLED_FRAME_S ? settle_good + 1 : 0;
			ui_loading(travel_landing == 1 && travel_title[0] ? travel_title : interior_place(), settle_s); // (fast travel: the
			// place picked on the map - Riverwood's travel said "Whiterun", the city you'd left)
			actors_set_loading(1);
			const int big = interior_current() < 0 || actors_in_view() >= BIG_CROWD;
			if ((stream_pending() == 0 && actors_meshes_waiting() == 0 && settle_good >= (big ? SETTLED_BIG_FRAMES : SETTLED_FRAMES) &&
			     settle_s > (big ? SETTLE_BIG_MIN_S : SETTLE_MIN_S)) || settle_s > (big ? SETTLE_BIG_MAX_S : SETTLE_MAX_S)) {
				transition = 2, travel_title[0] = 0;
				actors_set_loading(0);
				ui_loading(NULL, 0.0f);
				game_log("loading: %.1f s (%s, %d people in view)", settle_s, big ? "a big place" : "a room", actors_in_view());
			}
		} else if (transition == 2 && (fade -= 1.0f / (60.0f * DOOR_FADE_IN_S)) <= 0.0f) {
			fade = 0.0f;
			transition = 0;
			if (autosave && dead_t < 0.0f)
				autosave_wait = AUTOSAVE_WAIT_S; // (once standing: saved with the scene's picture)
			autosave = 0;
		}
		// The autosave: only once you're standing on something (never mid-fall), with this
		// frame's scene as its picture
		if (autosave_wait > 0.0f && !transition) {
			autosave_wait -= frame_s;
			if ((player.grounded || player.swimming) && dead_t < 0.0f)
				autosave_now = 1, capture_pending = 1, autosave_wait = 0.0f;
		}
		if (opening.teleport || opening.enter >= 0) { // (the opening moves you: loaded behind black)
			if (opening.enter >= 0) {
				const float zero[3] = {0, 0, 0};
				interior_go(opening.enter, zero, opening.look_yaw);
				yaw = opening.look_yaw, pitch = 0.0f;
			}
			player.x = opening.pos[0], player.feet = opening.pos[1] + 0.05f, player.z = opening.pos[2], player.vy = 0.0f;
			projectiles_clear();
			fade = 1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, airborne = 0, travel_landing = 2;
		}
		{ // (a quest moving you: the carriage to the Embassy's party - loaded behind black)
			int q_in;
			float q_at[3], q_yaw;
			if (quests_take_move(&q_in, q_at, &q_yaw) && interior_go(q_in, q_at, q_yaw)) {
				player.x = q_at[0], player.feet = q_at[1] + 0.05f, player.z = q_at[2], player.vy = 0.0f;
				yaw = q_yaw, pitch = 0.0f;
				projectiles_clear();
				fade = 1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, airborne = 0, travel_landing = 2;
				game_log("quest: moved to place %d at %.1f %.1f %.1f", q_in, q_at[0], q_at[1], q_at[2]);
			}
		}
		perf_lap("save_load_death");
		// Saving and loading (the pause menu)
		if (title_loading == 2 && !(pad.buttons & SCE_CTRL_CROSS)) // (the X that chose Continue: not a pick in the list)
			menu_open_load(), title_loading = 1;
		int request_slot = SAVE_NEW;
		int request = menu_take_request(&request_slot);
		if (test_travel >= 0) // (an autotest's "travel": as if picked on the map)
			request = MENU_TRAVEL, request_slot = test_travel, test_travel = -1;
		if (request == MENU_SAVE && intro_active() && !intro_stage()) {
			ui_notice("You can't save right now", 3.0f); // (Helgen, before the keep: as Skyrim)
		} else if (request == MENU_SAVE) {
			SaveGame g;
			fill_save(&g, &player, yaw, pitch, &vitals);
			ui_notice(write_game(&g, request_slot) ? "Game saved" : "Couldn't save the game", 3.0f);
		} else if (request == MENU_TRAVEL) { // fast travel (the map): out to the place, the time it took
			int kind, disc, in_region;
			const char *name;
			float tx, tz;
			const float here[3] = {player.x, player.feet, player.z};
			if (!worldmap_marker(request_slot, &kind, &name, &tx, &tz, &disc, &in_region)) {
			} else if (creatures_hostile_near(here, 60.0f)) {
				ui_notice("You cannot fast travel when enemies are nearby", 3.0f);
			} else {
				const float km = interior_current() < 0 ? hypotf(tx - player.x, tz - player.z) / 1000.0f : 1.0f;
				const float at[3] = {tx, 0.0f, tz};
				const int city = worldmap_marker_world(request_slot); // (Whiterun, Dragonsreach: into the city itself)
				interior_go(city > 0 ? -1 - city : -1, at, yaw);
				player.x = tx, player.z = tz, player.vy = 0.0f;
				projectiles_clear();
				for (int pass = 0; pass < 2; pass++) {
					terrain_update(player.x, player.z);
					objects_update(player.x, player.z);
					navmesh_update(player.x, player.z);
					stream_flush();
				}
				float ground;
				player.feet = terrain_height_at(player.x, player.z, &ground) ? ground + 0.1f : player.feet;
				const float marker_y = worldmap_marker_height(request_slot); // (on Skyrim's marker - High Hrothgar's steps, not its roof)
				travel_marker_y = marker_y, travel_marker_x = player.x, travel_marker_z = player.z;
				if (!isnan(marker_y)) // (where Skyrim puts you: its marker - the land can run high over a place's steps there)
					player.feet = marker_y + 0.1f;
				riding_traveled(here, (const float[3]){player.x, player.feet, player.z}, -1); // (your horse: along with you)
				travel_landing = 1;
				lighting_advance_hours(fmaxf(1.0f, km / FAST_TRAVEL_KMH)); // (Skyrim: about a horse's pace)
				fade = 1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, airborne = 0, travel_landing = 1;
				snprintf(travel_title, sizeof(travel_title), "%s", name); // (the loading screen: where you're going)
				char text[80];
				snprintf(text, sizeof(text), "Travelled to %s", name);
				ui_notice(text, 3.0f);
			}
		} else if (title_loading == 1 && request != MENU_LOAD && !menu_is_open()) {
			front_load = 1; // (backed out of the list: the newest, as before)
		}
		if (request == MENU_LOAD || front_load) {
			title_loading = 0;
			if (front_load)
				request_slot = save_latest(); // (the title's Continue: the newest save)
			front_load = 0;
			if (load_game(request_slot, &player, &yaw, &pitch, &vitals)) {
				fade = 1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, dead_t = -1.0f, airborne = 0, travel_landing = 2;
				ui_notice("Game loaded", 3.0f);
			} else {
				ui_notice("There's no saved game", 3.0f);
			}
		}
		// Dying: the screen goes dark, then the last save (or the way in, if there's none)
		if (dead_t >= 0.0f) {
			dead_t += frame_s;
			fade = fmaxf(fade, fminf(1.0f, dead_t / DEATH_FADE_S));
			if (dead_t >= DEATH_RESPAWN_S && intro_died()) { // (in the keep: back at its doors with the one you chose)
				vitals_init(&vitals);
				effects_reset();
				creatures_cure_player_poison();
				dead_t = -1.0f, airborne = 0;
			} else if (dead_t >= DEATH_RESPAWN_S) {
				if (!load_game(save_latest(), &player, &yaw, &pitch, &vitals)) {
					float pos[3];
					interior_entry(pos, &yaw);
					if (interior_current() >= 0)
						player.x = pos[0], player.feet = pos[1], player.z = pos[2];
					player.vy = 0.0f;
					vitals_init(&vitals);
					effects_reset();
				}
				dead_t = -1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, airborne = 0, travel_landing = 2;
			}
		}
		perf_lap("autotest");
		static char shot_name[64]; // (a scripted test run's picture, taken once the frame's drawn)
		for (AutotestAction act; (act = autotest_next(clock_s)).kind != AT_NONE;) {
			if (act.kind == AT_QUIT) {
				game_log("autotest: done");
				sceKernelExitProcess(0);
			} else if (act.kind == AT_WEAR) { // (everything carried that's worn or held: put on)
				int carried[256];
				const int nc = items_inventory(carried, 256);
				for (int k = 0; k < nc; k++)
					if (items_type(carried[k]) == IT_ARMO || items_type(carried[k]) == IT_WEAP)
						game_log("autotest: wear %s -> %d (worn %d)", items_name(carried[k]), equip_toggle(carried[k]),
						         equip_worn(carried[k]));
			} else if (act.kind == AT_OPENING && !strcmp(act.arg, "done")) { // ("opening done": past it, everyone at their day)
				intro_load(1);
				for (int i = 0; i < actors_count(); i++)
					actors_script_release(i);
			} else if (act.kind == AT_OPENING && !strcmp(act.arg, "title")) { // ("opening title": what the title's New Game does - the
				racemenu_close(), racemenu_on = 0, race_zoom = race_facial_zoom = 0; //  real path, with the cart's view primed; a test run skips the title,
				begin_opening = 1;                                          //  starting on the race menu: put away, as the title's New Game never shows it)
			} else if (act.kind == AT_OPENING) { // (Helgen from the cart, as a new game)
				quests_new_game(1);
				objects_set_switches(0);
				equip_strip();
				intro_start();
				if (!strcmp(act.arg, "escape"))
					intro_jump_to_escape();
				else if (!strcmp(act.arg, "climb"))
					intro_jump_to_climb();
				else if (!strcmp(act.arg, "town"))
					intro_jump_to_town();
				else if (!strcmp(act.arg, "wall"))
					intro_jump_to_wall();
				else if (!strcmp(act.arg, "lineup"))
					intro_jump_to_lineup();
				else if (!strncmp(act.arg, "river", 5))
					intro_jump_to_riverwood(act.arg[5] != 'h'); // ("river": with Ralof, "riverh": with Hadvar)
				else if (!strncmp(act.arg, "keep", 4))
					intro_jump_to_keep(act.arg[4] == 'h' ? 0 : act.arg[4] == 'e' ? 2 : 1); // ("keep": with Ralof, "keeph": Hadvar, "keepe": Ralof, at the caves' end)
			} else if (act.kind == AT_WARP) { // ("warp x y z": there, in the place you're in - a dungeon's far end)
				float wx = 0.0f, wy = 0.0f, wz = 0.0f;
				int dn = -1;
				if (sscanf(act.arg, "door %d", &dn) == 1) { // ("warp door 2": onto that door - its list logged)
					float doors[32][3];
					const char *labels[32];
					const int nd = interior_doors(doors, labels, 32);
					for (int k = 0; k < nd; k++)
						game_log("autotest: door %d '%s' (%.1f %.1f %.1f)", k, labels[k] ? labels[k] : "", doors[k][0], doors[k][1], doors[k][2]);
					if (dn < nd)
						player.x = doors[dn][0], player.feet = doors[dn][1] + 0.3f, player.z = doors[dn][2], player.vy = 0.0f, airborne = 0;
				} else if (sscanf(act.arg, "%f %f %f", &wx, &wy, &wz) == 3)
					player.x = wx, player.feet = wy + 0.3f, player.z = wz, player.vy = 0.0f, airborne = 0;
			} else if (act.kind == AT_PULL) { // ("pull": the lever or chain nearest you)
				const float at[3] = {player.x, player.feet, player.z};
				game_log("autotest: pull %s", activators_pull_near(at, 8.0f) ? "a lever" : "- none near");
			} else if (act.kind == AT_TAKE && !strcmp(act.arg, "@container")) { // ("take @container": as a container opened here - a quest's box)
				quests_container_opened(-1, interior_current());
				game_log("autotest: a container opened here");
			} else if (act.kind == AT_TAKE) { // ("take Iron Sword": the nearest of it, picked up - from beside it)
				const float here[3] = {player.x, player.feet, player.z};
				float at[3];
				const int t = loot_find(act.arg, here, at);
				int handle = -1;
				const char *name = NULL;
				if (t >= 0)
					player.x = at[0] + 0.6f, player.z = at[2], player.feet = at[1] + 0.2f, player.vy = 0.0f;
				const int got = t >= 0 ? loot_activate(t, &handle, &name) : -1;
				game_log("autotest: take '%s' -> %d at %.1f %.1f %.1f (result %d, have %d)", act.arg, t, t >= 0 ? at[0] : 0.0f,
				         t >= 0 ? at[1] : 0.0f, t >= 0 ? at[2] : 0.0f, got, items_have(items_find_name(act.arg)));
				if (got == 2) { // (a container: opened as you would - a quest's things put in - and emptied)
					quests_container_opened(handle, interior_current());
					int its[64], counts[64];
					const int n = items_in_container(handle, its, counts, 64);
					for (int i = 0; i < n; i++) {
						game_log("autotest: took %d %s", counts[i], items_name(its[i]));
						items_take(handle, its[i], counts[i]);
					}
					loot_close();
				}
			} else if (act.kind == AT_SLAY) { // ("slay": the foes about you struck down - a test past a fight)
				const float at[3] = {player.x, player.feet, player.z};
				const float r = atof(act.arg); // ("slay 200": that far round)
				game_log("autotest: slay -> %d", creatures_area(at, r > 0.0f ? r : 20.0f, 100000.0f));
			} else if (act.kind == AT_BEFRIEND) { // ("befriend uthgerd": a friend, as a favour done would make them)
				for (int i = 0; i < actors_count(); i++)
					if (!strcmp(actors_id(i), act.arg))
						followers_befriend(i);
				game_log("autotest: befriend %s", act.arg);
			} else if (act.kind == AT_HAIR) { // ("hair DarkElfRace 0 8": that race's Custom face, a man's (1 a woman's), its hair 8)
				char race_eid[32];
				int woman = 0, hair = 0;
				float turn = 0.0f; // (a 4th number: degrees round - the back of the head)
				if (sscanf(act.arg, "%31s %d %d %f", race_eid, &woman, &hair, &turn) >= 3)
					for (int ri = 0; ri < races_count(); ri++) {
						const Race *r = races_get(ri);
						if (!r || strcmp(r->eid, race_eid))
							continue;
						for (int f = 0; f < r->face_count; f++)
							if (face_custom(r, f) && face_female(r, f) == woman) {
								ChargenLook l;
								if (chargen_preset(r->eid, woman, 0, &l))
									player_look = l, look_old_picks = 0;
								set_character(ri, f);
								player_look.pick[CG_HAIR] = (signed char)hair;
								chargen_apply(r->eid, woman, player_look.pick);
								game_log("autotest: hair %s %d -> %d of %d", race_eid, woman, hair, chargen_count(r->eid, woman, CG_HAIR));
								test_face_zoom = 1, race_turn = turn;
							}
					}
			} else if (act.kind == AT_LOOKAT) { // ("lookat x z": the view turned to that point)
				float lx, lz;
				float at[3];
				if (sscanf(act.arg, "%f %f", &lx, &lz) == 2)
					yaw = atan2f(lx - player.x, -(lz - player.z)) * 180.0f / (float)M_PI, pitch = -5.0f;
				else if (creatures_find_titled(act.arg, at)) // ("lookat bandit": at one awake by its title)
					yaw = atan2f(at[0] - player.x, -(at[2] - player.z)) * 180.0f / (float)M_PI, pitch = -10.0f;
			} else if (act.kind == AT_STATE) { // ("state label": the player as things stand - an audit's before and after)
				const SkillState *sk = skills_state();
				char line[400];
				int n = snprintf(line, sizeof(line), "state %s: at %.1f %.1f %.1f in %s | H %.0f/%.0f M %.0f/%.0f S %.0f/%.0f | lv %d xp %.1f pend %d perks %d | gold %d items %d | follower %d | bounty",
				                 act.arg, player.x, player.feet, player.z, interior_current() >= 0 ? interior_id() : T_LCTN_000130FF_FULL0,
				                 vitals.health, vitals.max_health, vitals.magicka, vitals.max_magicka, vitals.stamina, vitals.max_stamina,
				                 sk->player_level, sk->player_xp, sk->pending, sk->perk_points, items_gold(), items_count(),
				                 followers_current());
				for (int h = 0; h < crime_hold_count() && n < (int)sizeof(line) - 8; h++)
					n += snprintf(line + n, sizeof(line) - n, " %d", crime_bounty(h));
				game_log("%s", line);
				n = 0;
				for (int k = 0; k < SKILL_COUNT && n < (int)sizeof(line) - 24; k++)
					n += snprintf(line + n, sizeof(line) - n, "%s%.6s %d+%.0f", k ? ", " : "skills: ", skills_name(k), sk->level[k], sk->xp[k]);
				game_log("%s", line);
				n = 0;
				for (int q = 0; q < QUEST_COUNT && n < (int)sizeof(line) - 12; q++)
					n += snprintf(line + n, sizeof(line) - n, "%s%d", q ? " " : "quests: ", quests_stage(q));
				game_log("%s", line);
			} else if (act.kind == AT_WATCH) { // ("watch generaltullius": logged every 2 s)
				for (int i = 0; i < actors_count(); i++)
					if (!strcmp(actors_id(i), act.arg))
						actors_watch(i);
			} else if (act.kind == AT_SKILL) { // ("skill speech 15": the Speech skill at 15 - a test of a check failed)
				int level = 0;
				char which[24] = "";
				if (sscanf(act.arg, "%23s %d", which, &level) == 2) // ("skill Destruction 40": any skill, by its name)
					for (int k = 0; k < SKILL_COUNT; k++)
						if (!strcasecmp(skills_name(k), which))
							skills_state()->level[k] = level;
				game_log("autotest: skill %s -> speech %d", act.arg, skills_state()->level[SK_SPEECH]);
			} else if (act.kind == AT_SEARCH) { // ("search": the bodies about you searched - what each holds)
				for (int i = 0; i < 32; i++) {
					const char *name = NULL;
					const int h = creatures_search(i, &name);
					if (h < 0)
						continue;
					quests_body_searched(h, interior_current()); // (as searching it would: a quest's thing put on it)
					int its[64], counts[64];
					const int n = items_in_container(h, its, counts, 64);
					game_log("autotest: search %s -> %d kinds%s%s", name ? name : "?", n, n > 0 ? ", first " : "",
					         n > 0 ? items_name(its[0]) : "");
					for (int j = 1; j < n; j++)
						game_log("autotest:   and %d %s", counts[j], items_name(its[j]));
				}
			} else if (act.kind == AT_DRAGON) { // ("dragon": one brought down beside you - its death, soul, bones)
				const float at[3] = {player.x, player.feet, player.z};
				if (!strncmp(act.arg, "kind:", 5)) { // ("dragon kind:hagraven": that kind a little ahead, hostile - any creature)
					const float r = yaw * (float)M_PI / 180.0f, ahead[3] = {player.x + sinf(r) * 9.0f, player.feet, player.z - cosf(r) * 9.0f};
					game_log("autotest: %s placed -> %d", act.arg + 5, creatures_place_dragon(act.arg + 5, ahead, r + (float)M_PI, 0));
				} else if (!strcmp(act.arg, "live") || !strcmp(act.arg, "perched")) { // ("dragon live": one landed a little ahead, coming for you)
					const float r = yaw * (float)M_PI / 180.0f, ahead[3] = {player.x + sinf(r) * 12.0f, player.feet, player.z - cosf(r) * 12.0f};
					game_log("autotest: dragon live -> %d", creatures_place_dragon(NULL, ahead, 0.0f, act.arg[0] == 'p'));
				} else
					game_log("autotest: dragon -> %d", creatures_test_dragon_dies(at));
			} else if (act.kind == AT_QGO) { // ("qgo time_wound": to a quest's place)
				game_log("autotest: qgo %s -> %d", act.arg, quests_test_goto(act.arg));
			} else if (act.kind == AT_FLOORWORD) { // ("floorword ro": its letters on the floor before you)
				const float yr = yaw * (float)M_PI / 180.0f;
				const float at[3] = {player.x + sinf(yr) * 1.6f, player.feet, player.z - cosf(yr) * 1.6f};
				wordfx_show(act.arg, at, yr, 6.0f);
			} else if (act.kind == AT_STRIKE) { // ("strike nazeem 40": a blow on them)
				char who[48];
				float dmg = 40.0f;
				sscanf(act.arg, "%47s %f", who, &dmg);
				int done = 0;
				for (int i = 0; i < actors_count(); i++) {
					float at[3];
					if (strcmp(actors_id(i), who))
						continue;
					if (brawl_active() && brawl_who() == i && actors_where(i, at)) { // (a brawl's, a spar's: a blow at them)
						const float eye[3] = {player.x, player.feet + EYE_HEIGHT, player.z};
						float d[3] = {at[0] - eye[0], at[1] + 1.2f - eye[1], at[2] - eye[2]};
						const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
						for (int k = 0; k < 3; k++)
							d[k] /= l > 0.01f ? l : 1.0f;
						done = brawl_strike(eye, d, l + 0.5f, dmg); // (from wherever you stand: a test of the bout, not your aim)
					} else
						done = people_test_strike(i, dmg);
				}
				game_log("autotest: strike %s %.0f -> %d", who, dmg, done);
			} else if (act.kind == AT_FOLLOW) { // ("follow hirelingjenassa": with you, as hired)
				int found = -1;
				if (act.arg[0] == '@') { // ("follow @farkas": put beside you as a quest does - not hired)
					const float at[3] = {player.x, player.feet, player.z};
					found = qk_beside(act.arg + 1, at, 1.5f);
				}
				for (int i = 0; i < actors_count() && act.arg[0] != '@'; i++)
					if (!strcmp(actors_id(i), act.arg))
						followers_assign(i), found = i;
				game_log("autotest: follow %s -> %d", act.arg, found);
			} else if (act.kind == AT_PERK) { // ("perk Power Bash": taken)
				game_log("autotest: perk '%s' -> %d", act.arg, perks_grant_named(act.arg));
			} else if (act.kind == AT_HP) { // ("hp 30": the health of those about you)
				const float at[3] = {player.x, player.feet, player.z};
				creatures_test_health(at, act.arg[0] ? (float)atof(act.arg) : 30.0f);
			} else if (act.kind == AT_TALK) { // ("talk hirelingjenassa": talking to them, wherever you're looking)
				for (int i = 0; i < actors_count(); i++)
					if (!strcmp(actors_id(i), act.arg))
						test_talk = i;
				game_log("autotest: talk %s -> %d", act.arg, test_talk);
			} else if (act.kind == AT_USEDOOR && act.arg[0] == '#') { // ("usedoor #1": through this room's door 1, as "warp door" lists them)
				float doors[32][3];
				const char *labels[32];
				const int nd = interior_doors(doors, labels, 32), k = atoi(act.arg + 1);
				if (k >= 0 && k < nd)
					test_door = k, test_door_label = labels[k];
				game_log("autotest: usedoor %s -> '%s'", act.arg, k >= 0 && k < nd ? labels[k] : "(none)");
			} else if (act.kind == AT_USEDOOR) { // ("usedoor Whiterun": to Tamriel's door into the city, and through it)
				const int k = interior_gate_door(act.arg, &test_door_label);
				float at[3];
				if (k >= 0 && interior_door_pos(k, at)) {
					player.x = at[0] + 2.0f, player.z = at[2], player.feet = at[1] + 0.3f, player.vy = 0.0f, airborne = 0;
					test_door = k;
				}
				game_log("autotest: usedoor %s -> %d '%s'", act.arg, k, k >= 0 ? test_door_label : "");
			} else if (act.kind == AT_WORD && act.arg[0] == '=') { // ("word =Clear Skies": that shout into the voice slot)
				for (int k = 0; k < shouts_count(); k++)
					if (!strcmp(shouts_name(k), act.arg + 1))
						shouts_equip(k), test_fast_shouts = 1;
				game_log("autotest: shout %s equipped -> %d", act.arg + 1, shouts_equipped());
			} else if (act.kind == AT_WORD) { // ("word Fus": a Word of Power learned and unlocked)
				const char *w = shouts_learn_word(act.arg);
				shouts_add_souls(1);
				for (int k = 0; k < shouts_count(); k++)
					if (shouts_learned(k) > shouts_known(k))
						shouts_unlock(k), shouts_equip(k); // (into the voice slot)
				test_fast_shouts = 1; // (a test: shout after shout)
				game_log("autotest: word %s -> %s", act.arg, w ? w : "(not one, or known)");
			} else if (act.kind == AT_TRAVEL) { // ("travel Bleak Falls Barrow": fast travel there, as from the map)
				for (int k = 0; k < worldmap_count(); k++) {
					int kind, disc, in_region;
					const char *nm;
					float mx, mz;
					if (worldmap_marker(k, &kind, &nm, &mx, &mz, &disc, &in_region) && !strcmp(nm, act.arg))
						test_travel = k;
				}
				game_log("autotest: travel %s -> %d", act.arg, test_travel);
			} else if (act.kind == AT_LOADSAVE) { // ("loadsave": the newest save, as the title's Continue)
				stage = STAGE_PLAY, front_load = 1;
				racemenu_close(), racemenu_on = 0; // (a test run starts in the race menu: closed)
			} else if (act.kind == AT_RIDE) { // ("ride": on your horse, or off it - "ride new": a horse of yours beside you first)
				if (!strcmp(act.arg, "new")) {
					const float horse[6] = {1.0f, (float)interior_current(), player.x + 2.0f, player.feet, player.z, 0.0f};
					riding_load(horse);
				}
				test_ride = 1;
			} else if (act.kind == AT_QUEST) { // ("quest 2 20": Dragon Rising at stage 20 - a test past the rest)
				int q = -1, st = 0;
				if (!strcmp(act.arg, "beast")) { // ("quest beast": the blood given, changed now)
					beast_give(), beast_ready_it(1);
					game_log("autotest: beast -> %d", beast_change(&vitals, 1));
				} else if (sscanf(act.arg, "co%d %d", &q, &st) == 2) { // ("quest co3 20": the Companions' The Silver Hand at 20)
					companions_set(q, st);
					game_log("autotest: companions %d stage %d", q, st);
				} else if (sscanf(act.arg, "mg%d %d", &q, &st) == 2) { // ("quest mg4 20": the College's (college.h's order) at 20)
					college_set(q, st);
					game_log("autotest: college %d stage %d", q, st);
				} else if (sscanf(act.arg, "tg%d %d", &q, &st) == 2) { // ("quest tg3 45": the Thieves Guild's (thieves.h's order))
					thieves_set(q, st);
					game_log("autotest: thieves %d stage %d", q, st);
				} else {
					if (sscanf(act.arg, "%d %d", &q, &st) == 2)
						quests_set(q, st);
					game_log("autotest: quest %d stage %d", q, st);
				}
			} else if (act.kind == AT_SLEEP) { // ("sleep": as a night in a bed - in a jail cell, the time served)
				test_sleep = 1;
			} else if (act.kind == AT_WALKTO) {
				float secs = 20.0f;
				float gy = player.feet;
				if (sscanf(act.arg, "%f %f %f %f", &walkto_at[0], &walkto_at[1], &secs, &gy) >= 2) {
					walkto_left = secs, walkto_still = 0.0f, walkto_last[0] = player.x, walkto_last[1] = player.z;
					walkto_goal[0] = walkto_at[0], walkto_goal[1] = walkto_at[1], walkto_n = walkto_i = 0;
					NavRef a, b;
					float pa[3], pb[3];
					if (navmesh_nearest(player.x, player.feet, player.z, 3.0f, &a, pa) &&
					    navmesh_nearest(walkto_at[0], gy, walkto_at[1], 4.0f, &b, pb))
						walkto_n = navmesh_path(a, pa, b, pb, walkto_pts, 96);
					game_log("walkto: %d corners along the navmesh to %.1f %.1f%s", walkto_n, walkto_at[0], walkto_at[1],
					         walkto_n ? "" : " (no way found: straight at it)");
				}
			} else if (act.kind == AT_MEASURE) { // ("measure": Options' Measure, here)
				test_measure = 1;
			} else if (act.kind == AT_DEBUG) { // ("debug": Debug controls on - no longer in Options)
				menu_set_debug(1);
				game_log("autotest: debug controls on");
			} else if (act.kind == AT_GOD) { // ("god": not to be killed - a run through the keep's fights)
				test_god = 1;
			} else if (act.kind == AT_EQUIP) { // ("equip Hunting Bow": put on / in the right hand)
				const int it = items_find_name(act.arg);
				if (it >= 0 && items_scroll(it)) { // (a scroll: into the right hand, readied)
					magic_set_scroll(HAND_RIGHT, it), magic_set_readied(1);
					game_log("autotest: equip '%s' -> scroll in hand", act.arg);
				} else
					game_log("autotest: equip '%s' -> %d", act.arg, it >= 0 && !equip_worn(it) ? equip_toggle(it) : -1);
			} else if (act.kind == AT_UNEQUIP) { // ("unequip Iron War Axe": out of your hands, into your pack; "unequip": what's in them)
				int wk;
				const int it = act.arg[0] ? items_find_name(act.arg) : equip_weapon(&wk);
				if (!act.arg[0]) // (and no spells: bare hands)
					magic_set_hand(HAND_LEFT, -1), magic_set_hand(HAND_RIGHT, -1);
				game_log("autotest: unequip '%s' -> %d", act.arg, it >= 0 && equip_worn(it) ? equip_toggle(it) : -1);
			} else if (act.kind == AT_LEFT) { // ("left Imperial Sword": into the left hand)
				const int it = items_find_name(act.arg);
				int lk = 0, rk = 0;
				const int ok = it >= 0 ? equip_left_toggle(it) : 0;
				game_log("autotest: left '%s' (have %d) -> %d: left %d right %d", act.arg, it >= 0 ? items_have(it) : 0, ok,
				         equip_left_weapon(&lk), equip_weapon(&rk));
			} else if (act.kind == AT_GIVE) { // ("give Golden Claw": one of it; "give Gold 500": that many)
				char what[64];
				int count = 1;
				snprintf(what, sizeof(what), "%s", act.arg);
				char *sp = strrchr(what, ' ');
				if (sp && atoi(sp + 1) > 0)
					count = atoi(sp + 1), *sp = 0;
				const int it = items_find_name(what);
				game_log("autotest: give '%s' x%d is item %d", what, count, it);
				if (it >= 0)
					items_add(it, count);
			} else if (act.kind == AT_SPELL) { // ("spell Conjure Familiar": learned, into the right hand, readied; "spell both ...": both)
				const int both = !strncmp(act.arg, "both ", 5);
				if (both)
					memmove(act.arg, act.arg + 5, strlen(act.arg + 5) + 1);
				magic_learn("", act.arg);
				int found = -1;
				for (int sp = 0; sp < magic_count(); sp++)
					if (!strcmp(magic_name(sp), act.arg))
						found = sp;
				game_log("autotest: spell '%s' is %d of %d", act.arg, found, magic_count());
				if (found >= 0)
					magic_set_hand(HAND_RIGHT, found), magic_set_readied(1), vitals.magicka = vitals.max_magicka = 600.0f;
				if (found >= 0 && both)
					magic_set_hand(HAND_LEFT, found);
			} else if (act.kind == AT_ENTER) { // (inside, a step in from its first door)
				const int in = interior_find(act.arg);
				const float zero[3] = {0.0f, 0.0f, 0.0f};
				if (in >= 0 && interior_go(in, zero, 0.0f)) {
					float dp[3];
					for (int d = 0; doors_position(d, dp); d++) // (its locked doors, as the game finds them)
						if (locks_find(in, dp) >= 0)
							game_log("autotest: door %d at %.1f %.1f %.1f locked (%s)", d, dp[0], dp[1], dp[2], locks_label(locks_find(in, dp), "Door"));
					float doors[8][3];
					const char *labels[8];
					const int nd = interior_doors(doors, labels, 8);
					if (nd > 0)
						player.x = doors[0][0], player.feet = doors[0][1] + 0.3f, player.z = doors[0][2];
					player.vy = 0.0f, airborne = 0;
					fade = 1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, travel_landing = 2, airborne = 0;
				}
			} else if (act.kind == AT_KIT) {
				game_log("autotest: kit %d things", items_give_crafting_kit());
			} else if (act.kind == AT_STATION) {
				menu_open_crafting((int)act.x);
			} else if (act.kind == AT_SAY || act.kind == AT_BARTER) {
				for (int i = 0; i < actors_count(); i++)
					if (!strcmp(actors_id(i), act.arg)) {
						if (act.kind == AT_SAY) {
							const char *opts[8];
							const int n = quests_topics(i, opts, 8);
							game_log("autotest: %s has %d topics%s%s", act.arg, n, n ? ": " : "", n ? opts[0] : "");
							quests_say(i, (int)act.x);
						} else {
							const int v = items_vendor(act.arg);
							game_log("autotest: %s vendor %d", act.arg, v);
							if (v >= 0)
								menu_open_barter(v, talk_name(i));
						}
					}
			} else if (act.kind == AT_SHOT) {
				snprintf(shot_name, sizeof(shot_name), "%s", act.arg);
			} else if (act.kind == AT_CITY || act.kind == AT_GOTO || act.kind == AT_NEAR) {
				float tx = act.x, tz = act.z;
				int target = -1; // (Tamriel; a city: -2 - its number)
				if (act.kind == AT_CITY)
					for (int w = 1; w < worlds_count(); w++)
						if (!strcmp(worlds_prefix_of(w), act.arg))
							target = -2 - (w - 1);
				float face = yaw, near_y = -1e9f;
				if (act.kind == AT_NEAR) { // (beside them, facing them; "near balgruufthegreater:4:2" - 4 m off, from the 2nd of 8 ways round)
					float at[3], off = 1.4f;
					int from = 0;
					char who[48];
					snprintf(who, sizeof(who), "%s", act.arg);
					char *colon = strchr(who, ':');
					if (colon)
						*colon = 0, sscanf(colon + 1, "%f:%d", &off, &from);
					int found_creature = 0; // ("near arvel_the_swift": a creature by its name, underscores for spaces)
					{
						char title[48];
						snprintf(title, sizeof(title), "%s", who);
						for (char *u = title; *u; u++)
							if (*u == '_')
								*u = ' ';
						found_creature = creatures_find_titled(title, at);
					}
					for (int i = 0; i < actors_count() + found_creature; i++)
						if ((i == actors_count() || (!strcmp(actors_id(i), who) && actors_shown(i, at)))) {
							tx = at[0] + 1.6f, tz = at[2], face = -90.0f, near_y = at[1];
							for (int k = from; k < from + 8; k++) { // (on their floor beside them, not in a wall: the navmesh's)
								const float ang = k * 0.785f, px = at[0] + off * cosf(ang), pz = at[2] + off * sinf(ang);
								NavRef nr;
								float h;
								if (navmesh_locate_level(px, at[1] + 0.5f, pz, off > 2.0f ? 2.5f : 1.5f, &nr, &h)) {
									tx = px, tz = pz, near_y = h;
									face = atan2f(at[0] - px, -(at[2] - pz)) * 180.0f / (float)M_PI;
									break;
								}
							}
							break;
						}
					target = interior_current() >= 0 ? interior_current() : -2 - (worlds_current() - 1);
					if (worlds_current() == 0 && interior_current() < 0)
						target = -1;
				}
				if (act.kind != AT_NEAR || target != interior_current()) {
					const float at[3] = {tx, 0.0f, tz};
					interior_go(target, at, face);
				}
				player.x = tx, player.z = tz, player.vy = 0.0f, yaw = face, pitch = 0.0f;
				for (int pass = 0; pass < 2; pass++) {
					terrain_update(player.x, player.z);
					objects_update(player.x, player.z);
					navmesh_update(player.x, player.z);
					stream_flush();
				}
				float ground;
				if (terrain_height_at(player.x, player.z, &ground)) { // (onto what's there: a city's paving, a floor)
					const float top[3] = {player.x, ground + 60.0f, player.z}, low[3] = {player.x, ground - 5.0f, player.z};
					const float t = collision_ray_ground(top, low);
					player.feet = (t < 1.0f ? top[1] + (low[1] - top[1]) * t : ground) + 0.3f;
				}
				if (near_y > -1e8f && interior_current() >= 0) // (inside: on their floor - a keep's levels)
					player.feet = near_y + 0.3f;
				airborne = 0;
				if (act.kind != AT_NEAR) // (as fast travel: the loading screen till it's settled)
					fade = 1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, travel_landing = 2, airborne = 0;
			}
		}
		perf_lap("quests_dialogue");
		ui_fade(fmaxf(fade, opening.fade));
		ui_opening_card(stage == STAGE_PLAY && intro_active() ? opening.card : 0, opening.title);
		if (!paused && stage == STAGE_PLAY) { // (the quests: where you are, a dragon's soul taken)
			const float feet_q[3] = {player.x, player.feet, player.z};
			quests_update(frame_s, feet_q, interior_current(), souls_this_frame);
			static float rumble_t;
			{ // (who you are, for the guards' greetings: INFO 000AC12A)
				const Race *pr = races_get(races_player());
				creatures_set_player_identity(actors_player_female(), pr ? pr->eid : "NordRace");
			}
			if (quests_take_rumble()) // ("Dovahkiin!": the ground shakes with it, as long as it rolls on)
				rumble_t = 5.0f;
			if (rumble_t > 0.0f)
				rumble_t -= frame_s, shout_shake = fmaxf(shout_shake, 0.35f + 0.5f * fminf(1.0f, rumble_t / 3.0f));
			// A brawl (Uthgerd's): her fists, yours; a weapon or spell in hand and it's off
			int wk;
			const int armed = (!brawl_spar() && magic_readied() && equip_weapon(&wk) >= 0) || cast_this_frame; // (a weapon drawn, a spell cast; a spar: a spell)
			cast_this_frame = 0;
			const int brawled = brawl_update(frame_s, feet_q, &vitals.health, vitals.max_health, armed);
			if (brawled != BRAWL_ON && brawled != BRAWL_NONE)
				quests_brawl_over(brawled);
		}
		souls_this_frame = 0;
		governor_live = stage == STAGE_PLAY && !transition && !paused;
		perf_set_live(governor_live); // (a frame over 150 ms in play: logged with where it went)
		{ // Talking: what was chosen (a quest's topic, Barter), the menu shut
			int choice = menu_take_dialogue();
			// A conversation's answers (Skyrim's, as it has them - dtree.c): the menu's yours to pick from
			// once they've said their piece; what you pick, they answer
			static int dialogue_tree = -1; // (the menu open is the answers: this many, then "Goodbye.")
			if (dialogue_tree >= 0 && choice != -1) {
				dtree_choose(choice >= 0 && choice < dialogue_tree ? choice : -1);
				dialogue_tree = -1, choice = -1;
			}
			if (dialogue_tree < 0 && !menu_is_open() && dtree_busy() && dialogue_with < 0) {
				const char *answers[13];
				int n = dtree_choices(answers, 11);
				if (n > 0) {
					dialogue_tree = n;
					answers[n++] = T_DIAL_000D8BE8_FULL0;
					menu_open_dialogue(talk_name(dtree_actor()), answers, n);
				}
			}
			if (quests_choice(choice)) // (a quest's own question: the Embassy's Solar guard)
				choice = -1;
			const int arrest_hold = crime_arrest_hold();
			int go_door = -1;
			if (services_choice(choice, &go_door)) { // (training, the gate guard: ours)
				if (go_door >= 0)
					lock_go_door = go_door;
				choice = -1;
			}
			int jail_go = 0, days = 0, place = -99;
			float at[3], face = 0.0f;
			if (choice != -1 && crime_arresting() && dialogue_with < 0 &&
			    crime_arrest_choice(choice) == ARREST_JAIL) { // Jail: into its cell (or, its jail not here, the time served at once)
				if (crime_jail_enter(arrest_hold, &place, at, &face))
					jail_go = 1;
				else
					days = crime_jail(arrest_hold, &place, at, &face), jail_go = days > 0;
			} else if (jail_serve) { // (slept in the cell: the time served, then out at its door)
				jail_serve = 0;
				days = crime_jail(crime_jailed(), &place, at, &face), jail_go = days > 0;
			} else if (crime_jailed() >= 0 && interior_current() != crime_jail_place()) {
				crime_escaped(); // (out of the jail some other way)
			}
			if (jail_go && place > -99) {
				if (days > 0)
					lighting_advance_hours(days * 24.0f);
				face = face * 180.0f / (float)M_PI;
				interior_go(place, at, face);
				player.x = at[0], player.z = at[2], player.feet = at[1] + 0.3f, player.vy = 0.0f;
				yaw = face, pitch = 0.0f;
				projectiles_clear();
				for (int pass = 0; pass < 2; pass++) {
					terrain_update(player.x, player.z);
					objects_update(player.x, player.z);
					navmesh_update(player.x, player.z);
					stream_flush();
				}
				float ground; // (Skyrim's marker under the hill's skin here: onto the ground)
				if (place < 0 && terrain_height_at(player.x, player.z, &ground) && ground > player.feet)
					player.feet = ground + 0.3f;
				game_log("crime: %s at %.1f %.1f %.1f (place %d)", days ? "out of jail" : "in the cell", player.x, player.feet,
				         player.z, place);
				fade = 1.0f, transition = 3, settle_s = 0.0f, settle_good = 0, airborne = 0, travel_landing = 2;
				char text[96];
				if (days > 0) {
					vitals.health = vitals.max_health, vitals.magicka = vitals.max_magicka, vitals.stamina = vitals.max_stamina;
					snprintf(text, sizeof(text), "You served %d day%s in jail. Your skills have suffered.", days, days > 1 ? "s" : "");
				} else {
					snprintf(text, sizeof(text), "In jail: sleep to serve your time, or find a way out");
				}
				ui_notice(text, 5.0f);
			} else if (choice >= 0 && dialogue_with >= 0 && choice < dialogue_end) {
				if (choice < dialogue_topics)
					quests_say(dialogue_with, choice);
				else if (choice == dialogue_topics && dialogue_vendor >= 0)
					menu_open_barter(dialogue_vendor, talk_name(dialogue_with));
				else if (choice >= dialogue_ride)
					riding_say(dialogue_with, choice - dialogue_ride);
				else if (choice >= dialogue_service)
					services_say(dialogue_with, choice - dialogue_service);
				else if (choice >= dialogue_follow) {
					int trade = -1;
					followers_say(dialogue_with, choice - dialogue_follow, &trade);
					if (trade >= 0)
						menu_open_container(trade, talk_name(dialogue_with));
				}
			}
			if (choice != -1)
				dialogue_with = -1;
		}
		perf_lap("compass");
		{ // The compass: the places within Skyrim's distances (iMapMarkerVisibleDistance: those not
		  // found yet, fHUDCompassLocationMaxDist: the rest; 70 units a metre)
			static UiCompassMark marks[UI_COMPASS_MARKS];
			int n = 0;
			for (int i = 0; i < worldmap_count() && n < UI_COMPASS_MARKS && interior_current() < 0; i++) {
				int kind, disc, in_region;
				const char *name;
				float mx, mz;
				if (!worldmap_marker(i, &kind, &name, &mx, &mz, &disc, &in_region) || !in_region)
					continue;
				const float dx = mx - player.x, dz = mz - player.z, d = hypotf(dx, dz);
				if (d > (disc ? COMPASS_FOUND_M : COMPASS_UNFOUND_M) || d < 1.0f)
					continue;
				marks[n].bearing = atan2f(dx, -dz) * 180.0f / (float)M_PI; // (north: -z)
				marks[n].kind = kind, marks[n].discovered = disc;
				n++;
			}
			float qm[UI_MAP_QUESTS][2]; // (the active quests' objectives: gold markers - Skyrim's journal, each quest set active
			// its own; the opening's: whom to follow, both at the keep)
			const int nq = intro_active() && intro_stage() == 0 ? intro_markers(qm, 2) : quests_markers(qm, UI_MAP_QUESTS);
			menu_set_quest_markers(nq, (const float (*)[2])qm); // (and on the world map)
			for (int q = 0; q < nq && n < UI_COMPASS_MARKS && opts->hud_markers; q++) // (the options' may hide them: the map keeps them)
				if (interior_current() < 0 && hypotf(qm[q][0] - player.x, qm[q][1] - player.z) > 2.0f) {
					marks[n].bearing = atan2f(qm[q][0] - player.x, -(qm[q][1] - player.z)) * 180.0f / (float)M_PI;
					marks[n].kind = -1, marks[n].discovered = 1;
					n++;
				}
			{ // Clairvoyance held: Skyrim's guide - a wisp every moment along the way to what you're
			  // after (by the navmesh about you, toward the crossing whose way on - pack/navmesh/ways.txt -
			  // is shortest; indoors, to the nearest way out)
				static float guide_t;
				if (!magic_guiding()) {
					guide_t = 0.0f;
				} else if ((guide_t -= frame_s) <= 0.0f) {
					guide_t = 2.0f;
					const float feet[3] = {player.x, player.feet, player.z};
					float goal[3], pts[64 * 3], qg[2];
					int have = 0, n = 0;
					if (interior_current() >= 0) {
						float doors[16][3];
						const char *labels[16];
						const int nd = interior_doors(doors, labels, 16);
						float best = 1e9f;
						for (int k = 0; k < nd; k++) {
							const float d = hypotf(doors[k][0] - feet[0], doors[k][2] - feet[2]);
							if (d > 3.0f && d < best)
								best = d, memcpy(goal, doors[k], sizeof(goal)), have = 1;
						}
					} else if (worlds_current() > 0 && quests_marker(qg) > 0 && !city_holds(qg, feet)) {
						// (in a walled city, bound for somewhere outside it: to its gate whose way on is
						// shortest - its own navmesh, not Tamriel's ways: it had pointed straight at the
						// objective, into the walls, Solitude's especially)
						float gates[8][3], best = 1e30f;
						const int ng = interior_city_gates(gates, 8);
						game_log("magic: in a walled city, %d gates out", ng);
						for (int k = 0; k < ng; k++) {
							const float d = hypotf(gates[k][0] - feet[0], gates[k][2] - feet[2]) +
							                hypotf(qg[0] - gates[k][0], qg[1] - gates[k][2]);
							if (d < best)
								best = d, memcpy(goal, gates[k], sizeof(goal)), have = 1;
						}
					} else if (quests_marker(qg) > 0) {
						// (by the ways - converter/waygraph.py - the crossing near you whose way on is
						// shortest, the navmesh's path there counted: up the 7000 Steps, round a
						// cliff, over the bridge; or the marker itself, when near and walkable)
						float best = 1e30f, path[64 * 3], len;
						int cand[5];
						const int nc = ways_toward(feet, qg, GUIDE_REACH_M, cand, 5);
						for (int k = -1; k < nc; k++) {
							float to[3] = {qg[0], feet[1], qg[1]}, on = 0.0f;
							if (k < 0) {
								if (hypotf(qg[0] - feet[0], qg[1] - feet[2]) > GUIDE_REACH_M)
									continue;
								terrain_height_at(to[0], to[2], &to[1]);
							} else {
								ways_node(cand[k], to), on = ways_left(cand[k]);
							}
							const int pn = guide_path(feet, to, path, &len);
							if (pn >= 2 && len + on < best) {
								best = len + on, n = pn;
								memcpy(pts, path, pn * 3 * sizeof(float));
							}
						}
						const float dx = qg[0] - feet[0], dz = qg[1] - feet[2], d = hypotf(dx, dz);
						const float reach = fminf(d, 60.0f);
						goal[0] = feet[0] + dx / fmaxf(d, 0.01f) * reach, goal[2] = feet[2] + dz / fmaxf(d, 0.01f) * reach;
						goal[1] = feet[1];
						terrain_height_at(goal[0], goal[2], &goal[1]);
						have = d > 2.0f;
					}
					if (!have) {
						ui_notice("There is nowhere you are bound for", 2.0f);
						guide_t = 4.0f;
					} else if (n < 2) {
						NavRef from, to;
						float h, at[3];
						memcpy(pts, feet, sizeof(feet));
						n = 1;
						if (navmesh_locate(feet[0], feet[1], feet[2], &from, &h) &&
						    navmesh_nearest(goal[0], goal[1], goal[2], 25.0f, &to, at))
							n += navmesh_path(from, feet, to, at, pts + 3, 63);
						if (n < 2) { // (no navmesh between: over the ground, straight)
							const float len = hypotf(goal[0] - feet[0], goal[2] - feet[2]);
							const int steps = (int)fminf(30.0f, fmaxf(1.0f, len / 3.0f));
							for (int k = 1; k <= steps && n < 64; k++, n++) {
								float *p = &pts[n * 3];
								p[0] = feet[0] + (goal[0] - feet[0]) * k / steps, p[2] = feet[2] + (goal[2] - feet[2]) * k / steps;
								p[1] = feet[1];
								terrain_height_at(p[0], p[2], &p[1]);
							}
						}
					}
					if (have && n >= 2) {
						for (int k = 0; k < n; k++)
							pts[k * 3 + 1] += k ? 0.7f : 1.1f; // (low, at the knees to the waist: from the hands)
						spellfx_guide_launch(pts, n);
						game_log("magic: the way shown, %d points, to %.0f %.0f", n, pts[(n - 1) * 3], pts[(n - 1) * 3 + 2]);
					}
				}
			}
			if (aura_left > 0.0f) { // Aura Whisper: the living and the dead about you, on the compass
				float found[12][2];
				const float from[3] = {player.x, player.feet, player.z};
				for (int u = 0; u < 2; u++) {
					const int nd = creatures_positions(from, 80.0f, u, found, 12);
					for (int d = 0; d < nd && n < UI_COMPASS_MARKS; d++) {
						marks[n].bearing = atan2f(found[d][0] - player.x, -(found[d][1] - player.z)) * 180.0f / (float)M_PI;
						marks[n].kind = -1, marks[n].discovered = 1;
						n++;
					}
				}
			}
			{ // Detect Life / Dead held: those within its range, on the compass
				int undead = 0;
				const float range = magic_detect(&undead);
				if (range > 0.0f) {
					float found[8][2];
					const float from[3] = {player.x, player.feet, player.z};
					const int nd = creatures_positions(from, range, undead, found, 8);
					for (int d = 0; d < nd && n < UI_COMPASS_MARKS; d++) {
						marks[n].bearing = atan2f(found[d][0] - player.x, -(found[d][1] - player.z)) * 180.0f / (float)M_PI;
						marks[n].kind = -1, marks[n].discovered = 1;
						n++;
					}
				}
			}
			ui_compass(!paused && !transition && dead_t < 0.0f && opts->compass, yaw, marks, n);
		}
		perf_lap("cleared");
		{ // Skyrim's "<place> Cleared": the last of its hostiles killed while you're there
			static int place_was = -2, was_cleared;
			const int here = interior_current();
			if (here != place_was) {
				place_was = here, was_cleared = here >= 0 && interior_location_cleared(interior_title(here));
			} else if (here >= 0 && !was_cleared && frame % 30 == 0 && interior_location_cleared(interior_title(here))) {
				was_cleared = 1;
				char text[96];
				snprintf(text, sizeof(text), "%s Cleared", interior_title(here));
				ui_notice(text, 4.0f);
				game_log("cleared: %s", interior_title(here));
			}
		}
		// Indoors, falling out of the world puts you back at the door you came in by
		if (indoors && player.feet < interior_floor() - 15.0f) {
			float pos[3];
			interior_entry(pos, &yaw);
			player.x = pos[0], player.feet = pos[1], player.z = pos[2], player.vy = 0.0f;
			airborne = 0;
		}
		perf_lap("upload_steps");
		unsigned long long t1 = stream_now_us();
		// Finish streamed loads: GPU uploads, capped so a frame never stalls on them
		perf_end(PERF_GAME);
		perf_begin(PERF_UPLOAD);
		stream_pump(STREAM_BUDGET_US);
		perf_end(PERF_UPLOAD);
		perf_begin(PERF_GAME);
		unsigned long long t2 = stream_now_us();
		// Diagnostics: log hitches (frames far over the 60 Hz budget)
		if (t0 - last_frame_us > 40000 && frame > 2)
			sceClibPrintf("HITCH frame %d: %u ms since the last frame (prev update %u us, stream %u us, %d jobs queued)\n",
			              frame, (unsigned)((t0 - last_frame_us) / 1000), (unsigned)hitch_update_us,
			              (unsigned)hitch_stream_us, stream_pending());
		last_frame_us = t0;
		hitch_update_us = t1 - t0, hitch_stream_us = t2 - t1;
		// A slope raises the feet steadily every frame, stairs in spikes: only the part of a
		// frame's change above the recent trend counts as a step.
		static float step_offset, last_feet, climb_rate;
		static int was_grounded;
		float rise = player.feet - last_feet;
		if (!noclip && player.grounded && was_grounded && fabsf(rise) < STEP_MAX_M) {
			step_offset -= (rise - climb_rate) * (1.0f - menu_step_keep()); // the camera lags behind the snap...
			climb_rate += (rise - climb_rate) * 0.2f;
		} else {
			step_offset = climb_rate = 0.0f;
		}
		step_offset *= expf(-(frame_s) / STEP_EASE_S); // ...and catches up
		last_feet = player.feet, was_grounded = player.grounded;
		// (The body is drawn with the same offset: in third person it glides up and down
		// stairs as the camera does, by the stair bounce setting, instead of jumping a
		// whole step at a time)
		static float crouch;
		crouch += ((sneaking && !player.swimming ? 1.0f : 0.0f) - crouch) * fminf(1.0f, dt * 8.0f);
		float cam_y = player.feet + EYE_HEIGHT - SNEAK_EYE_DROP_M * crouch + step_offset - (seated ? SEATED_EYE_DROP_M : 0.0f) +
		              (riding_mounted() ? RIDING_EYE_LIFT_M : 0.0f) + // (in the saddle: higher)
		              (beast_active() ? BEAST_EYE_LIFT_M : 0.0f);     // (the werewolf: taller)
		// The player's body: its velocity picks and paces the clip
		static float last_px, last_pz;
		float pvx = dt > 0.0f ? (player.x - last_px) / dt : 0.0f, pvz = dt > 0.0f ? (player.z - last_pz) / dt : 0.0f;
		if (hypotf(pvx, pvz) > 20.0f) // a door or a respawn, not a step
			pvx = pvz = 0.0f;
		last_px = player.x, last_pz = player.z;
		perf_lap("pose");
		perf_begin(PERF_POSE);
		// (the race menu: the body turned to the camera, whatever's in its hands - a spell ready turned its back)
		actors_player_face_camera(stage == STAGE_RACE, race_turn);
		riding_update(dt, (const float[3]){player.x, player.feet, player.z}, actors_player_heading_deg(), hypotf(pvx, pvz),
		              interior_current() >= 0 ? interior_current() : worlds_current() > 0 ? -2 : -1);
		if (killmove_left > 0.0f) { // (a killmove: its clip through, then yours again)
			killmove_t += dt;
			if ((killmove_left -= dt) <= 0.0f || (killmove_victim >= 0 && creatures_killmove_done(killmove_victim))) {
				killmove_left = 0.0f, killmove_victim = -1;
				actors_player_scene(NULL, 0.0f);
			} else {
				actors_player_scene(killmove_clip, killmove_t);
			}
		}
		actors_player_update(player.x, player.feet + step_offset, player.z, yaw, pvx, pvz, player.vy, player.grounded || noclip, player.swimming, sneaking,
		                     stage == STAGE_RACE ? frame_s : dt, // (the race menu: the character breathes)
		                     third_person && !player_hidden && !beast_active()); // (the beast's body drawn for yours)
		beast_update(dt, (const float[3]){player.x, player.feet + step_offset, player.z}, actors_player_heading_deg(), hypotf(pvx, pvz),
		             interior_current(), &vitals); // (its time up: you again)
		perf_end(PERF_POSE);
		perf_lap("camera");
		perf_begin(PERF_CAMERA);
		// The view: your eyes, or over your shoulder behind you (sets player_hidden for the
		// next frame's body)
		if (opening.eye_set) // (riding the cart, kneeling at the block: where the head is)
			cam_x = opening.eye[0], cam_y = opening.eye[1], cam_z = opening.eye[2];
		float view_x = cam_x, view_y = cam_y, view_z = cam_z;
		if (third_person) {
			const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
			const float fx = sinf(yr) * cosf(pr), fy = sinf(pr), fz = -cosf(yr) * cosf(pr);
			float dist = indoors ? THIRD_PERSON_DIST_INSIDE_M : THIRD_PERSON_DIST_M, drop = THIRD_PERSON_DROP_M;
			view_x = cam_x - fx * dist + cosf(yr) * THIRD_PERSON_SHOULDER_M;
			view_y = cam_y - drop - fy * dist;
			view_z = cam_z - fz * dist + sinf(yr) * THIRD_PERSON_SHOULDER_M;
			// Skyrim's race menu, framed as the user's reference video has it: the head 60% across the
			// screen, close - head and shoulders - on the Race tab and the face's tabs, from the head to the
			// hips on the Body tab; eased from one to the next
			static float frame_d = -1.0f, frame_y = RACE_FRAME_CLOSE_Y;
			float head_bone[3];
			if (stage == STAGE_RACE && actors_player_eye(head_bone)) {
				const int close = racemenu_close_on_face() || test_face_zoom, body = !close && racemenu_portrait();
				const float want_d = close ? RACE_FRAME_CLOSE_M : body ? RACE_FRAME_BODY_M : RACE_FRAME_RACE_M;
				const float want_y = close ? RACE_FRAME_CLOSE_Y : body ? RACE_FRAME_BODY_Y : RACE_FRAME_RACE_Y;
				const float ease = frame_d < 0.0f ? 1.0f : fminf(1.0f, frame_s * 5.0f);
				frame_d = frame_d < 0.0f ? want_d : frame_d + (want_d - frame_d) * ease;
				frame_y += (want_y - frame_y) * ease;
				const float tv = tanf(view_fov * 0.5f * (float)M_PI / 180.0f), th = tv * (float)SCREEN_W / (float)SCREEN_H;
				const float side = (RACE_FRAME_X - 0.5f) * 2.0f * th * frame_d, up = (0.5f - frame_y) * 2.0f * tv * frame_d;
				const float ux = -sinf(yr) * sinf(pr), uy = cosf(pr), uz = cosf(yr) * sinf(pr); // (the view's up)
				cam_x = head_bone[0], cam_y = head_bone[1], cam_z = head_bone[2];
				dist = frame_d, drop = 0.0f;
				view_x = cam_x - fx * dist - cosf(yr) * side - ux * up;
				view_y = cam_y - fy * dist - uy * up;
				view_z = cam_z - fz * dist - sinf(yr) * side - uz * up;
			} else {
				frame_d = -1.0f;
			}
			// Not through walls: stop just in front of whatever is between the head and the camera
			const float head[3] = {cam_x, cam_y - drop, cam_z}, want[3] = {view_x, view_y, view_z};
			// The view has a width: besides the centre line, lines to points beside and
			// above/below the camera, so a wall just off to one side can't end up in front
			// of the near clip plane (which showed the far side of the wall)
			const float rx = cosf(yr) * THIRD_PERSON_BODY_M, rz = sinf(yr) * THIRD_PERSON_BODY_M;
			const float offsets[4][3] = {{rx, 0, rz}, {-rx, 0, -rz}, {0, THIRD_PERSON_BODY_M, 0},
			                             {0, -THIRD_PERSON_BODY_M, 0}};
			float centre = collision_ray(head, want), side = 1.0f;
			for (int k = 0; k < 4; k++) {
				const float to[3] = {want[0] + offsets[k][0], want[1] + offsets[k][1], want[2] + offsets[k][2]};
				side = fminf(side, collision_ray(head, to));
			}
			const float gap = THIRD_PERSON_WALL_GAP_M / dist;
			if (centre < 1.0f)
				centre = fmaxf(0.0f, centre - gap);
			if (side < 1.0f)
				side = fmaxf(0.0f, side - gap);
			// Something straight between the head and the camera: in at once (never through
			// the wall). Something only beside the view (a pillar going past, a rock's edge):
			// glide in. Back out gently. (Snapping every way made the view jump about, and
			// flash, walking past buildings and walls.)
			static float cam_reach = 1.0f;
			float target = fminf(centre, side);
			if (centre < cam_reach)
				cam_reach = centre;
			else if (target < cam_reach)
				cam_reach = fmaxf(target, cam_reach - dt * THIRD_PERSON_EASE_IN);
			else
				cam_reach = fminf(target, cam_reach + dt * THIRD_PERSON_EASE_OUT);
			float t = cam_reach;
			player_hidden = t * dist < THIRD_PERSON_HIDE_M; // (the camera would be inside the head)
			if (t < 1.0f) {
				view_x = head[0] + (want[0] - head[0]) * t;
				view_y = head[1] + (want[1] - head[1]) * t;
				view_z = head[2] + (want[2] - head[2]) * t;
			}
			float ground;
			if (!indoors && terrain_height_at(view_x, view_z, &ground) && view_y < ground + 0.4f)
				view_y = ground + 0.4f; // (not under the hill behind you)
			if (player.swimming && player.feet >= player.water - SWIM_DEPTH - 0.05f && view_y < player.water + 0.3f)
				view_y = player.water + 0.3f; // (nor under the water you're swimming at the top of)
		}
		last_view[0] = view_x, last_view[1] = view_y, last_view[2] = view_z; // (aiming arrows)

		perf_lap("end");
		perf_end(PERF_GAME);
		perf_begin(PERF_WORLD);
		perf_lap("w_post");
		// The world renders at low resolution into the post-processing target
		post_begin();
		perf_lap("w_spells");
		objects_set_render_size(post_render_w(), post_render_h());
		perf_end(PERF_CAMERA);
		atmosphere_set_camera(view_x, view_y, view_z);
		// First person: the arms in front of you (while holding the torch)
		{
			perf_begin(PERF_POSE);
			const float eye[3] = {view_x, view_y, view_z};
			actors_first_update(eye, yaw, pitch, hypotf(pvx, pvz), dt, !third_person && !player.swimming);
			perf_end(PERF_POSE);
		}
		// The held torch: its flame (the hand's, a frame old; first person: low on the left
		// of the view) lights everything around it, flickering
		float torch[3];
		int held = actors_player_torch(torch);
		if (held == 1) {
			const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
			const float fwd[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)}, rgt[3] = {cosf(yr), 0.0f, sinf(yr)};
			const float eye[3] = {view_x, view_y, view_z};
			for (int k = 0; k < 3; k++)
				torch[k] = eye[k] + fwd[k] * 0.55f - rgt[k] * 0.32f;
			torch[1] -= 0.2f;
		}
		if (held && !(player.swimming && player.feet < player.water - SWIM_DEPTH - 0.3f)) {
			const float t = clock_s;
			const float flick = 0.88f + 0.12f * sinf(t * 11.0f) * sinf(t * 6.7f + 0.8f);
			const float rgb[3] = {TORCH_R * flick, TORCH_G * flick, TORCH_B * flick};
			atmosphere_set_torch(torch, TORCH_REACH_M * (0.95f + 0.05f * flick), rgb);
			objects_fx_torch(torch);
		} else {
			atmosphere_set_torch(NULL, 0.0f, NULL);
			objects_fx_torch(NULL);
		}
		// Spells: from the palms (Skyrim's magic nodes, as the hands were last drawn), aimed
		// where you look
		{
			const float yr = yaw * (float)M_PI / 180.0f, pr = pitch * (float)M_PI / 180.0f;
			const float fwd[3] = {sinf(yr) * cosf(pr), sinf(pr), -cosf(yr) * cosf(pr)}, rgt[3] = {cosf(yr), 0.0f, sinf(yr)};
			const float eye[3] = {cam_x, cam_y, cam_z};
			float hand[3];
			if (third_person) {
				hand[0] = player.x + sinf(yr) * 0.45f + rgt[0] * 0.25f;
				hand[1] = player.feet + 1.25f;
				hand[2] = player.z - cosf(yr) * 0.45f + rgt[2] * 0.25f;
			} else {
				for (int k = 0; k < 3; k++)
					hand[k] = eye[k] + fwd[k] * 0.5f + rgt[k] * 0.3f;
				hand[1] -= 0.25f;
			}
			// (the palms as last drawn; before the hands have been drawn, the estimate)
			float palms[2][3];
			if (!actors_player_palms(palms))
				for (int h = 0; h < 2; h++)
					memcpy(palms[h], hand, sizeof(hand));
			MagicFx mfx;
			const float candle_before = magic_candle_left();
			magic_update(dt, cast_pressed, cast_held, &vitals, eye, palms, fwd, yaw, &mfx);
			{
				float flesh_left;
				const int flesh = magic_flesh(&flesh_left);
				actors_player_flesh(flesh, flesh_left);
			}
			{ // Shouts: SELECT held gathers the words; let go, shouted
				const Race *race = races_get(races_player());
				if (race)
					shouts_set_voice(race->eid, actors_player_female());
				const int shout_held = (pad.buttons & SCE_CTRL_SELECT) && dead_t < 0.0f && !player.swimming && !seated && !beast_active();
				const float feet[3] = {player.x, player.feet, player.z};
				ShoutFx sfx_out;
				shouts_update(dt, shout_held, eye, fwd, feet, &sfx_out);
				if (sfx_out.dash)
					dash_left = sfx_out.dash_s, dash_dir[0] = sfx_out.dash_dir[0], dash_dir[1] = sfx_out.dash_dir[1],
					dash_speed = sfx_out.dash_speed;
				if (sfx_out.ethereal_s > 0.0f)
					ethereal_left = sfx_out.ethereal_s, ui_notice(T_SHOU_00032920_FULL0, 2.0f);
				if (sfx_out.slow_s > 0.0f)
					slow_time_left = sfx_out.slow_s, slow_time_mult = sfx_out.slow_mult;
				if (sfx_out.fury_s > 0.0f)
					fury_left = sfx_out.fury_s, actors_player_attack_speed(sfx_out.fury_mult);
				if (sfx_out.aura_s > 0.0f)
					aura_left = sfx_out.aura_s;
				if (sfx_out.clear_s > 0.0f)
					weather_clear_for(sfx_out.clear_s);
				if (sfx_out.storm_s > 0.0f) {
					if (interior_current() >= 0)
						ui_notice("Storm Call can only be used outdoors", 3.0f);
					else
						storm_left = sfx_out.storm_s, storm_harm = sfx_out.storm_harm, storm_tick = 1.0f;
				}
				if (storm_left > 0.0f) { // (a bolt every few seconds on one of your foes about you)
					storm_left -= dt;
					if (interior_current() >= 0)
						storm_left = 0.0f;
					else if ((storm_tick -= dt) <= 0.0f) {
						storm_tick = STORM_CALL_EVERY_S;
						float hit[3];
						if (creatures_storm_strike(feet, STORM_CALL_M, storm_harm, hit)) {
							spellfx_burst(hit, ELEMENT_SHOCK, 1.5f);
							sfx_play_at("mag_shock_hit", hit, 120.0f);
						}
					}
				}
				// (their time running out)
				if (ethereal_left > 0.0f && (ethereal_left -= dt) <= 0.0f)
					ethereal_left = 0.0f;
				actors_player_ghost(ethereal_left > 0.0f);
				if (slow_time_left > 0.0f && (slow_time_left -= dt) <= 0.0f)
					slow_time_mult = 1.0f;
				if (fury_left > 0.0f && (fury_left -= dt) <= 0.0f)
					actors_player_attack_speed(1.0f);
				if (fury_left <= 0.0f) { // (Dual Flurry: two weapons swing faster - Skyrim's 20%, 35%)
					int wk;
					const int dual = equip_weapon(&wk) >= 0 && wk != 7 && wk != 9 && lweapon_now() >= 0;
					actors_player_attack_speed(dual ? perks_has_form(PERK_DUAL_FLURRY_2) ? 1.35f
					                                : perks_has_form(PERK_DUAL_FLURRY_1) ? 1.2f : 1.0f : 1.0f);
				}
				if (aura_left > 0.0f)
					aura_left -= dt;
				if (sfx_out.shouted)
					quests_shouted(sfx_out.kind, interior_current()); // (the Greybeards: INFO 0002F2C5)
				if (sfx_out.shouted) { // (the air shudders: a flash of its colour, the view shaken)
					static const int rgb[SHOUT_KINDS][3] = {{170, 190, 230}, {255, 120, 40}, {150, 200, 255}, {200, 210, 230},
					                                        {200, 220, 255}, {190, 190, 220}, {170, 220, 255}, {200, 80, 80},
					                                        {180, 180, 255}, {170, 230, 170}, {200, 180, 120}, {220, 230, 255},
					                                        {230, 200, 255}, {200, 200, 210}, {160, 60, 90}, {220, 235, 255}};
					memcpy(shout_flash_rgb, rgb[sfx_out.kind], sizeof(shout_flash_rgb));
					shout_flash = 0.12f + 0.08f * sfx_out.shouted, shout_shake = 0.25f * sfx_out.shouted;
				}
			}
			// Spell sounds: Candlelight's cast; Flames and Healing loop while they're held
			{
				spell_sounds_ran = 1;
				const int sp = mfx.hand[0].sparks || mfx.hand[1].sparks;
				if (sp && spark_voice < 0)
					spark_voice = sfx_loop("mag_shock_lp", 0.55f);
				if (!sp && spark_voice >= 0)
					sfx_stop(spark_voice), spark_voice = -1;
				if (mfx.rune_cast)
					sfx_play("mag_rune_cast", 0.8f, 0.0f);
				if (mfx.heal_burst) // (a golden wash over the view too)
					shout_flash = fmaxf(shout_flash, 0.35f), shout_flash_rgb[0] = 255, shout_flash_rgb[1] = 214,
					shout_flash_rgb[2] = 120;
				for (int b = 0; b < mfx.bursts; b++)
					sfx_play_at("mag_rune_impact", mfx.burst_pos[b], 40.0f);
				if (mfx.muffle_cast)
					sfx_play("mag_ready_alteration", 0.6f, 0.0f);
				// Bound Sword: a sword of light in the hand that cast it while it lasts (both hands: two)
				static int bound_item = -1;
				const int bound_hands = magic_take_bound_cast();
				if (bound_hands) {
					const char *bname = magic_bound_name(); // ("Bound Sword", "Bound Bow", "Bound Battleaxe", "Bound Dagger")
					const int it = items_find_name(bname);
					if (bound_item >= 0 && bound_item != it) { // (another bound weapon in hand: it goes)
						int lk0;
						if (equip_left_weapon(&lk0) == bound_item)
							equip_free_left_weapon();
						if (equip_worn(bound_item))
							equip_toggle(bound_item);
						items_add(bound_item, -items_have(bound_item));
						bound_item = -1;
					}
					if (it >= 0) {
						int lk, rk;
						const int have_left = equip_left_weapon(&lk) == it, have_right = equip_weapon(&rk) == it;
						const int want = ((bound_hands & 2) || have_right) + ((bound_hands & 1) || have_left);
						if (items_have(it) < want)
							items_add(it, want - items_have(it));
						if ((bound_hands & 2) && !have_right) {
							equip_toggle(it);
							magic_set_hand(HAND_RIGHT, -1);
						}
						if ((bound_hands & 1) && !have_left) {
							magic_set_hand(HAND_LEFT, -1);
							equip_left_toggle(it);
						}
						bound_item = it;
						if (strstr(bname, "Bow")) { // (its arrows with it, as Skyrim's: a hundred, of light)
							const int arrows = items_find_name(T_AMMO_0010B0A7_FULL0);
							if (arrows >= 0) {
								if (items_have(arrows) < 100)
									items_add(arrows, 100 - items_have(arrows));
								if (!equip_worn(arrows))
									equip_toggle(arrows);
							}
						}
						magic_set_readied(1);
						sfx_play("wpn_blade1hand_draw", 0.7f, 0.0f);
					} else {
						ui_notice("(that bound weapon isn't in the pack)", 2.0f);
					}
				}
				if (magic_take_bound_end() && bound_item >= 0) {
					int lk;
					if (equip_left_weapon(&lk) == bound_item)
						equip_free_left_weapon();
					if (equip_worn(bound_item))
						equip_toggle(bound_item);
					items_add(bound_item, -items_have(bound_item));
					const int arrows = items_find_name(T_AMMO_0010B0A7_FULL0);
					if (arrows >= 0 && items_have(arrows) > 0) {
						if (equip_worn(arrows))
							equip_toggle(arrows);
						items_add(arrows, -items_have(arrows));
					}
					ui_notice("Your bound weapon fades", 2.0f);
					bound_item = -1;
				}
				for (int tr = creatures_take_trapped(); tr > 0; tr--) { // Soul Trap: into the least empty gem you carry
					static const uint32_t GEMS[5][2] = {{0x2E4E2, 0x2E4E3}, {0x2E4E4, 0x2E4E5}, {0x2E4E6, 0x2E4F3},
					                                    {0x2E4F4, 0x2E4FB}, {0x2E4FC, 0x2E4FF}}; // (Skyrim's: empty, filled)
					int filled = 0;
					for (int g = 0; g < 5 && !filled; g++) {
						const int empty = items_by_form(GEMS[g][0]), full = items_by_form(GEMS[g][1]);
						if (empty >= 0 && full >= 0 && items_have(empty) > 0)
							items_add(empty, -1), items_add(full, 1), filled = 1;
					}
					ui_notice(filled ? "Soul captured" : "A soul escapes you: no empty soul gem", 3.0f);
				}
				if (magic_candle_left() > candle_before + 1.0f)
					sfx_play("mag_candlelight", 0.8f, 0.0f);
				const int fl = mfx.hand[0].flames || mfx.hand[1].flames, he = mfx.hand[0].healing || mfx.hand[1].healing;
				if (fl && flames_voice < 0)
					flames_voice = sfx_loop("mag_flames_lp", 0.6f);
				if (!fl && flames_voice >= 0)
					sfx_stop(flames_voice), flames_voice = -1;
				if (he && heal_voice < 0)
					heal_voice = sfx_loop("mag_heal_lp", 0.5f);
				if (!he && heal_voice >= 0)
					sfx_stop(heal_voice), heal_voice = -1;
			}
			actors_player_magic(mfx.hand[HAND_LEFT].spell >= 0 ? mfx.hand[HAND_LEFT].pose : -1,
			                    mfx.hand[HAND_RIGHT].spell >= 0 ? mfx.hand[HAND_RIGHT].pose : -1);
			const float feet[3] = {player.x, player.feet + step_offset, player.z};
			spellfx_set_view(yaw, pitch);
			perf_end(PERF_GAME);
			perf_begin(PERF_FX);
			spellfx_update(dt, clock_s, &mfx, palms, third_person, feet);
			perf_end(PERF_FX);
			perf_begin(PERF_GAME);
			// A spell's light outshines the torch: Flames from the stream's middle, Healing
			// from the hand(s), Candlelight; a flame readied in a hand glows a little
			const float t = clock_s, flick = 0.85f + 0.15f * sinf(t * 13.0f) * sinf(t * 7.9f + 0.4f);
			const MagicHand *mh = mfx.hand;
			const int flaming = mh[0].flames + mh[1].flames, healing = mh[0].healing + mh[1].healing;
			const int from = mh[HAND_RIGHT].flames || mh[HAND_RIGHT].healing ? HAND_RIGHT : HAND_LEFT;
			float torch_spot[3];
			if (flaming) {
				float mid[3];
				for (int c = 0; c < 3; c++)
					mid[c] = (palms[from][c] + mfx.flame_to[c]) * 0.5f;
				const float rgb[3] = {1.4f * flick, 0.75f * flick, 0.3f * flick};
				atmosphere_set_torch(mid, flaming == 2 ? 8.5f : 7.0f, rgb);
			} else if (healing) {
				float at[3];
				for (int c = 0; c < 3; c++)
					at[c] = healing == 2 ? (palms[0][c] + palms[1][c]) * 0.5f : palms[from][c];
				const float rgb[3] = {0.9f * flick, 0.8f * flick, 0.45f * flick};
				atmosphere_set_torch(at, 4.5f, rgb);
			} else if (mfx.candle) {
				const float rgb[3] = {0.85f, 0.9f, 1.05f};
				atmosphere_set_torch(mfx.candle_pos, CANDLE_REACH_M, rgb);
			} else if ((mh[0].spell == SPELL_FLAMES || mh[1].spell == SPELL_FLAMES) && !actors_player_torch(torch_spot)) {
				const float rgb[3] = {1.1f * flick, 0.6f * flick, 0.25f * flick};
				atmosphere_set_torch(palms[mh[1].spell == SPELL_FLAMES ? HAND_RIGHT : HAND_LEFT], 3.0f, rgb);
			}
		}
		perf_lap("w_sky");
		// Under water (the view below the surface where it is): murky fog, no sky
		float view_river, view_lake;
		int view_has_river = !indoors && objects_water_at_y(view_x, view_y, view_z, &view_river);
		int view_has_lake = !indoors && terrain_water_at(view_x, view_z, &view_lake);
		float surface = view_has_river && view_has_lake ? fmaxf(view_river, view_lake) : view_has_river ? view_river : view_lake;
		atmosphere_set_underwater((view_has_river || view_has_lake) && view_y < surface);
		float fog_rgb[3];
		if (atmosphere_underwater_fog(fog_rgb)) { // no sky: the fog colour behind everything
			glClearColor(fog_rgb[0], fog_rgb[1], fog_rgb[2], 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		} else if (interior_fog(fog_rgb)) {
			// Indoors: a dark shade of the fog behind everything. Vita3K leaves pinholes along
			// lines at power-of-two screen positions (not seams: they stay put on screen), and
			// the bright fog colour showed through them as dotted lines in dark dungeons
			const float k = INTERIOR_BACKDROP;
			glClearColor(fog_rgb[0] * k, fog_rgb[1] * k, fog_rgb[2] * k, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		} else {
			glClear(GL_DEPTH_BUFFER_BIT);
			atmosphere_set_view_roll(intro_active() ? opening.roll : 0.0f);
			if (!ms_skip(MS_NO_SKY))
				atmosphere_draw_sky(yaw, pitch, view_fov * zoom_fov, (float)SCREEN_W / SCREEN_H);
		}

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(view_fov * zoom_fov, (double)SCREEN_W / SCREEN_H, third_person ? THIRD_PERSON_NEAR_M : NEAR_CLIP_M, far_clip);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		{ // (a shout's shudder: the view shaken, dying away; its flash fading)
			const float t = clock_s, k = shout_shake * shout_shake;
			if (intro_active() && opening.roll != 0.0f) // (the opening's: your head on its side at the block)
				glRotatef(opening.roll, 0.0f, 0.0f, 1.0f);
			glRotatef(-pitch + k * 1.6f * sinf(t * 47.0f), 1.0f, 0.0f, 0.0f);
			glRotatef(yaw + k * 1.2f * sinf(t * 39.0f + 1.3f), 0.0f, 1.0f, 0.0f);
			shout_shake = fmaxf(0.0f, shout_shake - dt * 1.5f);
			if (shout_flash <= 0.0f && creatures_poisoned()) // (poisoned: a faint sickly pulse)
				ui_tint(60, 140, 30, 0.07f + 0.04f * sinf(t * 5.0f));
			else
				ui_tint(shout_flash_rgb[0], shout_flash_rgb[1], shout_flash_rgb[2], shout_flash);
			shout_flash = fmaxf(0.0f, shout_flash - dt * 0.6f);
		}
		glTranslatef(-view_x, -view_y, -view_z);

		// Horizontal half-FOV of the projection, plus a margin for pitch and overhangs
		float half_fov_x = atanf(tanf(view_fov * zoom_fov * 0.5f * (float)M_PI / 180.0f) * SCREEN_W / SCREEN_H) * 180.0f / (float)M_PI;
		terrain_set_view(view_x, view_z, yaw, half_fov_x + 10.0f);
		perf_lap("w_prepare");
		if (!ms_skip(MS_NO_PEOPLE))
			actors_prepare(view_x, view_z); // (posed on another core while the world draws)
		perf_lap("w_terrain");
		if (!indoors && !ms_skip(MS_NO_TERRAIN))
			terrain_draw(view_x, view_z);
		perf_lap("w_objects");
		if (!ms_skip(MS_NO_OBJECTS))
			objects_draw(view_x, view_y, view_z);
		perf_lap(NULL);
		perf_end(PERF_WORLD);
		perf_begin(PERF_ACTORS);
		if (!ms_skip(MS_NO_PEOPLE))
			actors_draw(view_x, view_z);
		wordfx_draw(); // (a Word of Power on the floor: its glowing letters)
		perf_end(PERF_ACTORS);
		perf_begin(PERF_WORLD);
		perf_lap("w_doors");
		if (!ms_skip(MS_NO_CARTS))
			doors_draw(view_x, view_z);
		perf_lap("w_rest");
		projectiles_draw();
		if (third_person) { // (the arrow on the string: from the drawing hand past the bow)
			float palms[2][3], speed, gravity;
			if (bow_nocked >= 0 && actors_player_palms(palms))
				projectiles_draw_nocked(items_flight(bow_nocked, &speed, &gravity), palms[HAND_RIGHT], palms[HAND_LEFT]);
		}
		if (!ms_skip(MS_NO_OBJECTS))
			loot_draw(view_x, view_z);
		perf_lap("w_fish");
		if (!indoors && !ms_skip(MS_NO_WATER))
			fish_draw(view_x, view_z, clock_s);
		perf_lap("w_water");
		if (!indoors && !ms_skip(MS_NO_WATER))
			terrain_draw_water();
		if (!ms_skip(MS_NO_WATER))
			objects_draw_water(view_x, view_z);
		perf_lap(NULL);
		perf_end(PERF_WORLD);
		perf_begin(PERF_FX);
		if (!ms_skip(MS_NO_FX)) {
			objects_draw_fx(view_x, view_z, yaw, clock_s);
			spellfx_draw_world(yaw, pitch);
		}
		{ // (mages' fire bolts in flight)
			static ObjectsSprite bolts[64];
			int nb = projectiles_sprites(bolts, 32);
			nb += traps_sprites(bolts + nb, 64 - nb);
			if (nb)
				objects_draw_sprites(bolts, nb, yaw, pitch);
		}
		float under_fog[3];
		if (!indoors && !atmosphere_underwater_fog(under_fog) && !ms_skip(MS_NO_FX)) { // (rain, snow)
			const float cam_at[3] = {view_x, view_y, view_z};
			weather_draw(cam_at, yaw, pitch);
		}
		if (!third_person && !ms_skip(MS_NO_ARMS)) { // the arms: drawn over everything (never into the wall in front of you)
			glClear(GL_DEPTH_BUFFER_BIT);
			// (their own near plane, a hand's breadth in: a drawn bow's hand at your cheek was cut away
			// by the world's 0.15 m)
			glMatrixMode(GL_PROJECTION);
			glPushMatrix();
			glLoadIdentity();
			gluPerspective(FOV_Y * zoom_fov, (double)SCREEN_W / SCREEN_H, ARMS_NEAR_M, 20.0);
			glMatrixMode(GL_MODELVIEW);
			actors_draw_first();
			float palms[2][3], speed, gravity; // (the arrow on the string, with the arms: they're posed now)
			if (bow_nocked >= 0 && actors_player_palms(palms)) {
				projectiles_draw_nocked(items_flight(bow_nocked, &speed, &gravity), palms[HAND_RIGHT], palms[HAND_LEFT]);
				loot_draw_moving_now();
			}
			spellfx_draw_hands(yaw, pitch); // (the spell in the hands: in front of them)
			glMatrixMode(GL_PROJECTION);
			glPopMatrix();
			glMatrixMode(GL_MODELVIEW);
		}
		perf_end(PERF_FX);
		perf_begin(PERF_POST);
		lockpick_draw();
		preview_draw(clock_s); // (the inventory's item)
		{ // (the motion blur's turn this frame: the view's yaw and pitch moved, as shares of the screen - half a frame's
		  // worth, a film camera's shutter; a jump of the view - a door, a load - none)
			static float last_yaw, last_pitch;
			float dyaw = yaw - last_yaw, dpitch = pitch - last_pitch;
			while (dyaw > 180.0f)
				dyaw -= 360.0f;
			while (dyaw < -180.0f)
				dyaw += 360.0f;
			last_yaw = yaw, last_pitch = pitch;
			const float vfov = view_fov * zoom_fov, hfov = vfov * (float)SCREEN_W / SCREEN_H;
			float u = dyaw / hfov * 0.5f, v = dpitch / vfov * 0.5f;
			if (fabsf(u) > 0.15f || fabsf(v) > 0.15f || transition || menu_is_open())
				u = v = 0.0f;
			post_camera_motion(u, -v);
		}
		post_end();
		if (capture_pending) { // (a save's picture: the scene, before the menus are drawn over it)
			capture_pending = 0;
			capture_thumb();
			if (autosave_now && intro_active() && !intro_stage())
				autosave_now = 0; // (not in Helgen before the keep)
			if (autosave_now) {
				autosave_now = 0;
				SaveGame g;
				fill_save(&g, &player, yaw, pitch, &vitals);
				if (write_game(&g, SAVE_AUTO))
					ui_notice("Autosaved", 3.0f);
			}
		}
		if (!ms_skip(MS_NO_HUD))
			ui_draw();
		if (shot_name[0]) { // (the picture, as the screen shows it: a PPM on the card)
			uint8_t *px = malloc(SCREEN_W * SCREEN_H * 4);
			char path[128];
			snprintf(path, sizeof(path), "ux0:data/opensky_shot_%s.ppm", shot_name);
			FILE *f = px ? fopen(path, "wb") : NULL;
			if (f) {
				glReadPixels(0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, px);
				fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
				for (int y = SCREEN_H - 1; y >= 0; y--)
					for (int x = 0; x < SCREEN_W; x++)
						fwrite(&px[(y * SCREEN_W + x) * 4], 1, 3, f);
				fclose(f);
				game_log("autotest: picture %s", path);
			}
			free(px);
			shot_name[0] = 0;
		}
		perf_end(PERF_POST);

		{ // (every 10 s: where you are and the frame's sections, to the log on the card)
			static unsigned long long logged;
			if (stage == STAGE_PLAY && stream_now_us() - logged > 10000000ull) {
				char line[320];
				const int n = snprintf(line, sizeof(line), "play: %s (%.0f, %.0f) %s | ", interior_place(), player.x,
				                       player.z, interior_current() >= 0 ? "inside" : "outside");
				perf_summary(line + n, sizeof(line) - n);
				boot_log(line);
				static unsigned frame_at_log;
				char laps_line[640];
				const int ln = snprintf(laps_line, sizeof(laps_line), "laps:");
				perf_laps_summary(laps_line + ln, sizeof(laps_line) - ln, (int)(frame - frame_at_log));
				const int used = (int)strlen(laps_line);
				objects_stats(laps_line + used, sizeof(laps_line) - used);
				frame_at_log = frame;
				boot_log(laps_line);
				logged = stream_now_us();
			}
		}
		perf_begin(PERF_SWAP);
		const unsigned long long swap_from = stream_now_us();
		vglSwapBuffers(naming ? GL_TRUE : GL_FALSE); // (the keyboard: the system draws it over the frame)
		swap_wait_s = (stream_now_us() - swap_from) / 1e6f; // (on the GPU: the dynamic resolution's measure)
		if (measure_on) { // (a measure: the frame waited out on the GPU - its work, and the GPU's after it, apart)
			glFinish();
			const unsigned long long now = stream_now_us();
			measure_frame_done(swap_from - measure_from_us, now - swap_from);
			measure_from_us = stream_now_us();
		} else if ((menu_take_measure() || test_measure) && stage == STAGE_PLAY) {
			test_measure = 0;
			measure_start();
			measure_from_us = stream_now_us();
		}
		if (menu_settings()->frame_rate == FPS_30 && !measure_on) { // held to 1/30 s: early, it waits (on a timer, then
			static unsigned long long shown;        //  the screen's refresh: Vita3K doesn't hold it)
			const unsigned long long due = shown + FRAME_30_US;
			unsigned long long now = stream_now_us();
			if (shown && now + 2500 < due)
				sceKernelDelayThread((SceUInt)(due - now - 2500));
			if (shown && stream_now_us() < due)
				sceDisplayWaitVblankStart();
			now = stream_now_us();
			if (shown && now + 500 < due) // (no refresh wait to be had: the rest on the timer)
				sceKernelDelayThread((SceUInt)(due - now));
			now = stream_now_us();
			shown = shown && now < due + FRAME_30_US ? due : now; // (late: from now; on time: steady steps)
		}
		perf_end(PERF_SWAP);
#if DEBUG_TEXTURE_INTEGRITY
		if (frame == 5 || frame % 600 == 0)
			sktex_verify_all(frame == 5 ? "frame 5" : "periodic");
#endif
	}

	music_shutdown();
	sceKernelExitProcess(0);
	return 0;
}
