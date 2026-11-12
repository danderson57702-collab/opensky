#include "loot.h"

#include "alchemy.h"
#include "terrain.h"
#include "worlds.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aim.h"
#include "doors.h"
#include "furniture.h"
#include "interior.h"
#include "sfx.h"
#include "fileio.h"
#include "lockpick.h"
#include "items.h"
#include "lighting.h"
#include "objects.h"
#include "skmesh.h"
#include "traps.h"
#include "activators.h"
#include "ui.h"
#include "gamelog.h"
#include "stream.h"
#include "gametext.h"

#define MAX_CONTAINERS 256
#define MAX_LOOSE 1024
#define ITEM_MODELS 512
#define REACH_M 2.8f         // from the eye
#define CONTAINER_AIM_M 0.55f // how near the crosshair has to pass a container's middle...
#define ITEM_AIM_M 0.22f      // ...or a loose thing
#define PLANT_AIM_M 0.45f     // ...or a plant to harvest (a bush, a cluster of mushrooms)
#define NO_MODEL 0xFFFF       // (a loose item drawn with the scenery: a plant)
#define DRAW_RADIUS_M 30.0f
#define QUANT_M 4.0f
#define OWNED 1
#define HARVEST 2 // (a coin purse: taking it gives what its container holds, its gold)

typedef struct {
	uint32_t ref;
	int container, lock, owned;
	float centre[3], radius; // where the crosshair has to pass (its model's middle, its size)
	int lid; // doors.c index (-1: nothing moves)
} Container;

typedef struct {
	int model; // objects model index, -1 free
	int loaded;
	SkmMesh mesh;
	unsigned tex[8];
	float uv_c[2], uv_e;
} ItemModel;

typedef struct {
	uint32_t ref;
	int item, owned; // (harvest: item is a container index)
	int harvest;
	int count; // dropped things: how many lie there (0: a placed thing)
	ItemModel *model;
	float m[9], t[3];
	uint8_t tint[3];
	ObjectVertex *verts[8]; // placed (once)
} Loose;

static char pack[128];
static Container containers[MAX_CONTAINERS];
static int container_count;
static Loose loose[MAX_LOOSE];
static int loose_count;
static ItemModel models[ITEM_MODELS];
static int open_lid = -1;
static char label[96];
static int ext_cx = 1 << 30, ext_cy; // outdoors: the cell the loot around was loaded for

void loot_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	for (int i = 0; i < ITEM_MODELS; i++)
		models[i].model = -1;
}

// Outdoors, the loose things whose model isn't read yet wait here: one model read a frame (loot_models_step), not all of
// a new cell's as you cross into it - each a mesh and its textures off the card on the game's own thread, up to 0.75 s
// in a frame on the Vita (the user's log, Nov 10)
#define MAX_WAITING 256
static struct { Loose l; int model; } waiting[MAX_WAITING];
static int waiting_count, defer_models;

static int model_known(int index) { // (1: read already - or tried and failed; model_for won't touch the card)
	for (int i = 0; i < ITEM_MODELS; i++)
		if (models[i].model == index)
			return 1;
	return 0;
}

static ItemModel *model_for(int index) {
	for (int i = 0; i < ITEM_MODELS; i++)
		if (models[i].model == index)
			return models[i].loaded ? &models[i] : NULL;
	for (int i = 0; i < ITEM_MODELS; i++) {
		ItemModel *im = &models[i];
		if (im->model >= 0)
			continue;
		im->model = index;
		char path[400];
		snprintf(path, sizeof(path), "%smeshes/%s.skmesh", pack, objects_model_name(index));
		if (skmesh_load(path, &im->mesh) != 0) {
			memset(&im->mesh, 0, sizeof(im->mesh));
			return NULL;
		}
		float umin[2] = {1e9f, 1e9f}, umax[2] = {-1e9f, -1e9f};
		for (int s = 0; s < im->mesh.submesh_count && s < 8; s++) {
			SkmSubmesh *sm = &im->mesh.submeshes[s];
			im->tex[s] = objects_texture(sm->texture);
			for (uint32_t v = 0; v < sm->vertex_count; v++)
				for (int k = 0; k < 2; k++)
					umin[k] = fminf(umin[k], sm->vertices[v].uv[k]), umax[k] = fmaxf(umax[k], sm->vertices[v].uv[k]);
		}
		im->uv_c[0] = (umin[0] + umax[0]) * 0.5f, im->uv_c[1] = (umin[1] + umax[1]) * 0.5f;
		im->uv_e = fmaxf(fmaxf(umax[0] - umin[0], umax[1] - umin[1]) * 0.5f, 1e-3f);
		im->loaded = 1;
		return im;
	}
	return NULL;
}

