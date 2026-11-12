#include "interior.h"

#include "gamelog.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>

#include "actors.h"
#include "atmosphere.h"
#include "aim.h"
#include "activators.h"
#include "furniture.h"
#include "doors.h"
#include "loot.h"
#include "fileio.h"
#include "navmesh.h"
#include "objects.h"
#include "creatures.h"
#include "terrain.h"
#include "traps.h"
#include "worlds.h"

#define SKIT_VERSION 9 // (8: before rooms and portals; 1: before swing doors, 2: before containers and loose items, 3: before door
                       //  sizes, 4: before containers' aim radius, 5: before activators, 6: before furniture,
                       //  7: before traps; still read)
#define SKDR_VERSION 2 // (1: before door sizes; still read)
#define MAX_INTERIORS 640
#define MAX_DOORS 512
#define REACH_M 2.6f        // how close a door has to be...
#define DOOR_MID_M 1.1f     // a door's middle above its placement
#define DOOR_AIM_M 0.9f     // ...and how near the crosshair has to pass
#define DRAW_RADIUS_CAP_M 60.0f
#define TAMRIEL -1
#define CITY(n) (-2 - (n)) // walled city n (world n + 1, worlds.h)
#define WORLD_OF(target) ((target) <= CITY(0) ? -1 - (target) : 0)

typedef struct {
	float pos[3];
	int target;         // TAMRIEL, CITY(n) or an interior index
	float arrival[3];
	float arrival_yaw;  // radians, facing (sin, 0, -cos)
	float centre[3], radius; // where the crosshair has to pass (the door's model as placed)
	char label[64];
} Door;

static char pack[128];
static char names[MAX_INTERIORS][64];
static char titles[MAX_INTERIORS][64]; // the place's name ("Dustman's Cairn")
static int interior_count;
static Door outside[MAX_DOORS], inside[MAX_DOORS];
static int outside_count, inside_count;
static int current = TAMRIEL; // outdoors: TAMRIEL or CITY(n)
static float fog[3], floor_y;
static float draw_cap_m = DRAW_RADIUS_CAP_M, cell_near, cell_far; // (Fidelity's reach; the interior's own fog)

// An interior's reach: its own fog's, within Fidelity's cap (the fog brought in to match)
static float interior_radius(float *near) {
	const float radius = fminf(cell_far, draw_cap_m);
	*near = fminf(cell_near, radius * 0.45f);
	return radius;
}

int interior_gate_of(const char *title, float out[2]) {
	for (int k = 0; k < outside_count; k++) {
		const int t = outside[k].target;
		if (t <= CITY(0) && WORLD_OF(t) < worlds_count() && !strcmp(worlds_title(WORLD_OF(t)), title)) {
			out[0] = outside[k].pos[0], out[1] = outside[k].pos[2];
			return 1;
		}
	}
	return 0;
}

int interior_gate_door(const char *title, const char **label) {
	for (int k = 0; k < outside_count && current < 0; k++) {
		const int t = outside[k].target;
		if (t <= CITY(0) && WORLD_OF(t) < worlds_count() && !strcmp(worlds_title(WORLD_OF(t)), title)) {
			*label = outside[k].label;
			return k;
		}
	}
	return -1;
}

void interior_set_draw_cap(float m) {
	draw_cap_m = fminf(m, DRAW_RADIUS_CAP_M);
	if (current >= 0) {
		float near;
		const float radius = interior_radius(&near);
		objects_interior_set_radius(radius);
		atmosphere_set_interior(fog, near, radius);
	}
}
static float entry_pos[3], entry_yaw;
static char place[64]; // where you are, as the door you came through called it

