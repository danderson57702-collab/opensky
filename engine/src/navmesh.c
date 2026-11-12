#include "navmesh.h"
#include "worlds.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"
#include "stream.h"
#include "terrain.h"

#ifdef __vita__
#include <psp2/kernel/clib.h>
#define nav_log sceClibPrintf
#else
#define nav_log printf
#endif

#define SKNV_VERSION 1
#define MAX_NAV_CELLS 49 // 7x7: the 5x5 plus one ring of hysteresis
#define NO_NEIGHBOUR 0xFFFFFFFFu
#define SEARCH_NODES 6000
#define HASH_SIZE 16384 // power of two, > 2 * SEARCH_NODES
#define AGENT_RADIUS_M 0.3f
#define LOCATE_MAX_DY 4.0f

typedef struct {
	uint16_t v[3];
	uint16_t flags;
	uint32_t nbr[3]; // see converter/navmesh.py
} NavTri;

typedef struct {
	int used, cx, cy;
	const char *prefix; // its world (worlds_prefix)
	int state; // 0 loading, 1 loaded (tri_count may be 0: no navmesh there)
	unsigned gen;
	int vert_count, tri_count;
	float *verts;
	NavTri *tris;
	void *file;
} NavCell;

static char pack_dir[128];
static NavCell cells[MAX_NAV_CELLS];
static NavCell interior; // inside a building: the only navmesh (cell 0, 0)
static int indoors;
static unsigned next_gen = 1;

static int cell_of_x(float x) { return indoors ? 0 : (int)floorf(x / CELL_SIZE_M); }
static int cell_of_z(float z) { return indoors ? 0 : (int)floorf(-z / CELL_SIZE_M); }

static NavCell *find_cell(int cx, int cy) {
	if (indoors)
		return cx == 0 && cy == 0 ? &interior : NULL;
	for (int i = 0; i < MAX_NAV_CELLS; i++)
		if (cells[i].used && cells[i].cx == cx && cells[i].cy == cy)
			return &cells[i];
	return NULL;
}

static NavCell *loaded_cell(int cx, int cy) {
	NavCell *c = find_cell(cx, cy);
	return c && c->state == 1 && c->tri_count ? c : NULL;
}

// --- Loading ----------------------------------------------------------------------------

typedef struct {
	StreamJob base;
	int slot;
	unsigned gen;
	int cx, cy;
	const char *prefix;
	void *file;
	long size;
} NavJob;

static void nav_job_work(StreamJob *base) {
	NavJob *j = (NavJob *)base;
	char path[256];
	snprintf(path, sizeof(path), "%snavmesh/%s_%d_%d.sknav", pack_dir, j->prefix, j->cx, j->cy);
	j->file = file_read_all(path, &j->size);
}

// Installs a .sknav file's contents into c (taking the file); marks it loaded either way
static void install(NavCell *c, void *file, long size) {
	c->state = 1;
	const uint8_t *d = file;
	uint32_t nv = 0, nt = 0;
	if (d && size >= 24 && !memcmp(d, "SKNV", 4) && *(const uint16_t *)(d + 4) == SKNV_VERSION) {
		memcpy(&nv, d + 16, 4);
		memcpy(&nt, d + 20, 4);
		if (size < 24 + (long)nv * 12 + (long)nt * (long)sizeof(NavTri))
			nv = nt = 0;
	}
	if (!nt) {
		free(file);
		return; // no navmesh in this cell
	}
	// The file is kept: vertices and triangles are used in place (4-byte aligned)
	c->file = file;
	c->verts = (float *)(d + 24);
	c->tris = (NavTri *)(d + 24 + nv * 12);
	c->vert_count = (int)nv;
	c->tri_count = (int)nt;
	for (uint32_t i = 0; i < nt; i++)
		for (int k = 0; k < 3; k++)
			if (c->tris[i].v[k] >= nv) {
				nav_log("navmesh: bad vertex index in cell %d,%d\n", c->cx, c->cy);
				c->tri_count = 0;
				return;
			}
}

