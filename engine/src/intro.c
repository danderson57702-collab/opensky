#include "intro.h"
#include "navmesh.h"
#include "collision.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>

#include "activators.h"
#include "actors.h"
#include "creatures.h"
#include "doors.h"
#include "equip.h"
#include "fileio.h"
#include "gamelog.h"
#include "interior.h"
#include "items.h"
#include "lighting.h"
#include "locks.h"
#include "objects.h"
#include "stream.h"
#include "projectiles.h"
#include "quests.h"
#include "races.h"
#include "sfx.h"
#include "spellfx.h"
#include "talk.h"
#include "terrain.h"
#include "ui.h"
#include "voice.h"
#include "gametext.h"

#define MAX_LINES 640
#define MAX_ACTIONS 400
#define MAX_MARKERS 900
#define MAX_ROUTE 400
#define SCENE_MAX_LINES 96
#define LINE_GAP_S 0.35f
#define PHASE_MAX_S 8.0f       // a phase waits this long at most for walkers (not for you)
#define CART_SPEED 2.6f        // m/s along the road (Skyrim's carts amble)
#define CART_FAST 4.4f         // (at most: the talk on the cart is shorter than our road at a walk)
#define CART_GREET_M 28.0f     // INFO 000F778A this far before Helgen's east gate (INFO 000F5E78 at it)
// The opening, as Skyrim's (the user's reference video, to the second - Nov 10): black; "Bethesda Game Studios" 3.5-7.5 s;
// "Presents" 9.5-12.5 s; the ride fades in from 14 s, slowly, to 32 s; INFO 020181BC over it 21.5-25.5 s; the
// game's name 27-31.5 s; INFO 00101AEA at 48 s
#define OPEN_BLACK_S 14.0f      // the ride begins in black this long...
#define OPEN_FADE_END_S 32.0f   // ...and fades in slowly till here
#define OPEN_SOONER_S 0.0f      // (the cart from its road's start)
#define CART_FIRST_LINE_S 48.0f // INFO 00101AEA
#define CARD_FADE_S 0.9f
static const float OPEN_CARDS[4][2] = {{3.5f, 7.6f}, {9.4f, 12.6f}, {21.4f, 25.6f}, {27.0f, 31.6f}}; // (each card: from, to)
#define OPEN_LOOK_TURN 1.22f    // (the first view from your seat, across the cart: turned this far toward its front)
#define CART_TOWN_SPEED 1.75f   // m/s through Helgen after the greeting (the gate to the square in ~80 s)
#define ARRIVE_LINES_M 70.0f   // Helgen's lines this far before the cart stops
#define FADE_S 0.8f
#define FOLLOW_GATE_M 6.0f     // a phase led by Ralof / Hadvar waits till you're this near them
#define JUMP_NEAR_M 3.0f       // the tower's broken wall: this near, the jump
#define JUMP_S 1.1f            // ... across into the inn's roof
#define GLIDE_SPEED 1.4f       // the player walked by the scene (to the line, to the block)
#define RIVERWOOD_NEAR_M 70.0f // the follower walks you this near Riverwood
#define PERCH_ABOVE_M 14.0f   // Alduin comes down onto a perch from this far above it
#define MEET_TOGETHER_M 3.0f   // (the greeting: Ralof and Gerdur this near - met, not calling across the river)
#define HOD_COME_S 30.0f       // (... Hod, down from the mill, there by this long, if stuck on the way)
#define CLEARING_THERE_M 4.0f  // (Riverwood: Gerdur this near her mark at the clearing - there)
#define CLEARING_WAIT_S 45.0f  // (... waited for this long at most)
#define MEET_WAIT_S 5.0f       // ... walked to each other this long at most; then she's stepped up to him
#define MEET_NEAR_M 4.0f       // ... and introduces you to Gerdur / Alvor this near them
#define ALDUIN_SPEED 17.0f
#define ALDUIN_RING_M 45.0f    // Alduin between his packages: circling Helgen this far out...
#define ALDUIN_HEIGHT_M 26.0f  // ...this high
#define BREATH_EVERY_S 0.45f
#define ALDUIN_BODY_UP_M 2.5f  // (perched: his body's middle this far over his feet - the view's on it, not his feet)
#define HEAD_REST_M 1.55f      // (a standing body's head this far over its feet)
#define BLOCK_BODY_M 0.95f     // (the first prisoner lies this far to the block's right)
#define BLOCK_EYE_FWD_M 0.85f  // (kneeling, your eyes this far ahead of where you kneel - out over the stone, the basket below
#define BLOCK_EYE_UP_M 0.62f   //  them, as the reference's 7:00; at 0.35 they were behind the stone, and it filled the view)
#define BLOCK_BASKET_M 1.29f     // (Skyrim's CrateOpen by the block, and the head in it: this far ahead of the block, to its right (-: left), this high)
#define BLOCK_BASKET_SIDE_M -0.61f
#define BLOCK_BASKET_UP_M 0.02f
#define KEEP_DOOR_LATEST_S 150.0f // (the door opens this long after the cuffs at the latest - should the ambush at the gate not play)
#define KEEP_LOCKED_DOOR 0x0009B0A5u // (the keep's 255-level door: its key is nowhere in the opening)
#define KEEP_GATE_NEAR_M 7.0f        // (the keep's first room: the Imperials come to its gate as you go to it...)
#define KEEP_IMPERIALS_LATEST_S 150.0f // (... or this long after Gunjar's gear was yours)
#define KEEP_ENTER_PROMPT_M 3.5f  // (the keep's door, outside: "Enter" this near the one you follow's mark by it...)
#define KEEP_ENTER_M 1.6f         // (... and through it walking into it)
#define KEEP_ENTER_LATEST_S 600.0f
#define KEEP_DOOR_NEAR_M 40.0f // (out of the keep this near its doors: back the way you came)
#define FOLLOWER_STRIKE_S 1.6f // (Ralof / Hadvar in a fight: a blow this often...)
#define FOLLOWER_REACH_M 2.8f  // (...at whoever is this near them...)
#define FOLLOWER_DAMAGE 14.0f  // (...this hard)
#define LEAD_LOST_M 25.0f      // (the one you follow this far off: brought beside you)
#define LEAD_STUCK_S 30.0f     // (... or this long on one stretch of the way)
#define LEAD_GO_M 5.0f         // (the one you follow sets off again with you this near...)
#define LEAD_WAIT_M 9.0f       // (...and waits for you past this)
#define WALL_HOLD_S 4.5f   // (Alduin on the wall over you: the view held up at him this long)
#define TOWER_HEAD_S 3.0f
#define TOWER_HEAD_OUT_M 10.0f // (... clinging this far outside the wall: his head in the hole, not his body in the room)  // (his head in the broken wall, breathing fire through it, this long)
#define TOWER_BURST_S 2.2f // (the wall's burst: the view held on it this long)
#define KEEP_CUT_S 7.0f
#define MEET_LEAVE_M 30.0f     // (Riverwood: they go back to their day once you're this far off)
#define BRIDGE_PAST_Z 88.0f    // (Helgen Keep: past its bridge - over it, the way on to the caves)        // (in the keep: the bindings cut this long in - INFO 00107D89)
#define LOKIR_WATCH_S 3.0f     // (Lokir down: looked at this long before INFO 0003F88B)      // (his fire through the tower's wall: this long)
#define TOWER_PERCH_OUT_M 6.5f // (Alduin at the tower's broken wall: this far outside it)
#define METEOR_EVERY_S 2.5f    // (his shout's burning rocks, while the town burns)
#define METEOR_HIGH_M 45.0f    // (... falling from this high over their mark,
#define METEOR_SLANT_M 14.0f   //  slanting in from this far aside,
#define METEOR_SPEED 38.0f     //  this fast)
#define OPENING_HOUR 8.0f
#define SWITCH_ATTACK 1 // (converter/esm.py SWITCHES: Helgen as Alduin leaves it...)
#define SWITCH_RUINS 2  // (... and its ruins after)

enum {
	ST_OFF, ST_CART, ST_LINEUP, ST_RACE, ST_EXECUTION, ST_BLOCK, ST_BLACKOUT, ST_TOWER, ST_LEAP, ST_TOWN, ST_CHOICE,
	ST_TOKEEP, ST_KEEP, ST_RIVERWOOD, ST_MEET, ST_DONE
};
enum { WITH_NONE, WITH_RALOF, WITH_HADVAR };

typedef struct {
	char scene[40], speaker[40], cond[48], voice[96];
	int phase, action, variant;
	char *text;
} Line;

typedef struct {
	char scene[40], who[40], package[64], targets[240];
	int start, end, index;
} Action;

typedef struct {
	char name[48];
	float pos[3], heading;
} Marker;

static Line lines[MAX_LINES];
static int line_count;
static Action actions[MAX_ACTIONS];
static int action_count;
static Marker markers[MAX_MARKERS];
static int marker_count;
static float route[MAX_ROUTE][3], route_len[MAX_ROUTE];
static int route_n, cart_model = -1;
static int loaded;

static int stage, with, chosen_answer = -1;
static float stage_t, fade, fade_to;
static int race_chosen, jump_teleport;
static int lying; // (knocked down by the shout: on the ground till Ralof's "Get up!")
static float lie_at[3], lie_heading;
static float face_yaw, face_pitch, face_left; // (a view set: held a moment, past the loading after a move)
static int look_on;                           // (the view drawn toward look_at: what's happening)
static float look_at[3];
static float ear[3];                          // where the player is (speakers farther are heard, not seen)

static void face(float heading_rad, float pitch_deg) {
	face_yaw = -heading_rad * 180.0f / (float)M_PI, face_pitch = pitch_deg, face_left = 0.4f;
}

static void go(int s) {
	stage = s, stage_t = 0.0f;
	game_log("intro: stage %d", s);
}

// --- Data ----------------------------------------------------------------------------------

static char *field(char **p) {
	char *s = *p, *tab = strchr(s, '\t');
	if (tab)
		*tab = 0, *p = tab + 1;
	else
		*p = s + strlen(s);
	return s;
}

static const Marker *marker(const char *name);

void intro_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%squests.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size);
	if (!text)
		return;
	for (char *line = text, *end = text + size; line < end;) {
		char *nl = memchr(line, '\n', end - line);
		if (!nl)
			nl = end;
		*nl = 0;
		char *p = line;
		const char *kind = field(&p);
		if (kind[0] == 'C' && !kind[1] && line_count < MAX_LINES) {
			Line *l = &lines[line_count];
			snprintf(l->scene, sizeof(l->scene), "%s", field(&p));
			l->phase = atoi(field(&p)), l->action = atoi(field(&p)), l->variant = atoi(field(&p));
			snprintf(l->speaker, sizeof(l->speaker), "%s", field(&p));
			snprintf(l->cond, sizeof(l->cond), "%s", field(&p));
			snprintf(l->voice, sizeof(l->voice), "%s", field(&p));
			l->text = strdup(p);
			line_count++;
		} else if (kind[0] == 'A' && !kind[1] && action_count < MAX_ACTIONS) {
			Action *a = &actions[action_count++];
			snprintf(a->scene, sizeof(a->scene), "%s", field(&p));
			a->start = atoi(field(&p)), a->end = atoi(field(&p)), a->index = atoi(field(&p));
			snprintf(a->who, sizeof(a->who), "%s", field(&p));
			snprintf(a->package, sizeof(a->package), "%s", field(&p));
			snprintf(a->targets, sizeof(a->targets), "%s", field(&p));
		} else if (kind[0] == 'P' && !kind[1] && marker_count < MAX_MARKERS) {
			Marker *m = &markers[marker_count++];
			snprintf(m->name, sizeof(m->name), "%s", field(&p));
			for (int k = 0; k < 3; k++)
				m->pos[k] = (float)atof(field(&p));
			m->heading = (float)atof(field(&p));
		} else if (kind[0] == 'W' && !kind[1] && route_n < MAX_ROUTE) {
			field(&p);
			for (int k = 0; k < 3; k++)
				route[route_n][k] = (float)atof(field(&p));
			route_len[route_n] = route_n ? route_len[route_n - 1] + hypotf(route[route_n][0] - route[route_n - 1][0],
			                                                               route[route_n][2] - route[route_n - 1][2])
			                             : 0.0f;
			route_n++;
		} else if (kind[0] == 'O' && !kind[1] && !strcmp(field(&p), "cart")) {
			cart_model = atoi(field(&p));
		}
		line = nl + 1;
	}
	free(text);
	// The cart's road inside the gate, as Skyrim's markers lay it (the navmesh's way round the north
	// lane ended on the wrong cart's spot, facing south): through the east gate, down south-west to the
	// carts' queue (mq101cart2marker4start), west along the square, then north - mq101cart2marker5start
	// and on to where the player's cart (the second) parks beside the first, facing north
	{
		const Marker *mid_m = marker("mq101cart2marker5start"), *end_m = marker("mq101cart2disabled");
		int g = -1;
		float best = 1e9f;
		for (int k = 0; k < route_n; k++) {
			const float d = hypotf(route[k][0] - 286.2f, route[k][2] - 1135.0f);
			if (d < best)
				best = d, g = k;
		}
		if (mid_m && end_m && g > 0 && best < 6.0f) {
			const float via_xz[][2] = {{279.5f, 1146.0f}, {272.0f, 1159.0f}, {262.0f, 1165.0f}, {252.0f, 1168.0f},
			                          {240.0f, 1170.5f}, {229.0f, 1172.0f}, {219.0f, 1170.0f}};
			const size_t nvia = sizeof(via_xz) / sizeof(via_xz[0]);
			route_n = g + 1;
			for (size_t k = 0; k < nvia + 2 && route_n < MAX_ROUTE; k++) {
				const float *xz = k < nvia ? via_xz[k] : k == nvia ? mid_m->pos : end_m->pos;
				const float x = k < nvia ? xz[0] : xz[0], z = k < nvia ? xz[1] : xz[2];
				route[route_n][0] = x, route[route_n][1] = end_m->pos[1], route[route_n][2] = z;
				route_len[route_n] = route_len[route_n - 1] + hypotf(x - route[route_n - 1][0], z - route[route_n - 1][2]);
				route_n++;
			}
		}
	}
	loaded = line_count > 0 && route_n > 1;
	game_log("intro: %d lines, %d moves, %d markers, the cart's road %d points (%.0f m)", line_count, action_count,
	         marker_count, route_n, route_n ? route_len[route_n - 1] : 0.0f);
}

static const Marker *marker(const char *name) {
	for (int i = 0; i < marker_count; i++)
		if (!strcmp(markers[i].name, name))
			return &markers[i];
	return NULL;
}

// The k-th of a package's targets ("a,b,c"), as a marker
static const Marker *target(const Action *a, int k) {
	char buf[240];
	snprintf(buf, sizeof(buf), "%s", a->targets);
	char *save = NULL;
	for (char *t = strtok_r(buf, ",", &save); t; t = strtok_r(NULL, ",", &save), k--)
		if (k == 0)
			return marker(t);
	return NULL;
}

// --- The cast ------------------------------------------------------------------------------

// Skyrim's quest aliases with no NPC of their own (or whose NPC is a generic one here): who
// plays them, and their name in the subtitles when nobody does
static const struct { const char *alias, *actor, *name; } ALIASES[] = {
	{"imperialsoldierfort01", "mq101imperialsoldierofficer", T_NPC_000AAFAC_FULL0},
	{"imperialsoldier01", "mq101imperialsoldier01", T_NPC_0001FC5D_FULL0},
	{"imperialsoldier02", "mq101imperialsoldier02", T_NPC_0001FC5D_FULL0},
	{"imperialsoldierhelgen02", "mq101imperialsoldier03", T_NPC_0001FC5D_FULL0},
	{"imperialsoldierfodderc01", "mq101imperialsoldier04", T_NPC_0001FC5D_FULL0},
	{"duncgimperialsoldierfodderc01", "mq101imperialsoldier04", T_NPC_0001FC5D_FULL0},
	{"stormcloakprisoner01", "mq101stormcloakcartmalebroder", T_NPC_00027498_FULL0},
	{"stormcloakprisoner02", "mq101stormcloakcartfemale", T_NPC_00027498_FULL0},
	{"stormcloakprisoner03", "mq101stormcloakcartmale", T_NPC_00027498_FULL0},
	{"stormcloakprisoner04", "mq101stormcloakcartmale02", T_NPC_00027498_FULL0},
	{"stormcloaktowera", "mq101stormcloakprisonermalenord", T_NPC_00027498_FULL0},
	{"civiliangunnar", "gunnarstoneeye", "Gunnar"},
	{"civiliantorolf", "torolf", T_NPC_00013641_FULL0},
	{"civilianvilod", "vilod", T_NPC_0001363E_FULL0},
	{"civilianmatlara", "matlara", T_NPC_00013640_FULL0},
	{"civilianingrid", "ingrid", T_NPC_0001363F_FULL0},
	{"civilianhaming", "haming", T_NPC_00013642_FULL0},
	{"civiliantorri", NULL, "Torri"},
	{"priest", "mq101imperialpriest", "Priest of Arkay"},
	{"headsman", "mq101headsman", "Headsman"},
	{"prisoner01", "mq101prisoner", T_NPC_0004679A_FULL0},
	{"helgenarcher01", NULL, T_NPC_00045BE0_FULL0},
	{"helgenarcher02", NULL, T_NPC_00045BE0_FULL0},
	{"barracksroomsoldier01", NULL, T_NPC_0001FC5D_FULL0},
	{"generaltullius", "generaltullius", T_NPC_0001327E_FULL0},
	{"ulfric", "ulfric", T_NPC_0001414D_FULL0},
	{"elenwen", "elenwen", T_NPC_00013269_FULL0},
	{"mq101headsman", "mq101headsman", "Headsman"},
	{"mq101imperialpriest", "mq101imperialpriest", "Priest of Arkay"},
};

static int actor_id(const char *id) {
	for (int i = 0; i < actors_count(); i++)
		if (!strcmp(actors_id(i), id))
			return i;
	return -1;
}

static int cast(const char *speaker, const char **name) {
	*name = "Soldier";
	for (size_t k = 0; k < sizeof(ALIASES) / sizeof(ALIASES[0]); k++)
		if (!strcmp(ALIASES[k].alias, speaker)) {
			*name = ALIASES[k].name;
			return ALIASES[k].actor ? actor_id(ALIASES[k].actor) : -1;
		}
	return actor_id(speaker);
}

static int who(const char *id) {
	const char *name;
	return cast(id, &name);
}

// Someone taken over, placed at a marker in a clip
static void place_headsman(void);
static int place(const char *id, const char *mark, const char *clip) {
	const int i = who(id);
	const Marker *m = marker(mark);
	if (i < 0 || !m)
		return i;
	actors_script(i, 1);
	actors_script_place(i, m->pos, m->heading);
	actors_script_clip(i, clip, 0);
	return i;
}

static void hide(const char *id) {
	const int i = who(id);
	const float away[3] = {0.0f, -500.0f, 0.0f};
	if (i >= 0)
		actors_script(i, 1), actors_script_place(i, away, 0.0f);
}

static void look_toward(const float at[3]) {
	look_on = 1;
	memcpy(look_at, at, sizeof(look_at));
}

static void look_toward_actor(const char *id, float up) {
	float at[3];
	const int i = who(id);
	if (i >= 0 && actors_where(i, at))
		at[1] += up, look_toward(at);
}

// Everyone the opening moves (released to their schedules - or out of sight - at its end)
static const char *const CAST[] = {"ralof", "hadvar", "mq101prisoner", "ulfric", "generaltullius", "elenwen",
                                   "mq101headsman", "mq101imperialpriest", "mq101imperialsoldierofficer",
                                   "mq101imperialsoldier01", "mq101imperialsoldier02", "mq101imperialsoldier03",
                                   "mq101imperialsoldier04", "mq101stormcloakcartmalebroder", "mq101stormcloakcartfemale",
                                   "mq101stormcloakcartmale", "mq101stormcloakcartmale02", "mq101stormcloakprisonermalenord"};

static void release_cast(const char *keep) {
	for (size_t k = 0; k < sizeof(CAST) / sizeof(CAST[0]); k++) {
		if (keep && !strcmp(CAST[k], keep))
			continue;
		const int i = actor_id(CAST[k]);
		if (i < 0)
			continue;
		if (!strcmp(CAST[k], "ralof") || !strcmp(CAST[k], "hadvar"))
			actors_script(i, 0); // (to Riverwood, by their schedules)
		else
			hide(CAST[k]); // (Helgen's done with: out of sight)
	}
}

// --- Speech: Skyrim's lines in their order, voiced (or heard, from someone not here) ---------

static struct {
	int line[SCENE_MAX_LINES];
	int n, i, offscreen;
	float gap;
	int speaker, listener; // (whose line it is, to whom - -1: you: their heads kept turned to each other while it lasts)
} sc;

static int matches(const char *cond) {
	if (!strcmp(cond, "-"))
		return 1;
	const Race *r = races_get(races_player());
	char buf[48];
	snprintf(buf, sizeof(buf), "%s", cond);
	char *save = NULL;
	for (char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		const int not = tok[0] == '!';
		const char *what = tok + not;
		int is;
		if (!strcmp(what, "female"))
			is = actors_player_female();
		else if (!strcmp(what, "male"))
			is = !actors_player_female();
		else {
			char eid[24] = "";
			for (int k = 0; r && r->eid[k] && k < 23; k++)
				eid[k] = (char)(r->eid[k] | 0x20), eid[k + 1] = 0;
			is = !strcmp(eid, what);
		}
		if (is == not)
			return 0;
	}
	return 1;
}

// The lines of a scene's phases [from, to] onto the queue: for each of its actions the first
// line meant for this player (a race's own, Skyrim's "he" or "she") - or (last) the last such
// variant, Skyrim's for the way it went here - all its responses in turn
static void queue_lines(const char *name, int from, int to, int last) {
	if (sc.i >= sc.n)
		sc.n = sc.i = 0;
	int last_phase = -1, last_action = -1, chosen = -1;
	for (int i = 0; i < line_count && sc.n < SCENE_MAX_LINES; i++) {
		const Line *l = &lines[i];
		if (strcmp(l->scene, name) || l->phase < from || l->phase > to)
			continue;
		if (l->phase != last_phase || l->action != last_action)
			last_phase = l->phase, last_action = l->action, chosen = -1;
		if (chosen >= 0 && l->variant != chosen && !(last && l->variant > chosen && matches(l->cond)))
			continue;
		if (!matches(l->cond))
			continue;
		if (chosen >= 0 && l->variant != chosen) // (a later variant: instead of the one taken)
			while (sc.n > sc.i && lines[sc.line[sc.n - 1]].phase == l->phase && lines[sc.line[sc.n - 1]].action == l->action)
				sc.n--;
		chosen = l->variant;
		sc.line[sc.n++] = i;
	}
}

static void scene(const char *name, int from, int to) {
	sc.n = sc.i = 0;
	queue_lines(name, from, to, 0);
	game_log("intro: lines %s %d..%d: %d", name, from, to, sc.n);
}

static void scene_variants(const char *name, int from, int to, int last) {
	sc.n = sc.i = 0;
	queue_lines(name, from, to, last);
}

#define SCENE_HEAR_M 14.0f

static int speaking(void) {
	return sc.i < sc.n || talk_speaking() >= 0 || voice_playing();
}

// The line being said: the speaker's head kept on whom they talk to (and theirs on the speaker) every
// frame - set once as the line began, it went stale as the cart turned under them, or they walked
static void heads_step(void) {
	float at[3], ear_head[3] = {ear[0], ear[1] + 1.6f, ear[2]};
	if (sc.speaker < 0)
		return;
	if (sc.listener >= 0 && actors_where(sc.listener, at)) {
		at[1] += 1.6f;
		actors_script_look_at(sc.speaker, at);
		if (actors_where(sc.speaker, at))
			actors_script_look_at(sc.listener, at);
	} else {
		actors_script_look_at(sc.speaker, ear_head);
	}
}

// Lines said to you, not to the one who spoke before (Skyrim's scenes face them at the player):
// on the cart, Ralof's "Hey, you" and Lokir's INFO 00101AE4; Ralof's INFO 000AA799
static int said_to_you(const Line *l) {
	return (!strcmp(l->scene, "mq101scene1") && (l->phase == 4 || l->phase == 5 || l->phase == 7)) ||
	       (!strcmp(l->scene, "mq101scene3") && l->phase <= 2) ||
	       (!strcmp(l->scene, "mq101scene4") && !strcmp(l->speaker, "hadvar") && l->phase >= 14);
}

static void speech_step(float dt) {
	if (talk_speaking() >= 0 || voice_playing()) {
		heads_step();
		return;
	}
	if (sc.speaker >= 0) // (the line's done: heads back)
		actors_script_look_at(sc.speaker, NULL), actors_script_look_at(sc.listener, NULL), sc.speaker = sc.listener = -1;
	if (sc.offscreen)
		ui_subtitle(NULL, NULL), sc.offscreen = 0;
	if (sc.i >= sc.n || (sc.gap -= dt) > 0.0f)
		return;
	const Line *l = &lines[sc.line[sc.i++]];
	const char *name;
	const int i = cast(l->speaker, &name);
	const char *path = l->voice, *text = l->text;
	float at[3];
	if (!l->text[0]) { // (a grunt with no words: Skyrim's are silent here)
		sc.gap = 0.0f;
		return;
	}
	// Whom they're talking to: the one who spoke before them (or speaks next); they turn their
	// head to them, and that one to the speaker - else to you
	int listener = said_to_you(l) ? -2 : -1;
	for (int k = sc.i - 2; k >= 0 && listener == -1; k--) {
		const char *other;
		const int j = cast(lines[sc.line[k]].speaker, &other);
		if (j != i)
			listener = j;
	}
	for (int k = sc.i; k < sc.n && listener == -1; k++) {
		const char *other;
		const int j = cast(lines[sc.line[k]].speaker, &other);
		if (j != i)
			listener = j;
	}
	if (listener < -1)
		listener = -1;
	if (strstr(l->voice, "_000f5e78_")) // (Ralof on the cart, of the General: INFO 000F5E78 - turned to him, as the reference)
		listener = who("generaltullius");
	else if (strstr(l->voice, "_000f778c_")) // (and of those with him: INFO 000F778C)
		listener = who("elenwen");
	sc.speaker = i, sc.listener = listener;
	game_log("intro: %s says \"%.70s\"%s", name, text ? text : "", listener >= 0 ? " (to someone)" : "");
	float there[3];
	if (i >= 0 && listener >= 0 && actors_where(listener, there)) {
		float self[3];
		// (standing still, in no clip of their own - not sat, not kneeling: the speaker turns to them,
		// not only the head - Gerdur talked to Ralof with her back to him)
		if (!actors_script_walking(i) && !actors_script_has_clip(i) && actors_where(i, self) &&
		    hypotf(there[0] - self[0], there[2] - self[2]) < 6.0f)
			actors_script_face(i, atan2f(-(there[0] - self[0]), -(there[2] - self[2])));
		there[1] += 1.6f;
		actors_script_look_at(i, there);
		if (actors_where(i, at))
			actors_script_look_at(listener, at);
	} else if (i >= 0) {
		actors_script_look_at(i, ear);
	}
	if (i >= 0 && talk_named(i) && actors_shown(i, at) && hypotf(at[0] - ear[0], at[2] - ear[2]) < SCENE_HEAR_M) {
		talk_scene(i, &path, (const char *const *)&text, 1);
	} else { // (no one here plays them: heard, with their name)
		voice_play(path);
		voice_set_mix(1.0f, 0.0f);
		ui_subtitle(talk_named(i) ? talk_name(i) : name, text);
		sc.offscreen = 1;
	}
	sc.gap = LINE_GAP_S;
}