// Parses doors at p; returns the position after them, or NULL
static const uint8_t *read_doors(const uint8_t *p, const uint8_t *end, Door *out, int *count, int sized) {
	uint32_t n;
	if (p + 4 > end)
		return NULL;
	memcpy(&n, p, 4);
	p += 4;
	*count = 0;
	for (uint32_t i = 0; i < n; i++) {
		const int fixed = sized ? 48 : 32;
		if (p + fixed + 1 > end)
			return NULL;
		Door d;
		memcpy(d.pos, p, 12);
		memcpy(&d.target, p + 12, 4);
		memcpy(d.arrival, p + 16, 12);
		memcpy(&d.arrival_yaw, p + 28, 4);
		if (sized) {
			memcpy(d.centre, p + 32, 12);
			memcpy(&d.radius, p + 44, 4);
		} else {
			d.centre[0] = d.pos[0], d.centre[1] = d.pos[1] + DOOR_MID_M, d.centre[2] = d.pos[2];
			d.radius = DOOR_AIM_M;
		}
		int len = p[fixed];
		if (p + fixed + 1 + len > end)
			return NULL;
		snprintf(d.label, sizeof(d.label), "%.*s", len, (const char *)(p + fixed + 1));
		p += (fixed + 1 + len + 3) & ~3;
		if (*count < MAX_DOORS && d.target >= CITY(worlds_count() - 2) && d.target < interior_count)
			out[(*count)++] = d;
	}
	return p;
}

// The doors outdoors in world w: Tamriel's (exterior.skdoor) or a city's (<city>.skdoor)
static void load_outside(int w) {
	char path[256];
	snprintf(path, sizeof(path), "%sinteriors/%s.skdoor", pack, w ? worlds_prefix_of(w) : "exterior");
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	outside_count = 0;
	const int version = d && size >= 8 ? *(uint16_t *)(d + 4) : 0;
	if (d && !memcmp(d, "SKDR", 4) && version >= 1 && version <= SKDR_VERSION)
		if (!read_doors(d + 8, d + size, outside, &outside_count, version >= 2))
			sceClibPrintf("interior: bad file %s\n", path);
	free(d);
}

// Outdoors in world w from now on: its doors, and the cells whose files are the city's
static void set_world(int w) {
	if (w == worlds_current())
		return;
	worlds_set(w);
	load_outside(w);
	objects_world_changed();
	navmesh_world_changed();
	loot_world_changed();
	terrain_world_changed();
	sceClibPrintf("interior: world %s (%d doors)\n", worlds_title(w), outside_count);
}

void interior_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%sinteriors/interiors.txt", pack);
	long size = 0;
	char *list = file_read_all(path, &size);
	if (!list) {
		sceClibPrintf("interior: no %s\n", path);
		return;
	}
	for (char *line = list, *end = list + size; line < end && interior_count < MAX_INTERIORS;) {
		char *nl = memchr(line, '\n', end - line);
		if (!nl)
			nl = end;
		const char *tab = memchr(line, '\t', nl - line); // ("editor id<tab>place name")
		const char *name_end = tab ? tab : nl;
		snprintf(names[interior_count], sizeof(names[0]), "%.*s", (int)(name_end - line), line);
		snprintf(titles[interior_count], sizeof(titles[0]), "%.*s", tab ? (int)(nl - tab - 1) : 0, tab ? tab + 1 : "");
		interior_count++;
		line = nl + 1;
	}
	free(list);
	worlds_init(pack);
	load_outside(0);
	sceClibPrintf("interior: %d interiors, %d doors outside, %d worlds\n", interior_count, outside_count,
	              worlds_count());
}

int interior_current(void) {
	return current;
}

int interior_fog(float rgb[3]) {
	if (current < 0)
		return 0;
	memcpy(rgb, fog, sizeof(fog));
	return 1;
}

float interior_floor(void) {
	return floor_y;
}

// Skyrim's walk-through doors (a cave's mouth, a mine's back way out: no model to aim at - their
// radius negative, converter/interiors.py): the one you've walked into, once (not the one you
// just came in by, till you've been away from it); -1 none
#define WALK_USE_M 1.9f
#define WALK_REARM_M 5.0f
static int walk_armed;

