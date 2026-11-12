#include "objects.h"
#include "worlds.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include <psp2/kernel/clib.h>

#include "atmosphere.h"
#include "collision.h"
#include "fileio.h"
#include "gamelog.h"
#include "lighting.h"
#include "shader.h"
#include "sktex.h"
#include "stream.h"
#include "terrain.h"
#include "water.h"

#define SKOB_VERSION 1
#define MAX_CELLS 128 // (2 * (HUGE_RADIUS + 1) + 1)^2 = 121
#define MAX_TEXTURES 1024
#define LARGE_RADIUS_M 6.0f  // bounding radius above which objects draw to LARGE_RADIUS (houses: ~117 m)
#define HUGE_RADIUS_M 25.0f  // ...and to HUGE_RADIUS
#define WATER_TEXTURE "_water"
#define SKCO_VERSION 1

enum { KIND_OPAQUE, KIND_ALPHA, KIND_WATER, KIND_COUNT };
enum { GROUP_DETAIL, GROUP_LARGE, GROUP_HUGE, GROUP_COUNT };

static void init_object_shader(void);

// Object vertices as the converter bakes them (converter/bake_cells.py): positions and
// UVs quantized to 16 bits around each batch's own centre and scale, light baked into
// the colour. 16 bytes; besides memory, this matters under Vita3K on macOS: without
// memory mapping it copies every draw's vertices into a 64 MB ring that wraps without
// waiting for the frames still in flight, and once a frame needs more than about a
// third of it, geometry being drawn gets overwritten (exploding polygons).
typedef ObjectVertex PackedVertex; // (objects.h)

// World switches (converter/esm.py SWITCHES: Helgen before and after Alduin...): which are on;
// a tag is a switch's number, + SWITCH_ON_SHOWS if it's there once that's on (else until then)
#define SWITCH_ON_SHOWS 0x80
#define HELGEN_DEBRIS_H 4.0f // (the attack's fallen things lower than this: no collision)
static uint32_t switches_on;
#define HELGEN_X0 150.0f // (Helgen: x, z bounds)
#define HELGEN_X1 320.0f
#define HELGEN_Z0 1060.0f
#define HELGEN_Z1 1230.0f

static int switch_shown(int tag) {
	if (!tag)
		return 1;
	const int on = (switches_on >> (tag & 31)) & 1;
	return (tag & SWITCH_ON_SHOWS) ? on : !on;
}
static const float FLOW_TINT[3] = {0.68f, 0.8f, 0.84f}; // flowing water's colour, near the still water's

typedef struct {
	uint16_t model;
	uint16_t reserved;
	float m[9];
	float t[3];
} Entry; // matches .skobj entries (52 bytes)

typedef struct {
	GLuint tex, vbo, ibo;
	int kind, index_count, vertex_count;
	int flow;    // waterfall/stream surface: texture scrolls (Skyrim animates these UVs)
	int two_sided; // Skyrim's double-sided material flag (foliage, banners): no culling
	float pos[4]; // world = packed position (normalized, -1..1) * pos[3] + pos[0..2]
	float uv[3];  // uv = packed uv (normalized) * uv[2] + uv[0..1]
	float lo[2], hi[2];  // its geometry's x, z bounds (each batch is a quarter cell or less)
	unsigned in_budget;  // the frame it was last let into the frame budget
	float *water; // KIND_WATER: its triangles in world space (water_count * 9), for swimming
	int water_count;
	float wmin[2], wmax[2]; // their x, z bounds
	int switch_tag; // shown by a world switch (objects_set_switch; 0 always)
} Batch;

typedef struct {
	Batch *batches;
	int count;
	int state;    // VAR_NONE / VAR_QUEUED (being built on the stream thread) / VAR_READY
	unsigned job; // id of the queued build (results of cancelled builds are dropped)
	unsigned bytes;      // its vertices and indices (the frame budget)
	unsigned in_budget;  // the frame it was last let into the frame budget
	int has_water;       // water or flowing water (rivers, waterfalls): kept in the budget first
	float lo[2], hi[2];  // its geometry's x, z bounds (objects overhang their cells, mountains far)
} Group;
enum { VAR_NONE, VAR_QUEUED, VAR_READY };

// Detail levels. Each size group of a cell keeps one Group per level it needs, and the
// object shader dissolves neighbouring levels into each other by distance (see
// variant_band), so nothing pops as the player walks.
enum { LOD_FULL, LOD_MID, LOD_FAR, LOD_COUNT };
static float now_s; // set each frame by objects_set_time
#define FLOW_TILES_PER_S 0.9f // waterfall texture scroll speed

enum { FX_FIRE, FX_GLOW };

typedef struct {
	int kind;
	float x, y, z, w, h; // base position (world), size in meters
	float seed;
} FxPoint;

typedef struct {
	int used, cx, cy;
	unsigned gen;      // identifies this use of the slot
	const char *prefix; // the world its files are from (worlds_prefix)
	int entries_state; // 0 loading (streamed), 1 loaded
	Entry *entries;
	int entry_count;
	Group var[GROUP_COUNT][LOD_COUNT]; // per size group, per detail level
	FxPoint *fx; // fires/glows of this cell's objects, built with the detail group
	int fx_count;
} CellObjects;

typedef struct {
	char name[256];
	FxPoint *fx;  // model-space fire/glow points from fx.txt
	int fx_count;
	float *col_tris; // Skyrim collision (.skcol), count * 9 floats, model space
	int col_count;
	float col_min[3], col_max[3]; // model-space bounds of col_tris
	int col_state;   // 0 = not loaded, 1 = loaded (possibly empty)
	int fish;        // Skyrim's ambient salmon: animated by fish.c, not baked, no collision
} Model;

static char pack_dir[128];
static Model *models;
static int model_count;
static CellObjects cells[MAX_CELLS];
static struct {
	char name[128];
	GLuint tex;
	uint8_t pinned; // asked for by name (objects_texture: people, doors, loot keep it): never freed
} textures[MAX_TEXTURES];
static volatile int texture_count;
static GLuint fx_fire_tex, fx_glow_tex;
static GLuint sprite_tex[SPRITE_KINDS];

// --- Caches ------------------------------------------------------------------------

// Any thread. The table is append-only: an entry is complete before the count covers it.
static GLuint texture_find(const char *name) {
	int n = texture_count;
	__sync_synchronize();
	for (int i = 0; i < n; i++)
		if (!strcmp(textures[i].name, name))
			return textures[i].tex;
	return 0;
}

// Render thread: the texture for name, uploading data (its file, read by the stream
// thread; may be NULL) if it isn't cached yet.
static GLuint world_texture(const char *name);
static int tex_cap; // textures load no larger than this (their smaller mips; 0: as they are)

void objects_set_texture_cap(int max_dim) {
	tex_cap = max_dim;
}

static GLuint texture_get(const char *name, const void *data, long size) {
	GLuint tex = texture_find(name);
	if (tex)
		return tex;
	tex = data ? sktex_upload(data, size, tex_cap, name) : 0; // (the performance setting's largest)
	if (!tex)
		tex = sktex_white();
	if (texture_count < MAX_TEXTURES) {
		snprintf(textures[texture_count].name, sizeof(textures[0].name), "%s", name);
		textures[texture_count].tex = tex;
		__sync_synchronize();
		texture_count++;
	}
	return tex;
}

static float entry_scale(const Entry *e) {
	return sqrtf(e->m[0] * e->m[0] + e->m[3] * e->m[3] + e->m[6] * e->m[6]);
}

// --- Batch loading (stream thread) ------------------------------------------------
// The converter bakes each cell's objects of one size group at one detail level into
// per-texture batches (converter/bake_cells.py, .skbat). The stream thread reads the
// file and the textures it needs; the render thread then uploads one batch at a time
// within its frame budget.

#define SKBT_VERSION 5 // (4: before each batch's world switch; still read)
#define FINE_UNITS (32.0f * 32767.0f) // a batch's pos[3] over this: the shader's fine step

typedef struct {
	char tex[128]; // texture name ("" for water)
	int kind, flow, two_sided;
	float pos[4], uv[3];
	int vcount, icount, switch_tag;
	const PackedVertex *pv; // into the job's file buffer
	const uint16_t *idx;
	void *tex_data; // texture file, if it wasn't cached when the job ran
	long tex_size;
} BatchSource;

static int gpu_out_of_memory; // a buffer allocation failed since the last report

// Render thread
// A CPU copy of a water surface (rivers, ponds: the few water batches), so the player can
// tell how deep the water is (objects_water_at)
static void keep_water(Batch *batch, const BatchSource *b) {
	int n = b->icount / 3;
	float *w = malloc(n * 9 * sizeof(float));
	if (!w)
		return;
	batch->wmin[0] = batch->wmin[1] = INFINITY;
	batch->wmax[0] = batch->wmax[1] = -INFINITY;
	const float s = b->pos[3] / 32767.0f;
	for (int i = 0; i < n * 3; i++) {
		const PackedVertex *v = &b->pv[b->idx[i]];
		float *o = &w[i * 3];
		o[0] = v->x * s + b->pos[0], o[1] = v->y * s + b->pos[1], o[2] = v->z * s + b->pos[2];
		batch->wmin[0] = fminf(batch->wmin[0], o[0]), batch->wmax[0] = fmaxf(batch->wmax[0], o[0]);
		batch->wmin[1] = fminf(batch->wmin[1], o[2]), batch->wmax[1] = fmaxf(batch->wmax[1], o[2]);
	}
	batch->water = w;
	batch->water_count = n;
}

