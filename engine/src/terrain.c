#include "terrain.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include <psp2/kernel/clib.h>

#include "atmosphere.h"
#include "lighting.h"
#include "shader.h"
#include "sktex.h"
#include "stream.h"
#include "water.h"
#include "worlds.h"

#define GRID 33
#define GRID_VERTS (GRID * GRID)
#define SKIRT_VERTS (4 * GRID)
#define CELL_VERTS (GRID_VERTS + SKIRT_VERTS)
#define SKIRT_DEPTH_M 3.0f
#define LOD_STEP 4
#define FAR_STEP 8
#define FAR_TEXTURE_PX 32
#define DETAIL_REPEATS 14.0f // detail texture repeats per cell (~4.2 m each)
#define SKCL_VERSION 1
#define MAX_SLOTS 400 // (2 * (FAR_RADIUS + 1) + 1)^2 = 361 with one ring of hysteresis

typedef struct {
	float x, y, z;
	float u0, v0; // color map
	float u1, v1; // detail texture
	uint8_t r, g, b, a; // baked light x Skyrim's vertex tint
} TerrainVertex;

typedef struct {
	int used, absent;
	int loading;     // geometry is being streamed in: not drawn, no ground yet
	int tex_loading; // a color map upgrade is in flight
	unsigned gen;    // identifies this use of the slot (stale stream results are dropped)
	int cx, cy;
	GLuint vbo, tex;
	GLuint vbo_lod, vbo_far; // compact copies for the coarser levels (compact_vbo)
	int tex_full; // 0 while only the low-res far copy of the color map is loaded
	float water;  // meters, NAN if none
	float heights[GRID_VERTS]; // meters, row 0 = south
} Cell;

typedef struct {
	GLuint ibo;
	int count;
} IndexList;

// Colour map x vertex light x 2, times the detail texture x 2 (it is centred on mid-grey),
// then the shared atmosphere fog (computed per vertex, PS2-style)
static const char *TERRAIN_VS = // atmosphere_cg() is prepended
	"uniform float4x4 u_wvp;\n"
	"void main(float3 position, float2 uv0, float2 uv1, float4 color,\n"
	"	float4 out vPosition : POSITION, float2 out vUv0 : TEXCOORD0, float2 out vUv1 : TEXCOORD1,\n"
	"	float4 out vColor : COLOR, float4 out vFog : TEXCOORD2, float3 out vTorch : TEXCOORD3,\n"
	"	float3 out vTorchColor : TEXCOORD4) {\n"
	"	vPosition = mul(float4(position, 1.0f), u_wvp);\n"
	"	vUv0 = uv0;\n"
	"	vUv1 = uv1;\n"
	"	vColor = float4(atmo_light(color.rgb), 1.0f);\n"
	"	vFog = atmo_fog(position);\n"
	"	vTorch = atmo_torch(position);\n"
	"	vTorchColor = u_torchColor;\n"
	"}\n";

static const char *TERRAIN_FS =
	"uniform sampler2D u_color;\n"
	"uniform sampler2D u_detail;\n"
	"float4 main(float2 vUv0 : TEXCOORD0, float2 vUv1 : TEXCOORD1, float4 vColor : COLOR,\n"
	"	float4 vFog : TEXCOORD2, float3 vTorch : TEXCOORD3, float3 vTorchColor : TEXCOORD4) {\n"
	"	float3 t = tex2D(u_color, vUv0).rgb * tex2D(u_detail, vUv1).rgb * 2.0f;\n"
	"	float3 c = t * vColor.rgb * 2.0f;\n"
	TORCH_CG
	TONEMAP_CG
	"	return float4(lerp(c, vFog.rgb, vFog.a), 1.0f);\n"
	"}\n";