// --- Alduin: following his packages (perched, hovering, strafing; between them, circling) ---

enum { AL_NONE, AL_CIRCLE, AL_PATH, AL_HOVER, AL_PERCH, AL_GROUND };
static void breath_on(void);
static void breath_off(void);
static int meteors_on; // (Alduin's shout from the tower: burning rocks fall about you from then on)
static struct {
	int beast, kind, mode, breathe;
	float pos[3], heading, t, breath_t, meteor_t, angle;
	float path[8][3];
	float dir[3]; // (his flight's direction, turned toward each waypoint over a moment: a curve through them, not corners)
	int n, i;
	float aim[3]; // where his breath goes
	int arrived;
} al = {-1, -1};

static void alduin_show(const float at[3]) {
	if (al.kind < 0)
		al.kind = actors_beast_kind("alduin", "dragon");
	if (al.beast < 0 && al.kind >= 0) {
		al.beast = actors_beast_spawn(al.kind);
		actors_beast_look(al.beast, 1, 0.0f);
		memcpy(al.pos, at, sizeof(al.pos));
		al.t = 0.0f;
		al.mode = AL_CIRCLE;
		game_log("opening: Alduin comes (kind %d, beast %d) from %.1f %.1f %.1f", al.kind, al.beast, at[0], at[1], at[2]);
	} else if (al.beast < 0) {
		game_log("opening: no Alduin to show (kind %d)", al.kind);
	}
}

// The head of the first prisoner, in the crate by the block (Skyrim's: his head lies in it as you kneel)
static int head_beast = -1;
static float head_at[3], head_heading;
static void head_gone(void) {
	if (head_beast >= 0)
		actors_beast_remove(head_beast);
	head_beast = -1;
}

static void alduin_gone(void) {
	breath_off(); // (his fire's sound gone with him)
	if (al.beast >= 0)
		actors_beast_remove(al.beast);
	al.beast = -1, al.mode = AL_NONE;
}

// One of Skyrim's dunCGAlduin* / MQ101Alduin* packages: by what it's called
static void alduin_package(const Action *a) {
	if (stage == ST_TOWER) // (Skyrim's tower: his flights are heard, not seen - he's not drawn till the jump)
		return;
	const Marker *first = target(a, 0);
	if (al.beast < 0) {
		const Marker *west = marker("mq101alduinoffstagewestmarker");
		if (west)
			alduin_show(west->pos);
	}
	if (!first || al.beast < 0)
		return;
	const char *p = a->package;
	al.breathe = (strstr(p, "shout") && !strstr(p, "shouttower")) || strstr(p, "breath"); // (his shout from the tower: the yard thrown down, no fire - the reference)
	al.arrived = 0;
	memset(al.dir, 0, sizeof(al.dir)); // (a new flight: its first heading from where he is)
	// (a travel to a landing - "...btravelrubbleoneshout" to "...blandrubbleref", Haming's scene: a
	// ground landing; flown as a plain flight he went straight through the tower to it, and circled low)
	// (a shout from the tower - DunCGAlduinShoutTower01/02: perched where he is, the shout aimed at its second
	// reference, the block - flown as a strafe through both, he dived off the tower into you at the block)
	const int is_perch = strstr(p, "perch") != NULL || strstr(p, "shouttower") != NULL;
	const int lands = is_perch || (strstr(first->name, "land") && !strstr(first->name, "island"));
	if (lands && !strstr(p, "hover")) { // (landing there; shouting from it)
		al.n = 1, al.i = 0, memcpy(al.path[0], first->pos, sizeof(al.path[0]));
		const Marker *tower = marker("mq101towerstaggermarker");
		if (tower && !strcmp(first->name, "duncgtowerhole01ref")) { // (the wall he breaks: clinging outside it, head in)
			float ox = first->pos[0] - tower->pos[0], oz = first->pos[2] - tower->pos[2];
			const float d = fmaxf(hypotf(ox, oz), 0.1f);
			al.path[0][0] += ox / d * TOWER_PERCH_OUT_M, al.path[0][2] += oz / d * TOWER_PERCH_OUT_M;
			al.path[0][1] -= 2.5f;
			memcpy(al.aim, tower->pos, sizeof(al.aim));
			al.mode = AL_PATH, al.angle = -1.0f;
			return;
		}
		// (to a perch or from a low point across buildings: arc over the midpoint, so he comes down onto it
		// from above - a straight line from offstage west ran through the tower's roof)
		if ((is_perch || al.pos[1] < first->pos[1] + 10.0f) && hypotf(al.pos[0] - first->pos[0], al.pos[2] - first->pos[2]) > 12.0f) {
			memcpy(al.path[1], first->pos, sizeof(al.path[1]));
			al.path[0][0] = (al.pos[0] + first->pos[0]) * 0.5f;
			al.path[0][1] = fmaxf(first->pos[1], al.pos[1]) + PERCH_ABOVE_M;
			al.path[0][2] = (al.pos[2] + first->pos[2]) * 0.5f;
			al.n = 2;
		}
		if (al.t < 0.1f)
			al.heading = atan2f(-(al.path[0][0] - al.pos[0]), -(al.path[0][2] - al.pos[2]));
		al.mode = AL_PATH;
		const Marker *aim = target(a, 1);
		if (aim) {
			memcpy(al.aim, aim->pos, sizeof(al.aim));
		} else {
			al.aim[0] = first->pos[0] - sinf(first->heading) * 10.0f;
			al.aim[1] = first->pos[1];
			al.aim[2] = first->pos[2] - cosf(first->heading) * 10.0f;
		}
		al.angle = is_perch ? -1.0f : -4.0f; // (-1: perch on tower; -4: stand on ground)
	} else if (strstr(p, "hover")) { // (hovering at the first of its markers, breathing at the targets)
		al.n = 1, al.i = 0, memcpy(al.path[0], first->pos, sizeof(al.path[0]));
		al.mode = AL_PATH, al.angle = -2.0f; // (to hover at the end)
		const Marker *aim = target(a, 3);
		memcpy(al.aim, aim ? aim->pos : first->pos, sizeof(al.aim));
	} else { // (strafing, patrolling, a flight: through them all, then circling)
		al.n = 0;
		for (int k = 0; k < 8; k++) {
			const Marker *m = target(a, k);
			if (m)
				memcpy(al.path[al.n++], m->pos, sizeof(al.path[0]));
		}
		al.i = 0, al.mode = AL_PATH, al.angle = 0.0f;
		memcpy(al.aim, first->pos, sizeof(al.aim));
		if (al.n > 2) // (the strafe's target: his breath along it)
			memcpy(al.aim, al.path[1], sizeof(al.aim));
	}
}

static void alduin_update(float dt, const float player[3]) {
	if (al.beast < 0)
		return;
	al.t += dt;
	const char *clip = "glide";
	if (al.mode == AL_CIRCLE) { // (round Helgen, high)
		const Marker *c = marker("centermarkerhelgen");
		const float ctr[3] = {c ? c->pos[0] : player[0], c ? c->pos[1] : player[1], c ? c->pos[2] : player[2]};
		al.angle += ALDUIN_SPEED / ALDUIN_RING_M * dt;
		const float want[3] = {ctr[0] + cosf(al.angle) * ALDUIN_RING_M, ctr[1] + ALDUIN_HEIGHT_M,
		                       ctr[2] + sinf(al.angle) * ALDUIN_RING_M};
		for (int k = 0; k < 3; k++)
			al.pos[k] += (want[k] - al.pos[k]) * fminf(1.0f, dt * 1.5f);
		const float tx = -sinf(al.angle), tz = cosf(al.angle);
		al.heading = atan2f(-tx, -tz);
		clip = "glide_l";
	} else if (al.mode == AL_PATH) {
		const float *to = al.path[al.i];
		const float dx = to[0] - al.pos[0], dy = to[1] - al.pos[1], dz = to[2] - al.pos[2];
		const float d = sqrtf(dx * dx + dy * dy + dz * dz), last = al.i == al.n - 1;
		const float speed = last && al.angle < 0.0f ? fmaxf(5.0f, fminf(ALDUIN_SPEED, d * 1.2f)) : ALDUIN_SPEED;
		if (d < (last ? 1.5f : fmaxf(1.5f, speed * 0.5f))) { // (a waypoint on the way is passed by, not circled)
			if (!last)
				al.i++;
			else if (al.angle == -1.0f)
				al.mode = AL_PERCH, al.arrived = 1, sfx_play_at("dragon_land", al.pos, 200.0f),
				game_log("opening: Alduin perches at %.1f %.1f %.1f", al.pos[0], al.pos[1], al.pos[2]);
			else if (al.angle == -4.0f)
				al.mode = AL_GROUND, al.arrived = 1, sfx_play_at("dragon_land", al.pos, 200.0f);
			else if (al.angle == -2.0f)
				al.mode = AL_HOVER, al.arrived = 1;
			else if (al.angle == -3.0f) { // (a fly-over: off into the sky, gone)
				alduin_gone();
				return;
			}
			else
				al.mode = AL_CIRCLE, al.arrived = 1, al.angle = atan2f(al.pos[2] - player[2], al.pos[0] - player[0]);
		} else {
			const float step = fminf(d, speed * dt);
			float want_dir[3] = {dx / d, dy / d, dz / d};
			const float have = sqrtf(al.dir[0] * al.dir[0] + al.dir[1] * al.dir[1] + al.dir[2] * al.dir[2]);
			if (have < 0.5f) {
				memcpy(al.dir, want_dir, sizeof(al.dir));
			} else {
				const float blend = fminf(1.0f, dt * (last && d < 12.0f ? 8.0f : 2.5f));
				float len = 0.0f;
				for (int k = 0; k < 3; k++)
					al.dir[k] += (want_dir[k] - al.dir[k]) * blend, len += al.dir[k] * al.dir[k];
				len = fmaxf(sqrtf(len), 1e-4f);
				for (int k = 0; k < 3; k++)
					al.dir[k] /= len;
			}
			al.pos[0] += al.dir[0] * step, al.pos[1] += al.dir[1] * step, al.pos[2] += al.dir[2] * step;
			const int align_aim = last && al.angle < 0.0f && (d < 8.0f || hypotf(dx, dz) < 2.0f);
			const float ax = align_aim ? (al.aim[0] - al.pos[0]) : dx;
			const float az = align_aim ? (al.aim[2] - al.pos[2]) : dz;
			if (hypotf(ax, az) > 0.2f) {
				const float want = atan2f(-ax, -az);
				float diff = want - al.heading;
				while (diff > (float)M_PI)
					diff -= 2.0f * (float)M_PI;
				while (diff < -(float)M_PI)
					diff += 2.0f * (float)M_PI;
				al.heading += diff * fminf(1.0f, dt * 4.0f);
			}
		}
		clip = last && al.angle < 0.0f && d < 18.0f ? "hover" : "glide";
	} else { // (perched, on the ground, or hovering: facing his breath's target)
		const float dx = al.aim[0] - al.pos[0], dz = al.aim[2] - al.pos[2];
		if (hypotf(dx, dz) > 0.5f)
			al.heading = atan2f(-dx, -dz);
		clip = al.mode == AL_PERCH ? (al.breathe ? "breathe" : "idle") :
		       al.mode == AL_GROUND ? (al.breathe ? "breathe" : "combat") :
		       (al.breathe ? "hover_breathe" : "hover");
	}
	actors_beast_pose(al.beast, al.pos[0], al.pos[1], al.pos[2], al.heading, clip, al.t, NULL, 0.0f, 0.0f, dt);
	// His fire: on its target while he breathes - heard while it's seen (his scene packages breathed fire in
	// silence: the road's two at Torolf and the yard, the wall's) - and burning rocks falling on the town
	const int fire_now = al.breathe && (al.mode == AL_PERCH || al.mode == AL_GROUND || al.mode == AL_HOVER || al.mode == AL_PATH);
	if (fire_now)
		breath_on();
	else
		breath_off();
	if (fire_now && (al.breath_t -= dt) <= 0.0f) {
		al.breath_t = BREATH_EVERY_S;
		const float spread = 2.5f;
		const float at[3] = {al.aim[0] + ((rand() % 100) / 50.0f - 1.0f) * spread, al.aim[1],
		                     al.aim[2] + ((rand() % 100) / 50.0f - 1.0f) * spread};
		spellfx_burst(at, ELEMENT_FIRE, 2.5f);
		if (rand() % 3 == 0)
			sfx_play_at("mag_fire_hit", at, 60.0f);
	}
	// (his shout from the tower calls them down - Skyrim's meteors: from then on while you're out in the town)
	if ((meteors_on && stage <= ST_BLACKOUT) || (stage > ST_TOWER && stage <= ST_TOKEEP)) {
		if ((al.meteor_t -= dt) <= 0.0f) {
			al.meteor_t = METEOR_EVERY_S * (0.6f + (rand() % 100) / 100.0f) * (stage <= ST_BLACKOUT ? 0.5f : 1.0f);
			const float a = (rand() % 628) / 100.0f, r = 9.0f + (rand() % 25);
			float at[3] = {player[0] + cosf(a) * r, player[1], player[2] + sinf(a) * r}, ground;
			if (terrain_height_at(at[0], at[2], &ground))
				at[1] = ground;
			// (falling: a burning rock from high over the town, slanting down onto its mark - the Fireball spell's
			// bolt and burst, already loaded for the spells: no more video memory; harmless, for the sight of it)
			const float side = ((rand() % 100) / 50.0f - 1.0f) * METEOR_SLANT_M;
			const float from[3] = {at[0] + side, at[1] + METEOR_HIGH_M, at[2] + METEOR_SLANT_M * 0.6f};
			float dir[3] = {at[0] - from[0], at[1] - from[1], at[2] - from[2]};
			const float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
			for (int k = 0; k < 3; k++)
				dir[k] /= len;
			projectiles_fire_hostile_bolt(from, dir, METEOR_SPEED, ELEMENT_FIRE, 0.0f, 3.5f);
			static int told;
			if (told++ < 3)
				game_log("opening: a burning rock falls %.0f m from you", r);
			sfx_play_at("trp_rockfall", at, 80.0f);
		}
	}
}

// Out of the caves: "Wait!" - Alduin low over the mountain behind you, over your heads, and off
// north into the sky till he's gone (Skyrim's MQ102 opening, either way you went)
static int ruin_said; // (the road's INFO 00017E00 line: said once a game)
static int farewell_pending; // (INFO 00092517: once he is)
static void alduin_fly_over(const float feet[3], float heading) {
	alduin_gone();
	const float fx = -sinf(heading), fz = -cosf(heading), rx = -fz, rz = fx;
	const float from[3] = {feet[0] - fx * 140.0f - rx * 40.0f, feet[1] + 45.0f, feet[2] - fz * 140.0f - rz * 40.0f};
	alduin_show(from);
	if (al.beast < 0)
		return;
	const float over[3] = {feet[0] + rx * 16.0f, feet[1] + 21.0f, feet[2] + rz * 16.0f}; // (to one side: the view looks up to 50 degrees)
	const float away[3] = {feet[0] + fx * 260.0f + rx * 90.0f, feet[1] + 110.0f, feet[2] + fz * 260.0f + rz * 90.0f};
	memcpy(al.path[0], over, sizeof(over));
	memcpy(al.path[1], away, sizeof(away));
	al.n = 2, al.i = 0, al.mode = AL_PATH, al.angle = -3.0f, al.breathe = 0;
	al.heading = atan2f(-(over[0] - from[0]), -(over[2] - from[2]));
	sfx_play("dragon_roar_far", 1.0f, 0.0f);
	sfx_play_at("dragon_flyby", over, 200.0f);
}

// --- The scene player: Skyrim's scenes as written, phase by phase --------------------------
//
// A phase starts its lines (queued in order) and its packages (who walks or runs where,
// Alduin's flights); it ends when its lines are said, those it sent walking (for this phase
// alone) have got there - or PHASE_MAX_S - and the opening's own gate lets it (you've kept
// up with Ralof, answered INFO 0003F88B...).

#define MAX_TRACKED 24
static struct {
	char name[40];
	int phase, to, active, running;
	float t;
	int walkers[MAX_TRACKED], nw;
} run;
static struct { int actor; char clip[24]; int once; float heading; int pending; } after[MAX_TRACKED];

static int phase_exists(const char *name, int p) {
	for (int i = 0; i < line_count; i++)
		if (lines[i].phase == p && !strcmp(lines[i].scene, name))
			return 1;
	for (int i = 0; i < action_count; i++)
		if (actions[i].start == p && !strcmp(actions[i].scene, name))
			return 1;
	return 0;
}

static int next_phase(const char *name, int after_p, int to) {
	for (int p = after_p + 1; p <= to; p++)
		if (phase_exists(name, p))
			return p;
	return -1;
}

static void when_there(int actor, const char *clip, int once, float heading) {
	for (int k = 0; k < MAX_TRACKED; k++)
		if (!after[k].pending || after[k].actor == actor) {
			after[k].actor = actor, after[k].once = once, after[k].heading = heading, after[k].pending = 1;
			snprintf(after[k].clip, sizeof(after[k].clip), "%s", clip ? clip : "");
			return;
		}
}

static float player_to[3];
static int breath_voice = -1; // (Alduin's fire: its loop, stopped when he stops)
static float breath_started; // (stage_t when it started: how long it was heard, in the log)
static int breath_logged;
static void breath_on(void) {
	if (breath_voice < 0) { // (Skyrim's: "Yol" shouted, then his fire's roar - the dragon flames loop - while it lasts)
		sfx_play_at("dragon_yol", al.pos, 200.0f);
		breath_voice = sfx_loop("dragon_breath_lp", 1.0f);
		breath_started = stage_t;
		if (breath_logged++ < 24) // (the user heard none, Nov 8: whether it starts, and for how long)
			game_log("opening: Alduin's fire heard (sound %d, mode %d)", breath_voice, al.mode);
	}
}
static void breath_off(void) {
	if (breath_voice >= 0) {
		sfx_stop(breath_voice), breath_voice = -1;
		if (breath_logged < 24)
			game_log("opening: Alduin's fire stops after %.2f s", stage_t - breath_started);
	}
}
static float lead_mark[3]; // (the keep: the way out)
static int lead_on;
#define GUARD_BEHIND_M 1.1f // (the captain at the block: this far behind the prisoner kneeling at it)
#define WALL_JOIN_M 9.0f          // (INFO 000DB8D5: this near Hadvar's mark by it, you're in beside him...)
#define WALL_BEHIND_HADVAR_M 1.0f // (... a step behind him)
#define BLOCK_LOOK_HADVAR_S 3.5f  // (stood at the block, looking back at Hadvar this long: the reference's 6:56-7:00)
#define BLOCK_STAND_BACK_M 0.6f   // (stood this far behind where you kneel)
#define BLOCK_ALDUIN_COMES_S 5.0f // (on the block this long, he comes: landing on the tower some 9 s later, as 7:13)
#define BLOCK_RAISE_AFTER_LAND_S 3.0f // (he's landed: the axe goes up this long after...)
#define BLOCK_SHOUT_AFTER_LAND_S 6.5f // (... and his shout: landed 7:13, the shout 7:20 in the reference)
#define BLOCK_HEAD_ROLL_DEG -24.0f   // (the head on its side on the block: the view tilted)
#define BLOCK_KNOCKED_ROLL_DEG -85.0f // (thrown off it by the shout)
#define ALDUIN_LAND_SHAKE_S 1.2f // (he lands on the tower: the view shakes this long)
#define BLOCK_DAZED_S 5.0f        // (the dazed blur before the black)
#define FIRST_BODY_FALL_FROM_S 0.45f // (the death clip begun this far in: down from the kneel, no standing up into it)
// The block as Skyrim lays it out and the reference shows it (6:00-6:19, 7:00-7:32): its stone runs east-west, 0.65 to
// 1.92 m ahead (west) of the block's mark (HeadChoppingBlock.nif); the condemned kneel on its north side facing south, the
// neck across it, the head into the basket on its south side (CrateOpen: 1.29 m west, 0.6 m south of the mark); the
// captain behind them (north: your left as you watch from the line, not between you and them); the headsman at
// MQ101HeadsmanMarker2, south-east of the stone, facing them - the tower behind him as you look up from the block.
// (We knelt them on the stone's west end facing east, the captain behind them in your way and the headsman across it:
// the scene turned 90 degrees from Skyrim's and the reference's)
#define BLOCK_STONE_MID_M 1.285f   // (the stone's middle: this far ahead (west) of the block's mark...)
#define BLOCK_STONE_SIDE_M -0.126f // (... and to its right (north; -: south))
#define BLOCK_KNEEL_M 0.75f        // (kneeling this far north of the stone's middle, facing it: the neck over it)
#define BLOCK_HEADSMAN_M 1.8f      // (the headsman this far from the stone's middle, if Skyrim's mark for him is missing)
static int block_frame(float kneel[3], float *kneel_heading, float headsman[3], float *headsman_heading) {
	const Marker *b = marker("mq101choppingblock");
	if (!b)
		return 0;
	const float fx = -sinf(b->heading), fz = -cosf(b->heading); // (along the stone: west)
	const float rx = cosf(b->heading), rz = -sinf(b->heading);  // (its right: north)
	const float mid[2] = {b->pos[0] + fx * BLOCK_STONE_MID_M + rx * BLOCK_STONE_SIDE_M,
	                      b->pos[2] + fz * BLOCK_STONE_MID_M + rz * BLOCK_STONE_SIDE_M};
	if (kneel)
		kneel[0] = mid[0] + rx * BLOCK_KNEEL_M, kneel[1] = b->pos[1], kneel[2] = mid[1] + rz * BLOCK_KNEEL_M;
	if (kneel_heading)
		*kneel_heading = atan2f(rx, rz); // (facing south, over the stone)
	if (headsman || headsman_heading) {
		const Marker *hm = marker("mq101headsmanmarker2");
		float at[3], h;
		if (hm) {
			memcpy(at, hm->pos, sizeof(at));
			h = atan2f(-(mid[0] - at[0]), -(mid[1] - at[2])); // (facing the neck on the stone: his mark's own heading, within a few degrees)
		} else { // (south-east of the stone, facing it)
			const float sx = -rx * 0.7f - fx * 0.7f, sz = -rz * 0.7f - fz * 0.7f;
			at[0] = mid[0] + sx * BLOCK_HEADSMAN_M, at[1] = b->pos[1], at[2] = mid[1] + sz * BLOCK_HEADSMAN_M;
			h = atan2f(sx, sz);
		}
		if (headsman)
			memcpy(headsman, at, sizeof(at));
		if (headsman_heading)
			*headsman_heading = h;
	}
	return 1;
}
// Your own turn at the block, as the reference frames it (7:00-7:32) - and as the round before this did, which the user found
// "more right" (Nov 8): knelt at its west side facing east, the headsman across it, and Alduin's tower beyond him. On the
// block the view goes up at the tower with Alduin landing in the middle of it, the headsman in front of its left side
// (Skyrim's marks for the first prisoner put the tower 100 degrees to your left: him and Hadvar filled the view instead)
#define YOU_KNEEL_M 0.55f            // (kneeling this far west of the block's mark, facing east)
#define YOU_EYE_FWD_M 0.35f          // (your eyes this far ahead of where you kneel, this high)
#define YOU_EYE_UP_M 0.5f
#define YOU_HEADSMAN_M 1.45f         // (the headsman this far from your eyes...)
#define YOU_HEADSMAN_LEFT_DEG 12.0f  // (... this far left of the tower's line: before its left side)
#define YOU_ROLL_DEG -12.0f          // (the head on its side on the block: the reference's tower leans this much)
#define YOU_ALDUIN_UP_M 2.0f         // (looking at this far over his perch: him, landed, in the middle)
static int player_block_frame(float kneel[3], float *kneel_heading, float eye[3], float headsman[3], float *headsman_heading) {
	const Marker *b = marker("mq101choppingblock");
	if (!b)
		return 0;
	const float fx = -sinf(b->heading), fz = -cosf(b->heading); // (the block's front: west)
	const float k[3] = {b->pos[0] + fx * YOU_KNEEL_M, b->pos[1], b->pos[2] + fz * YOU_KNEEL_M};
	const float kh = b->heading + (float)M_PI; // (facing east, over the block)
	const float e[3] = {k[0] - fx * YOU_EYE_FWD_M, k[1] + YOU_EYE_UP_M, k[2] - fz * YOU_EYE_FWD_M};
	if (kneel)
		memcpy(kneel, k, sizeof(k));
	if (kneel_heading)
		*kneel_heading = kh;
	if (eye)
		memcpy(eye, e, sizeof(e));
	if (headsman || headsman_heading) {
		const Marker *perch = marker("mq101alduinperchmarker1");
		float dx = perch ? perch->pos[0] - e[0] : -fx, dz = perch ? perch->pos[2] - e[2] : -fz;
		const float d = hypotf(dx, dz) > 0.01f ? hypotf(dx, dz) : 1.0f;
		dx /= d, dz /= d;
		const float a = YOU_HEADSMAN_LEFT_DEG * (float)M_PI / 180.0f; // (turned left: on the screen, -x; the world's yaw, the other way)
		const float lx = dx * cosf(a) + dz * sinf(a), lz = -dx * sinf(a) + dz * cosf(a);
		const float at[3] = {e[0] + lx * YOU_HEADSMAN_M, b->pos[1], e[2] + lz * YOU_HEADSMAN_M};
		if (headsman)
			memcpy(headsman, at, sizeof(at));
		if (headsman_heading)
			*headsman_heading = atan2f(-(e[0] - at[0]), -(e[2] - at[2])); // (facing you)
	}
	return 1;
}
#define GLIDE_CLEAR_M 0.7f // (walked by a scene: round anyone nearer your way than this)
static int player_moving;

