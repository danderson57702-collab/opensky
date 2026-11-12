#include "actors.h"
#include "stream.h"
#include "skinvert.h"
#include "gamelog.h"
#include "sktex.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "anim.h"
#include "collision.h"
#include "fileio.h"
#include "lighting.h"
#include "navmesh.h"
#include "objects.h"
#include "terrain.h"
#include "worlds.h"

#define SKNP_VERSION 5
#define MAX_ACTORS 1200 // (the whole map: 820 people)
#define MESHES_A_FRAME 1      // NPCs' models read in one frame at most (as they come into view: two at once were a stall the
                              // user felt - a few hundred ms each on the Vita, read from the card on the game's own thread)
#define MESHES_LOADING 8      // ... behind a loading screen (no frame to keep smooth)
static int loading_screen, meshes_waiting, in_view; // (the loading screen up; models still to read, people in view)
#define MESH_IDLE_FRAMES 600u // ... and given back after this long out of sight (~20 s)
#define SCRIPT_FAR_M 80.0f // (a scene's people meant to be seen from afar: drawn this far)
#define PRELOAD_KEEP_FRAMES 5400u // (a model read ahead: kept this long unseen - 3 minutes)
#define MAX_BATCH_LIST 128 // batches in one file's list (a character, a gear file)
#define MAX_BONES 64
#define GPU_BUFFERS_IDLE 600 // (frames: about 20 s unseen)
#define MAX_PATH 48
#define MAX_DRAWN_BATCHES 512

#define SIM_RADIUS_M 110.0f   // closer than this (and on loaded navmesh) they walk (at most: the performance setting's)
#define FAR_THINK_M 30.0f     // beyond this they think every few frames (the performance setting's)
#define DRAW_RADIUS_M draw_radius // the full-detail dissolve band's end (objects_set_near_fade) + a little
static float draw_radius = 60.0f;

void actors_set_draw_radius(float m) {
	draw_radius = m;
}
#define FAR_DRAW_M 600.0f     // a dragon: seen from as far as the fog lets it be
#define WALK_SPEED 1.25f      // m/s, without a walk clip (else the clip's own speed)
#define STRIDE_M 1.5f         // ground covered by one walk cycle (two steps), without a clip
#define PART_SKIN 0x8000      // batch part tags (converter/npcs.py)
#define SLOT_HEAD 0x1u
#define SLOT_HAIR 0x2u
#define MAX_ITEMS 64 // the wardrobe and gear put on since
#define POSE_SMOOTH 12.0f     // the player's pose eases toward the chosen clip (per second)
#define FALL_POSE_AFTER_S 0.5f // walking off an edge: in the air longer than this...
#define FALL_POSE_EASE_S 0.35f  // ...the falling pose blends in over this
#define JUMP_BLEND_IN_S 0.08f   // the take-off clip takes over this fast
#define LANDING_AFTER_S 0.25f   // landing after this long in the air plays the landing
// Skyrim's take-off clip holds a whole hop: feet leave the ground, then touch down again
// at this clip time and the rest of the clip is the knees taking the landing. Its air
// part is played over the physics' air time (the converter took the hop's height out of
// it), so the feet come down as the body does.
#define JUMP_TOUCHDOWN_S 0.83f
#define TORCH_RAISE_RATE 5.0f
#define FIRST_CAMERA_Y 1.721f    // Skyrim's first-person skeleton: its camera node above its origin     // per second
#define RUN_LEAN_UPRIGHT 0.5f // running: how far the spine is stood back up toward the walk posture
#define SNEAK_RUN_FROM 1.4f   // m/s: sneaking faster than this plays the sneak run
#define SWIM_CLIP_SPEED 1.6f  // m/s the swim stroke is played at its own rate for
#define PLAYER_TURN_RATE 9.0f // rad/s: the body turning to where it runs (third person)
#define PLAYER_RUN_FROM 2.4f  // m/s: faster than this plays the run cycle...
#define PLAYER_SPRINT_FROM 3.9f // ...and the sprint
#define TURN_RATE 4.0f        // rad/s
#define ARRIVE_M 0.35f
#define WANDER_MIN_RADIUS_M 1.5f // smaller package radii: stand still
#define FIRE_CLEAR_M 1.2f        // people keep this far from a fire
#define QUANT_EXTENT_M 2.0f   // skinned vertices are quantized within this of the hips
#define RELIGHT_RAD 0.3f      // re-bake vertex light after turning this much
// Re-posing (CPU skinning) is the expensive part: nearby NPCs every frame, farther ones
// every 2nd / 3rd frame (staggered). In between they are drawn with their last pose at
// their current position, which nobody can tell at that distance.
#define POSE_EVERY_FRAME_M 8.0f
#define POSE_HALF_RATE_M 20.0f
#define SEPARATION_M 0.55f   // people keep this far apart (centres)...
#define SEPARATION_RATE 1.5f // ...easing apart at up to this (m/s)
#define SCENE_SEPARATION_M 0.9f   // (a scene's people - Helgen's lineup walked through one another and you: wider, quicker)
#define SCENE_SEPARATION_RATE 3.5f
#define POSE_NEAREST 3        // ...only the nearest this many every frame...
#define SCRIPTED_POSE_M 35.0f // (a scene's people this near: posed every frame, the budget aside)
#define POSE_MAX_PER_FRAME 5  // ...and at most this many in all (others wait their turn)

enum { HIDDEN, IDLE, WALKING };
#define DOOR_STEP_OUT_M 3.0f // coming out of a door: onto the walkable ground this near it
#define DOOR_DROP_M 2.5f     // ... and this far below or above (a door's spot is up in its frame)
enum { OUTDOORS, INSIDE };
enum { ACT_NONE, ACT_SLEEP };
enum { SLEEP_BED_LEFT, SLEEP_BED_RIGHT, SLEEP_BEDROLL };
enum { BED_NONE, BED_IN, BED_OUT };
#define WOKEN_AWAKE_S 25.0f     // woken up: up this long before going back to bed
#define NEAREST_WALKABLE_M 2.5f
#define FLOOR_DY_M 1.2f          // spots are looked up on their own floor (not a loft above or below)
#define BED_CLEAR_HEIGHT_M 1.1f  // a side of the bed is free if nothing blocks at this height // a spot off the navmesh (a bed): walk to the nearest point this close
// Bone order written by converter/npcs.py (BONES)
enum {
	B_COM, B_PELVIS, B_LTHIGH, B_LCALF, B_LFOOT, B_RTHIGH, B_RCALF, B_RFOOT, B_SPINE, B_SPINE1, B_SPINE2, B_NECK,
	B_HEAD, B_LCLAV, B_LUARM, B_LFARM, B_LHAND, B_RCLAV, B_RUARM, B_RFARM, B_RHAND, B_JAW, BONE_COUNT
};
#define JAW_OPEN_RAD 0.22f // fully open (loudest speech)
#define MOUTH_RATE 18.0f   // how fast the jaw follows the voice (per second)

typedef struct {
	float pos[3];
	float uv[2];
	int8_t n[3];
	uint8_t bone2;
	uint8_t col[4];
	uint8_t bone[2];
	uint8_t w[2]; // weights of bone[0], bone[1] (of 255); bone2 gets the rest
} SrcVertex; // 32 bytes, converter/npcs.py VERTEX_DTYPE

typedef struct {
	uint16_t start, end; // minutes of the day
	uint8_t kind;
	uint8_t activity;    // ACT_SLEEP: asleep in a bed (the inside spot is the bed's)
	uint8_t sleep_clip;  // SLEEP_*: how they lie in it
	uint8_t world;       // 0 Tamriel, n: walled city n (worlds.h): its streets, or its door
	float pos[3];        // the spot, or the outside of the building's door
	float heading;
	float radius;
	int32_t interior;    // which building (interiors.txt index), -1: not converted
	float inside[3];     // where in there
	float inside_heading;
	float inside_radius;
} Sched; // 52 bytes, as in the file

typedef struct {
	uint16_t v;
	int16_t d[3]; // where it goes with the mouth fully open, 1/10000 m (model space)
} MouthMorph;   // as in the file (converter/npcs.py: Skyrim's lip-sync morphs)
#define MOUTH_MORPH_UNIT 1e-4f

typedef struct {
	GLuint tex;
	int alpha;
	int vcount, icount;
	SrcVertex *src;
	int part;           // what it belongs to (see PART_SKIN)
	int lit;            // its vertex light has been baked (for the current heading)
	uint16_t *idx;
	ObjectVertex *out; // uv and colour filled in; positions written each frame
	float uv[3];
	MouthMorph *mouth; // talking: vertices of the face that move with the mouth (by vertex)
	int nmouth;
	int gear_tag;      // a weapon's: GEAR_HAND (drawn), GEAR_SHEATHED, GEAR_SCABBARD; 0 otherwise
	GLuint vbo, ibo;   // posed on the GPU: its vertices and indices there (made when first drawn, freed
	                   // when it's been out of sight a while: GPU_BUFFERS_IDLE)
	int is_hair;
	// The character creator's (the player's: .skmorph, converter/npcs.py write_skmorph): its part of
	// the look - the face (its texture made from the tint layers), bare skin (the skin tone), hair (the
	// hair colour) - and its morphs (the face's sliders, the weight), over its vertices as loaded
	uint8_t role;
	uint8_t toned;     // gear's bare skin (SKIN_TONE_TAG): the wearer's skin tone too
	float *base_pos;   // (morphed: the positions as loaded, 3 a vertex)
	struct LookMorph *morph;
	int nmorph;
} ABatch;
#define ROLE_FACE 1
#define ROLE_SKIN 2
#define ROLE_HAIR 3
typedef struct LookMorph {
	uint16_t name; // (look_morph_names)
	uint16_t v;
	int16_t d[3]; // 1/10000 m, model space
} LookMorph;
enum { GEAR_NONE, GEAR_HAND, GEAR_SHEATHED, GEAR_SCABBARD }; // (converter/gear.py)

typedef struct {
	char name[32];
	int nbones;
	int parent[MAX_BONES];
	float joint[MAX_BONES][3];
	Sched *sched;
	int nsched;
	Sched deflt;
	ABatch *batches;
	int nbatches;

	const Sched *goal;
	int state, simulated, need_plan;
	float x, y, z, heading;
	float door[3];
	int has_door;
	NavRef nav;
	float path[MAX_PATH * 3];
	int path_n, path_i;
	float wait;       // idle time left before wandering, or before retrying a plan
	int asleep;       // lying in the goal's bed (its sleeping spot, sleep_clip)
	int bed_anim;     // BED_IN getting into bed, BED_OUT getting up (Skyrim's clips, root motion)
	float bed_t;      // ... how far into it
	float skip_dt;    // (far off, thinking every few frames: the time since it last did)
	float bed_x, bed_z, bed_h; // where (and facing) the clip started
	int bed_clip;     // SLEEP_*: which side of the bed they get in and out (chosen per bed)
	float walk;       // 0 standing .. 1 walking (animation blend)
	float phase;      // walk cycle 0..1
	float t;          // seconds, for idle motion
	float look, look_target, look_timer; // head turn (rad)
	float lit_heading;
	int lit_valid;
	float attend_time, attend_x, attend_z; // talking: stand still facing this point
	Sched eff;           // indoors: the goal as seen from inside the player's building
	const Sched *goal_src;
	int goal_mode;       // what eff was made for (see effective_goal)
	int goal_changed;
	char id[64]; // pack file name (editor id, lowercase)
	int female;  // which skeleton / clip set (CLIPS_FIRST: the first-person arms)
	float pitch; // the first-person arms: tilted with the view (a creature falling dead: over backwards)
	float roll;  // a creature falling dead: onto its side
	float scale; // drawn this much bigger (a creature's race height: a giant spider); 0: 1
	float quant_m; // its skinned vertices are quantized within this of the hips (0: QUANT_EXTENT_M)
	float palette[MAX_BONES * 12]; // its pose for the GPU (objects_skinning): per bone 3 rows, rotation | translation
	int drawn_frame;               // (skin_frame when last drawn on the GPU: its buffers freed after a while)
	int mesh_loaded;               // (an NPC: its batches read - only while near and in view; the rest of it always)
	unsigned seen_frame;           // ... when it was last in view
	unsigned keep_until;           // (read ahead of its scene - actors_preload: not given back before this frame)
	int script_far;                // (a scene's, meant to be seen from afar - Tullius and Elenwen past Helgen's gate: drawn
	                               //  out to SCRIPT_FAR_M whatever the setting's reach for people)
	int far;       // (a creature) drawn out to FAR_DRAW_M, without the distance dissolve (a dragon)
	float fade;    // ... this much of it dithered away (0..1: a dead dragon's skin burning off)
	int ghost;     // a spirit's: half see-through, blue-white (a conjured familiar - Skyrim's spectral wolf)
	int lit_pale;  // (the spirit look its colours were lit with: 0 none, 1 a spirit's, 2 Become Ethereal's)
	struct {
		char name[48];
		uint32_t slots;
		int equipped;
		int weapon;      // a weapon (gear): drawn or sheathed
		int tint;        // poisoned (a weapon): a sickly green sheen
		unsigned glow;   // enchanted (a weapon): its element's colour, pulsing on the blade (0xRRGGBB, 0 none)
		uint32_t hidden_by; // (one of the character's own parts - chargen.c: hidden while these slots are covered)
		int part;        // (... one of them: worn for good, never shown in the wardrobe)
	} items[MAX_ITEMS]; // the player's wardrobe
	int nitems;
	BonePose pose[ANIM_MAX_BONES]; // the player's pose (eased between clips)
	int has_pose;
	int posed;   // the vertex buffers hold a pose
	float mouth, mouth_target; // 0 closed .. 1 open (talking; set each frame by talk.c)
	unsigned rng;
	// Directed by a script (the opening, intro.c): its schedule set aside
	int scripted;
	float script_stuck; // (a script's walk getting nowhere: seconds)
	unsigned pose_frame; // (the draw frame it was last posed in)
	float script_best;  // (the nearest it's got to its path's next point, metres)
	int script_wp;      // (which point that is)
	int show_prop; // (PROP_PART batches drawn: actors_script_prop)
	int headless; // (the block's: the axe has fallen - the head and all on it gone)
	int corpse; // (a scene's dead: lies where they fell; searched, not talked to)
	int headonly; // (a head cut off, laid in a crate: all of the body drawn into the neck but the head)
	int script_walk, script_follow, script_run; // walking to script_to; following the player; running
	float script_to[3], script_t, script_repath;
	char script_clip[24];  // standing in this clip ("": idle)
	int script_once;       // ... played once, held at its end (kneeling at the block)
	char script_next[24];  // ... then this, looping (thrown back, then cowering)
	float script_look;     // the head turned this far (rad) toward someone they're talking to
	int torch_item;      // the player's torch (an item; -1: none)
	float torch_tip[3];  // its flame end, bind pose (model space)
	float torch_w;       // 0 .. 1: the arm raised to hold it
	float torch_at[3];   // the flame, world space, when last skinned
	int torch_at_frame;
	float magic_w[2];    // 0 .. 1: the left / right arm casting (a readied spell)
	float palm_at[2][3]; // where spells leave the left / right hand, world, when last skinned
	int palm_frame;
} Actor;

static Actor actors[MAX_ACTORS];
static int actor_count;
static int cur_interior = -1;  // the building the player is in, -1 outdoors
#define SNEAK_HEAD_STEADY 0.75f // sneaking: the head held this much to the standing idle's
#define BEAST_EVERY_FRAME_M 9.0f
#define BEAST_HALF_RATE_M 22.0f
#define BEAST_KINDS 384 // creature and enemy kinds (creatures.c)
#define BEAST_SETS 48   // their clip sets (people use the characters')
#define MAX_BEASTS 24
static ClipSet clip_sets[3 + BEAST_SETS]; // male, female, first person (the arms in front of the camera), creatures
#define CLIPS_FIRST 2
static Actor player;
static Actor player1st; // the first-person arms (hands, sleeves, what they hold)
static int player1st_loaded, player1st_shown;
static int player_loaded, player_shown;
static int player_base_items; // the player's own clothes (gear put on later comes after them)
static float player_air_s;     // time off the ground (the falling clip)
static float exit_door[3];     // its door to Tamriel, from the inside
// Skyrim's magic nodes per clip set: where spells leave the left and right hands (bind
// pose, model space; anims/magic_nodes.txt)
static float magic_node[3][2][3];
static int magic_node_ok[3];
// Getting into and out of bed: Skyrim's root motion for those clips (anims/root_motion.txt):
// per clip set (male, female) and clip (in: left, right, bedroll; then out)
#define ROOT_KEYS 24
typedef struct {
	int n;
	float key[ROOT_KEYS][4]; // t, x, z (model space, metres), yaw (radians)
} RootMotion;
static RootMotion bed_motion[2][6];
// Other clips that move the whole body (sitting down, getting up), by name, per clip set
#define NAMED_MOTIONS 16
static struct {
	char name[24];
	RootMotion m;
} named_motion[2][NAMED_MOTIONS];
static int named_motion_count[2];
static const char *bed_clip_names[6] = {"bed_left_in", "bed_right_in", "bedroll_in",
                                        "bed_left_out", "bed_right_out", "bedroll_out"};
static int magic_pose[2] = {-1, -1};
static int player_sneaking; // (the weapon stances crouch with you)
#define SKIN_TONE_TAG 0x4000 // (converter/npcs.py)
#define LOOK_MAX_RAD 1.45f   // (a head turned toward someone, at most: about 83 degrees, the neck half of it)
#define PROP_PART 0x1000     // (converter/npcs.py PART_PROP: a scene's prop in hand)
#define BOUND_TAG 0x2000     // (converter/gear.py: a bound weapon)
static uint8_t player_tone[3] = {255, 255, 255}; // the player's skin tone, for gear's bare skin
static int gear_slot_free[MAX_ITEMS]; // (gear taken off the player: its slot to reuse)
static int weapon_drawn;  // the player's weapons in hand (readied) or sheathed
static int weapon_kind = -1; // the weapon in the right hand (Skyrim's animation type), -1 none
static int left_kind = -1;   // ... and in the left (dual wielding: one-handed ones), -1

// An arm holding a drawn weapon: the right; both for two-handed ones and bows
static int weapon_arm(int arm) {
	if (arm == 0 && weapon_drawn && left_kind > 0)
		return 1;
	return weapon_drawn && weapon_kind > 0 && (arm == 1 || weapon_kind >= 5);
} // the player's left / right arm casting (actors_player_magic)

// Skyrim's combat stance for what's readied: its locomotion clips ("1hm_walk_left"...),
// or NULL (nothing readied: the ordinary ones)
static const char *combat_set(void) {
	if (weapon_drawn && weapon_kind <= 0 && left_kind > 0)
		return "1hm";
	if (weapon_drawn && weapon_kind > 0)
		return weapon_kind == 5 || weapon_kind == 6 || weapon_kind == 8 ? "2hm"
		       : weapon_kind == 7 || weapon_kind == 9                    ? "bow"
		                                                                 : "1hm";
	return magic_pose[0] >= 0 || magic_pose[1] >= 0 ? "mag" : NULL;
}

static float frand(Actor *a) {
	a->rng = a->rng * 1664525u + 1013904223u;
	return (a->rng >> 8) / 16777216.0f;
}

// --- Loading ----------------------------------------------------------------------------

typedef struct {
	const uint8_t *p, *end;
	int ok;
} Cur;

static void take(Cur *c, void *dst, size_t n) {
	if (!c->ok || c->p + n > c->end) {
		c->ok = 0;
		return;
	}
	if (dst)
		memcpy(dst, c->p, n);
	c->p += n;
}

static void quantize_uvs(ABatch *b) {
	float lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
	for (int i = 0; i < b->vcount; i++)
		for (int k = 0; k < 2; k++)
			lo[k] = fminf(lo[k], b->src[i].uv[k]), hi[k] = fmaxf(hi[k], b->src[i].uv[k]);
	float extent = fmaxf(fmaxf(hi[0] - lo[0], hi[1] - lo[1]) * 0.5f, 1e-3f);
	b->uv[0] = (lo[0] + hi[0]) * 0.5f, b->uv[1] = (lo[1] + hi[1]) * 0.5f, b->uv[2] = extent;
	for (int i = 0; i < b->vcount; i++) {
		ObjectVertex *o = &b->out[i];
		o->u = (int16_t)lrintf((b->src[i].uv[0] - b->uv[0]) / extent * 32767.0f);
		o->v = (int16_t)lrintf((b->src[i].uv[1] - b->uv[1]) / extent * 32767.0f);
		o->a = b->src[i].col[3];
		o->pad = 0;
	}
}

