#include "doors.h"
#include "gamelog.h"
#include "creatures.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "aim.h"
#include "collision.h"
#include "fileio.h"
#include "objects.h"
#include "sfx.h"
#include "skmesh.h"

#define MAX_DOORS 256
#define MAX_PARTS 4
#define PART_MODELS 128
#define REACH_M 2.4f         // how close a door has to be...
#define AIM_RADIUS_M 0.8f    // ...and how near the crosshair has to pass its middle
#define SWING_S 0.9f         // closed -> open
#define ACTIVATOR_S 1.6f     // a pillar's turn, a gate rising, a lever going down
#define NPC_REACH_M 3.2f     // an NPC this close to a closed door opens it (open before they reach it: at a run, Hadvar was through before it swung)...
#define NPC_GONE_M 4.4f      // ...and it closes again once nobody is within this
#define NPC_CLOSE_AFTER_S 1.5f
#define QUANT_M 8.0f         // part vertices are quantized within this of the door
#define DRAW_RADIUS_M 60.0f
#define SKCO_VERSION 1

typedef struct {
	int model;      // objects model index, -1 free
	SkmMesh mesh;
	unsigned tex[8];
	float uv_c[2], uv_e;
	float *col;     // collision triangles (model space), col_count * 9
	int col_count;
	int loaded;
	float mid[3], size; // its bounds' middle and half their diagonal (model space: traps' blows)
} PartModel;

typedef struct {
	PartModel *model;
	float pivot[3], axis[3], angle, slide[3];
	ObjectVertex *verts[8]; // per submesh, posed (rebuilt when the door moves)
	uint8_t verts_ready[8]; // (a prop's: as modelled, built once)
} Part;

typedef struct {
	float m[9], t[3], centre[3];
	uint8_t tint[3];
	int part_count;
	Part parts[MAX_PARTS];
	float open, target; // 0 closed .. 1 open
	int by_npc;         // opened by an NPC passing through: closes after them
	float idle;         // nobody near (NPC doors)
	int posed;          // the vertices hold the current pose
	int container;      // a container's lid (loot.c opens it; no collision, not a door), or
	                    // KIND_ACTIVATOR: moved by activators.c (it collides; not a door either)
	float turns;        // steps already taken (a puzzle pillar: its motion is one step)
	int stepping;       // moving one step on (turns grows at the end)
	int spring;         // a lever: back once it's down
	float swing_s;      // seconds closed -> open
	int gone;           // taken away (a claw door opened: not drawn, no collision, no aim)
	int is_prop;        // a script's moving model (doors_prop): turned on the GPU, not re-posed
} Door;

enum { KIND_DOOR, KIND_CONTAINER, KIND_ACTIVATOR, KIND_TRAP };
static char pack[128];
static PartModel models[PART_MODELS];
static Door doors[MAX_DOORS];
static int door_count;
#define MAX_PROPS 4
static Door props[MAX_PROPS]; // (models moved by a script - the opening's cart; part_count 0: free)
static int collision_dirty;

void doors_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	for (int i = 0; i < PART_MODELS; i++)
		models[i].model = -1;
}

static void load_collision(PartModel *pm, const char *name) {
	char path[400];
	snprintf(path, sizeof(path), "%smeshes/%s.skcol", pack, name);
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	uint32_t count = 0;
	if (d && size >= 12 && !memcmp(d, "SKCO", 4) && *(uint16_t *)(d + 4) == SKCO_VERSION)
		memcpy(&count, d + 8, 4);
	if (count && size >= 12 + (long)count * 36) {
		pm->col = malloc(count * 36);
		if (pm->col) {
			memcpy(pm->col, d + 12, count * 36);
			pm->col_count = (int)count;
		}
	}
	free(d);
}