// Without the torch light, if the shader above doesn't compile
static const char *TERRAIN_VS_PLAIN = // atmosphere_cg() is prepended
	"uniform float4x4 u_wvp;\n"
	"void main(float3 position, float2 uv0, float2 uv1, float4 color,\n"
	"	float4 out vPosition : POSITION, float2 out vUv0 : TEXCOORD0, float2 out vUv1 : TEXCOORD1,\n"
	"	float4 out vColor : COLOR, float4 out vFog : TEXCOORD2) {\n"
	"	vPosition = mul(float4(position, 1.0f), u_wvp);\n"
	"	vUv0 = uv0;\n"
	"	vUv1 = uv1;\n"
	"	vColor = float4(atmo_light(color.rgb), 1.0f);\n"
	"	vFog = atmo_fog(position);\n"
	"}\n";
static const char *TERRAIN_FS_PLAIN =
	"uniform sampler2D u_color;\n"
	"uniform sampler2D u_detail;\n"
	"float4 main(float2 vUv0 : TEXCOORD0, float2 vUv1 : TEXCOORD1, float4 vColor : COLOR,\n"
	"	float4 vFog : TEXCOORD2) {\n"
	"	float3 c = tex2D(u_color, vUv0).rgb * vColor.rgb * tex2D(u_detail, vUv1).rgb * 4.0f;\n"
	TONEMAP_CG
	"	return float4(lerp(c, vFog.rgb, vFog.a), 1.0f);\n"
	"}\n";

static GLuint terrain_progs[ATMO_VARIANTS], terrain_prog;
static GLint terrain_wvps[ATMO_VARIANTS], terrain_wvp;
static AtmoUniforms terrain_atmos[ATMO_VARIANTS], terrain_atmo;

static char pack_dir[128], world_name[32];
static Cell slots[MAX_SLOTS];
static unsigned next_gen = 1;
static IndexList full_indices, lod_indices, far_indices;
static GLuint detail_tex;

static float cell_origin_x(int cx) { return cx * CELL_SIZE_M; }
static float cell_origin_z(int cy) { return -cy * CELL_SIZE_M; }

static struct { float x, z, fx, fz, rx, rz, slope; } view;

void terrain_set_view(float cam_x, float cam_z, float yaw_deg, float half_fov_x_deg) {
	float y = yaw_deg * (float)M_PI / 180.0f;
	view.x = cam_x, view.z = cam_z;
	view.fx = sinf(y), view.fz = -cosf(y); // forward (yaw 0 looks down -z)
	view.rx = cosf(y), view.rz = sinf(y);  // right
	view.slope = tanf(half_fov_x_deg * (float)M_PI / 180.0f);
}

// Conservative: a cell is culled only when all four corners are outside the same
// side of the horizontal frustum (left, right or behind the camera).
static int active = 1;

void terrain_set_active(int on) {
	active = on;
}

int terrain_rect_in_view(float x0, float z0, float x1, float z1) {
	if (!active)
		return 1;
	int left = 0, right = 0, behind = 0;
	for (int i = 0; i < 4; i++) {
		float dx = ((i & 1) ? x1 : x0) - view.x;
		float dz = ((i >> 1) ? z1 : z0) - view.z;
		float zv = dx * view.fx + dz * view.fz;
		float xv = dx * view.rx + dz * view.rz;
		left += xv < -view.slope * zv;
		right += xv > view.slope * zv;
		behind += zv < 0.0f;
	}
	return left < 4 && right < 4 && behind < 4;
}

int terrain_cell_in_view(int cx, int cy) {
	return terrain_rect_in_view(cx * CELL_SIZE_M, -cy * CELL_SIZE_M, (cx + 1) * CELL_SIZE_M, -(cy + 1) * CELL_SIZE_M);
}

int terrain_cell_x(float x) { return (int)floorf(x / CELL_SIZE_M); }
int terrain_cell_y(float z) { return (int)floorf(-z / CELL_SIZE_M); }

// --- Shared index buffers -----------------------------------------------------

static int vidx(int i, int j) { return j * GRID + i; } // i east, j north

// Skirt vertex k along edge e (0 south, 1 north, 2 west, 3 east)
static int sidx(int e, int k) { return GRID_VERTS + e * GRID + k; }
static int edge_vertex(int e, int k) {
	switch (e) {
	case 0: return vidx(k, 0);
	case 1: return vidx(k, GRID - 1);
	case 2: return vidx(0, k);
	default: return vidx(GRID - 1, k);
	}
}