// n batches at the cursor, appended to the actor's (item >= 0: they belong to that item,
// their tag being a weapon's GEAR_*); 0 if malformed
static int read_batches(Cur *cp, Actor *a, uint32_t n, int item) {
	Cur c = *cp;
	ABatch *grown = realloc(a->batches, (a->nbatches + n + 1) * sizeof(ABatch));
	if (!grown)
		return 0;
	a->batches = grown;
	memset(a->batches + a->nbatches, 0, (n + 1) * sizeof(ABatch));
	for (uint32_t i = 0; i < n && c.ok; i++) {
		ABatch *b = &a->batches[a->nbatches];
		uint8_t hdr[4];
		char tex[128] = "";
		uint32_t vc, ic;
		take(&c, hdr, 4);
		if (hdr[1] >= sizeof(tex))
			c.ok = 0;
		take(&c, tex, hdr[1]);
		take(&c, NULL, (4 - hdr[1] % 4) % 4);
		take(&c, &vc, 4);
		take(&c, &ic, 4);
		if (!c.ok || vc > 65535 || ic > 0x100000)
			return 0;
		b->alpha = hdr[0];
		b->part = hdr[2] | hdr[3] << 8;
		const int toned = (b->part & SKIN_TONE_TAG) != 0; // (gear's bare skin: the wearer's tone)
		b->toned = (uint8_t)toned;
		const int bound = (b->part & BOUND_TAG) != 0;     // (a bound weapon: ghostly light)
		b->part &= ~(SKIN_TONE_TAG | BOUND_TAG);
		if (bound)
			b->alpha = ALPHA_GLOW;
		if (item >= 0) // (gear: its item; a weapon's batches say drawn or sheathed)
			b->gear_tag = !(b->part & PART_SKIN) && (b->part > 1 || (b->part == 1 && a->items[item].weapon)) ? b->part : GEAR_NONE, // (a hair's
			b->part = item + 1; // skin-and-slot tag isn't a weapon's state: read as one, the character creator's hair cards were never drawn -
			                    // only their thin hairline shell, bare skin showing where the long hair should be)
		b->is_hair = 0;
		if (item >= 0 && item < a->nitems) {
			if (!strcmp(a->items[item].name, "Hair") || !strcmp(a->items[item].name, "Beard"))
				b->is_hair = 1;
		}
		if ((b->part & PART_SKIN) && (b->part & SLOT_HAIR))
			b->is_hair = 1;
		if (strstr(tex, "hair") || strstr(tex, "Hair") || strstr(tex, "HAIR") ||
		    strstr(tex, "hlne") || strstr(tex, "Hlne") || strstr(tex, "HLNE") ||
		    strstr(tex, "beard") || strstr(tex, "Beard") || strstr(tex, "BEARD"))
			b->is_hair = 1;
		if (b->is_hair && !b->alpha)
			b->alpha = 1;
		b->vcount = (int)vc, b->icount = (int)ic;
		b->src = malloc(vc * sizeof(SrcVertex));
		b->idx = malloc(ic * sizeof(uint16_t));
		b->out = calloc(vc ? vc : 1, sizeof(ObjectVertex));
		if (!b->src || !b->idx || !b->out)
			return 0;
		take(&c, b->src, vc * sizeof(SrcVertex));
		if (toned && c.ok)
			for (uint32_t k = 0; k < vc; k++)
				for (int j = 0; j < 3; j++)
					b->src[k].col[j] = (uint8_t)(b->src[k].col[j] * player_tone[j] / 255);
		if (b->is_hair && c.ok) // (hair cards' own normals point every which way - Skyrim's hair shader doesn't light by them; here each
			for (uint32_t k = 0; k < vc; k++) // card was lit or black by its facing, a checkerboard: one soft up-normal, evenly lit)
				b->src[k].n[0] = 0, b->src[k].n[1] = 127, b->src[k].n[2] = 0;
		if (bound && c.ok) // (a cold blue-violet, as Skyrim's bound weapons)
			for (uint32_t k = 0; k < vc; k++)
				b->src[k].col[0] = 70, b->src[k].col[1] = 110, b->src[k].col[2] = 255;
		take(&c, b->idx, ic * sizeof(uint16_t));
		take(&c, NULL, (ic * 2) % 4);
		uint32_t nm = 0;
		take(&c, &nm, 4);
		if (!c.ok || nm > vc)
			return 0;
		if (nm) {
			b->mouth = malloc(nm * sizeof(MouthMorph));
			if (!b->mouth)
				return 0;
			take(&c, b->mouth, nm * sizeof(MouthMorph));
			b->nmouth = (int)nm;
			for (uint32_t k = 0; k < nm && c.ok; k++)
				if (b->mouth[k].v >= vc || (k && b->mouth[k].v <= b->mouth[k - 1].v))
					c.ok = 0; // (sorted by vertex: skin() walks them alongside)
		}
		for (uint32_t k = 0; k < ic && c.ok; k++)
			if (b->idx[k] >= vc)
				c.ok = 0;
		for (uint32_t k = 0; k < vc && c.ok; k++)
			if (b->src[k].bone[0] >= a->nbones || b->src[k].bone[1] >= a->nbones || b->src[k].bone2 >= a->nbones ||
			    b->src[k].w[0] + b->src[k].w[1] > 255)
				c.ok = 0;
		if (!c.ok)
			return 0;
		quantize_uvs(b);
		b->tex = objects_texture(tex);
		a->nbatches++;
	}
	*cp = c;
	return c.ok;
}

// n batches at the cursor, passed over (an NPC's model: read when it's first in view)
static int skip_batches(Cur *c, uint32_t n) {
	for (uint32_t i = 0; i < n && c->ok; i++) {
		uint8_t hdr[4];
		uint32_t vc, ic, nm = 0;
		take(c, hdr, 4);
		take(c, NULL, hdr[1] + (4 - hdr[1] % 4) % 4);
		take(c, &vc, 4);
		take(c, &ic, 4);
		if (!c->ok || vc > 65535 || ic > 0x100000)
			return 0;
		take(c, NULL, vc * sizeof(SrcVertex) + ic * sizeof(uint16_t) + (ic * 2) % 4);
		take(c, &nm, 4);
		if (!c->ok || nm > vc)
			return 0;
		take(c, NULL, nm * sizeof(MouthMorph));
	}
	return c->ok;
}

static int load_actor_ex(const char *path, Actor *a, int meshes);
static int load_actor(const char *path, Actor *a) {
	return load_actor_ex(path, a, 1);
}

static char actors_pack[128];
static void free_gpu_buffers(ABatch *b);
static void load_look(Actor *a, const char *model_path, int list, int first, int count);
static void apply_look_range(Actor *a, int first, int count);

// An NPC's model, read now (in view): its batches, as load_actor would have
static int load_mesh(Actor *a) {
	char path[256];
	snprintf(path, sizeof(path), "%sactors/%s.sknpc", actors_pack, a->id);
	long size = 0;
	uint8_t *buf = file_read_all(path, &size);
	if (!buf)
		return 0;
	Cur c = {buf, buf + size, 1};
	uint32_t n = 0;
	take(&c, NULL, 8 + 32);
	take(&c, &n, 4);
	take(&c, NULL, n * 16);
	take(&c, &n, 4);
	take(&c, NULL, (n + 1) * sizeof(Sched));
	take(&c, &n, 4);
	const int ok = c.ok && n <= MAX_BATCH_LIST && read_batches(&c, a, n, -1);
	free(buf);
	a->mesh_loaded = 1; // (even if bad: not tried again every frame)
	return ok;
}

static void free_mesh(Actor *a) {
	for (int i = 0; i < a->nbatches; i++)
		free_gpu_buffers(&a->batches[i]), free(a->batches[i].src), free(a->batches[i].idx), free(a->batches[i].out),
		free(a->batches[i].mouth);
	free(a->batches);
	a->batches = NULL, a->nbatches = 0, a->mesh_loaded = 0, a->drawn_frame = 0, a->posed = 0;
}

static int load_actor_ex(const char *path, Actor *a, int meshes) {
	long size = 0;
	uint8_t *buf = file_read_all(path, &size);
	if (!buf)
		return 0;
	Cur c = {buf, buf + size, 1};
	char magic[4];
	uint16_t version, flags = 0;
	uint32_t n = 0;
	take(&c, magic, 4);
	take(&c, &version, 2);
	take(&c, &flags, 2);
	a->female = flags & 1;
	take(&c, a->name, 32);
	a->name[31] = 0;
	if (!c.ok || memcmp(magic, "SKNP", 4) || version != SKNP_VERSION)
		goto bad;
	take(&c, &n, 4);
	if (n > MAX_BONES)
		goto bad;
	a->nbones = (int)n;
	for (int i = 0; i < a->nbones; i++) {
		int32_t parent = -1;
		take(&c, &parent, 4);
		take(&c, a->joint[i], 12);
		a->parent[i] = parent >= 0 && parent < i ? parent : -1;
	}
	take(&c, &n, 4);
	if (!c.ok || n > 256)
		goto bad;
	a->nsched = (int)n;
	a->sched = malloc((n ? n : 1) * sizeof(Sched));
	take(&c, a->sched, n * sizeof(Sched));
	take(&c, &a->deflt, sizeof(Sched));
	take(&c, &n, 4);
	if (!c.ok || n > MAX_BATCH_LIST || !(meshes ? read_batches(&c, a, n, -1) : skip_batches(&c, n)))
		goto bad;
	a->mesh_loaded = meshes;
	take(&c, &n, 4);
	for (uint32_t i = 0; i < n && c.ok; i++) {
		uint8_t eq_len[2];
		uint32_t slots;
		char name[256];
		take(&c, &slots, 4);
		take(&c, eq_len, 2);
		take(&c, name, eq_len[1]);
		take(&c, NULL, (4 - (6 + eq_len[1]) % 4) % 4);
		if (c.ok && a->nitems < MAX_ITEMS) {
			snprintf(a->items[a->nitems].name, sizeof(a->items[0].name), "%.*s", eq_len[1], name);
			a->items[a->nitems].slots = slots;
			a->items[a->nitems].equipped = eq_len[0];
			a->nitems++;
		}
	}
	if (!c.ok)
		goto bad;
	free(buf);
	return 1;
bad:
	sceClibPrintf("actors: bad file %s\n", path);
	free(buf);
	return 0;
}

// The player's torch: the item named so, and its flame end (the vertex farthest from the
// hand holding it)
static void find_torch(Actor *a) {
	a->torch_item = -1;
	for (int i = 0; i < a->nitems; i++)
		if (!strncmp(a->items[i].name, "Torch", 5))
			a->torch_item = i;
	if (a->torch_item < 0 || a->nbones <= B_LHAND)
		return;
	float best = -1.0f;
	for (int b = 0; b < a->nbatches; b++) {
		const ABatch *bt = &a->batches[b];
		if (bt->part != a->torch_item + 1)
			continue;
		for (int v = 0; v < bt->vcount; v++) {
			const float *p = bt->src[v].pos, *j = a->joint[B_LHAND];
			float d = (p[0] - j[0]) * (p[0] - j[0]) + (p[1] - j[1]) * (p[1] - j[1]) + (p[2] - j[2]) * (p[2] - j[2]);
			if (d > best)
				best = d, memcpy(a->torch_tip, p, sizeof(a->torch_tip));
		}
	}
	if (best < 0.0f)
		a->torch_item = -1;
}


// A batch's GPU buffers given back
static void free_gpu_buffers(ABatch *b) {
	if (b->vbo)
		glDeleteBuffers(1, &b->vbo);
	if (b->ibo)
		glDeleteBuffers(1, &b->ibo);
	b->vbo = b->ibo = 0;
}

static void free_batch(ABatch *b) {
	free_gpu_buffers(b), free(b->src), free(b->idx), free(b->out), free(b->mouth), free(b->base_pos), free(b->morph);
	b->src = NULL, b->idx = NULL, b->out = NULL, b->mouth = NULL, b->base_pos = NULL, b->morph = NULL;
}

static void free_actor(Actor *a) {
	for (int i = 0; i < a->nbatches; i++)
		free_batch(&a->batches[i]);
	free(a->batches);
	free(a->sched);
	memset(a, 0, sizeof(*a));
}

int actors_player_load(const char *stem, const unsigned char tone[3]) {
	char path[256];
	const float x = player.x, y = player.y, z = player.z, heading = player.heading;
	free_actor(&player1st), free_actor(&player);
	player_loaded = player1st_loaded = player_shown = player1st_shown = 0;
	if (tone)
		memcpy(player_tone, tone, 3);
	snprintf(path, sizeof(path), "%sactors/player/%s_1st.sknpc", actors_pack, stem);
	if (load_actor(path, &player1st)) {
		load_look(&player1st, path, 0, 0, player1st.nbatches);
		player1st.female = CLIPS_FIRST;
		player1st.state = IDLE;
		find_torch(&player1st);
		player1st_loaded = clip_sets[CLIPS_FIRST].bones > 0;
	}
	snprintf(path, sizeof(path), "%sactors/player/%s.sknpc", actors_pack, stem);
	if (!load_actor(path, &player)) {
		sceClibPrintf("actors: no player %s\n", path);
		return 0;
	}
	load_look(&player, path, 0, 0, player.nbatches);
	player_loaded = 1;
	player_base_items = player.nitems;
	memset(gear_slot_free, 0, sizeof(gear_slot_free)); // (a new body: its gear loaded afresh)
	find_torch(&player);
	player.state = IDLE;
	player.x = x, player.y = y, player.z = z, player.heading = heading;
	return 1;
}

void actors_init(const char *pack_dir) {
	char path[256];
	for (int f = 0; f < 2; f++) {
		snprintf(path, sizeof(path), "%sanims/%s.skanim", pack_dir, f ? "female" : "male");
		if (!anim_load(path, &clip_sets[f]))
			sceClibPrintf("actors: no animations at %s (procedural motion)\n", path);
	}
	snprintf(path, sizeof(path), "%sanims/first.skanim", pack_dir);
	if (!anim_load(path, &clip_sets[CLIPS_FIRST]))
		sceClibPrintf("actors: no first-person animations at %s\n", path);
	snprintf(path, sizeof(path), "%sanims/magic_nodes.txt", pack_dir);
	long nodes_size = 0;
	char *nodes = file_read_all(path, &nodes_size);
	if (nodes) {
		char *text = realloc(nodes, nodes_size + 1);
		if (text) {
			text[nodes_size] = 0;
			for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
				char name[16];
				float *m = NULL, v[6];
				if (sscanf(line, "%15s %f %f %f %f %f %f", name, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 7)
					continue;
				int set = !strcmp(name, "male") ? 0 : !strcmp(name, "female") ? 1 : !strcmp(name, "first") ? CLIPS_FIRST : -1;
				if (set < 0)
					continue;
				m = &magic_node[set][0][0];
				memcpy(m, v, sizeof(v));
				magic_node_ok[set] = 1;
			}
			free(text);
		} else {
			free(nodes);
		}
	}
	snprintf(path, sizeof(path), "%sanims/root_motion.txt", pack_dir);
	long motion_size = 0;
	char *motion = file_read_all(path, &motion_size);
	char *mtext = motion ? realloc(motion, motion_size + 1) : NULL;
	if (mtext) {
		mtext[motion_size] = 0;
		for (char *line = strtok(mtext, "\n"); line; line = strtok(NULL, "\n")) {
			char set_name[16], clip[24];
			int n = 0, used = 0;
			if (sscanf(line, "%15s %23s %d%n", set_name, clip, &n, &used) != 3)
				continue;
			int set = !strcmp(set_name, "female"), k = -1;
			for (int c = 0; c < 6; c++)
				if (!strcmp(clip, bed_clip_names[c]))
					k = c;
			if (n <= 0 || n > ROOT_KEYS)
				continue;
			if (k < 0 && named_motion_count[set] >= NAMED_MOTIONS)
				continue;
			if (k < 0)
				snprintf(named_motion[set][named_motion_count[set]].name, 24, "%s", clip);
			RootMotion *m = k >= 0 ? &bed_motion[set][k] : &named_motion[set][named_motion_count[set]++].m;
			const char *q = line + used;
			for (m->n = 0; m->n < n; m->n++) {
				float *key = m->key[m->n];
				if (sscanf(q, "%f %f %f %f%n", &key[0], &key[1], &key[2], &key[3], &used) != 4)
					break;
				q += used;
			}
		}
		free(mtext);
	} else {
		free(motion);
	}
	snprintf(actors_pack, sizeof(actors_pack), "%s", pack_dir);
	snprintf(path, sizeof(path), "%sactors/actors.txt", pack_dir);
	long size = 0;
	char *list = file_read_all(path, &size);
	if (!list) {
		sceClibPrintf("actors: no %s\n", path);
		return;
	}
	char *line = list, *end = list + size;
	while (line < end && actor_count < MAX_ACTORS) {
		char *nl = memchr(line, '\n', end - line);
		if (!nl)
			nl = end;
		*nl = 0;
		if (*line) {
			Actor *a = &actors[actor_count];
			memset(a, 0, sizeof(*a));
			snprintf(path, sizeof(path), "%sactors/%s", pack_dir, line);
			if (load_actor_ex(path, a, 0)) { // (its model when it's in view: 505 people's took 148 MB)
				// The file name without ".sknpc" (the converter writes the editor id in lowercase)
				snprintf(a->id, sizeof(a->id), "%.*s", (int)(strcspn(line, ".")), line);
				a->rng = 0x9e3779b9u * (unsigned)(actor_count + 1);
				a->nav.tri = -1;
				a->state = IDLE;
				a->t = frand(a) * 100.0f;
				actor_count++;
			}
		}
		line = nl + 1;
	}
	free(list);
	sceClibPrintf("actors: %d NPCs\n", actor_count);
}

// --- Behaviour ------------------------------------------------------------------------

static const Sched *current_goal(const Actor *a, float minute) {
	for (int i = 0; i < a->nsched; i++)
		if (minute >= a->sched[i].start && minute < a->sched[i].end)
			return &a->sched[i];
	return &a->deflt;
}

static float wrap_angle(float x) {
	while (x > (float)M_PI)
		x -= 2.0f * (float)M_PI;
	while (x < -(float)M_PI)
		x += 2.0f * (float)M_PI;
	return x;
}

// Two schedule entries that keep them in the same place doing the same (a night's sleep
// split at midnight): no reason to get up and lie down again
static int same_spot(const Sched *a, const Sched *b) {
	if (!a || !b)
		return 0;
	if (a == b)
		return 1;
	return a->kind == b->kind && a->interior == b->interior && a->activity == b->activity &&
	       fabsf(a->pos[0] - b->pos[0]) + fabsf(a->pos[2] - b->pos[2]) < 0.05f &&
	       fabsf(a->inside[0] - b->inside[0]) + fabsf(a->inside[2] - b->inside[2]) < 0.05f &&
	       fabsf(a->radius - b->radius) < 0.01f;
}

static const RootMotion *bed_motion_for(const Actor *a, int clip, int out) {
	return &bed_motion[a->female == 1][clip % 3 + (out ? 3 : 0)];
}

// The root motion at t: (x, z) model space at the clip's start, yaw (from rest at t = 0)
static void root_at(const RootMotion *m, float t, float out[3]) {
	float prev[4] = {0, 0, 0, 0};
	for (int i = 0; i < m->n; i++) {
		const float *k = m->key[i];
		if (k[0] >= t) {
			float u = k[0] > prev[0] ? (t - prev[0]) / (k[0] - prev[0]) : 1.0f;
			for (int c = 0; c < 3; c++)
				out[c] = prev[c + 1] + (k[c + 1] - prev[c + 1]) * u;
			return;
		}
		memcpy(prev, k, sizeof(prev));
	}
	memcpy(out, prev + 1, 3 * sizeof(float));
}

static const RootMotion *root_named(int set, const char *name) {
	static const RootMotion none;
	for (int k = 0; k < 6; k++) // (the bed clips': kept for NPCs' beds)
		if (!strcmp(bed_clip_names[k], name))
			return &bed_motion[set][k];
	for (int i = 0; i < named_motion_count[set]; i++)
		if (!strcmp(named_motion[set][i].name, name))
			return &named_motion[set][i].m;
	return &none;
}

static float root_duration(const RootMotion *m) {
	return m->n ? m->key[m->n - 1][0] : 0.0f;
}

// A model-space (x, z) turned by heading h into the world
static void turn_xz(float h, float x, float z, float *wx, float *wz) {
	*wx = cosf(h) * x + sinf(h) * z;
	*wz = -sinf(h) * x + cosf(h) * z;
}

// Where getting into the goal's bed starts (beside it, facing it): the clip's root motion
// ends at the bed's sleeping spot
static void bed_start(const Actor *a, const Sched *g, int clip, float *x, float *z, float *h) {
	const RootMotion *m = bed_motion_for(a, clip, 0);
	*x = g->pos[0], *z = g->pos[2], *h = g->heading;
	if (!m->n)
		return;
	const float *end = m->key[m->n - 1];
	float wx, wz;
	*h = g->heading - end[3];
	turn_xz(*h, end[1], end[2], &wx, &wz);
	*x -= wx, *z -= wz;
}

// Which side of the goal's bed to get in by: one Skyrim's marker allows (sleep_clip: 1 left,
// 2 right, 4 a bedroll) where the spot beside the bed is floor on the bed's level with
// nothing (a wall) between it and the bed
static int choose_bed_clip(const Actor *a, const Sched *g) {
	int mask = g->sleep_clip;
	if (mask & 4)
		return SLEEP_BEDROLL;
	if (!(mask & 3))
		mask = 1;
	int best = SLEEP_BED_LEFT, best_score = -1;
	for (int k = SLEEP_BED_LEFT; k <= SLEEP_BED_RIGHT; k++) {
		if (!(mask & (1 << k)))
			continue;
		float x, z, h, hh;
		bed_start(a, g, k, &x, &z, &h);
		NavRef r;
		int score = 0;
		if (navmesh_locate_level(x, g->pos[1], z, 0.6f, &r, &hh))
			score += 2;
		const float from[3] = {g->pos[0], g->pos[1] + BED_CLEAR_HEIGHT_M, g->pos[2]};
		const float to[3] = {x, g->pos[1] + BED_CLEAR_HEIGHT_M, z};
		if (collision_ray(from, to) >= 1.0f)
			score += 1;
		if (score > best_score)
			best = k, best_score = score;
	}
	return best;
}

static void bed_begin(Actor *a, int which) {
	a->bed_anim = which, a->bed_t = 0.0f;
	if (which == BED_IN && a->goal) {
		bed_start(a, a->goal, a->bed_clip, &a->bed_x, &a->bed_z, &a->bed_h);
		a->y = a->goal->pos[1]; // (on the bed's floor)
	} else {
		a->bed_x = a->x, a->bed_z = a->z, a->bed_h = a->heading;
	}
	a->x = a->bed_x, a->z = a->bed_z, a->heading = a->bed_h;
	a->asleep = 0;
	a->state = IDLE;
}