static void push_batch(Group *g, const BatchSource *b, GLuint tex) {
	if (!b->icount)
		return;
	Batch batch = {tex, 0, 0, b->kind, b->icount, b->vcount, b->flow, b->two_sided,
	               {b->pos[0], b->pos[1], b->pos[2], b->pos[3]}, {b->uv[0], b->uv[1], b->uv[2]}};
	batch.switch_tag = b->switch_tag;
	while (glGetError() != GL_NO_ERROR)
		;
	if (b->flow) { // (streams and waterfalls: toned toward the still water's grey-blue, a little brighter)
		PackedVertex *pv = (PackedVertex *)b->pv; // (the job's own buffer)
		for (int v = 0; v < b->vcount; v++)
			pv[v].r = (uint8_t)(pv[v].r * FLOW_TINT[0]), pv[v].g = (uint8_t)(pv[v].g * FLOW_TINT[1]),
			pv[v].b = (uint8_t)(pv[v].b * FLOW_TINT[2]);
	}
	glGenBuffers(1, &batch.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
	glBufferData(GL_ARRAY_BUFFER, b->vcount * sizeof(PackedVertex), b->pv, GL_STATIC_DRAW);
	glGenBuffers(1, &batch.ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, b->icount * sizeof(uint16_t), b->idx, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	// vitaGL leaves a buffer without storage when GPU memory runs out; drawing it reads
	// garbage (exploded, rainbow triangles), so such batches are dropped.
	if (glGetError() == GL_OUT_OF_MEMORY) {
		glDeleteBuffers(1, &batch.vbo);
		glDeleteBuffers(1, &batch.ibo);
		gpu_out_of_memory = 1;
		return;
	}
	if (b->kind == KIND_WATER)
		keep_water(&batch, b);
	// Its real x, z bounds (from the vertices: pos[3] is only an upper bound)
	{
		const float sc = b->pos[3] / 32767.0f;
		float lx = 1e30f, hx = -1e30f, lz = 1e30f, hz = -1e30f;
		for (unsigned v = 0; v < (unsigned)b->vcount; v++) {
			float px = b->pv[v].x * sc, pz = b->pv[v].z * sc;
			lx = fminf(lx, px), hx = fmaxf(hx, px), lz = fminf(lz, pz), hz = fmaxf(hz, pz);
		}
		batch.lo[0] = b->pos[0] + lx - 0.05f, batch.hi[0] = b->pos[0] + hx + 0.05f;
		batch.lo[1] = b->pos[2] + lz - 0.05f, batch.hi[1] = b->pos[2] + hz + 0.05f;
	}
	const float x0 = batch.lo[0], x1 = batch.hi[0], z0 = batch.lo[1], z1 = batch.hi[1];
	if (!g->count)
		g->lo[0] = x0, g->hi[0] = x1, g->lo[1] = z0, g->hi[1] = z1;
	g->lo[0] = fminf(g->lo[0], x0), g->hi[0] = fmaxf(g->hi[0], x1);
	g->lo[1] = fminf(g->lo[1], z0), g->hi[1] = fmaxf(g->hi[1], z1);
	g->bytes += b->vcount * sizeof(PackedVertex) + b->icount * sizeof(uint16_t);
	g->has_water |= b->kind == KIND_WATER || b->flow;
	g->batches = realloc(g->batches, (g->count + 1) * sizeof(Batch));
	g->batches[g->count++] = batch;
}

// Parses a .skbat file (see converter/bake_cells.py); the sources point into buf.
// Returns the batch count, or -1 if the file is malformed.
static int parse_batches(const uint8_t *buf, long size, BatchSource **out) {
	*out = NULL;
	uint16_t version, count;
	if (size < 8 || memcmp(buf, "SKBT", 4))
		return -1;
	memcpy(&version, buf + 4, 2);
	memcpy(&count, buf + 6, 2);
	if (version != SKBT_VERSION && version != 4)
		return -1;
	BatchSource *b = calloc(count ? count : 1, sizeof(BatchSource));
	if (!b)
		return -1;
	long p = 8;
	for (int i = 0; i < count; i++) {
		BatchSource *s = &b[i];
		if (p + 4 > size)
			goto bad;
		int name_len = buf[p + 2];
		s->kind = buf[p], s->flow = buf[p + 1], s->two_sided = buf[p + 3];
		p += 4;
		if (s->kind < 0 || s->kind >= KIND_COUNT || name_len >= (int)sizeof(s->tex) || p + name_len + 36 > size)
			goto bad;
		memcpy(s->tex, buf + p, name_len);
		p += (name_len + 3) & ~3;
		if (version >= 5) {
			uint16_t sw;
			memcpy(&sw, buf + p, 2);
			s->switch_tag = sw, p += 4;
		}
		uint32_t vc, ic;
		memcpy(s->pos, buf + p, 16);
		memcpy(s->uv, buf + p + 16, 12);
		memcpy(&vc, buf + p + 28, 4);
		memcpy(&ic, buf + p + 32, 4);
		p += 36;
		long vbytes = (long)vc * (long)sizeof(PackedVertex), ibytes = (long)ic * 2;
		if (vc > 65535 || ic > 0x1000000 || p + vbytes + ibytes > size)
			goto bad;
		s->vcount = (int)vc, s->icount = (int)ic;
		s->pv = (const PackedVertex *)(buf + p);
		p += vbytes;
		s->idx = (const uint16_t *)(buf + p);
		for (uint32_t k = 0; k < ic; k++)
			if (s->idx[k] >= vc)
				goto bad;
		p += (ibytes + 3) & ~3;
	}
	*out = b;
	return count;
bad:
	free(b);
	return -1;
}

static FxPoint *collect_fx(const Entry *entries, int entry_count, int *out_count) {
	int total = 0;
	for (int i = 0; i < entry_count; i++)
		if (entries[i].model < model_count)
			total += models[entries[i].model].fx_count;
	*out_count = 0;
	FxPoint *fx = total ? malloc(total * sizeof(FxPoint)) : NULL;
	for (int i = 0; i < entry_count && fx; i++) {
		const Entry *e = &entries[i];
		if (e->model >= model_count || !switch_shown(e->reserved))
			continue;
		const Model *m = &models[e->model];
		float scale = entry_scale(e);
		for (int k = 0; k < m->fx_count; k++) {
			const FxPoint *p = &m->fx[k];
			FxPoint *o = &fx[(*out_count)++];
			o->kind = p->kind;
			o->x = e->m[0] * p->x + e->m[1] * p->y + e->m[2] * p->z + e->t[0];
			o->y = e->m[3] * p->x + e->m[4] * p->y + e->m[5] * p->z + e->t[1];
			o->z = e->m[6] * p->x + e->m[7] * p->y + e->m[8] * p->z + e->t[2];
			o->w = p->w * scale;
			o->h = p->h * scale;
			o->seed = fmodf(fabsf(o->x * 12.9898f + o->z * 78.233f), 6.2831f);
		}
	}
	return fx;
}

static CellObjects *find_cell(int cx, int cy);
static int variant_radius(int g, int l);
static int request_radius(int g, int l);
static volatile int stream_pcx, stream_pcy; // player cell, for the stream thread to skip stale work
static unsigned next_job = 1;

typedef struct {
	StreamJob base;
	int slot, cx, cy;
	unsigned gen, job;
	const char *prefix;
	int group, lod;
	Entry *entries; // copy of the cell's placements
	int entry_count;
	int skipped;    // the player moved away before the job ran
	uint8_t *file;  // the .skbat file, which the sources point into
	BatchSource *src;
	int src_count;
	FxPoint *fx;
	int fx_count;
	int next;       // render thread: next builder to upload
	Group out;      // render thread: batches uploaded so far
} GroupJob;

static void group_job_work(StreamJob *base) {
	GroupJob *j = (GroupJob *)base;
	int ring = abs(j->cx - stream_pcx) > abs(j->cy - stream_pcy) ? abs(j->cx - stream_pcx) : abs(j->cy - stream_pcy);
	if (ring > request_radius(j->group, j->lod) + 1) {
		j->skipped = 1;
		return;
	}
	if (j->group == GROUP_DETAIL && j->lod == LOD_FULL)
		j->fx = collect_fx(j->entries, j->entry_count, &j->fx_count);
	char path[420];
	snprintf(path, sizeof(path), "%sobjects/baked/%s_%d_%d_%d_%d.skbat", pack_dir, j->prefix, j->cx, j->cy,
	         j->group, j->lod);
	long size = 0;
	j->file = file_read_all(path, &size);
	if (!j->file)
		return; // nothing of this size group in the cell
	j->src_count = parse_batches(j->file, size, &j->src);
	if (j->src_count < 0) {
		sceClibPrintf("objects: bad file %s\n", path);
		j->src_count = 0;
		return;
	}
	// Read the files of textures the render thread doesn't have yet (once per name)
	for (int i = 0; i < j->src_count; i++) {
		BatchSource *b = &j->src[i];
		if (b->kind == KIND_WATER || texture_find(b->tex))
			continue;
		int seen = 0;
		for (int k = 0; k < i && !seen; k++)
			seen = j->src[k].tex_data && !strcmp(j->src[k].tex, b->tex);
		if (seen)
			continue;
		snprintf(path, sizeof(path), "%stextures/%s.sktex", pack_dir, b->tex);
		b->tex_data = sktex_read(path, &b->tex_size);
	}
}

static int group_job_wanted(const GroupJob *j, CellObjects **out) {
	CellObjects *c = &cells[j->slot];
	*out = c;
	return !j->skipped && c->used && c->gen == j->gen && c->var[j->group][j->lod].state == VAR_QUEUED &&
	       c->var[j->group][j->lod].job == j->job;
}

static void free_group(Group *g);

static int group_job_finish(StreamJob *base, unsigned long long deadline) {
	GroupJob *j = (GroupJob *)base;
	CellObjects *c;
	if (!group_job_wanted(j, &c)) {
		free_group(&j->out);
		return 1;
	}
	while (j->next < j->src_count) {
		const BatchSource *b = &j->src[j->next];
		GLuint tex = 0;
		if (b->kind != KIND_WATER) {
			// tex_data sits on the first builder of each name; later ones find it cached
			tex = texture_get(b->tex, b->tex_data, b->tex_size);
		}
		push_batch(&j->out, b, tex);
		j->next++;
		if (j->next < j->src_count && stream_now_us() >= deadline)
			return 0; // continue next frame
	}
	Group *g = &c->var[j->group][j->lod];
	g->batches = j->out.batches;
	g->count = j->out.count;
	g->bytes = j->out.bytes;
	g->has_water = j->out.has_water;
	memcpy(g->lo, j->out.lo, sizeof(g->lo));
	memcpy(g->hi, j->out.hi, sizeof(g->hi));
	g->state = VAR_READY;
	memset(&j->out, 0, sizeof(j->out));
	if (j->fx) {
		free(c->fx);
		c->fx = j->fx;
		c->fx_count = j->fx_count;
		j->fx = NULL;
	}
	return 1;
}

static void group_job_discard(StreamJob *base) {
	GroupJob *j = (GroupJob *)base;
	for (int i = 0; i < j->src_count; i++)
		free(j->src[i].tex_data);
	free(j->src);
	free(j->file);
	free(j->entries);
	free(j->fx);
	free(j);
}

static int group_job_priority(StreamJob *base) {
	const GroupJob *j = (const GroupJob *)base;
	int dx = abs(j->cx - stream_pcx), dy = abs(j->cy - stream_pcy), ring = dx > dy ? dx : dy;
	return STREAM_PRIORITY(ring - variant_radius(j->group, j->lod), ring);
}

static void submit_group_job(CellObjects *c, int group, int lod) {
	GroupJob *j = calloc(1, sizeof(*j));
	if (!j)
		return;
	if (group == GROUP_DETAIL && lod == LOD_FULL && c->entry_count) { // for fire/glow points
		j->entries = malloc(c->entry_count * sizeof(Entry));
		if (!j->entries) {
			free(j);
			return;
		}
		memcpy(j->entries, c->entries, c->entry_count * sizeof(Entry));
		j->entry_count = c->entry_count;
	}
	j->base.priority = group_job_priority;
	j->base.work = group_job_work, j->base.what = "objects";
	j->base.finish = group_job_finish;
	j->base.discard = group_job_discard;
	j->slot = (int)(c - cells);
	j->cx = c->cx, j->cy = c->cy;
	j->gen = c->gen;
	j->prefix = c->prefix;
	j->group = group, j->lod = lod;
	j->job = next_job++;
	c->var[group][lod].state = VAR_QUEUED;
	c->var[group][lod].job = j->job;
	stream_submit(&j->base);
}

// Skyrim's own collision for a model (.skcol). Models without one (plants, grass,
// small clutter) don't block the player, as in Skyrim.
static void load_model_collision(Model *m) {
	m->col_state = 1;
	char path[420];
	snprintf(path, sizeof(path), "%smeshes/%s.skcol", pack_dir, m->name);
	long size = 0;
	uint8_t *buf = file_read_all(path, &size);
	if (!buf)
		return;
	uint32_t count = 0;
	if (size >= 12)
		memcpy(&count, buf + 8, 4);
	if (size >= 12 && !memcmp(buf, "SKCO", 4) && *(uint16_t *)(buf + 4) == SKCO_VERSION && count < 1000000 &&
	    size >= 12 + (long)count * 9 * (long)sizeof(float)) {
		m->col_tris = malloc((count ? count : 1) * 9 * sizeof(float));
		if (m->col_tris) {
			memcpy(m->col_tris, buf + 12, count * 9 * sizeof(float));
			m->col_count = (int)count;
		}
	}
	free(buf);
	for (int k = 0; k < 3; k++)
		m->col_min[k] = INFINITY, m->col_max[k] = -INFINITY;
	for (int i = 0; i < m->col_count * 3; i++)
		for (int k = 0; k < 3; k++) {
			m->col_min[k] = fminf(m->col_min[k], m->col_tris[i * 3 + k]);
			m->col_max[k] = fmaxf(m->col_max[k], m->col_tris[i * 3 + k]);
		}
}

// Collision objects can reach far beyond the cell they are placed in (mountain cliffs
// span up to ~140 m), so the collision region around the player gathers from every
// object within this many cells whose collision bounds overlap the region.
#define COLLISION_SOURCE_RADIUS 3
#define COLLISION_REGION_MARGIN_M 4.0f
#define INTERIOR_COLLISION_HALF_M 40.0f // an interior's collision: this far around the player...
#define INTERIOR_COLLISION_MOVE_M 14.0f // ...built again after moving this far

typedef struct {
	StreamJob base;
	float rx0, rz0, rx1, rz1;
	Entry *entries; // copies of the source cells' placements
	int entry_count;
	CollisionRegion *region;
	unsigned interior; // an interior's region (its generation), 0: the exterior's
} CollisionJob;

static void collision_job_work(StreamJob *base) {
	CollisionJob *j = (CollisionJob *)base;
	const float rx0 = j->rx0, rx1 = j->rx1, rz0 = j->rz0, rz1 = j->rz1;
	int cap = 4096, n = 0;
	float *tris = malloc(cap * 9 * sizeof(float));
	int *tags = malloc(cap * sizeof(int)); // model index per triangle, for diagnostics
	for (int i = 0; i < j->entry_count && tris && tags; i++) {
		const Entry *e = &j->entries[i];
		stream_progress++;
		if (e->model >= model_count || !switch_shown(e->reserved))
			continue;
		Model *m = &models[e->model];
		if ((e->reserved & SWITCH_ON_SHOWS) && (strstr(m->name, "rubble") || strstr(m->name, "collisionmarker")))
			continue; // (the attack's rubble and its blockers: no collision - the way through the town was shut)
		if (strstr(m->name, "collisionmarker") && e->t[0] > HELGEN_X0 && e->t[0] < HELGEN_X1 && e->t[2] > HELGEN_Z0 &&
		    e->t[2] < HELGEN_Z1)
			continue; // (Helgen's invisible walls, its scripts' for its states: they shut Hadvar's way out)
		if (m->fish)
			continue; // (Skyrim gives them a little box: it would block you in the river)
		if (!m->col_state)
			load_model_collision(m); // stream thread only
		if (!m->col_count)
			continue;
		if ((e->reserved & SWITCH_ON_SHOWS) && m->col_max[1] - m->col_min[1] < HELGEN_DEBRIS_H &&
		    e->t[0] > HELGEN_X0 && e->t[0] < HELGEN_X1 && e->t[2] > HELGEN_Z0 && e->t[2] < HELGEN_Z1)
			continue; // (what the attack brings down - beams, stones, carts - low: climbed over, not a wall across the way)
		// World-space bounds of the model's collision (8 transformed corners)
		float lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
		for (int k = 0; k < 8; k++) {
			float p[3] = {k & 1 ? m->col_max[0] : m->col_min[0], k & 2 ? m->col_max[1] : m->col_min[1],
			              k & 4 ? m->col_max[2] : m->col_min[2]};
			float wx = e->m[0] * p[0] + e->m[1] * p[1] + e->m[2] * p[2] + e->t[0];
			float wz = e->m[6] * p[0] + e->m[7] * p[1] + e->m[8] * p[2] + e->t[2];
			lo[0] = fminf(lo[0], wx), hi[0] = fmaxf(hi[0], wx);
			lo[1] = fminf(lo[1], wz), hi[1] = fmaxf(hi[1], wz);
		}
		if (hi[0] < rx0 || lo[0] > rx1 || hi[1] < rz0 || lo[1] > rz1)
			continue;
		for (int t = 0; t < m->col_count; t++) {
			float w[9];
			for (int v = 0; v < 3; v++) {
				const float *p = &m->col_tris[t * 9 + v * 3];
				w[v * 3 + 0] = e->m[0] * p[0] + e->m[1] * p[1] + e->m[2] * p[2] + e->t[0];
				w[v * 3 + 1] = e->m[3] * p[0] + e->m[4] * p[1] + e->m[5] * p[2] + e->t[1];
				w[v * 3 + 2] = e->m[6] * p[0] + e->m[7] * p[1] + e->m[8] * p[2] + e->t[2];
			}
			if (fmaxf(w[0], fmaxf(w[3], w[6])) < rx0 || fminf(w[0], fminf(w[3], w[6])) > rx1 ||
			    fmaxf(w[2], fmaxf(w[5], w[8])) < rz0 || fminf(w[2], fminf(w[5], w[8])) > rz1)
				continue;
			if (n == cap) {
				float *grown = realloc(tris, cap * 2 * 9 * sizeof(float));
				int *grown_tags = grown ? realloc(tags, cap * 2 * sizeof(int)) : NULL;
				if (grown)
					tris = grown;
				if (!grown_tags)
					break;
				tags = grown_tags;
				cap *= 2;
			}
			memcpy(&tris[n * 9], w, sizeof(w));
			tags[n] = e->model;
			n++;
		}
	}
	if (tris && tags)
		j->region = collision_build(rx0, rz0, rx1, rz1, tris, tags, n);
	free(tris);
	free(tags);
}

static int indoors;          // an interior is loaded (objects_interior_*)
static int force_collision;  // rebuild the exterior collision region (back outside)
static unsigned interior_gen; // which interior is loaded (its collision jobs say)

static int collision_job_finish(StreamJob *base, unsigned long long deadline) {
	(void)deadline;
	CollisionJob *j = (CollisionJob *)base;
	if (j->region && (j->interior ? indoors && j->interior == interior_gen : !indoors))
		collision_install(j->region); // jobs finish in order: the newest region wins
	else
		collision_free(j->region);
	j->region = NULL;
	return 1;
}

static void collision_job_discard(StreamJob *base) {
	CollisionJob *j = (CollisionJob *)base;
	collision_free(j->region);
	free(j->entries);
	free(j);
}

// Queues the collision region around cell (pcx, pcy); 0 if a source cell's placements
// are still loading (try again later).
static int collision_job_priority(StreamJob *base) {
	(void)base;
	return STREAM_PRIORITY_URGENT;
}

static int submit_collision_job(int pcx, int pcy) {
	int total = 0;
	for (int dy = -COLLISION_SOURCE_RADIUS; dy <= COLLISION_SOURCE_RADIUS; dy++)
		for (int dx = -COLLISION_SOURCE_RADIUS; dx <= COLLISION_SOURCE_RADIUS; dx++) {
			const CellObjects *c = find_cell(pcx + dx, pcy + dy);
			if (!c || !c->entries_state)
				return 0;
			total += c->entry_count;
		}
	CollisionJob *j = calloc(1, sizeof(*j));
	if (!j)
		return 1;
	j->entries = malloc((total ? total : 1) * sizeof(Entry));
	if (!j->entries) {
		free(j);
		return 1;
	}
	for (int dy = -COLLISION_SOURCE_RADIUS; dy <= COLLISION_SOURCE_RADIUS; dy++)
		for (int dx = -COLLISION_SOURCE_RADIUS; dx <= COLLISION_SOURCE_RADIUS; dx++) {
			const CellObjects *c = find_cell(pcx + dx, pcy + dy);
			memcpy(&j->entries[j->entry_count], c->entries, c->entry_count * sizeof(Entry));
			j->entry_count += c->entry_count;
		}
	j->rx0 = (pcx - 1) * CELL_SIZE_M - COLLISION_REGION_MARGIN_M;
	j->rx1 = (pcx + 2) * CELL_SIZE_M + COLLISION_REGION_MARGIN_M;
	j->rz0 = -(pcy + 2) * CELL_SIZE_M - COLLISION_REGION_MARGIN_M;
	j->rz1 = -(pcy - 1) * CELL_SIZE_M + COLLISION_REGION_MARGIN_M;
	j->base.priority = collision_job_priority;
	j->base.work = collision_job_work, j->base.what = "collision";
	j->base.finish = collision_job_finish;
	j->base.discard = collision_job_discard;
	stream_submit(&j->base);
	return 1;
}

static void free_group(Group *g) {
	for (int i = 0; i < g->count; i++) {
		glDeleteBuffers(1, &g->batches[i].vbo);
		glDeleteBuffers(1, &g->batches[i].ibo);
		free(g->batches[i].water);
	}
	free(g->batches);
	memset(g, 0, sizeof(*g)); // state = VAR_NONE
}

// --- Cells ---------------------------------------------------------------------------

typedef struct {
	StreamJob base;
	int slot, cx, cy;
	unsigned gen;
	const char *prefix;
	Entry *entries;
	int entry_count;
} EntriesJob;

static void entries_job_work(StreamJob *base) {
	EntriesJob *j = (EntriesJob *)base;
	char path[420];
	snprintf(path, sizeof(path), "%sobjects/%s_%d_%d.skobj", pack_dir, j->prefix, j->cx, j->cy);
	long size = 0;
	uint8_t *buf = file_read_all(path, &size);
	if (!buf)
		return; // no objects in this cell
	// "SKOB", u16 version, u16 reserved, s32 x, s32 y, u32 count, then entries
	uint32_t count = 0;
	if (size >= 20)
		memcpy(&count, buf + 16, 4);
	int ok = size >= 20 && !memcmp(buf, "SKOB", 4) && *(uint16_t *)(buf + 4) == SKOB_VERSION && count < 100000 &&
	         size >= 20 + (long)count * (long)sizeof(Entry);
	if (ok && count) {
		j->entries = malloc(count * sizeof(Entry));
		if (j->entries) {
			memcpy(j->entries, buf + 20, count * sizeof(Entry));
			j->entry_count = (int)count;
		}
	}
	free(buf);
	if (!ok)
		sceClibPrintf("objects: bad file %s\n", path);
}

static int entries_job_finish(StreamJob *base, unsigned long long deadline) {
	(void)deadline;
	EntriesJob *j = (EntriesJob *)base;
	CellObjects *c = &cells[j->slot];
	if (!c->used || c->gen != j->gen)
		return 1; // the cell was unloaded meanwhile
	c->entries = j->entries;
	c->entry_count = j->entry_count;
	c->entries_state = 1;
	j->entries = NULL;
	return 1;
}

static void entries_job_discard(StreamJob *base) {
	EntriesJob *j = (EntriesJob *)base;
	free(j->entries);
	free(j);
}

// Placements are tiny and everything else in the cell (and collision) waits on them
static int entries_job_priority(StreamJob *base) {
	(void)base;
	return STREAM_PRIORITY_URGENT + 1;
}

static void submit_entries_job(CellObjects *c) {
	EntriesJob *j = calloc(1, sizeof(*j));
	if (!j)
		return;
	j->base.priority = entries_job_priority;
	j->base.work = entries_job_work, j->base.what = "placements";
	j->base.finish = entries_job_finish;
	j->base.discard = entries_job_discard;
	j->slot = (int)(c - cells);
	j->cx = c->cx, j->cy = c->cy;
	j->gen = c->gen;
	j->prefix = c->prefix;
	stream_submit(&j->base);
}

static CellObjects *find_cell(int cx, int cy) {
	for (int i = 0; i < MAX_CELLS; i++)
		if (cells[i].used && cells[i].cx == cx && cells[i].cy == cy)
			return &cells[i];
	return NULL;
}

static void unload_cell(CellObjects *c) {
	for (int g = 0; g < GROUP_COUNT; g++)
		for (int l = 0; l < LOD_COUNT; l++)
			free_group(&c->var[g][l]);
	free(c->fx);
	free(c->entries);
	memset(c, 0, sizeof(*c));
}

static int ring_of(const CellObjects *c, int pcx, int pcy) {
	int dx = abs(c->cx - pcx), dy = abs(c->cy - pcy);
	return dx > dy ? dx : dy;
}

// --- Public API -------------------------------------------------------------------------

void objects_init(const char *pack, const char *world) {
	(void)world; // cells' files are per world (worlds_prefix)
	snprintf(pack_dir, sizeof(pack_dir), "%s", pack);

	// (both tables through file_read_all: from the pack's objects archive where there is one - a data folder made
	// for the card holds the archives only, Nov 11 - else the loose files)
	char path[420];
	snprintf(path, sizeof(path), "%sobjects/models.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size);
	if (!text) {
		sceClibPrintf("objects: no model table at %s\n", path);
		return;
	}
	int cap = 0;
	for (char *line = text, *end = text + size; line < end;) {
		char *eol = memchr(line, '\n', end - line);
		if (!eol)
			eol = end;
		char name[256];
		const int n = (int)(eol - line) < 255 ? (int)(eol - line) : 255;
		memcpy(name, line, n), name[n] = 0;
		name[strcspn(name, "\r\n")] = 0;
		line = eol + 1;
		if (!name[0])
			continue;
		if (model_count == cap) {
			cap = cap ? cap * 2 : 256;
			models = realloc(models, cap * sizeof(Model));
		}
		// "<mesh> <far LOD mesh or -> <mid LOD mesh or ->" (the LOD meshes only matter to
		// the converter's bake; the engine needs the name for the model's collision)
		Model *m = &models[model_count++];
		memset(m, 0, sizeof(Model));
		sscanf(name, "%255s", m->name);
		m->fish = objects_is_fish_model(m->name);
	}
	free(text);
	sceClibPrintf("objects: %d models\n", model_count);

	// Fire/glow points: "model kind x y z w h" per line
	snprintf(path, sizeof(path), "%sobjects/fx.txt", pack_dir);
	text = file_read_all(path, &size);
	int fx_lines = 0;
	if (text) {
		for (char *line = text, *end = text + size; line < end;) {
			char *eol = memchr(line, '\n', end - line);
			if (!eol)
				eol = end;
			char one[160];
			const int n = (int)(eol - line) < 159 ? (int)(eol - line) : 159;
			memcpy(one, line, n), one[n] = 0;
			line = eol + 1;
			int idx, kind;
			float x, y, z, w, h;
			if (sscanf(one, "%d %d %f %f %f %f %f", &idx, &kind, &x, &y, &z, &w, &h) != 7)
				continue;
			if (idx < 0 || idx >= model_count)
				continue;
			Model *m = &models[idx];
			m->fx = realloc(m->fx, (m->fx_count + 1) * sizeof(FxPoint));
			m->fx[m->fx_count++] = (FxPoint){kind, x, y, z, w, h, 0.0f};
			fx_lines++;
		}
		free(text);
	}
	snprintf(path, sizeof(path), "%stextures/fx_fire.sktex", pack_dir);
	fx_fire_tex = sktex_load(path);
	snprintf(path, sizeof(path), "%stextures/fx_glow.sktex", pack_dir);
	fx_glow_tex = sktex_load(path);
	static const char *sprite_names[SPRITE_KINDS] = {NULL, "fx_flame", NULL, "fx_white", "fx_rays", "fx_sparks", "fx_ring",
	                                                  "fx_rune", "fx_bolt", "fx_arcs"};
	for (int i = 0; i < SPRITE_KINDS; i++) {
		if (!sprite_names[i])
			continue;
		snprintf(path, sizeof(path), "%stextures/%s.sktex", pack_dir, sprite_names[i]);
		sprite_tex[i] = sktex_load(path);
	}
	sprite_tex[SPRITE_FIRE] = fx_fire_tex, sprite_tex[SPRITE_GLOW] = fx_glow_tex;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	sceClibPrintf("objects: %d fire/glow points\n", fx_lines);
	init_object_shader();
}

// Ring (in cells) out to which a size group's detail level is needed, -1 if never.
// Full detail: small objects in the 3x3 cells. Mid: large and huge objects in the 5x5,
// from the player outwards (the mid level is the full mesh too, so they have no separate
// near level: it would only hold and draw a second copy of the same geometry). Far: huge
// objects out to the 9x9. Ring r is guaranteed to cover at least r * CELL_SIZE_M around
// the player, which is what bounds the dissolve distances in variant_band.
static int huge_radius = OBJECTS_HUGE_RADIUS; // (the performance setting's: how far the biggest are drawn)
static float far_fade_start = 210.0f, far_fade_end = 234.0f;
static int rings_changed;

void objects_set_far_radius(int rings) {
	rings = rings < OBJECTS_LARGE_RADIUS ? OBJECTS_LARGE_RADIUS : rings > OBJECTS_HUGE_RADIUS ? OBJECTS_HUGE_RADIUS : rings;
	if (rings == huge_radius)
		return;
	huge_radius = rings;
	// (their far dissolve inside what's loaded: Skyrim's 210..234 m at 4 rings, in proportion)
	far_fade_start = 210.0f * rings / OBJECTS_HUGE_RADIUS, far_fade_end = 234.0f * rings / OBJECTS_HUGE_RADIUS;
	rings_changed = 1;
}

static int variant_radius(int g, int l) {
	if (l == LOD_FULL) // (every group: near you at full detail, out as far as its objects reach,
		// as mountains do; drawn only within its band: large and huge beyond on their mid mesh)
		return g == GROUP_DETAIL ? OBJECTS_DETAIL_RADIUS : g == GROUP_LARGE ? OBJECTS_LARGE_RADIUS : huge_radius;
	if (l == LOD_MID)
		return g == GROUP_DETAIL ? -1 : g == GROUP_HUGE ? huge_radius : OBJECTS_LARGE_RADIUS;
	return g == GROUP_HUGE ? huge_radius : -1;
}

// Ring out to which a level is built (and kept): one ring beyond where it's drawn, so a
// cell's objects are ready before the player gets close enough to see them. Built but
// undrawn levels cost memory only. The far level stops at the loaded placements.
#define PREFETCH_RINGS 0 // 1 raised memory pressure; revisit once loading is cheap
static int request_radius(int g, int l) {
	int r = variant_radius(g, l);
	return r >= 0 && l != LOD_FAR ? r + PREFETCH_RINGS : r;
}

// Frees detail levels of cells the player has walked away from. A level stays until
// the player is FREE_MARGIN_M beyond the ring it is needed in, so walking back and
// forth along a cell border doesn't rebuild it every time, without keeping a whole
// extra ring of cells in GPU memory.
#define FREE_MARGIN_M 15.0f

static void cell_distance_range(const CellObjects *c, float x, float z, float margin, float *dmin, float *dmax);

static void free_far_variants(float x, float z) {
	for (int i = 0; i < MAX_CELLS; i++) {
		CellObjects *c = &cells[i];
		if (!c->used)
			continue;
		float dmin, dmax;
		cell_distance_range(c, x, z, 0.0f, &dmin, &dmax);
		int ring = ring_of(c, terrain_cell_x(x), terrain_cell_y(z));
		for (int g = 0; g < GROUP_COUNT; g++)
			for (int l = 0; l < LOD_COUNT; l++) {
				Group *grp = &c->var[g][l];
				if (grp->state == VAR_NONE)
					continue;
				// Requests go by ring (a square of cells), so a level is kept anywhere in
				// that square: freeing by distance alone (a circle) dropped the diagonal
				// cells, which were requested again the next frame, and so on, every frame
				int radius = request_radius(g, l);
				if (radius >= 0 && (ring <= radius || dmin <= radius * CELL_SIZE_M + FREE_MARGIN_M))
					continue;
				free_group(grp); // a queued build's result is dropped when it arrives
				if (g == GROUP_DETAIL && l == LOD_FULL) {
					free(c->fx);
					c->fx = NULL;
					c->fx_count = 0;
				}
			}
	}
}

static void report_memory(int pcx, int pcy) {
	sceClibPrintf("objects: cell %d,%d gpu free: vram %u KB, ram %u KB, phycont %u KB%s\n", pcx, pcy,
	              (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024),
	              (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024),
	              gpu_out_of_memory ? " -- OUT OF GPU MEMORY, some objects dropped" : "");
	gpu_out_of_memory = 0;
}

static int world_changed;

void objects_set_switch(int number, int on) {
	const uint32_t was = switches_on;
	if (on)
		switches_on |= 1u << (number & 31);
	else
		switches_on &= ~(1u << (number & 31));
	if (switches_on != was)
		force_collision = 1; // (walls fallen, rubble come: the collision built again)
}

uint32_t objects_switches(void) {
	return switches_on;
}

void objects_set_switches(uint32_t bits) {
	if (bits != switches_on)
		switches_on = bits, force_collision = 1;
}

void objects_world_changed(void) {
	for (int i = 0; i < MAX_CELLS; i++)
		if (cells[i].used && cells[i].prefix != worlds_prefix(cells[i].cx, cells[i].cy))
			unload_cell(&cells[i]);
	world_changed = 1;
	force_collision = 1;
}

void objects_update(float x, float z) {
	static int last_pcx = 0x7fffffff, last_pcy = 0x7fffffff;
	static int collision_wanted;
	// (placements out to the collision's source cells at least, whatever is drawn: the
	// collision waits on them - a short view distance mustn't leave you walking through walls)
	const int place_radius = huge_radius > COLLISION_SOURCE_RADIUS ? huge_radius : COLLISION_SOURCE_RADIUS;
	if (world_changed || rings_changed)
		world_changed = rings_changed = 0, last_pcx = 0x7fffffff;
	int pcx = terrain_cell_x(x), pcy = terrain_cell_y(z);
	stream_pcx = pcx, stream_pcy = pcy;
	free_far_variants(x, z);

	if (force_collision) {
		force_collision = 0;
		collision_wanted = 1;
	}
	if (pcx != last_pcx || pcy != last_pcy) {
		last_pcx = pcx, last_pcy = pcy;
		for (int i = 0; i < MAX_CELLS; i++)
			if (cells[i].used && ring_of(&cells[i], pcx, pcy) > place_radius + 1)
				unload_cell(&cells[i]);
		// Placements of every cell in range, nearest first
		for (int ring = 0; ring <= place_radius; ring++)
			for (int dy = -ring; dy <= ring; dy++)
				for (int dx = -ring; dx <= ring; dx++) {
					if (abs(dx) != ring && abs(dy) != ring)
						continue;
					if (find_cell(pcx + dx, pcy + dy))
						continue;
					CellObjects *c = NULL;
					for (int i = 0; i < MAX_CELLS && !c; i++)
						if (!cells[i].used)
							c = &cells[i];
					if (!c)
						continue; // no free slots (shouldn't happen)
					static unsigned next_gen = 1;
					c->used = 1;
					c->gen = next_gen++;
					c->cx = pcx + dx;
					c->cy = pcy + dy;
					c->prefix = worlds_prefix(c->cx, c->cy);
					submit_entries_job(c);
				}
		collision_wanted = 1;
		report_memory(pcx, pcy);
	}
	// The collision region first (gameplay), as soon as its source cells are known
	if (collision_wanted && submit_collision_job(pcx, pcy))
		collision_wanted = 0;
	// Then every (size group, detail level) needed within its ring, nearest first
	for (int ring = 0; ring <= huge_radius; ring++)
		for (int dy = -ring; dy <= ring; dy++)
			for (int dx = -ring; dx <= ring; dx++) {
				if (abs(dx) != ring && abs(dy) != ring)
					continue;
				CellObjects *c = find_cell(pcx + dx, pcy + dy);
				if (!c || !c->entries_state)
					continue;
				for (int g = 0; g < GROUP_COUNT; g++)
					for (int l = 0; l < LOD_COUNT; l++) {
						int radius = request_radius(g, l);
						if (radius >= 0 && ring <= radius && c->var[g][l].state == VAR_NONE)
							submit_group_job(c, g, l);
					}
			}
}

static void bind_batch(const Batch *b) {
	glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
	glVertexPointer(3, GL_SHORT, sizeof(PackedVertex), (void *)offsetof(PackedVertex, x));
	glTexCoordPointer(2, GL_SHORT, sizeof(PackedVertex), (void *)offsetof(PackedVertex, u));
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(PackedVertex), (void *)offsetof(PackedVertex, r));
}

// The level drawn at a ring by the single-level paths (water, fixed-function fallback)
static int level_for_ring(int g, int ring) {
	if (ring <= OBJECTS_DETAIL_RADIUS)
		return LOD_FULL;
	return ring <= OBJECTS_LARGE_RADIUS ? LOD_MID : LOD_FAR;
}

static const float group_overhang_m[GROUP_COUNT] = {15.0f, 40.0f, 150.0f};

// A group's objects can overhang their cell by up to its overhang (big rocks and cliffs
// reach far), so test the cell grown by that: culling less would pop them in and out
// as the view turns
static int group_visible(const CellObjects *c, int g) {
	float m = group_overhang_m[g];
	return terrain_rect_in_view(c->cx * CELL_SIZE_M - m, -c->cy * CELL_SIZE_M + m,
	                            (c->cx + 1) * CELL_SIZE_M + m, -(c->cy + 1) * CELL_SIZE_M - m);
}

// Fixed-function path: water, and the fallback if the object shader failed to build.
static void draw_kind(int kind, int pcx, int pcy) {
	for (int i = 0; i < MAX_CELLS; i++) {
		const CellObjects *c = &cells[i];
		if (!c->used)
			continue;
		int ring = ring_of(c, pcx, pcy);
		for (int g = 0; g < GROUP_COUNT; g++) {
			if (!group_visible(c, g))
				continue;
			// One level per cell: the finest one built for this distance
			int l = level_for_ring(g, ring);
			const Group *grp = &c->var[g][l];
			for (int k = 0; k < grp->count; k++) {
				const Batch *b = &grp->batches[k];
				if (b->kind != kind)
					continue;
				if (kind != KIND_WATER)
					glBindTexture(GL_TEXTURE_2D, b->tex);
				if (b->two_sided)
					glDisable(GL_CULL_FACE);
				bind_batch(b);
				// Undo the quantization with the modelview and texture matrices
				glMatrixMode(GL_MODELVIEW);
				glPushMatrix();
				glTranslatef(b->pos[0], b->pos[1], b->pos[2]);
				glScalef(b->pos[3], b->pos[3], b->pos[3]);
				glMatrixMode(GL_TEXTURE);
				glLoadIdentity();
				glTranslatef(b->uv[0], b->uv[1], 0.0f);
				glScalef(b->uv[2], b->uv[2], 1.0f);
				glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT, 0);
				glLoadIdentity();
				glMatrixMode(GL_MODELVIEW);
				glPopMatrix();
				if (b->two_sided)
					glEnable(GL_CULL_FACE);
			}
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// --- Object shader: distance dissolve ------------------------------------------------
//
// Each detail level is visible inside a distance band and dissolves out of it with a
// 4x4 ordered-dither pattern (PS2-style screen-door transparency: no sorting, depth stays
// correct). Neighbouring levels use exactly complementary tests on the same threshold,
// so across a hand-over every pixel shows one level or the other: no gaps, no overlap.
// The shader also does what the fixed-function path did: texture x vertex colour x 2
// (overbright), alpha test for foliage, and linear fog.

static const char *OBJ_VS = // atmosphere_cg() is prepended
	"uniform float4x4 u_wvp;\n"
	"uniform float4 u_pos; // dequantization: world = (position * 32 + fine) * w + xyz\n"
	"uniform float4 u_uv;  // uv = texcoord * z + xy (xy includes the flowing-water scroll)\n"
	"void main(float4 position, float2 texcoord, float4 color,\n"
	"	float4 out vPosition : POSITION, float2 out vTexcoord : TEXCOORD0, float4 out vColor : COLOR,\n"
	"	float4 out vClip : TEXCOORD1, float2 out vRel : TEXCOORD2, float4 out vFog : TEXCOORD3,\n"
	"	float3 out vTorch : TEXCOORD4, float3 out vTorchColor : TEXCOORD5) {\n"
	"	// Whole numbers (not normalized: exact). w: 5 more bits per axis (x << 10 | y << 5 | z),\n"
	"	// 1/32 of a step each; u_pos.w is the fine step (a power of two: exact products)\n"
	"	float f = position.w;\n"
	"	float fx = floor(f / 1024.0f), fy = floor((f - fx * 1024.0f) / 32.0f), fz = f - fx * 1024.0f - fy * 32.0f;\n"
	"	float3 q = position.xyz * 32.0f + float3(fx, fy, fz);\n"
	"	float3 world = q * u_pos.w + u_pos.xyz;\n"
	"	vPosition = mul(float4(world, 1.0f), u_wvp);\n"
	"	vClip = vPosition;\n"
	"	vTexcoord = texcoord * u_uv.z + u_uv.xy;\n"
	"	vColor = float4(atmo_light(color.rgb), color.a);\n"
	"	vRel = world.xz - u_cam.xz; // (its length per pixel: interpolating the length itself\n"
	"	                            // across long triangles put their middles far away)\n"
	"	vFog = atmo_fog(world);\n"
	"	vTorch = atmo_torch(world);\n"
	"	vTorchColor = u_torchColor;\n"
	"}\n";

static const char *OBJ_FS =
	"uniform sampler2D u_tex;\n"
	"uniform float4 u_band;     // near dissolve (x..y), far dissolve (z..w), horizontal metres\n"
	"uniform float u_alphaRef;  // alpha test reference (<0: off)\n"
	"uniform float2 u_screen;\n"
	"float bayer2(float2 q) { return fmod(2.0f * q.x + 3.0f * q.y, 4.0f); }\n"
	"float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR, float4 vClip : TEXCOORD1,\n"
	"	float2 vRel : TEXCOORD2, float4 vFog : TEXCOORD3, float3 vTorch : TEXCOORD4, float3 vTorchColor : TEXCOORD5) {\n"
	"	float vDist = length(vRel);\n"
	"	float2 pix = floor((vClip.xy / vClip.w * 0.5f + 0.5f) * u_screen);\n"
	"	float2 p = fmod(pix, 4.0f);\n"
	"	float thr = (4.0f * bayer2(fmod(p, 2.0f)) + bayer2(floor(p * 0.5f)) + 0.5f) / 16.0f;\n"
	"	float rn = saturate((vDist - u_band.x) / (u_band.y - u_band.x));\n"
	"	float rf = saturate((u_band.w - vDist) / (u_band.w - u_band.z));\n"
	"	if (rn < 1.0f - thr || rf <= thr) discard;\n"
	"	float4 t = tex2D(u_tex, vTexcoord);\n"
	"	float4 c = float4(t.rgb * vColor.rgb * 2.0f, t.a * vColor.a);\n"
	"	if (c.a < u_alphaRef) discard;\n"
	TORCH_CG
	TONEMAP_CG
	"	return float4(lerp(c.rgb, vFog.rgb, vFog.a), c.a);\n"
	"}\n";

// The same for what's solid and wholly within its band: no dissolve, no alpha test - nothing
// discarded. (The Vita's GPU skips the pixels hidden behind others only for shaders that never
// discard: with the dissolving one on everything, a city's every wall was shaded behind every
// other, several times a pixel.)
static const char *SOLID_FS =
	"uniform sampler2D u_tex;\n"
	"float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR, float4 vFog : TEXCOORD3,\n"
	"	float3 vTorch : TEXCOORD4, float3 vTorchColor : TEXCOORD5) {\n"
	"	float4 t = tex2D(u_tex, vTexcoord);\n"
	"	float4 c = float4(t.rgb * vColor.rgb * 2.0f, t.a * vColor.a);\n"
	TORCH_CG
	TONEMAP_CG
	"	return float4(lerp(c.rgb, vFog.rgb, vFog.a), c.a);\n"
	"}\n";

// Characters posed on the GPU: their own vertices (actors.c's, as in the file: position,
// texcoord, normal with the third bone in its w, colour, two bones and their weights), the
// pose as up to 64 bones (three rows each: rotation and translation, the heading and scale in
// them), lit per vertex as lighting_bake does on the CPU (and tinted: a poisoned or enchanted
// weapon). The CPU only poses the skeleton; posing every vertex of a crowd took a third of a
// city's frame.
static const char *SKIN_VS = // atmosphere_cg() is prepended
	"uniform float4x4 u_wvp;\n"
	"uniform float4 u_pos;        // the character's origin (world)\n"
	"uniform float4 u_bones[192]; // 64 bones x 3 rows\n"
	"uniform float4 u_light[4];   // lighting_bake: ground, sky, sun light, sun direction\n"
	"uniform float4 u_tintMul;\n"
	"uniform float4 u_tintAdd;\n"
	"void main(float3 position, float2 texcoord, float4 normal, float4 color, float4 bones,\n"
	"	float4 out vPosition : POSITION, float2 out vTexcoord : TEXCOORD0, float4 out vColor : COLOR,\n"
	"	float4 out vClip : TEXCOORD1, float2 out vRel : TEXCOORD2, float4 out vFog : TEXCOORD3,\n"
	"	float3 out vTorch : TEXCOORD4, float3 out vTorchColor : TEXCOORD5) {\n"
	"	float b2 = floor(normal.w * 127.0f + 0.5f);\n"
	"	if (b2 < 0.0f) b2 += 256.0f; // (a byte: read as a signed normalized one)\n"
	"	float w0 = bones.z / 255.0f, w1 = bones.w / 255.0f, w2 = 1.0f - w0 - w1;\n"
	"	int i0 = (int)bones.x * 3, i1 = (int)bones.y * 3, i2 = (int)b2 * 3;\n"
	"	float4 p = float4(position, 1.0f);\n"
	"	float3 r0 = float3(dot(u_bones[i0], p), dot(u_bones[i0 + 1], p), dot(u_bones[i0 + 2], p));\n"
	"	float3 r1 = float3(dot(u_bones[i1], p), dot(u_bones[i1 + 1], p), dot(u_bones[i1 + 2], p));\n"
	"	float3 r2 = float3(dot(u_bones[i2], p), dot(u_bones[i2 + 1], p), dot(u_bones[i2 + 2], p));\n"
	"	float3 world = r0 * w0 + r1 * w1 + r2 * w2 + u_pos.xyz;\n"
	"	float3 n = normalize(float3(dot(u_bones[i0].xyz, normal.xyz), dot(u_bones[i0 + 1].xyz, normal.xyz),\n"
	"	                            dot(u_bones[i0 + 2].xyz, normal.xyz)));\n"
	"	float hemi = n.y * 0.5f + 0.5f, ndl = max(dot(n, u_light[3].xyz), 0.0f);\n"
	"	float3 lit = (u_light[0].rgb + (u_light[1].rgb - u_light[0].rgb) * hemi + u_light[2].rgb * ndl) * color.rgb * 0.5f;\n"
	"	lit = saturate(lit * u_tintMul.rgb + u_tintAdd.rgb);\n"
	"	vPosition = mul(float4(world, 1.0f), u_wvp);\n"
	"	vClip = vPosition;\n"
	"	vTexcoord = texcoord;\n"
	"	vColor = float4(atmo_light(lit), color.a);\n"
	"	vRel = world.xz - u_cam.xz;\n"
	"	vFog = atmo_fog(world);\n"
	"	vTorch = atmo_torch(world);\n"
	"	vTorchColor = u_torchColor * color.rgb; // (the torch on their own colour too: hair's tint - under a torch,\n"
	"	                                         //  hair lit as its grey texture alone read as blonde)\n"
	"}\n";

// Fires and glows: additive, so they fade to black (nothing added) in the fog
static const char *FX_VS = // atmosphere_cg() is prepended
	"uniform float4x4 u_wvp;\n"
	"void main(float3 position, float2 texcoord, float4 color,\n"
	"	float4 out vPosition : POSITION, float2 out vTexcoord : TEXCOORD0, float4 out vColor : COLOR) {\n"
	"	vPosition = mul(float4(position, 1.0f), u_wvp);\n"
	"	vTexcoord = texcoord;\n"
	"	vColor = float4(color.rgb * 2.0f * (1.0f - atmo_fog(position).a), color.a);\n"
	"}\n";

static const char *FX_FS =
	"uniform sampler2D u_tex;\n"
	"float4 main(float2 vTexcoord : TEXCOORD0, float4 vColor : COLOR) {\n"
	"	return tex2D(u_tex, vTexcoord) * vColor;\n"
	"}\n";

static GLuint obj_prog, fx_prog; // the current atmosphere variant's (see use_variant)
static float render_w = 960.0f, render_h = 544.0f;
static GLint u_wvp, u_tex, u_band, u_alpha_ref, u_screen, u_pos, u_uv, fx_wvp;
static AtmoUniforms obj_atmo, fx_atmo;

typedef struct {
	GLuint prog, fx_prog, solid;
	GLint wvp, tex, band, alpha_ref, screen, pos, uv, fx_wvp;
	GLint s_wvp, s_tex, s_pos, s_uv; // (the solid program's)
	AtmoUniforms atmo, fx_atmo, s_atmo;
	GLuint skin[2]; // characters posed on the GPU: dissolving / alpha-tested, and solid
	struct { GLint wvp, tex, band, alpha_ref, screen, pos, bones, light, tmul, tadd; AtmoUniforms atmo; } sk[2];
} ObjShaders;
static ObjShaders variants[ATMO_VARIANTS];

// Points the globals above at the programs of the current atmosphere debug variant
static void use_variant(void) {
	const ObjShaders *v = &variants[atmosphere_variant()];
	obj_prog = v->prog, fx_prog = v->fx_prog;
	u_wvp = v->wvp, u_tex = v->tex, u_band = v->band, u_alpha_ref = v->alpha_ref;
	u_screen = v->screen, u_pos = v->pos, u_uv = v->uv, fx_wvp = v->fx_wvp;
	obj_atmo = v->atmo, fx_atmo = v->fx_atmo;
}

// Dissolve distances (metres, horizontal). Each band sits inside the distance its cells
// are guaranteed to be loaded at (ring r covers >= r * 58.5 m), so nothing is ever cut.
static float full_fade_start = 45.0f, full_fade_end = 58.0f; // (the performance setting's: objects_set_near_fade)
#define FULL_FADE_START full_fade_start
#define FULL_FADE_END full_fade_end

void objects_set_near_fade(float start_m, float end_m) {
	full_fade_end = end_m < 14.0f ? 14.0f : end_m > 58.0f ? 58.0f : end_m;
	full_fade_start = start_m < full_fade_end - 4.0f ? start_m : full_fade_end - 4.0f;
}
#define MID_FADE_START 100.0f
#define MID_FADE_END 117.0f

static void variant_band(int g, int l, float band[4]) {
	if (l == LOD_FULL) {
		band[0] = -2.0f, band[1] = -1.0f; // no near dissolve
		band[2] = FULL_FADE_START, band[3] = FULL_FADE_END;
	} else if (l == LOD_MID) {
		band[0] = FULL_FADE_START, band[1] = FULL_FADE_END; // (taking over from their full detail)
		band[2] = MID_FADE_START, band[3] = MID_FADE_END;
	} else {
		band[0] = MID_FADE_START, band[1] = MID_FADE_END;
		band[2] = far_fade_start, band[3] = far_fade_end;
	}
}

static void init_object_shader(void) {
	static const char *const attribs[] = {"position", "texcoord", "color"};
	for (int i = 0; i < ATMO_VARIANTS; i++) {
		ObjShaders *v = &variants[i];
		v->prog = shader_program_prefixed("objects", atmosphere_cg(i), OBJ_VS, OBJ_FS, attribs, 3);
		if (v->prog) {
			v->wvp = glGetUniformLocation(v->prog, "u_wvp");
			v->tex = glGetUniformLocation(v->prog, "u_tex");
			v->band = glGetUniformLocation(v->prog, "u_band");
			v->alpha_ref = glGetUniformLocation(v->prog, "u_alphaRef");
			v->screen = glGetUniformLocation(v->prog, "u_screen");
			v->pos = glGetUniformLocation(v->prog, "u_pos");
			v->uv = glGetUniformLocation(v->prog, "u_uv");
			atmosphere_locate(v->prog, &v->atmo);
		}
		v->solid = shader_program_prefixed("objects solid", atmosphere_cg(i), OBJ_VS, SOLID_FS, attribs, 3);
		if (v->solid) {
			v->s_wvp = glGetUniformLocation(v->solid, "u_wvp");
			v->s_tex = glGetUniformLocation(v->solid, "u_tex");
			v->s_pos = glGetUniformLocation(v->solid, "u_pos");
			v->s_uv = glGetUniformLocation(v->solid, "u_uv");
			atmosphere_locate(v->solid, &v->s_atmo);
		}
		static const char *const skin_attribs[] = {"position", "texcoord", "normal", "color", "bones"};
		for (int k = 0; k < 2; k++) {
			v->skin[k] = shader_program_prefixed(k ? "skinned solid" : "skinned", atmosphere_cg(i), SKIN_VS,
			                                     k ? SOLID_FS : OBJ_FS, skin_attribs, 5);
			if (!v->skin[k])
				continue;
			v->sk[k].wvp = glGetUniformLocation(v->skin[k], "u_wvp");
			v->sk[k].tex = glGetUniformLocation(v->skin[k], "u_tex");
			v->sk[k].band = glGetUniformLocation(v->skin[k], "u_band");
			v->sk[k].alpha_ref = glGetUniformLocation(v->skin[k], "u_alphaRef");
			v->sk[k].screen = glGetUniformLocation(v->skin[k], "u_screen");
			v->sk[k].pos = glGetUniformLocation(v->skin[k], "u_pos");
			v->sk[k].bones = glGetUniformLocation(v->skin[k], "u_bones");
			v->sk[k].light = glGetUniformLocation(v->skin[k], "u_light");
			v->sk[k].tmul = glGetUniformLocation(v->skin[k], "u_tintMul");
			v->sk[k].tadd = glGetUniformLocation(v->skin[k], "u_tintAdd");
			atmosphere_locate(v->skin[k], &v->sk[k].atmo);
		}
		v->fx_prog = shader_program_prefixed("fx", atmosphere_cg(i), FX_VS, FX_FS, attribs, 3);
		if (v->fx_prog) {
			v->fx_wvp = glGetUniformLocation(v->fx_prog, "u_wvp");
			atmosphere_locate(v->fx_prog, &v->fx_atmo);
			glUseProgram(v->fx_prog);
			glUniform1i(glGetUniformLocation(v->fx_prog, "u_tex"), 0);
			glUseProgram(0);
		}
	}
	use_variant();
	if (!obj_prog)
		sceClibPrintf("objects: using fixed-function fallback (no distance dissolve)\n");
}

void objects_set_render_size(int w, int h) {
	render_w = (float)w, render_h = (float)h;
}



// Horizontal distance range from (x, z) to a cell's rectangle grown by `margin`
static void cell_distance_range(const CellObjects *c, float x, float z, float margin, float *dmin, float *dmax) {
	float x0 = c->cx * CELL_SIZE_M - margin, x1 = (c->cx + 1) * CELL_SIZE_M + margin;
	float z0 = -(c->cy + 1) * CELL_SIZE_M - margin, z1 = -c->cy * CELL_SIZE_M + margin;
	float dx = x < x0 ? x0 - x : x > x1 ? x - x1 : 0.0f;
	float dz = z < z0 ? z0 - z : z > z1 ? z - z1 : 0.0f;
	*dmin = sqrtf(dx * dx + dz * dz);
	float fx = fmaxf(fabsf(x - x0), fabsf(x - x1)), fz = fmaxf(fabsf(z - z0), fabsf(z - z1));
	*dmax = sqrtf(fx * fx + fz * fz);
}

static int shaded_band; // last value set in the object program (see draw_shaded)
static unsigned frame_vertex_bytes, frame_draws; // drawn this frame (diagnostics)
// Sums since objects_stats last asked: the world's draws and triangles, the characters' and the
// bytes of theirs copied (client memory: vitaGL copies them every draw), the frames
static unsigned long long frame_tris, tris_mark; // (the world's triangles last frame: the governor's budget)
static unsigned long long st_world_draws, st_world_tris, st_dyn_draws, st_dyn_tris, st_dyn_bytes, st_frames;
static unsigned long long st_level_tris[GROUP_COUNT][LOD_COUNT]; // (by size group and detail level)

// One static batch through the object program (its uniforms and buffers)
static void draw_batch(const Batch *b) {
	if (!switch_shown(b->switch_tag))
		return; // (a world switch's other state: Helgen before / after Alduin)
	float scroll = b->flow ? -fmodf(now_s * FLOW_TILES_PER_S, 1.0f) : 0.0f;
	glUniform4f(u_pos, b->pos[0], b->pos[1], b->pos[2], b->pos[3] / FINE_UNITS); // (exact: see the shader)
	glUniform4f(u_uv, b->uv[0], b->uv[1] + scroll, b->uv[2], 0.0f);
	glBindTexture(GL_TEXTURE_2D, b->tex);
	glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
	glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(PackedVertex), (void *)offsetof(PackedVertex, x));
	glVertexAttribPointer(1, 2, GL_SHORT, GL_TRUE, sizeof(PackedVertex), (void *)offsetof(PackedVertex, u));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedVertex), (void *)offsetof(PackedVertex, r));
	glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT, 0);
	frame_vertex_bytes += b->vertex_count * sizeof(PackedVertex) + b->index_count * sizeof(uint16_t);
	frame_draws++;
	st_world_draws++, st_world_tris += b->index_count / 3;
}