// Who a package aimed at a person goes to, by its name: "...approachgerdur",
// "...traveltoalvor", "...followralof", "...toralof" (Skyrim's target is the person, not a
// marker - and nobody moved: Ralof and Gerdur talked across the mill)
static const char *package_person(const char *p) {
	static const char *const VERBS[] = {"approach", "travelto", "follow", "to"};
	for (size_t v = 0; v < sizeof(VERBS) / sizeof(VERBS[0]); v++) {
		const char *at = strstr(p, VERBS[v]);
		while (at) {
			const char *name = at + strlen(VERBS[v]);
			if (*name && who(name) >= 0)
				return name;
			at = strstr(at + 1, VERBS[v]);
		}
	}
	return NULL;
}

// A scene's "follow" packages: kept to, while their phases last ("Ralof follows Gerdur" to
// the clearing - he stood at the mill, set off once from beside her and arrived at once)
#define MAX_FOLLOWS 8
static struct {
	int who, leader, end;
	char scene[40];
	float t;
} follows[MAX_FOLLOWS];
static int follow_count;

static void follow_step(float dt) {
	for (int k = 0; k < follow_count; k++) {
		if (!run.active || strcmp(run.name, follows[k].scene) || run.phase > follows[k].end) { // (over: dropped)
			follows[k--] = follows[--follow_count];
			continue;
		}
		if ((follows[k].t -= dt) > 0.0f)
			continue;
		follows[k].t = 0.7f;
		float me[3], them[3];
		if (!actors_where(follows[k].who, me) || !actors_where(follows[k].leader, them))
			continue;
		float dx = me[0] - them[0], dz = me[2] - them[2];
		const float d = hypotf(dx, dz);
		if (d < 3.0f)
			continue;
		dx /= d, dz /= d;
		const float to[3] = {them[0] + dx * 1.6f, them[1], them[2] + dz * 1.6f};
		actors_script_walk(follows[k].who, to, d > 8.0f);
	}
}

// A package that names no place or person we have (a "wait", a "guard", a "watch the execution": no
// target in the ESM's alias list): the actor holds where they stand, in the scene's hands - left
// to their own AI they wandered off their marks (the headsman, the prisoners, the soldiers)
static int first_beheaded; // (the first prisoner's body, fallen from the block: nothing poses it again)
static void hold_in_place(int i) {
	if (i < 0 || actors_script_walking(i) || (first_beheaded && i == who("mq101stormcloakcartmalebroder")))
		return;
	actors_script(i, 1);
	actors_script_clip(i, NULL, 0);
}

static void toward_actor(const Action *a, int i) {
	const char *name = package_person(a->package);
	const int j = name ? who(name) : -1;
	float me[3], them[3];
	if (j < 0 || j == i || !actors_where(i, me) || !actors_where(j, them)) {
		hold_in_place(i);
		return;
	}
	if (strstr(a->package, "follow") && follow_count < MAX_FOLLOWS) { // (kept to, till its phases are done)
		follows[follow_count].who = i, follows[follow_count].leader = j, follows[follow_count].end = a->end;
		snprintf(follows[follow_count].scene, sizeof(follows[0].scene), "%s", a->scene);
		follows[follow_count++].t = 0.0f;
		actors_script(i, 1);
		return;
	}
	// (both coming to each other - "Gerdur!", "Ralof!": they meet between)
	int both = 0;
	for (int k = 0; k < action_count && !both; k++) {
		const Action *o = &actions[k];
		both = !strcmp(o->scene, a->scene) && !strcmp(o->who, name) && o->start <= run.phase && o->end >= run.phase &&
		       (o->package[0] && package_person(o->package) && !strcmp(package_person(o->package), a->who));
	}
	float dx = me[0] - them[0], dz = me[2] - them[2];
	const float d = fmaxf(hypotf(dx, dz), 0.01f);
	dx /= d, dz /= d;
	const float apart = 1.4f, from = both ? d * 0.5f + apart * 0.5f : apart;
	if (d <= from + 0.3f)
		return; // (near enough)
	const float to[3] = {them[0] + dx * from, them[1], them[2] + dz * from};
	actors_script(i, 1);
	actors_script_walk(i, to, run.running || strstr(a->package, "run") != NULL);
	when_there(i, NULL, 0, atan2f(dx, dz)); // (facing them)
	if (a->end <= a->start + 1 && run.nw < MAX_TRACKED)
		run.walkers[run.nw++] = i;
}

// A walker's goal clear of whoever stands there (or is bound for it): the player, the rest of the
// cast - Skyrim's people step round each other; ours walked into the player's spot (the captain, twice) and into
// one another
#define SPREAD_M 0.95f
static void spread_goal(int self, float goal[3]) {
	for (int pass = 0; pass < 4; pass++) {
		int moved = 0;
		float others[24][3];
		int n = 0;
		memcpy(others[n++], player_moving ? player_to : ear, sizeof(others[0]));
		memcpy(others[n++], ear, sizeof(others[0]));
		for (size_t k = 0; k < sizeof(CAST) / sizeof(CAST[0]) && n < 24; k++) {
			const int j = who(CAST[k]);
			if (j >= 0 && j != self && actors_where(j, others[n]) && others[n][1] > -400.0f)
				n++;
		}
		for (int k = 0; k < n; k++) {
			float dx = goal[0] - others[k][0], dz = goal[2] - others[k][2];
			const float d = hypotf(dx, dz);
			if (d >= SPREAD_M || fabsf(goal[1] - others[k][1]) > 3.0f)
				continue;
			if (d < 0.01f)
				dx = 1.0f, dz = 0.0f;
			else
				dx /= d, dz /= d;
			goal[0] = others[k][0] + dx * SPREAD_M, goal[2] = others[k][2] + dz * SPREAD_M;
			moved = 1;
		}
		if (!moved)
			break;
	}
}

static void start_package(const Action *a) {
	const char *p = a->package;
	if (!strcmp(a->who, "alduin") || !strcmp(a->who, "mq101alduin")) {
		alduin_package(a);
		return;
	}
	if (!strcmp(a->who, "generaltullius") && !strcmp(run.name, "mq101dragonattackscenedtokeep"))
		return; // (Tullius by the keep's door for this one: in sight as he shouts - not back off east)
	if (stage == ST_CART && !strcmp(a->who, "generaltullius") && !strcmp(run.name, "mq101scene1"))
		return; // (Tullius stays by Elenwen while the carts come in: his walk down crossed the cart's road - not
		        //  after: the lineup's scene begins as the cart stops, and he never walked to the block)
	if (!strcmp(run.name, "mq101scene4") && !strcmp(a->who, "mq101prisoner"))
		return; // (Lokir: his run is ours - its package walked him off another way)
	const Marker *m = NULL;
	if (strstr(p, "tomarker6")) { // (to the row by the block - its "marker6", not the package's first reference)
		char name[48];
		snprintf(name, sizeof(name), "mq101%smarker6", a->who);
		m = marker(name);
	}
	for (int k = 0; k < 6 && !m; k++) // (its first that's a place: "#14" is the player, no marker)
		m = target(a, k);
	if (!strcmp(a->who, "player")) { // (stepping forward from the line: walked by the scene)
		if (strstr(a->package, "tomarker6") && marker("mq101playermarker6")) // (the row by the block: close)
			m = marker("mq101playermarker6");
		if (m)
			memcpy(player_to, m->pos, sizeof(player_to)), player_moving = 1;
		return;
	}
	const int i = who(a->who);
	if (first_beheaded && i >= 0 && i == who("mq101stormcloakcartmalebroder"))
		return; // (his body lies where it fell: a later phase's INFO 020133AE stood it up again, headless)
	if (i >= 0 && !m) { // (a package aimed at someone, not a place: to them)
		toward_actor(a, i);
		return;
	}
	if (i < 0 || !m)
		return;
	actors_script(i, 1);
	float goal[3]; // (where: the mark - or, the captain's at the block, behind the prisoner kneeling at it)
	memcpy(goal, m->pos, sizeof(goal));
	if (!strcmp(p, "mq102ralofsit")) { // (Riverwood's clearing: Skyrim's InvisibleChairMarker #8d2b9 is on the rim of the
		// big cut stump - TreePineForestCutStump01, its top 1.9 m across, 0.6 m over the marker: sat there, he's on the
		// stump's edge, as the reference shows him (3:20-5:50); at the stump's middle on the ground he sat inside it.
		// Turned to Gerdur and Hod at their marks, the stump on his right)
		static Marker seat;
		const Marker *g = marker("mq102gerduratclearingmarker"), *h = marker("mq102hodatclearingmarker");
		seat = *m;
		if (g && h) {
			const float cx = (g->pos[0] + h->pos[0]) * 0.5f, cz = (g->pos[2] + h->pos[2]) * 0.5f;
			seat.heading = atan2f(-(cx - seat.pos[0]), -(cz - seat.pos[2]));
		}
		m = &seat;
	}
	const int behind_block = strstr(p, "sitblock") != NULL;
	static Marker kneel_m;
	if (strstr(p, "choppingblock") || behind_block) { // (the block's near side, facing it: the prisoner - and the captain behind him)
		float k[3], kh;
		if (block_frame(k, &kh, NULL, NULL)) {
			memcpy(kneel_m.pos, k, sizeof(k)), kneel_m.heading = kh;
			if (behind_block)
				kneel_m.pos[0] += sinf(kh) * GUARD_BEHIND_M, kneel_m.pos[2] += cosf(kh) * GUARD_BEHIND_M;
			m = &kneel_m;
			memcpy(goal, m->pos, sizeof(goal));
		}
	}
	float at[3];
	if (!actors_where(i, at) || at[1] < -400.0f || hypotf(at[0] - goal[0], at[2] - goal[2]) > 120.0f) {
		actors_script_place(i, goal, m->heading); // (offstage till now - out of sight, or not in Helgen: there)
		memcpy(at, goal, sizeof(at));
	}
	if (!behind_block && strcmp(a->who, "mq101headsman") && stage <= ST_EXECUTION)
		spread_goal(i, goal); // (the lineup: clear of you and each other - the block's, the headsman's are their marks)
	const int far = hypotf(at[0] - goal[0], at[2] - goal[2]) > 0.6f;
	// What they do there, by the package's name: kneel at the block, sit, take cover, lie wounded
	const char *clip = strstr(p, "choppingblock") ? "block_in" : strstr(p, "sit") && !strstr(p, "sitblock") ? "chair_idle"
	                 : strstr(p, "wounded") ? "death" : strstr(p, "hide") || strstr(p, "crouch") || strstr(p, "tense") ? "sneak_idle"
	                 : behind_block ? "guard_enter" : NULL; // (stepping in behind him, the kick down to the block)
	const int once = clip && (!strcmp(clip, "block_in") || !strcmp(clip, "death") || !strcmp(clip, "guard_enter"));
	if (far) {
		actors_script_walk(i, goal, run.running || strstr(p, "run") != NULL);
		when_there(i, clip, once, m->heading);
		if (a->end <= a->start + 1 && run.nw < MAX_TRACKED) // (this phase waits for them)
			run.walkers[run.nw++] = i;
	} else {
		actors_script_clip(i, clip, once);
		actors_script_face(i, m->heading);
	}
}

static void run_enter(int p) {
	run.phase = p, run.t = 0.0f, run.nw = 0;
	queue_lines(run.name, p, p, 0);
	for (int k = 0; k < action_count; k++)
		if (actions[k].start == p && !strcmp(actions[k].scene, run.name))
			start_package(&actions[k]);
}

// A scene from phase from to phase to (running: everyone hurries - the town's burning)
static void run_scene(const char *name, int from, int to, int running) {
	snprintf(run.name, sizeof(run.name), "%s", name);
	run.to = to, run.active = 1, run.running = running;
	sc.n = sc.i = 0;
	game_log("intro: scene %s %d..%d", name, from, to);
	const int p = phase_exists(name, from) ? from : next_phase(name, from, to);
	if (p < 0)
		run.active = 0;
	else
		run_enter(p);
}

static int gate(void); // (the opening's own: may this phase end yet)

static void run_step(float dt) {
	for (int k = 0; k < MAX_TRACKED; k++) // (those arrived: what they do there)
		if (after[k].pending && !actors_script_walking(after[k].actor)) {
			after[k].pending = 0;
			actors_script_clip(after[k].actor, after[k].clip[0] ? after[k].clip : NULL, after[k].once);
			actors_script_face(after[k].actor, after[k].heading);
		}
	if (!run.active)
		return;
	run.t += dt;
	int walking = 0;
	for (int k = 0; k < run.nw; k++)
		walking |= actors_script_walking(run.walkers[k]);
	// (how long a phase waits for its walkers: the arrival's and the list's lines follow each other
	// as Skyrim's do - INFO 00046784 INFO 0004678D, "Halt!" "Archers!" - not after the
	// soldiers have all got to their places)
	// (and along the wall past Vilod: Skyrim's next scene - Tullius, INFO 0010A187 - begins
	// as you pass him; waiting for Hadvar's walks, it began 45 m on and he shouted out of sight)
	const float wait = !strcmp(run.name, "mq101scene3") || (!strcmp(run.name, "mq101dragonattackscenecperchwall") && run.phase >= 4) ? 0.6f
	                   : !strcmp(run.name, "mq101scene4") && run.phase < 16 ? 0.6f
	                   : !strcmp(run.name, "mq102ralofgerdurscene") && run.phase >= 17 && run.phase <= 20 ? 0.6f // (Hod from the mill
	                   : PHASE_MAX_S;                                                                            //  as she sets off)
	if (speaking() || (walking && run.t < wait) || !gate()) {
		static float stuck_told;
		if (run.t > 40.0f && run.t - stuck_told > 20.0f) // (a phase held this long: what it waits on, in the log)
			stuck_told = run.t, game_log("intro: %s phase %d held %.0f s - lines %d/%d, talk %d, voice %d, walking %d, gate %d", run.name,
			                             run.phase, run.t, sc.i, sc.n, talk_speaking(), voice_playing(), walking, gate());
		if (run.t < 1.0f)
			stuck_told = 0.0f;
		return;
	}
	const int p = next_phase(run.name, run.phase, run.to);
	if (p < 0)
		run.active = 0;
	else
		run_enter(p);
}

static int phase_is(const char *name, int p) {
	return run.active && run.phase == p && !strcmp(run.name, name);
}

static int phase_at_least(const char *name, int p) {
	return !strcmp(run.name, name) && (run.phase >= p || !run.active);
}

// --- The cart -------------------------------------------------------------------------------

static float cart_s;   // how far along the road
static float gate_s = -1.0f; // (how far along it Helgen's east gate is: the soldier's greeting there)

// The cart's talk (mq101scene1) has got to the soldier's greeting at the gate
static int cart_greeted(void) {
	return !strcmp(run.name, "mq101scene1") && (run.phase >= 21 || !run.active);
}

// About how long until the soldier's greeting (phase 21): when each phase's line began, from
// "Hey, you" (on a Vita: the lines' lengths and gaps)
static float cart_talk_left(void) {
	static const float AT[22] = {0, 0, 0, 0, 0.0f, 3.2f, 11.7f, 22.6f, 28.2f, 32.3f, 34.1f, 36.0f, 41.4f, 51.3f,
	                             59.2f, 62.0f, 62.4f, 66.4f, 67.8f, 72.5f, 76.0f, 76.7f};
	if (strcmp(run.name, "mq101scene1") || !run.active) // (not begun: "Hey, you" is CART_FIRST_LINE_S in)
		return AT[21] + fmaxf(0.0f, CART_FIRST_LINE_S - stage_t);
	const int p = run.phase < 4 ? 4 : run.phase > 21 ? 21 : run.phase;
	const float next = p < 21 ? AT[p + 1] : AT[21];
	return AT[21] - fminf(AT[p] + run.t, fmaxf(next, AT[p]));
}
static float cart_eye[3];
static int cart_prop = -1, horse = -1, horse_kind = -1;

// The road's point s metres along it (as the converter laid it: between the navmesh's triangles)
static void road_point(float s, float pos[3]) {
	s = fminf(fmaxf(s, 0.0f), route_len[route_n - 1]);
	int i = 0;
	while (i < route_n - 2 && route_len[i + 1] < s)
		i++;
	const float seg = fmaxf(route_len[i + 1] - route_len[i], 1e-3f), u = fminf(1.0f, fmaxf(0.0f, (s - route_len[i]) / seg));
	for (int k = 0; k < 3; k++)
		pos[k] = route[i][k] + (route[i + 1][k] - route[i][k]) * u;
}

// Where on the road s metres along is (pos) and which way it runs (heading as actors'): smoothed
// over 8 m of it, the heading along a 10 m chord - its points zig-zag a little between the
// navmesh's triangles, and the cart had followed each: swaying side to side, and at the start
// set across the road on the hillside
static void road_at(float s, float pos[3], float *heading, float *pitch) {
	float sum[3] = {0.0f, 0.0f, 0.0f}, p[3];
	for (int k = -2; k <= 2; k++) {
		road_point(s + (float)k * 2.0f, p);
		for (int c = 0; c < 3; c++)
			sum[c] += p[c] * 0.2f;
	}
	memcpy(pos, sum, sizeof(sum));
	float ground;
	if (terrain_height_at(pos[0], pos[2], &ground))
		pos[1] = ground;
	float ahead[3], behind[3];
	road_point(fmaxf(s, 5.0f) + 5.0f, ahead);
	road_point(s - 5.0f, behind);
	const float dx = ahead[0] - behind[0], dz = ahead[2] - behind[2];
	*heading = atan2f(-dx, -dz);
	if (pitch) {
		float ga, gb;
		const int ok = terrain_height_at(ahead[0], ahead[2], &ga) && terrain_height_at(behind[0], behind[2], &gb);
		*pitch = ok ? atanf((ga - gb) / fmaxf(hypotf(dx, dz), 0.5f)) : 0.0f;
	}
}

// A point of the cart (its frame: x right, y up, z back) in the world, tilted with it (pitch:
// its nose up) - the benches go down a hill with the bed, not through it
static float cart_pitch, cart_base[3], cart_heading;
static void cart_point(const float base[3], float heading, const float in[3], float out[3]) {
	const float cp = cosf(cart_pitch), sp = sinf(cart_pitch);
	const float local[3] = {in[0], in[1] * cp - in[2] * sp, in[2] * cp + in[1] * sp};
	const float c = cosf(heading), s = sinf(heading);
	out[0] = base[0] + local[0] * c + local[2] * s;
	out[1] = base[1] + local[1];
	out[2] = base[2] - local[0] * s + local[2] * c;
}

// Who sits where in Skyrim's prisoner carriage: its bed 1.5 m up (over the wheels), two
// benches along it under the cage facing each other (a seat's clip sits the hips a chair's
// height above where it's put), the player at the back left across from Ralof; the driver on
// the box in front
#define BED_Y 1.5f
static const struct { const char *who; float x, y, z, turn; const char *clip; } SEATS[] = {
	// (sat as on chairs: the carriage's benches are a chair's height over its floor; Skyrim's cart
	// clips carry their seat in a root offset this engine doesn't apply - they sat beside it)
	{"ralof", 0.55f, BED_Y, 1.3f, 1.5708f, "chair_idle"},
	{"mq101prisoner", 0.55f, BED_Y, 2.5f, 1.5708f, "chair_idle"},
	{"ulfric", -0.55f, BED_Y, 2.5f, -1.5708f, "chair_idle"},
	{"mq101imperialsoldier02", 0.0f, BED_Y, -0.45f, 0.0f, "chair_idle"},
};
static const float PLAYER_SEAT[3] = {-0.55f, BED_Y, 1.3f};
static const float PLAYER_EYE[3] = {-0.45f, BED_Y + 1.15f, 1.3f}; // (sat: the head over the bench)
#define PLAYER_TURN -1.5708f
#define HORSE_AHEAD_M 3.4f

#define CART_AXLE_M 1.8f // (its wheels this far before and behind its middle)
#define CART_EASE 0.9f   // (how quickly it follows the road's turns and slopes: no jolts)

static void cart_place(float dt, float *player_heading, float player_pos[3]) {
	float base[3], want_heading;
	road_at(cart_s, base, &want_heading, NULL);
	// Its height and tilt from the ground under both axles (not its middle alone: the ends were
	// in the ground on a slope), eased - the road's turns and bumps as a cart takes them
	// The ground fitted with a line along it (five points over 8 m: a carriage rides a dip or a hump
	// on its four wheels and its length, it doesn't drop its nose into each - the see-saw at the start)
	const float fx = -sinf(want_heading), fz = -cosf(want_heading);
	float sum_h = 0.0f, sum_dh = 0.0f, sum_dd = 0.0f;
	int ground = 1;
	for (int k = -2; k <= 2; k++) {
		const float d = (float)k * CART_AXLE_M * 1.1f;
		float h = base[1];
		ground &= terrain_height_at(base[0] + fx * d, base[2] + fz * d, &h);
		sum_h += h, sum_dh += d * h, sum_dd += d * d;
	}
	static float heading, pitch, y, last_pitch, last_y;
	static int smooth;
	if (stage_t < dt * 1.5f)
		smooth = 0, last_pitch = 0.0f, last_y = base[1];
	// (the ground only once it's streamed in - till then, as last known: a cart tipping to and fro
	// as the land loaded was a see-saw too)
	const float want_pitch = ground ? atanf(sum_dh / sum_dd) : last_pitch;
	const float want_y = ground ? sum_h / 5.0f - 0.05f : last_y;
	last_pitch = want_pitch, last_y = want_y;
	if (!smooth && ground)
		heading = want_heading, pitch = want_pitch, y = want_y, smooth = 1;
	else if (!smooth)
		heading = want_heading, pitch = 0.0f, y = want_y;
	const float ease = 1.0f - expf(-dt * CART_EASE);
	float dh = want_heading - heading;
	while (dh > (float)M_PI)
		dh -= 2.0f * (float)M_PI;
	while (dh < -(float)M_PI)
		dh += 2.0f * (float)M_PI;
	heading += dh * ease, pitch += (want_pitch - pitch) * ease, y += (want_y - y) * ease;
	base[1] = y, cart_pitch = pitch;
	memcpy(cart_base, base, sizeof(cart_base)), cart_heading = heading;
	static const uint8_t tint[3] = {205, 200, 190};
	if (cart_prop < 0 && cart_model >= 0)
		cart_prop = doors_prop(cart_model, tint);
	doors_prop_place(cart_prop, base, heading, pitch);
	for (size_t k = 0; k < sizeof(SEATS) / sizeof(SEATS[0]); k++) {
		const int i = who(SEATS[k].who);
		if (i < 0)
			continue;
		float at[3];
		const float local[3] = {SEATS[k].x, SEATS[k].y, SEATS[k].z};
		cart_point(base, heading, local, at);
		if (stage_t < dt * 1.5f) // (first frame: sat down)
			actors_script(i, 1), actors_script_clip(i, SEATS[k].clip, 0);
		actors_script(i, 1);
		actors_script_place(i, at, heading + SEATS[k].turn);
	}
	cart_point(base, heading, PLAYER_SEAT, player_pos);
	cart_point(base, heading, PLAYER_EYE, cart_eye);
	*player_heading = heading + PLAYER_TURN;
	if (horse >= 0) {
		// (ahead of the cart along its own heading: at the road's end too - it had stopped on it)
		const float ahead[3] = {0.0f, 0.0f, -HORSE_AHEAD_M};
		float hp[3];
		const float hh = heading;
		cart_point(base, heading, ahead, hp);
		terrain_height_at(hp[0], hp[2], &hp[1]);
		static float ht;
		ht += dt;
		const int moving = cart_s < route_len[route_n - 1] - 0.01f;
		actors_beast_pose(horse, hp[0], hp[1], hp[2], hh, moving ? "walk" : "idle", ht, NULL, 0.0f, 0.0f, dt);
	}
}

// The first cart (Ulfric's men in it), ahead of yours down the road the whole ride, and parked in the
// square first - beside where yours stops (Skyrim's MQ101Cart1Marker5) - its prisoners down and to the
// line as you come in (the reference: it's ahead from the first, through the gate before you)
#define LEAD_AHEAD_M 12.0f
static int lead_prop = -1, lead_horse = -1, lead_driver = -1, lead_parked, lead_seated;
static const char *const LEAD_SEATS[4] = {"mq101stormcloakcartmalebroder", "mq101stormcloakcartfemale", "mq101stormcloakcartmale",
                                          "mq101stormcloakcartmale02"};
static void walk_line(const char *id);

static void lead_point(const float base[3], float heading, const float in[3], float out[3]) {
	const float c = cosf(heading), sn = sinf(heading);
	out[0] = base[0] + in[0] * c + in[2] * sn;
	out[1] = base[1] + in[1];
	out[2] = base[2] - in[0] * sn + in[2] * c;
}

static void lead_gone(void) {
	doors_prop_remove(lead_prop), lead_prop = -1;
	if (lead_horse >= 0)
		actors_beast_remove(lead_horse), lead_horse = -1;
	if (lead_driver >= 0)
		actors_beast_remove(lead_driver), lead_driver = -1;
	lead_parked = lead_seated = 0;
}