// Getting into / out of bed, a frame on: 1 while it goes on
static int bed_step(Actor *a, float dt) {
	if (!a->bed_anim)
		return 0;
	const RootMotion *m = bed_motion_for(a, a->bed_clip, a->bed_anim == BED_OUT);
	a->bed_t += dt;
	float r[3], wx, wz;
	root_at(m, a->bed_t, r);
	turn_xz(a->bed_h, r[0], r[1], &wx, &wz);
	a->x = a->bed_x + wx, a->z = a->bed_z + wz;
	a->heading = wrap_angle(a->bed_h + r[2]);
	if (a->bed_t < root_duration(m))
		return 1;
	if (a->bed_anim == BED_IN && a->goal) { // in: asleep on the sleeping spot
		a->asleep = 1;
		a->x = a->goal->pos[0], a->y = a->goal->pos[1], a->z = a->goal->pos[2], a->heading = a->goal->heading;
		a->wait = 1e9f;
	} else { // up: standing beside the bed, back on the navmesh (on the bed's floor)
		float h, at[3];
		a->nav.tri = -1;
		if (navmesh_locate_level(a->x, a->y, a->z, FLOOR_DY_M, &a->nav, &h))
			a->y = h;
		else if (navmesh_nearest_level(a->x, a->y, a->z, NEAREST_WALKABLE_M, FLOOR_DY_M, &a->nav, at))
			a->x = at[0], a->y = at[1], a->z = at[2];
	}
	a->bed_anim = BED_NONE;
	return 0;
}

// Somewhere to stand at a spot: within a wandering spot's area rather than its middle (an
// inn's is its hearth), on the walkable floor, and never in a fire
static void place_on_floor(Actor *a, const Sched *goal) {
	for (int tries = 0; tries < 8; tries++) {
		float px = goal->pos[0], pz = goal->pos[2];
		if (goal->radius >= WANDER_MIN_RADIUS_M || tries > 0) {
			float r = fmaxf(goal->radius, 2.0f) * 0.6f * sqrtf(frand(a)), ang = frand(a) * 6.2832f;
			px += r * cosf(ang), pz += r * sinf(ang);
		}
		NavRef ref;
		float h, at[3];
		if (navmesh_locate_level(px, goal->pos[1], pz, FLOOR_DY_M, &ref, &h))
			at[0] = px, at[1] = h, at[2] = pz;
		else if (!navmesh_nearest_level(px, goal->pos[1], pz, NEAREST_WALKABLE_M, FLOOR_DY_M, &ref, at))
			continue;
		if (objects_fire_near(at[0], at[1], at[2], FIRE_CLEAR_M))
			continue;
		a->x = at[0], a->y = at[1], a->z = at[2];
		return;
	}
}

// Where they are when nobody is watching: at the goal (or inside)
static void settle(Actor *a, const Sched *goal, int changed) {
	if (a->goal == goal && !a->simulated && !changed)
		return;
	a->goal = goal;
	a->simulated = 0;
	a->path_n = a->path_i = 0;
	a->nav.tri = -1;
	a->need_plan = 0;
	if (goal->kind == INSIDE) {
		a->state = HIDDEN;
		memcpy(a->door, goal->pos, sizeof(a->door));
		a->has_door = 1;
		return;
	}
	a->state = IDLE;
	a->x = goal->pos[0], a->y = goal->pos[1], a->z = goal->pos[2];
	a->heading = goal->heading;
	if (goal->activity != ACT_SLEEP)
		place_on_floor(a, goal);
	a->wait = 4.0f + frand(a) * 12.0f;
	a->walk = 0.0f;
	a->asleep = goal->activity == ACT_SLEEP;
	a->bed_anim = BED_NONE;
	if (a->asleep)
		a->bed_clip = choose_bed_clip(a, goal);
	if (a->asleep)
		a->wait = 1e9f;
}

// Plans a walk to (tx, ty, tz); returns 0 if there's no way there. A spot off the navmesh
// (a bed, inside a table) is walked to by way of the nearest walkable point: never
// straight through the furniture and walls in between.
// A scene's people round what's solid on their way: Skyrim's navmesh runs under the rubble
// Alduin brings down over Helgen's streets and its actors steer round it - here each straight
// leg is tried against the collision at knee height, and one that hits gets a point beside
// what's in the way (Hadvar led you through the rubble by the wall)
#define OBSTACLE_UP_M 0.7f
static int clear_leg(const float a[3], const float b[3]) {
	const float p[3] = {a[0], a[1] + OBSTACLE_UP_M, a[2]}, q[3] = {b[0], b[1] + OBSTACLE_UP_M, b[2]};
	return collision_ray(p, q) >= 0.999f;
}

static void round_obstacles(Actor *a, const float from[3]) {
	float prev[3];
	memcpy(prev, from, sizeof(prev));
	for (int i = 0; i < a->path_n && a->path_n < MAX_PATH; i++) {
		float *p = &a->path[i * 3];
		if (!clear_leg(prev, p)) {
			const float dx = p[0] - prev[0], dz = p[2] - prev[2], len = hypotf(dx, dz);
			if (len > 0.5f) {
				const float px = -dz / len, pz = dx / len;
				const float mid[3] = {(prev[0] + p[0]) * 0.5f, (prev[1] + p[1]) * 0.5f, (prev[2] + p[2]) * 0.5f};
				static const float OFF[] = {1.5f, -1.5f, 3.0f, -3.0f, 4.5f, -4.5f, 6.0f, -6.0f};
				for (int k = 0; k < 8; k++) {
					const float c[3] = {mid[0] + px * OFF[k], mid[1], mid[2] + pz * OFF[k]};
					if (clear_leg(prev, c) && clear_leg(c, p)) {
						memmove(&a->path[(i + 1) * 3], &a->path[i * 3], (size_t)(a->path_n - i) * 3 * sizeof(float));
						memcpy(&a->path[i * 3], c, sizeof(c));
						a->path_n++, i++;
						p = &a->path[i * 3];
						break;
					}
				}
			}
		}
		memcpy(prev, p, sizeof(prev));
	}
}

static int plan_to(Actor *a, float tx, float ty, float tz) {
	NavRef to;
	float h, f[3] = {a->x, a->y, a->z}, t[3] = {tx, ty, tz};
	if (a->nav.tri < 0) {
		if (navmesh_locate_level(a->x, a->y, a->z, FLOOR_DY_M, &a->nav, &h))
			a->y = h, f[1] = h;
		else if (navmesh_nearest_level(a->x, a->y, a->z, NEAREST_WALKABLE_M, FLOOR_DY_M, &a->nav, f))
			a->x = f[0], a->y = f[1], a->z = f[2]; // (off it, e.g. getting out of bed: beside it)
		else if (navmesh_locate(a->x, a->y, a->z, &a->nav, &h))
			a->y = h, f[1] = h;
	}
	int found = navmesh_locate_level(tx, ty, tz, FLOOR_DY_M, &to, &h);
	if (found)
		t[1] = h;
	else
		found = navmesh_nearest_level(tx, ty, tz, NEAREST_WALKABLE_M, FLOOR_DY_M, &to, t);
	if (found && a->nav.tri >= 0) {
		a->path_n = navmesh_path(a->nav, f, to, t, a->path, MAX_PATH);
		if (a->scripted && !navmesh_indoors())
			round_obstacles(a, f);
	} else if (a->nav.tri < 0 && !found) {
		// No navmesh here at all: walk straight there if it's close
		float d = hypotf(tx - a->x, tz - a->z);
		a->path_n = d < 6.0f ? 1 : 0;
		memcpy(a->path, t, sizeof(t));
	} else {
		a->path_n = 0;
	}
	a->path_i = 0;
	a->state = a->path_n ? WALKING : IDLE;
	return a->path_n > 0;
}

static const Clip *walk_clip(const Actor *a) {
	const ClipSet *cs = &clip_sets[a->female];
	const Clip *run = a->scripted && a->script_run && cs->bones ? anim_clip(cs, "run") : NULL;
	return run ? run : cs->bones ? anim_clip(cs, "walk") : NULL;
}

static void walk_along(Actor *a, float dt) {
	const Clip *wc = walk_clip(a);
	float speed = wc && wc->speed > 0.1f ? wc->speed : WALK_SPEED;
	float stride = wc && wc->speed > 0.1f ? wc->speed * wc->duration : STRIDE_M;
	float step = speed * dt, moved = 0.0f;
	while (step > 1e-4f && a->path_i < a->path_n) {
		const float *p = &a->path[a->path_i * 3];
		float dx = p[0] - a->x, dz = p[2] - a->z, d = hypotf(dx, dz);
		if (d < ARRIVE_M * 0.5f || d <= step) {
			a->x = p[0], a->z = p[2];
			step -= d, moved += d;
			a->path_i++;
			continue;
		}
		a->x += dx / d * step, a->z += dz / d * step;
		moved += step;
		// Turn toward the direction of travel (forward is -sin h, -cos h)
		float want = atan2f(-dx, -dz), diff = wrap_angle(want - a->heading);
		float turn = TURN_RATE * dt;
		a->heading = wrap_angle(a->heading + (fabsf(diff) < turn ? diff : copysignf(turn, diff)));
		step = 0.0f;
	}
	float h;
	const float y_was = a->y;
	if (navmesh_track(&a->nav, a->x, a->z, &h))
		a->y = h;
	else if (navmesh_locate_level(a->x, a->y, a->z, FLOOR_DY_M, &a->nav, &h))
		a->y = h; // (lost the track, on stairs say: the floor under them again, not a float)
	else if (cur_interior < 0 && terrain_height_at(a->x, a->z, &h)) // (off the navmesh outdoors: on the ground -
		a->y += (h - a->y) * fminf(1.0f, dt * 8.0f);                //  a follower walked "straight" at its own height
		                                                             //  floated over Solitude's slope as you went down)
	else if (a->path_i < a->path_n)
		a->y += (a->path[a->path_i * 3 + 1] - a->y) * fminf(1.0f, dt * 4.0f);
	a->phase = fmodf(a->phase + moved / stride, 1.0f); // the feet keep pace with the ground
	if (a->scripted && fabsf(a->y - y_was) > 0.12f) { // (a step in the way: logged - a scripted walker dropping or climbing at once)
		static int logged;
		if (logged++ < 40)
			game_log("actors: %s's floor jumps %.2f m at %.1f %.1f %.1f", a->id, a->y - y_was, a->x, a->y, a->z);
	}
}

// Onto the walkable floor nearest a spot: near and on about its level first, then wider (a
// great hall's doors have their spot high in a tall frame: Dragonsreach's left people
// standing in the air in front of them). Returns whether it found any.
static int settle_near(Actor *a, const float from[3], float near_m, float drop_m) {
	static const float wider[2][2] = {{6.0f, 6.0f}, {10.0f, 12.0f}};
	float out[3];
	a->nav.tri = -1;
	if (navmesh_nearest_level(from[0], from[1], from[2], near_m, drop_m, &a->nav, out)) {
		a->x = out[0], a->y = out[1], a->z = out[2];
		return 1;
	}
	for (int k = 0; k < 2; k++)
		if (navmesh_nearest_level(from[0], from[1], from[2], wider[k][0], wider[k][1], &a->nav, out)) {
			a->x = out[0], a->y = out[1], a->z = out[2];
			return 1;
		}
	return 0;
}

static void simulate(Actor *a, const Sched *goal, int changed, float dt) {
	if (!a->simulated) {
		a->simulated = 1;
		a->nav.tri = -1;
		float h;
		// (on their own floor; a sleeper stays in the bed, which isn't walkable: the floor
		// under a loft bed is the room below)
		float at[3];
		if (a->state != HIDDEN && !a->asleep && navmesh_locate_level(a->x, a->y, a->z, FLOOR_DY_M, &a->nav, &h))
			a->y = h;
		else if (a->state != HIDDEN && !a->asleep && !a->bed_anim && // (off the floor - up in a doorframe: down onto it)
		         navmesh_nearest_level(a->x, a->y, a->z, 1.5f, DOOR_DROP_M, &a->nav, at))
			a->x = at[0], a->y = at[1], a->z = at[2];
		else if (a->state != HIDDEN && !a->asleep && !a->bed_anim) {
			const float here[3] = {a->x, a->y, a->z};
			settle_near(a, here, 1.5f, DOOR_DROP_M);
		}
	}
	if ((goal != a->goal && !same_spot(goal, a->goal)) || changed) {
		a->need_plan = 1;
		a->wait = 0.0f;
		if (a->asleep) // getting up (Skyrim's clip), then on to the new goal
			bed_begin(a, BED_OUT);
	}
	a->goal = goal;
	if (a->bed_anim)
		return;
	if (a->state == HIDDEN) {
		if (goal->kind == INSIDE)
			return;
		// Come out of the door they went in by - onto the ground in front of it (the door's own
		// spot is in its frame: walking off from there went through the door)
		const float *from = a->has_door ? a->door : goal->pos;
		a->x = from[0], a->y = from[1], a->z = from[2];
		settle_near(a, from, DOOR_STEP_OUT_M, DOOR_DROP_M);
		a->state = IDLE;
		a->need_plan = 1;
	}
	if (a->state == WALKING) {
		walk_along(a, dt);
		if (a->path_i >= a->path_n) {
			if (a->need_plan)
				; // the goal changed on the way: plan again below
			else if (goal->kind == INSIDE) {
				a->state = HIDDEN;
				memcpy(a->door, goal->pos, sizeof(a->door));
				a->has_door = 1;
				return;
			} else if (goal->activity == ACT_SLEEP) {
				// Beside the bed: into it (Skyrim's clip walks them onto it)
				bed_begin(a, BED_IN);
				return;
			} else {
				a->state = IDLE;
				a->wait = 6.0f + frand(a) * 14.0f;
			}
		}
		if (a->state == WALKING && !a->need_plan)
			return;
	}
	// Standing: plan to the goal, wander around it, or face the way the spot faces
	if (a->wait > 0.0f) {
		a->wait -= dt;
		if (a->state == IDLE && !a->need_plan && goal->kind == OUTDOORS && goal->radius < WANDER_MIN_RADIUS_M) {
			float diff = wrap_angle(goal->heading - a->heading);
			a->heading = wrap_angle(a->heading + diff * fminf(1.0f, dt * 2.0f));
		}
		return;
	}
	if (a->need_plan) {
		float gx = goal->pos[0], gz = goal->pos[2];
		if (goal->activity == ACT_SLEEP) { // (to where getting into bed starts, on a free side)
			float h;
			a->bed_clip = choose_bed_clip(a, goal);
			bed_start(a, goal, a->bed_clip, &gx, &gz, &h);
		}
		if (goal->kind == OUTDOORS && goal->radius >= WANDER_MIN_RADIUS_M) {
			for (int tries = 0; tries < 6; tries++) {
				float r = goal->radius * 0.6f * sqrtf(frand(a)), ang = frand(a) * 6.2832f;
				gx = goal->pos[0] + r * cosf(ang), gz = goal->pos[2] + r * sinf(ang);
				if (!objects_fire_near(gx, goal->pos[1], gz, FIRE_CLEAR_M))
					break;
			}
		}
		if (plan_to(a, gx, goal->pos[1], gz) || plan_to(a, goal->pos[0], goal->pos[1], goal->pos[2]))
			a->need_plan = 0;
		else
			a->wait = 3.0f; // no way there (yet): try again shortly
		return;
	}
	if (goal->kind == OUTDOORS && goal->radius >= WANDER_MIN_RADIUS_M) {
		// Wander to another spot within the package's radius
		for (int tries = 0; tries < 4; tries++) {
			float r = goal->radius * 0.8f * sqrtf(frand(a)), ang = frand(a) * 6.2832f;
			const float tx = goal->pos[0] + r * cosf(ang), tz = goal->pos[2] + r * sinf(ang);
			if (objects_fire_near(tx, goal->pos[1], tz, FIRE_CLEAR_M))
				continue; // (not into the hearth)
			if (plan_to(a, tx, goal->pos[1], tz))
				return;
		}
		a->wait = 5.0f;
	} else {
		a->wait = 1e9f; // stays put until the schedule changes
	}
}

// Inside a building the world is that building: those whose schedule has them in here
// are at their spot inside (and wander there), the rest are elsewhere, i.e. hidden; and
// anyone whose time in here is up walks out through the exit and disappears. Outdoors,
// those whose schedule is in another world (a walled city's streets, or Tamriel's while
// the player is in the city) are simply away: nobody walks between worlds.
enum { MODE_OUTSIDE, MODE_HERE, MODE_ELSEWHERE, MODE_AWAY };

static const Sched *effective_goal(Actor *a, const Sched *goal) {
	int mode = cur_interior >= 0 ? (goal->kind == INSIDE && goal->interior == cur_interior ? MODE_HERE : MODE_ELSEWHERE)
	           : goal->world != worlds_current() ? MODE_AWAY : MODE_OUTSIDE;
	if (a->goal_src != goal || a->goal_mode != mode) {
		if (a->goal_mode != mode || !same_spot(a->goal_src, goal))
			a->goal_changed = 1; // simulate plans to it, settle places them
		a->goal_src = goal, a->goal_mode = mode;
	}
	if (mode == MODE_OUTSIDE)
		return goal;
	memset(&a->eff, 0, sizeof(a->eff));
	a->eff.start = goal->start, a->eff.end = goal->end;
	a->eff.interior = -1;
	if (mode == MODE_AWAY) {
		a->eff.kind = INSIDE; // (hidden)
		memcpy(a->eff.pos, goal->pos, sizeof(a->eff.pos));
	} else if (mode == MODE_HERE) {
		a->eff.kind = OUTDOORS; // (free to walk about: this is the world now)
		a->eff.activity = goal->activity, a->eff.sleep_clip = goal->sleep_clip;
		memcpy(a->eff.pos, goal->inside, sizeof(a->eff.pos));
		a->eff.heading = goal->inside_heading;
		a->eff.radius = goal->inside_radius;
	} else {
		a->eff.kind = INSIDE; // out the door
		memcpy(a->eff.pos, exit_door, sizeof(a->eff.pos));
	}
	return &a->eff;
}

void actors_set_interior(int interior, const float exit[3]) {
	cur_interior = interior;
	if (exit)
		memcpy(exit_door, exit, sizeof(exit_door));
	for (int i = 0; i < actor_count; i++) {
		Actor *a = &actors[i];
		if (a->scripted) { // (a script's: along with you - placed by it in the new world)
			a->state = IDLE, a->script_walk = 0, a->nav.tri = -1, a->path_n = a->path_i = 0;
			continue;
		}
		a->goal = NULL, a->goal_src = NULL;
		a->simulated = 1; // so settle places everyone for the new world
		a->state = HIDDEN; // nobody walks between worlds
		a->has_door = cur_interior >= 0;
		memcpy(a->door, exit_door, sizeof(a->door));
		a->attend_time = 0.0f;
	}
}

// People don't walk through each other: near the player, any two up and about closer than
// SEPARATION_M ease apart (on the navmesh; a step that would leave it isn't taken). Several
// going the same way no longer walk stacked in one another, nor stand on the same spot.
static int about(const Actor *a) {
	// (a scene's people too, on their feet: Helgen's cast walked through one another - not those
	// held in a clip: sat on the cart, kneeling at the block)
	// (a scene's walker in a clip - the prisoner walked bound to the block - counts too: the
	// captain walked straight through him)
	return a->state != HIDDEN && !a->asleep && !a->bed_anim &&
	       (!a->scripted || ((!a->script_clip[0] || a->state == WALKING) && a->y > -400.0f)); // (nor those a script keeps offstage, all on one spot)
}

static void nudge(Actor *a, float dx, float dz) {
	if (a->nav.tri < 0) {
		a->x += dx, a->z += dz;
		return;
	}
	NavRef ref = a->nav;
	float h;
	if (navmesh_track(&ref, a->x + dx, a->z + dz, &h) && fabsf(h - a->y) < 0.5f)
		a->x += dx, a->z += dz, a->y = h, a->nav = ref;
}

#define BODY_RADIUS_M 0.32f // (a person's shoulders: you and they don't pass through each other)

int actors_block_player(float *px, float *pz, float py, float radius) {
	int hit = 0;
	for (int i = 0; i < actor_count; i++) {
		Actor *a = &actors[i];
		if (!about(a) || fabsf(a->y - py) > 1.6f)
			continue;
		const float dx = *px - a->x, dz = *pz - a->z, d = hypotf(dx, dz), min = radius + BODY_RADIUS_M;
		if (d >= min)
			continue;
		const float ux = d > 1e-3f ? dx / d : 1.0f, uz = d > 1e-3f ? dz / d : 0.0f, over = min - d;
		// (someone at a scene's mark, or seated: they hold it, you give way; else you both do)
		const int holds = a->scripted && a->state != WALKING;
		const float yours = holds ? 1.0f : 0.6f;
		*px += ux * over * yours, *pz += uz * over * yours;
		if (!holds)
			nudge(a, -ux * over * (1.0f - yours), -uz * over * (1.0f - yours));
		hit = 1;
	}
	return hit;
}

static float sim_radius = SIM_RADIUS_M;
static int think_every = 1, pose_max = POSE_MAX_PER_FRAME;
static int pose_nearest = POSE_NEAREST;            // (in a city: fewer, actors_set_city)
static float pose_half_m = POSE_HALF_RATE_M, pose_full_m = POSE_EVERY_FRAME_M;

