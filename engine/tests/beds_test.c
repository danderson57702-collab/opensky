// Host harness: NPCs' beds in Riverwood's houses (from the pack's schedules). Every bed's
// sleeping spot needs a walkable point beside it, reached by a navmesh path from the
// house's other spots (never by walking straight through the furniture).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fileio.h"
#include "navmesh.h"
#include "stream.h"
#ifndef PACK
#define PACK "../../pack/"
#endif

void stream_submit(StreamJob *job) {
	job->work(job);
	job->finish(job, 0);
	job->discard(job);
}
unsigned long long stream_now_us(void) { return 0; }

typedef struct {
	uint16_t start, end;
	uint8_t kind, activity, sleep_clip, pad;
	float pos[3], heading, radius;
	int32_t interior;
	float inside[3], inside_heading, inside_radius;
} Sched;

#define MAX_SPOTS 64
static char interiors[128][64];
static int interior_count;

int main(void) {
	FILE *f = fopen(PACK "interiors/interiors.txt", "r");
	if (!f)
		return printf("no interiors\n"), 1;
	char line[160]; // ("editor id<tab>place name" a line)
	while (interior_count < 128 && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\t\r\n")] = 0;
		snprintf(interiors[interior_count++], sizeof(interiors[0]), "%s", line);
	}
	fclose(f);
	// Every NPC's schedule entries inside Riverwood's houses
	Sched spots[128][MAX_SPOTS];
	int counts[128] = {0};
	char names[128][MAX_SPOTS][32];
	f = fopen(PACK "actors/actors.txt", "r");
	char file[96];
	while (f && fscanf(f, "%95s", file) == 1) {
		char path[256];
		snprintf(path, sizeof(path), PACK "actors/%s", file);
		long size = 0;
		uint8_t *d = file_read_all(path, &size);
		if (!d || size < 44)
			continue;
		uint32_t bones, entries;
		memcpy(&bones, d + 40, 4);
		long o = 44 + bones * 16;
		memcpy(&entries, d + o, 4);
		o += 4;
		for (uint32_t e = 0; e < entries; e++, o += sizeof(Sched)) {
			Sched s;
			memcpy(&s, d + o, sizeof(s));
			if (s.kind == 1 && s.interior >= 0 && s.interior < interior_count &&
			    strstr(interiors[s.interior], "riverwood") && counts[s.interior] < MAX_SPOTS) {
				snprintf(names[s.interior][counts[s.interior]], 32, "%.31s", file);
				spots[s.interior][counts[s.interior]++] = s;
			}
		}
		free(d);
	}
	if (f)
		fclose(f);
	navmesh_init(PACK, "tamriel");
	int failures = 0, beds = 0;
	for (int in = 0; in < interior_count; in++) {
		if (!counts[in])
			continue;
		char nav[128];
		snprintf(nav, sizeof(nav), "interiors/%s.sknav", interiors[in]);
		navmesh_set_interior(nav);
		for (int i = 0; i < counts[in]; i++) {
			const Sched *s = &spots[in][i];
			if (s->activity != 1)
				continue;
			beds++;
			NavRef bed;
			float side[3];
			if (!navmesh_nearest(s->inside[0], s->inside[1], s->inside[2], 2.5f, &bed, side)) {
				printf("%s, %s: no walkable point by the bed\n", interiors[in], names[in][i]);
				failures++;
				continue;
			}
			float off = hypotf(side[0] - s->inside[0], side[2] - s->inside[2]);
			int paths = 0, ok = 0;
			for (int j = 0; j < counts[in]; j++) {
				const Sched *o = &spots[in][j];
				if (o->activity == 1)
					continue;
				NavRef from;
				float h, fp[3] = {o->inside[0], o->inside[1], o->inside[2]}, pts[64 * 3];
				if (!navmesh_locate(fp[0], fp[1], fp[2], &from, &h) && !navmesh_nearest(fp[0], fp[1], fp[2], 2.5f, &from, fp))
					continue;
				paths++;
				ok += navmesh_path(from, fp, bed, side, pts, 64) > 0;
			}
			printf("%-28s %-26s bed side %.2f m away, reached from %d of %d spots\n", interiors[in], names[in][i],
			       off, ok, paths);
			failures += paths > 0 && ok == 0;
		}
	}
	printf(failures ? "beds: %d FAILURES (of %d)\n" : "beds: all %d reachable\n", failures ? failures : beds, beds);
	return failures != 0;
}