static void clear(void) {
	for (int i = 0; i < loose_count; i++)
		for (int s = 0; s < 8; s++)
			free(loose[i].verts[s]);
	container_count = loose_count = 0;
	waiting_count = 0;
	open_lid = -1;
}

// A containers + loose items section (an interior's, or an outdoor cell's .skloot), added
static const uint8_t *append(const uint8_t *p, const uint8_t *end, int sized) {
	if (!p || p + 4 > end)
		return NULL;
	uint32_t n;
	memcpy(&n, p, 4);
	p += 4;
	for (uint32_t i = 0; i < n && p; i++) {
		if (p + 8 > end)
			return NULL;
		Container c = {0};
		uint16_t cidx;
		memcpy(&c.ref, p, 4);
		memcpy(&cidx, p + 4, 2);
		c.container = cidx, c.lock = p[6], c.owned = (p[7] & OWNED) != 0;
		c.radius = CONTAINER_AIM_M;
		if (sized) {
			memcpy(&c.radius, p + 8, 4);
			p += 4;
		}
		memcpy(c.centre, p + 8 + 48, 12);
		p = doors_add_container(p + 8, end, &c.lid);
		if (p && container_count < MAX_CONTAINERS)
			containers[container_count++] = c;
	}
	if (!p || p + 4 > end)
		return NULL;
	memcpy(&n, p, 4);
	p += 4;
	if (p + (size_t)n * 60 > end)
		return NULL;
	for (uint32_t i = 0; i < n; i++, p += 60) {
		Loose l = {0};
		uint16_t item, model;
		memcpy(&l.ref, p, 4);
		memcpy(&item, p + 4, 2);
		memcpy(&model, p + 6, 2);
		l.item = item, l.owned = (p[8] & OWNED) != 0, l.harvest = (p[8] & HARVEST) != 0;
		memcpy(l.tint, p + 9, 3);
		memcpy(l.m, p + 12, 36);
		memcpy(l.t, p + 48, 12);
		if (model == NO_MODEL && l.harvest && items_harvested(l.ref, lighting_game_hours())) // (picked: growing back)
			continue;
		if (items_taken(l.ref) || loose_count >= MAX_LOOSE)
			continue;
		if (model == NO_MODEL && l.harvest) { // (a plant, a mushroom: harvested where it stands - its model's the scenery's)
			l.model = NULL;
			loose[loose_count++] = l;
			continue;
		}
		if (defer_models && !model_known(model)) { // (outdoors: its model read in a later frame)
			if (waiting_count < MAX_WAITING)
				waiting[waiting_count].l = l, waiting[waiting_count++].model = model;
			continue;
		}
		l.model = model_for(model);
		if (l.model)
			loose[loose_count++] = l;
	}
	return p;
}

// One waiting model read this frame, and everything that was waiting on a model now read set down
static void loot_models_step(void) {
	if (!waiting_count)
		return;
	int read = 0, kept = 0;
	for (int i = 0; i < waiting_count; i++) {
		if (!model_known(waiting[i].model)) {
			if (read) {
				waiting[kept++] = waiting[i];
				continue;
			}
			read = 1;
		}
		waiting[i].l.model = model_for(waiting[i].model);
		if (waiting[i].l.model && loose_count < MAX_LOOSE)
			loose[loose_count++] = waiting[i].l;
	}
	waiting_count = kept;
}