// The coarser levels draw from compact copies of the grid (every step-th vertex: g = 32 /
// step + 1 per side, then their skirts), not the full one: Vita3K copies a draw's whole
// vertex buffer every frame, and the distant cells' full grids were several MB a frame.
#define LOD_GRID ((GRID - 1) / LOD_STEP + 1)
#define FAR_GRID ((GRID - 1) / FAR_STEP + 1)

// Index list for a grid of g x g vertices followed by its four skirts (g each)
static void build_index_list(IndexList *list, int g) {
	int quads = g - 1;
	int max = quads * quads * 6 + 4 * quads * 12;
	uint16_t *idx = malloc(max * sizeof(uint16_t));
	int n = 0;
#define V(i, j) ((uint16_t)((j) * g + (i)))
#define S(e, k) ((uint16_t)(g * g + (e) * g + (k)))
	for (int j = 0; j < g - 1; j++) {
		for (int i = 0; i < g - 1; i++) {
			// Diagonal v00-v11; terrain_height_at() interpolates the same triangles
			idx[n++] = V(i, j), idx[n++] = V(i + 1, j), idx[n++] = V(i + 1, j + 1);
			idx[n++] = V(i, j), idx[n++] = V(i + 1, j + 1), idx[n++] = V(i, j + 1);
		}
	}
	// Skirts hang below every edge to hide cracks between full and LOD neighbors.
	// Emitted double-sided so their facing doesn't matter.
	for (int e = 0; e < 4; e++) {
		for (int k = 0; k < g - 1; k++) {
			uint16_t a = e == 0 ? V(k, 0) : e == 1 ? V(k, g - 1) : e == 2 ? V(0, k) : V(g - 1, k);
			uint16_t b = e == 0 ? V(k + 1, 0) : e == 1 ? V(k + 1, g - 1) : e == 2 ? V(0, k + 1) : V(g - 1, k + 1);
			uint16_t sa = S(e, k), sb = S(e, k + 1);
			idx[n++] = a, idx[n++] = b, idx[n++] = sb;
			idx[n++] = a, idx[n++] = sb, idx[n++] = sa;
			idx[n++] = a, idx[n++] = sb, idx[n++] = b;
			idx[n++] = a, idx[n++] = sa, idx[n++] = sb;
		}
	}
#undef V
#undef S
	glGenBuffers(1, &list->ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, list->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, n * sizeof(uint16_t), idx, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	list->count = n;
	free(idx);
}

// A compact copy of a cell's vertices: every step-th one of the grid, then of its skirts
static GLuint compact_vbo(const TerrainVertex *full, int step) {
	int g = (GRID - 1) / step + 1;
	TerrainVertex *v = malloc((g * g + 4 * g) * sizeof(TerrainVertex));
	if (!v)
		return 0;
	for (int j = 0; j < g; j++)
		for (int i = 0; i < g; i++)
			v[j * g + i] = full[vidx(i * step, j * step)];
	for (int e = 0; e < 4; e++)
		for (int k = 0; k < g; k++)
			v[g * g + e * g + k] = full[sidx(e, k * step)];
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, (g * g + 4 * g) * sizeof(TerrainVertex), v, GL_STATIC_DRAW);
	free(v);
	return vbo;
}

// --- Cell loading ------------------------------------------------------------

// --- Streaming -------------------------------------------------------------------
// A cell's file is read and its vertices built and lit on the stream thread; the render
// thread only creates the vertex buffer and uploads the color map.

typedef struct {
	StreamJob base;
	int slot;
	unsigned gen;
	int cx, cy;
	int geometry;      // 0: color map upgrade only
	int tex_full;      // full-resolution color map (else the small far copy)
	int ok;            // geometry: the cell file was valid
	float water;
	float heights[GRID_VERTS];
	TerrainVertex *verts;
	void *tex_data;
	long tex_size;
} TerrainJob;

