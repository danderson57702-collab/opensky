#include <stdio.h>
#include <math.h>
#include "collision.h"
#include "terrain.h"
int terrain_height_at(float x, float z, float *out) { (void)x; (void)z; *out = 0.0f; return 1; }
int terrain_cell_x(float x) { return (int)floorf(x / CELL_SIZE_M); }
int terrain_cell_y(float z) { return (int)floorf(-z / CELL_SIZE_M); }
static int quad(float *t, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float dx, float dy, float dz) {
	float q[18] = {ax,ay,az, bx,by,bz, cx,cy,cz, ax,ay,az, cx,cy,cz, dx,dy,dz};
	/* Skyrim collision faces outward: make floors and slopes face up (walls don't care) */
	float e1[3] = {bx-ax, by-ay, bz-az}, e2[3] = {cx-ax, cy-ay, cz-az};
	if (e1[2]*e2[0] - e1[0]*e2[2] < 0) {
		float r[18] = {ax,ay,az, cx,cy,cz, bx,by,bz, ax,ay,az, dx,dy,dz, cx,cy,cz};
		for (int i = 0; i < 18; i++) q[i] = r[i];
	}
	for (int i = 0; i < 18; i++) t[i] = q[i];
	return 2;
}
/* triangle wound to face away from the interior point c (as Skyrim's collision is) */
static int tri_out(float *t, const float *a, const float *b, const float *d, const float *c) {
	float e1[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]}, e2[3] = {d[0]-a[0], d[1]-a[1], d[2]-a[2]};
	float n[3] = {e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
	float flip = n[0]*(a[0]-c[0]) + n[1]*(a[1]-c[1]) + n[2]*(a[2]-c[2]) < 0;
	const float *v[3] = {a, flip ? d : b, flip ? b : d};
	for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) t[i*3+k] = v[i][k];
	return 1;
}
/* closed box with outward faces */
static int box(float *t, float x0, float y0, float z0, float x1, float y1, float z1) {
	float c[3] = {(x0+x1)/2, (y0+y1)/2, (z0+z1)/2}, p[8][3];
	for (int i = 0; i < 8; i++) { p[i][0] = i&1 ? x1 : x0; p[i][1] = i&2 ? y1 : y0; p[i][2] = i&4 ? z1 : z0; }
	static const int f[6][4] = {{0,1,3,2},{4,5,7,6},{0,1,5,4},{2,3,7,6},{0,2,6,4},{1,3,7,5}};
	int n = 0;
	for (int i = 0; i < 6; i++) {
		n += tri_out(t + n*9, p[f[i][0]], p[f[i][1]], p[f[i][2]], c);
		n += tri_out(t + n*9, p[f[i][0]], p[f[i][2]], p[f[i][3]], c);
	}
	return n;
}
/* diamond boulder: belly faces down-outward under the equator, slopes up to the top */
#define ROCK_X 80.0f
#define ROCK_R 1.6f
static int boulder(float *t) {
	float c[3] = {ROCK_X, 1.0f, 0}, top[3] = {ROCK_X, 2.6f, 0}, bot[3] = {ROCK_X, -0.4f, 0};
	float eq[4][3] = {{ROCK_X+ROCK_R,1,0}, {ROCK_X,1,ROCK_R}, {ROCK_X-ROCK_R,1,0}, {ROCK_X,1,-ROCK_R}};
	int n = 0;
	for (int i = 0; i < 4; i++) {
		n += tri_out(t + n*9, eq[i], eq[(i+1)%4], top, c);
		n += tri_out(t + n*9, eq[i], eq[(i+1)%4], bot, c);
	}
	return n;
}
static int inside_boulder(float x, float y, float z) {
	float dy = y - 1.0f;
	return fabsf(x - ROCK_X) / ROCK_R + fabsf(z) / ROCK_R + (dy > 0 ? dy / 1.6f : -dy / 1.4f) < 0.97f;
}
int main(void) {
	float tris[9 * 900]; int n = 0;
	n += quad(tris + n*9, 5,0,-10, 5,0,10, 5,4,10, 5,4,-10);          /* wall at x=5 */
	n += quad(tris + n*9, -5,0.3f,-2, -3,0.3f,-2, -3,0.3f,2, -5,0.3f,2); /* 0.3 m step at x -5..-3 */
	n += quad(tris + n*9, 0,1.0f,8, 2,1.0f,8, 2,1.0f,10, 0,1.0f,10);    /* 1 m ledge at z 8..10 */
	n += quad(tris + n*9, 10,2.0f,-1, 14,2.0f,-1, 14,2.0f,1, 10,2.0f,1);/* bridge deck 2 m up */
	{ /* degenerate (zero-area) triangles, as found in Skyrim meshes */
		float d[18] = {2,0.5f,0, 2,0.5f,0, 2,0.5f,0,  3,0.2f,0, 3.5f,0.2f,0, 4,0.2f,0};
		for (int i = 0; i < 18; i++) tris[n * 9 + i] = d[i];
		n += 2;
	}
	/* plank bridge at z = 20..30 (x -2..2): 0.12 m thick boards with vertical edges, 0.25 m apart */
	for (int b = 0; b < 20; b++) {
		float z0 = 20 + b * 0.5f, z1 = z0 + 0.25f, y = 0.12f + 0.01f * (b % 3);
		n += quad(tris + n*9, -2,y,z0, 2,y,z0, 2,y,z1, -2,y,z1);      /* top */
		n += quad(tris + n*9, -2,0,z0, 2,0,z0, 2,y,z0, -2,y,z0);      /* front edge */
		n += quad(tris + n*9, -2,0,z1, 2,0,z1, 2,y,z1, -2,y,z1);      /* back edge */
	}
	/* pit: 0.8 m walls around x 20..22, z -1..1 (player starts inside) */
	n += quad(tris + n*9, 20,0,-1, 20,0,1, 20,0.8f,1, 20,0.8f,-1);
	n += quad(tris + n*9, 22,0,-1, 22,0,1, 22,0.8f,1, 22,0.8f,-1);
	n += quad(tris + n*9, 20,0,-1, 22,0,-1, 22,0.8f,-1, 20,0.8f,-1);
	n += quad(tris + n*9, 20,0,1, 22,0,1, 22,0.8f,1, 20,0.8f,1);
	n += quad(tris + n*9, 22,0.8f,-3, 26,0.8f,-3, 26,0.8f,3, 22,0.8f,3); /* walkable top beside the pit */
	/* half-buried road rock at x 40..41.6: 0.55 m high, sides ~55 degrees */
	n += quad(tris + n*9, 40,0,-3, 40.4f,0.55f,-3, 40.4f,0.55f,3, 40,0,3);
	n += quad(tris + n*9, 40.4f,0.55f,-3, 41.2f,0.55f,-3, 41.2f,0.55f,3, 40.4f,0.55f,3);
	n += quad(tris + n*9, 41.2f,0.55f,-3, 41.6f,0,-3, 41.6f,0,3, 41.2f,0.55f,3);
	n += boulder(tris + n*9);
	n += box(tris + n*9, 60, 1.3f, -3, 64, 1.7f, 3); /* low slab (underside 1.3 m) */
	n += box(tris + n*9, 66, 2.0f, -3, 70, 2.4f, 3); /* overhang with head room (underside 2 m) */
	/* staircase of separate tread blocks 3 cm apart (as in Skyrim's tower stairs), x -42..-40,
	   rising 0.35 m per 0.4 m from z = 0 */
	for (int i = 0; i < 10; i++)
		n += box(tris + n*9, -42, 0.35f * i, i * 0.4f, -40, 0.35f * (i + 1), i * 0.4f + 0.37f);
	collision_set_region(-100, -100, 100, 100, tris, NULL, n);
	int ok = 1;
	#define CHECK(c, msg) do { int r_ = (c); printf("%-52s %s\n", msg, r_ ? "PASS" : "FAIL"); ok &= r_; } while (0)
	Player p = {0, 0, 0, 0};
	for (int i = 0; i < 20; i++) collision_move(&p, 0.15f, 0, 1/60.f);
	CHECK(isfinite(p.x) && isfinite(p.z) && isfinite(p.feet), "walking past zero-area triangles stays finite");
	printf("   x=%.3f z=%.3f feet=%.3f\n", p.x, p.z, p.feet);
	p = (Player){0, 0, 0, 0};
	for (int i = 0; i < 100; i++) collision_move(&p, 0.15f, 0, 1/60.f);
	CHECK(p.x <= 5 - PLAYER_RADIUS + 0.02f && p.x > 4.0f, "walking into a wall stops at the wall");
	printf("   x=%.3f\n", p.x);
	p = (Player){-1, 0, 0, 0};
	for (int i = 0; i < 20; i++) collision_move(&p, -0.15f, 0, 1/60.f);
	CHECK(fabsf(p.feet - 0.3f) < 0.01f, "steps up onto a 0.3 m step");
	p = (Player){1, 0, 6, 0};
	for (int i = 0; i < 30; i++) collision_move(&p, 0, 0.15f, 1/60.f);
	CHECK(p.z < 8.0f && p.feet < 0.01f, "blocked by a 1 m ledge (too high to step)");
	printf("   z=%.3f feet=%.3f\n", p.z, p.feet);
	p = (Player){12, 2.0f, 0, 0};
	for (int i = 0; i < 10; i++) collision_move(&p, 0, 0, 1/60.f);
	CHECK(fabsf(p.feet - 2.0f) < 0.01f, "stands on a bridge deck above the terrain");
	for (int i = 0; i < 40; i++) collision_move(&p, 0.15f, 0, 1/60.f);
	for (int i = 0; i < 60; i++) collision_move(&p, 0, 0, 1/60.f);
	CHECK(p.x > 14 && fabsf(p.feet) < 0.01f, "walks off the bridge end and falls to the ground");
	p = (Player){0, 0, 0, 0};
	for (int i = 0; i < 5; i++) collision_move(&p, 1.2f, 0, 1/60.f);
	CHECK(p.x < 5.0f, "fast travel does not tunnel through the wall");
	p = (Player){0, 0, 18, 0, 1};
	int frames = 0;
	while (p.z < 30.5f && frames < 400) { collision_move(&p, 0, 0.15f, 1/60.f); frames++; }
	CHECK(p.z >= 30.5f && frames < 90, "crosses a plank bridge without snagging");
	printf("   frames=%d (ideal ~84) z=%.2f\n", frames, p.z);
	p = (Player){1.5f, 0, 0.6f, 0, 1};
	for (int i = 0; i < 60; i++) collision_move(&p, 0.10f, 0.10f, 1/60.f);
	CHECK(p.z > 3.0f, "slides along a wall when walking into it at an angle");
	printf("   x=%.2f z=%.2f\n", p.x, p.z);
	p = (Player){-41, 0, -1, 0, 1};
	for (int i = 0; i < 300 && p.z < 3.8f; i++) collision_move(&p, 0, 0.06f, 1/60.f);
	CHECK(p.z >= 3.8f && p.feet > 3.4f, "climbs stairs made of blocks with gaps between them");
	printf("   z=%.2f feet=%.2f\n", p.z, p.feet);
	{ /* a jump on flat ground: the whole arc, no snap to the ground on the way down */
		p = (Player){-30, 0, -30, 0, 1};
		collision_move(&p, 0, 0, 1/60.f);
		collision_jump(&p);
		int air = 0; float drop = 0, last = p.feet;
		do { collision_move(&p, 0, 0, 1/60.f); air++; drop = fmaxf(drop, last - p.feet); last = p.feet; }
		while (!p.grounded && air < 200);
		float expect = 2 * PLAYER_JUMP_SPEED / PLAYER_GRAVITY * 60;
		CHECK(fabsf(air - expect) < 4 && drop < 0.25f, "a jump falls all the way down (no snap)");
		printf("   %d frames in the air (expected ~%.0f), largest drop per frame %.2f m\n", air, expect, drop);
	}
	{ /* water 2 m deep over flat ground (x -60, z 30): float at the surface, walk out where it's shallow */
		p = (Player){-60, 0, 30, 0, 1};
		p.in_water = 1, p.water = 2.0f;
		for (int i = 0; i < 120; i++) collision_move(&p, 0, 0, 1/60.f);
		CHECK(p.swimming && fabsf(p.feet - (2.0f - SWIM_DEPTH)) < 0.01f, "floats at the surface of deep water");
		p.water = 1.0f; /* shallow: wade */
		for (int i = 0; i < 30; i++) collision_move(&p, 0, 0, 1/60.f);
		CHECK(!p.swimming && p.grounded && p.feet < 0.01f, "stands on the bottom in shallow water");
		p.water = 2.0f; p.in_water = 1; p.feet = 3.0f; p.grounded = 0; /* fall in from above */
		for (int i = 0; i < 180; i++) collision_move(&p, 0, 0, 1/60.f);
		CHECK(p.swimming && fabsf(p.feet - (2.0f - SWIM_DEPTH)) < 0.01f, "falling in comes back up to swim");
		collision_jump(&p);
		CHECK(p.vy == 0.0f, "no jumping while swimming");
		for (int i = 0; i < 120; i++) { p.swim_dy = -1.0f / 60; collision_move(&p, 0, 0, 1/60.f); }
		CHECK(p.swimming && fabsf(p.feet) < 0.01f, "dives down to the bed, not through it");
		p.swim_dy = 0.0f;
		for (int i = 0; i < 180; i++) collision_move(&p, 0, 0, 1/60.f);
		CHECK(p.swimming && fabsf(p.feet - (2.0f - SWIM_DEPTH)) < 0.01f, "floats back up to the surface");
	}
	p = (Player){21, 0, 0, 0, 1};
	for (int i = 0; i < 60; i++) collision_move(&p, 0.15f, 0, 1/60.f);
	CHECK(p.x < 22, "cannot walk out of a 0.8 m pit");
	collision_jump(&p);
	for (int i = 0; i < 20; i++) collision_move(&p, 0.15f, 0, 1/60.f); /* ~3 m: lands on the 4 m ledge */
	CHECK(p.x > 22.5f && fabsf(p.feet - 0.8f) < 0.01f, "jumps out of the pit onto the ledge");
	printf("   x=%.2f feet=%.2f\n", p.x, p.feet);
	p = (Player){38, 0, 0, 0, 1};
	frames = 0;
	while (p.x < 43 && frames < 200) { collision_move(&p, 0.15f, 0, 1/60.f); frames++; }
	CHECK(p.x >= 43 && frames < 40, "walks over a half-buried 0.55 m road rock");
	printf("   frames=%d (ideal ~34)\n", frames);
	{ /* walk and jump-spam into the boulder from several directions */
		int inside = 0;
		for (int dir = 0; dir < 8; dir++) {
			float a = dir * 0.785398f, sx = -cosf(a), sz = -sinf(a);
			p = (Player){ROCK_X - sx * 4.0f, 0, -sz * 4.0f, 0, 1};
			for (int i = 0; i < 240; i++) {
				if (i % 20 == 0) collision_jump(&p);
				collision_move(&p, sx * 0.15f, sz * 0.15f, 1/60.f);
				for (float h = 0.2f; h < 1.8f; h += 0.4f)
					if (inside_boulder(p.x, p.feet + h, p.z)) {
						if (!inside) printf("   dir %d frame %d: x=%.2f z=%.2f feet=%.2f vy=%.2f h=%.1f\n", dir, i, p.x, p.z, p.feet, p.vy, h);
						inside = 1;
					}
			}
		}
		CHECK(!inside, "jumping into a boulder never ends inside it");
	}
	p = (Player){55, 0, 0, 0, 1};
	for (int i = 0; i < 60; i++) collision_move(&p, 0.15f, 0, 1/60.f);
	CHECK(p.x < 60 && p.feet < 0.01f, "blocked by a slab too low to walk under");
	printf("   x=%.2f feet=%.2f\n", p.x, p.feet);
	p = (Player){68, 0, 0, 0, 1};
	float top = 0;
	collision_jump(&p);
	for (int i = 0; i < 60; i++) { collision_move(&p, 0, 0, 1/60.f); top = fmaxf(top, p.feet); }
	CHECK(top <= 2.0f - 1.8f + 0.01f && p.feet < 0.01f, "jump under an overhang: head stops, lands back");
	printf("   peak feet=%.2f\n", top);
	p = (Player){65, 0, 0, 0, 1};
	for (int i = 0; i < 40; i++) collision_move(&p, 0.15f, 0, 1/60.f);
	CHECK(p.x > 70 && p.feet < 0.01f, "walks under an overhang with head room");
	{ /* camera ray: the wall at x = 5 lies between (3, 1.5, 0) and (7, 1.5, 0) halfway */
		const float a[3] = {3, 1.5f, 0}, b[3] = {7, 1.5f, 0}, c[3] = {3, 1.5f, 0}, d[3] = {4, 1.5f, 2};
		float t = collision_ray(a, b), t2 = collision_ray(c, d);
		CHECK(fabsf(t - 0.5f) < 0.01f, "camera ray stops at the wall between");
		CHECK(t2 >= 1.0f, "camera ray with nothing between is clear");
		printf("   hit at %.3f, clear %.3f\n", t, t2);
	}
	printf(ok ? "ALL PASS\n" : "FAILURES\n");
	return !ok;
}