// --- Interiors ------------------------------------------------------------------------------

typedef struct {
	float min[3], max[3];
	int room; // (Skyrim's room bound it's in: drawn while that room's in sight; -1 none: always)
	Group grp;
} ITile;

// Skyrim's rooms and portals in an interior: the room you're in, and those seen through a
// portal in view (and on, through theirs) are drawn - Skyrim's own way of keeping a fort's or
// a barrow's far halls off the GPU
#define MAX_ROOMS 128
#define MAX_PORTALS 256
#define MAX_ROOM_LINKS 256
typedef struct {
	float min[3], max[3];
	int16_t a, b;
} Portal;
static float room_box[MAX_ROOMS][6];
static Portal portals[MAX_PORTALS];
static int16_t room_links[MAX_ROOM_LINKS][2];
static int room_count, portal_count, link_count;
static uint8_t room_seen[MAX_ROOMS];
static int rooms_culling; // (this frame: the camera's in a room)

static ITile *itiles;
static int itile_count, itile_cap;
static FxPoint *in_fx; // the interior's fires and glows
static int in_fx_count;
static float in_radius = 60.0f;
static float view_wvp[16]; // this frame's, for culling interior tiles

// Whether any part of the box can be in front of the camera and inside the view
static int box_visible(const float m[16], const float mn[3], const float mx[3]) {
	int out[5] = {0};
	for (int i = 0; i < 8; i++) {
		float x = i & 1 ? mx[0] : mn[0], y = i & 2 ? mx[1] : mn[1], z = i & 4 ? mx[2] : mn[2];
		float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
		float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
		float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
		out[0] += cx < -cw, out[1] += cx > cw, out[2] += cy < -cw, out[3] += cy > cw, out[4] += cw < 0.05f;
	}
	for (int k = 0; k < 5; k++)
		if (out[k] == 8)
			return 0;
	return 1;
}