static int build_cell_vertices(TerrainJob *j) {
	char path[192];
	snprintf(path, sizeof(path), "%sterrain/%s_%d_%d.skcell", pack_dir, world_name, j->cx, j->cy);
	long size;
	uint8_t *buf = sktex_read(path, &size); // (plain file read)
	const long expected = 20 + GRID_VERTS * (4 + 3 + 3);
	if (!buf || size != expected || memcmp(buf, "SKCL", 4) || *(uint16_t *)(buf + 4) != SKCL_VERSION) {
		free(buf);
		return 0;
	}
	memcpy(&j->water, buf + 16, 4);
	memcpy(j->heights, buf + 20, sizeof(j->heights));
	const int8_t *normals = (const int8_t *)(buf + 20 + GRID_VERTS * 4);
	const uint8_t *colors = buf + 20 + GRID_VERTS * 7;

	TerrainVertex *v = malloc(CELL_VERTS * sizeof(TerrainVertex));
	if (!v) {
		free(buf);
		return 0;
	}
	const float spacing = CELL_SIZE_M / (GRID - 1);
	const float ox = cell_origin_x(j->cx), oz = cell_origin_z(j->cy);
	for (int jj = 0; jj < GRID; jj++) {
		for (int i = 0; i < GRID; i++) {
			int k = vidx(i, jj);
			TerrainVertex *t = &v[k];
			t->x = ox + i * spacing;
			t->y = j->heights[k];
			t->z = oz - jj * spacing;
			t->u0 = (float)i / (GRID - 1);
			t->v0 = 1.0f - (float)jj / (GRID - 1); // color map row 0 = north
			t->u1 = t->u0 * DETAIL_REPEATS;
			t->v1 = (float)jj / (GRID - 1) * DETAIL_REPEATS;
			float n[3] = {normals[k * 3] / 127.0f, normals[k * 3 + 1] / 127.0f, normals[k * 3 + 2] / 127.0f};
			// Skyrim's vertex colours tint the ground (dirt patches, shading under trees)
			uint8_t rgb[3];
			lighting_bake(n, colors[k * 3] / 255.0f, colors[k * 3 + 1] / 255.0f, colors[k * 3 + 2] / 255.0f, rgb);
			t->r = rgb[0], t->g = rgb[1], t->b = rgb[2], t->a = 255;
		}
	}
	for (int e = 0; e < 4; e++) {
		for (int k = 0; k < GRID; k++) {
			TerrainVertex sv = v[edge_vertex(e, k)];
			sv.y -= SKIRT_DEPTH_M;
			v[sidx(e, k)] = sv;
		}
	}
	free(buf);
	j->verts = v;
	return 1;
}

static void cell_job_work(StreamJob *base) {
	TerrainJob *j = (TerrainJob *)base;
	if (j->geometry)
		j->ok = build_cell_vertices(j);
	if (!j->geometry || j->ok) {
		char path[192];
		snprintf(path, sizeof(path), "%sterrain/%s_%d_%d.sktex", pack_dir, world_name, j->cx, j->cy);
		j->tex_data = sktex_read(path, &j->tex_size);
	}
}

static void set_cell_texture(Cell *c, const TerrainJob *j) {
	GLuint tex = j->tex_data ? sktex_upload(j->tex_data, j->tex_size, j->tex_full ? 0 : FAR_TEXTURE_PX, "terrain") : 0;
	if (!tex)
		tex = sktex_white();
	// Clamp so bilinear filtering doesn't bleed the opposite edge across cell seams
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (c->tex)
		glDeleteTextures(1, &c->tex);
	c->tex = tex;
	c->tex_full = j->tex_full;
}