// What the player dropped here (items.h), lying on the ground where it fell
static void add_dropped(void) {
	const int place = interior_current();
	for (int i = 0; i < items_dropped_count() && loose_count < MAX_LOOSE; i++) {
		Loose l = {0};
		float yaw;
		if (!items_dropped(i, place, &l.item, &l.count, l.t, &yaw, &l.ref))
			continue;
		int model = items_model(l.item);
		if (model < 0) { // (no model of its own on the ground - quest gear, some armour: one like it, so it can be picked up again)
			const int like = items_find_name(items_type(l.item) == IT_ARMO ? T_ARMO_0003619E_FULL0 : T_ALCH_03003538_FULL0);
			model = like >= 0 ? items_model(like) : -1;
			if (model < 0)
				model = items_model(items_find_gold());
		}
		l.model = model >= 0 ? model_for(model) : NULL;
		if (!l.model)
			continue;
		const float c = cosf(yaw), s = sinf(yaw);
		const float m[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
		memcpy(l.m, m, sizeof(m));
		float low = 1e9f; // (resting on its lowest point)
		for (int k = 0; k < l.model->mesh.submesh_count && k < 8; k++)
			for (uint32_t v = 0; v < l.model->mesh.submeshes[k].vertex_count; v++)
				low = fminf(low, l.model->mesh.submeshes[k].vertices[v].pos[1]);
		if (low < 1e8f)
			l.t[1] -= low;
		l.tint[0] = l.tint[1] = l.tint[2] = 200;
		loose[loose_count++] = l;
	}
}

void loot_refresh_dropped(void) {
	for (int i = 0; i < loose_count; i++)
		if (loose[i].count > 0) {
			for (int s = 0; s < 8; s++)
				free(loose[i].verts[s]);
			loose[i--] = loose[--loose_count];
		}
	add_dropped();
}

const uint8_t *loot_load(const uint8_t *p, const uint8_t *end, int sized) {
	clear();
	doors_set_open(-1, 0);
	ext_cx = 1 << 30; // (outdoors again: reload around the player)
	const uint8_t *after = append(p, end, sized);
	add_dropped();
	return after;
}

void loot_world_changed(void) {
	ext_cx = 1 << 30;
}

void loot_exterior(int cx, int cy) {
	if (cx == ext_cx && cy == ext_cy) {
		loot_models_step();
		return;
	}
	const unsigned long long t0 = stream_now_us();
	ext_cx = cx, ext_cy = cy;
	defer_models = 1;
	clear();
	furniture_clear();
	traps_clear();
	activators_clear();
	doors_load(NULL, NULL); // (outdoors the moving parts are containers' lids, traps, puzzles, a few doors)
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++) {
			char path[256];
			snprintf(path, sizeof(path), "%sobjects/%s_%d_%d.skloot", pack, worlds_prefix(cx + dx, cy + dy), cx + dx, cy + dy);
			long size = 0;
			uint8_t *d = file_read_all(path, &size);
			if (d && size >= 16 && !memcmp(d, "SKLO", 4)) {
				const int version = *(const uint16_t *)(d + 4);
				const uint8_t *after = append(d + 16, d + size, version >= 2);
				const uint8_t *after_f = version >= 3 ? furniture_append(after, d + size) : NULL; // (its seats and beds)
				const uint8_t *after_t = version >= 4 ? traps_append(after_f, d + size) : NULL; // (its traps)
				const uint8_t *after_a = version >= 5 ? activators_append(after_t, d + size) : NULL; // (its puzzles, levers)
				if (version >= 6)
					doors_append(after_a, d + size); // (its doors that open in place: a tower's)
			}
			free(d);
		}
	defer_models = 0;
	add_dropped();
	const unsigned ms = (unsigned)((stream_now_us() - t0) / 1000);
	if (ms > 40) // (still a stall: how long, with how many models left for later)
		game_log("loot: the cells about %d %d read in %u ms (%d things, %d waiting on models)", cx, cy, ms, loose_count, waiting_count);
}

// Targets: containers 0.., loose items MAX_CONTAINERS + i
int loot_find(const char *name, const float near[3], float at[3]) {
	int best = -1;
	float best_d = 1e9f;
	for (int i = 0; i < loose_count; i++) {
		const float d = hypotf(loose[i].t[0] - near[0], loose[i].t[2] - near[2]) + fabsf(loose[i].t[1] - near[1]) * 2.0f;
		const int plant = !strcmp(name, "plant") && loose[i].harvest && !loose[i].model; // (a test's "take plant": the nearest)
		if ((plant || (!loose[i].harvest && !strcmp(items_name(loose[i].item), name))) && d < best_d)
			best = i, best_d = d;
	}
	if (best < 0 && !strcmp(name, "container")) { // (a test's "take container": the nearest, opened)
		for (int i = 0; i < container_count; i++) {
			const float d = hypotf(containers[i].centre[0] - near[0], containers[i].centre[2] - near[2]);
			if (d < best_d)
				best = i, best_d = d;
		}
		if (best >= 0) {
			memcpy(at, containers[best].centre, 3 * sizeof(float));
			return best;
		}
	}
	if (best < 0) { // (a test's: what is near, then)
		for (int i = 0, shown = 0; i < loose_count && shown < 12; i++)
			if (!loose[i].harvest && hypotf(loose[i].t[0] - near[0], loose[i].t[2] - near[2]) < 25.0f)
				game_log("loot: near you '%s'%s", items_name(loose[i].item), loose[i].owned ? " (owned)" : ""), shown++;
		return -1;
	}
	memcpy(at, loose[best].t, 3 * sizeof(float));
	return MAX_CONTAINERS + best;
}