static PartModel *model_for(int index) {
	for (int i = 0; i < PART_MODELS; i++)
		if (models[i].model == index)
			return models[i].loaded ? &models[i] : NULL;
	for (int i = 0; i < PART_MODELS; i++) {
		PartModel *pm = &models[i];
		if (pm->model >= 0)
			continue;
		pm->model = index; // (also marks a failed load)
		char path[400];
		const char *name = objects_model_name(index);
		snprintf(path, sizeof(path), "%smeshes/%s.skmesh", pack, name);
		if (skmesh_load(path, &pm->mesh) != 0) {
			memset(&pm->mesh, 0, sizeof(pm->mesh));
			return NULL;
		}
		float umin[2] = {1e9f, 1e9f}, umax[2] = {-1e9f, -1e9f};
		for (int s = 0; s < pm->mesh.submesh_count && s < 8; s++) {
			SkmSubmesh *sm = &pm->mesh.submeshes[s];
			pm->tex[s] = objects_texture(sm->texture);
			for (uint32_t v = 0; v < sm->vertex_count; v++)
				for (int k = 0; k < 2; k++)
					umin[k] = fminf(umin[k], sm->vertices[v].uv[k]), umax[k] = fmaxf(umax[k], sm->vertices[v].uv[k]);
		}
		pm->uv_c[0] = (umin[0] + umax[0]) * 0.5f, pm->uv_c[1] = (umin[1] + umax[1]) * 0.5f;
		pm->uv_e = fmaxf(fmaxf(umax[0] - umin[0], umax[1] - umin[1]) * 0.5f, 1e-3f);
		float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
		for (int s = 0; s < pm->mesh.submesh_count && s < 8; s++)
			for (uint32_t v = 0; v < pm->mesh.submeshes[s].vertex_count; v++)
				for (int k = 0; k < 3; k++)
					lo[k] = fminf(lo[k], pm->mesh.submeshes[s].vertices[v].pos[k]),
					hi[k] = fmaxf(hi[k], pm->mesh.submeshes[s].vertices[v].pos[k]);
		for (int k = 0; k < 3; k++)
			pm->mid[k] = (lo[k] + hi[k]) * 0.5f;
		pm->size = 0.5f * sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
		                        (hi[2] - lo[2]) * (hi[2] - lo[2]));
		load_collision(pm, name);
		pm->loaded = 1;
		return pm;
	}
	return NULL;
}

// Doors standing open when the cells about you were loaded again (a step into the next cell):
// open again just as they were - Helgen's gate swung shut under the carts and opened a second time
#define MAX_KEPT 32
static struct {
	float centre[3], open, target, idle;
	int by_npc;
} kept[MAX_KEPT];
static int kept_count;

static void clear(void) {
	kept_count = 0;
	for (int i = 0; i < door_count && kept_count < MAX_KEPT; i++)
		if (doors[i].container == KIND_DOOR && (doors[i].open > 0.0f || doors[i].target > 0.0f)) {
			memcpy(kept[kept_count].centre, doors[i].centre, sizeof(kept[0].centre));
			kept[kept_count].open = doors[i].open, kept[kept_count].target = doors[i].target;
			kept[kept_count].idle = doors[i].idle, kept[kept_count].by_npc = doors[i].by_npc;
			kept_count++;
		}
	for (int i = 0; i < door_count; i++)
		for (int p = 0; p < doors[i].part_count; p++)
			for (int s = 0; s < 8; s++)
				free(doors[i].parts[p].verts[s]);
	door_count = 0;
	collision_set_dynamic(NULL, 0);
}

// One door-like record (placement, centre, light, moving parts); the position after it, or NULL
static const uint8_t *parse_door(const uint8_t *p, const uint8_t *end, int container, int *index) {
	*index = -1;
	if (p + 64 > end)
		return NULL;
	const int parts = p[63];
	if (p + 64 + parts * 44 > end)
		return NULL;
	if (door_count >= MAX_DOORS)
		return p + 64 + parts * 44;
	Door *d = &doors[door_count];
	memset(d, 0, sizeof(*d));
	memcpy(d->m, p, 36);
	memcpy(d->t, p + 36, 12);
	memcpy(d->centre, p + 48, 12);
	memcpy(d->tint, p + 60, 3);
	d->container = container;
	d->swing_s = container == KIND_CONTAINER ? SWING_S * 0.7f : container == KIND_ACTIVATOR ? ACTIVATOR_S : SWING_S;
	p += 64;
	for (int k = 0; k < parts; k++, p += 44) {
		uint16_t model;
		memcpy(&model, p, 2);
		if (d->part_count >= MAX_PARTS)
			continue;
		Part *pt = &d->parts[d->part_count];
		pt->model = model_for(model);
		if (!pt->model)
			continue;
		memcpy(pt->pivot, p + 4, 12);
		memcpy(pt->axis, p + 16, 12);
		memcpy(&pt->angle, p + 28, 4);
		memcpy(pt->slide, p + 32, 12);
		d->part_count++;
	}
	if (d->part_count)
		*index = door_count++;
	for (int k = 0; d->part_count && container == KIND_DOOR && k < kept_count; k++)
		if (fabsf(kept[k].centre[0] - d->centre[0]) + fabsf(kept[k].centre[1] - d->centre[1]) +
		        fabsf(kept[k].centre[2] - d->centre[2]) < 0.05f) {
			d->open = kept[k].open, d->target = kept[k].target, d->idle = kept[k].idle, d->by_npc = kept[k].by_npc;
			break;
		}
	return p;
}