static int cell_job_finish(StreamJob *base, unsigned long long deadline) {
	(void)deadline;
	TerrainJob *j = (TerrainJob *)base;
	Cell *c = &slots[j->slot];
	if (!c->used || c->gen != j->gen)
		return 1; // the cell was evicted meanwhile
	if (!j->geometry) {
		c->tex_loading = 0;
		set_cell_texture(c, j);
		return 1;
	}
	c->loading = 0;
	if (!j->ok) {
		c->absent = 1; // outside the converted region
		return 1;
	}
	c->water = fabsf(j->water) < 10000.0f ? j->water : NAN; // (Skyrim's "no water" marker, kilometres up)
	memcpy(c->heights, j->heights, sizeof(c->heights));
	glGenBuffers(1, &c->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
	glBufferData(GL_ARRAY_BUFFER, CELL_VERTS * sizeof(TerrainVertex), j->verts, GL_STATIC_DRAW);
	c->vbo_lod = compact_vbo(j->verts, LOD_STEP);
	c->vbo_far = compact_vbo(j->verts, FAR_STEP);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	set_cell_texture(c, j);
	return 1;
}

static void cell_job_discard(StreamJob *base) {
	TerrainJob *j = (TerrainJob *)base;
	free(j->verts);
	free(j->tex_data);
	free(j);
}

static int far_radius, lod_radius; // (below)
static volatile int job_pcx, job_pcy; // player cell, for job priorities (worker thread)

// Geometry is seen out to the far ring, full colour maps inside the LOD ring
static int cell_job_priority(StreamJob *base) {
	const TerrainJob *j = (const TerrainJob *)base;
	int dx = abs(j->cx - job_pcx), dy = abs(j->cy - job_pcy), ring = dx > dy ? dx : dy;
	return STREAM_PRIORITY(ring - (j->geometry ? far_radius : lod_radius), ring);
}

static void submit_cell_job(Cell *c, int geometry, int tex_full) {
	TerrainJob *j = calloc(1, sizeof(*j));
	if (!j)
		return;
	j->base.priority = cell_job_priority;
	j->base.work = cell_job_work, j->base.what = "land";
	j->base.finish = cell_job_finish;
	j->base.discard = cell_job_discard;
	j->slot = (int)(c - slots);
	j->gen = c->gen;
	j->cx = c->cx, j->cy = c->cy;
	j->geometry = geometry;
	j->tex_full = tex_full;
	if (geometry)
		c->loading = 1;
	else
		c->tex_loading = 1;
	stream_submit(&j->base);
}

static void unload_cell(Cell *c) {
	if (c->vbo)
		glDeleteBuffers(1, &c->vbo);
	if (c->vbo_lod)
		glDeleteBuffers(1, &c->vbo_lod);
	if (c->vbo_far)
		glDeleteBuffers(1, &c->vbo_far);
	if (c->tex)
		glDeleteTextures(1, &c->tex);
	memset(c, 0, sizeof(*c));
}

static Cell *find_cell(int cx, int cy) {
	for (int i = 0; i < MAX_SLOTS; i++)
		if (slots[i].used && slots[i].cx == cx && slots[i].cy == cy)
			return &slots[i];
	return NULL;
}

// --- Public API ------------------------------------------------------------------

void terrain_init(const char *pack, const char *world) {
	snprintf(pack_dir, sizeof(pack_dir), "%s", pack);
	snprintf(world_name, sizeof(world_name), "%s", world);
	build_index_list(&full_indices, GRID);
	build_index_list(&lod_indices, LOD_GRID);
	build_index_list(&far_indices, FAR_GRID);

	char path[192];
	snprintf(path, sizeof(path), "%stextures/terrain_detail.sktex", pack_dir);
	detail_tex = sktex_load(path);
	if (!detail_tex)
		detail_tex = sktex_white();

	static const char *const attribs[] = {"position", "uv0", "uv1", "color"};
	for (int i = 0; i < ATMO_VARIANTS; i++) {
		GLuint prog = shader_program_prefixed("terrain", atmosphere_cg(i), TERRAIN_VS, TERRAIN_FS, attribs, 4);
		if (!prog)
			prog = shader_program_prefixed("terrain (plain)", atmosphere_cg(i), TERRAIN_VS_PLAIN, TERRAIN_FS_PLAIN, attribs, 4);
		terrain_progs[i] = prog;
		if (!prog)
			continue;
		terrain_wvps[i] = glGetUniformLocation(prog, "u_wvp");
		atmosphere_locate(prog, &terrain_atmos[i]);
		glUseProgram(prog);
		glUniform1i(glGetUniformLocation(prog, "u_color"), 0);
		glUniform1i(glGetUniformLocation(prog, "u_detail"), 1);
		glUseProgram(0);
	}
}

static int ring_of(const Cell *c, int pcx, int pcy) {
	int dx = abs(c->cx - pcx), dy = abs(c->cy - pcy);
	return dx > dy ? dx : dy;
}

static int last_pcx = 0x7fffffff, last_pcy = 0x7fffffff;
static int far_radius = TERRAIN_FAR_RADIUS, lod_radius = TERRAIN_LOD_RADIUS; // (the performance setting's)

void terrain_set_far_radius(int rings) {
	rings = rings < 3 ? 3 : rings > TERRAIN_FAR_RADIUS ? TERRAIN_FAR_RADIUS : rings;
	if (rings == far_radius)
		return;
	far_radius = rings;
	lod_radius = TERRAIN_LOD_RADIUS < rings ? TERRAIN_LOD_RADIUS : rings;
	last_pcx = last_pcy = 0x7fffffff; // (evict / load to the new edge)
}

int terrain_far_radius(void) {
	return far_radius;
}

void terrain_world_changed(void) {
	const char *prefix = worlds_terrain_prefix();
	if (!strcmp(prefix, world_name))
		return;
	for (int i = 0; i < MAX_SLOTS; i++) // (the old world's land: gone; in-flight loads are dropped)
		if (slots[i].used)
			unload_cell(&slots[i]);
	snprintf(world_name, sizeof(world_name), "%s", prefix);
	last_pcx = last_pcy = 0x7fffffff;
}

void terrain_release(void) {
	for (int i = 0; i < MAX_SLOTS; i++) // (indoors: the land's buffers and textures freed; streamed again outside)
		if (slots[i].used)
			unload_cell(&slots[i]);
	last_pcx = last_pcy = 0x7fffffff;
}

void terrain_update(float x, float z) {
	int pcx = terrain_cell_x(x), pcy = terrain_cell_y(z);
	if (pcx == last_pcx && pcy == last_pcy)
		return;
	last_pcx = pcx, last_pcy = pcy;
	job_pcx = pcx, job_pcy = pcy;

	// Evict cells that drifted out of range (one ring of hysteresis)
	for (int i = 0; i < MAX_SLOTS; i++) {
		Cell *c = &slots[i];
		if (c->used && ring_of(c, pcx, pcy) > far_radius + 1)
			unload_cell(c);
	}
	// Queue what's missing, nearest rings first
	for (int ring = 0; ring <= far_radius; ring++) {
		for (int dy = -ring; dy <= ring; dy++) {
			for (int dx = -ring; dx <= ring; dx++) {
				if (abs(dx) != ring && abs(dy) != ring)
					continue;
				int cx = pcx + dx, cy = pcy + dy;
				Cell *existing = find_cell(cx, cy);
				if (existing) {
					// Cells coming in from the far ring get their full-resolution color map
					if (!existing->absent && !existing->loading && !existing->tex_loading &&
					    ring <= lod_radius && !existing->tex_full)
						submit_cell_job(existing, 0, 1);
					continue;
				}
				Cell *slot = NULL;
				for (int i = 0; i < MAX_SLOTS && !slot; i++)
					if (!slots[i].used)
						slot = &slots[i];
				if (!slot)
					return;
				slot->used = 1;
				slot->gen = next_gen++;
				slot->cx = cx;
				slot->cy = cy;
				slot->water = NAN;
				submit_cell_job(slot, 1, ring <= lod_radius);
			}
		}
	}
}

void terrain_draw(float cam_x, float cam_z) {
	int v = atmosphere_variant();
	terrain_prog = terrain_progs[v], terrain_wvp = terrain_wvps[v], terrain_atmo = terrain_atmos[v];
	if (!terrain_prog)
		return;
	int pcx = terrain_cell_x(cam_x), pcy = terrain_cell_y(cam_z);
	float wvp[16];
	shader_wvp(wvp);
	glUseProgram(terrain_prog);
	glUniformMatrix4fv(terrain_wvp, 1, GL_FALSE, wvp);
	atmosphere_apply(&terrain_atmo);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, detail_tex);
	glActiveTexture(GL_TEXTURE0);
	for (int a = 0; a < 4; a++)
		glEnableVertexAttribArray(a);

	for (int pass = 0; pass < 3; pass++) {
		const IndexList *list = pass == 0 ? &full_indices : pass == 1 ? &lod_indices : &far_indices;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, list->ibo);
		for (int i = 0; i < MAX_SLOTS; i++) {
			const Cell *c = &slots[i];
			if (!c->used || c->absent || c->loading)
				continue;
			int ring = ring_of(c, pcx, pcy);
			int want = ring <= TERRAIN_FULL_RADIUS ? 0 : ring <= lod_radius ? 1 : 2;
			if (ring > far_radius || want != pass)
				continue;
			if (!terrain_cell_in_view(c->cx, c->cy))
				continue;
			GLuint vbo = pass == 0 ? c->vbo : pass == 1 ? c->vbo_lod : c->vbo_far;
			if (!vbo)
				continue;
			glBindTexture(GL_TEXTURE_2D, c->tex);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, x));
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, u0));
			glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, u1));
			glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TerrainVertex), (void *)offsetof(TerrainVertex, r));
			glDrawElements(GL_TRIANGLES, list->count, GL_UNSIGNED_SHORT, 0);
		}
	}
	for (int a = 0; a < 4; a++)
		glDisableVertexAttribArray(a);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

