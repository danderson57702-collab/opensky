#include "worlds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"

#define MAX_WORLDS 48 // (Tamriel, 5 walled cities, 27 worlds of their own: the whole map, 29 Oct)
#define MAX_CITY_CELLS 3072 // (a world of its own whole: Sovngarde is 2969 cells)

static struct {
	char prefix[24], title[32];
	int cells[MAX_CITY_CELLS][2], cell_count;
	int own_land; // a world of its own (a grotto, Labyrinthian): its land and everything in it its own
} worlds[MAX_WORLDS] = {{"tamriel", "Outside", {{0}}, 0, 0}}; // (its name: the pack's "=" line, from the player's copy)
static int world_count = 1, current;

void worlds_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sworlds.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line && world_count < MAX_WORLDS; line = strtok(NULL, "\n")) {
		int used = 0;
		if (line[0] == '=' && line[1] == ' ') { // (the open world's own name)
			snprintf(worlds[0].title, sizeof(worlds[0].title), "%s", line + 2);
			for (char *t = worlds[0].title; *t; t++)
				if (*t == '_')
					*t = ' ';
			continue;
		}
		if (sscanf(line, "%23s %31s%n", worlds[world_count].prefix, worlds[world_count].title, &used) != 2)
			continue;
		for (char *t = worlds[world_count].title; *t; t++)
			if (*t == '_')
				*t = ' ';
		const char *p = line + used;
		if (p[0] == ' ' && p[1] == '!') // (its own land)
			worlds[world_count].own_land = 1, p += 2;
		int x, y, n;
		while (worlds[world_count].cell_count < MAX_CITY_CELLS && sscanf(p, " %d,%d%n", &x, &y, &n) == 2) {
			worlds[world_count].cells[worlds[world_count].cell_count][0] = x;
			worlds[world_count].cells[worlds[world_count].cell_count][1] = y;
			worlds[world_count].cell_count++;
			p += n;
		}
		world_count++;
	}
	free(buf);
}

int worlds_count(void) {
	return world_count;
}

const char *worlds_title(int w) {
	return w >= 0 && w < world_count ? worlds[w].title : "";
}

const char *worlds_prefix_of(int w) {
	return w >= 0 && w < world_count ? worlds[w].prefix : "tamriel";
}

void worlds_set(int w) {
	current = w >= 0 && w < world_count ? w : 0;
}

int worlds_current(void) {
	return current;
}

int worlds_find(const char *prefix) {
	for (int w = 0; w < world_count; w++)
		if (!strcmp(worlds[w].prefix, prefix))
			return w;
	return -1;
}

int worlds_own_land(void) {
	return worlds[current].own_land;
}

const char *worlds_terrain_prefix(void) {
	return worlds[current].own_land ? worlds[current].prefix : worlds[0].prefix;
}

const char *worlds_prefix(int cx, int cy) {
	if (worlds[current].own_land)
		return worlds[current].prefix;
	for (int i = 0; current && i < worlds[current].cell_count; i++)
		if (worlds[current].cells[i][0] == cx && worlds[current].cells[i][1] == cy)
			return worlds[current].prefix;
	return worlds[0].prefix;
}