static int nav_job_finish(StreamJob *base, unsigned long long deadline) {
	(void)deadline;
	NavJob *j = (NavJob *)base;
	NavCell *c = &cells[j->slot];
	if (!c->used || c->gen != j->gen)
		return 1;
	install(c, j->file, j->size);
	j->file = NULL;
	return 1;
}

void navmesh_set_interior(const char *path) {
	free(interior.file);
	memset(&interior, 0, sizeof(interior));
	indoors = path != NULL;
	if (!indoors)
		return;
	char full[256];
	snprintf(full, sizeof(full), "%s%s", pack_dir, path);
	long size = 0;
	void *file = file_read_all(full, &size);
	interior.used = 1;
	install(&interior, file, size);
}

static void nav_job_discard(StreamJob *base) {
	NavJob *j = (NavJob *)base;
	free(j->file);
	free(j);
}

static int nav_job_priority(StreamJob *base) {
	(void)base;
	return STREAM_PRIORITY(-NAV_RADIUS, 0); // small, and NPCs wait on it
}

void navmesh_init(const char *pack, const char *world) {
	(void)world; // cells' files are per world (worlds_prefix)
	snprintf(pack_dir, sizeof(pack_dir), "%s", pack);
}

static int world_changed;

void navmesh_world_changed(void) {
	for (int i = 0; i < MAX_NAV_CELLS; i++)
		if (cells[i].used && cells[i].prefix != worlds_prefix(cells[i].cx, cells[i].cy)) {
			free(cells[i].file);
			memset(&cells[i], 0, sizeof(cells[i]));
		}
	world_changed = 1;
}

void navmesh_update(float x, float z) {
	static int last_cx = 0x7fffffff, last_cy = 0x7fffffff;
	if (world_changed)
		world_changed = 0, last_cx = 0x7fffffff;
	if (indoors) {
		last_cx = 0x7fffffff; // re-check the exterior when back outside
		return;
	}
	int pcx = cell_of_x(x), pcy = cell_of_z(z);
	if (pcx == last_cx && pcy == last_cy)
		return;
	last_cx = pcx, last_cy = pcy;
	for (int i = 0; i < MAX_NAV_CELLS; i++) {
		NavCell *c = &cells[i];
		if (c->used && (abs(c->cx - pcx) > NAV_RADIUS + 1 || abs(c->cy - pcy) > NAV_RADIUS + 1)) {
			free(c->file);
			memset(c, 0, sizeof(*c)); // a load in flight is dropped (generation)
		}
	}
	for (int dy = -NAV_RADIUS; dy <= NAV_RADIUS; dy++)
		for (int dx = -NAV_RADIUS; dx <= NAV_RADIUS; dx++) {
			if (find_cell(pcx + dx, pcy + dy))
				continue;
			NavCell *c = NULL;
			for (int i = 0; i < MAX_NAV_CELLS && !c; i++)
				if (!cells[i].used)
					c = &cells[i];
			NavJob *j = c ? calloc(1, sizeof(*j)) : NULL;
			if (!j)
				return;
			c->used = 1, c->state = 0, c->gen = next_gen++;
			c->cx = pcx + dx, c->cy = pcy + dy;
			c->prefix = worlds_prefix(c->cx, c->cy);
			j->base.priority = nav_job_priority;
			j->base.work = nav_job_work, j->base.what = "paths";
			j->base.finish = nav_job_finish;
			j->base.discard = nav_job_discard;
			j->slot = (int)(c - cells);
			j->gen = c->gen;
			j->cx = c->cx, j->cy = c->cy;
			j->prefix = c->prefix;
			stream_submit(&j->base);
		}
}

int navmesh_ready(float x, float z) {
	const NavCell *c = find_cell(cell_of_x(x), cell_of_z(z));
	return c && c->state == 1;
}

// --- Geometry ---------------------------------------------------------------------------

static const float *vert(const NavCell *c, const NavTri *t, int k) {
	return &c->verts[t->v[k] * 3];
}