static void lead_cart_update(float dt) {
	const Marker *park = marker("mq101cart1marker5");
	if (!park || cart_model < 0 || route_n < 2)
		return;
	static float turn_s = -1.0f; // (where it leaves your road for its own spot: the last turn into the square)
	if (turn_s < 0.0f) {
		float best = 1e9f;
		for (int k = 0; k < route_n; k++) {
			const float d = hypotf(route[k][0] - 219.0f, route[k][2] - 1170.0f);
			if (d < best)
				best = d, turn_s = route_len[k];
		}
	}
	const float s1 = cart_s + LEAD_AHEAD_M;
	float base[3], h, pitch;
	road_at(fminf(s1, turn_s), base, &h, &pitch);
	{ // (its heading along the road between its axles, eased as ours is: the road's own heading at its
	  // middle swung it broadside at each bend of the path, sliding sideways behind its horse)
		float fr[3], bk[3], fh, bh;
		road_at(fminf(s1, turn_s) + CART_AXLE_M, fr, &fh, NULL);
		road_at(fmaxf(0.0f, fminf(s1, turn_s) - CART_AXLE_M), bk, &bh, NULL);
		if (hypotf(fr[0] - bk[0], fr[2] - bk[2]) > 0.5f)
			h = atan2f(-(fr[0] - bk[0]), -(fr[2] - bk[2]));
		static float eased;
		static int eased_on;
		if (stage_t < dt * 1.5f)
			eased_on = 0;
		if (!eased_on)
			eased = h, eased_on = 1;
		float dh = h - eased;
		while (dh > (float)M_PI)
			dh -= 2.0f * (float)M_PI;
		while (dh < -(float)M_PI)
			dh += 2.0f * (float)M_PI;
		eased += dh * (1.0f - expf(-dt * CART_EASE));
		h = eased;
	}
	float u = 0.0f;
	if (s1 > turn_s) { // (off the road onto its spot, turning to face as it parks)
		const float d = fmaxf(1.0f, hypotf(park->pos[0] - base[0], park->pos[2] - base[2]));
		u = fminf(1.0f, (s1 - turn_s) / d);
		float dh = park->heading - h;
		while (dh > (float)M_PI)
			dh -= 2.0f * (float)M_PI;
		while (dh < -(float)M_PI)
			dh += 2.0f * (float)M_PI;
		const float e = u * u * (3.0f - 2.0f * u);
		base[0] += (park->pos[0] - base[0]) * e, base[2] += (park->pos[2] - base[2]) * e;
		h += dh * e, pitch *= 1.0f - e;
		terrain_height_at(base[0], base[2], &base[1]);
	}
	static const uint8_t tint[3] = {205, 200, 190};
	if (lead_prop < 0)
		lead_prop = doors_prop(cart_model, tint);
	doors_prop_place(lead_prop, base, h, pitch);
	static float t;
	t += dt;
	if (lead_horse < 0 && horse_kind >= 0)
		lead_horse = actors_beast_spawn(horse_kind);
	if (lead_horse >= 0) {
		const float ahead[3] = {0.0f, 0.0f, -HORSE_AHEAD_M};
		float hp[3];
		lead_point(base, h, ahead, hp);
		terrain_height_at(hp[0], hp[2], &hp[1]);
		actors_beast_pose(lead_horse, hp[0], hp[1], hp[2], h, u < 1.0f ? "walk" : "idle", t, NULL, 0.0f, 0.0f, dt);
	}
	static int driver_kind = -2;
	if (driver_kind == -2)
		driver_kind = actors_beast_kind("person_326", "@male");
	if (lead_driver < 0 && driver_kind >= 0)
		lead_driver = actors_beast_spawn(driver_kind);
	if (lead_driver >= 0) {
		const float seat[3] = {0.0f, BED_Y, -0.45f};
		float at[3];
		lead_point(base, h, seat, at);
		actors_beast_pose(lead_driver, at[0], at[1], at[2], h, "chair_idle", t, NULL, 0.0f, 0.0f, dt);
	}
	if (lead_parked)
		return;
	for (int k = 0; k < 4; k++) { // (sat two to a side, facing in)
		const int i = who(LEAD_SEATS[k]);
		if (i < 0)
			continue;
		const float side = k % 2 ? 0.55f : -0.55f, seat[3] = {side, BED_Y, k < 2 ? 1.3f : 2.5f};
		float at[3];
		lead_point(base, h, seat, at);
		if (!lead_seated)
			actors_script(i, 1), actors_script_headless(i, 0), actors_script_clip(i, "chair_idle", 0);
		actors_script_place(i, at, h + (side > 0.0f ? 1.5708f : -1.5708f));
	}
	lead_seated = 1;
	if (u >= 1.0f) { // (parked: down over the back, to the line, one after another)
		lead_parked = 1;
		for (int k = 0; k < 4; k++) {
			const int i = who(LEAD_SEATS[k]);
			if (i < 0)
				continue;
			const float down[3] = {(k % 2 ? 1.0f : -1.0f) * 0.5f, 0.0f, 3.2f + 0.6f * (float)k};
			float at[3];
			lead_point(base, h, down, at);
			terrain_height_at(at[0], at[2], &at[1]);
			actors_script_place(i, at, h + (float)M_PI), actors_script_clip(i, "", 0);
			walk_line(LEAD_SEATS[k]); // (to their place in the line, at Ulfric's left)
		}
		game_log("opening: the first cart parked at %.1f %.1f facing %.2f, its prisoners to the line", base[0], base[2], h);
	}
}

// General Tullius and Elenwen, mounted just inside Helgen's gate as the carts come in (Skyrim's
// markers: a line of theirs
// ) - their horses, and they on them
#define MOUNT_SEAT_M 1.0f  // (where a rider's put over the horse's feet: the seat's clip lifts the hips from there)
#define MOUNTS_FROM_M 200.0f // (there from this far before the cart's stop: in place before INFO 000F5E78, 28 m short of the gate - from 130 m they came only after it)
static int mounts[2] = {-1, -1};
static int justiciars[2] = {-1, -1}; // (the Thalmor with Elenwen: two soldiers at their marks)

// Where Tullius, Elenwen and her Thalmor wait as the carts come in: in the street ahead of the cart past the gate, to
// its right, facing it - in sight as Ralof says INFO 000F5E78 (the reference's 2:20-2:40; at Skyrim's
// marker on the terrace they were some 60 degrees off to the right, out of view, the user's Nov 8 run). Falls back to
// the marker (no gate on the road found)
#define LOOK_AT_HIM_S 3.5f       // (the view turned to them this long as Ralof points them out)
#define MOUNTS_PAST_GATE_M 24.0f // (this far ahead of the gate, the way the cart comes through it...)
#define MOUNTS_ASIDE_M 5.5f      // (... this far to its right: on the street's verge as the cart goes by them)
static int mounts_spot(int k, float out[3], float *heading) {
	static const char *const MARK[4] = {"mq101elenwenhorse", "mq101elenwenhorse", "mq101justiciar1marker1", "mq101justiciar2marker1"};
	if (gate_s < 0.0f) {
		const Marker *m = marker(MARK[k]);
		if (!m)
			return 0;
		memcpy(out, m->pos, 3 * sizeof(float)), *heading = m->heading;
		return 1;
	}
	float p[3], h;
	road_at(gate_s + MOUNTS_PAST_GATE_M, p, &h, NULL); // (beside the cart's own road, this far along it past the gate: it bends
	// left there - measured straight on from the gate they stood 17 m off it, up on a house's deck behind its posts)
	const float fx = -sinf(h), fz = -cosf(h), rx = cosf(h), rz = -sinf(h); // (the road's way on, and its right)
	static const float ALONG[4] = {0.0f, 0.0f, -2.2f, 2.2f}, ASIDE[4] = {0.0f, 0.0f, 1.6f, 1.6f}; // (the horse; the Thalmor either side, back)
	const float aside = MOUNTS_ASIDE_M + ASIDE[k];
	out[0] = p[0] + fx * ALONG[k] + rx * aside, out[2] = p[2] + fz * ALONG[k] + rz * aside, out[1] = p[1];
	terrain_height_at(out[0], out[2], &out[1]);
	*heading = h + (float)M_PI; // (facing back down the road, the carts coming)
	return 1;
}

static void mounts_update(float left_m, float dt) {
	// (Skyrim's markers: Elenwen mounted up on the terrace inside the gate, Tullius by her - seen as
	// the carts come in, INFO 000F5E78; not on the cart's road)
	static const char *const WHO[2] = {"elenwen", "generaltullius"};
	if (left_m > MOUNTS_FROM_M || horse_kind < 0)
		return;
	static int told;
	if (stage_t < 1.0f)
		told = 0;
	if (!told) {
		float sp[3], sh;
		if (mounts_spot(0, sp, &sh))
		{
			float off = 1e9f; // (how far off the cart's road they stand: it mustn't drive through them)
			for (int k = 0; k < route_n; k++)
				off = fminf(off, hypotf(route[k][0] - sp[0], route[k][2] - sp[2]));
			game_log("opening: Tullius, Elenwen and her Thalmor in the street past the gate (%.0f %.0f), %.0f m before the cart's stop, %.1f m off its road",
			         sp[0], sp[2], left_m, off);
		}
		told = 1;
	}
	{ // (a test's check: where they are as Ralof's INFO 000F5E78 begins)
		static int said;
		if (stage_t < 1.0f)
			said = 0;
		float e[3], t[3];
		if (!said && !strcmp(run.name, "mq101scene1") && run.phase >= 25 && actors_where(who("elenwen"), e) && actors_where(who("generaltullius"), t)) {
			float cp[3], ch;
			road_at(cart_s, cp, &ch, NULL);
			float es[3], ts[3];
			game_log("opening: at 'Look at him' - the cart at %.0f %.0f heading %.0f deg; Elenwen at %.0f %.1f %.0f (shown %d), Tullius at %.0f %.1f %.0f (shown %d), horse %d",
			         cp[0], cp[2], ch * 57.3f, e[0], e[1], e[2], actors_shown(who("elenwen"), es), t[0], t[1], t[2],
			         actors_shown(who("generaltullius"), ts), mounts[0]);
			said = 1;
		}
		// (INFO 000F5E78: your eyes go to them a moment, as Ralof's do - then yours again. Left where you
		// happened to be looking, they went by unseen: the user never saw them in four builds)
		static float look_t;
		if (!said)
			look_t = 0.0f;
		else if ((look_t += dt) < LOOK_AT_HIM_S && actors_where(who("elenwen"), e))
			e[1] += 0.4f, look_toward(e);
	}
	{ // Tullius on foot beside her horse, off the road: his own marker for it is on the cart's road -
	  // the cart drove through him and his horse
		static Marker spot1;
		const Marker *m = mounts_spot(1, spot1.pos, &spot1.heading) ? &spot1 : NULL;
		const int i = who(WHO[1]);
		if (m && i >= 0 && !actors_script_walking(i)) {
			const float c = cosf(m->heading), sn = sinf(m->heading);
			float by[3] = {m->pos[0] - c * 1.6f, m->pos[1], m->pos[2] + sn * 1.6f};
			const float other[3] = {m->pos[0] + c * 1.6f, m->pos[1], m->pos[2] - sn * 1.6f};
			float near_a = 1e9f, near_b = 1e9f; // (the side of the horse away from the road)
			for (int k = 0; k < route_n; k++) {
				near_a = fminf(near_a, hypotf(route[k][0] - by[0], route[k][2] - by[2]));
				near_b = fminf(near_b, hypotf(route[k][0] - other[0], route[k][2] - other[2]));
			}
			if (near_b > near_a)
				memcpy(by, other, sizeof(by));
			actors_script(i, 1), actors_script_place(i, by, m->heading);
			actors_script_clip(i, "", 0);
			actors_script_far(i, 1); // (seen from the cart, 35 m off: past the reach for people a short-of-memory Vita draws to -
			// 21 m in the user's run, Nov 10: the fourth time they weren't there)
		}
	}
	for (int k = 0; k < 2; k++) { // (Elenwen's escort: Thalmor soldiers where Skyrim stands its justiciars)
		static Marker jspot[2];
		const Marker *m = mounts_spot(2 + k, jspot[k].pos, &jspot[k].heading) ? &jspot[k] : NULL;
		static int jkind = -2;
		if (jkind == -2)
			jkind = actors_beast_kind("person_160", "@male");
		if (!m || jkind < 0)
			continue;
		if (justiciars[k] < 0)
			justiciars[k] = actors_beast_spawn(jkind);
		if (justiciars[k] >= 0) {
			actors_beast_look(justiciars[k], 1, 0.0f); // (from afar too)
			actors_beast_pose(justiciars[k], m->pos[0], m->pos[1], m->pos[2], m->heading, "idle", stage_t, NULL, 0.0f, 0.0f, dt);
		}
	}
	for (int k = 0; k < 1; k++) {
		static Marker hspot;
		const Marker *m = mounts_spot(0, hspot.pos, &hspot.heading) ? &hspot : NULL;
		if (!m)
			continue;
		if (mounts[k] < 0)
			mounts[k] = actors_beast_spawn(horse_kind);
		const int i = who(WHO[k]);
		const float seat[3] = {m->pos[0], m->pos[1] + MOUNT_SEAT_M, m->pos[2]};
		if (i >= 0 && !actors_script_walking(i)) { // (held in the saddle till the cart's stopped)
			actors_script(i, 1), actors_script_place(i, seat, m->heading);
			actors_script_clip(i, "chair_idle", 0);
			actors_script_far(i, 1);
		}
		if (mounts[k] >= 0)
			actors_beast_look(mounts[k], 1, 0.0f);
		if (mounts[k] >= 0)
			actors_beast_pose(mounts[k], m->pos[0], m->pos[1], m->pos[2], m->heading, "idle", stage_t, NULL, 0.0f, 0.0f, dt);
	}
}

static void cart_gone(void) {
	actors_script_far(who("elenwen"), 0), actors_script_far(who("generaltullius"), 0);
	for (int k = 0; k < 2; k++) {
		if (mounts[k] >= 0)
			actors_beast_remove(mounts[k]), mounts[k] = -1;
		if (justiciars[k] >= 0)
			actors_beast_remove(justiciars[k]), justiciars[k] = -1;
	}
	doors_prop_remove(cart_prop), cart_prop = -1;
	lead_gone();
	if (horse >= 0)
		actors_beast_remove(horse), horse = -1;
}

// --- The gates: where the opening waits for you ---------------------------------------------

static float gate_feet[3];

static int near_actor(const char *id, float m) {
	float at[3];
	const int i = who(id);
	return i >= 0 && actors_where(i, at) && hypotf(at[0] - gate_feet[0], at[2] - gate_feet[2]) < m;
}

static float head_off_t = -1.0f; // (the first prisoner's head off: this long ago; -1 not yet)

static int gate(void) {
	if (!strcmp(run.name, "mq101scene4") && run.phase == 41) // (the blow lands before INFO 00108CEA)
		return head_off_t > 0.5f;
	if (!strcmp(run.name, "mq101scene1") && run.phase >= 19 && run.phase < 21) // (INFO 000F778A: at the gate)
		return gate_s < 0.0f || cart_s >= gate_s - CART_GREET_M || run.t > 25.0f;
	if (!strcmp(run.name, "mq101scene4"))
		return run.phase != 15 || race_chosen; // (INFO 0003F88B: the race menu and your name first)
	if (!strcmp(run.name, "mq101dragonattackscene1")) // (Ralof leads: into the tower, up it)
		return !(run.phase == 3 || run.phase == 10 || run.phase == 13 || run.phase == 19) || near_actor("ralof", FOLLOW_GATE_M);
	if (!strcmp(run.name, "mq101dragonattackscenecperchwall")) {
		if (run.phase == 0)
			return !actors_script_walking(who("hadvar")) && run.t > 1.0f;
		if (run.phase == 2)
			return al.mode != AL_PERCH || run.t > 15.0f;
	}
	if (!strncmp(run.name, "mq101dragonattackscene", 22) && strcmp(run.name, "mq101dragonattackscenedtokeep"))
		return near_actor("hadvar", FOLLOW_GATE_M + 3.0f) || run.t > 6.0f; // (Hadvar leads through the town: not forever)
	if ((!strcmp(run.name, "mq102ralofgerdurscene") || !strcmp(run.name, "mq102hadvaralvorscene")) && run.phase <= 7) {
		// ("Gerdur!" ... "Brother!": not called across the river - Skyrim's approach packages bring
		// them together first; the greeting waits for that, a few seconds at most)
		float a[3], b[3];
		const int i = who(with == WITH_RALOF ? "ralof" : "hadvar"), j = who(with == WITH_RALOF ? "gerdur" : "alvor");
		if (i >= 0 && j >= 0 && actors_where(i, a) && actors_where(j, b) &&
		    (hypotf(a[0] - b[0], a[2] - b[2]) > MEET_TOGETHER_M || fabsf(a[1] - b[1]) > 1.5f) && run.t < MEET_WAIT_S)
			return 0;
	}
	if (!strcmp(run.name, "mq102ralofgerdurscene") && run.phase >= 21 && run.phase <= 23) {
		// (Frodnar's INFO 00054E92 at the clearing, as the reference (2:48): Gerdur walked there from the mill - some
		// 20 s, the river's walkway between - and he's run up to Ralof; a phase waited 8 s at most for her, and his
		// lines played at the mill, he nowhere near)
		const Marker *gm = marker("mq102gerduratclearingmarker");
		float g[3], r[3], fr[3];
		const int gi = actor_id("gerdur"), ri = actor_id("ralof"), fi = actor_id("frodnar");
		const int gerdur_there = !gm || gi < 0 || !actors_where(gi, g) || hypotf(g[0] - gm->pos[0], g[2] - gm->pos[2]) < CLEARING_THERE_M;
		const int frodnar_there = fi < 0 || ri < 0 || !actors_where(fi, fr) || !actors_where(ri, r) || hypotf(fr[0] - r[0], fr[2] - r[2]) < 2.6f;
		if (!(gerdur_there && frodnar_there) && run.t < CLEARING_WAIT_S)
			return 0;
	}
	if (!strcmp(run.name, "mq102ralofgerdurscene") && run.phase >= 30 && run.phase < 32) { // (the talk at the stump: once they're all there)
		static const char *const AT[] = {"gerdur", "ralof", "hod"};
		for (int k = 0; k < 3; k++)
			if (actors_script_walking(actor_id(AT[k])) && run.t < 40.0f)
				return 0;
	}
	if (!strcmp(run.name, "mq102hadvaralvorscene") && run.phase >= 17) { // (Alvor's: the same, his folk about him)
		static const char *const AT[] = {"alvor", "hadvar"};
		for (int k = 0; k < 2; k++)
			if (actors_script_walking(actor_id(AT[k])) && run.t < 40.0f)
				return 0;
	}
	if (!strncmp(run.name, "mq102", 5) && run.phase >= 17) // (Riverwood: on to the clearing with you there, not ahead of you)
		return near_actor(with == WITH_RALOF ? "gerdur" : "alvor", 14.0f) || run.t > 12.0f;
	return 1;
}

// --- The stages ---------------------------------------------------------------------------------

int intro_start(void) {
	if (!loaded)
		return 0;
	first_beheaded = 0, meteors_on = 0;
	with = WITH_NONE, cart_s = CART_SPEED * OPEN_SOONER_S, race_chosen = 0, lying = 0, ruin_said = 0, fade = 1.0f, fade_to = 0.0f, chosen_answer = -1;
	sc.n = sc.i = 0, sc.speaker = sc.listener = -1, run.active = 0, look_on = 0, player_moving = 0, lead_on = 0;
	memset(after, 0, sizeof(after));
	go(ST_CART);
	lighting_set_hours(OPENING_HOUR); // (a morning, as Skyrim's)
	objects_set_switch(SWITCH_ATTACK, 0), objects_set_switch(SWITCH_RUINS, 0);
	// The prisoner's rags, nothing else
	const int rags = quests_item("rags"), shoes = quests_item("rags_shoes");
	if (rags >= 0)
		items_add(rags, 1);
	if (shoes >= 0)
		items_add(shoes, 1);
	if (rags >= 0 && !equip_worn(rags))
		equip_toggle(rags);
	if (shoes >= 0 && !equip_worn(shoes))
		equip_toggle(shoes);
	const int cuffs = quests_item("cuffs"); // (hands bound, till Ralof / Hadvar cuts them loose in the keep)
	if (cuffs >= 0) {
		if (items_have(cuffs) <= 0)
			items_add(cuffs, 1);
		if (!equip_worn(cuffs))
			equip_toggle(cuffs);
	}
	quests_set(Q_UNBOUND, 1);
	return 1;
}

// The cart's view before the first frame: intro_update doesn't run while a loading or fade-in
// transition is on, so the camera took the player's own spot - under the cart, on the ground the
// land was still streaming in - for the first second. Here the cart and everyone on it are put
// in place (as intro_update's first frame would) and the eye set to the player's seat at once.
void intro_prime_view(IntroFrame *f) {
	if (!loaded || stage != ST_CART || stage_t > 0.5f || cart_s > CART_SPEED * OPEN_SOONER_S + 0.01f)
		return;
	f->fade = 1.0f; // (the loading screen's own fade out showed the cart a moment before the black came: black now)
	float heading;
	const float kept_t = stage_t;
	stage_t = 0.0f; // (the first-frame set-up in cart_place and the seats runs each call: nothing's moved yet)
	cart_place(1.0f / 60.0f, &heading, f->pos);
	stage_t = kept_t; // (put back: reset every frame, intro_update's own first frame - its teleport, a loading screen - came round again and again)
	f->hold = f->lock_move = 1;
	f->eye_set = 1;
	memcpy(f->eye, cart_eye, sizeof(f->eye));
	face(heading + OPEN_LOOK_TURN, -4.0f); // (Skyrim's first shot: ahead over the driver's back, Ralof at your right)
	f->face = 1, f->look_yaw = face_yaw, f->look_pitch = face_pitch;
}

int intro_first_person(void) {
	return intro_active() && ((stage <= ST_RACE && !race_chosen) || stage == ST_EXECUTION || stage == ST_BLOCK); // (the block: your own eyes, as Skyrim's)
}

int intro_ready(void) {
	return loaded;
}

int intro_on_cart(void) {
	return intro_active() && stage == ST_CART;
}

int intro_active(void) {
	return stage != ST_OFF && stage != ST_DONE;
}

void intro_race_chosen(void) {
	race_chosen = 1;
}

void intro_choose(int ralof) {
	chosen_answer = ralof ? WITH_RALOF : WITH_HADVAR;
}

enum { AMB_WAIT, AMB_COMING, AMB_COVER, AMB_SHOUT, AMB_FIGHT, AMB_KEY, AMB_SEARCH, AMB_FOUND, AMB_DONE };
#define KEY_KNEEL_FROM_M 0.7f // (Ralof down on one knee this far from the captain's body: beside her)
static int amb = AMB_DONE; // (the keep's ambush at the gate: Skyrim's MQ101KeepIntroScene phases 21-35 - the Imperials come, Ralof's men kill them)
static float amb_t;
static float key_body[3]; // (the body Ralof searches for the keep's key)
static int key_body_set;
static int amb_foe[2] = {-1, -1}; // (the two Imperials at the gate, as foes: their spawns)

static int test_to_end; // (testing: on to the caves' last chamber once the gear's given)

static int keep_jump; // (a test jump to the keep: straight through its door, not walked to it)
void intro_jump_to_keep(int ralof) {
	keep_jump = 1;
	test_to_end = ralof > 1;
	ralof = ralof != 0;
	amb = AMB_DONE; // (a test jump: no ambush)
	cart_gone();
	actors_player_ride(NULL, 0.0f, NULL);
	chosen_answer = ralof ? WITH_RALOF : WITH_HADVAR;
	go(ST_CHOICE);
	fade = 1.0f, fade_to = 0.0f;
}

void intro_jump_to_lineup(void) {
	cart_s = route_len[route_n - 1];
	go(ST_CART);
	stage_t = 1e5f, fade = 1.0f, fade_to = 0.0f; // (as the cart stops)
}

static int follower(void);

void intro_jump_to_riverwood(int ralof) { // (testing: out of the caves, on the road with them)
	cart_gone();
	actors_player_ride(NULL, 0.0f, NULL);
	with = ralof ? WITH_RALOF : WITH_HADVAR;
	const int fw = follower();
	actors_script(fw, 1), actors_script_follow(fw, 1);
	go(ST_RIVERWOOD);
	fade = 1.0f, fade_to = 0.0f;
}

int intro_died(void) {
	if (stage != ST_KEEP)
		return 0;
	chosen_answer = with, lead_on = 0;
	go(ST_TOKEEP);
	stage_t = 10.0f, fade = fade_to = 1.0f; // (straight in again, behind black)
	return 1;
}

static int test_to_town; // (testing: past the tower, landed in the inn - Hadvar's way through the town)
static int test_wall; // (testing: the town as it plays, you put beside Hadvar's mark as INFO 000DB8D5 begins - and later
                      //  walked on with him: to the keep's yard, the choice, Ralof's door)
void intro_jump_to_town(void) {
	go(ST_BLACKOUT);
	stage_t = 2.0f, fade = fade_to = 1.0f, jump_teleport = 1, test_to_town = 1;
}

void intro_jump_to_wall(void) {
	intro_jump_to_town();
	test_wall = 1;
}

static int test_climb; // (testing: the tower's stairs, at their foot, facing the wall)
void intro_jump_to_escape(void) {
	go(ST_BLACKOUT);
	stage_t = 2.0f, fade = fade_to = 1.0f, jump_teleport = 1;
}

void intro_jump_to_climb(void) {
	intro_jump_to_escape();
	test_climb = 1;
}

int intro_face_point(float out[3]) {
	return stage == ST_RACE && actors_where(who("hadvar"), out);
}

int intro_side(void) {
	if (stage != ST_TOKEEP && stage != ST_KEEP)
		return 0;
	return with == WITH_RALOF ? 1 : with == WITH_HADVAR ? 2 : 0;
}

int intro_stage(void) {
	return stage >= ST_KEEP && stage < ST_DONE ? stage * 4 + with : stage == ST_DONE ? 1 : 0;
}

void intro_load(int saved) {
	stage = saved > 1 ? saved / 4 : saved == 1 ? ST_DONE : ST_OFF;
	with = saved > 1 ? saved % 4 : WITH_NONE;
	if (stage == ST_KEEP || stage == ST_RIVERWOOD) { // (back with them: following you)
		const int f = actor_id(with == WITH_RALOF ? "ralof" : "hadvar");
		actors_script(f, 1);
		actors_script_follow(f, 1);
	}
	stage_t = 10.0f;
}

static int follower(void) {
	return actor_id(with == WITH_RALOF ? "ralof" : "hadvar");
}

int intro_markers(float out[][2], int max) {
	float at[3];
	int n = 0;
	if ((stage == ST_RIVERWOOD || stage == ST_MEET) && n < max) { // (on to Riverwood: Gerdur's / Alvor's)
		const int t = actor_id(with == WITH_RALOF ? "gerdur" : "alvor");
		if (t >= 0 && actors_where(t, at))
			out[n][0] = at[0], out[n][1] = at[2], n++;
	}
	if (stage == ST_KEEP && lead_on && n < max) // (the way out: where they're taking you)
		out[n][0] = lead_mark[0], out[n][1] = lead_mark[2], n++;
	const int r = actor_id("ralof"), h = actor_id("hadvar");
	if (stage == ST_TOWER && n < max && r >= 0 && actors_where(r, at))
		out[n][0] = at[0], out[n][1] = at[2], n++;
	if ((stage == ST_TOWN || stage == ST_LEAP) && n < max && h >= 0 && actors_where(h, at))
		out[n][0] = at[0], out[n][1] = at[2], n++;
	if (stage == ST_TOKEEP && n < max) { // (the keep's door they're running for)
		const Marker *door = marker(with == WITH_RALOF ? "#107a98" : "duncghadvaroutsidekeepdoormarker");
		if (door)
			out[n][0] = door->pos[0], out[n][1] = door->pos[2], n++;
	}
	if (stage == ST_CHOICE)
		for (int k = 0; k < 2 && n < max; k++)
			if (actors_where(k ? h : r, at))
				out[n][0] = at[0], out[n][1] = at[2], n++;
	return n;
}

