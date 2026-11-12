// Host harness: real navmeshes from the pack around Riverwood; paths between the spots
// NPCs use must stay on the navmesh and be reasonably direct.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "navmesh.h"
#include "stream.h"
#ifndef PACK
#define PACK "../../pack/"
#endif

// Synchronous stand-in for the streaming worker
void stream_submit(StreamJob *job) {
	job->work(job);
	job->finish(job, 0);
	job->discard(job);
}
unsigned long long stream_now_us(void) { return 0; }

static int on_mesh(const float *a, const float *b) {
	for (int s = 1; s < 20; s++) {
		float t = s / 20.0f, x = a[0] + (b[0] - a[0]) * t, y = a[1] + (b[1] - a[1]) * t, z = a[2] + (b[2] - a[2]) * t;
		NavRef r;
		float h;
		if (!navmesh_locate(x, y, z, &r, &h))
			return 0;
	}
	return 1;
}

int main(void) {
	navmesh_init(PACK, "tamriel");
	// Riverwood spots (engine space) from the NPC schedules: forge, mill, Hod's sawmill,
	// Faendal's hunting ground, houses' doors
	const float spots[][3] = {{294.1f, -1.2f, 644.8f}, {263.8f, -1.6f, 645.5f}, {277.6f, 1.3f, 635.8f},
	                          {269.9f, 3.4f, 698.8f}, {305.8f, -1.2f, 639.7f}, {335.8f, -0.1f, 674.2f},
	                          {332.3f, -0.1f, 635.4f}};
	const int n = sizeof(spots) / sizeof(spots[0]);
	navmesh_update(300.0f, 650.0f);
	int failures = 0;
	for (int i = 0; i < n; i++)
		for (int j = 0; j < n; j++) {
			if (i == j)
				continue;
			NavRef a, b;
			float ha, hb, f[3], t[3];
			if (!navmesh_locate(spots[i][0], spots[i][1], spots[i][2], &a, &ha) ||
			    !navmesh_locate(spots[j][0], spots[j][1], spots[j][2], &b, &hb)) {
				printf("spot %d or %d not on the navmesh\n", i, j);
				failures++;
				continue;
			}
			f[0] = spots[i][0], f[1] = ha, f[2] = spots[i][2];
			t[0] = spots[j][0], t[1] = hb, t[2] = spots[j][2];
			float pts[64 * 3];
			int k = navmesh_path(a, f, b, t, pts, 64);
			if (!k) {
				printf("%d -> %d: no path\n", i, j);
				failures++;
				continue;
			}
			float len = 0.0f, straight = hypotf(t[0] - f[0], t[2] - f[2]);
			const float *prev = f;
			int off = 0;
			for (int p = 0; p < k; p++) {
				len += hypotf(pts[p * 3] - prev[0], pts[p * 3 + 2] - prev[2]);
				off += !on_mesh(prev, &pts[p * 3]);
				prev = &pts[p * 3];
			}
			printf("%d -> %d: %2d corners, %6.1f m (straight %6.1f m)%s\n", i, j, k, len, straight,
			       off ? "  LEAVES THE NAVMESH" : "");
			failures += off > 0 || len > straight * 3.0f + 20.0f;
		}
	printf(failures ? "navmesh: %d FAILURES\n" : "navmesh: all paths ok\n", failures);
	return failures != 0;
}