// Barycentric height of (x, z) on the triangle if it lies inside (with a small margin)
static int tri_height(const NavCell *c, const NavTri *t, float x, float z, float *h) {
	const float *a = vert(c, t, 0), *b = vert(c, t, 1), *p = vert(c, t, 2);
	float den = (b[2] - p[2]) * (a[0] - p[0]) + (p[0] - b[0]) * (a[2] - p[2]);
	if (fabsf(den) < 1e-8f)
		return 0;
	float l0 = ((b[2] - p[2]) * (x - p[0]) + (p[0] - b[0]) * (z - p[2])) / den;
	float l1 = ((p[2] - a[2]) * (x - p[0]) + (a[0] - p[0]) * (z - p[2])) / den;
	float l2 = 1.0f - l0 - l1;
	const float eps = -1e-3f;
	if (l0 < eps || l1 < eps || l2 < eps)
		return 0;
	*h = l0 * a[1] + l1 * b[1] + l2 * p[1];
	return 1;
}

static void centroid(const NavCell *c, const NavTri *t, float out[3]) {
	for (int i = 0; i < 3; i++)
		out[i] = (vert(c, t, 0)[i] + vert(c, t, 1)[i] + vert(c, t, 2)[i]) / 3.0f;
}

// Neighbour across edge k: its cell and triangle, or 0 if none / not loaded
static int neighbour(const NavCell *c, const NavTri *t, int k, NavCell **nc, int *ntri) {
	uint32_t n = t->nbr[k];
	if (n == NO_NEIGHBOUR)
		return 0;
	int dx = (int)((n >> 24) & 3) - 1, dy = (int)((n >> 26) & 3) - 1;
	NavCell *o = (dx || dy) ? loaded_cell(c->cx + dx, c->cy + dy) : (NavCell *)c;
	int tri = (int)(n & 0xFFFFFF);
	if (!o || tri >= o->tri_count)
		return 0;
	*nc = o, *ntri = tri;
	return 1;
}

int navmesh_locate(float x, float y, float z, NavRef *ref, float *height) {
	return navmesh_locate_level(x, y, z, LOCATE_MAX_DY, ref, height);
}

int navmesh_locate_level(float x, float y, float z, float max_dy, NavRef *ref, float *height) {
	const NavCell *c = loaded_cell(cell_of_x(x), cell_of_z(z));
	if (!c)
		return 0;
	int best = -1;
	float best_h = 0.0f, best_d = max_dy;
	for (int i = 0; i < c->tri_count; i++) {
		float h;
		if (tri_height(c, &c->tris[i], x, z, &h) && fabsf(h - y) < best_d)
			best = i, best_h = h, best_d = fabsf(h - y);
	}
	if (best < 0)
		return 0;
	ref->cx = c->cx, ref->cy = c->cy, ref->tri = best;
	*height = best_h;
	return 1;
}

// The closest point to p on segment a-b (x, z)
static void closest_on_segment(const float *a, const float *b, float px, float pz, float out[2]) {
	float ex = b[0] - a[0], ez = b[2] - a[2], len2 = ex * ex + ez * ez;
	float u = len2 > 1e-8f ? ((px - a[0]) * ex + (pz - a[2]) * ez) / len2 : 0.0f;
	u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
	out[0] = a[0] + ex * u, out[1] = a[2] + ez * u;
}

int navmesh_nearest(float x, float y, float z, float max_m, NavRef *ref, float out[3]) {
	return navmesh_nearest_level(x, y, z, max_m, LOCATE_MAX_DY, ref, out);
}