static float box_distance(const float mn[3], const float mx[3], float x, float y, float z) {
	float dx = fmaxf(fmaxf(mn[0] - x, 0.0f), x - mx[0]);
	float dy = fmaxf(fmaxf(mn[1] - y, 0.0f), y - mx[1]);
	float dz = fmaxf(fmaxf(mn[2] - z, 0.0f), z - mx[2]);
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

// The rooms in sight from (x, y, z): the camera's, those linked to it, and those through a
// portal in view and in reach (and theirs on). Out of every room (in a wall, a third-person
// camera): no culling
static void rooms_in_sight(float x, float y, float z) {
	rooms_culling = 0;
	if (!room_count)
		return;
	memset(room_seen, 0, sizeof(room_seen));
	for (int r = 0; r < room_count; r++) {
		const float *b = room_box[r];
		if (x >= b[0] && x <= b[3] && y >= b[1] && y <= b[4] && z >= b[2] && z <= b[5])
			room_seen[r] = 1, rooms_culling = 1;
	}
	if (!rooms_culling)
		return;
	for (int changed = 1, pass = 0; changed && pass < MAX_ROOMS; pass++) {
		changed = 0;
		for (int l = 0; l < link_count; l++) { // (open to each other: no portal between)
			const int a = room_links[l][0], b = room_links[l][1];
			if (room_seen[a] != room_seen[b])
				room_seen[a] = room_seen[b] = 1, changed = 1;
		}
		for (int k = 0; k < portal_count; k++) {
			const Portal *p = &portals[k];
			const int a = p->a, b = p->b;
			const int sa = a >= 0 && room_seen[a], sb = b >= 0 && room_seen[b];
			if (sa == sb || box_distance(p->min, p->max, x, y, z) > in_radius || !box_visible(view_wvp, p->min, p->max))
				continue;
			if (a >= 0)
				room_seen[a] = 1;
			if (b >= 0)
				room_seen[b] = 1;
			changed = 1;
		}
	}
}

int objects_point_in_sight(float x, float y, float z) {
	if (!indoors || !rooms_culling)
		return 1;
	int in_any = 0;
	for (int r = 0; r < room_count; r++) {
		const float *b = room_box[r];
		if (x >= b[0] && x <= b[3] && y >= b[1] && y <= b[4] && z >= b[2] && z <= b[5]) {
			if (room_seen[r])
				return 1;
			in_any = 1;
		}
	}
	return !in_any; // (in no room: as the walls out of every room, always)
}

static int tile_in_sight(const ITile *it) {
	return !rooms_culling || it->room < 0 || it->room >= room_count || room_seen[it->room];
}

static void draw_tiles(int kind, int two_sided, float cam_x, float cam_y, float cam_z) {
	for (int t = 0; t < itile_count; t++) {
		const ITile *it = &itiles[t];
		if (!tile_in_sight(it) || box_distance(it->min, it->max, cam_x, cam_y, cam_z) > in_radius ||
		    !box_visible(view_wvp, it->min, it->max))
			continue;
		for (int k = 0; k < it->grp.count; k++) {
			const Batch *b = &it->grp.batches[k];
			if (b->kind == kind && b->two_sided == two_sided)
				draw_batch(b);
		}
	}
}

void objects_interior_begin(float draw_radius_m) {
	objects_interior_end();
	indoors = 1;
	in_radius = draw_radius_m;
}

void objects_interior_set_radius(float draw_radius_m) {
	in_radius = draw_radius_m;
}

void objects_interior_rooms(const float *boxes, int rooms, const uint8_t *portal_data, int nportals,
                            const int16_t *links, int nlinks) {
	room_count = rooms < MAX_ROOMS ? rooms : MAX_ROOMS;
	memcpy(room_box, boxes, (size_t)room_count * 6 * sizeof(float));
	portal_count = 0;
	for (int i = 0; i < nportals && portal_count < MAX_PORTALS; i++) {
		Portal *p = &portals[portal_count];
		memcpy(p->min, portal_data + i * 28, 12);
		memcpy(p->max, portal_data + i * 28 + 12, 12);
		memcpy(&p->a, portal_data + i * 28 + 24, 2);
		memcpy(&p->b, portal_data + i * 28 + 26, 2);
		if (p->a < room_count && p->b < room_count)
			portal_count++;
	}
	link_count = 0;
	for (int i = 0; i < nlinks && link_count < MAX_ROOM_LINKS; i++)
		if (links[i * 2] >= 0 && links[i * 2] < room_count && links[i * 2 + 1] >= 0 && links[i * 2 + 1] < room_count)
			room_links[link_count][0] = links[i * 2], room_links[link_count][1] = links[i * 2 + 1], link_count++;
	game_log("interior: %d rooms, %d portals, %d links", room_count, portal_count, link_count);
}

int objects_interior_tile(const float min[3], const float max[3], int room, const void *skbat, long size) {
	BatchSource *src;
	int n = parse_batches(skbat, size, &src);
	if (n < 0)
		return 0;
	if (itile_count == itile_cap) {
		itile_cap = itile_cap ? itile_cap * 2 : 64;
		itiles = realloc(itiles, itile_cap * sizeof(ITile));
	}
	ITile *it = &itiles[itile_count++];
	memset(it, 0, sizeof(*it));
	memcpy(it->min, min, sizeof(it->min));
	memcpy(it->max, max, sizeof(it->max));
	it->room = room;
	for (int i = 0; i < n; i++)
		push_batch(&it->grp, &src[i], src[i].kind == KIND_WATER ? 0 : world_texture(src[i].tex));
	it->grp.state = VAR_READY;
	free(src);
	return 1;
}

// An interior's placements (kept while inside): its collision is built for the square of
// INTERIOR_COLLISION_HALF_M around the player, again whenever they've moved
// INTERIOR_COLLISION_MOVE_M from its middle (a great dungeon's whole collision at once
// doesn't fit in memory)
static Entry *in_entries;
static int in_entry_count, in_collision_pending;
static float in_collision_x = 1e9f, in_collision_z = 1e9f;

static int interior_collision_finish(StreamJob *base, unsigned long long deadline) {
	in_collision_pending = 0;
	return collision_job_finish(base, deadline);
}

static void interior_collision_discard(StreamJob *base) {
	in_collision_pending = 0;
	collision_job_discard(base);
}

static CollisionJob *interior_collision_job(float x, float z) {
	CollisionJob *j = calloc(1, sizeof(*j));
	if (!j)
		return NULL;
	j->entries = malloc((in_entry_count ? in_entry_count : 1) * sizeof(Entry));
	if (!j->entries) {
		free(j);
		return NULL;
	}
	memcpy(j->entries, in_entries, in_entry_count * sizeof(Entry));
	j->entry_count = in_entry_count;
	j->rx0 = x - INTERIOR_COLLISION_HALF_M, j->rx1 = x + INTERIOR_COLLISION_HALF_M;
	j->rz0 = z - INTERIOR_COLLISION_HALF_M, j->rz1 = z + INTERIOR_COLLISION_HALF_M;
	j->interior = interior_gen;
	return j;
}

void objects_interior_collision(const void *entries, int count, const float min[3], const float max[3]) {
	(void)min, (void)max;
	free(in_entries);
	in_entries = malloc((count ? count : 1) * sizeof(Entry));
	in_entry_count = in_entries ? count : 0;
	if (in_entries)
		memcpy(in_entries, entries, count * sizeof(Entry));
	interior_gen++;
	in_collision_x = in_collision_z = 1e9f; // (built at the player on the first update)
	collision_clear();
	free(in_fx);
	in_fx = collect_fx(in_entries, in_entry_count, &in_fx_count);
}

int objects_fire_near(float x, float y, float z, float radius) {
	for (int i = 0; i < in_fx_count && indoors; i++) {
		const FxPoint *f = &in_fx[i];
		if (f->kind == FX_FIRE && fabsf(f->y - y) < 2.5f && hypotf(f->x - x, f->z - z) < radius + f->w * 0.5f)
			return 1;
	}
	return 0;
}

void objects_interior_update(float x, float z) {
	if (!indoors || !in_entries)
		return;
	const float moved = fmaxf(fabsf(x - in_collision_x), fabsf(z - in_collision_z));
	if (moved > INTERIOR_COLLISION_HALF_M * 0.75f) {
		// Arriving (or somehow far off the region): now, before the next step (and any
		// region still being built for elsewhere is dropped)
		interior_gen++;
		CollisionJob *j = interior_collision_job(x, z);
		if (!j)
			return;
		collision_job_work(&j->base);
		if (j->region)
			collision_install(j->region);
		free(j->entries);
		free(j);
		in_collision_x = x, in_collision_z = z;
	} else if (moved > INTERIOR_COLLISION_MOVE_M && !in_collision_pending) {
		// Walking on: built on the streaming thread; the old region serves till it's in
		CollisionJob *j = interior_collision_job(x, z);
		if (!j)
			return;
		j->base.priority = collision_job_priority;
		j->base.work = collision_job_work, j->base.what = "collision (inside)";
		j->base.finish = interior_collision_finish;
		j->base.discard = interior_collision_discard;
		in_collision_pending = 1;
		in_collision_x = x, in_collision_z = z;
		stream_submit(&j->base);
	}
}

void objects_interior_end(void) {
	free(in_entries);
	in_entries = NULL;
	in_entry_count = 0;
	interior_gen++; // (its collision jobs still in flight are dropped)
	for (int i = 0; i < itile_count; i++)
		free_group(&itiles[i].grp);
	free(itiles);
	itiles = NULL;
	itile_count = itile_cap = 0;
	room_count = portal_count = link_count = 0, rooms_culling = 0;
	free(in_fx);
	in_fx = NULL;
	in_fx_count = 0;
	if (indoors)
		force_collision = 1; // the exterior's region comes back
	indoors = 0;
}

// A level's distance range from (x, z) and whether it can be in view, by its own bounds
static int bounds_near(const float lo[2], const float hi[2], float x, float z, float *dmin, float *dmax) {
	float dx = x < lo[0] ? lo[0] - x : x > hi[0] ? x - hi[0] : 0.0f;
	float dz = z < lo[1] ? lo[1] - z : z > hi[1] ? z - hi[1] : 0.0f;
	*dmin = sqrtf(dx * dx + dz * dz);
	float fx = fmaxf(fabsf(x - lo[0]), fabsf(x - hi[0])), fz = fmaxf(fabsf(z - lo[1]), fabsf(z - hi[1]));
	*dmax = sqrtf(fx * fx + fz * fz);
	return terrain_rect_in_view(lo[0], hi[1], hi[0], lo[1]);
}

static int group_near(const Group *grp, float x, float z, float *dmin, float *dmax) {
	return bounds_near(grp->lo, grp->hi, x, z, dmin, dmax);
}

// The frame budget. Vita3K copies every draw's vertices and indices into a ring each frame,
// and past about 20 MB a frame it overwrites geometry still being drawn (exploding
// polygons). Each frame the visible levels are let in nearest first while they fit; the
// farthest go undrawn for that frame instead. (The real Vita draws from its buffers.)
#define FRAME_BUDGET_BYTES (16u << 20) // objects; terrain, characters, fish and water take ~2-4 MB more
#define MAX_BUDGET_BATCHES 12000
static unsigned budget_frame;

static int budget_cmp(const void *a, const void *b) {
	float da = ((const float *)a)[0], db = ((const float *)b)[0];
	return da < db ? -1 : da > db;
}

static float view_limit = 1e9f; // nothing farther than this is drawn (a city's fog: objects_set_view_limit)

void objects_set_view_limit(float m) {
	view_limit = m > 0.0f ? m : 1e9f;
}

static void choose_budget(float cam_x, float cam_z) {
	static struct { float key; Batch *b; } cand[MAX_BUDGET_BATCHES];
	int n = 0;
	budget_frame++;
	for (int i = 0; i < MAX_CELLS; i++) {
		CellObjects *c = &cells[i];
		if (!c->used)
			continue;
		for (int g = 0; g < GROUP_COUNT; g++) {
			for (int l = 0; l < LOD_COUNT; l++) {
				Group *grp = &c->var[g][l];
				float band[4], dmin, dmax;
				variant_band(g, l, band);
				if (!grp->count || !group_near(grp, cam_x, cam_z, &dmin, &dmax) || dmin > band[3] || dmax < band[0] ||
				    dmin > view_limit)
					continue;
				for (int k = 0; k < grp->count && n < MAX_BUDGET_BATCHES; k++) {
					Batch *b = &grp->batches[k];
					if (!bounds_near(b->lo, b->hi, cam_x, cam_z, &dmin, &dmax) || dmin > band[3] || dmax < band[0] ||
					    dmin > view_limit)
						continue;
					// Nearest first; water (rivers vanishing) and the far horizon (mountains
					// missing) count as nearer than they are
					float key = dmin;
					if (b->kind == KIND_WATER || b->flow)
						key *= 0.1f;
					else if (l == LOD_FAR)
						key *= 0.4f;
					cand[n].key = key, cand[n].b = b;
					n++;
				}
			}
		}
	}
	unsigned total = 0; // (all of it fits - the Vita's case: no sorting, every one in)
	for (int k = 0; k < n; k++)
		total += cand[k].b->vertex_count * sizeof(PackedVertex) + cand[k].b->index_count * sizeof(uint16_t);
	if (total <= FRAME_BUDGET_BYTES) {
		for (int k = 0; k < n; k++)
			cand[k].b->in_budget = budget_frame;
		return;
	}
	qsort(cand, n, sizeof(cand[0]), budget_cmp);
	unsigned used = 0;
	static unsigned dropped_frames;
	int dropped = 0;
	for (int k = 0; k < n; k++) {
		unsigned bytes = cand[k].b->vertex_count * sizeof(PackedVertex) + cand[k].b->index_count * sizeof(uint16_t);
		if (used + bytes > FRAME_BUDGET_BYTES) {
			dropped = 1;
			continue; // (a smaller, farther one may still fit)
		}
		used += bytes;
		cand[k].b->in_budget = budget_frame;
	}
	if (dropped && ++dropped_frames % 300 == 1)
		sceClibPrintf("objects: over the frame budget, the farthest left out (%u frames so far)\n", dropped_frames);
}

// Whether a batch lies wholly within its band (seen whole: no dissolving) - the solid program
static int batch_solid(const Batch *b, const float band[4], float cx, float cz) {
	const float nx = fmaxf(b->lo[0] - cx, fmaxf(0.0f, cx - b->hi[0])), nz = fmaxf(b->lo[1] - cz, fmaxf(0.0f, cz - b->hi[1]));
	const float fx = fmaxf(fabsf(cx - b->lo[0]), fabsf(cx - b->hi[0])), fz = fmaxf(fabsf(cz - b->lo[1]), fabsf(cz - b->hi[1]));
	return hypotf(nx, nz) >= band[1] && hypotf(fx, fz) <= band[2];
}

static int solid_pass = -1; // (draw_shaded: -1 every batch; 1 only those wholly in their band; 0 the rest)

static void draw_shaded(int kind, int two_sided, float cam_x, float cam_y, float cam_z) {
	if (indoors) {
		draw_tiles(kind, two_sided, cam_x, cam_y, cam_z);
		return;
	}
	for (int i = 0; i < MAX_CELLS; i++) {
		const CellObjects *c = &cells[i];
		if (!c->used)
			continue;
		for (int g = 0; g < GROUP_COUNT; g++) {
			for (int l = 0; l < LOD_COUNT; l++) {
				const Group *grp = &c->var[g][l];
				if (!grp->count)
					continue;
				for (int k = 0; k < grp->count; k++) {
					const Batch *b = &grp->batches[k];
					// (choose_budget: in view, within its level's band, and in the budget)
					if (b->in_budget != budget_frame || b->kind != kind || b->two_sided != two_sided)
						continue;
					if (solid_pass >= 0) {
						float band[4];
						variant_band(g, l, band);
						if (batch_solid(b, band, cam_x, cam_z) != solid_pass)
							continue;
						if (solid_pass) { // (the solid program has no band)
							draw_batch(b);
							st_level_tris[g][l] += b->index_count / 3;
							continue;
						}
					}
					// Uniforms only when they change: every change makes vitaGL upload the whole
					// uniform block again for the next draw (and the pool it comes from is finite)
					if (g * LOD_COUNT + l != shaded_band) { // (the band depends on both)
						float band[4];
						variant_band(g, l, band);
						glUniform4fv(u_band, 1, band);
						shaded_band = g * LOD_COUNT + l;
					}
					draw_batch(b);
					st_level_tris[g][l] += b->index_count / 3;
				}
			}
		}
	}
	(void)cam_y;
}

void objects_set_time(float time_s) {
	now_s = time_s;
}

static float draw_cam[2]; // (this frame's camera: per-item dissolves of the dynamic geometry)

void objects_draw(float cam_x, float cam_y, float cam_z) {
	draw_cam[0] = cam_x, draw_cam[1] = cam_z;
	int pcx = terrain_cell_x(cam_x), pcy = terrain_cell_y(cam_z);
	use_variant();
	if (!obj_prog) { // shader unavailable: plain fixed-function drawing
		draw_kind(KIND_OPAQUE, pcx, pcy);
		glEnable(GL_ALPHA_TEST);
		draw_kind(KIND_ALPHA, pcx, pcy);
		glDisable(GL_ALPHA_TEST);
		return;
	}
	float wvp[16];
	shader_wvp(wvp);
	memcpy(view_wvp, wvp, sizeof(view_wvp));
	if (!indoors)
		choose_budget(cam_x, cam_z);
	else
		rooms_in_sight(cam_x, cam_y, cam_z);
	glUseProgram(obj_prog);
	glUniformMatrix4fv(u_wvp, 1, GL_FALSE, wvp);
	atmosphere_apply(&obj_atmo);
	glUniform1i(u_tex, 0);
	glUniform2f(u_screen, render_w, render_h); // dither cells = render target pixels
	shaded_band = -1;
	if (indoors) {
		const float band[4] = {-2.0f, -1.0f, in_radius - 6.0f, in_radius};
		glUniform4fv(u_band, 1, band);
	}
	for (int a = 0; a < 3; a++)
		glEnableVertexAttribArray(a);

	frame_vertex_bytes = frame_draws = 0;
	// Culling follows Skyrim's materials: only double-sided ones (foliage cards, banners,
	// ivy) are drawn from behind. Alpha-tested roofs and planks are one-sided, and seen
	// from below would show their sunlit top.
	const ObjShaders *sv = &variants[atmosphere_variant()];
	if (sv->solid && !indoors) { // the opaque wholly in their bands first: nothing discarded
		const GLint keep_pos = u_pos, keep_uv = u_uv;
		glUseProgram(sv->solid);
		glUniformMatrix4fv(sv->s_wvp, 1, GL_FALSE, wvp);
		atmosphere_apply(&sv->s_atmo);
		glUniform1i(sv->s_tex, 0);
		u_pos = sv->s_pos, u_uv = sv->s_uv;
		solid_pass = 1;
		draw_shaded(KIND_OPAQUE, 0, cam_x, cam_y, cam_z);
		glDisable(GL_CULL_FACE);
		draw_shaded(KIND_OPAQUE, 1, cam_x, cam_y, cam_z);
		glEnable(GL_CULL_FACE);
		u_pos = keep_pos, u_uv = keep_uv;
		glUseProgram(obj_prog);
		solid_pass = 0; // (then the rest, dissolving)
	}
	glUniform1f(u_alpha_ref, -1.0f); // opaque: no alpha test
	draw_shaded(KIND_OPAQUE, 0, cam_x, cam_y, cam_z);
	glDisable(GL_CULL_FACE);
	draw_shaded(KIND_OPAQUE, 1, cam_x, cam_y, cam_z);
	solid_pass = -1; // (the alpha-tested: all of them, discarding)
	glUniform1f(u_alpha_ref, 0.5f);
	draw_shaded(KIND_ALPHA, 1, cam_x, cam_y, cam_z);
	glEnable(GL_CULL_FACE);
	draw_shaded(KIND_ALPHA, 0, cam_x, cam_y, cam_z);
	st_frames++;
	frame_tris = st_world_tris - tris_mark, tris_mark = st_world_tris;
	// Peak geometry drawn per frame, every ~5 s (Vita3K copies all of it every frame)
	static unsigned peak_bytes, peak_draws, frames;
	if (frame_vertex_bytes > peak_bytes)
		peak_bytes = frame_vertex_bytes, peak_draws = frame_draws;
	if (++frames == 300) {
		sceClibPrintf("objects: peak per frame %u KB of vertices+indices, %u draws\n", peak_bytes / 1024, peak_draws);
		peak_bytes = peak_draws = frames = 0;
	}

	for (int a = 0; a < 3; a++)
		glDisableVertexAttribArray(a);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

#define SOFT_EDGE_ALPHA 0.08f // (the blended pass: down to this coverage)

int objects_skinning(void) {
	const ObjShaders *sv = &variants[atmosphere_variant()];
	return sv->skin[0] && sv->skin[1];
}

// The characters posed on the GPU, in one of objects_draw_dynamic's passes (-1: the solid
// one, drawn first; else 0 opaque, 1 alpha-tested, 2 its soft edge). done: those drawn solid
static void draw_skinned(const ObjectsDynamic *items, int count, int pass, const float wvp[16], const float band[4],
                         uint8_t *done) {
	const ObjShaders *sv = &variants[atmosphere_variant()];
	const int k = pass < 0;
	if (!sv->skin[k])
		return;
	const typeof(sv->sk[0]) *u = &sv->sk[k];
	int used = 0;
	float light[16];
	lighting_bake_params(light);
	GLuint last_tex = 0;
	for (int i = 0; i < count; i++) {
		const ObjectsDynamic *d = &items[i];
		if (!d->skin_src || !d->index_count)
			continue;
		const float dist = hypotf(d->pos[0] - draw_cam[0], d->pos[2] - draw_cam[1]);
		if (pass < 0) { // (opaque and wholly in the band: nothing discarded)
			if (d->alpha != 0 || d->fade > 0.0f || (d->fade == 0.0f && dist + 3.0f > band[2]))
				continue;
			if (i < 512)
				done[i] = 1;
		} else {
			if ((pass ? !ALPHA_CUT(d->alpha) : d->alpha != 0) || (i < 512 && done[i]))
				continue;
		}
		const float ref = d->alpha > ALPHA_GLOW ? d->alpha / 255.0f : 0.5f;
		if (pass == 2 && ref <= SOFT_EDGE_ALPHA)
			continue;
		if (!used++) { // (the program, once there's something for it)
			glUseProgram(sv->skin[k]);
			glUniformMatrix4fv(u->wvp, 1, GL_FALSE, wvp);
			atmosphere_apply(&u->atmo);
			glUniform1i(u->tex, 0);
			glUniform4fv(u->light, 4, light);
			if (!k)
				glUniform2f(u->screen, render_w, render_h);
			glEnableVertexAttribArray(3);
			glEnableVertexAttribArray(4);
		}
		if (!k) {
			glUniform1f(u->alpha_ref, pass == 0 ? -1.0f : pass == 1 ? ref : SOFT_EDGE_ALPHA);
			if (d->fade < 0.0f)
				glUniform4f(u->band, -2.0f, -1.0f, 1e5f, 2e5f);
			else if (d->fade > 0.0f)
				glUniform4f(u->band, -2.0f, -1.0f, dist + (1.0f - d->fade) * 1e4f - 1e4f, dist + (1.0f - d->fade) * 1e4f);
			else
				glUniform4fv(u->band, 1, band);
		}
		glUniform4f(u->pos, d->pos[0], d->pos[1], d->pos[2], 0.0f);
		glUniform4fv(u->bones, 192, d->bones);
		glUniform4f(u->tmul, d->tint_mul[0], d->tint_mul[1], d->tint_mul[2], 1.0f);
		glUniform4f(u->tadd, d->tint_add[0], d->tint_add[1], d->tint_add[2], 0.0f);
		if (d->tex != last_tex) {
			glBindTexture(GL_TEXTURE_2D, d->tex);
			last_tex = d->tex;
		}
		const uint8_t *v = d->skin_vbo ? NULL : d->skin_src; // (SrcVertex: 32 bytes; offsets into its buffer)
		glBindBuffer(GL_ARRAY_BUFFER, d->skin_vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, d->skin_ibo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, v);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, v + 12);
		glVertexAttribPointer(2, 4, GL_BYTE, GL_TRUE, 32, v + 20);
		glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, v + 24);
		glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, v + 28);
		const int hair = d->is_hair;
		if (hair) {
			glDisable(GL_CULL_FACE);
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GREATER, 0.5f);
		}
		glDrawElements(GL_TRIANGLES, d->index_count, GL_UNSIGNED_SHORT, d->skin_ibo ? NULL : d->indices);
		if (hair) {
			glEnable(GL_CULL_FACE);
			glDisable(GL_ALPHA_TEST);
			if (pass > 0)
				glDisable(GL_CULL_FACE);
		}
		st_dyn_draws++, st_dyn_tris += d->index_count / 3;
		if (!d->skin_vbo)
			st_dyn_bytes += d->vertex_count * 32u + d->index_count * 2u;
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	if (used) {
		glDisableVertexAttribArray(3);
		glDisableVertexAttribArray(4);
		glUseProgram(obj_prog);
	}
}