int loot_aim(const float eye[3], const float dir[3], float *along, const char **out) {
	int best = -1;
	float best_t = 1e9f;
	for (int i = 0; i < container_count + loose_count; i++) {
		const int is_container = i < container_count;
		const float *c = is_container ? containers[i].centre : loose[i - container_count].t;
		const float r = is_container ? containers[i].radius : loose[i - container_count].model ? ITEM_AIM_M : PLANT_AIM_M;
		float t;
		if (aim_hits(eye, dir, c, r, REACH_M + r * 0.5f, &t) && t < best_t)
			best = i, best_t = t;
	}
	if (best < 0)
		return -1;
	*along = best_t;
	if (best < container_count) {
		const Container *c = &containers[best];
		const char *name = items_container_name(c->container);
		if (c->lock && !items_unlocked(c->ref))
			snprintf(label, sizeof(label), c->lock > 100 ? "%s (Requires key)" : "Pick lock: %s", name);
		else
			snprintf(label, sizeof(label), c->owned ? "Steal from %s" : "Open %s", name);
		*out = label;
		return best;
	}
	const Loose *l = &loose[best - container_count];
	if (l->count > 1)
		snprintf(label, sizeof(label), "Take %s (%d)", items_name(l->item), l->count);
	else
		snprintf(label, sizeof(label), !l->model ? (l->owned ? "Steal from %s" : "Harvest %s") : l->owned ? "Steal %s" : "Take %s",
		         l->harvest ? items_container_name(l->item) : items_name(l->item)); // (a plant: Skyrim's "Harvest")
	*out = label;
	return MAX_CONTAINERS + (best - container_count);
}

static int theft; // (the worth of someone's things taken loose since last asked)

int loot_take_theft(void) {
	const int v = theft;
	theft = 0;
	return v;
}

int loot_owned(int target) {
	if (target >= MAX_CONTAINERS)
		return target - MAX_CONTAINERS < loose_count && loose[target - MAX_CONTAINERS].owned;
	return target >= 0 && target < container_count && containers[target].owned;
}

int loot_activate(int target, int *handle, const char **name) {
	if (target >= MAX_CONTAINERS) {
		int i = target - MAX_CONTAINERS;
		if (i < 0 || i >= loose_count)
			return 0;
		Loose *l = &loose[i];
		char text[80];
		if (l->count > 0) { // (something dropped)
			const int n = items_pick_up(l->ref);
			snprintf(text, sizeof(text), n > 1 ? "%s (%d) added" : "%s added", items_name(l->item), n);
			sfx_play("itm_up", 0.7f, 0.0f);
		} else if (l->harvest) { // (a coin purse: its gold, rolled from Skyrim's leveled list)
			const int h = items_open_container(l->ref, l->item);
			int got[8], counts[8];
			const int n = items_in_container(h, got, counts, 8);
			int gold = 0;
			for (int k = 0; k < n; k++) {
				items_take(h, got[k], counts[k]);
				if (items_form(got[k]) == 0xF)
					gold += counts[k];
			}
			if (l->owned) // (someone's purse)
				theft += gold;
			if (gold > 0)
				snprintf(text, sizeof(text), "%s: %d Gold", items_container_name(l->item), gold);
			else
				snprintf(text, sizeof(text), "%s added", n ? items_name(got[0]) : items_container_name(l->item));
			if (!gold && n && items_type(got[0]) == IT_INGR && alchemy_harvest_count() > 1) { // (Green Thumb: two)
				items_add(got[0], counts[0] * (alchemy_harvest_count() - 1));
				snprintf(text, sizeof(text), "%s (%d) added", items_name(got[0]), counts[0] * alchemy_harvest_count());
			}
			sfx_play("itm_coinpurse_up", 0.7f, 0.0f);
		} else {
			items_add(l->item, 1);
			if (l->owned) // (someone's: stolen - crime.c reports it, if seen)
				items_mark_stolen(l->item, 1), theft += items_value(l->item);
			sfx_play(items_form(l->item) == 0xF ? "itm_gold" : "itm_up", 0.7f, 0.0f);
			snprintf(text, sizeof(text), "%s added", items_name(l->item));
		}
		if (!l->count && !l->model && l->harvest) // (a plant: grows back)
			items_set_harvested(l->ref, lighting_game_hours());
		else if (!l->count)
			items_set_taken(l->ref);
		ui_notice(text, 2.0f);
		for (int s = 0; s < 8; s++)
			free(l->verts[s]);
		loose[i] = loose[--loose_count]; // (gone from the world)
		return 1;
	}
	if (target < 0 || target >= container_count)
		return 0;
	const Container *c = &containers[target];
	if (c->lock && !items_unlocked(c->ref)) {
		lockpick_start(c->lock, target); // (opens it when picked: loot_unlocked)
		return 0;
	}
	*handle = items_open_container(c->ref, c->container);
	items_set_theft(*handle, c->owned ? 1 : 0); // (someone's: what you take from it is stolen)
	*name = items_container_name(c->container);
	open_lid = c->lid;
	doors_set_open(open_lid, 1);
	sfx_play(c->lid >= 0 ? "chest_open" : "itm_up", 0.6f, 0.0f);
	return 2;
}