int navmesh_nearest_level(float x, float y, float z, float max_m, float max_dy, NavRef *ref, float out[3]) {
	const NavCell *c = loaded_cell(cell_of_x(x), cell_of_z(z));
	if (!c)
		return 0;
	int best = -1;
	float best_d = max_m * max_m, bx = 0, bz = 0;
	for (int i = 0; i < c->tri_count; i++) {
		const NavTri *t = &c->tris[i];
		float h, cx = x, cz = z;
		if (!tri_height(c, t, x, z, &h)) {
			// Outside it: the nearest point on its edges, nudged a little inside
			float d2 = 1e30f;
			for (int k = 0; k < 3; k++) {
				float q[2];
				closest_on_segment(vert(c, t, k), vert(c, t, (k + 1) % 3), x, z, q);
				float dd = (q[0] - x) * (q[0] - x) + (q[1] - z) * (q[1] - z);
				if (dd < d2)
					d2 = dd, cx = q[0], cz = q[1];
			}
			float m[3];
			centroid(c, t, m);
			cx += (m[0] - cx) * 0.05f, cz += (m[2] - cz) * 0.05f;
			if (!tri_height(c, t, cx, cz, &h))
				continue;
		}
		if (fabsf(h - y) > max_dy)
			continue;
		float d = (cx - x) * (cx - x) + (cz - z) * (cz - z);
		if (d < best_d)
			best = i, best_d = d, bx = cx, bz = cz;
	}
	if (best < 0)
		return 0;
	ref->cx = c->cx, ref->cy = c->cy, ref->tri = best;
	float h;
	tri_height(c, &c->tris[best], bx, bz, &h);
	out[0] = bx, out[1] = h, out[2] = bz;
	return 1;
}

int navmesh_track(NavRef *ref, float x, float z, float *height) {
	NavCell *c = ref->tri >= 0 ? loaded_cell(ref->cx, ref->cy) : NULL;
	if (!c || ref->tri >= c->tri_count)
		return 0;
	const NavTri *t = &c->tris[ref->tri];
	if (tri_height(c, t, x, z, height))
		return 1;
	// Neighbours, then theirs
	for (int k = 0; k < 3; k++) {
		NavCell *n1;
		int t1;
		if (!neighbour(c, t, k, &n1, &t1))
			continue;
		if (tri_height(n1, &n1->tris[t1], x, z, height)) {
			ref->cx = n1->cx, ref->cy = n1->cy, ref->tri = t1;
			return 1;
		}
		for (int k2 = 0; k2 < 3; k2++) {
			NavCell *n2;
			int t2;
			if (neighbour(n1, &n1->tris[t1], k2, &n2, &t2) && tri_height(n2, &n2->tris[t2], x, z, height)) {
				ref->cx = n2->cx, ref->cy = n2->cy, ref->tri = t2;
				return 1;
			}
		}
	}
	float h0 = *height;
	return navmesh_locate(x, h0, z, ref, height);
}

// --- Path finding (A* over triangles, then the funnel algorithm) -------------------------

typedef struct {
	NavCell *cell;
	int tri;
	int parent; // node index, -1 for the start
	int via;    // edge of the parent's triangle that leads here
	float g, f;
	int closed;
} Node;

static Node nodes[SEARCH_NODES];
static int hash_table[HASH_SIZE]; // node index + 1, 0 = empty
static int heap[SEARCH_NODES], heap_n;

static unsigned node_hash(const NavCell *c, int tri) {
	unsigned h = (unsigned)(c - cells) * 2654435761u ^ (unsigned)tri * 40503u;
	return (h ^ (h >> 15)) & (HASH_SIZE - 1);
}

static int node_find(NavCell *c, int tri, int *slot) {
	unsigned h = node_hash(c, tri);
	while (hash_table[h]) {
		Node *n = &nodes[hash_table[h] - 1];
		if (n->cell == c && n->tri == tri)
			return hash_table[h] - 1;
		h = (h + 1) & (HASH_SIZE - 1);
	}
	*slot = (int)h;
	return -1;
}

static void heap_push(int i) {
	int k = heap_n++;
	heap[k] = i;
	while (k > 0) {
		int p = (k - 1) / 2;
		if (nodes[heap[p]].f <= nodes[heap[k]].f)
			break;
		int t = heap[p];
		heap[p] = heap[k], heap[k] = t, k = p;
	}
}