int interior_walk_door(const float feet[3]) {
	const Door *doors = current < 0 ? outside : inside;
	const int n = current < 0 ? outside_count : inside_count;
	int near = -1;
	float best = 1e9f;
	for (int i = 0; i < n; i++) {
		if (doors[i].radius >= 0.0f || fabsf(doors[i].pos[1] - feet[1]) > 3.0f)
			continue;
		const float d = hypotf(doors[i].pos[0] - feet[0], doors[i].pos[2] - feet[2]);
		if (d < best)
			best = d, near = i;
	}
	if (near < 0 || best > WALK_REARM_M) {
		walk_armed = 1;
		return -1;
	}
	if (walk_armed && best < WALK_USE_M) {
		walk_armed = 0;
		return near;
	}
	return -1;
}

int interior_walk_near(const float feet[3], float within, const char **label) {
	const Door *doors = current < 0 ? outside : inside;
	const int n = current < 0 ? outside_count : inside_count;
	int near = -1;
	float best = within;
	for (int i = 0; i < n; i++) {
		if (doors[i].radius >= 0.0f || fabsf(doors[i].pos[1] - feet[1]) > 3.0f)
			continue;
		const float d = hypotf(doors[i].pos[0] - feet[0], doors[i].pos[2] - feet[2]);
		if (d < best)
			best = d, near = i;
	}
	if (near >= 0 && label)
		*label = doors[near].label;
	return near;
}

void interior_walk_disarm(void) {
	walk_armed = 0;
}

int interior_door_aim(const float eye[3], const float dir[3], float *along, const char **label) {
	const Door *doors = current < 0 ? outside : inside;
	int n = current < 0 ? outside_count : inside_count, best = -1;
	float best_t = 1e9f;
	for (int i = 0; i < n; i++) {
		float t;
		const float r = fabsf(doors[i].radius);
		if (aim_hits(eye, dir, doors[i].centre, r, REACH_M + r * 0.5f, &t) && t < best_t)
			best = i, best_t = t;
	}
	if (best >= 0) {
		*along = best_t;
		if (label)
			*label = doors[best].label;
	}
	return best;
}

int interior_city_gates(float pos[][3], int max) {
	if (current >= TAMRIEL) // (inside, or out in Tamriel: none)
		return 0;
	int n = 0;
	for (int i = 0; i < outside_count && n < max; i++)
		if (outside[i].target == TAMRIEL)
			memcpy(pos[n++], outside[i].pos, 3 * sizeof(float));
	return n;
}

int interior_door_pos(int i, float out[3]) {
	const Door *doors = current < 0 ? outside : inside;
	const int n = current < 0 ? outside_count : inside_count;
	if (i < 0 || i >= n)
		return 0;
	memcpy(out, doors[i].pos, 3 * sizeof(float));
	return 1;
}

// Inside's door outdoors (for NPCs leaving), or the first door
static void exit_of_inside(float out[3]) {
	for (int i = 0; i < inside_count; i++)
		if (inside[i].target < 0) {
			memcpy(out, inside[i].pos, 3 * sizeof(float));
			return;
		}
	if (inside_count)
		memcpy(out, inside[0].pos, 3 * sizeof(float));
}

// Outdoors (from an interior or another world): target TAMRIEL or CITY(n)
static void go_outdoors(int target) {
	if (current >= 0) {
		objects_interior_end();
		objects_release(0); // (the interior's textures)
		doors_load(NULL, NULL);
		loot_load(NULL, NULL, 0);
		activators_load(NULL, NULL);
		furniture_clear();
		traps_clear();
		navmesh_set_interior(NULL);
		terrain_set_active(1);
		atmosphere_set_interior(NULL, 0.0f, 0.0f);
	}
	set_world(WORLD_OF(target));
	current = target;
	inside_count = 0;
	actors_set_interior(TAMRIEL, NULL);
}

