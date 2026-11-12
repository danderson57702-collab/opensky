#include "furniture.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "aim.h"

#define MAX_FURNITURE 256
#define MAX_MARKERS 4
#define REACH_M 2.4f
#define ENTRY_FRONT 0x1 // (Skyrim's marker entry points)
#define ENTRY_RIGHT 0x4
#define ENTRY_LEFT 0x8

typedef struct {
	float pos[3], heading;
	int anim, entry;
} Marker;

typedef struct {
	uint32_t ref;
	int kind, marker_count, station;
	float centre[3], radius;
	Marker markers[MAX_MARKERS];
	char label[64];
} Furniture;

static Furniture furniture[MAX_FURNITURE];
static int furniture_count;

void furniture_clear(void) {
	furniture_count = 0;
}

const uint8_t *furniture_append(const uint8_t *p, const uint8_t *end) {
	if (!p || p + 4 > end)
		return NULL;
	uint32_t n;
	memcpy(&n, p, 4);
	p += 4;
	for (uint32_t i = 0; i < n; i++) {
		if (p + 24 > end)
			return NULL;
		Furniture f;
		memset(&f, 0, sizeof(f));
		memcpy(&f.ref, p, 4);
		f.kind = p[4], f.station = p[7];
		const int markers = p[5], label_len = p[6];
		memcpy(f.centre, p + 8, 12);
		memcpy(&f.radius, p + 20, 4);
		p += 24;
		if (p + markers * 20 + ((label_len + 3) & ~3) > end)
			return NULL;
		for (int k = 0; k < markers; k++, p += 20) {
			if (f.marker_count >= MAX_MARKERS)
				continue;
			Marker *m = &f.markers[f.marker_count++];
			uint16_t anim, entry;
			memcpy(m->pos, p, 12);
			memcpy(&m->heading, p + 12, 4);
			memcpy(&anim, p + 16, 2);
			memcpy(&entry, p + 18, 2);
			m->anim = anim, m->entry = entry;
		}
		snprintf(f.label, sizeof(f.label), "%.*s", label_len, (const char *)p);
		p += (label_len + 3) & ~3;
		if (furniture_count < MAX_FURNITURE)
			furniture[furniture_count++] = f;
	}
	return p;
}

static uint32_t taken[8]; // (seats someone's in - the Jarl's throne: not offered to you)

void furniture_set_taken(int index, int on) {
	if (index < 0 || index >= furniture_count)
		return;
	const uint32_t ref = furniture[index].ref;
	for (int k = 0; k < 8; k++)
		if (taken[k] == ref)
			taken[k] = 0;
	for (int k = 0; k < 8 && on; k++)
		if (!taken[k]) {
			taken[k] = ref;
			break;
		}
}

static int is_taken(uint32_t ref) {
	for (int k = 0; k < 8; k++)
		if (ref && taken[k] == ref)
			return 1;
	return 0;
}

int furniture_aim(const float eye[3], const float dir[3], float *along, const char **label) {
	int best = -1;
	float best_t = 1e9f;
	for (int i = 0; i < furniture_count; i++) {
		float t;
		if (is_taken(furniture[i].ref))
			continue;
		const float r = furniture[i].radius;
		if (aim_hits(eye, dir, furniture[i].centre, r, REACH_M + r * 0.5f, &t) && t < best_t)
			best = i, best_t = t;
	}
	if (best >= 0) {
		*along = best_t;
		*label = furniture[best].label;
	}
	return best;
}

uint32_t furniture_ref(int index) {
	return index >= 0 && index < furniture_count ? furniture[index].ref : 0;
}

int furniture_station(int index) {
	return index >= 0 && index < furniture_count ? furniture[index].station : 0;
}

int furniture_find(const char *label) {
	for (int i = 0; i < furniture_count; i++)
		if (!strcmp(furniture[i].label, label))
			return i;
	return -1;
}

int furniture_kind(int index) {
	return index >= 0 && index < furniture_count ? furniture[index].kind : 0;
}

int furniture_seat(int index, float x, float z, float pos[3], float *heading, const char **in, const char **idle,
                   const char **out) {
	if (index < 0 || index >= furniture_count)
		return 0;
	const Furniture *f = &furniture[index];
	const Marker *best = NULL;
	float best_d = 1e9f;
	const int bed = f->kind == FURN_BED || f->kind == FURN_BEDROLL;
	for (int k = 0; k < f->marker_count; k++) {
		const float d = hypotf(f->markers[k].pos[0] - x, f->markers[k].pos[2] - z);
		if (f->markers[k].anim == (bed ? 2 : 1) && d < best_d) // (sitting / sleeping markers)
			best = &f->markers[k], best_d = d;
	}
	if (!best)
		return 0;
	memcpy(pos, best->pos, 3 * sizeof(float));
	*heading = best->heading;
	if (f->kind == FURN_BEDROLL) {
		*in = "bedroll_in", *idle = "bedroll", *out = "bedroll_out";
		return 1;
	}
	if (bed) { // in from the side you're on, if the bed allows it
		const float side = (x - best->pos[0]) * cosf(best->heading) + (z - best->pos[2]) * -sinf(best->heading);
		const int right = best->entry & ENTRY_RIGHT && (side > 0.0f || !(best->entry & ENTRY_LEFT));
		*in = right ? "bed_right_in" : "bed_left_in", *idle = right ? "bed_right" : "bed_left";
		*out = right ? "bed_right_out" : "bed_left_out";
		return 1;
	}
	if (f->kind == FURN_STOOL) {
		*in = "stool_in", *idle = "stool_idle", *out = "stool_out";
		return 1;
	}
	// From the side you're on, if the seat can be got onto from there (Skyrim's entry points);
	// the seat faces (-sin h, -cos h), its right is (cos h, -sin h)
	const float side = (x - best->pos[0]) * cosf(best->heading) + (z - best->pos[2]) * -sinf(best->heading);
	const int entry = best->entry ? best->entry : ENTRY_FRONT;
	if (side > 0.3f && (entry & ENTRY_RIGHT))
		*in = "chair_right_in", *out = "chair_right_out";
	else if (side < -0.3f && (entry & ENTRY_LEFT))
		*in = "chair_left_in", *out = "chair_left_out";
	else if (entry & ENTRY_FRONT)
		*in = "chair_front_in", *out = "chair_front_out";
	else if (entry & ENTRY_LEFT)
		*in = "chair_left_in", *out = "chair_left_out";
	else
		*in = "chair_right_in", *out = "chair_right_out";
	*idle = "chair_idle";
	return 1;
}