// The view matrix for a dynamic item turned about its position (ObjectsDynamic.rot): wvp x
// T(p) R T(-p), column-major
static void turned_wvp(const float wvp[16], const ObjectsDynamic *d, float out[16]) {
	const float *r = d->rot, *p = d->pos;
	float m[16] = {r[0], r[3], r[6], 0.0f, r[1], r[4], r[7], 0.0f, r[2], r[5], r[8], 0.0f, 0, 0, 0, 1.0f};
	for (int i = 0; i < 3; i++)
		m[12 + i] = p[i] - (r[i * 3] * p[0] + r[i * 3 + 1] * p[1] + r[i * 3 + 2] * p[2]);
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++)
			out[c * 4 + rr] = wvp[rr] * m[c * 4] + wvp[4 + rr] * m[c * 4 + 1] + wvp[8 + rr] * m[c * 4 + 2] + wvp[12 + rr] * m[c * 4 + 3];
}

void objects_draw_dynamic(const ObjectsDynamic *items, int count) {
	if (!count || !obj_prog)
		return;
	float wvp[16], band[4];
	shader_wvp(wvp);
	glUseProgram(obj_prog);
	glUniformMatrix4fv(u_wvp, 1, GL_FALSE, wvp);
	atmosphere_apply(&obj_atmo);
	glUniform1i(u_tex, 0);
	glUniform2f(u_screen, render_w, render_h);
	variant_band(GROUP_DETAIL, LOD_FULL, band); // dissolve with the small objects around them
	glUniform4fv(u_band, 1, band);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	for (int a = 0; a < 3; a++)
		glEnableVertexAttribArray(a);
	// Opaque (culled), then alpha-tested (two-sided), then the alpha-tested again blended
	// over themselves: the soft fringe a hard cut leaves jagged (hair, beards, lashes) fades
	// out instead (no depth writes: it only covers what's behind it)
	const ObjShaders *sv = &variants[atmosphere_variant()];
	uint8_t solid_done[512] = {0}; // (opaque and wholly within the band: drawn first, nothing discarded)
	GLuint last_tex = 0;
	if (sv->solid) {
		glUseProgram(sv->solid);
		glUniformMatrix4fv(sv->s_wvp, 1, GL_FALSE, wvp);
		atmosphere_apply(&sv->s_atmo);
		glUniform1i(sv->s_tex, 0);
		for (int i = 0; i < count && i < 512; i++) {
			const ObjectsDynamic *d = &items[i];
			const float dist = hypotf(d->pos[0] - draw_cam[0], d->pos[2] - draw_cam[1]);
			if (d->skin_src || d->alpha != 0 || !d->index_count || d->fade > 0.0f ||
			    (d->fade == 0.0f && dist + 3.0f > band[2]))
				continue;
			if (d->rot) {
				float tw[16];
				turned_wvp(wvp, d, tw);
				glUniformMatrix4fv(sv->s_wvp, 1, GL_FALSE, tw);
			}
			glUniform4f(sv->s_pos, d->pos[0], d->pos[1], d->pos[2], d->pos[3] / FINE_UNITS);
			glUniform4f(sv->s_uv, d->uv[0], d->uv[1], d->uv[2], 0.0f);
			if (d->tex != last_tex) {
				glBindTexture(GL_TEXTURE_2D, d->tex);
				last_tex = d->tex;
			}
			const ObjectVertex *v = d->vertices;
			glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(ObjectVertex), &v->x);
			glVertexAttribPointer(1, 2, GL_SHORT, GL_TRUE, sizeof(ObjectVertex), &v->u);
			glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ObjectVertex), &v->r);
			glDrawElements(GL_TRIANGLES, d->index_count, GL_UNSIGNED_SHORT, d->indices);
			if (d->rot)
				glUniformMatrix4fv(sv->s_wvp, 1, GL_FALSE, wvp);
			solid_done[i] = 1;
		}
		glUseProgram(obj_prog);
	}
	// The GPU-posed: solid, then opaque, before anyone's alpha passes
	draw_skinned(items, count, -1, wvp, band, solid_done);
	draw_skinned(items, count, 0, wvp, band, solid_done);
	last_tex = 0;
	glUseProgram(obj_prog);
	glUniformMatrix4fv(u_wvp, 1, GL_FALSE, wvp);
	glUniform4fv(u_band, 1, band);
	for (int pass = 0; pass < 3; pass++) {
		if (pass)
			glDisable(GL_CULL_FACE);
		if (pass == 2) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
		}
		glUniform1f(u_alpha_ref, pass == 0 ? -1.0f : pass == 1 ? 0.5f : SOFT_EDGE_ALPHA);
		float last_fade = 0.0f, last_ref = pass == 1 ? 0.5f : 0.0f;
		for (int i = 0; i < count; i++) {
			const ObjectsDynamic *d = &items[i];
			if (d->skin_src || (pass ? !ALPHA_CUT(d->alpha) : d->alpha != 0) || !d->index_count || (i < 512 && solid_done[i]))
				continue;
			if (pass) { // (its own cut; one already below the soft edge has nothing left to blend)
				const float ref = d->alpha > ALPHA_GLOW ? d->alpha / 255.0f : 0.5f;
				if (pass == 2 && ref <= SOFT_EDGE_ALPHA)
					continue;
				if (pass == 1 && ref != last_ref)
					glUniform1f(u_alpha_ref, last_ref = ref);
			}
			if (d->fade != last_fade) { // (a flyer seen from afar, or a body burning away)
				last_fade = d->fade;
				if (d->fade < 0.0f) {
					glUniform4f(u_band, -2.0f, -1.0f, 1e5f, 2e5f);
				} else if (d->fade > 0.0f) { // (that much of it dithered away, wherever it stands)
					const float dist = hypotf(d->pos[0] - draw_cam[0], d->pos[2] - draw_cam[1]);
					glUniform4f(u_band, -2.0f, -1.0f, dist + (1.0f - d->fade) * 1e4f - 1e4f, dist + (1.0f - d->fade) * 1e4f);
				} else {
					glUniform4fv(u_band, 1, band);
				}
			}
			if (d->rot) {
				float tw[16];
				turned_wvp(wvp, d, tw);
				glUniformMatrix4fv(u_wvp, 1, GL_FALSE, tw);
			}
			glUniform4f(u_pos, d->pos[0], d->pos[1], d->pos[2], d->pos[3] / FINE_UNITS);
			glUniform4f(u_uv, d->uv[0], d->uv[1], d->uv[2], 0.0f);
			if (d->tex != last_tex) {
				glBindTexture(GL_TEXTURE_2D, d->tex);
				last_tex = d->tex;
			}
			const ObjectVertex *v = d->vertices;
			glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(ObjectVertex), &v->x);
			glVertexAttribPointer(1, 2, GL_SHORT, GL_TRUE, sizeof(ObjectVertex), &v->u);
			glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ObjectVertex), &v->r);
			const int hair = d->is_hair;
			if (hair) {
				glDisable(GL_CULL_FACE);
				glEnable(GL_ALPHA_TEST);
				glAlphaFunc(GL_GREATER, 0.5f);
			}
			glDrawElements(GL_TRIANGLES, d->index_count, GL_UNSIGNED_SHORT, d->indices);
			if (d->rot)
				glUniformMatrix4fv(u_wvp, 1, GL_FALSE, wvp);
			if (hair) {
				glEnable(GL_CULL_FACE);
				glDisable(GL_ALPHA_TEST);
				if (pass > 0)
					glDisable(GL_CULL_FACE);
			}
		}
		if (last_fade != 0.0f)
			glUniform4fv(u_band, 1, band);
		if (pass) { // (the GPU-posed's alpha-tested and soft edges, in the same state)
			draw_skinned(items, count, pass, wvp, band, solid_done);
			last_tex = 0;
			glUseProgram(obj_prog);
			glUniformMatrix4fv(u_wvp, 1, GL_FALSE, wvp);
			glUniform4fv(u_band, 1, band);
			glUniform1f(u_alpha_ref, pass == 1 ? 0.5f : SOFT_EDGE_ALPHA);
		}
	}
	// Glowing light (a bound weapon): added over everything, a little flicker in its alpha
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);
	glUniform1f(u_alpha_ref, -1.0f);
	for (int i = 0; i < count; i++) {
		const ObjectsDynamic *d = &items[i];
		if (d->skin_src || d->alpha != ALPHA_GLOW || !d->index_count)
			continue;
		glUniform4f(u_pos, d->pos[0], d->pos[1], d->pos[2], d->pos[3] / FINE_UNITS);
		glUniform4f(u_uv, d->uv[0], d->uv[1], d->uv[2], 0.0f);
		if (d->tex != last_tex) {
			glBindTexture(GL_TEXTURE_2D, d->tex);
			last_tex = d->tex;
		}
		const ObjectVertex *v = d->vertices;
		glVertexAttribPointer(0, 4, GL_SHORT, GL_FALSE, sizeof(ObjectVertex), &v->x);
		glVertexAttribPointer(1, 2, GL_SHORT, GL_TRUE, sizeof(ObjectVertex), &v->u);
		glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ObjectVertex), &v->r);
		glDrawElements(GL_TRIANGLES, d->index_count, GL_UNSIGNED_SHORT, d->indices);
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_CULL_FACE);
	for (int a = 0; a < 3; a++)
		glDisableVertexAttribArray(a);
	glUseProgram(0);
}