void actors_set_city(int city) {
	pose_nearest = city ? 2 : POSE_NEAREST;
	pose_full_m = city ? 6.0f : POSE_EVERY_FRAME_M;
	pose_half_m = city ? 12.0f : POSE_HALF_RATE_M;
}
static unsigned think_tick;

static void separate(float dt_s, float player_x, float player_z) {
	if (dt_s <= 0.0f)
		return;
	for (int i = 0; i < actor_count; i++) {
		Actor *a = &actors[i];
		if (!about(a) || hypotf(a->x - player_x, a->z - player_z) > sim_radius)
			continue;
		for (int j = i + 1; j < actor_count; j++) {
			Actor *b = &actors[j];
			if (!about(b) || fabsf(a->y - b->y) > 1.5f)
				continue;
			float dx = b->x - a->x, dz = b->z - a->z, d = hypotf(dx, dz);
			const int scene = a->scripted && b->scripted;
			const float keep = scene ? SCENE_SEPARATION_M : SEPARATION_M;
			if (d >= keep)
				continue;
			if (d < 1e-3f) // (exactly on top of each other: apart by who they are)
				dx = cosf((float)i), dz = sinf((float)i), d = 1.0f;
			const float push = fminf(keep - d, (scene ? SCENE_SEPARATION_RATE : SEPARATION_RATE) * dt_s) * 0.5f;
			// (talking or idle: the one walking does the moving)
			float wa = a->state == WALKING || b->state != WALKING ? 1.0f : 0.2f;
			float wb = b->state == WALKING || a->state != WALKING ? 1.0f : 0.2f;
			if (a->scripted && a->state != WALKING) // (a scene's people standing at their marks: not shoved off them -
				wa = 0.0f, wb = wb > 0.0f ? 1.0f : 0.0f; //  they shuffled as if walking; the walker goes round)
			if (b->scripted && b->state != WALKING)
				wb = 0.0f, wa = wa > 0.0f ? 1.0f : 0.0f;
			nudge(a, -dx / d * push * wa, -dz / d * push * wa);
			nudge(b, dx / d * push * wb, dz / d * push * wb);
		}
		if (a->scripted && a->state == WALKING) { // (a scene's walker and you: it goes round - the captain ran into you twice)
			float dx = a->x - player_x, dz = a->z - player_z, d = hypotf(dx, dz);
			if (d < SCENE_SEPARATION_M && d > 1e-3f)
				nudge(a, dx / d * (SCENE_SEPARATION_M - d) * fminf(1.0f, SCENE_SEPARATION_RATE * dt_s),
				      dz / d * (SCENE_SEPARATION_M - d) * fminf(1.0f, SCENE_SEPARATION_RATE * dt_s));
		}
	}
}

void actors_set_detail(float sim_m, int far_think_every, int poses_per_frame) {
	sim_radius = sim_m < 30.0f ? 30.0f : sim_m > SIM_RADIUS_M ? SIM_RADIUS_M : sim_m;
	think_every = far_think_every < 1 ? 1 : far_think_every > 8 ? 8 : far_think_every;
	pose_max = poses_per_frame < 1 ? 1 : poses_per_frame;
}

// --- Directed by a script (intro.c) -----------------------------------------------------------

#define FOLLOW_NEAR_M 2.2f // a follower stops this near you...
#define FOLLOW_FAR_M 3.6f  // ...and sets off again past this
#define FOLLOW_REPATH_S 0.8f

static void script_step(Actor *a, float dt, float px, float pz) {
	a->t += dt, a->script_t += dt;
	if (a->script_once && a->script_next[0] && a->script_clip[0]) { // (the once clip done: on to the next, looping)
		const Clip *own = anim_clip(&clip_sets[a->female], a->script_clip);
		if (!own || a->script_t >= own->duration) {
			snprintf(a->script_clip, sizeof(a->script_clip), "%s", a->script_next);
			a->script_next[0] = 0, a->script_once = 0, a->script_t = 0.0f;
		}
	}
	if (a->script_follow) {
		const float d = hypotf(a->x - px, a->z - pz);
		a->script_repath -= dt;
		if (d > FOLLOW_FAR_M && (a->state != WALKING || a->script_repath <= 0.0f)) {
			a->script_to[0] = px, a->script_to[1] = a->y, a->script_to[2] = pz;
			a->script_walk = 1, a->state = IDLE, a->script_repath = FOLLOW_REPATH_S;
			a->script_run = d > 8.0f;
		} else if (d < FOLLOW_NEAR_M && a->state == WALKING) {
			a->state = IDLE, a->script_walk = 0;
		}
	}
	if (a->script_walk && a->state != WALKING) {
		a->nav.tri = -1;
		a->path_n = a->path_i = 0;
		if (!plan_to(a, a->script_to[0], a->script_to[1], a->script_to[2]) || a->path_n <= 0) {
			a->path[0] = a->script_to[0], a->path[1] = a->script_to[1], a->path[2] = a->script_to[2];
			a->path_n = 1; // (no navmesh between: straight there)
			game_log("actors: %s has no way to (%.0f %.0f %.0f) from (%.0f %.0f %.0f): straight", a->name, a->script_to[0],
			         a->script_to[1], a->script_to[2], a->x, a->y, a->z);
		}
		a->state = WALKING, a->script_stuck = 0.0f;
	}
	if (a->state == WALKING) {
		const float x0 = a->x, z0 = a->z;
		walk_along(a, dt);
		if (a->path_i >= a->path_n)
			a->state = IDLE, a->script_walk = 0;
		// (held up - someone in the way, the others easing it back: there, as near as it gets.
		// Tullius stood mid-stride through his lines, walking at a mark he couldn't reach)
		// (by its progress toward the mark, not how much it moved: shoved to and fro in a crowd it
		// moved plenty, getting nowhere - the headsman and soldiers stood mid-stride at the block)
		// - toward the path's next point, so a way round a building, further from the mark for a
		// while, isn't taken for stuck)
		(void)x0, (void)z0;
		const int wp = a->path_i < a->path_n ? a->path_i : a->path_n - 1;
		const float left = wp >= 0 ? hypotf(a->path[wp * 3] - a->x, a->path[wp * 3 + 2] - a->z) : 0.0f;
		if (a->script_stuck <= 0.0f || wp != a->script_wp || left < a->script_best - 0.3f)
			a->script_best = left, a->script_stuck = dt, a->script_wp = wp;
		else
			a->script_stuck += dt;
		if (a->script_stuck > 2.0f && !a->script_follow)
			a->state = IDLE, a->script_walk = 0, a->script_stuck = 0.0f;
	}
	const float target = a->state == WALKING ? 1.0f : 0.0f;
	a->walk += (target - a->walk) * fminf(1.0f, dt * 5.0f);
	a->look += (a->script_look - a->look) * fminf(1.0f, dt * 3.0f);
	a->mouth += (a->mouth_target - a->mouth) * fminf(1.0f, dt * MOUTH_RATE);
	a->mouth_target = 0.0f;
}

void actors_script_look_at(int i, const float *at) {
	if (i < 0 || i >= actor_count || !actors[i].scripted)
		return;
	Actor *a = &actors[i];
	if (!at) {
		a->script_look = 0.0f;
		return;
	}
	const float want = wrap_angle(atan2f(-(at[0] - a->x), -(at[2] - a->z)) - a->heading);
	a->script_look = fmaxf(-LOOK_MAX_RAD, fminf(LOOK_MAX_RAD, want)); // (head and neck turn so far: Lokir, beside Ralof on
	// the cart's bench, is 90 degrees round - at 44 the head never got there)
}

void actors_script(int i, int on) {
	if (i < 0 || i >= actor_count)
		return;
	Actor *a = &actors[i];
	if (on && !a->scripted) {
		a->scripted = 1, a->script_walk = a->script_follow = a->script_run = 0, a->script_clip[0] = 0, a->corpse = 0;
		a->simulated = 1; // (where it is is where it's been put)
		a->asleep = 0, a->bed_anim = 0, a->attend_time = 0.0f;
		if (a->state == HIDDEN || a->state == WALKING)
			a->state = IDLE;
	} else if (!on && a->scripted) {
		a->scripted = 0, a->goal = NULL; // (back to its schedule, from where it is)
		a->state = IDLE;
	}
}

void actors_script_release(int i) {
	if (i < 0 || i >= actor_count)
		return;
	Actor *a = &actors[i];
	a->scripted = 0, a->script_walk = a->script_follow = 0, a->goal = NULL, a->goal_src = NULL;
	a->simulated = 1, a->state = HIDDEN; // (settled again where their day has them: home)
	a->nav.tri = -1, a->path_n = a->path_i = 0;
}

void actors_script_place(int i, const float pos[3], float heading) {
	if (i < 0 || i >= actor_count || !actors[i].scripted)
		return;
	Actor *a = &actors[i];
	a->x = pos[0], a->y = pos[1], a->z = pos[2], a->heading = heading;
	a->state = IDLE, a->script_walk = 0, a->nav.tri = -1, a->posed = 0;
}

void actors_script_clip(int i, const char *clip, int once) {
	if (i < 0 || i >= actor_count)
		return;
	Actor *a = &actors[i];
	snprintf(a->script_clip, sizeof(a->script_clip), "%s", clip ? clip : "");
	a->script_once = once, a->script_t = 0.0f, a->script_next[0] = 0;
}

void actors_script_clip_from(int i, const char *clip, int once, float t0) { // (begun t0 seconds in: the fall of a death without its stagger up)
	actors_script_clip(i, clip, once);
	if (i >= 0 && i < actor_count)
		actors[i].script_t = t0;
}

void actors_script_clip_next(int i, const char *clip) {
	if (i >= 0 && i < actor_count)
		snprintf(actors[i].script_next, sizeof(actors[i].script_next), "%s", clip ? clip : "");
}

void actors_script_prop(int i, int on) {
	if (i >= 0 && i < actor_count)
		actors[i].show_prop = on;
}

static int player_ghost;          // (Become Ethereal: the player drawn pale)
static int player_flesh;          // (Oakflesh .. Ebonyflesh: its shell over you - 1 oak .. 4 ebony)
static float flesh_fade;          // (... coming on and wearing off: 0..1)
static float attack_speed = 1.0f; // (Elemental Fury: swings this much quicker)
static int player_bound; // (Skyrim's opening: the arms in its bound pose)

void actors_player_bound(int on) {
	player_bound = on;
}

void actors_script_corpse(int i, int on) {
	if (i >= 0 && i < actor_count)
		actors[i].corpse = on;
}

int actors_corpse(int i) {
	return i >= 0 && i < actor_count && actors[i].corpse;
}

// Their clothes and armour taken: the body as it is under them (Skyrim's underwear)
static void strip_gear(Actor *a) {
	for (int k = 0; k < a->nitems; k++)
		if (!a->items[k].hidden_by && !a->items[k].part)
			a->items[k].equipped = 0;
}

void actors_script_strip(int i) {
	if (i >= 0 && i < actor_count)
		strip_gear(&actors[i]);
}

void actors_script_headless(int i, int on) {
	if (i >= 0 && i < actor_count)
		actors[i].headless = on;
}

void actors_script_walk(int i, const float to[3], int run) {
	if (i < 0 || i >= actor_count || !actors[i].scripted)
		return;
	Actor *a = &actors[i];
	memcpy(a->script_to, to, sizeof(a->script_to));
	a->script_walk = 1, a->script_run = run, a->script_follow = 0, a->state = IDLE, a->script_clip[0] = 0;
}

int actors_script_has_clip(int i) {
	return i >= 0 && i < actor_count && actors[i].scripted && actors[i].script_clip[0];
}

int actors_scripted(int i) {
	return i >= 0 && i < actor_count && actors[i].scripted;
}

int actors_script_walking(int i) {
	return i >= 0 && i < actor_count && actors[i].scripted && (actors[i].script_walk || actors[i].state == WALKING);
}

void actors_script_follow(int i, int on) {
	if (i < 0 || i >= actor_count || !actors[i].scripted)
		return;
	actors[i].script_follow = on, actors[i].script_clip[0] = 0;
}

void actors_script_face(int i, float heading) {
	if (i >= 0 && i < actor_count && actors[i].scripted)
		actors[i].heading = heading;
}

#define POSE_LONGEST_WAIT 8u // (frames: anyone in view posed at least this often)
static unsigned draw_frame; // (frames drawn: posing's turns)
static int watched[4] = {-1, -1, -1, -1}; // (testing: these logged every 2 s - state, walk, clip)

void actors_watch(int i) {
	for (int k = 0; k < 4; k++)
		if (watched[k] < 0 || watched[k] == i) {
			watched[k] = i;
			return;
		}
}

void actors_update(float dt_s, float hours, float player_x, float player_z) {
	float minute = hours * 60.0f;
	static float watch_t;
	if ((watch_t += dt_s) > 2.0f) {
		watch_t = 0.0f;
		for (int k = 0; k < 4; k++)
			if (watched[k] >= 0) {
				const Actor *a = &actors[watched[k]];
				game_log("watch: %s state %d walk %.2f phase %.2f clip '%s' t %.1f scripted %d swalk %d posed %u ago at %.1f %.1f %.1f",
				         a->id, a->state, a->walk, a->phase, a->script_clip, a->script_t, a->scripted, a->script_walk,
				         draw_frame - a->pose_frame, a->x, a->y, a->z);
			}
	}
	for (int i = 0; i < actor_count; i++) {
		Actor *a = &actors[i];
		if (a->scripted) {
			script_step(a, dt_s, player_x, player_z);
			continue;
		}
		const Sched *goal = effective_goal(a, current_goal(a, minute));
		int changed = a->goal_changed;
		a->goal_changed = 0;
		if (!a->goal) {
			settle(a, goal, 1); // first frame, or a new world: start where the schedule has them
			changed = 0;
		}
		a->t += dt_s;
		float px = a->state == HIDDEN && a->has_door ? a->door[0] : a->x;
		float pz = a->state == HIDDEN && a->has_door ? a->door[2] : a->z;
		if (a->state == HIDDEN && goal->kind == OUTDOORS && !a->has_door)
			px = goal->pos[0], pz = goal->pos[2];
		const float away = hypotf(px - player_x, pz - player_z);
		int near = away < sim_radius && navmesh_ready(px, pz);
		if (!near || a->goal_mode == MODE_AWAY) {
			settle(a, goal, changed);
			continue;
		}
		// Far off (the performance setting): thinking every few frames, taking turns, with
		// the time since it last did
		if (think_every > 1 && away > FAR_THINK_M && a->state != HIDDEN && !a->bed_anim &&
		    (think_tick + (unsigned)i) % (unsigned)think_every) {
			a->skip_dt += dt_s;
			a->goal_changed |= changed;
			continue;
		}
		dt_s += a->skip_dt; // (this one's own step: restored below)
		const float frame_dt = dt_s - a->skip_dt;
		a->skip_dt = 0.0f;
		if (a->bed_anim) {
			// Getting into or out of bed: the clip moves them (and then the goal's changes
			// are acted on)
			if (a->attend_time > 0.0f)
				a->attend_time -= dt_s;
			a->goal_changed |= changed;
			bed_step(a, dt_s);
		} else if (a->attend_time > 0.0f && a->state != HIDDEN) {
			// Talking: stand and face the listener (the walk resumes afterwards)
			a->attend_time -= dt_s;
			a->look_target = 0.0f;
			a->goal_changed |= changed; // acted on once the conversation is over
			float want = atan2f(-(a->attend_x - a->x), -(a->attend_z - a->z));
			float diff = wrap_angle(want - a->heading), turn = TURN_RATE * 0.6f * dt_s;
			a->heading = wrap_angle(a->heading + (fabsf(diff) < turn ? diff : copysignf(turn, diff)));
		} else {
			simulate(a, goal, changed, dt_s);
		}
		float target = a->state == WALKING && a->attend_time <= 0.0f ? 1.0f : 0.0f;
		a->walk += (target - a->walk) * fminf(1.0f, dt_s * 5.0f);
		// Idle glances: a new head direction every few seconds
		a->look_timer -= dt_s;
		if (a->look_timer <= 0.0f) {
			a->look_timer = 2.5f + frand(a) * 5.0f;
			a->look_target = (frand(a) - 0.5f) * 0.63f; // (glances as before: the head now turns the whole of look)
		}
		if (a->asleep)
			a->look_target = 0.0f;
		a->look += (a->look_target * (1.0f - a->walk) - a->look) * fminf(1.0f, dt_s * 2.0f);
		a->mouth += (a->mouth_target - a->mouth) * fminf(1.0f, dt_s * MOUTH_RATE);
		a->mouth_target = 0.0f; // closes unless talk.c says otherwise next frame
		dt_s = frame_dt;
	}
	think_tick++;
	separate(dt_s, player_x, player_z);
}

// --- Animation and skinning --------------------------------------------------------------

typedef struct {
	float r[9]; // row-major
	float t[3];
} Xform;