int loot_unlocked(int target, int *handle, const char **name) {
	if (target < 0 || target >= container_count)
		return 0;
	items_set_unlocked(containers[target].ref);
	return loot_activate(target, handle, name);
}

void loot_close(void) {
	if (open_lid >= 0)
		sfx_play("chest_close", 0.6f, 0.0f);
	doors_set_open(open_lid, 0);
	open_lid = -1;
}

// A model's vertices placed (model space -> world rotation, relative to its position),
// quantized as objects are, lit with tint
static void place_verts(ObjectVertex *out, const SkmSubmesh *sm, const ItemModel *im, const float m[9],
                        const uint8_t tint[3]) {
	const float q = 32767.0f / QUANT_M;
	for (uint32_t v = 0; v < sm->vertex_count; v++) {
		const SkmVertex *src = &sm->vertices[v];
		const float *p = src->pos;
		ObjectVertex *o = &out[v];
		float w[3];
		for (int k = 0; k < 3; k++)
			w[k] = m[k * 3] * p[0] + m[k * 3 + 1] * p[1] + m[k * 3 + 2] * p[2];
		o->x = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, w[0] * q));
		o->y = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, w[1] * q));
		o->z = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, w[2] * q));
		o->pad = 0;
		o->u = (int16_t)((src->uv[0] - im->uv_c[0]) / im->uv_e * 32767.0f);
		o->v = (int16_t)((src->uv[1] - im->uv_c[1]) / im->uv_e * 32767.0f);
		o->r = (uint8_t)(src->col[0] * tint[0] / 255);
		o->g = (uint8_t)(src->col[1] * tint[1] / 255);
		o->b = (uint8_t)(src->col[2] * tint[2] / 255);
		o->a = src->col[3];
	}
}

// Things moving this frame (arrows in flight): drawn with the loose things, then forgotten
#define MAX_MOVING 48
#define MOVING_VERTS 24576
static struct {
	ItemModel *model;
	float m[9], t[3];
	uint8_t tint[3];
} moving[MAX_MOVING];
static int moving_count;

int loot_draw_moving(int model, const float m[9], const float t[3], const uint8_t tint[3]) {
	if (moving_count >= MAX_MOVING || model < 0)
		return 0;
	ItemModel *im = model_for(model);
	if (!im)
		return 0;
	moving[moving_count].model = im;
	memcpy(moving[moving_count].m, m, 36);
	memcpy(moving[moving_count].t, t, 12);
	memcpy(moving[moving_count].tint, tint, 3);
	moving_count++;
	return 1;
}

int loot_model_extent(int model, float lo[3], float hi[3]) {
	ItemModel *im = model >= 0 ? model_for(model) : NULL;
	if (!im)
		return 0;
	for (int k = 0; k < 3; k++)
		lo[k] = 1e9f, hi[k] = -1e9f;
	for (int s = 0; s < im->mesh.submesh_count && s < 8; s++)
		for (uint32_t v = 0; v < im->mesh.submeshes[s].vertex_count; v++)
			for (int k = 0; k < 3; k++) {
				const float p = im->mesh.submeshes[s].vertices[v].pos[k];
				lo[k] = fminf(lo[k], p), hi[k] = fmaxf(hi[k], p);
			}
	return lo[0] < 1e8f;
}