unsigned int objects_texture(const char *name) {
	const GLuint tex = world_texture(name);
	for (int i = 0; i < texture_count; i++) // (held by someone outside the world's batches: kept)
		if (textures[i].tex == tex && !strcmp(textures[i].name, name))
			textures[i].pinned = 1;
	return tex;
}

// A texture by name for the world's own batches (an interior's tiles): freed with them
static GLuint world_texture(const char *name) {
	GLuint tex = texture_find(name);
	if (tex)
		return tex;
	char path[420];
	snprintf(path, sizeof(path), "%stextures/%s.sktex", pack_dir, name);
	long size = 0;
	void *data = sktex_read(path, &size);
	tex = texture_get(name, data, size);
	free(data);
	return tex;
}

static int group_has_water(const Group *grp) {
	for (int k = 0; k < grp->count; k++)
		if (grp->batches[k].kind == KIND_WATER)
			return 1;
	return 0;
}

#define WATER_FLAT_NY2 (0.8f * 0.8f) // water surfaces flatter than ~37 degrees

// The highest flat water surface of a group's water batches over (x, z) into *best
// (objects_water_at_y: the height asked from; NAN: any water at (x, z))
static float water_probe_y = NAN;

// Water above the one asking counts only if nothing solid is between them and its surface: a
// pool on the floor above a passage isn't water in the passage (Dustman's Cairn: you were
// suddenly deep "under" a pool upstairs)
static int water_reachable(float x, float z, float surface) {
	if (isnan(water_probe_y) || surface <= water_probe_y + 1.0f)
		return 1;
	const float from[3] = {x, water_probe_y + 0.5f, z}, to[3] = {x, surface - 0.05f, z};
	return collision_ray(from, to) >= 1.0f;
}