const uint8_t *doors_load(const uint8_t *p, const uint8_t *end) {
	clear();
	collision_dirty = 1;
	return doors_append(p, end);
}

const uint8_t *doors_append(const uint8_t *p, const uint8_t *end) {
	if (!p || p + 4 > end)
		return NULL;
	uint32_t n;
	memcpy(&n, p, 4);
	p += 4;
	for (uint32_t i = 0; i < n && p; i++) {
		int index;
		p = parse_door(p, end, 0, &index);
	}
	collision_dirty = 1;
	return p;
}

const uint8_t *doors_add_container(const uint8_t *p, const uint8_t *end, int *index) {
	return parse_door(p, end, KIND_CONTAINER, index);
}

const uint8_t *doors_add_activator(const uint8_t *p, const uint8_t *end, int *index) {
	collision_dirty = 1;
	return parse_door(p, end, KIND_ACTIVATOR, index);
}

const uint8_t *doors_add_trap(const uint8_t *p, const uint8_t *end, int *index) {
	return parse_door(p, end, KIND_TRAP, index);
}

void doors_set_turns(int door, float turns) {
	if (door >= 0 && door < door_count)
		doors[door].turns = turns, doors[door].posed = 0, collision_dirty = 1;
}

void doors_set_pose(int door, float u) {
	if (door >= 0 && door < door_count && doors[door].open != u)
		doors[door].open = doors[door].target = u, doors[door].posed = 0;
}

int doors_moving(int door) {
	return door >= 0 && door < door_count && (doors[door].open != doors[door].target || doors[door].stepping);
}

void doors_step(int door) {
	if (door < 0 || door >= door_count || doors_moving(door))
		return;
	doors[door].stepping = 1, doors[door].target = 1.0f;
}

void doors_pull(int door) {
	if (door < 0 || door >= door_count || doors_moving(door))
		return;
	doors[door].spring = 1, doors[door].target = 1.0f;
}

void doors_set_open_now(int door, int open) {
	if (door >= 0 && door < door_count) {
		doors[door].open = doors[door].target = open ? 1.0f : 0.0f;
		doors[door].posed = 0;
		collision_dirty = 1;
	}
}

void doors_set_open(int door, int open) {
	if (door >= 0 && door < door_count)
		doors[door].target = open ? 1.0f : 0.0f;
}

// How far open it looks (eased: a door swings out quickly and settles; a trap: as set)
static float swing(const Door *d) {
	if (d->container == KIND_TRAP)
		return d->open;
	return d->open * d->open * (3.0f - 2.0f * d->open);
}

