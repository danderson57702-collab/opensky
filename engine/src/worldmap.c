#include "worldmap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "sktex.h"
#include "music.h"
#include "ui.h"
#include "worlds.h"

#define DISCOVER_M 45.0f // a place is discovered when you come this near its marker
#define M_VISIBLE 1
#define M_TRAVEL 2
#define M_REGION 4

typedef struct {
	int kind, flags, discovered;
	char city[16]; // (a walled city's prefix, worlds.h - "" Tamriel)
	float x, z;
	float y; // (its height, Skyrim's marker's: fast travel lands there; NAN none)
	char name[40];
} Marker;

static Marker markers[MAP_MARKERS_MAX];
static int marker_count;
static unsigned province_tex, region_tex;
static float province_bounds[4], region_bounds[4];

void worldmap_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%smap/province.sktex", pack_dir);
	province_tex = sktex_load(path);
	snprintf(path, sizeof(path), "%smap/region.sktex", pack_dir);
	region_tex = sktex_load(path);
	snprintf(path, sizeof(path), "%smap/map.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		float b[4];
		if (line[0] == 'P' && sscanf(line + 2, "%f %f %f %f", &b[0], &b[1], &b[2], &b[3]) == 4)
			memcpy(province_bounds, b, sizeof(b));
		else if (line[0] == 'R' && sscanf(line + 2, "%f %f %f %f", &b[0], &b[1], &b[2], &b[3]) == 4)
			memcpy(region_bounds, b, sizeof(b));
		else if (line[0] == 'M' && marker_count < MAP_MARKERS_MAX) {
			Marker *m = &markers[marker_count];
			int used = 0;
			if (sscanf(line + 2, "%d %d %f %f %n", &m->kind, &m->flags, &m->x, &m->z, &used) == 4 && used) {
				const char *nm = line + 2 + used;
				m->city[0] = 0;
				if (nm[0] == '@') { // (a walled city's own: "@whiterun Dragonsreach")
					int skip = 0;
					sscanf(nm + 1, "%15s %n", m->city, &skip);
					nm += 1 + skip;
				}
				m->y = NAN;
				if (nm[0] == '^') { // (its height: "^322.4 High Hrothgar")
					int skip = 0;
					sscanf(nm + 1, "%f %n", &m->y, &skip);
					nm += 1 + skip;
				}
				snprintf(m->name, sizeof(m->name), "%s", nm);
				m->discovered = (m->flags & M_VISIBLE) != 0; // (the cities show from the start)
				marker_count++;
			}
		}
	}
	free(buf);
}

void worldmap_update(float x, float z) {
	for (int i = 0; i < marker_count; i++) {
		Marker *m = &markers[i];
		if (m->discovered || !(m->flags & M_REGION) || hypotf(m->x - x, m->z - z) > DISCOVER_M)
			continue;
		m->discovered = 1;
		char text[64];
		snprintf(text, sizeof(text), "Discovered: %s", m->name);
		ui_notice(text, 4.0f);
		static const char *const FOUND[3] = {"mus_discover_genericlocation_01", "mus_discover_genericlocation_02",
		                                     "mus_discover_genericlocation_03"};
		music_special(FOUND[i % 3]); // (Skyrim's sting for a place found)
	}
}

int worldmap_count(void) {
	return marker_count;
}

int worldmap_marker(int i, int *kind, const char **name, float *x, float *z, int *discovered, int *in_region) {
	if (i < 0 || i >= marker_count)
		return 0;
	const Marker *m = &markers[i];
	*kind = m->kind, *name = m->name, *x = m->x, *z = m->z;
	*discovered = m->discovered, *in_region = (m->flags & M_REGION) != 0;
	return 1;
}

int worldmap_marker_world(int i) {
	if (i < 0 || i >= marker_count || !markers[i].city[0])
		return 0;
	for (int w = 1; w < worlds_count(); w++) // (the worlds are read after the map: looked up now)
		if (!strcmp(worlds_prefix_of(w), markers[i].city))
			return w;
	return 0;
}

unsigned worldmap_province(float bounds[4]) {
	memcpy(bounds, province_bounds, sizeof(province_bounds));
	return province_tex;
}

unsigned worldmap_region(float bounds[4]) {
	memcpy(bounds, region_bounds, sizeof(region_bounds));
	return region_tex;
}

void worldmap_save(uint8_t bits[MAP_MARKERS_MAX / 8]) {
	memset(bits, 0, MAP_MARKERS_MAX / 8);
	for (int i = 0; i < marker_count; i++)
		if (markers[i].discovered)
			bits[i / 8] |= (uint8_t)(1 << (i % 8));
}

void worldmap_load(const uint8_t bits[MAP_MARKERS_MAX / 8]) {
	for (int i = 0; i < marker_count; i++)
		markers[i].discovered = (bits[i / 8] >> (i % 8) & 1) || (markers[i].flags & M_VISIBLE);
}

float worldmap_marker_height(int i) {
	return i >= 0 && i < marker_count ? markers[i].y : NAN;
}