static int enter_interior(int index) {
	char path[256];
	snprintf(path, sizeof(path), "%sinteriors/%s.skint", pack, names[index]);
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	const uint8_t *p = d, *end = d + size;
	const int version = d && size >= 9 ? *(uint16_t *)(d + 4) : 0;
	if (!d || size < 9 || memcmp(d, "SKIT", 4) || version < 1 || version > SKIT_VERSION) {
		sceClibPrintf("interior: can't load %s\n", path);
		free(d);
		return 0;
	}
	p = d + ((9 + d[8] + 3) & ~3);
	float hdr[11];
	if (p + sizeof(hdr) > end)
		goto bad;
	memcpy(hdr, p, sizeof(hdr));
	p += sizeof(hdr);
	float near = hdr[3], far = hdr[4], bmin[3] = {hdr[5], hdr[6], hdr[7]}, bmax[3] = {hdr[8], hdr[9], hdr[10]};
	static Door doors[MAX_DOORS];
	int door_count = 0;
	p = read_doors(p, end, doors, &door_count, version >= 4);
	if (!p || p + 4 > end)
		goto bad;
	// The switch: from here on the interior is the world
	if (current >= 0)
		objects_interior_end();
	else // (from outside: the land's and the objects' video memory back - they stream in again on the way out)
		terrain_release();
	objects_release(1); // (and the textures nothing shown uses now)
	cell_near = near, cell_far = far;
	float radius = interior_radius(&near);
	objects_interior_begin(radius);
	uint32_t tiles;
	memcpy(&tiles, p, 4);
	p += 4;
	for (uint32_t i = 0; i < tiles; i++) {
		float box[6];
		uint32_t len;
		int32_t room = -1;
		const int head = version >= 9 ? 32 : 28; // (9: its room)
		if (p + head > end)
			break;
		memcpy(box, p, 24);
		if (version >= 9)
			memcpy(&room, p + 24, 4);
		memcpy(&len, p + head - 4, 4);
		p += head;
		if (p + len > end || !objects_interior_tile(box, box + 3, room, p, (long)len))
			break;
		p += (len + 3) & ~3;
	}
	uint32_t entries = 0;
	if (p + 4 <= end) {
		memcpy(&entries, p, 4);
		p += 4;
		if (p + (size_t)entries * 52 > end)
			entries = 0;
	}
	objects_interior_collision(p, (int)entries, bmin, bmax);
	const uint8_t *after = doors_load(version >= 2 ? p + (size_t)entries * 52 : NULL, end);
	const uint8_t *after_loot = loot_load(version >= 3 ? after : NULL, end, version >= 5);
	const uint8_t *after_acts = activators_load(version >= 6 ? after_loot : NULL, end);
	furniture_clear();
	const uint8_t *after_furniture = furniture_append(version >= 7 ? after_acts : NULL, end);
	traps_clear();
	const uint8_t *after_traps = traps_append(version >= 8 ? after_furniture : NULL, end);
	if (version >= 9 && after_traps && after_traps + 4 <= end) { // (Skyrim's rooms and portals)
		const uint8_t *q = after_traps;
		uint32_t nr = 0, np = 0, nl = 0;
		memcpy(&nr, q, 4), q += 4;
		const float *boxes = (const float *)q;
		q += (size_t)nr * 24;
		if (q + 4 <= end) {
			memcpy(&np, q, 4), q += 4;
			const uint8_t *pd = q;
			q += (size_t)np * 28;
			if (q + 4 <= end) {
				memcpy(&nl, q, 4), q += 4;
				if (q + (size_t)nl * 4 <= end)
					objects_interior_rooms(boxes, (int)nr, pd, (int)np, (const int16_t *)q, (int)nl);
			}
		}
	}
	snprintf(path, sizeof(path), "interiors/%s.sknav", names[index]);
	navmesh_set_interior(path);
	doors_update(0.0f);
	terrain_set_active(0);
	memcpy(fog, hdr, sizeof(fog));
	atmosphere_set_interior(fog, near, radius);
	floor_y = bmin[1];
	memcpy(inside, doors, door_count * sizeof(Door));
	inside_count = door_count;
	for (int i = 0; i < inside_count; i++) // (the ways out: which are walked through - the log)
		game_log("interior: door %d '%s' at %.0f %.0f %.0f%s", i, inside[i].label, inside[i].pos[0], inside[i].pos[1],
		         inside[i].pos[2], inside[i].radius < 0.0f ? " (walk-through)" : "");
	current = index;
	float exit[3] = {0};
	exit_of_inside(exit);
	actors_set_interior(index, exit);
	free(d);
	sceClibPrintf("interior: entered %s (%u tiles, %d doors)\n", names[index], (unsigned)tiles, door_count);
	return 1;
bad:
	sceClibPrintf("interior: bad file %s\n", path);
	free(d);
	return 0;
}