// A part's pose at open fraction u: model-space p -> world (rotate about the pivot, slide,
// then the placement)
static void pose_point(const Door *d, const Part *pt, float u, const float p[3], float out[3]) {
	const float a = pt->angle * (u + d->turns), c = cosf(a), s = sinf(a), *k = pt->axis;
	float v[3] = {p[0] - pt->pivot[0], p[1] - pt->pivot[1], p[2] - pt->pivot[2]};
	float kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
	float cr[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
	float q[3];
	for (int i = 0; i < 3; i++) // Rodrigues
		q[i] = v[i] * c + cr[i] * s + k[i] * kv * (1.0f - c) + pt->pivot[i] + pt->slide[i] * u;
	for (int i = 0; i < 3; i++)
		out[i] = d->m[i * 3] * q[0] + d->m[i * 3 + 1] * q[1] + d->m[i * 3 + 2] * q[2] + d->t[i];
}

static void rebuild_collision(void) {
	static float tris[2048 * 9];
	int n = 0;
	for (int i = 0; i < door_count; i++) {
		const Door *d = &doors[i];
		if (d->container == KIND_CONTAINER || d->container == KIND_TRAP)
			continue;
		for (int p = 0; p < d->part_count; p++) {
			const PartModel *pm = d->parts[p].model;
			for (int t = 0; t < pm->col_count && n < 2048; t++, n++)
				for (int v = 0; v < 3; v++)
					pose_point(d, &d->parts[p], swing(d), &pm->col[t * 9 + v * 3], &tris[n * 9 + v * 3]);
		}
	}
	collision_set_dynamic(tris, n);
	static int logged_n = -1;
	if (n / 256 != logged_n / 256) // (a place's moving collision: how much there is)
		game_log("doors: %d moving collision triangles, %d doors", n, door_count), logged_n = n;
}

int doors_part_blow(int door, float out[3], float *radius) {
	if (door < 0 || door >= door_count || !doors[door].part_count)
		return 0;
	const Door *d = &doors[door];
	int best = 0; // (the part reaching farthest from its pivot: a blade, a mace's head)
	float far = -1.0f;
	for (int p = 0; p < d->part_count; p++) {
		const Part *pt = &d->parts[p];
		const float r = hypotf(hypotf(pt->model->mid[0] - pt->pivot[0], pt->model->mid[1] - pt->pivot[1]),
		                       pt->model->mid[2] - pt->pivot[2]);
		if (r > far)
			far = r, best = p;
	}
	pose_point(d, &d->parts[best], swing(d), d->parts[best].model->mid, out);
	*radius = d->parts[best].model->size;
	return 1;
}

int doors_aim(const float eye[3], const float dir[3], float *along, const char **label) {
	int best = -1;
	float best_t = 1e9f;
	for (int i = 0; i < door_count; i++) {
		float t;
		if (!doors[i].container && aim_hits(eye, dir, doors[i].centre, AIM_RADIUS_M, REACH_M, &t) && t < best_t)
			best = i, best_t = t;
	}
	if (best >= 0) {
		*along = best_t;
		if (label)
			*label = doors[best].target > 0.5f ? "Close door" : "Open door";
	}
	return best;
}

int doors_position(int door, float out[3]) {
	if (door < 0 || door >= door_count)
		return 0;
	memcpy(out, doors[door].t, 3 * sizeof(float));
	return 1;
}

int doors_is_open(int door) {
	return door >= 0 && door < door_count && doors[door].target > 0.5f;
}

void doors_toggle(int door) {
	if (door < 0 || door >= door_count)
		return;
	doors[door].target = doors[door].target > 0.5f ? 0.0f : 1.0f;
	doors[door].by_npc = 0;
	sfx_play(doors[door].target > 0.5f ? "door_open" : "door_close", 0.7f, 0.0f);
}

static int keep_npc_open; // (Helgen's opening: a gate opened for the carts stays open, as Skyrim's script leaves it)

void doors_keep_npc_open(int keep) {
	keep_npc_open = keep;
}

void doors_update(float dt) {
	for (int i = 0; i < door_count; i++) {
		Door *d = &doors[i];
		if (d->container)
			goto swing;
		// NPCs walking through: a closed door opens for them and closes behind them
		float nearest = 1e9f;
		for (int a = 0; a < actors_count(); a++) {
			float pos[3];
			if (!actors_where(a, pos) || fabsf(pos[1] + 1.0f - d->centre[1]) > 2.0f)
				continue;
			nearest = fminf(nearest, hypotf(pos[0] - d->centre[0], pos[2] - d->centre[2]));
		}
		{ // (and the creatures: the keep's soldiers after you, bandits, draugr - they went through shut doors)
			const float floor[3] = {d->centre[0], d->centre[1] - 1.0f, d->centre[2]};
			if (creatures_near(floor, NPC_REACH_M - 0.6f))
				nearest = fminf(nearest, NPC_REACH_M - 0.6f);
		}
		if (nearest < NPC_REACH_M && d->target < 0.5f) {
			d->target = 1.0f, d->by_npc = 1, d->idle = 0.0f;
			sfx_play_at("door_open", d->centre, 20.0f);
			game_log("doors: %d opened for someone passing (%.0f %.0f %.0f)", i, d->centre[0], d->centre[1], d->centre[2]);
		}
		if (d->by_npc) {
			d->idle = nearest > NPC_GONE_M ? d->idle + dt : 0.0f;
			if (d->idle > NPC_CLOSE_AFTER_S && !keep_npc_open) // (the second cart: it swung shut and open again)
				d->target = 0.0f, d->by_npc = 0;
		}
	swing:
		if (d->open != d->target) {
			float step = dt / d->swing_s;
			d->open = d->target > d->open ? fminf(d->target, d->open + step) : fmaxf(d->target, d->open - step);
			d->posed = 0;
			collision_dirty |= d->container != KIND_CONTAINER;
		} else if (d->stepping && d->open >= 1.0f) { // (a step done: it's the new rest)
			d->turns += 1.0f, d->open = d->target = 0.0f, d->stepping = 0;
			d->posed = 0;
		} else if (d->spring && d->open >= 1.0f) { // (a lever down: back up)
			d->target = 0.0f, d->spring = 0;
		}
	}
	if (collision_dirty) {
		rebuild_collision();
		collision_dirty = 0;
	}
}

static ObjectsDynamic draws[(MAX_DOORS + MAX_PROPS) * MAX_PARTS * 2];

static int draw_one(Door *d, int nd) {
	const float q = 32767.0f / QUANT_M;
	{
		const float u = swing(d);
		for (int p = 0; p < d->part_count; p++) {
			Part *pt = &d->parts[p];
			const PartModel *pm = pt->model;
			for (int s = 0; s < pm->mesh.submesh_count && s < 8 && nd < (int)(sizeof(draws) / sizeof(draws[0])); s++) {
				const SkmSubmesh *sm = &pm->mesh.submeshes[s];
				if (!pt->verts[s]) {
					pt->verts[s] = malloc(sm->vertex_count * sizeof(ObjectVertex));
					if (!pt->verts[s])
						continue;
					d->posed = 0;
				}
				ObjectVertex *out = pt->verts[s];
				if (d->is_prop ? !pt->verts_ready[s] : !d->posed) {
					for (uint32_t v = 0; v < sm->vertex_count; v++) {
						const SkmVertex *src = &sm->vertices[v];
						float w[3];
						if (d->is_prop) { // (a prop: as modelled - turned on the GPU, ObjectsDynamic.rot)
							w[0] = src->pos[0] + d->t[0], w[1] = src->pos[1] + d->t[1], w[2] = src->pos[2] + d->t[2];
						} else {
							pose_point(d, pt, u, src->pos, w);
						}
						ObjectVertex *o = &out[v];
						o->x = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, (w[0] - d->t[0]) * q));
						o->y = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, (w[1] - d->t[1]) * q));
						o->z = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, (w[2] - d->t[2]) * q));
						o->pad = 0;
						o->u = (int16_t)((src->uv[0] - pm->uv_c[0]) / pm->uv_e * 32767.0f);
						o->v = (int16_t)((src->uv[1] - pm->uv_c[1]) / pm->uv_e * 32767.0f);
						// The interior's light at the door (baked by the converter) x the vertex colour
						o->r = (uint8_t)(src->col[0] * d->tint[0] / 255);
						o->g = (uint8_t)(src->col[1] * d->tint[1] / 255);
						o->b = (uint8_t)(src->col[2] * d->tint[2] / 255);
						o->a = src->col[3];
					}
					pt->verts_ready[s] = 1;
				}
				ObjectsDynamic *dr = &draws[nd++];
				dr->vertices = out;
				dr->vertex_count = (int)sm->vertex_count;
				dr->indices = sm->indices;
				dr->index_count = (int)sm->index_count;
				dr->tex = pm->tex[s];
				dr->pos[0] = d->t[0], dr->pos[1] = d->t[1], dr->pos[2] = d->t[2], dr->pos[3] = QUANT_M;
				dr->uv[0] = pm->uv_c[0], dr->uv[1] = pm->uv_c[1], dr->uv[2] = pm->uv_e;
				dr->alpha = (sm->flags & SKMESH_ALPHA_TEST) != 0;
				dr->rot = d->is_prop ? d->m : NULL;
			}
		}
		d->posed = 1;
	}
	return nd;
}