// The lineup: everyone where Skyrim's scene begins (the carts emptied)
// The line, as Skyrim lays it out (its markers: Hadvar at MQ101HadvarMarker5b facing north, the
// player stepping up to MQ101PlayerLineMarker1 just north of him facing south): the prisoners stand
// in a row north of Hadvar, between him and the carts parked behind them, facing him - you behind
// Ulfric and Lokir, Ralof at your right, Ulfric's cart's men to your left. (We had you at
// MQ101PlayerMarker5, ten metres south: behind Hadvar, with your back to the carts; then two metres
// nearer the carts - standing in the back of your own)
static const struct { const char *who; float x, z; } LINE[] = {
	{"player", 209.2f, 1162.8f},
	{"ulfric", 209.1f, 1164.5f},
	{"mq101prisoner", 210.7f, 1164.6f},
	{"ralof", 207.9f, 1163.4f},
	{"mq101stormcloakcartmalebroder", 214.4f, 1163.4f},
	{"mq101stormcloakcartfemale", 212.9f, 1164.0f},
	{"mq101stormcloakcartmale", 214.0f, 1165.1f},
	{"mq101stormcloakcartmale02", 213.3f, 1162.4f},
};
#define LINE_Y 117.35f

static int line_spot(const char *id, float out[3]) {
	for (size_t k = 0; k < sizeof(LINE) / sizeof(LINE[0]); k++)
		if (!strcmp(LINE[k].who, id)) {
			out[0] = LINE[k].x, out[1] = LINE_Y, out[2] = LINE[k].z;
			terrain_height_at(out[0], out[2], &out[1]);
			return 1;
		}
	return 0;
}

// Standing in the line: turned to face Hadvar (walked there, they faced the way they came)
static void line_face_hadvar(void) {
	const Marker *h = marker("mq101hadvarmarker5b");
	for (size_t k = 1; h && k < sizeof(LINE) / sizeof(LINE[0]); k++) {
		const int i = who(LINE[k].who);
		float at[3];
		if (i >= 0 && !actors_script_walking(i) && !actors_script_has_clip(i) && actors_where(i, at) &&
		    hypotf(at[0] - LINE[k].x, at[2] - LINE[k].z) < 0.8f)
			actors_script_face(i, atan2f(-(h->pos[0] - at[0]), -(h->pos[2] - at[2])));
	}
}

static void walk_line(const char *id) {
	float to[3];
	const int i = who(id);
	if (i >= 0 && line_spot(id, to))
		actors_script(i, 1), actors_script_walk(i, to, 0);
}

static void place_line(const char *id) {
	float to[3];
	const int i = who(id);
	const Marker *h = marker("mq101hadvarmarker5b");
	if (i >= 0 && line_spot(id, to))
		actors_script(i, 1), actors_script_place(i, to, h ? atan2f(-(h->pos[0] - to[0]), -(h->pos[2] - to[2])) : 0.0f),
		    actors_script_clip(i, "", 0);
}

// (walking: off the cart's back to their places - the cart stopped, not a cut)
static void lineup(IntroFrame *f, int walking) {
	actors_script_headless(who("mq101stormcloakcartmalebroder"), 0); // (a new game's)
	float spot[3];
	if (line_spot("player", spot) && !walking) {
		memcpy(f->pos, spot, sizeof(f->pos));
		f->teleport = 1;
	} else if (line_spot("player", spot)) {
		memcpy(player_to, spot, sizeof(player_to)), player_moving = 1;
	}
	static const char *const OURS[] = {"ralof", "ulfric", "mq101prisoner"};
	for (int k = 0; k < 3; k++)
		(walking ? walk_line : place_line)(OURS[k]);
	if (!walking) // (the other cart's: walked off it as it parked; placed, jumping here)
		for (int k = 0; k < 4; k++)
			place_line(LEAD_SEATS[k]);
	place("hadvar", "mq101hadvarmarker5b", NULL);
	place("mq101imperialsoldierofficer", "mq101fortsoldier1marker6", NULL); // (at Hadvar's left, as the reference: Skyrim's mark beside his ledger)
	place("generaltullius", "mq101tulliusmarker5", NULL);
	place("elenwen", "mq101elenwenmarker5", NULL);
	place("mq101imperialpriest", "mq101priestwaitmarker1", NULL);
	place_headsman();
	place("mq101imperialsoldier01", "mq101guard1marker5", NULL);
	place("mq101imperialsoldier02", "mq101guard2marker5", NULL);
	look_toward_actor("hadvar", 1.6f);
}

// The headsman across the block from where they kneel, facing them, the axe at rest
static void place_headsman(void) {
	float at[3], h;
	const int i = who("mq101headsman");
	if (i < 0 || !block_frame(NULL, NULL, at, &h)) {
		place("mq101headsman", "mq101headsmanmarker2", "headsman_idle");
		return;
	}
	// (on the square's stones, at the block's own height: the land under Helgen's paving is lower - put on it,
	// he stood sunk to the waist)
	float ground = -1.0f;
	if (terrain_height_at(at[0], at[2], &ground) && ground > at[1])
		at[1] = ground;
	game_log("opening: the headsman at %.2f %.2f, %.2f up (the land under him %.2f)", at[0], at[2], at[1], ground);
	actors_script(i, 1), actors_script_place(i, at, h), actors_script_clip(i, "headsman_idle", 0);
}

// The square as the carts come into it: Hadvar with his list and the captain where the lineup has them,
// facing the carts as they pull in (Skyrim's MQ101HadvarMarker5b and MQ101FortSoldier1Marker5); the
// headsman and the priest at the block. (Not mq101imperialsoldier01, whose mark the carts' last turn
// crosses; not mq101imperialsoldier02, your driver - put at his mark, he stood up mid-ride)
static void arrival_staff(void) {
	place("hadvar", "mq101hadvarmarker5b", NULL);
	place("mq101imperialsoldierofficer", "mq101fortsoldier1marker6", NULL); // (at Hadvar's left, as the reference: Skyrim's mark beside his ledger)
	place("mq101imperialpriest", "mq101priestwaitmarker1", NULL);
	place_headsman();
	actors_script_prop(who("hadvar"), 1);
	game_log("opening: the square's staff in place as the carts come in");
}