void terrain_draw_water(void) {
	if (!water_begin())
		return;
	for (int i = 0; i < MAX_SLOTS; i++) {
		const Cell *c = &slots[i];
		if (!c->used || c->absent || c->loading || isnan(c->water))
			continue;
		float x0 = cell_origin_x(c->cx), x1 = x0 + CELL_SIZE_M;
		float z0 = cell_origin_z(c->cy), z1 = z0 - CELL_SIZE_M;
		const float quad[] = {x0, c->water, z0, x1, c->water, z0, x1, c->water, z1, x0, c->water, z1};
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), quad);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}
	water_end();
}

int terrain_water_at(float x, float z, float *surface) {
	if (!active)
		return 0;
	const Cell *c = find_cell(terrain_cell_x(x), terrain_cell_y(z));
	if (!c || c->absent || c->loading || isnan(c->water))
		return 0;
	*surface = c->water;
	return 1;
}

int terrain_height_at(float x, float z, float *out) {
	if (!active) {
		*out = -1e6f;
		return 1;
	}
	int cx = terrain_cell_x(x), cy = terrain_cell_y(z);
	const Cell *c = find_cell(cx, cy);
	if (c && c->absent && strcmp(world_name, "tamriel")) { // (a world of its own: no land here, only what's built)
		*out = -1e6f;
		return 1;
	}
	if (!c || c->absent || c->loading)
		return 0;
	const float spacing = CELL_SIZE_M / (GRID - 1);
	float fx = (x - cell_origin_x(cx)) / spacing;
	float fy = (cell_origin_z(cy) - z) / spacing;
	int i = (int)fx, j = (int)fy;
	if (i > GRID - 2) i = GRID - 2;
	if (j > GRID - 2) j = GRID - 2;
	if (i < 0) i = 0;
	if (j < 0) j = 0;
	float a = fx - i, b = fy - j;
	const float *h = c->heights;
	float h00 = h[vidx(i, j)], h10 = h[vidx(i + 1, j)], h01 = h[vidx(i, j + 1)], h11 = h[vidx(i + 1, j + 1)];
	// Same triangle split as the index buffer (diagonal 00-11)
	*out = a >= b ? h00 + a * (h10 - h00) + b * (h11 - h10)
	              : h00 + b * (h01 - h00) + a * (h11 - h01);
	return 1;
}