void doors_draw(float cam_x, float cam_z) {
	int nd = 0;
	for (int i = 0; i < door_count; i++)
		if (!doors[i].gone && hypotf(doors[i].t[0] - cam_x, doors[i].t[2] - cam_z) <= DRAW_RADIUS_M)
			nd = draw_one(&doors[i], nd);
	for (int i = 0; i < MAX_PROPS; i++)
		if (props[i].part_count && hypotf(props[i].t[0] - cam_x, props[i].t[2] - cam_z) <= DRAW_RADIUS_M)
			nd = draw_one(&props[i], nd);
	objects_draw_dynamic(draws, nd);
}

void doors_remove(int door) {
	if (door < 0 || door >= door_count || doors[door].gone)
		return;
	doors[door].gone = 1, doors[door].container = KIND_TRAP;
	collision_dirty = 1;
}

void doors_log_near(const float pos[3], float radius) {
	for (int i = 0; i < door_count; i++) {
		const Door *d = &doors[i];
		if (hypotf(d->centre[0] - pos[0], d->centre[2] - pos[2]) < radius)
			game_log("doors: #%d kind %d at %.1f %.1f %.1f open %.1f -> %.1f", i, d->container, d->centre[0], d->centre[1], d->centre[2], d->open, d->target);
	}
}