// The moving models queued (loot_draw_moving) into draws from nd (up to max), their vertices
// placed in scratch (up to verts_max); the queue emptied. Returns the new count
static int queue_moving(ObjectsDynamic *draws, int nd, int max, ObjectVertex *scratch, int verts_max) {
	int used = 0;
	for (int i = 0; i < moving_count && nd < max; i++) {
		if (!terrain_rect_in_view(moving[i].t[0] - 1.0f, moving[i].t[2] - 1.0f, moving[i].t[0] + 1.0f, moving[i].t[2] + 1.0f) ||
		    !objects_point_in_sight(moving[i].t[0], moving[i].t[1], moving[i].t[2]))
			continue;
		const ItemModel *im = moving[i].model;
		for (int s = 0; s < im->mesh.submesh_count && s < 8 && nd < max; s++) {
			const SkmSubmesh *sm = &im->mesh.submeshes[s];
			if (used + (int)sm->vertex_count > verts_max)
				break;
			place_verts(scratch + used, sm, im, moving[i].m, moving[i].tint);
			ObjectsDynamic *d = &draws[nd++];
			memset(d, 0, sizeof(*d));
			d->vertices = scratch + used;
			d->vertex_count = (int)sm->vertex_count;
			used += (int)sm->vertex_count;
			d->indices = sm->indices;
			d->index_count = (int)sm->index_count;
			d->tex = im->tex[s];
			d->pos[0] = moving[i].t[0], d->pos[1] = moving[i].t[1], d->pos[2] = moving[i].t[2], d->pos[3] = QUANT_M;
			d->uv[0] = im->uv_c[0], d->uv[1] = im->uv_c[1], d->uv[2] = im->uv_e;
			d->alpha = (sm->flags & SKMESH_ALPHA_TEST) != 0;
		}
	}
	moving_count = 0;
	return nd;
}

void loot_draw(float cam_x, float cam_z) {
	static ObjectsDynamic draws[512];
	static ObjectVertex scratch[MOVING_VERTS];
	int nd = 0;
	for (int i = 0; i < loose_count && nd < 512; i++) {
		Loose *l = &loose[i];
		if (hypotf(l->t[0] - cam_x, l->t[2] - cam_z) > DRAW_RADIUS_M)
			continue;
		if (!terrain_rect_in_view(l->t[0] - 1.0f, l->t[2] - 1.0f, l->t[0] + 1.0f, l->t[2] + 1.0f) ||
		    !objects_point_in_sight(l->t[0], l->t[1], l->t[2]))
			continue;
		const ItemModel *im = l->model;
		if (!im) // (a plant: drawn with the scenery)
			continue;
		for (int s = 0; s < im->mesh.submesh_count && s < 8 && nd < 512; s++) {
			const SkmSubmesh *sm = &im->mesh.submeshes[s];
			if (!l->verts[s]) { // placed once: model space -> world, the light at the spot
				l->verts[s] = malloc(sm->vertex_count * sizeof(ObjectVertex));
				if (!l->verts[s])
					continue;
				place_verts(l->verts[s], sm, im, l->m, l->tint);
			}
			ObjectsDynamic *d = &draws[nd++];
			memset(d, 0, sizeof(*d));
			d->vertices = l->verts[s];
			d->vertex_count = (int)sm->vertex_count;
			d->indices = sm->indices;
			d->index_count = (int)sm->index_count;
			d->tex = im->tex[s];
			d->pos[0] = l->t[0], d->pos[1] = l->t[1], d->pos[2] = l->t[2], d->pos[3] = QUANT_M;
			d->uv[0] = im->uv_c[0], d->uv[1] = im->uv_c[1], d->uv[2] = im->uv_e;
			d->alpha = (sm->flags & SKMESH_ALPHA_TEST) != 0;
		}
	}
	nd = queue_moving(draws, nd, 512, scratch, MOVING_VERTS);
	objects_draw_dynamic(draws, nd);
}

void loot_draw_moving_now(void) {
	static ObjectsDynamic draws[16];
	static ObjectVertex scratch[MOVING_VERTS / 4]; // (its own: the world's may still be in use)
	const int nd = queue_moving(draws, 0, 16, scratch, MOVING_VERTS / 4);
	objects_draw_dynamic(draws, nd);
}