static void rot_xyz(float rx, float ry, float rz, float out[9]) {
	// R = Ry * Rx * Rz: settle the arms (z), swing (x), then turn (y)
	float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
	float rxz[9] = {cz, -sz, 0, cx * sz, cx * cz, -sx, sx * sz, sx * cz, cx};
	float ryv[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out[i * 3 + j] = ryv[i * 3] * rxz[j] + ryv[i * 3 + 1] * rxz[3 + j] + ryv[i * 3 + 2] * rxz[6 + j];
}

static void mul_rr(const float a[9], const float b[9], float out[9]) {
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}

static void mul_rv(const float r[9], const float v[3], float out[3]) {
	for (int i = 0; i < 3; i++)
		out[i] = r[i * 3] * v[0] + r[i * 3 + 1] * v[1] + r[i * 3 + 2] * v[2];
}

// Per-bone rotations (model space, about the bone's joint) for this frame's pose
static void pose(const Actor *a, float rot[][3], float com[3]) {
	memset(rot, 0, sizeof(float) * 3 * a->nbones);
	com[0] = com[1] = com[2] = 0.0f;
	if (a->nbones < B_JAW)
		return; // not the skeleton this animates
	if (a->nbones > B_JAW && a->parent[a->nbones - 1] == B_HEAD)
		rot[a->nbones - 1][0] = -JAW_OPEN_RAD * a->mouth; // hinged near the ears: the chin drops
	const float w = a->walk, s = sinf(a->phase * 6.2832f), c = cosf(a->phase * 6.2832f);
	// Arms: from the bind pose's slight A toward the sides
	for (int side = 0; side < 2; side++) {
		int ua = side ? B_RUARM : B_LUARM, fa = side ? B_RFARM : B_LFARM;
		float dx = a->joint[fa][0] - a->joint[ua][0], dy = a->joint[fa][1] - a->joint[ua][1];
		rot[ua][2] = -0.55f * atan2f(dx, -dy);
	}
	// Walk cycle: legs swing in opposition, the swinging leg's knee bends, arms counter-swing
	rot[B_LTHIGH][0] = 0.42f * s * w;
	rot[B_RTHIGH][0] = -0.42f * s * w;
	rot[B_LCALF][0] = -(0.08f + 0.6f * fmaxf(0.0f, c)) * w;
	rot[B_RCALF][0] = -(0.08f + 0.6f * fmaxf(0.0f, -c)) * w;
	rot[B_LFOOT][0] = 0.15f * fmaxf(0.0f, c) * w;
	rot[B_RFOOT][0] = 0.15f * fmaxf(0.0f, -c) * w;
	rot[B_LUARM][0] = -0.28f * s * w;
	rot[B_RUARM][0] = 0.28f * s * w;
	rot[B_LFARM][0] = 0.12f + 0.12f * w;
	rot[B_RFARM][0] = 0.12f + 0.12f * w;
	rot[B_SPINE1][1] = 0.07f * s * w;  // shoulders counter-rotate the hips
	rot[B_SPINE][0] = -0.05f * w;      // a slight forward lean
	// The hips drop as the legs spread, keeping the planted foot on the ground
	float leg = a->joint[B_LTHIGH][1] - a->joint[B_LFOOT][1];
	com[1] = -leg * (1.0f - cosf(0.42f * s)) * w;
	// Idle: breathing, a slow weight shift, glancing around
	float idle = 1.0f - w;
	rot[B_SPINE2][0] = 0.02f * sinf(a->t * 1.5f);
	rot[B_COM][2] = 0.012f * sinf(a->t * 0.45f) * idle;
	rot[B_HEAD][1] = a->look * 0.6f;
	rot[B_NECK][1] = a->look * 0.4f;
}

static ObjectsDynamic drawn[MAX_DRAWN_BATCHES];
static int batch_shown(const Actor *a, const ABatch *b);

// --- The character creator's look (chargen.c) ----------------------------------------------
// The player's skin tone and hair colour, as Skyrim's skin and hair shaders multiply them (x2 over
// the white the converter left those batches: npcs.py ROLE_SKIN, ROLE_HAIR); the face's texture,
// made from the tint layers; the morphs' weights by name (the face's sliders, "weight")
#define LOOK_MORPH_NAMES 256
static char look_morph_names[LOOK_MORPH_NAMES][24];
static float look_morph_w[LOOK_MORPH_NAMES];
static int look_morph_count;
static float look_skin[3] = {1.0f, 1.0f, 1.0f}, look_hair[3] = {1.0f, 1.0f, 1.0f};
static int look_on; // (a look set: its roles coloured - not a ready face's, an older save's)

static int look_morph_id(const char *name) {
	for (int i = 0; i < look_morph_count; i++)
		if (!strncmp(look_morph_names[i], name, 23))
			return i;
	if (look_morph_count >= LOOK_MORPH_NAMES)
		return -1;
	snprintf(look_morph_names[look_morph_count], 24, "%s", name);
	look_morph_w[look_morph_count] = 0.0f;
	return look_morph_count++;
}

// The colour the look puts on a batch of the player's (NULL: none)
static const float *look_tint(const Actor *a, const ABatch *bt) {
	if (!look_on || (a != &player && a != &player1st))
		return NULL;
	return bt->role == ROLE_HAIR ? look_hair : (bt->role == ROLE_SKIN || bt->toned) ? look_skin : NULL;
}

// A model's .skmorph (beside it): its roles and morphs onto batches first .. first+count-1 of
// a, from the file's batch list `list` (a gear file's: 0 third person, 1 first)
static void load_look(Actor *a, const char *model_path, int list, int first, int count) {
	char path[256];
	snprintf(path, sizeof(path), "%s", model_path);
	char *dot = strrchr(path, '.');
	if (!dot || (size_t)(dot - path) + 9 > sizeof(path))
		return;
	strcpy(dot, ".skmorph");
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	if (!d)
		return;
	Cur c = {d, d + size, 1};
	char magic[4];
	uint16_t ver = 0, nroles = 0;
	take(&c, magic, 4), take(&c, &ver, 2), take(&c, &nroles, 2);
	if (!c.ok || memcmp(magic, "SKMO", 4) || ver != 1) {
		free(d);
		return;
	}
	for (int i = 0; i < nroles && c.ok; i++) {
		uint8_t r[4];
		take(&c, r, 4);
		if (c.ok && r[0] == list && r[1] < count)
			a->batches[first + r[1]].role = r[2];
	}
	uint32_t nnames = 0;
	take(&c, &nnames, 4);
	int ids[LOOK_MORPH_NAMES];
	for (uint32_t i = 0; i < nnames && c.ok; i++) {
		char name[24];
		take(&c, name, 24);
		name[23] = 0;
		if (i < LOOK_MORPH_NAMES)
			ids[i] = look_morph_id(name);
	}
	uint32_t nrec = 0;
	take(&c, &nrec, 4);
	if (!c.ok || nnames > LOOK_MORPH_NAMES || c.p + (size_t)nrec * 12 > c.end) {
		free(d);
		return;
	}
	// (sorted by list, batch, name, vertex: each batch's run in one piece)
	const uint8_t *rec = c.p;
	for (uint32_t i = 0; i < nrec;) {
		const int l = rec[i * 12], b = rec[i * 12 + 1];
		uint32_t j = i;
		while (j < nrec && rec[j * 12] == l && rec[j * 12 + 1] == b)
			j++;
		if (l == list && b < count) {
			ABatch *bt = &a->batches[first + b];
			LookMorph *m = malloc((j - i) * sizeof(LookMorph));
			float *base = bt->base_pos ? bt->base_pos : malloc(bt->vcount * 3 * sizeof(float));
			if (m && base && !bt->morph) {
				int n = 0;
				for (uint32_t k = i; k < j; k++) {
					const uint8_t *r = rec + k * 12;
					uint16_t name, v;
					memcpy(&name, r + 2, 2), memcpy(&v, r + 4, 2);
					if (name >= nnames || v >= bt->vcount || ids[name] < 0)
						continue;
					m[n].name = (uint16_t)ids[name], m[n].v = v;
					memcpy(m[n].d, r + 6, 6);
					n++;
				}
				if (!bt->base_pos)
					for (int v = 0; v < bt->vcount; v++)
						memcpy(&base[v * 3], bt->src[v].pos, 3 * sizeof(float));
				bt->base_pos = base, bt->morph = m, bt->nmorph = n;
			} else {
				free(m);
				if (!bt->base_pos)
					free(base);
			}
		}
		i = j;
	}
	free(d);
}

// The morphs' weights on a's vertices (from those as loaded): batches first .. first+count-1
static void apply_look_range(Actor *a, int first, int count) {
	for (int i = first; i < first + count && i < a->nbatches; i++) {
		ABatch *bt = &a->batches[i];
		if (!bt->base_pos)
			continue;
		for (int v = 0; v < bt->vcount; v++)
			memcpy(bt->src[v].pos, &bt->base_pos[v * 3], 3 * sizeof(float));
		for (int k = 0; k < bt->nmorph; k++) {
			const LookMorph *m = &bt->morph[k];
			const float w = look_morph_w[m->name] * MOUTH_MORPH_UNIT;
			if (w != 0.0f)
				for (int j = 0; j < 3; j++)
					bt->src[m->v].pos[j] += w * m->d[j];
		}
		free_gpu_buffers(bt); // (its vertices to the GPU again)
		bt->lit = 0;
	}
	a->lit_valid = 0;
}

static void apply_look(Actor *a) {
	apply_look_range(a, 0, a->nbatches);
}

void actors_player_look_morph(const char *name, float w) {
	const int id = look_morph_id(name);
	if (id >= 0)
		look_morph_w[id] = w;
}

void actors_player_look_morphs_clear(void) {
	for (int i = 0; i < look_morph_count; i++)
		look_morph_w[i] = 0.0f;
}

void actors_player_look_apply(void) {
	apply_look(&player);
	apply_look(&player1st);
}

void actors_player_look_colors(const float skin[3], const float hair[3]) {
	look_on = skin != NULL;
	{ // (what it lands on: logged)
		int roles[4] = {0}, toned = 0;
		for (int i = 0; i < player.nbatches; i++)
			roles[player.batches[i].role & 3]++, toned += player.batches[i].toned;
		game_log("look: skin %.2f %.2f %.2f, hair %.2f %.2f %.2f - face %d, skin %d, hair %d batches, %d toned (of %d)",
		         skin ? skin[0] : 0, skin ? skin[1] : 0, skin ? skin[2] : 0, hair ? hair[0] : 0, hair ? hair[1] : 0, hair ? hair[2] : 0,
		         roles[1], roles[2], roles[3], toned, player.nbatches);
	}
	for (int k = 0; k < 3; k++)
		look_skin[k] = skin ? skin[k] : 1.0f, look_hair[k] = hair ? hair[k] : 1.0f;
	for (int k = 0; k < 2; k++) {
		Actor *a = k ? &player1st : &player;
		for (int i = 0; i < a->nbatches; i++)
			a->batches[i].lit = 0; // (the CPU-posed: lit again with it)
		a->lit_valid = 0;
	}
}

int actors_player_look_face(unsigned tex) {
	int n = 0;
	for (int i = 0; i < player.nbatches; i++)
		if (player.batches[i].role == ROLE_FACE)
			player.batches[i].tex = tex, n++;
	return n;
}

int actors_player_look_roles(void) {
	int n = 0;
	for (int i = 0; i < player.nbatches; i++)
		n += player.batches[i].role != 0;
	return n;
}

static int skin_frame; // counts actors_draw calls (when the torch flame was last placed)
static int gpu_skinning(void);
static int gpu_batch(const ABatch *bt);

static void emit(Actor *a, int *count) {
	for (int i = 0; i < a->nbatches && *count < MAX_DRAWN_BATCHES; i++) {
		const ABatch *bt = &a->batches[i];
		if (!batch_shown(a, bt))
			continue;
		ObjectsDynamic *d = &drawn[(*count)++];
		d->vertices = bt->out;
		d->vertex_count = bt->vcount;
		d->indices = bt->idx;
		d->index_count = bt->icount;
		d->tex = a->ghost ? sktex_white() : bt->tex; // (a spirit - Skyrim's GhostFX: its form white, shaded, no fur: the
		                                             //  tint lit the fur's own brown - a plain wolf, see-through)
		d->alpha = bt->alpha;
		d->is_hair = (uint8_t)bt->is_hair;
		if (!d->is_hair) {
			const int mine = (a == &player || a == &player1st) && bt->part > 0 && bt->part <= a->nitems;
			if (mine && (a->items[bt->part - 1].hidden_by & (SLOT_HAIR | SLOT_HEAD)))
				d->is_hair = 1;
			else if (mine && (!strcmp(a->items[bt->part - 1].name, "Hair") || !strcmp(a->items[bt->part - 1].name, "Beard")))
				d->is_hair = 1;
		}
		if (d->is_hair && !d->alpha)
			d->alpha = 1;
		d->fade = a->fade > 0.0f ? a->fade : a->ghost ? 0.3f : player_ghost && (a == &player || a == &player1st) ? 0.45f // (a spirit; Become Ethereal)
		          : a->far || a->script_far ? -1.0f : 0.0f;
		d->pos[0] = a->x, d->pos[1] = a->y + 1.0f, d->pos[2] = a->z, d->pos[3] = a->quant_m > 0.0f ? a->quant_m : QUANT_EXTENT_M;
		memcpy(d->uv, bt->uv, sizeof(d->uv));
		d->skin_src = NULL, d->bones = NULL;
		if (gpu_skinning() && gpu_batch(bt)) { // (posed on the GPU: its own vertices, the pose, its tint)
			d->skin_src = bt->src, d->bones = a->palette;
			if (!bt->vbo) { // (its vertices on the GPU once: copying them every frame cost more than posing)
				ABatch *wb = (ABatch *)bt;
				glGenBuffers(1, &wb->vbo);
				glBindBuffer(GL_ARRAY_BUFFER, wb->vbo);
				glBufferData(GL_ARRAY_BUFFER, bt->vcount * sizeof(SrcVertex), bt->src, GL_STATIC_DRAW);
				glGenBuffers(1, &wb->ibo);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wb->ibo);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, bt->icount * sizeof(uint16_t), bt->idx, GL_STATIC_DRAW);
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			}
			d->skin_vbo = bt->vbo, d->skin_ibo = bt->ibo;
			a->drawn_frame = skin_frame;
			d->pos[1] = a->y;
			d->tint_mul[0] = d->tint_mul[1] = d->tint_mul[2] = 1.0f;
			d->tint_add[0] = d->tint_add[1] = d->tint_add[2] = 0.0f;
			const int mine = (a == &player || a == &player1st) && bt->part > 0 && bt->part <= a->nitems;
			if (mine && a->items[bt->part - 1].tint) // (a poison on the blade: tinged green)
				d->tint_mul[0] = 0.55f, d->tint_mul[1] = 1.15f, d->tint_mul[2] = 0.5f, d->tint_add[1] = 18.0f / 255.0f;
			const unsigned glow = mine && bt->gear_tag != GEAR_SHEATHED && bt->gear_tag != GEAR_SCABBARD
			                      ? a->items[bt->part - 1].glow : 0;
			if (player_flesh && (a == &player || a == &player1st)) { // (Oakflesh's bark, Stoneflesh's stone...: a shell
				// over you, its grain catching the light as it pulses - Skyrim's translucent armour, not nothing)
				static const float TINT[5][3] = {{0, 0, 0}, {0.30f, 0.18f, 0.06f}, {0.22f, 0.22f, 0.20f}, {0.26f, 0.27f, 0.30f},
				                                {0.12f, 0.06f, 0.16f}};
				const float pulse = (0.75f + 0.25f * sinf(skin_frame * 0.08f)) * flesh_fade;
				for (int k = 0; k < 3; k++)
					d->tint_mul[k] = 1.0f - 0.35f * flesh_fade, d->tint_add[k] = TINT[player_flesh][k] * pulse;
			}
			if (player_ghost && (a == &player || a == &player1st)) // (Become Ethereal: pale, a spirit's blue-white)
				d->tint_mul[0] = 0.55f, d->tint_mul[1] = 0.7f, d->tint_mul[2] = 0.9f, d->tint_add[0] = 0.12f,
				d->tint_add[1] = 0.16f, d->tint_add[2] = 0.24f;
			else if (a->ghost) // (a familiar: Skyrim's GhostFXShader - white (its texture: sktex_white), a touch of
				// the shader's blue, its shape still lit)
				d->tint_mul[0] = 0.6f, d->tint_mul[1] = 0.65f, d->tint_mul[2] = 0.7f, d->tint_add[0] = 0.22f,
				d->tint_add[1] = 0.26f, d->tint_add[2] = 0.34f;
			if (glow) { // (an enchantment: its colour added, pulsing)
				const float pulse = (0.35f + 0.25f * sinf(skin_frame * 0.12f) + 0.1f * sinf(skin_frame * 0.31f)) / 255.0f;
				d->tint_add[0] = (glow >> 16 & 255) * pulse, d->tint_add[1] = (glow >> 8 & 255) * pulse,
				d->tint_add[2] = (glow & 255) * pulse;
			}
			const float *look = look_tint(a, bt); // (your skin tone, your hair colour: the character creator's)
			if (look && a->ghost == 0 && !player_ghost)
				for (int k = 0; k < 3; k++)
					d->tint_mul[k] *= look[k];
		}
	}
}

// x = x after a rotation about the bind-pose point j (the rotation applied first)
static void rotate_about(Xform *x, const float j[3], const float r[9]) {
	float rj[3], d[3], xr[9], add[3];
	mul_rv(r, j, rj);
	for (int k = 0; k < 3; k++)
		d[k] = j[k] - rj[k];
	mul_rr(x->r, r, xr);
	mul_rv(x->r, d, add);
	memcpy(x->r, xr, sizeof(xr));
	for (int k = 0; k < 3; k++)
		x->t[k] += add[k];
}

// Whether a batch is shown: skin unless an equipped item covers its body slots; items
// when equipped
static int batch_shown(const Actor *a, const ABatch *b) {
	if (b->part == 0)
		return 1;
	if (b->part == PROP_PART) // (a scene's thing in hand - Hadvar's list: while its script says)
		return a->show_prop;
	uint32_t covered = 0;
	for (int i = 0; i < a->nitems; i++)
		if (a->items[i].equipped)
			covered |= a->items[i].slots;
	if (b->part & PART_SKIN)
		return !(covered & (uint32_t)(b->part & ~PART_SKIN));
	int item = b->part - 1;
	if (item >= a->nitems || !a->items[item].equipped)
		return 0;
	if (a->items[item].hidden_by & covered) // (hair under a helmet)
		return 0;
	if (b->gear_tag)
		return weapon_drawn ? b->gear_tag != GEAR_SHEATHED : b->gear_tag == GEAR_SHEATHED;
	return 1;
}

// Skyrim's clips: the bones' model-space changes from the bind pose, then the head's
// glance and the jaw on top. 0 if there are no clips for this skeleton.
static int clip_bones(const Actor *a, Xform bone[]) {
	const ClipSet *cs = &clip_sets[a->female];
	if (!cs->bones || a->nbones < cs->bones)
		return 0;
	const int jaw = a->nbones > cs->bones ? cs->bones : -1; // (appended after the clip bones)
	BonePose p[ANIM_MAX_BONES], q[ANIM_MAX_BONES];
	if (a->has_pose) {
		memcpy(p, a->pose, sizeof(BonePose) * cs->bones);
	} else {
		static const char *sleep_clips[3] = {"bed_left", "bed_right", "bedroll"};
		const int k = a->bed_clip % 3;
		const Clip *sleep = a->asleep && a->goal ? anim_clip(cs, sleep_clips[k]) : NULL;
		const Clip *bed = a->bed_anim ? anim_clip(cs, bed_clip_names[k + (a->bed_anim == BED_OUT ? 3 : 0)]) : NULL;
		const Clip *own = a->scripted && a->script_clip[0] ? anim_clip(cs, a->script_clip) : NULL;
		const Clip *idle = own ? own : bed ? bed : sleep ? sleep : anim_clip(cs, "idle"), *walk = walk_clip(a);
		if (!idle)
			return 0;
		const float t = own ? (a->script_once ? fminf(a->script_t, idle->duration - 0.001f) : a->script_t)
		              : bed ? fminf(a->bed_t, idle->duration - 0.001f) : a->t;
		anim_sample(cs, idle, t, p);
		if (walk && a->walk > 0.01f) {
			anim_sample(cs, walk, a->phase * walk->duration, q);
			anim_blend(p, q, cs->bones, a->walk);
		}
	}
	BoneDelta deltas[ANIM_MAX_BONES];
	anim_deltas(cs, p, deltas);
	for (int b = 0; b < cs->bones; b++) {
		memcpy(bone[b].r, deltas[b].r, sizeof(bone[b].r));
		memcpy(bone[b].t, deltas[b].t, sizeof(bone[b].t));
	}
	float r[9];
	if (fabsf(a->look) > 0.001f) { // (the head turned all the way, the neck half: no twist at the collar)
		rot_xyz(0.0f, a->look, 0.0f, r);
		rotate_about(&bone[B_HEAD], a->joint[B_HEAD], r);
		if (B_NECK < cs->bones) {
			rot_xyz(0.0f, a->look * 0.5f, 0.0f, r);
			rotate_about(&bone[B_NECK], a->joint[B_NECK], r);
		}
	}
	if (jaw >= 0) {
		bone[jaw] = bone[B_HEAD];
		rot_xyz(-JAW_OPEN_RAD * a->mouth, 0.0f, 0.0f, r);
		rotate_about(&bone[jaw], a->joint[jaw], r);
	}
	if (a->headless) { // (all on the head - face, hair, helmet - drawn into the neck: gone)
		const int nb = cs->bones < ANIM_MAX_BONES ? cs->bones : ANIM_MAX_BONES;
		for (int b = 0; b < nb; b++)
			if (b == B_HEAD || b == jaw) {
				float at[3];
				mul_rv(bone[b].r, a->joint[B_HEAD], at);
				for (int k = 0; k < 3; k++)
					at[k] += bone[b].t[k];
				memset(bone[b].r, 0, sizeof(bone[b].r));
				memcpy(bone[b].t, at, sizeof(at));
			}
	}
	return 1;
}

static void procedural_bones(const Actor *a, Xform bone[]) {
	float rot[MAX_BONES][3], com[3];
	pose(a, rot, com);
	for (int b = 0; b < a->nbones; b++) {
		float r[9], local_t[3], rj[3];
		rot_xyz(rot[b][0], rot[b][1], rot[b][2], r);
		mul_rv(r, a->joint[b], rj);
		for (int k = 0; k < 3; k++)
			local_t[k] = a->joint[b][k] - rj[k] + (b == B_COM ? com[k] : 0.0f);
		int p = a->parent[b];
		if (p < 0) {
			memcpy(bone[b].r, r, sizeof(r));
			memcpy(bone[b].t, local_t, sizeof(local_t));
		} else {
			mul_rr(bone[p].r, r, bone[b].r);
			mul_rv(bone[p].r, local_t, bone[b].t);
			for (int k = 0; k < 3; k++)
				bone[b].t[k] += bone[p].t[k];
		}
	}
}

// (skin_frame: counts actors_draw calls - when the torch flame was last placed; declared above emit)

// Characters posed on the GPU when its programs are there (objects_skinning); a talking face
// (its mouth moved per vertex) and a bound weapon's glow stay on the CPU
static int gpu_skinning(void) {
	static int known = -1;
	if (known < 0)
		known = objects_skinning();
	return known;
}

static int gpu_batch(const ABatch *bt) {
	return !bt->nmouth && bt->alpha != ALPHA_GLOW;
}

static float player_eye[3];
static int player_eye_frame;

int actors_player_eye(float out[3]) {
	if (!player_loaded || skin_frame - player_eye_frame > 2)
		return 0;
	memcpy(out, player_eye, sizeof(player_eye));
	return 1;
}