int interior_use_door(int door, float pos[3], float *yaw_deg) {
	const Door *doors = current < 0 ? outside : inside;
	int n = current < 0 ? outside_count : inside_count;
	if (door < 0 || door >= n)
		return 0;
	Door d = doors[door]; // (the list is replaced when the world changes)
	if (d.target < 0)
		go_outdoors(d.target);
	else if (!enter_interior(d.target))
		return 0;
	snprintf(place, sizeof(place), "%s", d.label);
	memcpy(pos, d.arrival, 3 * sizeof(float));
	*yaw_deg = d.arrival_yaw * 180.0f / (float)M_PI;
	memcpy(entry_pos, d.arrival, sizeof(entry_pos));
	entry_yaw = *yaw_deg;
	return 1;
}

int interior_go(int index, const float entry[3], float entry_yaw_deg) {
	walk_armed = 0; // (arriving anywhere - a door, a save, fast travel: not straight out by a walk-through door you're stood at)
	if (index < 0) {
		if (index < CITY(worlds_count() - 2))
			index = TAMRIEL; // (a city no longer in the pack)
		if (index != current)
			go_outdoors(index);
	} else if (index != current && !enter_interior(index)) {
		return 0;
	}
	memcpy(entry_pos, entry, sizeof(entry_pos));
	entry_yaw = entry_yaw_deg;
	return 1;
}

void interior_entry(float pos[3], float *yaw_deg) {
	memcpy(pos, entry_pos, sizeof(entry_pos));
	*yaw_deg = entry_yaw;
}

const char *interior_place(void) {
	if (place[0])
		return place;
	return current >= 0 ? (titles[current][0] ? titles[current] : names[current]) : worlds_title(worlds_current()); // (its name: a quest's move gives no door's)
}

void interior_set_place(const char *name) {
	snprintf(place, sizeof(place), "%s", name ? name : "");
}

const char *interior_id(void) {
	return current >= 0 ? names[current] : "";
}

int interior_doors(float pos[][3], const char **labels, int max) {
	if (current < 0)
		return 0;
	int n = 0;
	for (; n < inside_count && n < max; n++) {
		memcpy(pos[n], inside[n].pos, sizeof(pos[n]));
		labels[n] = inside[n].label;
	}
	return n;
}

const char *interior_title(int index) {
	return index >= 0 && index < interior_count ? titles[index] : "";
}

int interior_location_cleared(const char *title) {
	if (!title || !title[0])
		return 0;
	int hostile = 0, alive = 0;
	for (int i = 0; i < interior_count; i++) {
		if (strcmp(titles[i], title))
			continue;
		int left = 0;
		hostile += creatures_place_hostiles(i, &left);
		alive += left;
	}
	return hostile > 0 && alive == 0;
}

int interior_find(const char *editor_id) {
	for (int i = 0; i < interior_count; i++)
		if (!strcmp(names[i], editor_id))
			return i;
	return -1;
}