void intro_update(float dt, const float feet[3], int interior, unsigned pressed, IntroFrame *f) {
	memset(f, 0, sizeof(*f));
	f->enter = -1, f->use_door = -1, f->prompt_door = -1;
	{ // Ulfric gagged (MQ101's alias: ArmorGag) on the cart, in the line and at the block - not after: in the
	  // tower he speaks (INFO 001095D4), and he was still gagged there and on
		static int ulfric = -1;
		if (ulfric < 0)
			ulfric = who("ulfric");
		actors_script_prop(ulfric, intro_active() && stage >= ST_CART && stage < ST_BLACKOUT);
	}
	if (!intro_active())
		return;
	stage_t += dt;
	f->bound = stage < ST_KEEP; // (a prisoner: hands tied till they're cut in the keep)
	memcpy(ear, feet, sizeof(ear));
	memcpy(gate_feet, feet, sizeof(gate_feet));
	talk_hush(1.0f); // (no passing hellos: a scene's going on)
	speech_step(dt);
	run_step(dt);
	follow_step(dt);
	// Helgen under attack: its soldiers and people running, place to place (Skyrim's flee packages)
	if (stage >= ST_TOWER && stage <= ST_CHOICE) {
		static const char *const RUNNERS[] = {"mq101imperialsoldier03", "mq101imperialsoldier02", "mq101imperialsoldier03",
		                                      "ingrid", "mq101imperialsoldier04"};
		static const char *const POINTS[] = {"mq101tulliusmarker6", "duncgtulliusdpatrol01marker", "mq101gunnarmarker8",
		                                     "duncghadvarnearinnmarker", "mq101hadvartokeepmarkerf", "duncgtulliusdscenemarker01",
		                                     "mq101choppingblock"};
		static float run_t;
		static unsigned turn;
		if ((run_t -= dt) <= 0.0f) {
			run_t = 0.8f;
			const int i = who(RUNNERS[turn % 5]);
			const Marker *to = marker(POINTS[(unsigned)rand() % 7]);
			float at[3];
			int busy = 0; // (in the scene's own moves just now: left to it)
			for (int w = 0; w < run.nw && run.active; w++)
				busy |= run.walkers[w] == i;
			if (i >= 0 && to && !busy) {
				actors_script(i, 1);
				if (!actors_where(i, at) || at[1] < -400.0f) { // (hidden when the attack began: back, by the block)
					const Marker *from = marker(POINTS[turn % 7]);
					if (from)
						actors_script_place(i, from->pos, 0.0f);
				} else if (!actors_script_walking(i)) { // (somewhere about the spot, not on it: spread out)
					const float spot[3] = {to->pos[0] + ((float)(rand() % 100) - 50.0f) * 0.08f, to->pos[1],
					                       to->pos[2] + ((float)(rand() % 100) - 50.0f) * 0.08f};
					actors_script_walk(i, spot, 1);
				}
			}
			turn++;
		}
	}
	alduin_update(dt, feet);
	if (head_beast >= 0)
		actors_beast_pose(head_beast, head_at[0], head_at[1], head_at[2], head_heading, "idle", 0.0f, NULL, 0.0f, 0.0f, dt);
	fade += (fade_to - fade) * fminf(1.0f, dt / FADE_S * 3.0f);
	if (fabsf(fade - fade_to) < 0.01f) // (all the way: a fade at all hides the prompts - "(O) Search")
		fade = fade_to;
	f->fade = fade;
	if (face_left > 0.0f) {
		f->face = 1, f->look_yaw = face_yaw, f->look_pitch = face_pitch;
		face_left -= dt;
	}
	if (player_moving) { // (walked by the scene: to the line, to the block)
		const float dx = player_to[0] - feet[0], dz = player_to[2] - feet[2], d = hypotf(dx, dz);
		f->hold = f->lock_move = 1;
		if (d < 0.05f) {
			player_moving = 0;
			memcpy(f->pos, player_to, sizeof(f->pos));
		} else {
			const float step = fminf(d, GLIDE_SPEED * dt);
			float ux = dx / d, uz = dz / d;
			// (round anyone in the way, not through them: off the cart, you walked straight
			// through the captain at her mark)
			// (the side you go round someone kept till you're past them - picked afresh each frame, it
			// flipped as you veered and you stuttered back and forth against Ulfric)
			static int round_who = -1;
			static float round_turn;
			int found = -1;
			float nearest = 1e9f, found_side = 0.0f;
			for (int i = 0; i < actors_count() && d > GLIDE_CLEAR_M; i++) {
				float at[3];
				if (!actors_where(i, at) || at[1] < -400.0f || fabsf(at[1] - feet[1]) > 2.0f)
					continue;
				const float ox = at[0] - feet[0], oz = at[2] - feet[2], ahead = ox * ux + oz * uz;
				const float side = ox * uz - oz * ux; // (which side of your way they're on)
				if (ahead > -0.2f && ahead < GLIDE_CLEAR_M * 2.5f && fabsf(side) < GLIDE_CLEAR_M * 1.3f && ahead < nearest &&
				    hypotf(at[0] - player_to[0], at[2] - player_to[2]) > GLIDE_CLEAR_M)
					nearest = ahead, found = i, found_side = side;
			}
			if (found < 0) {
				round_who = -1;
			} else {
				if (found != round_who)
					round_who = found, round_turn = found_side > 0.0f ? -1.0f : 1.0f;
				const float turn = round_turn, c = cosf(turn), sn = sinf(turn);
				const float nx = ux * c - uz * sn, nz = ux * sn + uz * c;
				ux = nx, uz = nz;
			}
			f->pos[0] = feet[0] + ux * step, f->pos[2] = feet[2] + uz * step;
			// (the height: from where you stand to the mark's - Helgen's paving, not the ground under it)
			f->pos[1] = feet[1] + (player_to[1] - feet[1]) * (step / d);
		}
	}
	const Marker *m;
	switch (stage) {
	case ST_CART: { // The ride: INFO 00101AEA
		static int helgen_lines, stopped;
		if (stage_t < dt * 1.5f)
			helgen_lines = stopped = 0;
		static float step_t, down_at[3];
		static int stepped;
		if (stopped == 3) { // (stepping down one after another, as Skyrim's - not all at once, into each other)
			f->lock_move = f->hold = 1;
			memcpy(f->pos, down_at, sizeof(f->pos));
			look_toward_actor("hadvar", 1.6f);
			step_t += dt;
			static const float STEP_EVERY_S = 1.1f;
			while (stepped < (int)(sizeof(SEATS) / sizeof(SEATS[0])) && step_t > STEP_EVERY_S * (float)(stepped + 1)) {
				const int i = who(SEATS[stepped].who);
				if (i >= 0) {
					// (down over the cart's back, toward the square behind it - not over its side into the lane)
					const float side[3] = {SEATS[stepped].x > 0.0f ? 0.7f : -0.7f, 0.0f, 3.3f + 0.6f * (float)stepped};
					float at[3];
					cart_point(cart_base, cart_heading, side, at);
					terrain_height_at(at[0], at[2], &at[1]);
					actors_script_place(i, at, cart_heading), actors_script_clip(i, "", 0);
				}
				stepped++;
			}
			if (stepped >= (int)(sizeof(SEATS) / sizeof(SEATS[0])) && step_t > STEP_EVERY_S * (float)(stepped + 1)) {
				lineup(f, 1);
				stopped = 2, stage_t = 1e5f;
			}
			break;
		}
		if (stopped == 2) { // (off the cart: walked to the line, the others with you)
			f->lock_move = 1;
			line_face_hadvar();
			look_toward_actor("hadvar", 1.6f);
			if (!player_moving || stage_t > 1e5f + 8.0f) {
				player_moving = 0;
				game_log("opening: in the line at %.1f %.1f", feet[0], feet[2]);
				run_scene("mq101scene4", 0, 15, 0); // (the list... Lokir runs... INFO 0003F88B)
				actors_script_prop(who("hadvar"), 1); // (his list and quill)
				go(ST_LINEUP);
			}
			break;
		}
		f->lock_move = f->hold = 1;
		if (stage_t < dt * 1.5f) { // (first frame: everyone in place)
			f->teleport = 1;
			if (horse_kind < 0)
				horse_kind = actors_beast_kind("horse_cart", "horse");
			if (horse < 0 && horse_kind >= 0)
				horse = actors_beast_spawn(horse_kind);
		}
		float heading;
		cart_place(dt, &heading, f->pos);
		lead_cart_update(dt);
		actors_player_ride(f->pos, heading, "chair_idle");
		f->eye_set = 1;
		memcpy(f->eye, cart_eye, sizeof(f->eye));
		if (stage_t < dt * 1.5f) // (Skyrim's first shot: ahead over the driver's back, Ralof at your right - heading's yours, across the cart)
			face(heading + OPEN_LOOK_TURN, -4.0f);
		{ // (free to look about, as Skyrim's; the view turns with the cart as it takes the road's bends)
			static float last;
			if (stage_t >= dt * 1.5f) {
				float d = heading - last;
				while (d > (float)M_PI)
					d -= 2.0f * (float)M_PI;
				while (d < -(float)M_PI)
					d += 2.0f * (float)M_PI;
				f->yaw_turn = -d * 180.0f / (float)M_PI;
			}
			last = heading;
		}
		if (stage_t < OPEN_FADE_END_S) // (the black, then the slow fade in)
			f->fade = fmaxf(f->fade, stage_t < OPEN_BLACK_S ? 1.0f : powf(1.0f - (stage_t - OPEN_BLACK_S) / (OPEN_FADE_END_S - OPEN_BLACK_S), 0.6f)); // (eased: dark a while, as Skyrim's slow fade - straight, it looked quick)
		f->card = 0, f->title = 0.0f;
		for (int k = 0; k < 4; k++)
			if (stage_t >= OPEN_CARDS[k][0] && stage_t <= OPEN_CARDS[k][1]) {
				f->card = k + 1;
				f->title = fminf(1.0f, fminf((stage_t - OPEN_CARDS[k][0]) / CARD_FADE_S, (OPEN_CARDS[k][1] - stage_t) / CARD_FADE_S));
			}
		const float end = route_len[route_n - 1];
		mounts_update(end - cart_s, dt);
		{ // (the staff in the square, there before the cart: 60 m out)
			static int staffed;
			if (stage_t < dt * 1.5f)
				staffed = 0;
			if (!staffed && end - cart_s < 60.0f)
				arrival_staff(), staffed = 1;
		}
		if (stage_t > CART_FIRST_LINE_S && stage_t - dt <= CART_FIRST_LINE_S)
			run_scene("mq101scene1", 0, 99, 0);
		{ // (the cart sets off once you can see; at the talk's pace - at the east gate as the soldier
		  // greets the General, as Skyrim's - then at a walk through the town)
			static float v;
			if (stage_t < dt * 1.5f) {
				v = CART_SPEED, gate_s = -1.0f;
				const Marker *g = marker("mq101helgeneastgate");
				float best = 1e9f;
				for (int k = 0; g && k < route_n; k++) {
					const float d = hypotf(route[k][0] - g->pos[0], route[k][2] - g->pos[2]);
					if (d < best)
						best = d, gate_s = route_len[k];
				}
			}
			float want = cart_greeted() ? CART_TOWN_SPEED : CART_SPEED;
			if (gate_s > 0.0f && !cart_greeted() && cart_s >= gate_s - CART_GREET_M) // (at the gate before the talk's got there: creep, as the carts queue)
				want = cart_s >= gate_s - 6.0f ? 0.0f : 0.5f;
			if (gate_s > 0.0f && !cart_greeted() && cart_s < gate_s - CART_GREET_M)
				want = fminf(CART_FAST, fmaxf(CART_SPEED, (gate_s - CART_GREET_M - cart_s) / fmaxf(cart_talk_left(), 3.0f)));
			v += (want - v) * (1.0f - expf(-dt * 0.5f));
			static int told;
			if (stage_t < dt * 1.5f)
				told = 0;
			if (!told && cart_greeted() && run.active)
				game_log("intro: the greeting at %.0f m of the road (the gate at %.0f, %.0f s in)", cart_s, gate_s, stage_t), told = 1;
			if (stage_t > 1.0f && stage_t < 1e4f)
				cart_s = fminf(end, cart_s + v * dt * fminf(1.0f, (stage_t - 1.0f) * 0.5f));
		}
		if (!helgen_lines && end - cart_s < ARRIVE_LINES_M && !speaking() && !run.active) {
			scene("mq101scene3", 0, 2); // (INFO 000AA799)
			helgen_lines = 1;
		}
		{ // Haming and Torolf by their house as the carts come into the square (Skyrim's MQ101TorriLedgeScene):
		  // INFO 000E6D54 ... INFO 00015B46 - after Ralof's lines
			static int torri;
			static float torri_t;
			if (stage_t < dt * 1.5f)
				torri = 0;
			if (torri == 0 && end - cart_s < ARRIVE_LINES_M + 10.0f && stage_t < 1e4f) {
				place("torolf", "mq101torolfstartmarker", NULL), place("haming", "mq101torristartmarker", NULL);
				torri = 1;
			}
			if (torri == 1 && helgen_lines && !speaking()) {
				scene("mq101torriledgescene", 0, 99);
				torri = 2;
			}
			if (torri == 2 && !speaking()) { // (INFO 00015B45: the two of them in, out of sight)
				const Marker *h = marker("mq101torristartmarker");
				if (h) {
					const float in[3] = {h->pos[0] + sinf(h->heading) * 4.0f, h->pos[1], h->pos[2] + cosf(h->heading) * 4.0f};
					actors_script_walk(who("haming"), in, 0), actors_script_walk(who("torolf"), in, 0);
				}
				torri = 3, torri_t = 0.0f;
			}
			if (torri == 3 && (torri_t += dt) > 4.0f)
				hide("haming"), hide("torolf"), torri = 4;
		}
		if (cart_s >= end && !stopped && (helgen_lines || stage_t > 1e4f) && !speaking()) {
			run_scene("mq101scene3", 6, 99, 0); // ("Whoa!" ... INFO 00046784; the soldiers to their places)
			stopped = 1;
		}
		if (stopped == 1 && !run.active && !speaking()) { // Off the cart, down to the ground, into the line
			float down[3];
			static const float BACK[3] = {-0.3f, 0.0f, 3.6f}; // (over its back, last of the four, as Skyrim's)
			cart_point(cart_base, cart_heading, BACK, down);
			terrain_height_at(down[0], down[2], &down[1]);
			actors_player_ride(NULL, 0.0f, NULL);
			memcpy(f->pos, down, sizeof(f->pos));
			f->hold = 1; // (put on the ground where you step down: not a teleport - that was a loading screen over two metres)
			memcpy(down_at, down, sizeof(down_at));
			game_log("opening: the cart stopped at %.1f %.1f facing %.2f; you step down at %.1f %.1f", cart_base[0], cart_base[2],
			         cart_heading, down[0], down[2]);
			step_t = 0.0f, stepped = 0;
			stopped = 3;
		}
		break;
	}
	case ST_LINEUP: {
		static int ledger; // (Hadvar, stood: his ledger held up to read from, the quill at it)
		const int hv = who("hadvar");
		if (stage_t < dt * 1.5f)
			ledger = 0;
		if (hv >= 0 && !ledger && !actors_script_walking(hv))
			actors_script_clip(hv, "ledger_idle", 0), ledger = 1;
		else if (hv >= 0 && ledger && actors_script_walking(hv))
			ledger = 0;
	}
		f->lock_move = !player_moving ? 1 : f->lock_move;
		if (run.active && !strcmp(run.name, "mq101scene4") && run.phase < 3)
			line_face_hadvar();
		if (phase_is("mq101scene4", 2) || (run.active && !strcmp(run.name, "mq101scene4") && run.phase >= 13 && run.phase <= 18))
			look_toward_actor("hadvar", 1.6f); // (INFO 0003F88B and where you're from: facing him through it)
		if (run.active && run.phase >= 7 && run.phase <= 12) // (Lokir runs: watching him)
			look_toward_actor("mq101prisoner", 1.2f);
		if (run.active && run.phase >= 7 && run.phase <= 9) { // (Lokir bolts for the gate: running)
			const int l = who("mq101prisoner");
			if (l >= 0 && run.t < dt * 1.5f) {
				float back[3], bh; // (back up the road the carts came in by: where you're looking)
				road_at(fmaxf(0.0f, route_len[route_n - 1] - 20.0f), back, &bh, NULL); // (past Hadvar, back the way the carts came)
				actors_script_walk(l, back, 1);
			}
		}
		static int shot;
		if (stage_t < dt * 1.5f)
			shot = 0;
		if (!shot && run.phase >= 10 && run.t > 0.9f && !strcmp(run.name, "mq101scene4")) { // ("Archers!": loosed as it's called)
			shot = 1;
			const int l = who("mq101prisoner");
			float at[3];
			sfx_play("bow_fire", 0.8f, 0.0f);
			if (l >= 0 && actors_where(l, at)) {
				actors_script_place(l, at, 0.0f);
				actors_script_clip(l, "death", 1);
				sfx_play_at("arrow_flesh", at, 60.0f);
			}
		}
		static float since_shot;
		since_shot = shot ? since_shot + dt : 0.0f;
		if (shot && since_shot < LOKIR_WATCH_S) // (his fall: watched)
			look_toward_actor("mq101prisoner", 0.4f);
		if (phase_is("mq101scene4", 15) && !speaking() && (!shot || since_shot > LOKIR_WATCH_S)) {
			look_toward_actor("hadvar", 1.6f);
			f->race_menu = 1; // (the race menu, then your name)
			go(ST_RACE);
		}
		break;
	case ST_RACE: {
		f->lock_move = 1;
		static float hv_at[3], hv_heading; // (Hadvar stood between the menu's camera and you: out of its way while you're made)
		static int hv_hidden;
		const int hv = who("hadvar");
		if (stage_t < dt * 1.5f && hv >= 0 && actors_where(hv, hv_at)) {
			const Marker *hm = marker("mq101hadvarmarker5b");
			hv_heading = hm ? hm->heading : 0.0f;
			hide("hadvar"), hv_hidden = 1;
		}
		if (race_chosen && hv_hidden) {
			actors_script_place(hv, hv_at, hv_heading), actors_script_clip(hv, "ledger_idle", 0);
			hv_hidden = 0;
		}
		if (race_chosen) {
			run_scene("mq101scene4", 16, 52, 0); // (INFO 000EF986 ...)
			go(ST_EXECUTION);
		}
		break;
	}
	case ST_EXECUTION: { // Tullius, the last rites, the first to the block
		if (stage_t < dt * 1.5f) { // (Ulfric out in front, facing where Tullius comes to speak to him - MQ101UlfricMarker6, as the
			// reference: his package's "to marker 6" never got him there, and Tullius spoke to him 8 m off, by the carts)
			const Marker *um = marker("mq101ulfricmarker6");
			const int u = who("ulfric");
			if (um && u >= 0) {
				actors_script(u, 1), actors_script_clip(u, "", 0);
				actors_script_walk(u, um->pos, 0);
				when_there(u, NULL, 0, um->heading);
			}
		}
		if (run.active && !strcmp(run.name, "mq101scene4") && run.phase >= 16 && run.phase <= 18)
			look_toward_actor("hadvar", 1.6f); // (where you're from, a line of theirs: facing him)
		f->lock_move = !player_moving ? 1 : f->lock_move;
		static int roar2;
		if (stage_t < dt * 1.5f)
			roar2 = 0;
		if (!roar2 && phase_is("mq101scene4", 50)) // (INFO 000D77ED: nearer)
			sfx_play("dragon_roar_far", 1.0f, 0.0f), roar2 = 1;
		static int roar1;
		if (stage_t < dt * 1.5f)
			roar1 = 0;
		if (!roar1 && !strcmp(run.name, "mq101scene4") && run.phase >= 30 && run.phase <= 33) // (as Tullius ends: INFO 00013A9A)
			sfx_play("dragon_roar_far", 1.0f, 0.0f), roar1 = 1;
		if (0) // (the first roar, far off over the mountains: INFO 00013A9A)
			sfx_play("dragon_roar_far", 1.0f, 0.0f);
		if (run.phase >= 25 && run.phase <= 29)
			look_toward_actor("generaltullius", 1.6f);
		{
			static int told_speech;
			if (stage_t < dt * 1.5f)
				told_speech = 0;
			float u[3], t2[3];
			if (!told_speech && run.phase == 25 && run.t > 2.0f && actors_where(who("ulfric"), u) && actors_where(who("generaltullius"), t2))
				game_log("opening: Tullius's speech - Ulfric at %.1f %.1f, Tullius at %.1f %.1f (%.1f m apart)", u[0], u[2], t2[0], t2[2],
				         hypotf(u[0] - t2[0], u[2] - t2[2])), told_speech = 1;
		}
		if (run.phase >= 37 && run.phase <= 45) { // (watching the block)
			const Marker *b = marker("mq101choppingblock");
			if (b) {
				const float at[3] = {b->pos[0], b->pos[1] + 0.8f, b->pos[2]};
				look_toward(at);
			}
		}
		static int chopped;
		static float chop_t;
		if (stage_t < dt * 1.5f)
			chopped = 0;
		if (run.phase >= 39 && !chopped) { // (he's at the block - placed there if he hasn't got there)
			const int first = who("mq101stormcloakcartmalebroder");
			const Marker *b = marker("mq101choppingblock");
			float fat[3];
			float kat[3], kh;
			if (first >= 0 && b && run.t > 3.0f && actors_where(first, fat) && block_frame(kat, &kh, NULL, NULL) &&
			    hypotf(fat[0] - kat[0], fat[2] - kat[2]) > 0.8f) {
				actors_script_place(first, kat, kh);
				actors_script_clip(first, "block_in", 1);
			}
		}
		if (!strcmp(run.name, "mq101scene4") && run.active && !chopped &&
		    (run.phase >= 42 || (run.phase == 41 && !speaking() && run.t > 1.0f))) { // (INFO 000F46DC: said)
			game_log("opening: the axe falls (phase %d)", run.phase);
			{ // (who stands where at the block, for checking against the reference)
				static const char *const AT_BLOCK[] = {"mq101stormcloakcartmalebroder", "mq101headsman", "mq101imperialsoldierofficer", "ulfric", "generaltullius"};
				const Marker *bl = marker("mq101choppingblock");
				for (size_t k = 0; k < sizeof(AT_BLOCK) / sizeof(AT_BLOCK[0]); k++) {
					float w[3];
					if (actors_where(who(AT_BLOCK[k]), w) && bl)
						game_log("opening: at the block - %s at %.2f %.2f (%.2f east, %.2f south of it)", AT_BLOCK[k], w[0], w[2],
						         w[0] - bl->pos[0], w[2] - bl->pos[2]);
				}
			}
			const int first = who("mq101stormcloakcartmalebroder");
			const Marker *b = marker("mq101choppingblock");
			float kat[3], kh;
			if (first >= 0 && b && block_frame(kat, &kh, NULL, NULL)) // (on his knees at the block's near side, as the axe comes up)
				actors_script_place(first, kat, kh), actors_script_clip(first, "block_idle", 0);
			// (Skyrim's headsman: the axe up and down in a two-handed power attack - "headsman_chop" is
			// the player's, raised and held for Alduin, and the blow never came down)
			actors_script_clip(who("mq101headsman"), "w2hw_power", 1);
			chopped = 1, chop_t = 0.0f, head_off_t = -1.0f; // (its own clock: the stage's reset "chopped" the next frame, and the axe never fell)
		}
		chop_t += dt;
		if (head_off_t >= 0.0f)
			head_off_t += dt;
		if (chopped == 1 && chop_t > 0.97f) { // (the axe lands - 0.97 s into Skyrim's 2hw_attackpower: the hands at the block)
			head_off_t = 0.0f;
			sfx_play("wpn_hit_blade", 1.0f, 0.0f);
			actors_script_headless(who("mq101stormcloakcartmalebroder"), 1);
			{ // (his head, in the crate by the block: to its right, a little ahead)
				const Marker *bm = marker("mq101choppingblock");
				const int hk = actors_beast_kind("person_322", "@male");
				if (bm && hk >= 0 && head_beast < 0 && (head_beast = actors_beast_spawn(hk)) >= 0) {
					actors_beast_headonly(head_beast, 1);
					const float fx = -sinf(bm->heading), fz = -cosf(bm->heading);
					head_at[0] = bm->pos[0] + fx * BLOCK_BASKET_M - fz * BLOCK_BASKET_SIDE_M;
					head_at[2] = bm->pos[2] + fz * BLOCK_BASKET_M + fx * BLOCK_BASKET_SIDE_M;
					head_at[1] = bm->pos[1] + BLOCK_BASKET_UP_M - HEAD_REST_M; // (the body's origin: its head rests in the basket)
					head_heading = bm->heading + 2.4f;
				}
			}
			chopped = 2;
		}
		if (chopped == 2 && chop_t > 1.6f) { // (then the captain's kick, and his body falls from the block where it knelt - Skyrim's
			// fall from the block, from its kneel: no standing up into a death fall, flung back - and lies there)
			const int first = who("mq101stormcloakcartmalebroder");
			// (the death clip from its fall on - its start stands the body up first: the "flip" - from where he knelt,
			// facing back the way he knelt so he goes down on the near side, by the block)
			float kat[3], kh;
			const Marker *bm = marker("mq101choppingblock");
			if (first >= 0 && block_frame(kat, &kh, NULL, NULL)) // (turned west, the death clip's fall: down before the block, on
				actors_script_place(first, kat, bm ? bm->heading : kh); //  the line's side - not across your view of the block)
			actors_script_clip_from(first, "death", 1, FIRST_BODY_FALL_FROM_S);
			first_beheaded = 1;
			actors_script_clip(who("mq101imperialsoldierofficer"), "guard_enter", 1);
			chopped = 4;
		}
		if (chopped == 4 && chop_t > 2.4f) {
			actors_script_clip(who("mq101headsman"), "headsman_idle", 0);
			chopped = 3;
		}
		if (!run.active && !speaking()) { // (INFO 000CD668: up to its near side)
			float kat[3], kh;
			if (player_block_frame(kat, &kh, NULL, NULL, NULL)) {
				kat[0] += sinf(kh) * BLOCK_STAND_BACK_M, kat[2] += cosf(kh) * BLOCK_STAND_BACK_M;
				memcpy(player_to, kat, sizeof(player_to)), player_moving = 1;
			}
			go(ST_BLOCK);
		}
		break;
	}
	case ST_BLOCK: { // Kneeling at the block; Alduin comes - as the reference (6:52-7:32): stood at the block's near side, a look
		// back at Hadvar, pushed down by the captain, the head in the basket below, the headsman's legs, then on the block, the
		// head turned, looking up at him and the tower beyond as Alduin lands on it; his shout throws the headsman down
		f->lock_move = 1;
		m = marker("mq101choppingblock");
		float kneel[3], kh, hs_at[3], hs_h, eye[3];
		const int framed = player_block_frame(kneel, &kh, eye, hs_at, &hs_h);
		static float at_block; // (since you got there)
		if (stage_t < dt * 1.5f)
			at_block = -1.0f;
		if (player_moving) {
			look_toward_actor("mq101headsman", 1.5f); // (walking up: the headsman across the block, facing you)
			break;
		}
		if (at_block < 0.0f) { // (the headsman across the block from you, before the tower - you looking back at Hadvar)
			at_block = 0.0f, game_log("opening: at the block - a look back at Hadvar");
			const int hm = who("mq101headsman");
			if (hm >= 0 && framed) {
				float ground = -1.0f;
				if (terrain_height_at(hs_at[0], hs_at[2], &ground) && ground > hs_at[1])
					hs_at[1] = ground;
				actors_script(hm, 1), actors_script_place(hm, hs_at, hs_h), actors_script_clip(hm, "headsman_idle", 0);
			}
		}
		at_block += dt;
		const int down = at_block > BLOCK_LOOK_HADVAR_S; // (pushed down onto the block)
		if (framed && m) {
			f->hold = 1;
			memcpy(f->pos, kneel, sizeof(f->pos));
			if (!down) { // (stood at the block a moment: you turn to Hadvar)
				f->pos[0] += sinf(kh) * BLOCK_STAND_BACK_M, f->pos[2] += cosf(kh) * BLOCK_STAND_BACK_M; // (behind it: north)
				actors_player_ride(NULL, 0.0f, NULL);
				look_toward_actor("hadvar", 1.6f);
			} else {
				const float t = at_block - BLOCK_LOOK_HADVAR_S;
				static int kicked;
				if (t < dt * 1.5f)
					kicked = 0;
				if (!kicked) // (marks for a test's pictures: the shove down, the head turned up at the headsman)
					game_log("opening: pushed down onto the block");
				static int looked_up;
				if (t < dt * 1.5f)
					looked_up = 0;
				if (t >= 3.2f && !looked_up)
					looked_up = 1, game_log("opening: on the block, looking up at the headsman");
				if (!kicked) { // (the captain behind you, the shove down)
					const int cap = who("mq101imperialsoldierofficer");
					const float behind[3] = {kneel[0] - sinf(kh) * -GUARD_BEHIND_M, kneel[1], kneel[2] - cosf(kh) * -GUARD_BEHIND_M};
					if (cap >= 0)
						actors_script_place(cap, behind, kh), actors_script_clip(cap, "guard_enter", 1);
					kicked = 1;
				}
				actors_player_ride(f->pos, kh, t < 1.4f ? "block_in" : "block_idle");
				f->eye_set = 1;
				memcpy(f->eye, eye, sizeof(f->eye));
				const Marker *perch = marker("mq101alduinperchmarker1");
				if (t < 1.6f) { // (down at the block before you, his feet beyond)
					const float low[3] = {hs_at[0], hs_at[1] + 0.15f, hs_at[2]};
					look_toward(low);
				} else if (t < 3.2f || !perch) { // (the headsman's legs, across the block)
					const float legs[3] = {hs_at[0], hs_at[1] + 0.8f, hs_at[2]};
					look_toward(legs);
				} else { // (the head turned on the block: up past him at the tower - Alduin coming down onto the middle of it)
					f->roll = YOU_ROLL_DEG * fminf(1.0f, (t - 3.2f) / 0.8f);
					const float up[3] = {perch->pos[0], perch->pos[1] + YOU_ALDUIN_UP_M, perch->pos[2]};
					look_toward(up);
				}
			}
		}
		static int called, raised;
		if (stage_t < dt * 1.5f)
			called = raised = 0;
		const float t_down = at_block - BLOCK_LOOK_HADVAR_S;
		if (down && t_down > BLOCK_ALDUIN_COMES_S && !called) { // (he comes over the mountains behind the tower, and down onto it)
			const Marker *perch1 = marker("mq101alduinperchmarker1");
			if (perch1) { // (from behind the tower, away from the block: from the west his road ran over you - his wings in your face)
				const float from[3] = {perch1->pos[0] + 70.0f, perch1->pos[1] + 12.0f, perch1->pos[2] + 14.0f};
				alduin_show(from);
			}
			static Action land = {"", "alduin", "mq101alduinperch1", "mq101alduinperchmarker1,mq101alduintowerattacktargetref"};
			alduin_package(&land);
			run_scene("mq101alduinattackscene1", 0, 99, 1); // (INFO 000D07EE - everyone running for it, as Skyrim's)
			called = 1;
		}
		const int landed = al.mode == AL_PERCH || al.mode == AL_GROUND;
		static float since_landed;
		if (stage_t < dt * 1.5f || !landed)
			since_landed = 0.0f;
		else
			since_landed += dt;
		if (landed && since_landed < ALDUIN_LAND_SHAKE_S) // (his weight on the tower: the ground shakes, as Skyrim's)
			f->shake = 1;
		if (called && !raised && ((landed && since_landed > BLOCK_RAISE_AFTER_LAND_S) || t_down > BLOCK_ALDUIN_COMES_S + 14.0f)) {
			actors_script_clip(who("mq101headsman"), "headsman_chop", 1); // (the axe up, he on the tower behind: the reference's 7:16)
			raised = 1;
		}

		static float burning; // (the shout: the yard thrown down, a moment, then black)
		if (stage_t < dt * 1.5f)
			burning = 0.0f;
		// Skyrim's MQ101AlduinAttackScene1: INFO 000D07EE, INFO 000D07ED, a line of theirs
		// , he lands on the tower, "Dragon!" - and at once (phase 7) his shout knocks the yard down
		const int in_scene = run.active && !strcmp(run.name, "mq101alduinattackscene1");
		if (called && burning == 0.0f &&
		    ((((in_scene && run.phase >= 7) || !in_scene) && raised && since_landed > BLOCK_SHOUT_AFTER_LAND_S) || t_down > BLOCK_ALDUIN_COMES_S + 24.0f)) {
			burning = dt;
			meteors_on = 1, al.meteor_t = 0.8f; // (the sky's fire from now: falling about the yard as you lie dazed)
			sfx_play("shout_force_3", 1.0f, 0.0f), sfx_play("dragon_roar", 1.0f, 0.0f);
			static const char *const CROWD[] = {"ralof", "stormcloakprisoner01", "stormcloakprisoner02",
			                                    "stormcloakprisoner03", "stormcloakprisoner04", "ulfric"};
			for (size_t k = 0; k < sizeof(CROWD) / sizeof(CROWD[0]); k++) {
				const int c = who(CROWD[k]);
				if (c >= 0)
					actors_script_clip(c, k % 2 ? "stagger" : "cower_in", 1), actors_script_clip_next(c, "cower");
			}
			actors_script_clip(who("mq101headsman"), "death", 1); // (thrown down by it, out of your sight, the axe raised)
			game_log("opening: Alduin's shout from the tower (%.1f s on the block)", t_down);
		}
		if (burning > 0.0f) { // (knocked off the block: the view rolls over, shaken, and dims - dazed - then black)
			burning += dt;
			f->shake = burning < 2.0f;
			f->roll = YOU_ROLL_DEG + (BLOCK_KNOCKED_ROLL_DEG - YOU_ROLL_DEG) * fminf(1.0f, burning / 1.0f);
			if (burning < 0.4f)
				ui_tint(255, 255, 255, burning / 0.4f * 0.6f);
			else if (burning < BLOCK_DAZED_S)
				ui_tint(60, 45, 30, fminf(0.85f, (burning - 0.4f) / 1.5f * 0.85f)); // (the dazed, dark blur of the reference)
			if (burning > BLOCK_DAZED_S) {
				fade_to = 1.0f;
				go(ST_BLACKOUT);
			}
		}
		break;
	}
	case ST_BLACKOUT: // The shout: white, then black; the town burning when you come to
		f->lock_move = 1;
		{ // (behind the black: the town's people's models read now, one a frame, 2 s of it at most - read as each came into view,
			// Haming's road froze for a second, Nov 8)
			static unsigned long long preload_us;
			if (stage_t < dt * 1.5f)
				preload_us = 0;
			if (stage_t > 0.5f && preload_us < 2000000ull && (m = marker("mq101choppingblock"))) {
				const unsigned long long t0 = stream_now_us();
				if (!actors_preload(m->pos[0], m->pos[2], 90.0f, 1))
					preload_us = 2000000ull; // (all read)
				else
					preload_us += stream_now_us() - t0;
			}
		}
		if (stage_t < 0.4f)
			ui_tint(255, 255, 255, 1.0f - stage_t / 0.4f);
		if (jump_teleport && (m = marker("mq101choppingblock"))) { // (a test: straight here)
			jump_teleport = 0;
			memcpy(f->pos, m->pos, sizeof(f->pos));
			f->teleport = 1;
		}
		if (stage_t > 3.0f) {
			m = marker("mq101choppingblock");
			if (m) { // (thrown from the block: on the ground beside it)
				lie_at[0] = m->pos[0] + 1.2f, lie_at[1] = m->pos[1], lie_at[2] = m->pos[2] + 0.6f;
				lie_heading = m->heading, lying = 1;
			}
			actors_player_ride(NULL, 0.0f, NULL);
			actors_script_prop(who("hadvar"), 0); // (the list dropped: a dragon)
			// (Helgen as he leaves it - on fire, fallen, the tower's wall broken open - comes with
			// Alduin's crash through that wall in the tower: the wall stands till he breaks it)
			cart_gone();
			alduin_gone();
			head_gone();
			actors_script_clip(who("mq101headsman"), "death", 1);
			release_cast("ralof"); // (Helgen's people scatter; Ralof stays: Hadvar comes later)
			const int h = who("hadvar");
			actors_script(h, 1);
			hide("hadvar");
			place("ulfric", "mq101ulfricmarker8", NULL);
			// (those its scenes speak for - heard, and nobody there: at their places from the first)
			place("generaltullius", "duncgtulliusdpatrol01marker", NULL);
			place("gunnarstoneeye", "mq101gunnarmarker8", NULL);
			place("torolf", "#db923", "death"); // (struck down: Haming's father)
			place("mq101imperialsoldier01", "duncgviloddwoundedmarker", NULL); // (at Vilod's side: INFO 000DF7A4)
			place("haming", "#db923", NULL);
			place("vilod", "duncgviloddwoundedmarker", "block_idle"); // (on their knees, wounded)
			place("matlara", "duncgmatlaradwoundedmarker", "block_idle");
			fade_to = 0.0f;
			run_scene("mq101dragonattackscene1", 0, 25, 1); // (INFO 000DE45B ... into the tower, up it, "Jump!")
			quests_set(Q_UNBOUND, 2);
			go(ST_TOWER);
		}
		break;
	case ST_TOWER: { // Ralof's tower: Alduin through the wall; the jump into the inn
		look_on = 0;
		if (stage_t < dt * 1.5f && !test_climb)
			test_climb = 0;
		if (test_to_town && stage_t > 1.0f) { // (a test: leapt already)
			test_to_town = 0, lying = 0;
			actors_player_ride(NULL, 0.0f, NULL);
			memcpy(player_to, feet, sizeof(player_to));
			go(ST_LEAP);
			stage_t = JUMP_S;
			break;
		}
		static int crashed;
		const int crashed_now = crashed;
		if (test_climb && stage_t > 1.0f) { // (a test: at the stairs' foot, turned up them)
			const Marker *foot = marker("mq101ralofmarker9"), *wall = marker("duncgtowerhole01ref");
			static int moved_once;
			if (stage_t < dt * 1.5f + 1.0f)
				moved_once = 0;
			if (!moved_once && foot) {
				moved_once = 1;
				lying = 0, actors_player_ride(NULL, 0.0f, NULL);
				memcpy(f->pos, foot->pos, sizeof(f->pos));
				f->teleport = 1;
			}
			const Marker *top = marker("mq101ralofmarker10");
			if (top && foot && !crashed_now) { // (up them, at a walking pace: the wall bursts as you reach the top)
				const float u = fminf(1.0f, fmaxf(0.0f, (stage_t - 2.0f) / 7.0f));
				for (int k = 0; k < 3; k++)
					f->pos[k] = foot->pos[k] + (top->pos[k] - foot->pos[k]) * u;
				f->hold = f->lock_move = 1;
			}
			if (wall && !crashed_now)
				look_toward(wall->pos);
		}
		if (lying && !test_climb) { // (on the ground, looking up at Ralof: INFO 000DE45B)
			f->hold = f->lock_move = f->eye_set = 1;
			memcpy(f->pos, lie_at, sizeof(f->pos));
			f->eye[0] = lie_at[0], f->eye[1] = lie_at[1] + 0.35f, f->eye[2] = lie_at[2];
			actors_player_ride(lie_at, lie_heading, "bedroll");
			look_toward_actor("ralof", 1.0f);
			if (phase_at_least("mq101dragonattackscene1", 2) && !speaking())
				lying = 0, actors_player_ride(NULL, 0.0f, NULL); // (up)
		}
		if (phase_is("mq101dragonattackscene1", 1))
			look_toward_actor("ralof", 1.5f);
		const Marker *hole = marker("duncgtowerhole01ref"), *inn = marker("mq101playerinnmovemarker");
		// Skyrim's: no dragon seen in here - at the top of the stairs the wall bursts open ahead of you:
		// rock and a fireball through the hole, the view shaking, bright sky behind it
		static float crash_t, burst_t, tower_away[2][3];
		if (stage_t < dt * 1.5f)
			crashed = 0, crash_t = 0.0f, burst_t = 0.0f;
		if (!crashed && hole) {
			const float flat = hypotf(hole->pos[0] - feet[0], hole->pos[2] - feet[2]);
			if ((feet[1] > hole->pos[1] - 1.5f && flat < 7.0f) || phase_at_least("mq101dragonattackscene1", 19)) // (near the stairs' top, as the reference)
				crashed = 1, crash_t = 0.0f, burst_t = 0.0f;
			if (crashed) {
				// (the reference: his head through the broken wall, fire from it - clinging outside, facing in;
				// then away from the tower, out and up - not through it, as he'd glitched before)
				const Marker *tower = marker("mq101towerstaggermarker");
				if (tower) {
					float ox = hole->pos[0] - tower->pos[0], oz = hole->pos[2] - tower->pos[2];
					const float d = fmaxf(hypotf(ox, oz), 0.1f);
					ox /= d, oz /= d;
					const float at[3] = {hole->pos[0] + ox * TOWER_HEAD_OUT_M, hole->pos[1] - 3.0f, hole->pos[2] + oz * TOWER_HEAD_OUT_M};
					alduin_show(at);
					if (al.beast >= 0) {
						memcpy(al.pos, at, sizeof(al.pos));
						for (int k = 0; k < 3; k++) // (his fire across the room, short of you)
							al.aim[k] = feet[k] + (hole->pos[k] - feet[k]) * 0.4f;
						al.mode = AL_PERCH, al.arrived = 1, al.breathe = 1, al.t = 0.0f;
						al.heading = atan2f(-(feet[0] - at[0]), -(feet[2] - at[2]));
						tower_away[0][0] = at[0] + ox * 18.0f, tower_away[0][1] = at[1] + 8.0f, tower_away[0][2] = at[2] + oz * 18.0f;
						tower_away[1][0] = at[0] + ox * 70.0f, tower_away[1][1] = at[1] + 35.0f, tower_away[1][2] = at[2] + oz * 70.0f;
					}
				}
				objects_set_switch(SWITCH_ATTACK, 1);
				sfx_play("trp_rockfall", 1.0f, 0.0f);
				sfx_play("dragon_roar", 1.0f, 0.0f);
				ui_tint(255, 255, 255, 0.7f);
				game_log("opening: the tower's wall bursts open (Alduin %d at %.1f %.1f %.1f, the hole %.1f %.1f %.1f h %.2f, you %.1f %.1f %.1f)",
				         al.beast, al.pos[0], al.pos[1], al.pos[2], hole->pos[0], hole->pos[1], hole->pos[2], hole->heading, feet[0], feet[1], feet[2]);
			}
		}
		if (crashed && al.beast >= 0 && al.mode == AL_PERCH && crash_t > TOWER_HEAD_S) { // (his fire spent: off, away from the tower)
			al.breathe = 0, breath_off();
			memcpy(al.path[0], tower_away[0], sizeof(al.path[0])), memcpy(al.path[1], tower_away[1], sizeof(al.path[1]));
			al.n = 2, al.i = 0, al.mode = AL_PATH, al.angle = -3.0f; // (a fly-off: gone at its end)
			memset(al.dir, 0, sizeof(al.dir));
			sfx_play_at("dragon_roar", al.pos, 150.0f);
		}
		if (crashed && al.beast >= 0 && al.mode == AL_PERCH && crash_t < TOWER_HEAD_S)
			breath_on();
		if (crashed && hole && (crash_t += dt) < TOWER_BURST_S) {
			f->shake = 1;
			f->lock_move = 1; // (thrown back a moment: the view held on the hole)
			look_toward(hole->pos);
			if ((burst_t -= dt) <= 0.0f) { // (the fireball: through the hole and across the room, rock with it)
				burst_t = 0.25f;
				const float u = crash_t / TOWER_BURST_S;
				const float at[3] = {hole->pos[0] + (feet[0] - hole->pos[0]) * u * 0.8f + ((rand() % 100) / 50.0f - 1.0f) * 1.2f,
				                     hole->pos[1] + 0.3f + ((rand() % 100) / 100.0f),
				                     hole->pos[2] + (feet[2] - hole->pos[2]) * u * 0.8f + ((rand() % 100) / 50.0f - 1.0f) * 1.2f};
				spellfx_burst(at, ELEMENT_FIRE, 3.0f);
				if (rand() % 2)
					sfx_play_at("trp_rockfall", at, 40.0f);
			}
		}
		const int ready = phase_at_least("mq101dragonattackscene1", 20);
		if (ready && hole && inn && hypotf(hole->pos[0] - feet[0], hole->pos[2] - feet[2]) < JUMP_NEAR_M + 2.0f) {
			f->prompt = "(X) Jump through the roof";
			if (pressed & SCE_CTRL_CROSS) {
				memcpy(player_to, feet, sizeof(player_to)); // (from here)
				go(ST_LEAP);
			}
		}
		break;
	}
	case ST_LEAP: { // Across, through the inn's roof, down into it
		const Marker *inn = marker("mq101playerinnmovemarker");
		f->lock_move = f->hold = 1;
		const float u = fminf(1.0f, stage_t / JUMP_S);
		if (inn) {
			for (int k = 0; k < 3; k++)
				f->pos[k] = player_to[k] + (inn->pos[k] - player_to[k]) * u;
			f->pos[1] += sinf(u * (float)M_PI) * 1.5f; // (the arc)
		}
		if (u >= 1.0f) {
			sfx_play("fst_dirt_land", 1.0f, 0.0f);
			const int r = who("ralof");
			hide("ralof"); // (he'll catch up: at the keep)
			(void)r;
			const int h = who("hadvar");
			actors_script(h, 1);
			place("hadvar", "mq101hadvarmarker8", NULL); // (Skyrim's: out in the road calling Haming; by the inn to meet you later)
			run_scene("mq101dragonattackscenebperchtower", 0, 99, 1); // (INFO 000DB8D3)
			quests_set(Q_UNBOUND, 3);
			go(ST_TOWN);
		}
		break;
	}
	case ST_TOWN: { // Hadvar through the burning town: the wall, Tullius, to the keep
		static int part;
		static float apart, call_t; // (cut off from him - rubble, a wrong turn - a while: he comes to you; only long stuck, you to him)
		apart = near_actor("hadvar", FOLLOW_GATE_M + 3.0f) ? 0.0f : apart + dt;
		call_t -= dt;
		if (apart > 4.0f && apart <= 14.0f && call_t <= 0.0f) { // (INFO 000DB8D3: he runs back for you - the hard loading screen
			const int hv = who("hadvar");                   //  the old jump to him gave, right after the leap, was the player's)
			call_t = 1.5f;
			if (hv >= 0) {
				const float by[3] = {feet[0] + 1.2f, feet[1], feet[2] + 1.2f};
				actors_script(hv, 1), actors_script_walk(hv, by, 1);
			}
		} else if (apart > 14.0f) { // (can't reach him at all - rubble between: beside him)
			float at[3];
			if (actors_where(who("hadvar"), at)) {
				f->pos[0] = at[0] + 1.5f, f->pos[1] = at[1], f->pos[2] = at[2] + 1.5f;
				f->teleport = 1;
			}
			apart = 0.0f;
		}
		static int haming_step;
		if (stage_t < dt * 1.5f)
			part = 0, haming_step = 0;
		if (!strcmp(run.name, "mq101dragonattackscenebperchtower") && run.active) { // (INFO 000DB8D8)
			const Marker *g = marker(run.phase >= 3 ? "duncggunnarnearinnhidemarker" : "mq101gunnarmarker8");
			const int h = who("haming");
			if (run.phase == 3 && al.beast >= 0) { // (Alduin's fire on Torolf: "Torolf!")
				float ta[3];
				if (run.t < 2.5f && actors_where(who("torolf"), ta))
					al.breathe = 1, memcpy(al.aim, ta, sizeof(al.aim)), breath_on();
				else if (run.t < 3.0f)
					al.breathe = 0, breath_off();
			}
			if (run.phase >= 8 && al.beast >= 0 && (al.mode == AL_GROUND || al.mode == AL_PERCH)) { // (Hadvar sets off along the
				al.mode = AL_CIRCLE, al.arrived = 0, al.breathe = 0;                               //  wall: off the road, up into the sky)
				al.angle = atan2f(al.pos[2] - feet[2], al.pos[0] - feet[0]);
				breath_off();
				sfx_play_at("dragon_roar", al.pos, 150.0f);
				game_log("opening: Alduin takes off from the road (Hadvar sets off along the wall)");
			}
			if (g && h >= 0 && run.phase >= 1 && haming_step < (run.phase >= 3 ? 2 : 1)) {
				const float by[3] = {g->pos[0] + 0.9f, g->pos[1], g->pos[2] + 0.6f};
				actors_script(h, 1), actors_script_walk(h, by, 1);
				haming_step = run.phase >= 3 ? 2 : 1;
			}
		}
		if (!run.active && part == 0) {
			run_scene("mq101dragonattackscenecperchwall", 0, 99, 1); // (INFO 000DB8D5)
			static Action wall = {"", "alduin", "duncgalduincshoutwhileperchedpkg", "dunalduincperchref,duncgalduincshouttarget01ref"};
			alduin_package(&wall);
			part = 1;
		}
		if (test_wall == 1 && part == 1 && run.active && !strcmp(run.name, "mq101dragonattackscenecperchwall") && run.t > 2.5f) {
			const Marker *hm = marker("duncghadvarcwallperchhidemarker");
			if (hm) { // (a test: there beside his mark, as you'd have come)
				f->pos[0] = hm->pos[0] + sinf(hm->heading) * 3.0f, f->pos[1] = hm->pos[1], f->pos[2] = hm->pos[2] + cosf(hm->heading) * 3.0f;
				f->hold = 1;
				game_log("opening: (test) at the wall by Hadvar's mark");
			}
			test_wall = 2;
		}
		if (part == 1 && run.active && !strcmp(run.name, "mq101dragonattackscenecperchwall") && al.beast >= 0 && al.mode == AL_PERCH) {
			const Marker *perch = marker("dunalduincperchref");
			const float pd = perch ? hypotf(feet[0] - perch->pos[0], feet[2] - perch->pos[2]) : 99.0f;
			// Approach trigger: player moves past the wall corner (x > 263, z < 1138) or within 12m of perch
			const int in_trigger = (feet[0] > 263.0f && feet[2] < 1138.0f) || pd < 12.0f;
			// (the reference: Hadvar's INFO 000DB8D5 - you stop by it, he lands on it right over
			// you and breathes fire on the way ahead; the view up at him, a few seconds)
			static float wall_t;
			if (run.t < dt * 1.5f)
				wall_t = 0.0f;
			const Marker *hide = marker("duncghadvarcwallperchhidemarker");
			const float hd = hide ? hypotf(feet[0] - hide->pos[0], feet[2] - hide->pos[2]) : pd;
			if (al.arrived && (hd < WALL_JOIN_M || pd < 12.0f) && wall_t < WALL_HOLD_S) {
				if (wall_t == 0.0f && hide) { // (in against the wall beside Hadvar, a step behind him - as the reference: not
					// stopped wherever you were, him off on his own)
					player_to[0] = hide->pos[0] + sinf(hide->heading) * WALL_BEHIND_HADVAR_M;
					player_to[1] = hide->pos[1];
					player_to[2] = hide->pos[2] + cosf(hide->heading) * WALL_BEHIND_HADVAR_M;
					player_moving = 1;
				}
				wall_t += dt;
				f->lock_move = 1, f->shake = wall_t > 1.0f;
				const Marker *tg = marker("duncgalduincshouttarget01ref");
				const float body[3] = {al.pos[0], al.pos[1] + ALDUIN_BODY_UP_M, al.pos[2]};
				if (tg && wall_t > 2.5f) // (then down to where his fire falls: the way ahead, as the reference)
					look_toward(tg->pos);
				else
					look_toward(body);
				if (tg && wall_t > 1.0f)
					al.breathe = 1, memcpy(al.aim, tg->pos, sizeof(al.aim)), breath_on();
				if (wall_t >= WALL_HOLD_S)
					al.breathe = 0, breath_off(), game_log("opening: Alduin on the wall, his fire on the way ahead");
			}
			if (((in_trigger && wall_t >= WALL_HOLD_S) || run.t > 16.0f) && al.arrived) {
				al.mode = AL_CIRCLE;
				al.arrived = 0;
				al.breathe = 0;
				breath_off();
				sfx_play_at("dragon_roar", al.pos, 150.0f);
			}
		} else if (!run.active && part == 1) {
			place("ralof", "mq101raloftokeepmarkerf", NULL); // (Ralof's made it too: at the keep)
			run_scene("mq101dragonattackscenedtokeep", 0, 6, 1); // (INFO 00109B60 ...)
			{ // (INFO 0010A187, INFO 00103269: Tullius in sight, by the
			  // keep's door you're making for - Skyrim's scene begins as you pass him; ours, after
			  // Vilod's last words, 40 m on, and he shouted from out of sight)
				const int tl = who("generaltullius");
				const Marker *door = marker("mq101hadvartokeepmarkerf");
				float tp[3];
				if (tl >= 0 && door && actors_where(tl, tp) && hypotf(tp[0] - feet[0], tp[2] - feet[2]) > 25.0f) {
					float by[3] = {door->pos[0] + 3.0f, door->pos[1], door->pos[2] + 2.0f}, floor_at[3];
					NavRef nr; // (on the paving, as the navmesh has it - the land under it put him in the ground)
					if (navmesh_nearest(by[0], by[1], by[2], 3.0f, &nr, floor_at))
						memcpy(by, floor_at, sizeof(by));
					actors_script(tl, 1), actors_script_place(tl, by, atan2f(-(feet[0] - by[0]), -(feet[2] - by[2])));
					actors_script_clip(tl, "", 0);
				}
			}
			part = 2;
		} else if (!run.active && part == 2) {
			quests_set(Q_UNBOUND, 10);
			go(ST_CHOICE);
		}
		break;
	}
	case ST_CHOICE: { // Ralof's door or Hadvar's: who do you follow?
		{ // (the two doors: Ralof's, Hadvar's - between them, both in view as you choose)
			float ra[3], ha[3];
			if (actors_where(who("ralof"), ra) && actors_where(who("hadvar"), ha)) {
				const float mid[3] = {(ra[0] + ha[0]) * 0.5f, (ra[1] + ha[1]) * 0.5f + 1.3f, (ra[2] + ha[2]) * 0.5f};
				look_toward(mid);
			} else {
				look_toward_actor(stage_t < 2.0f ? "ralof" : "hadvar", 1.5f);
			}
		}
		if (stage_t > 1.0f && chosen_answer < 0 && stage_t - dt <= 1.0f)
			f->choice = 1;
		if (chosen_answer >= 0) {
			with = chosen_answer;
			run_scene("mq101dragonattackscenedtokeep", with == WITH_RALOF ? 7 : 8, with == WITH_RALOF ? 7 : 8, 1);
			quests_set(Q_UNBOUND, with == WITH_RALOF ? 11 : 12);
			go(ST_TOKEEP);
		}
		break;
	}
	case ST_TOKEEP: { // Into Helgen Keep: they run for its door, and you follow them in - walked there, its door used
		// (the user, Nov 7: "you should have to walk up to the keep's door to enter, like everywhere else" -
		// it was a fade straight inside as you chose)
		static int going_in;
		if (stage_t < dt * 1.5f)
			going_in = 0;
		if (keep_jump)
			going_in = 1, keep_jump = 0;
		if (!going_in) {
			const Marker *door = marker(with == WITH_RALOF ? "#107a98" : "duncghadvaroutsidekeepdoormarker");
			if (test_wall && door && stage_t > 6.0f && stage_t - dt <= 6.0f) { // (a test: walked up to it)
				f->pos[0] = door->pos[0] + 2.5f, f->pos[1] = door->pos[1], f->pos[2] = door->pos[2] + 1.0f, f->hold = 1;
				game_log("opening: (test) at the keep's door");
			}
			const float d = door ? hypotf(door->pos[0] - feet[0], door->pos[2] - feet[2]) : 0.0f;
			if (door && d < KEEP_ENTER_PROMPT_M && fabsf(door->pos[1] - feet[1]) < 3.0f)
				f->prompt = "Enter Helgen Keep"; // (the button shown with it: "(O)" was doubled)
			if (!door || (d < KEEP_ENTER_PROMPT_M && (pressed & SCE_CTRL_CIRCLE)) || d < KEEP_ENTER_M || stage_t > KEEP_ENTER_LATEST_S)
				going_in = 1, game_log("opening: through the keep's door (%.1f m from it, %.0f s)", d, stage_t);
			break;
		}
		fade_to = 1.0f;
		if (fade < 0.98f)
			break;
		alduin_gone(), breath_off();
		const int keep = interior_find("helgenkeep01");
		m = marker(with == WITH_RALOF ? "mq101keepintroulfricmarker1a" : "mq101keepintrohadvarmarker1a");
		if (keep < 0 || !m) {
			go(ST_DONE);
			break;
		}
		f->enter = keep;
		const float c2 = cosf(m->heading), s2 = sinf(m->heading);
		f->pos[0] = m->pos[0] + c2 * 1.4f, f->pos[1] = m->pos[1], f->pos[2] = m->pos[2] - s2 * 1.4f;
		const float to_them = atan2f(-(m->pos[0] - f->pos[0]), -(m->pos[2] - f->pos[2])); // (facing them)
		f->look_yaw = -to_them * 180.0f / (float)M_PI;
		face(to_them, 0.0f);
		const int fw = follower();
		release_cast(with == WITH_RALOF ? "ralof" : "hadvar");
		actors_script(fw, 1);
		actors_script_place(fw, m->pos, m->heading);
		fade_to = 0.0f;
		go(ST_KEEP);
		break;
	}
	case ST_KEEP: { // The keep: the bindings cut, gear, and out through the caves
		const int fw = follower();
		// (Skyrim's gates here open by its scripts - a lever, a key from a dead guard: as you or
		// the one you follow come to them)
		float fw_at[3];
		const int fw_here = actors_where(fw, fw_at);
		// (doors are yours to open - Circle; the one you follow opens those on the way as they go)

		if (stage_t < dt * 1.5f) { // (Skyrim's other side here as people to talk to: not while you escape them)
			const char *other = with == WITH_RALOF ? "mq101imperial" : "mq101stormcloak";
			const float away[3] = {0.0f, -500.0f, 0.0f};
			for (int i = 0; i < actors_count(); i++) {
				const char *id = actors_id(i);
				if (i != fw && (!strncmp(id, other, strlen(other)) || (with == WITH_RALOF && !strncmp(id, "mq101tortur", 11))))
					actors_script(i, 1), actors_script_place(i, away, 0.0f);
			}
		}
		static float collapse_shake;
		static int bridge_down;
		if (stage_t < dt * 1.5f)
			bridge_down = 0, collapse_shake = 0.0f;
		if (collapse_shake > 0.0f)
			collapse_shake -= dt, f->shake = 1;
		if (!bridge_down && interior >= 0 && feet[2] > BRIDGE_PAST_Z && feet[1] < -14.0f) {
			const float at[3] = {-57.0f, -18.0f, 81.0f}; // (Skyrim's NorRetractableBridge01 in Helgen Keep)
			if (activators_collapse_near(at, 6.0f)) {
				sfx_play("trp_rockfall", 1.0f, 0.0f), sfx_play("dragon_roar", 1.0f, 0.0f);
				collapse_shake = 1.6f; // (Alduin overhead: the keep shakes as it falls)
				ui_notice("The bridge collapses behind you", 3.0f);
			}
			bridge_down = 1;
		}
		if (interior >= 0 && activators_open_near(feet, 7.0f) + (fw_here ? activators_open_near(fw_at, 7.0f) : 0))
			game_log("opening: a gate raised at %.0f %.0f %.0f", feet[0], feet[1], feet[2]); // (MQ101's end gate: its script's)
		if (interior >= 0) { // (the caves' mouth - Skyrim's walk-through door: out into Skyrim as you reach it)
			float da[32][3];
			const char *dl[32];
			const int dn = interior_doors(da, dl, 32);
			const Marker *in0 = marker("mq101keepintroulfricmarker1a");
			static int listed;
			if (!listed) {
				listed = 1;
				for (int k = 0; k < dn; k++)
					game_log("opening: keep door %d '%s' at %.0f %.0f %.0f", k, dl[k] ? dl[k] : "", da[k][0], da[k][1], da[k][2]);
			}
			int far_k = -1;
			float far_d = 150.0f; // (the way out is this far from the way in, at least)
			for (int k = 0; k < dn && in0; k++) {
				const float d = hypotf(da[k][0] - in0->pos[0], da[k][2] - in0->pos[2]);
				if (d > far_d)
					far_d = d, far_k = k;
			}
			if (far_k >= 0 && hypotf(da[far_k][0] - feet[0], da[far_k][2] - feet[2]) < 3.0f &&
			    fabsf(da[far_k][1] - feet[1]) < 5.0f) {
				game_log("opening: out by the caves' mouth");
				f->use_door = far_k;
			} else if (far_k >= 0 && hypotf(da[far_k][0] - feet[0], da[far_k][2] - feet[2]) < 14.0f) {
				f->prompt = dl[far_k] && dl[far_k][0] ? dl[far_k] : "", f->prompt_door = far_k; // (the way out: as its door reads - and works so)
			}
		}
		static int cut; // (INFO 00107D89: off then, the hands free at once)
		static float in_keep; // (stage_t's set far on once the gear's given: this keeps time)
		if (stage_t < dt * 1.5f)
			cut = 0, in_keep = 0.0f;
		in_keep += dt;
		// (when it's said - INFO 00107D89, the phase after a line of theirs
		// : Ralof's 9th, Hadvar's 6th - not on a clock: the lines before it run long)
		int said = 0;
		for (int k = 0; k < sc.i && k < sc.n; k++) {
			const Line *ln = &lines[sc.line[k]];
			said |= !strncmp(ln->scene, "mq101keepintroscene", 19) && ln->phase == (with == WITH_RALOF ? 9 : 6);
		}
		if (!cut && (said || in_keep > KEEP_CUT_S + 40.0f)) {
			const int cuffs = quests_item("cuffs");
			equip_set_locked(0);
			if (cuffs >= 0 && items_have(cuffs) > 0) {
				if (equip_worn(cuffs))
					equip_toggle(cuffs);
				items_add(cuffs, -items_have(cuffs));
				ui_notice("Your bindings are cut", 3.0f);
				game_log("intro: the bindings cut");
			}
			cut = 1;
		}
		f->bound = !cut;
		static int door_unlocked; // (Skyrim's stage 300, the keep scene's end: MQ101 unlocks KeepLockedDoor1 - standing about or not)
		static float since_cut;
		if (stage_t < dt * 1.5f)
			door_unlocked = 0, since_cut = 0.0f;
		since_cut += cut ? dt : 0.0f;
		if (cut && !door_unlocked && (since_cut > KEEP_DOOR_LATEST_S || (amb >= AMB_DONE && stage_t >= 1e6f))) {
			const int lk = locks_find_ref(KEEP_LOCKED_DOOR);
			if (lk >= 0)
				locks_unlock(lk), game_log("opening: the keep's locked door unlocked (MQ101 stage 300)");
			door_unlocked = 1;
		}
		const Marker *door = marker(with == WITH_RALOF ? "mq101raloftokeepmarkerf" : "mq101hadvartokeepmarkerf");
		if (interior < 0 && door && hypotf(door->pos[0] - feet[0], door->pos[2] - feet[2]) < KEEP_DOOR_NEAR_M) {
			// (back out the way you came: the town's burning, the doors barred behind you - in again)
			const int keep = interior_find("helgenkeep01");
			const Marker *in = marker(with == WITH_RALOF ? "mq101keepintroulfricmarker1a" : "mq101keepintrohadvarmarker1a");
			if (keep >= 0 && in) {
				f->enter = keep;
				memcpy(f->pos, in->pos, sizeof(f->pos));
				f->look_yaw = -in->heading * 180.0f / (float)M_PI;
				ui_notice("The way back is blocked. Follow them out through the keep.", 4.0f);
			}
			break;
		}
		if (stage_t < dt * 1.5f)
			amb = with == WITH_RALOF && !test_to_end ? AMB_WAIT : AMB_DONE, amb_t = 0.0f;
		if (stage_t >= 1e6f && interior >= 0 && amb < AMB_DONE) { // The gate: Skyrim's Imperials come for the prisoners; Ralof's men kill them
			static const char *const FRIENDS[] = {"mq101stormcloakprisonermale02", "mq101stormcloakprisonermalenord",
			                                      "mq101stormcloakprisonerfemale02"};
			const int officer = who("mq101imperialsoldierofficer"), melee = who("mq101imperialsoldiermelee02");
			const Marker *o_from = marker("mq101keepintrohadvarmarker1c"), *m_from = marker("mq101keepintroimperialsoldier2marker1c"),
			             *o_to = marker("mq101keepintrohadvarmarker1d"), *m_to = marker("mq101keepintroimperialsoldier2marker1d");
			amb_t += dt;
			// (free to look about and move as they come - the user: the view held on the gate was wrong)
			// (the user, Nov 7 - and the reference: Gunjar searched, his gear on, a few swings, and only as
			// you go to the gate do the Imperials come: they came the moment Ralof had said his piece)
			const int to_gate = o_to && hypotf(o_to->pos[0] - feet[0], o_to->pos[2] - feet[2]) < KEEP_GATE_NEAR_M;
			if (amb == AMB_WAIT && !speaking() && ((to_gate && amb_t > 8.0f) || amb_t > KEEP_IMPERIALS_LATEST_S)) { // (footsteps on the far side)
				if (officer >= 0 && o_from && o_to)
					actors_script(officer, 1), actors_script_place(officer, o_from->pos, o_from->heading), actors_script_clip(officer, "", 0),
					    actors_script_walk(officer, o_to->pos, 0);
				if (melee >= 0 && m_from && m_to)
					actors_script(melee, 1), actors_script_place(melee, m_from->pos, m_from->heading), actors_script_clip(melee, "", 0),
					    actors_script_walk(melee, m_to->pos, 0);
				for (size_t k = 0; k < sizeof(FRIENDS) / sizeof(FRIENDS[0]); k++) // (just you and Ralof in that room, as Skyrim's: no others -
					hide(FRIENDS[k]);                                         //  three more had run ahead and cleared the keep for you)
				actors_script_follow(fw, 0);
				scene("mq101keepintrosceneb", 21, 21); // (INFO 001041A5)
				amb = AMB_COMING, amb_t = 0.0f;
				game_log("opening: the Imperials come to the keep's gate");
			} else if (amb == AMB_COMING && !speaking() && amb_t > 2.0f) {
				scene("mq101keepintrosceneb", 22, 22); // (INFO 0005CE8D)
				const Marker *cover = marker("mq101keepintrostormcloak2marker1a");
				if (cover)
					actors_script_walk(fw, cover->pos, 1);
				amb = AMB_COVER, amb_t = 0.0f;
			} else if (amb == AMB_COVER && !speaking() && amb_t > 1.0f) {
				scene("mq101keepintrosceneb", 24, 24); // (a line of theirs)
				amb = AMB_SHOUT, amb_t = 0.0f;
			} else if (amb == AMB_SHOUT && !speaking()) { // (the gate open: the fight - yours, with Ralof's men)
				if (o_to) {
					const int n = activators_force_open_near(o_to->pos, 9.0f) + doors_open_near(o_to->pos, 9.0f);
					game_log("opening: %d gates and doors opened at the keep's gate", n);
				}
				// The two at the gate become real foes: the captain comes for you, the soldier for Ralof (Skyrim's:
				// "Press X to attack" - your fight, Ralof at your side)
				const int who_foe[2] = {officer, melee};
				static const char *const FOE_KIND[2] = {"person_325", "person_326"};
				for (int k = 0; k < 2; k++) {
					float at[3];
					amb_foe[k] = -1;
					const Marker *to = k ? m_to : o_to;
					if (who_foe[k] < 0 || !actors_where(who_foe[k], at))
						continue;
					const int c = creatures_place_foe(FOE_KIND[k], at, to ? to->heading : 0.0f, 0, interior);
					if (c >= 0) // (drawn as themselves: the captain - Skyrim's MQ101ImperialSoldierOfficer, the woman - and her soldier)
						creatures_live_look_as(c, k ? "mq101imperialsoldiermelee02" : "mq101imperialsoldierofficer", k == 0);
					amb_foe[k] = c >= 0 ? creatures_live_spawn(c) : -1;
					hide(k ? "mq101imperialsoldiermelee02" : "mq101imperialsoldierofficer");
				}
				ui_notice("The Imperials attack! Draw your weapon and fight.", 4.0f);
				amb = AMB_FIGHT, amb_t = 0.0f;
				game_log("opening: the keep's fight begins (the Imperials are foes now)");
			} else if (amb == AMB_FIGHT) {
				static float blow_t, walk_t;
				if (amb_t < dt * 1.5f)
					blow_t = walk_t = 0.0f;
				float foe[3], at[3];
				// (Ralof takes the soldier - the captain once he's down; she comes for you)
				const int up_soldier = creatures_spawn_where(amb_foe[1], foe), up_captain = !up_soldier && creatures_spawn_where(amb_foe[0], foe);
				const int left = (amb_foe[0] >= 0 && creatures_spawn_alive(amb_foe[0]) == 1) + (amb_foe[1] >= 0 && creatures_spawn_alive(amb_foe[1]) == 1);
				if (fw >= 0 && actors_where(fw, at)) {
					creatures_set_friend(4, at, 1);
					blow_t -= dt, walk_t -= dt;
					if (up_soldier || up_captain) {
						const float d = hypotf(foe[0] - at[0], foe[2] - at[2]);
						if (d > 1.9f) {
							if (walk_t <= 0.0f) {
								const float by[3] = {foe[0] - 0.9f, foe[1], foe[2] + 0.6f};
								actors_script_walk(fw, by, 1);
								walk_t = 0.6f;
							}
						} else if (blow_t <= 0.0f) {
							actors_script_face(fw, atan2f(-(foe[0] - at[0]), -(foe[2] - at[2])));
							actors_script_clip(fw, "w1h_power", 1);
							sfx_play_at("wpn_hit_blade", foe, 40.0f);
							creatures_area(foe, 1.0f, 2.0f);
							blow_t = 1.4f;
						}
					}
				}
				if (!left || amb_t > 150.0f) { // (both down - or it's gone on too long: Ralof finishes it)
					for (int k = 0; k < 2; k++)
						if (creatures_spawn_where(amb_foe[k], foe))
							creatures_area(foe, 1.0f, 500.0f);
					creatures_set_friend(4, ear, 0);
					actors_script_clip(fw, "", 0);
					const Marker *back = marker("mq101keepintrohadvarmarker2a");
					// (to the captain's body, to search it for the key - Skyrim's MQ101RalofToImperialSoldierFort01 (the
					// officer's alias) and MQ101RalofGetDoorKey, as the reference shows him knelt by her: her body where
					// it fell - asked of the living only, it fell back to a marker by the wall, and he knelt over nobody)
					key_body_set = creatures_spawn_body(amb_foe[0], key_body) || creatures_spawn_body(amb_foe[1], key_body);
					if (!key_body_set && back)
						memcpy(key_body, back->pos, sizeof(key_body)), key_body_set = 1;
					if (key_body_set && actors_where(fw, at)) {
						float dx = at[0] - key_body[0], dz = at[2] - key_body[2];
						const float d = fmaxf(hypotf(dx, dz), 0.01f);
						const float by[3] = {key_body[0] + dx / d * KEY_KNEEL_FROM_M, key_body[1], key_body[2] + dz / d * KEY_KNEEL_FROM_M};
						actors_script_walk(fw, by, 0);
					}
					// (INFO 0004E1A5 as he goes to her: Skyrim's phase 29, the
					// reference's 6 s after the last one fell - it waited till he'd walked to the wall marker)
					scene("mq101keepintrosceneb", 29, 29);
					game_log("opening: the Imperials are down (%.0f s); Ralof to the %s's body (%.1f %.1f)", amb_t,
					         creatures_spawn_body(amb_foe[0], at) ? "captain" : "soldier", key_body[0], key_body[2]);
					amb = AMB_KEY, amb_t = 0.0f;
				}
			} else if (amb == AMB_KEY && amb_t > 0.8f && (!actors_script_walking(fw) || amb_t > 10.0f)) {
				float at[3];
				if (key_body_set && actors_where(fw, at)) { // (down on one knee by her, searching: Skyrim's IdleKneel -
					// not the block's kneel, his head turned back over his shoulder)
					actors_script_face(fw, atan2f(-(key_body[0] - at[0]), -(key_body[2] - at[2])));
					actors_script_clip(fw, "kneel_in", 1), actors_script_clip_next(fw, "kneel");
					const float look[3] = {key_body[0], key_body[1] + 0.2f, key_body[2]};
					actors_script_look_at(fw, look);
					game_log("opening: Ralof kneels by the body, %.2f m off", hypotf(at[0] - key_body[0], at[2] - key_body[2]));
				}
				amb = AMB_SEARCH, amb_t = 0.0f;
			} else if (amb == AMB_SEARCH && !speaking() && amb_t > 1.2f) {
				scene("mq101keepintrosceneb", 31, 32); // (INFO 0003DF63 INFO 0004E19E)
				amb = AMB_FOUND, amb_t = 0.0f;
			} else if (amb == AMB_FOUND && !speaking()) {
				actors_script_clip(fw, "kneel_out", 1); // (up again, the key in hand)
				actors_script_look_at(fw, NULL);
				scene("mq101keepintrosceneb", 35, 35); // (INFO 0004E1A3)
				actors_script_follow(fw, 1);
				amb = AMB_DONE;
				game_log("opening: the ambush at the keep's gate is over");
			}
		}
		if (stage_t >= 1e6f && interior >= 0 && test_to_end) { // (a test: by the bear's den, the end gate ahead)
			const Marker *e = marker("mq101keepfriendmarker13");
			if (e) {
				f->hold = 1;
				memcpy(f->pos, e->pos, sizeof(f->pos));
				const float by[3] = {e->pos[0] + 1.2f, e->pos[1], e->pos[2]};
				actors_script_place(fw, by, 0.0f);
			}
			test_to_end = 0;
		}
		if (stage_t >= 1e6f && interior >= 0 && amb >= AMB_DONE) { // Leading you out: Skyrim's own way through the keep, waiting for you
			// (MQ101's markers for whoever you follow: the barracks, the storeroom, the torture
			// chamber, the cells, the caves, the bear's den; then the keep's furthest door, the caves' mouth)
			static const char *const ROUTE[] = {
				"mq101keepfriendmarker2", "mq101keepfriendmarker3", "mq101keepfriendmarker3a", "mq101keepfriendmarker3b",
				"mq101keepfriendmarker4", "mq101keepfriendmarker5", "mq101keepfriendmarker6", "mq101keepfriendmarker7",
				"mq101keepfriendmarker8", "mq101keepfriendmarker9a", "mq101keepfriendmarker9", "mq101keepfriendmarker10",
				"mq101keepfriendmarker12", "mq101keepfriendmarker13", NULL};
			const int last = (int)(sizeof(ROUTE) / sizeof(ROUTE[0])) - 1; // (the door)
			static float exit_at[3];
			static int have_exit, leg, leading;
			if (!lead_on) { // (the keep's door furthest from where you came in: the caves' mouth)
				float doors_at[32][3];
				const char *labels[32];
				const int n = interior_doors(doors_at, labels, 32);
				const Marker *in = marker("mq101keepintroulfricmarker1a");
				float best = -1.0f;
				for (int k = 0; k < n && in; k++) {
					const float d = hypotf(doors_at[k][0] - in->pos[0], doors_at[k][2] - in->pos[2]);
					if (d > best)
						best = d, memcpy(exit_at, doors_at[k], sizeof(exit_at));
				}
				have_exit = best > 0.0f, leg = 0, leading = 0, lead_on = 1;
			}
			const Marker *wm = leg < last ? marker(ROUTE[leg]) : NULL;
			const float *to = wm ? wm->pos : have_exit ? exit_at : NULL;
			static int following;
			float at[3];
			if (to && actors_where(fw, at)) {
				memcpy(lead_mark, to, sizeof(lead_mark));
				const float apart = hypotf(at[0] - feet[0], at[2] - feet[2]);
				const int fighting = creatures_hostile_near(feet, 14.0f);
				static int said_spiders, said_bear; // (Skyrim's: past the spiders, at the bear)
				if (leg <= 1)
					said_spiders = said_bear = 0;
				static const float SPIDERS[3] = {-105.0f, -30.0f, 183.0f}, BEAR[3] = {-73.0f, -38.0f, 227.0f};
				if (!said_spiders && !fighting && !speaking() && hypotf(SPIDERS[0] - feet[0], SPIDERS[2] - feet[2]) < 14.0f) {
					if (with == WITH_RALOF)
						scene("mq101keepscene5b", 13, 13); // (INFO 0003EA79)
					said_spiders = 1;
				}
				if (!said_bear && !speaking() && hypotf(BEAR[0] - feet[0], BEAR[2] - feet[2]) < 30.0f && fabsf(BEAR[1] - feet[1]) < 8.0f) {
					scene(with == WITH_RALOF ? "mq101keepscene5b" : "mq101keepscene5a", with == WITH_RALOF ? 19 : 21,
					      with == WITH_RALOF ? 21 : 23); // (INFO 0005B1EC)
					said_bear = 1;
				}
				static float leg_t;
				static int leg_was = -1;
				leg_t = leg == leg_was ? leg_t + (leading ? dt : 0.0f) : 0.0f, leg_was = leg;
				if (apart > LEAD_LOST_M || (leading && leg_t > LEAD_STUCK_S)) { // (lost, or stuck on the way: beside you again)
					const float beside[3] = {feet[0] + 1.2f, feet[1], feet[2] + 1.2f};
					actors_script_place(fw, beside, 0.0f);
					memcpy(at, beside, sizeof(at));
					leading = 0, leg_t = 0.0f;
					if (leg < last && hypotf(to[0] - feet[0], to[2] - feet[2]) < 6.0f)
						leg++;
				}
				static float log_t;
				if ((log_t += dt) > 5.0f) { // (the way out: how far along - the card's log)
					log_t = 0.0f;
					game_log("opening: leading leg %d (%.0f %.0f %.0f) apart %.1f%s", leg, at[0], at[1], at[2], apart,
					         fighting ? " fighting" : "");
				}
				// A chain or lever on the way that works a gate (Helgen Keep's are pulled by whoever you
				// follow - Skyrim's mq101hadvaropengate: no target named, so nobody did, and the way
				// past the barracks stayed shut): to it, and pulled - once each
				static int lever = -1, pulled_n;
				static int pulled[16];
				static float lever_at[3], lever_t;
				int pulling = 0;
				if (leg <= 0 && stage_t < 1e6f + dt * 1.5f)
					lever = -1, pulled_n = 0;
				if (!fighting && lever < 0) {
					float lp[3];
					int li = -1;
					for (int skip = -1, tries = 0; tries < 4; tries++) { // (not one already pulled)
						li = activators_lever_near(at, 7.0f, lp, skip);
						int done = 0;
						for (int k = 0; k < pulled_n; k++)
							done |= pulled[k] == li;
						if (!done)
							break;
						skip = li, li = -1;
					}
					if (li >= 0 && hypotf(lp[0] - feet[0], lp[2] - feet[2]) < 14.0f) {
						float dx = at[0] - lp[0], dz = at[2] - lp[2];
						const float d = fmaxf(hypotf(dx, dz), 0.01f);
						const float by[3] = {lp[0] + dx / d * 0.8f, at[1], lp[2] + dz / d * 0.8f};
						actors_script_follow(fw, 0), following = 0, leading = 0;
						actors_script_walk(fw, by, 1);
						lever = li, lever_t = 0.0f;
						memcpy(lever_at, lp, sizeof(lever_at));
					}
				}
				if (lever >= 0) {
					pulling = 1;
					lever_t += dt;
					const float d = hypotf(lever_at[0] - at[0], lever_at[2] - at[2]);
					if (d < 1.9f || lever_t > 6.0f || (lever_t > 0.5f && !actors_script_walking(fw))) {
						actors_script_face(fw, atan2f(-(lever_at[0] - at[0]), -(lever_at[2] - at[2])));
						activators_use(lever);
						game_log("opening: %s pulls the chain at %.0f %.0f %.0f", with == WITH_RALOF ? T_NPC_0002BF9D_FULL0 : T_NPC_0002BF9F_FULL0,
						         lever_at[0], lever_at[1], lever_at[2]);
						if (pulled_n < 16)
							pulled[pulled_n++] = lever;
						lever = -1;
					}
				}
				static float strike_t;
				if (fighting) { // (a fight: at your side, striking whoever's at them - Skyrim's follower)
					if (leading || !following)
						actors_script_follow(fw, 1), leading = 0, following = 1;
					if ((strike_t -= dt) <= 0.0f) {
						strike_t = FOLLOWER_STRIKE_S;
						const float eye[3] = {at[0], at[1] + 1.4f, at[2]}, ahead[3] = {0.0f, 0.0f, -1.0f};
						float foe[3];
						if (creatures_aim_assist(eye, ahead, -1.0f, FOLLOWER_REACH_M + 0.5f, 0.0f, foe)) {
							actors_script_face(fw, atan2f(-(foe[0] - at[0]), -(foe[2] - at[2])));
							actors_script_clip(fw, "w1h_power", 1); // (a swing at them)
							if (creatures_area(at, FOLLOWER_REACH_M, FOLLOWER_DAMAGE) > 0)
								sfx_play_at("wpn_hit_blade", at, 30.0f);
						}
					}
				} else if (!leading && apart < LEAD_GO_M && following) {
					actors_script_follow(fw, 0), following = 0;
				}
				if (fighting || pulling) {
				} else if (leading && !actors_script_walking(fw)) { // (there: on to the next)
					leading = 0;
					if (leg < last)
						leg++;
				} else if (leading && (apart > LEAD_WAIT_M || fighting)) { // (you've fallen behind, or a fight: they wait)
					actors_script_place(fw, at, atan2f(-(feet[0] - at[0]), -(feet[2] - at[2])));
					leading = 0;
				} else if (!leading && !following && apart > LEAD_WAIT_M + 3.0f) {
					actors_script_follow(fw, 1), following = 1; // (you've gone on ahead: after you)
				} else if (!leading && apart < LEAD_GO_M && !fighting &&
				           hypotf(to[0] - at[0], to[2] - at[2]) > 1.5f) {
					int near_leg = -1; // (on from where you've got to: the way's mark nearest you, and the next)
					float near_d = 1e9f;
					for (int k = 0; k < last; k++) {
						const Marker *rm = marker(ROUTE[k]);
						const float d = rm ? hypotf(rm->pos[0] - feet[0], rm->pos[2] - feet[2]) + fabsf(rm->pos[1] - feet[1]) * 2.0f : 1e9f;
						if (d < near_d)
							near_d = d, near_leg = k;
					}
					if (near_leg + 1 > leg)
						leg = near_leg + 1 < last ? near_leg + 1 : last;
					wm = leg < last ? marker(ROUTE[leg]) : NULL, to = wm ? wm->pos : have_exit ? exit_at : to;
					actors_script_walk(fw, to, 1), leading = 1; // (INFO 00020135: at a run, as you go)
				} else if (!leading && leg < last && hypotf(to[0] - at[0], to[2] - at[2]) <= 1.5f) {
					leg++;
				}
			}
		}
		if (interior < 0 && stage_t > 5.0f) { // (out: the caves' end)
			actors_script(fw, 1);
			const float behind[3] = {feet[0] + 1.5f, feet[1], feet[2] + 1.5f};
			actors_script_place(fw, behind, 0.0f);
			actors_script_follow(fw, 1);
			scene(with == WITH_RALOF ? "mq102braloftoriverwoodscene" : "mq102ahadvartoriverwood", 4, 5); // ("Wait!")
			{ // (Alduin over you, from behind the mountain, the way out of the cave facing on)
				float on[3]; // (toward Riverwood - where he goes off to, north)
				const int ti = actor_id(with == WITH_RALOF ? "gerdur" : "alvor");
				const float h = ti >= 0 && actors_where(ti, on) ? atan2f(-(on[0] - feet[0]), -(on[2] - feet[2])) : 0.0f;
				alduin_fly_over(feet, h);
				farewell_pending = 1;
			}
			{ // (Skyrim's: the one you didn't follow comes out too and runs on - INFO 0009251A)
				const int other = actor_id(with == WITH_RALOF ? "hadvar" : "ralof");
				float rw[3];
				const int t = actor_id(with == WITH_RALOF ? "alvor" : "gerdur");
				if (other >= 0 && t >= 0 && actors_where(t, rw)) {
					const float ahead[3] = {feet[0] + 4.0f, feet[1], feet[2] - 5.0f};
					actors_script(other, 1), actors_script_place(other, ahead, 0.0f);
					actors_script_walk(other, rw, 1);
				}
			}
			objects_set_switch(SWITCH_RUINS, 1); // (Helgen's ruins from now on)
			quests_set(Q_UNBOUND, 100);
			quests_set(Q_BEFORE_THE_STORM, with == WITH_RALOF ? 5 : 6);
			go(ST_RIVERWOOD);
			break;
		}
		if (stage_t > 1.5f && stage_t - dt <= 1.5f) {
			if (with == WITH_RALOF)
				scene("mq101keepintrosceneb", 3, 14); // (INFO 00020130 ... Gunjar's gear)
			else
				scene("mq101keepintroscenea", 2, 12);
		}
		if (stage_t > 3.0f && !speaking() && stage_t < 1e6f) {
			static const char *const RALOF_GEAR[] = {"sc_cuirass", "sc_boots", "sc_gauntlets", "sc_weapon"};
			static const char *const HADVAR_GEAR[] = {"imp_cuirass", "imp_boots", "imp_gauntlets", "imp_weapon"};
			const char *const *gear = with == WITH_RALOF ? RALOF_GEAR : HADVAR_GEAR;
			const int gunjar = with == WITH_RALOF && creatures_opening_bodies(interior) > 0;
			if (gunjar)
				ui_notice("Search Gunjar's body for his gear", 6.0f);
			for (int k = 0; k < 4 && !gunjar; k++) {
				const int it = quests_item(gear[k]);
				if (it >= 0 && items_have(it) <= 0) {
					items_add(it, 1);
					char text[96];
					snprintf(text, sizeof(text), "%s added", items_name(it));
					ui_notice(text, 3.0f);
				}
			}

			actors_script_follow(fw, 1);
			quests_set(Q_UNBOUND, with == WITH_RALOF ? 21 : 22);
			stage_t = 1e6f; // (once)
		}
		break;
	}
	case ST_RIVERWOOD: { // With you all the way to Riverwood; then to Gerdur at the mill / Alvor at his forge
		float rw[3], fa[3];
		const int ralof = with == WITH_RALOF;
		const int fw = follower();
		const int target_i = actor_id(ralof ? "gerdur" : "alvor");
		const char *road = ralof ? "mq102braloftoriverwoodscene" : "mq102ahadvartoriverwood";
		if (farewell_pending) { // (watching him go over - then, once he's away, the farewell)
			const float away_m = al.beast >= 0 ? sqrtf((al.pos[0] - feet[0]) * (al.pos[0] - feet[0]) +
			                                           (al.pos[1] - feet[1]) * (al.pos[1] - feet[1]) +
			                                           (al.pos[2] - feet[2]) * (al.pos[2] - feet[2])) : 1e9f;
			if (al.beast >= 0 && (al.i == 0 || away_m < 110.0f))
				look_toward(al.pos);
			if (!speaking() && ((al.i >= 1 && away_m > 150.0f) || al.beast < 0 || stage_t > 25.0f)) {
				scene(road, 6, 9);
				farewell_pending = 0;
			}
		}
		// Off into a dungeon on the way (Embershard...): they don't trail you in - on to Riverwood,
		// waiting for you at its edge, as Skyrim's MQ102 sends them ahead
		static int gone_ahead;
		const Marker *town = marker("mq102outsideriverwoodmarker");
		if (stage_t < dt * 1.5f)
			gone_ahead = 0;
		if (!gone_ahead && interior_current() >= 0 && fw >= 0 && town) {
			actors_script_follow(fw, 0);
			actors_script_place(fw, town->pos, town->heading);
			gone_ahead = 1;
			game_log("intro: %s goes on to Riverwood to wait", ralof ? T_NPC_0002BF9D_FULL0 : T_NPC_0002BF9F_FULL0);
		}
		static float lost_t; // (fallen behind - a river, a fight, a slope: back at your shoulder, not left on the road)
		lost_t = fw >= 0 && actors_where(fw, fa) && hypotf(fa[0] - feet[0], fa[2] - feet[2]) < 28.0f ? 0.0f : lost_t + dt;
		if (gone_ahead)
			lost_t = 0.0f; // (waiting in Riverwood: not fetched back to your side)
		if (fw >= 0 && lost_t > 3.0f) {
			const float behind[3] = {feet[0] - 1.6f, feet[1], feet[2] + 1.6f};
			actors_script(fw, 1), actors_script_place(fw, behind, 0.0f), actors_script_follow(fw, 1);
			lost_t = 0.0f;
		}
		// (INFO 000AC11B): once, walking beside you - not heard from
		// Riverwood while you're in a cave, nor again after a load
		if (!ruin_said && !gone_ahead && stage_t > 70.0f && !speaking() && fw >= 0 && actors_where(fw, fa) &&
		    hypotf(fa[0] - feet[0], fa[2] - feet[2]) < 8.0f) {
			scene(road, 15, 17);
			ruin_said = 1;
		}
		const int at_town = town && hypotf(town->pos[0] - feet[0], town->pos[2] - feet[2]) < 45.0f;
		const int by_them = target_i >= 0 && actors_where(target_i, rw) &&
		                    hypotf(rw[0] - feet[0], rw[2] - feet[2]) < RIVERWOOD_NEAR_M;
		if (at_town || by_them) {
			if (!speaking())
				scene_variants(road, 26, 27, 0); // (INFO 000AC11E)
			quests_set(Q_BEFORE_THE_STORM, ralof ? 7 : 8); // (still beside you: they introduce you)
			// Skyrim's scene begins with them at their work: Gerdur at the mill, Hod up on it, Frodnar
			// by the road; Alvor at his forge, Sigrid and Dorthe by it (theirs goes on indoors in
			// Skyrim: here it's all at the forge, everyone in sight)
			if (ralof) {
				const Marker *mill = marker("mq102hodlookfrommillmarker");
				if (mill && target_i >= 0) {
					float g[3] = {mill->pos[0] + 2.2f, mill->pos[1], mill->pos[2] + 1.6f};
					terrain_height_at(g[0], g[2], &g[1]); // (on the ground below the mill's deck: they talked in the air)
					actors_script(target_i, 1), actors_script_place(target_i, g, mill->heading);
				}
				place("hod", "mq102hodlookfrommillmarker", NULL);
				const Marker *post = marker("#39fdb"); // (Frodnar at his post by the road - Skyrim's MQ102FrodnarHoldPosition -
				const int fr = actor_id("frodnar");     //  till INFO 00054E92 brings him running over)
				if (post && fr >= 0) {
					float by[3] = {post->pos[0], post->pos[1], post->pos[2]};
					terrain_height_at(by[0], by[2], &by[1]);
					actors_script(fr, 1), actors_script_place(fr, by, post->heading);
				}
			} else if (target_i >= 0 && (actors_where(target_i, rw) || actors_door(target_i, rw))) {
				if (!actors_where(target_i, rw)) { // (indoors - asleep at dawn: Skyrim's scene has him out all the same)
					const float out[3] = {rw[0] + 1.5f, rw[1], rw[2] + 1.5f};
					actors_script(target_i, 1), actors_script_place(target_i, out, 0.0f);
					memcpy(rw, out, sizeof(rw));
					game_log("intro: Alvor fetched out to meet you");
				}
				const float s1[3] = {rw[0] + 2.2f, rw[1], rw[2] + 1.4f}, d1[3] = {rw[0] - 1.6f, rw[1], rw[2] + 2.2f};
				const int sg = actor_id("sigrid"), dt_i = actor_id("dorthe");
				actors_script(target_i, 1);
				if (sg >= 0)
					actors_script(sg, 1), actors_script_place(sg, s1, 0.0f);
				if (dt_i >= 0)
					actors_script(dt_i, 1), actors_script_place(dt_i, d1, 0.0f);
			}
			if (gone_ahead && fw >= 0) // (waiting at the town's edge: with you again for the introductions)
				actors_script_follow(fw, 1), gone_ahead = 0;
			go(ST_MEET);
		}
		break;
	}
	case ST_MEET: { // Led to them; "Gerdur!" ... the clearing, Hod and Frodnar; the scene as Skyrim stages it
		float rw[3], fa[3];
		const int ralof = with == WITH_RALOF;
		const int target_i = actor_id(ralof ? "gerdur" : "alvor");
		const int fw = follower();
		static int step, frodnar_come;
		static float lead_t;
		if (stage_t < dt * 1.5f)
			step = 0, frodnar_come = 0, lead_t = 0.0f;
		if (target_i >= 0 && !actors_where(target_i, rw) && actors_door(target_i, rw)) {
			// (indoors - Alvor asleep at dawn, Gerdur at home at night: Skyrim's scene fetches
			// them all the same - out at their own door to meet you, not the meeting skipped)
			const float out[3] = {rw[0] + 1.5f, rw[1], rw[2] + 1.5f};
			actors_script(target_i, 1);
			actors_script_place(target_i, out, atan2f(-(feet[0] - out[0]), -(feet[2] - out[2])));
			game_log("intro: %s fetched out to meet you", ralof ? T_NPC_0001347C_FULL0 : T_NPC_00013475_FULL0);
		}
		if (target_i < 0 || !actors_where(target_i, rw)) {
			go(ST_DONE);
			break;
		}
		if (step == 0) { // (on ahead to them: "Come on. My sister Gerdur runs the mill")
			if ((lead_t -= dt) <= 0.0f && fw >= 0) {
				const float by[3] = {rw[0] + 1.4f, rw[1], rw[2] + 1.0f};
				actors_script_follow(fw, 0), actors_script_walk(fw, by, 0);
				lead_t = 4.0f;
			}
			if (hypotf(rw[0] - feet[0], rw[2] - feet[2]) < 7.0f && fabsf(rw[1] - feet[1]) < 6.0f && !speaking()) {
				if (fw >= 0 && (!actors_where(fw, fa) || hypotf(fa[0] - rw[0], fa[2] - rw[2]) > 8.0f ||
				                fabsf(fa[1] - rw[1]) > 1.5f)) { // (not there yet - or on the mill's deck above her: beside them)
					const float by[3] = {rw[0] + 1.4f, rw[1], rw[2] + 1.0f};
					actors_script_place(fw, by, atan2f(1.4f, 1.0f)); // (facing her)
				}
				actors_script_face(target_i, atan2f(-(feet[0] - rw[0]), -(feet[2] - rw[2])));
				run_scene(ralof ? "mq102ralofgerdurscene" : "mq102hadvaralvorscene", 6, 99, 0);
				step = 1;
			}
			break;
		}
		if (run.active && run.phase <= 7 && run.t >= MEET_WAIT_S && fw >= 0 && actors_where(fw, fa) &&
		    (hypotf(fa[0] - rw[0], fa[2] - rw[2]) > MEET_TOGETHER_M || fabsf(fa[1] - rw[1]) > 1.5f)) {
			// (not met - the mill's deck and the bank below, the river between: she comes to him,
			// as her approach package ends, facing him, on his ground)
			float dx = rw[0] - fa[0], dz = rw[2] - fa[2];
			const float d = fmaxf(hypotf(dx, dz), 0.01f);
			dx /= d, dz /= d;
			const float to[3] = {fa[0] + dx * 1.4f, fa[1], fa[2] + dz * 1.4f};
			actors_script_place(target_i, to, atan2f(dx, dz));
			actors_script_face(fw, atan2f(-dx, -dz));
			game_log("intro: %s stepped up to meet %s (%.1f m apart)", ralof ? T_NPC_0001347C_FULL0 : T_NPC_00013475_FULL0, ralof ? T_NPC_0002BF9D_FULL0 : T_NPC_0002BF9F_FULL0, d);
		}
		if (ralof && run.active && run.phase >= 21 && run.phase <= 30) { // (Frodnar runs over to Ralof - INFO 00054E92 -
			// and keeps at his side, in front of you, while he asks and is sent off: Skyrim's
			// MQ102FrodnarToRalof, MQ102FrodnarFollowRalof - he was off at the clearing, unseen, as he spoke)
			const int fr = actor_id("frodnar");
			float r[3], me[3];
			if (fr >= 0 && fw >= 0 && actors_where(fw, r) && actors_where(fr, me)) {
				const float d = hypotf(me[0] - r[0], me[2] - r[2]);
				float dx = feet[0] - r[0], dz = feet[2] - r[2];
				const float dl = fmaxf(hypotf(dx, dz), 0.01f);
				dx /= dl, dz /= dl;
				const float by[3] = {r[0] + dx * 1.3f - dz * 0.6f, r[1], r[2] + dz * 1.3f + dx * 0.6f}; // (between you and him, a little aside)
				actors_script(fr, 1);
				if (d > 2.2f && (!actors_script_walking(fr) || !frodnar_come))
					actors_script_walk(fr, by, d > 4.0f), frodnar_come = 1;
				else if (d <= 2.2f && !actors_script_walking(fr))
					actors_script_face(fr, atan2f(-(r[0] - me[0]), -(r[2] - me[2])));
			}
		}
		if (ralof && run.active && !strcmp(run.name, "mq102ralofgerdurscene") && run.phase >= 21 && run.phase <= 31) {
			// (Hod down from the mill to the clearing, as the reference has him there for INFO 00054EA4
			// (3:18): stopped on the way - the mill's deck, its steps - he stood off by the houses; walked on again, and
			// if he's still not there by the time Frodnar's sent off, he's come round the back of the houses)
			static float hod_t;
			if (run.phase == 21 && run.t < dt * 1.5f)
				hod_t = 0.0f;
			hod_t += dt;
			const Marker *hm = marker("mq102hodnearclearingmarker");
			const int hi = actor_id("hod");
			float hp[3];
			if (hm && hi >= 0 && actors_where(hi, hp) && hypotf(hp[0] - hm->pos[0], hp[2] - hm->pos[2]) > 3.0f &&
			    !actors_script_walking(hi)) {
				if (hod_t < HOD_COME_S)
					actors_script_walk(hi, hm->pos, 0);
				else
					actors_script_place(hi, hm->pos, hm->heading), game_log("intro: Hod (stuck on his way down) to the clearing");
			}
		}
		if (ralof && frodnar_come == 1 && run.active && run.phase >= 31) { // (INFO 0009A02F: off to the road)
			const Marker *post = marker("#39fdb");
			const int fr = actor_id("frodnar");
			if (post && fr >= 0)
				actors_script_walk(fr, post->pos, 1);
			frodnar_come = 2;
		}
		if (!run.active && !speaking() && step == 1) { // (the introductions over: the quest on - they stay put a while)
			quests_set(Q_BEFORE_THE_STORM, 10);
			step = 2;
		}
		if (step == 2 && hypotf(rw[0] - feet[0], rw[2] - feet[2]) > MEET_LEAVE_M) { // (you've walked on: their own days again)
			static const char *const CAST_R[] = {"gerdur", "hod", "frodnar"}, *const CAST_H[] = {"alvor", "sigrid", "dorthe"};
			for (int k = 0; k < 3; k++)
				actors_script(actor_id(ralof ? CAST_R[k] : CAST_H[k]), 0);
			actors_script(fw, 0);
			
			go(ST_DONE);
		}
		break;
	}
	}
	if (look_on) {
		f->look_on = 1;
		memcpy(f->look_at, look_at, sizeof(f->look_at));
		look_on = 0; // (asked for again each frame it's wanted)
	}
}