int doors_open_near(const float pos[3], float radius) {
	int n = 0;
	for (int i = 0; i < door_count; i++) {
		Door *d = &doors[i];
		if (d->container != KIND_DOOR || d->target >= 1.0f) // (doors only: not a lid, nor what a lever works - a bridge's deck)
			continue;
		if (hypotf(d->centre[0] - pos[0], d->centre[2] - pos[2]) < radius && fabsf(d->centre[1] - pos[1]) < 3.0f)
			d->target = 1.0f, n++;
	}
	return n;
}

// --- Props: a model a script moves (the opening's cart) -------------------------------------

int doors_prop(int model, const uint8_t tint[3]) {
	PartModel *pm = model >= 0 ? model_for(model) : NULL;
	for (int i = 0; pm && i < MAX_PROPS; i++)
		if (!props[i].part_count) {
			Door *d = &props[i];
			memset(d, 0, sizeof(*d));
			d->m[0] = d->m[4] = d->m[8] = 1.0f;
			memcpy(d->tint, tint, 3);
			d->parts[0].model = pm;
			d->part_count = 1;
			d->container = KIND_TRAP; // (no collision, no aim)
			d->is_prop = 1;
			return i;
		}
	return -1;
}

void doors_prop_place(int prop, const float pos[3], float heading, float pitch) {
	if (prop < 0 || prop >= MAX_PROPS || !props[prop].part_count)
		return;
	Door *d = &props[prop];
	memcpy(d->t, pos, sizeof(d->t));
	// (turned about y by heading - a model's front is its -z, as the actors' - then tilted by
	// pitch along it: the road's slope; posed again only when that changes)
	const float ch = cosf(heading), sh = sinf(heading), cp = cosf(pitch), sp = sinf(pitch);
	const float m[9] = {ch, sh * sp, sh * cp, 0.0f, cp, -sp, -sh, ch * sp, ch * cp};
	if (fabsf(m[0] - d->m[0]) + fabsf(m[2] - d->m[2]) + fabsf(m[5] - d->m[5]) > 0.002f)
		memcpy(d->m, m, sizeof(m)), d->posed = 0;
}

void doors_prop_remove(int prop) {
	if (prop < 0 || prop >= MAX_PROPS)
		return;
	for (int s = 0; s < 8; s++)
		free(props[prop].parts[0].verts[s]);
	memset(&props[prop], 0, sizeof(props[prop]));
}