static void skin(Actor *a) {
	Xform bone[MAX_BONES] = {{{0}}};
	if (!clip_bones(a, bone))
		procedural_bones(a, bone);
	if (a->headonly) { // (all the body drawn into the neck: only the head left, in the bone's own pose)
		float at[3];
		mul_rv(bone[B_HEAD].r, a->joint[B_HEAD], at);
		for (int k = 0; k < 3; k++)
			at[k] += bone[B_HEAD].t[k];
		const int jaw_b = a->nbones > B_JAW && a->parent[a->nbones - 1] == B_HEAD ? a->nbones - 1 : -1;
		for (int b = 0; b < a->nbones && b < MAX_BONES; b++)
			if (b != B_HEAD && b != jaw_b) {
				memset(bone[b].r, 0, sizeof(bone[b].r));
				memcpy(bone[b].t, at, sizeof(at));
			}
	}
	// Into the world (heading), relative to the quantization origin, in quantized units
	float ch = cosf(a->heading), sh = sinf(a->heading), cp = cosf(a->pitch), sp = sinf(a->pitch);
	float heading[9] = {ch, sh * sp, sh * cp, 0, cp, -sp, -sh, ch * sp, ch * cp}; // turn x tilt
	if (a->roll != 0.0f) { // (x roll about the forward axis)
		const float cr = cosf(a->roll), sr = sinf(a->roll), turned[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
		float out[9];
		mul_rr(heading, turned, out);
		memcpy(heading, out, sizeof(out));
	}
	const float q = 32767.0f / (a->quant_m > 0.0f ? a->quant_m : QUANT_EXTENT_M), origin_dy = 1.0f,
	            sc = a->scale > 0.0f ? a->scale : 1.0f;
	Xform m[MAX_BONES];
	for (int b = 0; b < a->nbones; b++) {
		mul_rr(heading, bone[b].r, m[b].r);
		mul_rv(heading, bone[b].t, m[b].t);
		for (int k = 0; k < 3; k++)
			m[b].t[k] *= sc;
		m[b].t[1] -= origin_dy;
		for (int k = 0; k < 9; k++)
			m[b].r[k] *= q * sc;
		for (int k = 0; k < 3; k++)
			m[b].t[k] *= q;
	}
#if SKIN_NEON
	SkinCols cols[MAX_BONES];
	for (int b = 0; b < a->nbones; b++)
		skin_cols(&cols[b], m[b].r, m[b].t);
#endif
	// The pose for the GPU: the bones turned and scaled, about the origin (world = pos + this)
	const int gpu = gpu_skinning();
	if (gpu)
		for (int b = 0; b < a->nbones && b < MAX_BONES; b++) {
			float r[9], t[3];
			mul_rr(heading, bone[b].r, r);
			mul_rv(heading, bone[b].t, t);
			float *row = &a->palette[b * 12];
			for (int k = 0; k < 3; k++) {
				row[k * 4 + 0] = r[k * 3 + 0] * sc, row[k * 4 + 1] = r[k * 3 + 1] * sc, row[k * 4 + 2] = r[k * 3 + 2] * sc;
				row[k * 4 + 3] = t[k] * sc;
			}
		}
	if ((a == &player || a == &player1st) && a->torch_item >= 0) { // the flame, in the world
		float tip[3], rot[3];
		mul_rv(bone[B_LHAND].r, a->torch_tip, tip);
		for (int k = 0; k < 3; k++)
			tip[k] += bone[B_LHAND].t[k];
		mul_rv(heading, tip, rot);
		a->torch_at[0] = a->x + rot[0], a->torch_at[1] = a->y + rot[1], a->torch_at[2] = a->z + rot[2];
		a->torch_at_frame = skin_frame;
	}
	if (a == &player) { // the eyes, in the world (riding the cart: the view sits where the clip puts the head)
		float p[3], rot[3];
		mul_rv(bone[B_HEAD].r, a->joint[B_HEAD], p);
		for (int k = 0; k < 3; k++)
			p[k] += bone[B_HEAD].t[k];
		p[1] += 0.08f;
		mul_rv(heading, p, rot);
		player_eye[0] = a->x + rot[0] * sc, player_eye[1] = a->y + rot[1] * sc, player_eye[2] = a->z + rot[2] * sc;
		player_eye_frame = skin_frame;
	}
	if ((a == &player || a == &player1st) && magic_node_ok[a->female]) { // the palms, in the world
		static const int hand[2] = {B_LHAND, B_RHAND};
		for (int h = 0; h < 2; h++) {
			float p[3], rot[3];
			mul_rv(bone[hand[h]].r, magic_node[a->female][h], p);
			for (int k = 0; k < 3; k++)
				p[k] += bone[hand[h]].t[k];
			mul_rv(heading, p, rot);
			a->palm_at[h][0] = a->x + rot[0], a->palm_at[h][1] = a->y + rot[1], a->palm_at[h][2] = a->z + rot[2];
		}
		a->palm_frame = skin_frame;
	}
	int relight = !a->lit_valid || fabsf(wrap_angle(a->heading - a->lit_heading)) > RELIGHT_RAD;
	// (a spirit's look - the familiar's white, Become Ethereal's pale blue - on the CPU-posed too: it
	// was only given on the GPU's, and on the Vita the familiar was left all but invisible)
	const int pale = (a == &player || a == &player1st) && player_ghost ? 2 : a->ghost ? 1 : 0;
	if (pale != a->lit_pale)
		relight = 1, a->lit_pale = pale;
	for (int i = 0; i < a->nbatches; i++) {
		ABatch *bt = &a->batches[i];
		if (!batch_shown(a, bt))
			continue;
		if (gpu && gpu_batch(bt))
			continue; // (posed on the GPU: draw_skinned)
		int relight_batch = relight || !bt->lit; // (an item just put on gets lit once)
		const int mine = (a == &player || a == &player1st) && bt->part > 0 && bt->part <= a->nitems;
		const int poisoned = mine && a->items[bt->part - 1].tint;
		const unsigned glow = mine && bt->gear_tag != GEAR_SHEATHED && bt->gear_tag != GEAR_SCABBARD
		                      ? a->items[bt->part - 1].glow : 0;
		float glow_rgb[3] = {0};
		if (glow) { // (lit each frame: the shimmer comes and goes)
			const float pulse = 0.35f + 0.25f * sinf(skin_frame * 0.12f) + 0.1f * sinf(skin_frame * 0.31f);
			glow_rgb[0] = (glow >> 16 & 255) * pulse, glow_rgb[1] = (glow >> 8 & 255) * pulse, glow_rgb[2] = (glow & 255) * pulse;
			relight_batch = 1;
		}
		bt->lit = 1;
		// Talking: the face's own open-mouth shape, by how open the mouth is now
		const float open = a->mouth * MOUTH_MORPH_UNIT;
		const MouthMorph *mm = open > 0.0f ? bt->mouth : NULL, *mm_end = mm ? bt->mouth + bt->nmouth : NULL;
		static const float NO_LOOK[3] = {1.0f, 1.0f, 1.0f};
		const float *look = look_tint(a, bt) ? look_tint(a, bt) : NO_LOOK; // (your skin tone, hair colour)
		for (int v = 0; v < bt->vcount; v++) {
			const SrcVertex *s = &bt->src[v];
			const Xform *m0 = &m[s->bone[0]], *m1 = &m[s->bone[1]], *m2 = &m[s->bone2];
			float w0 = s->w[0] * (1.0f / 255.0f), w1 = s->w[1] * (1.0f / 255.0f), w2 = 1.0f - w0 - w1;
			const float *p = s->pos;
			float opened[3];
			if (mm && mm < mm_end && mm->v == v) {
				for (int k = 0; k < 3; k++)
					opened[k] = p[k] + mm->d[k] * open;
				p = opened;
				mm++;
			}
			// One bone (most of a body): its transform; else the weighted blend of the bones'
			// transforms, applied once (skinvert.h)
			int16_t out[3];
#if SKIN_NEON
			(void)m0, (void)m1, (void)m2;
			skin_vertex_neon(&cols[s->bone[0]], &cols[s->bone[1]], &cols[s->bone2], w0, w1, w2, p, out);
#else
			skin_vertex_scalar(m0->r, m0->t, m1->r, m1->t, m2->r, m2->t, w0, w1, w2, p, out);
#endif
			ObjectVertex *o = &bt->out[v];
			o->x = out[0], o->y = out[1], o->z = out[2];
			if (relight_batch) {
				float nx = s->n[0] / 127.0f, ny = s->n[1] / 127.0f, nz = s->n[2] / 127.0f;
				float n[3] = {nx * ch + nz * sh, ny, -nx * sh + nz * ch};
				uint8_t rgb[3];
				lighting_bake(n, s->col[0] / 255.0f * look[0], s->col[1] / 255.0f * look[1], s->col[2] / 255.0f * look[2], rgb);
				if (poisoned) // (a poison on the blade: tinged green)
					rgb[0] = (uint8_t)(rgb[0] * 0.55f), rgb[1] = (uint8_t)fminf(255.0f, rgb[1] * 1.15f + 18.0f),
					rgb[2] = (uint8_t)(rgb[2] * 0.5f);
				if (glow) // (an enchantment: its colour added, a glow)
					for (int c = 0; c < 3; c++)
						rgb[c] = (uint8_t)fminf(255.0f, rgb[c] + glow_rgb[c]);
				if (pale) { // (as the GPU's tint: draw_skinned's)
					static const float MUL[2][3] = {{0.6f, 0.65f, 0.7f}, {0.55f, 0.7f, 0.9f}};
					static const float ADD[2][3] = {{0.22f, 0.26f, 0.34f}, {0.12f, 0.16f, 0.24f}};
					for (int c = 0; c < 3; c++)
						rgb[c] = (uint8_t)fminf(255.0f, rgb[c] * MUL[pale - 1][c] + ADD[pale - 1][c] * 255.0f);
				}
				o->r = rgb[0], o->g = rgb[1], o->b = rgb[2];
			}
		}
	}
	if (relight)
		a->lit_heading = a->heading, a->lit_valid = 1;
	a->posed = 1;
}

// --- Creatures (creatures.c runs them; here they're loaded, posed and drawn) -------------

static Actor beasts[MAX_BEASTS];
static int beast_used[MAX_BEASTS];
static char beast_paths[BEAST_KINDS][160];
static char beast_names[BEAST_KINDS][48];
static int beast_set_of[BEAST_KINDS]; // its clip set (clip_sets index)
static int beast_kinds;
static char set_names[BEAST_SETS][32];
static int set_count;

int actors_beast_kind(const char *name, const char *set) {
	for (int k = 0; k < beast_kinds; k++)
		if (!strcmp(beast_names[k], name))
			return k;
	if (beast_kinds >= BEAST_KINDS)
		return -1;
	int cs = !strcmp(set, "@male") ? 0 : !strcmp(set, "@female") ? 1 : -1;
	for (int s = 0; cs < 0 && s < set_count; s++)
		if (!strcmp(set_names[s], set))
			cs = 3 + s;
	if (cs < 0) { // (a creature's clips: loaded once for every kind on its skeleton)
		if (set_count >= BEAST_SETS)
			return -1;
		char path[256];
		snprintf(path, sizeof(path), "%screatures/%s.skanim", actors_pack, set);
		if (!anim_load(path, &clip_sets[3 + set_count])) {
			sceClibPrintf("actors: no creature clips %s\n", path);
			return -1;
		}
		snprintf(set_names[set_count], sizeof(set_names[0]), "%s", set);
		cs = 3 + set_count++;
	}
	if (!strncmp(name, "actors/", 7)) // (someone in particular: an actor's own model - the keep's captain as a foe)
		snprintf(beast_paths[beast_kinds], sizeof(beast_paths[0]), "%s%s.sknpc", actors_pack, name);
	else
		snprintf(beast_paths[beast_kinds], sizeof(beast_paths[0]), "%screatures/%s.sknpc", actors_pack, name);
	snprintf(beast_names[beast_kinds], sizeof(beast_names[0]), "%s", name);
	beast_set_of[beast_kinds] = cs;
	return beast_kinds++;
}

void actors_beast_scale(int i, float scale) {
	if (i < 0 || i >= MAX_BEASTS || !beast_used[i])
		return;
	Actor *a = &beasts[i];
	a->scale = scale;
	// (room for all of it, scaled - a mammoth, a giant spider - in the quantized vertices:
	// the bind pose's farthest point from the hips, with some for poses that reach farther)
	float far2 = 0.0f;
	for (int b = 0; b < a->nbatches; b++)
		for (int v = 0; v < a->batches[b].vcount; v++) {
			const float *p = a->batches[b].src[v].pos;
			far2 = fmaxf(far2, p[0] * p[0] + (p[1] - 1.0f) * (p[1] - 1.0f) + p[2] * p[2]);
		}
	a->quant_m = fmaxf(QUANT_EXTENT_M, sqrtf(far2) * scale * 1.35f);
}

void actors_beast_look(int i, int far, float fade) {
	if (i >= 0 && i < MAX_BEASTS && beast_used[i])
		beasts[i].far = far, beasts[i].fade = fade;
}

void actors_beast_fall(int i, float pitch, float roll) {
	if (i >= 0 && i < MAX_BEASTS && beast_used[i])
		beasts[i].pitch = pitch, beasts[i].roll = roll;
}

int actors_beast_spawn(int kind) {
	if (kind < 0 || kind >= beast_kinds)
		return -1;
	for (int i = 0; i < MAX_BEASTS; i++) {
		if (beast_used[i])
			continue;
		Actor *a = &beasts[i];
		memset(a, 0, sizeof(*a));
		if (!load_actor(beast_paths[kind], a))
			return -1;
		a->female = beast_set_of[kind];
		a->state = IDLE;
		beast_used[i] = 1;
		return i;
	}
	return -1;
}

void actors_beast_ghost(int i, int on) {
	if (i >= 0 && i < MAX_BEASTS && beast_used[i])
		beasts[i].ghost = on;
}

void actors_beast_headonly(int i, int on) {
	if (i >= 0 && i < MAX_BEASTS && beast_used[i])
		beasts[i].headonly = on;
}

void actors_beast_strip(int i) {
	if (i >= 0 && i < MAX_BEASTS && beast_used[i])
		strip_gear(&beasts[i]);
}

void actors_beast_remove(int i) {
	if (i >= 0 && i < MAX_BEASTS && beast_used[i]) {
		free_actor(&beasts[i]);
		beast_used[i] = 0;
	}
}

float actors_beast_clip(int kind, const char *name, float *speed) {
	const Clip *c = kind >= 0 && kind < beast_kinds ? anim_clip(&clip_sets[beast_set_of[kind]], name) : NULL;
	if (speed)
		*speed = c ? c->speed : 0.0f;
	return c ? c->duration : 0.0f;
}

void actors_beast_pose(int i, float x, float y, float z, float heading, const char *clip, float t, const char *blend,
                       float blend_t, float w, float dt) {
	if (i < 0 || i >= MAX_BEASTS || !beast_used[i])
		return;
	Actor *a = &beasts[i];
	const ClipSet *cs = &clip_sets[a->female];
	a->x = x, a->y = y, a->z = z, a->heading = heading;
	const Clip *c = anim_clip(cs, clip);
	if (!c)
		return;
	BonePose p[ANIM_MAX_BONES], q[ANIM_MAX_BONES];
	anim_sample(cs, c, t, p);
	const Clip *b = blend && w > 0.001f ? anim_clip(cs, blend) : NULL;
	if (b) {
		anim_sample(cs, b, blend_t, q);
		anim_blend(p, q, cs->bones, w);
	}
	if (!a->has_pose)
		memcpy(a->pose, p, sizeof(BonePose) * cs->bones);
	else
		anim_blend(a->pose, p, cs->bones, fminf(1.0f, dt * 14.0f));
	a->has_pose = 1;
}

// --- Posing on a second core --------------------------------------------------------------
//
// The Vita has three cores for games and the frame ran on one. Posing people (their bones,
// and the vertices of the few parts still posed on the CPU) is pure arithmetic on each
// actor's own data, so a second core does it while the first draws the world:
// actors_prepare picks who to pose and hands them over; actors_draw waits for them.

#define POSE_JOBS (MAX_ACTORS + MAX_BEASTS)
static Actor *pose_jobs[POSE_JOBS];
static volatile int pose_njobs;
static SceUID pose_go = -1, pose_done = -1;
static int pose_pending; // (handed over; not yet waited for)
static int order[MAX_ACTORS], order_n, beast_order[MAX_BEASTS], beast_n, posed_now;
static float dist[MAX_ACTORS];

static int pose_worker(SceSize args, void *argp) {
	(void)args, (void)argp;
	for (;;) {
		if (sceKernelWaitSema(pose_go, 1, NULL) < 0)
			return 0;
		for (int i = 0; i < pose_njobs; i++)
			skin(pose_jobs[i]);
		sceKernelSignalSema(pose_done, 1);
	}
}

static void pose_start(void) {
	if (pose_go >= 0)
		return;
	pose_go = sceKernelCreateSema("pose_go", 0, 0, 1, NULL);
	pose_done = sceKernelCreateSema("pose_done", 0, 0, 1, NULL);
	// (core 1: the frame's own work stays on core 0, sound and streaming have the rest)
	SceUID t = sceKernelCreateThread("pose", pose_worker, 0x10000100, 128 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
	if (pose_go < 0 || pose_done < 0 || t < 0 || sceKernelStartThread(t, 0, NULL) < 0) {
		sceClibPrintf("actors: no posing thread (%x %x %x): posing on the main core\n", pose_go, pose_done, t);
		pose_go = -2;
	}
}

void actors_prepare(float cam_x, float cam_z) {
	if (pose_pending)
		return;
	pose_start();
	draw_frame++;
	skin_frame++;
	const unsigned frame = draw_frame;
	int njobs = 0, posed = 0;
	// Who's in view, nearest first: the nearest few are posed every frame, the rest take
	// turns (a crowded hall would otherwise pose everyone every frame)
	int n = 0, meshes_read = 0, waiting = 0;
	const int per_frame = loading_screen ? MESHES_LOADING : MESHES_A_FRAME;
	for (int i = 0; i < actor_count; i++) {
		Actor *a = &actors[i];
		float d = hypotf(a->x - cam_x, a->z - cam_z);
		if (a->state == HIDDEN || d > (a->script_far ? SCRIPT_FAR_M : DRAW_RADIUS_M) || (a->scripted && a->y < -400.0f)) // (nor a script's kept
			continue; // offstage, all on one spot under the world: Helgen's cast - 21 drawn in the keep's middle room)
		if (!terrain_rect_in_view(a->x - 1.0f, a->z - 1.0f, a->x + 1.0f, a->z + 1.0f) ||
		    !objects_point_in_sight(a->x, a->y + 1.0f, a->z)) // (indoors: in a room out of sight)
			continue;
		if (!a->mesh_loaded) { // (its model read now: two a frame at most - a city's crowd over a few frames)
			if (meshes_read >= per_frame) {
				waiting++;
				continue;
			}
			load_mesh(a), meshes_read++;
		}
		a->seen_frame = draw_frame;
		int k = n++;
		for (; k > 0 && dist[order[k - 1]] > d; k--)
			order[k] = order[k - 1];
		order[k] = i, dist[i] = d;
	}
	order_n = n, meshes_waiting = waiting, in_view = n;
	for (int j = 0; j < n; j++) {
		const int i = order[j];
		Actor *a = &actors[i];
		const float d = dist[i];
		unsigned every = j < pose_nearest && d < pose_full_m ? 1 : d < pose_half_m ? 2 : 4;
		if (a->scripted && (a->state == WALKING || a->script_clip[0]) && d < SCRIPTED_POSE_M)
			every = 1; // (a scene's walkers and actors - Helgen's: posed each frame, not drawn sliding in their last pose)
		// (due once its turn's come round - every frame, or every second / fourth - and kept due
		// till it's had one: on a fixed turn, a crowd's farther people lost theirs to the nearer
		// every time, and stood for good in the last pose they had - mid-stride at the block)
		// (and never left waiting long: on a turn, the nearer ones posed each frame took the whole
		// budget, and the rest - Tullius, the captain - stood for good in a half-finished stride)
		if (!a->posed || (frame - a->pose_frame >= every && (every == 1 || posed < pose_max)) ||
		    frame - a->pose_frame >= POSE_LONGEST_WAIT) {
			pose_jobs[njobs++] = a;
			a->pose_frame = frame;
			posed++;
		}
	}
	posed_now = posed;
	beast_n = 0;
	for (int i = 0; i < MAX_BEASTS; i++) { // creatures
		Actor *a = &beasts[i];
		if (!beast_used[i] || !a->has_pose || hypotf(a->x - cam_x, a->z - cam_z) > (a->far ? FAR_DRAW_M : DRAW_RADIUS_M))
			continue;
		const float span = a->quant_m > 0.0f ? a->quant_m : 2.0f; // (a mammoth: wider)
		if (!terrain_rect_in_view(a->x - span, a->z - span, a->x + span, a->z + span) ||
		    !objects_point_in_sight(a->x, a->y + 1.0f, a->z))
			continue;
		// (posed every frame up close; farther off every second / fourth frame, taking turns:
		// a crowd of bandits would otherwise cost more than the rest of the frame)
		const float d = hypotf(a->x - cam_x, a->z - cam_z);
		const unsigned every = d < BEAST_EVERY_FRAME_M ? 1 : d < BEAST_HALF_RATE_M ? 2 : 4;
		if (!a->posed || frame - a->pose_frame >= every)
			pose_jobs[njobs++] = a, a->pose_frame = frame;
		beast_order[beast_n++] = i;
	}
	pose_njobs = njobs;
	if (pose_go >= 0) {
		pose_pending = 1;
		sceKernelSignalSema(pose_go, 1);
	} else { // (no second core to be had)
		for (int i = 0; i < njobs; i++)
			skin(pose_jobs[i]);
	}
}

void actors_draw(float cam_x, float cam_z) {
	static unsigned posed_total, drawn_total, frames;
	static unsigned long long skin_us_total, skin_us_peak;
	actors_prepare(cam_x, cam_z); // (if it wasn't already: posed now, on this core)
	unsigned long long t0 = sceKernelGetProcessTimeWide();
	if (pose_pending) {
		sceKernelWaitSema(pose_done, 1, NULL);
		pose_pending = 0;
	}
	const unsigned frame = draw_frame;
	int count = 0, shown = 0, beasts_now = 0;
	static int beasts_shown;
	const int posed = posed_now;
	for (int j = 0; j < order_n; j++) {
		emit(&actors[order[j]], &count);
		shown++;
	}
	// Those out of sight a while: their GPU buffers back (people come and go in a city)
	for (int i = (int)(frame % 16); i < actor_count; i += 16) {
		Actor *a = &actors[i];
		if (a->drawn_frame && skin_frame - a->drawn_frame > GPU_BUFFERS_IDLE) {
			for (int b = 0; b < a->nbatches; b++)
				free_gpu_buffers(&a->batches[b]);
			a->drawn_frame = 0;
		}
		if (a->mesh_loaded && frame - a->seen_frame > MESH_IDLE_FRAMES && (int)(frame - a->keep_until) > 0) // (out of sight a
			free_mesh(a); // while: its model given back - not one read ahead for a scene to come)
	}
	if (player_loaded && player_shown) { // third person: you
		skin(&player);
		emit(&player, &count);
	}
	for (int j = 0; j < beast_n; j++) { // creatures
		emit(&beasts[beast_order[j]], &count);
		beasts_now++;
	}
	beasts_shown = beasts_now;
	unsigned long long us = sceKernelGetProcessTimeWide() - t0;
	objects_draw_dynamic(drawn, count);
	// Diagnostics every ~5 s: time this core spent on people (waiting for the posing core, then
	// handing them to the GPU)
	skin_us_total += us, posed_total += posed, drawn_total += shown;
	if (us > skin_us_peak)
		skin_us_peak = us;
	if (++frames == 300) {
		game_log("actors: %.1f people drawn, %.1f posed a frame, %d creatures; people %llu us avg, %llu us peak",
		         drawn_total / 300.0f, posed_total / 300.0f, beasts_shown, skin_us_total / 300, skin_us_peak);
		if (order_n > 8) { // (a crowd: who's in it)
			char who[400] = "";
			for (int j = 0; j < order_n && strlen(who) < sizeof(who) - 40; j++)
				snprintf(who + strlen(who), sizeof(who) - strlen(who), "%s%s %.0fm", j ? ", " : "", actors[order[j]].id,
				         dist[order[j]]);
			game_log("actors: drawn: %s", who);
		}
		frames = posed_total = drawn_total = 0;
		skin_us_total = skin_us_peak = 0;
	}
}

void actors_set_loading(int on) {
	loading_screen = on;
}

// Models read ahead, behind a black screen (the opening's blackout after the block): those within radius of x, z - or in a
// scene - not read yet, up to max this call; how many were read. Each timed into the log (the Vita's cost, not the emulator's)
int actors_preload(float x, float z, float radius, int max) {
	int done = 0;
	for (int i = 0; i < actor_count && done < max; i++) {
		Actor *a = &actors[i];
		if (a->mesh_loaded || (!a->scripted && hypotf(a->x - x, a->z - z) > radius))
			continue;
		const unsigned long long t0 = stream_now_us();
		load_mesh(a);
		a->keep_until = draw_frame + PRELOAD_KEEP_FRAMES; // (kept till its scene: unseen, it was given back the next frame and
		done++;                                           //  read again - the same dozen models over and over, Nov 10)
		static int told;
		if (told++ < 40)
			game_log("actors: %s's model read ahead in %u ms", a->id, (unsigned)((stream_now_us() - t0) / 1000));
	}
	return done;
}

void actors_script_far(int i, int on) {
	if (i >= 0 && i < actor_count)
		actors[i].script_far = on;
}

int actors_meshes_waiting(void) {
	return meshes_waiting;
}

int actors_in_view(void) {
	return in_view;
}

int actors_count(void) {
	return actor_count;
}

const char *actors_id(int i) {
	return i >= 0 && i < actor_count ? actors[i].id : "";
}

void actors_wake(int i, float x, float z) {
	if (i < 0 || i >= actor_count || !actors[i].asleep)
		return;
	Actor *a = &actors[i];
	// Getting up (Skyrim's clip), then facing you; back to bed after a while (if it's still
	// bedtime)
	bed_begin(a, BED_OUT);
	a->need_plan = 1;
	a->wait = WOKEN_AWAKE_S;
	a->attend_time = 3.0f, a->attend_x = x, a->attend_z = z;
}

int actors_asleep(int i) {
	return i >= 0 && i < actor_count && actors[i].asleep;
}

int actors_door(int i, float pos[3]) {
	if (i < 0 || i >= actor_count || actors[i].state != HIDDEN || !actors[i].has_door)
		return 0;
	memcpy(pos, actors[i].door, 3 * sizeof(float));
	return 1;
}

int actors_where(int i, float pos[3]) {
	if (i < 0 || i >= actor_count)
		return 0;
	const Actor *a = &actors[i];
	pos[0] = a->x, pos[1] = a->y, pos[2] = a->z;
	return a->state != HIDDEN && a->simulated;
}

int actors_shown(int i, float pos[3]) {
	if (i < 0 || i >= actor_count)
		return 0;
	const Actor *a = &actors[i];
	pos[0] = a->x, pos[1] = a->y, pos[2] = a->z;
	return a->state != HIDDEN;
}

void actors_set_mouth(int i, float open) {
	if (i >= 0 && i < actor_count)
		actors[i].mouth_target = open < 0.0f ? 0.0f : (open > 1.0f ? 1.0f : open);
}

void actors_attend(int i, float x, float z, float seconds) {
	if (i < 0 || i >= actor_count)
		return;
	actors[i].attend_time = seconds;
	actors[i].attend_x = x, actors[i].attend_z = z;
}

// --- The player ------------------------------------------------------------------------------

// Bone b is the clavicle or below it (the arm, its twist bones, the hand)
static int in_arm(const ClipSet *cs, int b, int clavicle) {
	for (; b >= 0; b = cs->parent[b])
		if (b == clavicle)
			return 1;
	return 0;
}

static int in_left_arm(const ClipSet *cs, int b) {
	return in_arm(cs, b, B_LCLAV);
}

static void q_mul(const float a[4], const float b[4], float o[4]) { // (x, y, z, w)
	o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

// A bone's rotation in model space (its local rotations down from the root)
static void model_rotation(const ClipSet *cs, const BonePose *pose, int b, float out[4]) {
	float q[4] = {0, 0, 0, 1}, t[4];
	for (; b >= 0; b = cs->parent[b]) {
		q_mul(pose[b].q, q, t);
		memcpy(q, t, sizeof(q));
	}
	memcpy(out, q, sizeof(q));
}

// out = bone b of pose with the model-space rotation it has in clip (its parent as in pose)
static void match_model_rotation(const ClipSet *cs, const BonePose *pose, const BonePose *clip, int b, BonePose *out) {
	float parent_pose[4] = {0, 0, 0, 1}, want[4], inv[4];
	if (cs->parent[b] >= 0)
		model_rotation(cs, pose, cs->parent[b], parent_pose);
	model_rotation(cs, clip, b, want);
	inv[0] = -parent_pose[0], inv[1] = -parent_pose[1], inv[2] = -parent_pose[2], inv[3] = parent_pose[3];
	*out = pose[b];
	q_mul(inv, want, out->q);
}

// Skyrim's clip for an arm (0 left, 1 right) holding a readied spell: held ready, aimed
// (Flames) or on yourself (Healing); both hands doing the same, its two-handed clip.
// NULL: that arm holds no spell.
static const Clip *magic_clip(const ClipSet *cs, int arm) {
	static const char *names[3][3] = {{"mlh_idle", "mlh_aim", "mlh_self"}, {"mrh_idle", "mrh_aim", "mrh_self"},
	                                  {"dual_idle", "dual_aim", "dual_self"}};
	if (player_bound) // (a prisoner: the wrists together before you - Skyrim's bound idle; none in first person: lowered)
		return anim_clip(cs, "bound_idle");
	if (weapon_arm(arm) && magic_pose[arm] < 0) { // (a weapon drawn: Skyrim's ready pose for its kind)
		const int bow = weapon_kind == 7 || weapon_kind == 9;
		// (not the first-person arms: the camera's crouched already - its clip crouched them again, and
		// the bow drawn sneaking was below the view)
		const Clip *crouched = player_sneaking && cs != &clip_sets[CLIPS_FIRST]
		                       ? anim_clip(cs, bow ? "sneak_bow_idle" : "sneak_w1h_idle") : NULL;
		if (crouched) // (sneaking: its crouched stance)
			return crouched;
		const Clip *dual = left_kind > 0 ? anim_clip(cs, "dw_idle") : NULL;
		if (dual) // (a weapon in each hand: Skyrim's dual-wield stance)
			return dual;
		return anim_clip(cs, weapon_kind == 5 ? "w2hm_idle" : weapon_kind == 6 || weapon_kind == 8 ? "w2hw_idle"
		                     : bow ? "bow_idle" : "w1h_idle");
	}
	const int pose = magic_pose[arm];
	if (pose < 0 || pose > 2)
		return NULL;
	const Clip *c = magic_pose[0] == magic_pose[1] ? anim_clip(cs, names[2][pose]) : NULL;
	return c ? c : anim_clip(cs, names[arm][pose]);
}

// The player sitting (a chair, a stool): getting on with Skyrim's clip (its root motion walks
// the body from beside the seat onto it), sitting, getting up
enum { SEAT_NONE, SEAT_IN, SEAT_ON, SEAT_OUT };
static struct {
	int mode;
	char in[17], idle[17], out[17];
	float t, base[2], base_h; // the clip's start (in / out): where and facing
	float seat[3], heading;   // on it
	int ride;                 // moved by a script each frame (the opening's cart): no getting up
} seat;

int actors_player_seat(const float pos[3], float heading_rad, const char *in, const char *idle, const char *out) {
	const ClipSet *cs = &clip_sets[player.female];
	if (!player_loaded || seat.mode || !anim_clip(cs, in) || !anim_clip(cs, idle) || !anim_clip(cs, out))
		return 0;
	snprintf(seat.in, sizeof(seat.in), "%s", in);
	snprintf(seat.idle, sizeof(seat.idle), "%s", idle);
	snprintf(seat.out, sizeof(seat.out), "%s", out);
	memcpy(seat.seat, pos, sizeof(seat.seat));
	seat.heading = heading_rad;
	// Where the clip starts: its root motion ends on the seat
	const RootMotion *m = root_named(player.female == 1, in);
	seat.base[0] = pos[0], seat.base[1] = pos[2], seat.base_h = heading_rad;
	if (m->n) {
		const float *e = m->key[m->n - 1];
		float wx, wz;
		seat.base_h = heading_rad - e[3];
		turn_xz(seat.base_h, e[1], e[2], &wx, &wz);
		seat.base[0] -= wx, seat.base[1] -= wz;
	}
	seat.mode = SEAT_IN, seat.t = 0.0f;
	player.heading = seat.base_h;
	return 1;
}

void actors_player_ride(const float pos[3], float heading_rad, const char *idle) {
	if (!idle) {
		if (seat.mode == SEAT_ON && seat.ride)
			seat.mode = SEAT_NONE;
		seat.ride = 0;
		return;
	}
	if (strcmp(seat.idle, idle) || seat.mode != SEAT_ON)
		seat.t = 0.0f;
	snprintf(seat.idle, sizeof(seat.idle), "%s", idle);
	snprintf(seat.out, sizeof(seat.out), "%s", idle); // (no getting up: the script stands you)
	memcpy(seat.seat, pos, sizeof(seat.seat));
	seat.heading = heading_rad, seat.base_h = heading_rad;
	seat.base[0] = pos[0], seat.base[1] = pos[2];
	seat.mode = SEAT_ON, seat.ride = 1;
	player.heading = heading_rad;
}

void actors_player_stand(void) {
	if (seat.mode != SEAT_ON)
		return;
	seat.mode = SEAT_OUT, seat.t = 0.0f;
	seat.base[0] = seat.seat[0], seat.base[1] = seat.seat[2], seat.base_h = seat.heading;
}

int actors_player_seated(float pos[3], float *heading_deg) {
	if (!seat.mode)
		return 0;
	float x = seat.seat[0], z = seat.seat[2], h = seat.heading;
	if (seat.mode != SEAT_ON) {
		const RootMotion *m = root_named(player.female == 1, seat.mode == SEAT_IN ? seat.in : seat.out);
		float r[3] = {0, 0, 0}, wx, wz;
		root_at(m, seat.t, r);
		turn_xz(seat.base_h, r[0], r[1], &wx, &wz);
		x = seat.base[0] + wx, z = seat.base[1] + wz, h = wrap_angle(seat.base_h + r[2]);
	}
	pos[0] = x, pos[1] = seat.seat[1], pos[2] = z;
	*heading_deg = -h * 180.0f / (float)M_PI;
	return seat.mode;
}

// Sitting: the seat's clip is the whole body (and its heading); 1 if it is
static int seat_pose(Actor *a, const ClipSet *cs, float dt) {
	if (!seat.mode)
		return 0;
	seat.t += dt;
	const Clip *c = anim_clip(cs, seat.mode == SEAT_IN ? seat.in : seat.mode == SEAT_ON ? seat.idle : seat.out);
	if (!c) {
		seat.mode = SEAT_NONE;
		return 0;
	}
	if (seat.mode == SEAT_IN && seat.t >= c->duration)
		seat.mode = SEAT_ON, seat.t = 0.0f, c = anim_clip(cs, seat.idle);
	else if (seat.mode == SEAT_OUT && seat.t >= c->duration) {
		seat.mode = SEAT_NONE;
		return 0;
	}
	float p[3], hd;
	actors_player_seated(p, &hd);
	a->heading = -hd * (float)M_PI / 180.0f;
	BonePose target[ANIM_MAX_BONES];
	anim_sample(cs, c, seat.mode == SEAT_ON ? a->t : fminf(seat.t, c->duration - 0.001f), target);
	if (!a->has_pose)
		memcpy(a->pose, target, sizeof(BonePose) * cs->bones);
	else
		anim_blend(a->pose, target, cs->bones, fminf(1.0f, dt * 12.0f));
	a->has_pose = 1;
	return 1;
}

// Fighting: a swing (Skyrim's attack clip) over the upper body for its length, or the block
// held; standing, the whole body takes it
static struct {
	int active;
	char clip[17];
	float t, duration;
} attack;
static char block_clip[17];
static float block_w;

void actors_player_flesh(int kind, float left) {
	player_flesh = kind;
	flesh_fade = kind ? fminf(1.0f, fminf(left / 1.5f, 1.0f)) : 0.0f; // (fading the last moment and a half)
}

void actors_player_ghost(int on) {
	player_ghost = on;
}

void actors_player_attack_speed(float mult) {
	attack_speed = mult > 0.1f ? mult : 1.0f;
}

float actors_player_attack(const char *clip) {
	const Clip *c = anim_clip(&clip_sets[player.female], clip);
	if (!c || attack.active)
		return 0.0f;
	attack.active = 1, attack.t = 0.0f, attack.duration = c->duration;
	snprintf(attack.clip, sizeof(attack.clip), "%s", clip);
	return c->duration;
}

float actors_player_attack_time(void) {
	return attack.active ? attack.t : -1.0f;
}

void actors_player_block(const char *clip) {
	if (clip)
		snprintf(block_clip, sizeof(block_clip), "%s", clip);
	else
		block_clip[0] = 0;
}

static int upper_body(const ClipSet *cs, int b) {
	return b == B_SPINE || b == B_SPINE1 || b == B_SPINE2 || b == B_NECK || b == B_HEAD || in_arm(cs, b, B_LCLAV) ||
	       in_arm(cs, b, B_RCLAV);
}

// The swing / block over target (dt: advances them - the third-person pose; 0: the
// first-person arms take the same moment from their own clips)
static float bow_draw_t = -1.0f; // a bow being drawn: how long (actors_player_bow)

// An upper-body clip over target with weight w: its spine turned as the clip turns the whole
// torso (Skyrim's bow draw, swings and blocks turn the body at its COM - side-on to draw a
// bow; laid over another pose's hips, the arms would point the wrong way), the legs left alone
static void overlay_upper(const ClipSet *cs, BonePose *target, const BonePose *move, float w) {
	BonePose lean = target[B_SPINE];
	match_model_rotation(cs, target, move, B_SPINE, &lean);
	for (int b = 0; b < cs->bones; b++)
		if (b != B_SPINE && upper_body(cs, b))
			anim_blend(&target[b], &move[b], 1, w);
	anim_blend(&target[B_SPINE], &lean, 1, w);
}

static void fight_pose(const ClipSet *cs, BonePose *target, float dt, int whole) {
	BonePose move[ANIM_MAX_BONES];
	const Clip *draw = bow_draw_t >= 0.0f && !attack.active
	                   ? anim_clip(cs, player_sneaking && cs != &clip_sets[CLIPS_FIRST] ? "sneak_bow_draw" : "bow_draw") : NULL;
	if (!draw && bow_draw_t >= 0.0f && !attack.active)
		draw = anim_clip(cs, "bow_draw");
	if (draw) { // (drawn back, then held at full draw)
		anim_sample(cs, draw, fminf(bow_draw_t, draw->duration - 0.001f), move);
		overlay_upper(cs, target, move, 1.0f);
	}
	if (dt > 0.0f)
		block_w += ((block_clip[0] ? 1.0f : 0.0f) - block_w) * fminf(1.0f, dt * 10.0f);
	const Clip *bc = block_clip[0] ? anim_clip(cs, block_clip) : NULL;
	if (bc && block_w > 0.01f) {
		anim_sample(cs, bc, 0.0f, move);
		overlay_upper(cs, target, move, block_w);
	}
	if (!attack.active)
		return;
	const Clip *c = anim_clip(cs, attack.clip);
	if (dt > 0.0f && (attack.t += dt * attack_speed) >= attack.duration) {
		attack.active = 0;
		return;
	}
	if (!c)
		return;
	const float w = fminf(1.0f, fminf(attack.t / 0.06f, (attack.duration - attack.t) / 0.12f));
	anim_sample(cs, c, fminf(attack.t, c->duration - 0.001f), move);
	if (whole)
		anim_blend(target, move, cs->bones, w);
	else
		overlay_upper(cs, target, move, w);
}

void actors_player_bow(float draw_s) {
	bow_draw_t = draw_s;
}

float actors_player_bow_draw_length(void) {
	const Clip *c = anim_clip(&clip_sets[player.female], "bow_draw");
	return c ? c->duration : 1.0f;
}

void actors_player_magic(int left_pose, int right_pose) {
	magic_pose[0] = left_pose, magic_pose[1] = right_pose;
}

int actors_player_palms(float out[2][3]) {
	const Actor *a = player1st_shown ? &player1st : player_shown ? &player : NULL;
	if (!a || a->palm_frame != skin_frame || !magic_node_ok[a->female])
		return 0;
	memcpy(out, a->palm_at, sizeof(a->palm_at));
	return 1;
}

static int player_faces_camera;
static float player_face_turn; // (degrees: the right stick turning you round in the race menu)

static int player_faces_view;

void actors_player_face_view(int on) {
	player_faces_view = on;
}

void actors_player_face_camera(int on, float turn_deg) {
	player_faces_camera = on, player_face_turn = turn_deg;
}

static char mount_clip[24]; // (in the saddle: Skyrim's rider clip, at mount_t into it)
static float mount_t;

static char scene_clip[24]; // (a killmove: Skyrim's paired clip, the whole body, at scene_t into it)
static float scene_t;

void actors_player_scene(const char *clip, float t) {
	snprintf(scene_clip, sizeof(scene_clip), "%s", clip ? clip : "");
	scene_t = t;
}

void actors_player_mount(const char *clip, float t) {
	snprintf(mount_clip, sizeof(mount_clip), "%s", clip ? clip : "");
	mount_t = t;
}

void actors_player_update(float x, float y, float z, float yaw_deg, float vx, float vz, float vy, int grounded,
                          int swimming, int sneaking, float dt, int shown) {
	Actor *a = &player;
	player_shown = shown && player_loaded;
	player_sneaking = sneaking;
	if (!player_loaded)
		return;
	a->x = x, a->y = y, a->z = z;
	// In first person the body faces where you look; in third person it turns to where it's
	// going, and standing still the camera is free to circle it (as in Skyrim). With a spell
	// readied it turns to face where you look and walks sideways and back instead (Skyrim's
	// combat stance)
	const float speed = hypotf(vx, vz);
	// (or a scene turning the view to what's happening - Hadvar's INFO 0003F88B: you faced away
	// in third person, the body left as it walked into the line)
	const int facing_view = !shown || magic_pose[0] >= 0 || magic_pose[1] >= 0 || weapon_arm(1) || player_faces_view ||
	                        scene_clip[0]; // (a killmove: facing its victim, ahead)
	if (player_faces_camera) { // (the race menu: its face to you, wherever the view's from)
		a->heading = wrap_angle(-(yaw_deg + 180.0f + player_face_turn) * (float)M_PI / 180.0f);
	} else if (!shown) {
		a->heading = -yaw_deg * (float)M_PI / 180.0f;
	} else if (facing_view) {
		float want = -yaw_deg * (float)M_PI / 180.0f, diff = wrap_angle(want - a->heading);
		float turn = PLAYER_TURN_RATE * 1.5f * dt;
		a->heading = wrap_angle(a->heading + (fabsf(diff) < turn ? diff : copysignf(turn, diff)));
	} else if (speed > 0.3f) {
		float want = atan2f(-vx, -vz), diff = wrap_angle(want - a->heading), turn = PLAYER_TURN_RATE * dt;
		a->heading = wrap_angle(a->heading + (fabsf(diff) < turn ? diff : copysignf(turn, diff)));
	}
	a->t += dt;
	const int torch_up = a->torch_item >= 0 && a->items[a->torch_item].equipped && !swimming;
	a->torch_w += ((torch_up ? 1.0f : 0.0f) - a->torch_w) * fminf(1.0f, dt * TORCH_RAISE_RATE);
	const ClipSet *cs = &clip_sets[a->female];
	if (!cs->bones || cs->bones < B_JAW) {
		a->has_pose = 0;
		a->walk += ((hypotf(vx, vz) > 0.15f ? 1.0f : 0.0f) - a->walk) * fminf(1.0f, dt * 5.0f);
		a->phase = fmodf(a->phase + hypotf(vx, vz) * dt / STRIDE_M, 1.0f);
		return;
	}
	if (seat_pose(a, cs, dt))
		return;
	// Which way the body moves, relative to where it faces (forward -sin h, -cos h); in third
	// person it turns to face its way, so only the forward cycles play
	const float h = a->heading, fwd = !facing_view ? speed : vx * -sinf(h) + vz * -cosf(h);
	const float side = !facing_view ? 0.0f : vx * cosf(h) + vz * -sinf(h);
	BonePose target[ANIM_MAX_BONES], move[ANIM_MAX_BONES];
	const Clip *idle = anim_clip(cs, "idle");
	if (!idle)
		return;
	// A weapon drawn: its ready stance is the whole body's (feet apart, both hands on a
	// two-handed grip), not only the arms'
	const char *stance = shown && !swimming ? combat_set() : NULL;
	const Clip *ready = stance && weapon_arm(1) ? magic_clip(cs, 1) : NULL;
	anim_sample(cs, ready ? ready : idle, a->t, target);
	const Clip *mounted = mount_clip[0] ? anim_clip(cs, mount_clip) : NULL;
	if (mounted) { // (in the saddle: the rider's clip, the horse's gait's moment; nothing of the feet's own)
		anim_sample(cs, mounted, fmodf(mount_t, mounted->duration), target);
		sneaking = 0, swimming = 0, grounded = 1;
	}
	float combat_move = 0.0f; // how much a combat locomotion clip drives the body (its arms too)
	// Sneaking: Skyrim's crouched idle and walks instead (forward clips: the body turns
	// to its way in third person; in first person the camera drops)
	const Clip *sneak_idle = sneaking ? anim_clip(cs, "sneak_idle") : NULL;
	if (sneak_idle) {
		anim_sample(cs, sneak_idle, a->t, target);
		{ // (Skyrim's crouch looks about; the head kept mostly to the front, where you look)
			BonePose upright[ANIM_MAX_BONES];
			anim_sample(cs, idle, a->t, upright);
			static const int look[] = {B_NECK, B_HEAD};
			for (int k = 0; k < 2; k++)
				if (look[k] < cs->bones)
					anim_blend(&target[look[k]], &upright[look[k]], 1, SNEAK_HEAD_STEADY);
		}
		const Clip *c = anim_clip(cs, speed > SNEAK_RUN_FROM ? "sneak_run" : "sneak_walk");
		if (c && c->speed > 0.1f && speed > 0.15f) {
			a->phase = fmodf(a->phase + speed * dt / (c->speed * c->duration), 1.0f);
			anim_sample(cs, c, a->phase * c->duration, move);
			anim_blend(target, move, cs->bones, fminf(1.0f, speed / 0.4f));
		}
	} else
	// Moving: the cycle for the pace, advancing with the ground covered (feet stay planted);
	// it keeps going through short hops
	if (speed > 0.15f && !mounted) {
		const int run = speed > PLAYER_RUN_FROM, sprint = speed > PLAYER_SPRINT_FROM && fwd > 0.0f;
		const char *name = fabsf(fwd) >= fabsf(side)
		                   ? (fwd > 0.0f ? (sprint ? "sprint" : run ? "run" : "walk") : (run ? "run_back" : "walk_back"))
		                   : (side > 0.0f ? (run ? "run_right" : "walk_right") : (run ? "run_left" : "walk_left"));
		const Clip *c = NULL;
		if (stance && facing_view) { // (Skyrim's combat strafes: the body keeps facing the view)
			char combat[24];
			snprintf(combat, sizeof(combat), "%s_%s", stance, !strcmp(name, "sprint") ? "run" : name);
			c = anim_clip(cs, combat);
			if (c && c->speed > 0.1f)
				combat_move = fminf(1.0f, speed / 0.6f);
		}
		if (!c || c->speed <= 0.1f)
			c = anim_clip(cs, name);
		if (c && c->speed > 0.1f) {
			a->phase = fmodf(a->phase + speed * dt / (c->speed * c->duration), 1.0f);
			anim_sample(cs, c, a->phase * c->duration, move);
			// Skyrim's run and sprint pitch the whole upper body well forward; keep the legs
			// and arms but stand the spine part way back up toward the walk's posture
			const Clip *upright = run && !combat_move ? anim_clip(cs, fwd >= 0.0f ? "walk" : "walk_back") : NULL;
			if (upright) {
				BonePose straight[ANIM_MAX_BONES];
				anim_sample(cs, upright, a->phase * upright->duration, straight);
				static const int spine[] = {B_SPINE, B_SPINE1, B_SPINE2, B_NECK, B_HEAD};
				for (int k = 0; k < 5; k++)
					if (spine[k] < cs->bones)
						anim_blend(&move[spine[k]], &straight[spine[k]], 1, RUN_LEAN_UPRIGHT);
			}
			anim_blend(target, move, cs->bones, fminf(1.0f, speed / 0.6f));
		}
	}
	// Swimming: Skyrim's swim clips (treading water, the stroke paced by the speed)
	const Clip *swim_idle = anim_clip(cs, "swim_idle"), *swim = anim_clip(cs, "swim");
	if (swimming && swim_idle) {
		static float stroke;
		anim_sample(cs, swim_idle, a->t, target);
		if (swim && speed > 0.15f) {
			stroke = fmodf(stroke + dt * fminf(1.5f, speed / SWIM_CLIP_SPEED) / swim->duration, 1.0f);
			anim_sample(cs, swim, stroke * swim->duration, move);
			anim_blend(target, move, cs->bones, fminf(1.0f, speed / 0.6f));
		}
		grounded = 1; // (no falling or landing poses: getting out of the water is a walk)
		vy = 0.0f;
	}
	// Jumps: the take-off clip over the air time, the falling loop if the drop goes on past
	// it, then the clip's own landing. Walking off a ledge only falls (after a moment,
	// so steps and small drops don't), then lands with the landing clip.
	static int was_grounded = 1, jumping;
	static float jump_t, jump_rate = 1.0f, land_t = -1.0f;
	static const Clip *land_clip;
	const Clip *start = anim_clip(cs, "jump_start"), *fall = anim_clip(cs, "jump_fall"), *land = anim_clip(cs, "jump_land");
	if (!grounded && was_grounded) {
		jumping = vy > 1.0f && start; // (leaving the ground going up: a jump)
		player_air_s = 0.0f;
		land_t = -1.0f;
		jump_t = 0.0f;
		// Touch down in the clip when the physics comes back to the take-off height
		jump_rate = jumping ? JUMP_TOUCHDOWN_S / (2.0f * vy / PLAYER_GRAVITY) : 1.0f;
	}
	if (grounded && !was_grounded) {
		if (jumping) {
			land_clip = start, land_t = JUMP_TOUCHDOWN_S; // (also when landing early, up on something)
		} else if (land && player_air_s > LANDING_AFTER_S) {
			land_clip = land, land_t = 0.0f;
		}
	}
	was_grounded = grounded;
	if (!grounded) {
		player_air_s += dt;
		if (jumping) {
			jump_t += dt * jump_rate;
			float t = fminf(jump_t, JUMP_TOUCHDOWN_S);
			anim_sample(cs, start, t, move);
			anim_blend(target, move, cs->bones, fminf(1.0f, player_air_s / JUMP_BLEND_IN_S));
			// Still falling when the clip has come down: ease into the falling loop
			float over = (jump_t - JUMP_TOUCHDOWN_S) / jump_rate;
			if (fall && over > 0.0f) {
				float w = fminf(1.0f, over / FALL_POSE_EASE_S);
				anim_sample(cs, fall, over, move);
				anim_blend(target, move, cs->bones, w * w * (3.0f - 2.0f * w));
			}
		} else if (fall && player_air_s > FALL_POSE_AFTER_S) {
			float since = player_air_s - FALL_POSE_AFTER_S;
			float w = fminf(1.0f, since / FALL_POSE_EASE_S);
			anim_sample(cs, fall, since, move);
			anim_blend(target, move, cs->bones, w * w * (3.0f - 2.0f * w));
		}
	} else if (land_t >= 0.0f && land_clip) {
		float begin = land_clip == start ? JUMP_TOUCHDOWN_S : 0.0f, length = land_clip->duration - begin;
		float u = (land_t - begin) / length;
		if (u < 1.0f) {
			// The landing settles out (less so if you land running)
			float w = (1.0f - u) * (speed > 0.15f ? 0.5f : 1.0f);
			anim_sample(cs, land_clip, fminf(land_t, land_clip->duration - 0.001f), move);
			anim_blend(target, move, cs->bones, w);
			land_t += dt;
		} else {
			land_t = -1.0f;
		}
	}
	// Holding the torch: the left arm from Skyrim's torch clips (up and forward), over
	// whatever the rest of the body does
	if (a->torch_w > 0.001f) {
		const Clip *hold = anim_clip(cs, speed > 0.3f ? "torch_move" : "torch_idle");
		if (hold) {
			anim_sample(cs, hold, a->t, move);
			for (int b = 0; b < cs->bones; b++)
				if (in_left_arm(cs, b))
					anim_blend(&target[b], &move[b], 1, a->torch_w);
		}
	}
	// Readied spells: each casting arm from Skyrim's magic clips, over whatever the rest of
	// the body does (the torch's arm stays on the torch)
	for (int arm = 0; arm < 2; arm++) {
		const int on = (magic_pose[arm] >= 0 || weapon_arm(arm) || player_bound) && !swimming; // (bound: the tied wrists, hands empty or not)
		a->magic_w[arm] += ((on ? 1.0f : 0.0f) - a->magic_w[arm]) * fminf(1.0f, dt * TORCH_RAISE_RATE);
		const Clip *cast = magic_clip(cs, arm);
		if (a->magic_w[arm] < 0.001f || !cast)
			continue;
		// A weapon's arms: the combat locomotion clip already carries them as it moves
		const float arm_w = a->magic_w[arm] * (weapon_arm(arm) && magic_pose[arm] < 0 ? 1.0f - combat_move : 1.0f);
		if (arm_w < 0.001f)
			continue;
		anim_sample(cs, cast, a->t, move);
		// Aiming a spell or holding a weapon ready: the upper body as the clip turns it,
		// relative to the whole body (its arms reach forward from a body leaning into the
		// cast, a bow is held side-on; on the idle's upright spine they'd point at the sky),
		// while the legs keep doing what they do
		// (a spell aimed from the off hand beside a drawn weapon: the cast's lean wins, else that
		// arm would point at the sky from the weapon's upright stance)
		if (!sneaking && ((magic_pose[arm] == 1 && (arm == 1 || magic_pose[1] != 1)) ||
		                  (arm == 1 && weapon_arm(1) && magic_pose[1] < 0 && magic_pose[0] != 1))) {
			BonePose lean = target[B_SPINE];
			match_model_rotation(cs, target, move, B_SPINE, &lean);
			anim_blend(&target[B_SPINE], &lean, 1, arm_w);
			for (int b = B_SPINE1; b <= B_SPINE2; b++)
				anim_blend(&target[b], &move[b], 1, arm_w);
		}
		for (int b = 0; b < cs->bones; b++)
			if (in_arm(cs, b, arm ? B_RCLAV : B_LCLAV))
				anim_blend(&target[b], &move[b], 1, arm_w);
	}
	const Clip *scene = scene_clip[0] ? anim_clip(cs, scene_clip) : NULL;
	if (scene) // (a killmove: its clip is the whole body - nothing of the swing, the stance, the feet)
		anim_sample(cs, scene, fminf(scene_t, scene->duration - 0.01f), target);
	else
		fight_pose(cs, target, dt, speed < 0.3f);
	if (!a->has_pose)
		memcpy(a->pose, target, sizeof(BonePose) * cs->bones);
	else
		anim_blend(a->pose, target, cs->bones, fminf(1.0f, dt * (attack.active ? 30.0f : POSE_SMOOTH)));
	a->has_pose = 1;
}

float actors_player_heading_deg(void) {
	return -player.heading * 180.0f / (float)M_PI;
}

int actors_player_female(void) {
	return player_loaded && player.female == 1;
}

int actors_player_base_items(void) {
	return player_base_items;
}

int actors_player_items(void) {
	return player.nitems;
}

const char *actors_player_item(int i, int *equipped) {
	if (i < 0 || i >= player.nitems)
		return "";
	*equipped = player.items[i].equipped;
	return player.items[i].name;
}

// The first-person arms: where the camera is and looks, and whether they're shown (first
// person, holding the torch). Skyrim's first-person skeleton has its camera at
// FIRST_CAMERA_Y above its origin; the arms are placed so that point is the eye.
void actors_first_update(const float eye[3], float yaw_deg, float pitch_deg, float speed, float dt, int shown) {
	Actor *a = &player1st;
	player1st_shown = 0;
	if (!player1st_loaded || !player_loaded)
		return;
	for (int i = 0; i < a->nitems && i < player.nitems; i++)
		if (a->items[i].equipped != player.items[i].equipped && !player.items[i].part) { // (the head's parts: not in first person)
			a->items[i].equipped = player.items[i].equipped;
			for (int b = 0; b < a->nbatches; b++)
				a->batches[b].lit = 0;
		}
	const int torch = a->torch_item >= 0 && a->items[a->torch_item].equipped;
	const int magic = player_bound || magic_pose[1] >= 0 || (magic_pose[0] >= 0 && !torch) || weapon_arm(1); // (bound: the tied wrists before you)
	if (!shown || (!torch && !magic))
		return;
	player1st_shown = 1;
	a->heading = -yaw_deg * (float)M_PI / 180.0f;
	a->pitch = pitch_deg * (float)M_PI / 180.0f;
	float ch = cosf(a->heading), sh = sinf(a->heading), cp = cosf(a->pitch), sp = sinf(a->pitch);
	// eye - R * (0, FIRST_CAMERA_Y, 0), R = turn x tilt (its middle column)
	a->x = eye[0] - sh * sp * FIRST_CAMERA_Y;
	a->y = eye[1] - cp * FIRST_CAMERA_Y;
	a->z = eye[2] - ch * sp * FIRST_CAMERA_Y;
	a->t += dt;
	const ClipSet *cs = &clip_sets[CLIPS_FIRST];
	const Clip *c = NULL, *arm_clip[2] = {torch ? NULL : magic_clip(cs, 0), magic_clip(cs, 1)};
	const Clip *cast = arm_clip[1] ? arm_clip[1] : arm_clip[0];
	if (torch) {
		c = anim_clip(cs, speed > PLAYER_RUN_FROM ? "torch_run" : speed > 0.3f ? "torch_walk" : "torch_idle");
		if (!c)
			c = anim_clip(cs, "torch_idle");
	}
	if (!c && !cast) {
		player1st_shown = 0;
		return;
	}
	// The torch's clip (its arm), each casting arm from its own clip over it
	BonePose target[ANIM_MAX_BONES], move[ANIM_MAX_BONES];
	anim_sample(cs, c ? c : cast, a->t, target);
	for (int arm = 0; arm < 2; arm++) {
		if (!arm_clip[arm] || (arm_clip[arm] == cast && !c))
			continue; // (no spell, or the clip the arms already have)
		anim_sample(cs, arm_clip[arm], a->t, move);
		for (int b = 0; b < cs->bones; b++)
			if (in_arm(cs, b, arm ? B_RCLAV : B_LCLAV))
				target[b] = move[b];
	}
	// Walking with a weapon drawn: Skyrim's first-person combat walk and run move its arms
	int stepped = 0;
	if (weapon_arm(1) && magic_pose[1] < 0 && speed > 0.3f) {
		static float step;
		char name[24];
		snprintf(name, sizeof(name), "%s_%s", combat_set(), speed > PLAYER_RUN_FROM ? "run" : "walk");
		const Clip *w = anim_clip(cs, name);
		if (w && w->duration > 0.0f) {
			step = fmodf(step + dt / w->duration, 1.0f);
			anim_sample(cs, w, step * w->duration, move);
			const float k = fminf(1.0f, speed / 1.0f);
			for (int arm = 0; arm < 2; arm++)
				if (weapon_arm(arm) && !(arm == 0 && torch))
					for (int b = 0; b < cs->bones; b++)
						if (in_arm(cs, b, arm ? B_RCLAV : B_LCLAV))
							anim_blend(&target[b], &move[b], 1, k);
			stepped = 1;
		}
	}
	// (walking: the arms sway a little with the stride)
	if (cast && !c && !stepped && speed > 0.3f) {
		float sway = sinf(a->t * (speed > PLAYER_RUN_FROM ? 9.0f : 6.0f)) * fminf(1.0f, speed / 3.0f) * 0.012f;
		target[0].t[1] += fabsf(sway), target[0].t[0] += sway;
	}
	fight_pose(cs, target, 0.0f, 0); // (the same swing or block, the first-person arms' clip)
	if (!a->has_pose)
		memcpy(a->pose, target, sizeof(BonePose) * cs->bones);
	else
		anim_blend(a->pose, target, cs->bones, fminf(1.0f, dt * (attack.active ? 30.0f : 6.0f)));
	a->has_pose = 1;
}

void actors_draw_first(void) {
	if (!player1st_shown)
		return;
	skin(&player1st);
	int count = 0;
	emit(&player1st, &count);
	objects_draw_dynamic(drawn, count);
}

int actors_player_torch(float out[3]) {
	if (player1st_shown && player1st.torch_item >= 0 && player1st.items[player1st.torch_item].equipped &&
	    player1st.torch_at_frame == skin_frame) {
		memcpy(out, player1st.torch_at, 3 * sizeof(float));
		return 2;
	}
	const Actor *a = &player;
	if (!player_loaded || a->torch_item < 0 || !a->items[a->torch_item].equipped)
		return 0;
	if (!player_shown || a->torch_at_frame != skin_frame)
		return 1; // held, but not drawn this frame (first person): the caller places it
	memcpy(out, a->torch_at, 3 * sizeof(float));
	return 2;
}

void actors_player_set_worn(int i, int on) {
	if (i >= 0 && i < player.nitems && !player.items[i].equipped != !on)
		actors_player_toggle(i);
}

// A gear file (converter/gear.py) onto one of the player's actors as item `item`
static int add_gear(Actor *a, const uint8_t *d, long size, int first_person, int item) {
	Cur c = {d, d + size, 1};
	uint32_t n = 0;
	take(&c, NULL, 12); // (header)
	take(&c, &n, 4);
	if (!c.ok || n > MAX_BATCH_LIST)
		return 0;
	if (!first_person)
		return read_batches(&c, a, n, item);
	Cur skip = c;
	Actor tmp; // (the third-person batches are skipped over by reading them into a scratch actor)
	memset(&tmp, 0, sizeof(tmp));
	tmp.nbones = MAX_BONES;
	tmp.items[0].weapon = a->items[item].weapon;
	if (!read_batches(&skip, &tmp, n, 0))
		return 0;
	for (int i = 0; i < tmp.nbatches; i++) {
		free(tmp.batches[i].src), free(tmp.batches[i].idx), free(tmp.batches[i].out), free(tmp.batches[i].mouth);
	}
	free(tmp.batches);
	take(&skip, &n, 4);
	return skip.ok && n <= MAX_BATCH_LIST && read_batches(&skip, a, n, item);
}

void actors_player_set_part(int item, uint32_t hidden_by) {
	if (item < 0 || item >= player.nitems)
		return;
	for (int k = 0; k < 2; k++) {
		Actor *a = k ? &player1st : &player;
		if (item < a->nitems)
			a->items[item].hidden_by = hidden_by, a->items[item].part = 1, a->items[item].equipped = !k; // (not on the first-person arms)
	}
}

void actors_player_drop_gear(int item) {
	if (item < player_base_items || item >= player.nitems || gear_slot_free[item])
		return;
	for (int k = 0; k < 2; k++) {
		Actor *a = k ? &player1st : &player;
		int kept = 0;
		for (int i = 0; i < a->nbatches; i++) {
			ABatch *b = &a->batches[i];
			if (b->part == item + 1) {
				free_batch(b);
				continue;
			}
			a->batches[kept++] = *b;
		}
		a->nbatches = kept;
		if (item < a->nitems)
			a->items[item].equipped = 0, a->items[item].name[0] = 0;
	}
	gear_slot_free[item] = 1;
}

int actors_player_add_gear(const char *path, const char *name, uint32_t slots, int weapon) {
	int item = player.nitems; // (a slot given back first)
	for (int i = player_base_items; i < player.nitems; i++)
		if (gear_slot_free[i]) {
			item = i;
			break;
		}
	if (!player_loaded || item >= MAX_ITEMS)
		return -1;
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	if (!d || size < 16 || memcmp(d, "SKGR", 4)) {
		free(d);
		return -1;
	}
	for (int k = 0; k < 2; k++) {
		Actor *a = k ? &player1st : &player;
		if (k && !player1st_loaded)
			continue;
		snprintf(a->items[item].name, sizeof(a->items[0].name), "%s", name);
		a->items[item].slots = slots, a->items[item].equipped = 0, a->items[item].weapon = weapon; a->items[item].tint = 0, a->items[item].glow = 0;
		a->items[item].hidden_by = 0, a->items[item].part = 0;
		const int first = a->nbatches;
		if (!add_gear(a, d, size, k, item)) {
			free(d);
			return -1;
		}
		load_look(a, path, k, first, a->nbatches - first); // (its thin build, a head part's morphs and hair)
		if (look_morph_count)
			apply_look_range(a, first, a->nbatches - first);
		if (a->nitems < item + 1)
			a->nitems = item + 1;
	}
	free(d);
	gear_slot_free[item] = 0;
	return item;
}

void actors_player_weapon_drawn(int drawn) {
	weapon_drawn = drawn;
}

void actors_player_left_weapon(int kind) {
	left_kind = kind;
}

void actors_player_weapon(int kind) {
	weapon_kind = kind;
}

void actors_player_toggle(int i) {
	if (i < 0 || i >= player.nitems)
		return;
	int on = !player.items[i].equipped;
	if (on) // whatever else uses those body slots comes off
		for (int k = 0; k < player.nitems; k++)
			if (k != i && player.items[k].slots & player.items[i].slots)
				player.items[k].equipped = 0;
	player.items[i].equipped = on;
	for (int b = 0; b < player.nbatches; b++)
		player.batches[b].lit = 0; // (re-lit when next shown)
}

void actors_player_glow(int item, unsigned rgb) {
	for (int k = 0; k < 2; k++) {
		Actor *a = k ? &player1st : &player;
		if (item < 0 || item >= a->nitems || a->items[item].glow == rgb)
			continue;
		a->items[item].glow = rgb;
		for (int b = 0; b < a->nbatches; b++)
			if (a->batches[b].part == item + 1)
				a->batches[b].lit = 0;
	}
}

void actors_player_tint(int item, int on) {
	for (int k = 0; k < 2; k++) {
		Actor *a = k ? &player1st : &player;
		if (item < 0 || item >= a->nitems || a->items[item].tint == on)
			continue;
		a->items[item].tint = on;
		for (int b = 0; b < a->nbatches; b++)
			if (a->batches[b].part == item + 1)
				a->batches[b].lit = 0;
	}
}