static void group_water_at(const Group *grp, float x, float z, float *best) {
	for (int k = 0; k < grp->count; k++) {
		const Batch *b = &grp->batches[k];
		if (!b->water || x < b->wmin[0] || x > b->wmax[0] || z < b->wmin[1] || z > b->wmax[1])
			continue;
		for (int t = 0; t < b->water_count; t++) {
			const float *v = &b->water[t * 9];
			// Barycentric in x, z
			float e1x = v[3] - v[0], e1z = v[5] - v[2], e2x = v[6] - v[0], e2z = v[8] - v[2];
			float det = e1x * e2z - e1z * e2x;
			if (fabsf(det) < 1e-9f)
				continue;
			// Only surfaces you can float on: a waterfall's face isn't one (you swam up it)
			float e1y = v[4] - v[1], e2y = v[7] - v[1];
			float ny = e1z * e2x - e1x * e2z; // y of the cross product e1 x e2 (unnormalized)
			float nx = e1y * e2z - e1z * e2y, nz = e1x * e2y - e1y * e2x;
			if (ny * ny < WATER_FLAT_NY2 * (nx * nx + ny * ny + nz * nz))
				continue;
			float px = x - v[0], pz = z - v[2];
			float u = (px * e2z - pz * e2x) / det, w = (e1x * pz - e1z * px) / det;
			if (u < 0.0f || w < 0.0f || u + w > 1.0f)
				continue;
			float y = v[1] + u * (v[4] - v[1]) + w * (v[7] - v[1]);
			if (y > *best && water_reachable(x, z, y))
				*best = y;
		}
	}
}

int objects_water_at_y(float x, float y, float z, float *surface) {
	water_probe_y = y;
	const int found = objects_water_at(x, z, surface);
	water_probe_y = NAN;
	return found;
}

int objects_water_at(float x, float z, float *surface) {
	float best = -INFINITY;
	if (indoors) { // (a dungeon's streams and pools: Bleak Falls Sanctum's creek)
		for (int t = 0; t < itile_count; t++) {
			const ITile *it = &itiles[t];
			if (x >= it->min[0] && x <= it->max[0] && z >= it->min[2] && z <= it->max[2] && it->grp.has_water)
				group_water_at(&it->grp, x, z, &best);
		}
	} else {
		for (int i = 0; i < MAX_CELLS; i++) {
			const CellObjects *c = &cells[i];
			if (!c->used)
				continue;
			for (int g = 0; g < GROUP_COUNT; g++)
				for (int l = 0; l < LOD_COUNT; l++) {
					const Group *grp = &c->var[g][l];
					// (its own flag and bounds first: walking every batch of every cell for this,
					// twice a frame, cost more than the rest of the frame's game logic)
					if (!grp->has_water || x < grp->lo[0] - 1.0f || x > grp->hi[0] + 1.0f || z < grp->lo[1] - 1.0f ||
					    z > grp->hi[1] + 1.0f)
						continue;
					if (group_has_water(grp)) { // (the finest level with water only)
						group_water_at(grp, x, z, &best);
						break;
					}
				}
		}
	}
	if (best == -INFINITY)
		return 0;
	*surface = best;
	return 1;
}