static int heap_pop(void) {
	int top = heap[0];
	heap[0] = heap[--heap_n];
	int k = 0;
	for (;;) {
		int l = 2 * k + 1, r = l + 1, m = k;
		if (l < heap_n && nodes[heap[l]].f < nodes[heap[m]].f)
			m = l;
		if (r < heap_n && nodes[heap[r]].f < nodes[heap[m]].f)
			m = r;
		if (m == k)
			break;
		int t = heap[m];
		heap[m] = heap[k], heap[k] = t, k = m;
	}
	return top;
}

static float dist3(const float a[3], const float b[3]) {
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

// 2D cross product in the XZ plane
static float cross_xz(const float *o, const float *a, const float *b) {
	return (a[0] - o[0]) * (b[2] - o[2]) - (a[2] - o[2]) * (b[0] - o[0]);
}

static int same_xz(const float *a, const float *b) {
	return fabsf(a[0] - b[0]) < 1e-4f && fabsf(a[2] - b[2]) < 1e-4f;
}

// Simple stupid funnel (Mononen) over portals given as left/right point pairs, in XZ.
// portals[0] and portals[n - 1] are the start and the goal (left == right).
static int string_pull(const float (*left)[3], const float (*right)[3], int n, float *out, int max_out) {
	int count = 0;
	float apex[3], pl[3], pr[3];
	int apex_i = 0, left_i = 0, right_i = 0;
	memcpy(apex, left[0], sizeof(apex));
	memcpy(pl, left[0], sizeof(pl));
	memcpy(pr, right[0], sizeof(pr));
	for (int i = 1; i < n && count < max_out; i++) {
		// Right side: tighten while the new right is left of the current right
		if (cross_xz(apex, pr, right[i]) >= 0.0f) {
			if (same_xz(apex, pr) || cross_xz(apex, pl, right[i]) < 0.0f) {
				memcpy(pr, right[i], sizeof(pr));
				right_i = i;
			} else { // right crossed over left: left is a corner
				memcpy(&out[count++ * 3], pl, sizeof(pl));
				memcpy(apex, pl, sizeof(apex));
				apex_i = left_i;
				memcpy(pr, apex, sizeof(pr));
				right_i = apex_i;
				i = apex_i;
				continue;
			}
		}
		if (cross_xz(apex, pl, left[i]) <= 0.0f) {
			if (same_xz(apex, pl) || cross_xz(apex, pr, left[i]) > 0.0f) {
				memcpy(pl, left[i], sizeof(pl));
				left_i = i;
			} else {
				memcpy(&out[count++ * 3], pr, sizeof(pr));
				memcpy(apex, pr, sizeof(apex));
				apex_i = right_i;
				memcpy(pl, apex, sizeof(pl));
				left_i = apex_i;
				i = apex_i;
				continue;
			}
		}
	}
	if (count < max_out)
		memcpy(&out[count++ * 3], left[n - 1], 3 * sizeof(float));
	return count;
}

#define MAX_CORRIDOR 512

int navmesh_indoors(void) {
	return indoors;
}

int navmesh_path(NavRef from, const float f[3], NavRef to, const float t[3], float *points, int max_points) {
	NavCell *sc = loaded_cell(from.cx, from.cy), *gc = loaded_cell(to.cx, to.cy);
	if (!sc || !gc || from.tri < 0 || to.tri < 0 || from.tri >= sc->tri_count || to.tri >= gc->tri_count ||
	    max_points < 1)
		return 0;
	if (sc == gc && from.tri == to.tri) {
		memcpy(points, t, 3 * sizeof(float));
		return 1;
	}
	memset(hash_table, 0, sizeof(hash_table));
	heap_n = 0;
	int count = 0, slot = 0, found = -1;
	node_find(sc, from.tri, &slot);
	nodes[0] = (Node){sc, from.tri, -1, -1, 0.0f, dist3(f, t), 0};
	hash_table[slot] = 1;
	count = 1;
	heap_push(0);
	while (heap_n) {
		int cur = heap_pop();
		Node *n = &nodes[cur];
		if (n->closed)
			continue;
		n->closed = 1;
		if (n->cell == gc && n->tri == to.tri) {
			found = cur;
			break;
		}
		const NavTri *tri = &n->cell->tris[n->tri];
		float here[3];
		centroid(n->cell, tri, here);
		for (int k = 0; k < 3; k++) {
			NavCell *nc;
			int nt;
			if (!neighbour(n->cell, tri, k, &nc, &nt))
				continue;
			float there[3];
			if (nc == gc && nt == to.tri)
				memcpy(there, t, sizeof(there));
			else
				centroid(nc, &nc->tris[nt], there);
			float g = n->g + dist3(here, there);
			int idx = node_find(nc, nt, &slot);
			if (idx < 0) {
				if (count == SEARCH_NODES)
					continue;
				idx = count++;
				hash_table[slot] = idx + 1;
				nodes[idx] = (Node){nc, nt, cur, k, g, g + dist3(there, t), 0};
				heap_push(idx);
			} else if (!nodes[idx].closed && g < nodes[idx].g) {
				nodes[idx].g = g, nodes[idx].f = g + dist3(there, t);
				nodes[idx].parent = cur, nodes[idx].via = k;
				heap_push(idx);
			}
			n = &nodes[cur]; // (nodes is static; n stays valid)
		}
	}
	if (found < 0)
		return 0;

	// Corridor (start -> goal), then portals between consecutive triangles
	static int corridor[MAX_CORRIDOR];
	int len = 0;
	for (int i = found; i >= 0 && len < MAX_CORRIDOR; i = nodes[i].parent)
		corridor[len++] = i;
	if (len == MAX_CORRIDOR)
		return 0;
	static float pleft[MAX_CORRIDOR + 1][3], pright[MAX_CORRIDOR + 1][3];
	int np = 0;
	memcpy(pleft[np], f, sizeof(pleft[0]));
	memcpy(pright[np], f, sizeof(pright[0]));
	np++;
	for (int i = len - 1; i > 0; i--) {
		const Node *a = &nodes[corridor[i]], *b = &nodes[corridor[i - 1]];
		const NavTri *ta = &a->cell->tris[a->tri];
		int k = b->via;
		const float *e0 = vert(a->cell, ta, k), *e1 = vert(a->cell, ta, (k + 1) % 3);
		float ca[3], cb[3];
		centroid(a->cell, ta, ca);
		centroid(b->cell, &b->cell->tris[b->tri], cb);
		// Which end is on the left of the direction of travel (same convention as string_pull)
		int e0_left = cross_xz(ca, cb, e0) > 0.0f;
		const float *l = e0_left ? e0 : e1, *r = e0_left ? e1 : e0;
		// Keep a body's width from the portal ends
		float dx = r[0] - l[0], dz = r[2] - l[2];
		float elen = sqrtf(dx * dx + dz * dz);
		float shrink = elen > 0.0f ? fminf(AGENT_RADIUS_M, elen * 0.4f) / elen : 0.0f;
		for (int c = 0; c < 3; c++) {
			pleft[np][c] = l[c] + (r[c] - l[c]) * shrink;
			pright[np][c] = r[c] - (r[c] - l[c]) * shrink;
		}
		np++;
	}
	memcpy(pleft[np], t, sizeof(pleft[0]));
	memcpy(pright[np], t, sizeof(pright[0]));
	np++;
	return string_pull((const float(*)[3])pleft, (const float(*)[3])pright, np, points, max_points);
}

int navmesh_interior_mesh(const float **verts, int *vert_count, const uint16_t **tri_verts, int *tri_stride) {
	if (!indoors || !interior.used || interior.state != 1 || !interior.tri_count)
		return 0;
	*verts = interior.verts, *vert_count = interior.vert_count;
	*tri_verts = interior.tris[0].v, *tri_stride = (int)(sizeof(NavTri) / sizeof(uint16_t));
	return interior.tri_count;
}
