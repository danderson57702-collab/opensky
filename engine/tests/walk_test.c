// Host harness: real terrain + real object collision from the pack, scripted walks.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "collision.h"
#include "skmesh.h"
#include "terrain.h"
#ifndef PACK
#define PACK "../../pack/" // relative to engine/tests
#endif
typedef struct { int cx, cy, ok; float h[33*33]; } HCell;
static HCell hc[64]; static int nh;
static HCell *hcell(int cx, int cy) {
	for (int i = 0; i < nh; i++) if (hc[i].cx == cx && hc[i].cy == cy) return &hc[i];
	HCell *c = &hc[nh++]; c->cx = cx; c->cy = cy; c->ok = 0;
	char p[256]; snprintf(p, sizeof p, PACK "terrain/tamriel_%d_%d.skcell", cx, cy);
	FILE *f = fopen(p, "rb"); if (f) { fseek(f, 20, SEEK_SET); c->ok = fread(c->h, 4, 33*33, f) == 33*33; fclose(f); }
	return c;
}
int terrain_cell_x(float x) { return (int)floorf(x / CELL_SIZE_M); }
int terrain_cell_y(float z) { return (int)floorf(-z / CELL_SIZE_M); }
int terrain_height_at(float x, float z, float *out) {
	int cx = terrain_cell_x(x), cy = terrain_cell_y(z); HCell *c = hcell(cx, cy); if (!c->ok) return 0;
	float sp = CELL_SIZE_M / 32, fx = (x - cx * CELL_SIZE_M) / sp, fy = (-cy * CELL_SIZE_M - z) / sp;
	int i = (int)fx, j = (int)fy; if (i > 31) i = 31; if (j > 31) j = 31; if (i < 0) i = 0; if (j < 0) j = 0;
	float a = fx - i, b = fy - j, *h = c->h;
	float h00 = h[j*33+i], h10 = h[j*33+i+1], h01 = h[(j+1)*33+i], h11 = h[(j+1)*33+i+1];
	*out = a >= b ? h00 + a*(h10-h00) + b*(h11-h10) : h00 + b*(h01-h00) + a*(h11-h01); return 1;
}
typedef struct { unsigned short model, r; float m[9], t[3]; } Entry;
#define MAX_MODELS 16000
static char names[MAX_MODELS][256]; static int nm;
static SkmMesh meshes[MAX_MODELS]; static int loaded[MAX_MODELS];
static float *col[MAX_MODELS]; static int ncol[MAX_MODELS], colstate[MAX_MODELS];
static void load_col(int mi) {
	colstate[mi] = 1;
	char p[400]; snprintf(p, sizeof p, PACK "meshes/%s.skcol", names[mi]);
	FILE *f = fopen(p, "rb"); if (!f) return;
	// "SKCO", u16 version, u16 reserved, u32 count (same layout the engine reads)
	char magic[4]; unsigned short ver, res; unsigned count = 0;
	if (fread(magic, 4, 1, f) != 1 || fread(&ver, 2, 1, f) != 1 || fread(&res, 2, 1, f) != 1 ||
	    fread(&count, 4, 1, f) != 1 || memcmp(magic, "SKCO", 4) || ver != 1) { fclose(f); return; }
	col[mi] = malloc(count * 36); ncol[mi] = fread(col[mi], 36, count, f); fclose(f);
}
// Mirrors engine/src/objects.c build_collision_region: Skyrim collision of every object
// within 3 cells whose collision overlaps the player's 3x3-cell region.
static void build_region(int pcx, int pcy) {
	float rx0 = (pcx - 1) * CELL_SIZE_M - 4, rx1 = (pcx + 2) * CELL_SIZE_M + 4;
	float rz0 = -(pcy + 2) * CELL_SIZE_M - 4, rz1 = -(pcy - 1) * CELL_SIZE_M + 4;
	int cap = 1 << 16, n = 0; float *tris = malloc(cap * 36);
	for (int cy = pcy - 3; cy <= pcy + 3; cy++) for (int cx = pcx - 3; cx <= pcx + 3; cx++) {
		char p[256]; snprintf(p, sizeof p, PACK "objects/tamriel_%d_%d.skobj", cx, cy);
		FILE *f = fopen(p, "rb"); if (!f) continue;
		fseek(f, 16, SEEK_SET); unsigned count; fread(&count, 4, 1, f);
		Entry *e = malloc(count * sizeof(Entry)); fread(e, sizeof(Entry), count, f); fclose(f);
		for (unsigned k = 0; k < count; k++) {
			int mi = e[k].model; if (!colstate[mi]) load_col(mi);
			for (int t = 0; t < ncol[mi]; t++) {
				float w[9];
				for (int v = 0; v < 3; v++) {
					float *pp = &col[mi][t * 9 + v * 3];
					w[v*3+0] = e[k].m[0]*pp[0] + e[k].m[1]*pp[1] + e[k].m[2]*pp[2] + e[k].t[0];
					w[v*3+1] = e[k].m[3]*pp[0] + e[k].m[4]*pp[1] + e[k].m[5]*pp[2] + e[k].t[1];
					w[v*3+2] = e[k].m[6]*pp[0] + e[k].m[7]*pp[1] + e[k].m[8]*pp[2] + e[k].t[2];
				}
				if (fmaxf(w[0], fmaxf(w[3], w[6])) < rx0 || fminf(w[0], fminf(w[3], w[6])) > rx1 ||
				    fmaxf(w[2], fmaxf(w[5], w[8])) < rz0 || fminf(w[2], fminf(w[5], w[8])) > rz1) continue;
				if (n == cap) { cap *= 2; tris = realloc(tris, cap * 36); }
				memcpy(&tris[(size_t)n * 9], w, sizeof w); n++;
			}
		}
		free(e);
	}
	collision_set_region(rx0, rz0, rx1, rz1, tris, NULL, n); free(tris);
	printf("collision region around cell (%d, %d): %d triangles\n", pcx, pcy, n);
}
int main(void) {
	FILE *f = fopen(PACK "objects/models.txt", "r"); char line[600];
	while (nm < MAX_MODELS && fgets(line, sizeof line, f)) { line[strcspn(line, " \r\n")] = 0; snprintf(names[nm++], 256, "%s", line); }
	fclose(f);
	build_region(1, -4);
	int bad = 0;
	for (int yaw = 0; yaw < 360; yaw += 30) {
		Player p = {1.5f * CELL_SIZE_M, 0, 3.5f * CELL_SIZE_M, 0};
		terrain_height_at(p.x, p.z, &p.feet);
		float y = yaw * (float)M_PI / 180, fx = sinf(y), fz = -cosf(y), moved = 0;
		int nonfinite = 0;
		for (int fr = 0; fr < 900; fr++) {
			float px = p.x, pz = p.z;
			collision_move(&p, fx * 0.15f, fz * 0.15f, 1 / 60.f);
			if (!isfinite(p.x) || !isfinite(p.z) || !isfinite(p.feet)) nonfinite++;
			moved += hypotf(p.x - px, p.z - pz);
		}
		float th; terrain_height_at(p.x, p.z, &th);
		printf("yaw %3d: walked %6.1f m of 135, feet %+7.2f m vs terrain %+7.2f%s\n", yaw, moved, p.feet - th, 0.0f, nonfinite ? "  NON-FINITE!" : "");
		bad += nonfinite;
	}
	printf(bad ? "NON-FINITE STATES SEEN\n" : "all walks finite\n");

	return 0;
}