// Water isn't dissolved between levels (it's see-through, so two levels would show
// doubled), so each cell draws one: the finest level that has water and whose distance
// band reaches the cell. (Choosing by cell ring dropped rivers whenever a step across a
// cell line asked for a level that wasn't built or had no water.)
void objects_draw_water(float cam_x, float cam_z) {
	if (!water_begin())
		return;
	if (indoors) { // (a dungeon's streams and pools, near enough and in view)
		for (int t = 0; t < itile_count; t++) {
			const ITile *it = &itiles[t];
			if (!it->grp.has_water || !tile_in_sight(it) || box_distance(it->min, it->max, cam_x, it->min[1], cam_z) > in_radius ||
			    !box_visible(view_wvp, it->min, it->max))
				continue;
			for (int k = 0; k < it->grp.count; k++) {
				const Batch *b = &it->grp.batches[k];
				if (b->kind != KIND_WATER)
					continue;
				glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
				water_set_transform(b->pos[0], b->pos[1], b->pos[2], b->pos[3]);
				glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, sizeof(PackedVertex), (void *)offsetof(PackedVertex, x));
				glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT, 0);
			}
		}
		water_end();
		return;
	}
	for (int i = 0; i < MAX_CELLS; i++) {
		const CellObjects *c = &cells[i];
		if (!c->used)
			continue;
		for (int g = 0; g < GROUP_COUNT; g++) {
			if (!group_visible(c, g))
				continue;
			float dmin, dmax;
			cell_distance_range(c, cam_x, cam_z, group_overhang_m[g], &dmin, &dmax);
			const Group *grp = NULL;
			for (int l = 0; l < LOD_COUNT && !grp; l++) {
				float band[4];
				variant_band(g, l, band);
				if (dmin <= band[3] && group_has_water(&c->var[g][l]))
					grp = &c->var[g][l];
			}
			for (int k = 0; grp && k < grp->count; k++) {
				const Batch *b = &grp->batches[k];
				if (b->kind != KIND_WATER)
					continue;
				glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ibo);
				water_set_transform(b->pos[0], b->pos[1], b->pos[2], b->pos[3]);
				glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, sizeof(PackedVertex), (void *)offsetof(PackedVertex, x));
				glDrawElements(GL_TRIANGLES, b->index_count, GL_UNSIGNED_SHORT, 0);
			}
		}
	}
	water_end();
}

// --- Fire and glow sprites ----------------------------------------------------------

#define FX_MAX_QUADS 512
#define FIRE_FRAMES 8 // fx_fire atlas: 4 columns x 2 rows of flame frames
#define FIRE_FPS 12.0f

typedef struct {
	float x, y, z, u, v;
	uint8_t r, g, b, a;
} FxVertex;

static FxVertex fx_verts[FX_MAX_QUADS * 6];

// Camera-facing (around the vertical axis) quad from bottom-center p.
static int push_quad(int n, const float p[3], const float right[3], float w, float h, float u0, float v0,
                     float u1, float v1, const uint8_t rgba[4]) {
	if (n + 6 > FX_MAX_QUADS * 6)
		return n;
	float hw = w * 0.5f;
	FxVertex c[4] = {
		{p[0] - right[0] * hw, p[1], p[2] - right[2] * hw, u0, v1, 0, 0, 0, 0},
		{p[0] + right[0] * hw, p[1], p[2] + right[2] * hw, u1, v1, 0, 0, 0, 0},
		{p[0] + right[0] * hw, p[1] + h, p[2] + right[2] * hw, u1, v0, 0, 0, 0, 0},
		{p[0] - right[0] * hw, p[1] + h, p[2] - right[2] * hw, u0, v0, 0, 0, 0, 0},
	};
	for (int i = 0; i < 4; i++)
		c[i].r = rgba[0], c[i].g = rgba[1], c[i].b = rgba[2], c[i].a = rgba[3];
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	for (int i = 0; i < 6; i++)
		fx_verts[n++] = c[order[i]];
	return n;
}

static void draw_fx_batch(int count, GLuint tex) {
	if (!count || !fx_prog)
		return;
	glBindTexture(GL_TEXTURE_2D, tex);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FxVertex), &fx_verts[0].x);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FxVertex), &fx_verts[0].u);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FxVertex), &fx_verts[0].r);
	glDrawArrays(GL_TRIANGLES, 0, count);
}

static int torch_on;
static float torch_at[3];

void objects_fx_torch(const float pos[3]) {
	torch_on = pos != NULL;
	if (pos)
		memcpy(torch_at, pos, sizeof(torch_at));
}

void objects_draw_fx(float cam_x, float cam_z, float yaw_deg, float time_s) {
	use_variant();
	int pcx = terrain_cell_x(cam_x), pcy = terrain_cell_y(cam_z);
	float yaw = yaw_deg * (float)M_PI / 180.0f;
	const float right[3] = {cosf(yaw), 0.0f, sinf(yaw)};

	// Pass 1 fires, pass 2 glow halos (different textures)
	for (int pass = 0; pass < 2; pass++) {
		int n = 0;
		for (int i = indoors ? -1 : 0; i < (indoors ? 0 : MAX_CELLS); i++) {
			// Indoors: the interior's points (i = -1); outdoors: the nearby cells'
			const FxPoint *fx = in_fx;
			int fx_count = in_fx_count;
			if (i >= 0) {
				const CellObjects *c = &cells[i];
				if (!c->used || !c->fx_count || ring_of(c, pcx, pcy) > OBJECTS_DETAIL_RADIUS ||
				    !terrain_cell_in_view(c->cx, c->cy))
					continue;
				fx = c->fx, fx_count = c->fx_count;
			}
			for (int k = 0; k < fx_count; k++) {
				const FxPoint *p = &fx[k];
				float flicker = 0.8f + 0.2f * sinf(time_s * 13.0f + p->seed) * sinf(time_s * 7.3f + p->seed * 2.0f);
				float pos[3] = {p->x, p->y, p->z};
				if (pass == 0 && p->kind == FX_FIRE) {
					// Two layered flame cards on different flipbook frames
					for (int layer = 0; layer < 2; layer++) {
						int frame = ((int)(time_s * FIRE_FPS + p->seed * 3.0f) + layer * 3) % FIRE_FRAMES;
						float u0 = (frame % 4) * 0.25f, v0 = (frame / 4) * 0.5f;
						float sw = layer ? 0.75f : 1.0f, sh = (layer ? 0.85f : 1.0f) * flicker;
						uint8_t col[4] = {(uint8_t)(128 * flicker), (uint8_t)(110 * flicker), (uint8_t)(80 * flicker), 255};
						n = push_quad(n, pos, right, p->w * 1.3f * sw, p->h * 1.4f * sh, u0, v0, u0 + 0.25f, v0 + 0.5f, col);
					}
				} else if (pass == 1 && p->kind == FX_GLOW) {
					float s = p->w * (0.9f + 0.1f * flicker);
					float base[3] = {pos[0], pos[1] - s * 0.5f, pos[2]};
					uint8_t col[4] = {(uint8_t)(70 * flicker), (uint8_t)(48 * flicker), (uint8_t)(24 * flicker), 255};
					n = push_quad(n, base, right, s, s, 0.0f, 0.0f, 1.0f, 1.0f, col);
				}
			}
		}
		// The held torch's flame and its halo
		if (torch_on) {
			float flicker = 0.8f + 0.2f * sinf(time_s * 15.0f) * sinf(time_s * 8.1f + 1.3f);
			if (pass == 0) {
				for (int layer = 0; layer < 2; layer++) {
					int frame = ((int)(time_s * FIRE_FPS) + layer * 3) % FIRE_FRAMES;
					float u0 = (frame % 4) * 0.25f, v0 = (frame / 4) * 0.5f;
					float base[3] = {torch_at[0], torch_at[1] - 0.06f, torch_at[2]};
					uint8_t col[4] = {(uint8_t)(128 * flicker), (uint8_t)(110 * flicker), (uint8_t)(80 * flicker), 255};
					n = push_quad(n, base, right, (layer ? 0.14f : 0.2f), (layer ? 0.26f : 0.34f) * flicker, u0, v0,
					              u0 + 0.25f, v0 + 0.5f, col);
				}
			} else {
				float s = 0.7f * (0.9f + 0.1f * flicker);
				float base[3] = {torch_at[0], torch_at[1] - s * 0.4f, torch_at[2]};
				uint8_t col[4] = {(uint8_t)(60 * flicker), (uint8_t)(40 * flicker), (uint8_t)(18 * flicker), 255};
				n = push_quad(n, base, right, s, s, 0.0f, 0.0f, 1.0f, 1.0f, col);
			}
		}
		if (pass == 0) {
			// Additive, depth-tested but not written; the shader fades them out in the fog
			float wvp[16];
			shader_wvp(wvp);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glDisable(GL_CULL_FACE);
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			if (fx_prog) {
				glUseProgram(fx_prog);
				glUniformMatrix4fv(fx_wvp, 1, GL_FALSE, wvp);
				atmosphere_apply(&fx_atmo);
				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);
			}
			draw_fx_batch(n, fx_fire_tex);
		} else {
			draw_fx_batch(n, fx_glow_tex);
			if (fx_prog) {
				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				glUseProgram(0);
			}
			glDisable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_TRUE);
			glEnable(GL_CULL_FACE);
		}
	}
}

// Sprite flipbooks: columns x rows of frames
static const uint8_t sprite_grid[SPRITE_KINDS][2] = {{4, 2}, {8, 4}, {1, 1}, {1, 1}, {1, 1}, {4, 4}, {1, 1},
                                                     {1, 1}, {2, 1}, {2, 2}};

void objects_draw_sprites(const ObjectsSprite *s, int n, float yaw_deg, float pitch_deg) {
	if (!n || !fx_prog)
		return;
	use_variant();
	const float y = yaw_deg * (float)M_PI / 180.0f, p = pitch_deg * (float)M_PI / 180.0f;
	const float right[3] = {cosf(y), 0.0f, sinf(y)};
	const float up[3] = {-sinf(y) * sinf(p), cosf(p), cosf(y) * sinf(p)};
	float wvp[16];
	shader_wvp(wvp);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glUseProgram(fx_prog);
	glUniformMatrix4fv(fx_wvp, 1, GL_FALSE, wvp);
	atmosphere_apply(&fx_atmo);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	static const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
	for (int kind = 0; kind < SPRITE_KINDS; kind++) {
		if (!sprite_tex[kind])
			continue;
		const int cols = sprite_grid[kind][0], rows = sprite_grid[kind][1];
		int count = 0;
		for (int i = 0; i < n && count + 6 <= FX_MAX_QUADS * 6; i++) {
			const ObjectsSprite *q = &s[i];
			if (q->kind != kind)
				continue;
			// The quad's axes: the view's right and up (or level), turned by its angle
			const float c = cosf(q->angle), sn = sinf(q->angle), h = q->size * 0.5f;
			const float hy = h * (q->stretch > 0.0f ? q->stretch : 1.0f);
			float ax[3], ay[3];
			for (int k = 0; k < 3; k++) {
				const float r = q->flat ? (k == 0 ? 1.0f : 0.0f) : right[k];
				const float u = q->flat ? (k == 2 ? 1.0f : 0.0f) : up[k];
				ax[k] = (r * c + u * sn) * h, ay[k] = (u * c - r * sn) * hy;
			}
			const int f = q->frame % (cols * rows);
			const float u0 = (float)(f % cols) / cols, v0 = (float)(f / cols) / rows;
			FxVertex v[4];
			for (int k = 0; k < 4; k++) {
				const float cx = corner[k][0], cy = corner[k][1];
				v[k].x = q->pos[0] + ax[0] * cx + ay[0] * cy;
				v[k].y = q->pos[1] + ax[1] * cx + ay[1] * cy;
				v[k].z = q->pos[2] + ax[2] * cx + ay[2] * cy;
				v[k].u = u0 + (cx > 0 ? 1.0f : 0.0f) / cols;
				v[k].v = v0 + (cy > 0 ? 0.0f : 1.0f) / rows;
				v[k].r = q->rgba[0], v[k].g = q->rgba[1], v[k].b = q->rgba[2], v[k].a = q->rgba[3];
			}
			for (int k = 0; k < 6; k++)
				fx_verts[count++] = v[order[k]];
		}
		draw_fx_batch(count, sprite_tex[kind]);
	}
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
}

int objects_is_fish_model(const char *name) {
	return strstr(name, "fxambwater") && strstr(name, "salmon");
}

int objects_fish_near(float x, float z, float radius, ObjectsFish *out, int max) {
	int n = 0;
	if (indoors)
		return 0;
	for (int i = 0; i < MAX_CELLS && n < max; i++) {
		const CellObjects *c = &cells[i];
		if (!c->used || !c->entries_state)
			continue;
		for (int k = 0; k < c->entry_count && n < max; k++) {
			const Entry *e = &c->entries[k];
			if (e->model >= model_count || !models[e->model].fish)
				continue;
			float dx = e->t[0] - x, dz = e->t[2] - z;
			if (dx * dx + dz * dz > radius * radius)
				continue;
			ObjectsFish *f = &out[n++];
			f->model = e->model;
			memcpy(f->m, e->m, sizeof(f->m));
			memcpy(f->t, e->t, sizeof(f->t));
			f->id = (unsigned)(c->cx * 73856093) ^ (unsigned)(c->cy * 19349663) ^ (unsigned)k * 83492791u;
		}
	}
	return n;
}

const char *objects_model_name(int index) {
	return index >= 0 && index < model_count ? models[index].name : "?";
}

// --- Giving video memory back ----------------------------------------------------------
// Textures were kept for the whole session: after a while of exploring the 96 MB filled and
// a new place's (a dungeon's, a city's) went to main memory, which the GPU reads far slower -
// Dustman's Cairn ran at 11 fps. Here, with nothing streaming, the textures only the world's
// batches use and none of them still do are freed (and, going indoors, the outside's objects
// with them: streamed again when you come out).

#define TEX_IDS 8192 // (vitaGL's texture names are small numbers: found by name, not by search)
static int16_t slot_of[TEX_IDS]; // texture name -> its table slot + 1

static void mark_group(const Group *g, uint8_t *used) {
	for (int b = 0; b < g->count; b++) {
		const GLuint t = g->batches[b].tex;
		if (t < TEX_IDS) {
			if (slot_of[t])
				used[slot_of[t] - 1] = 1;
			continue;
		}
		for (int i = 0; i < texture_count; i++)
			if (textures[i].tex == t) {
				used[i] = 1;
				break;
			}
	}
}

int objects_release(int outside_too) {
	stream_flush(); // (no build in flight holds a texture the groups don't show)
	if (outside_too) {
		for (int i = 0; i < MAX_CELLS; i++)
			if (cells[i].used)
				unload_cell(&cells[i]);
		world_changed = 1;
	}
	static uint8_t used[MAX_TEXTURES];
	memset(used, 0, sizeof(used));
	memset(slot_of, 0, sizeof(slot_of));
	for (int i = 0; i < texture_count; i++)
		if (textures[i].tex < TEX_IDS)
			slot_of[textures[i].tex] = (int16_t)(i + 1);
	for (int i = 0; i < MAX_CELLS; i++)
		if (cells[i].used)
			for (int g = 0; g < GROUP_COUNT; g++)
				for (int l = 0; l < LOD_COUNT; l++)
					mark_group(&cells[i].var[g][l], used);
	for (int t = 0; t < itile_count; t++)
		mark_group(&itiles[t].grp, used);
	const GLuint white = sktex_white();
	int kept = 0, freed = 0;
	for (int i = 0; i < texture_count; i++) {
		if (!used[i] && !textures[i].pinned && textures[i].tex != white) {
			glDeleteTextures(1, &textures[i].tex);
			freed++;
			continue;
		}
		if (kept != i)
			textures[kept] = textures[i];
		kept++;
	}
	texture_count = kept;
	__sync_synchronize();
	return freed;
}

unsigned objects_frame_tris(void) {
	return (unsigned)frame_tris;
}

int objects_stats(char *out, int size) {
	const unsigned long long f = st_frames ? st_frames : 1;
	const int n = snprintf(out, size, " world %llu draws %llu k tris | people %llu draws %llu k tris %llu KB copied",
	                       st_world_draws / f, st_world_tris / f / 1000, st_dyn_draws / f, st_dyn_tris / f / 1000,
	                       st_dyn_bytes / f / 1024);
	int k = n;
	k += snprintf(out + k, size - k, " | k tris small/large/huge x full,mid,far:");
	for (int g = 0; g < GROUP_COUNT; g++)
		for (int l = 0; l < LOD_COUNT; l++)
			k += snprintf(out + k, size - k, " %llu", st_level_tris[g][l] / f / 1000), st_level_tris[g][l] = 0;
	st_world_draws = st_world_tris = st_dyn_draws = st_dyn_tris = st_dyn_bytes = st_frames = 0;
	tris_mark = 0;
	return k;
}
