#include "fish.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lighting.h"
#include "objects.h"
#include "skmesh.h"

// Skyrim's ambient salmon swim on spline paths baked into their effect models; here each
// one swims its own loop around where Skyrim placed it instead: facing its way, its body
// flexing in a wave that grows toward the tail. Waterfall salmon leap now and then.
// Everything is a function of time and the placement, so fish need no saved state.

#define FISH_MAX 32          // drawn at once (the nearest within FISH_RADIUS_M)
#define FISH_RADIUS_M 45.0f
#define FISH_MODELS 8
#define FISH_SPEED_MS 0.7f
#define FISH_LOOP_MIN_M 0.9f // loop radius range
#define FISH_LOOP_MAX_M 2.0f
#define WIGGLE_HZ 2.6f       // tail beats per second
#define WIGGLE_WAVES 0.9f    // body lengths per wave
#define WIGGLE_M 0.045f      // sideways swing at the tail tip
#define LEAP_EVERY_S 6.0f    // waterfall salmon: one leap in about this long
#define LEAP_S 1.1f
#define LEAP_HEIGHT_M 1.1f
#define QUANT_M 2.0f         // fish vertices are quantized within this of the fish

typedef struct {
	int model; // objects model index, -1 free
	SkmMesh mesh;
	unsigned tex[4];
	float len, zmin, zmax; // body along model z (the head at +z)
	float uv_c[2], uv_e;   // uv quantization
	int leaps;             // a waterfall salmon
} FishModel;

static FishModel models[FISH_MODELS];
static char pack[128];
static ObjectVertex *verts;   // FISH_MAX * vertices of all submeshes
static int verts_cap;
static ObjectsDynamic draws[FISH_MAX * 4];

void fish_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	for (int i = 0; i < FISH_MODELS; i++)
		models[i].model = -1;
	verts_cap = FISH_MAX * 160; // (a salmon is ~140 vertices)
	verts = malloc(verts_cap * sizeof(ObjectVertex));
	if (!verts)
		verts_cap = 0;
}

static FishModel *model_for(int index) {
	for (int i = 0; i < FISH_MODELS; i++)
		if (models[i].model == index)
			return models[i].mesh.submesh_count ? &models[i] : NULL;
	for (int i = 0; i < FISH_MODELS; i++) {
		FishModel *f = &models[i];
		if (f->model >= 0)
			continue;
		f->model = index; // (also marks a failed load, so it isn't retried every frame)
		char path[400];
		snprintf(path, sizeof(path), "%smeshes/%s.skmesh", pack, objects_model_name(index));
		if (skmesh_load(path, &f->mesh) != 0) {
			memset(&f->mesh, 0, sizeof(f->mesh));
			return NULL;
		}
		f->leaps = strstr(objects_model_name(index), "waterfall") != NULL;
		float umin[2] = {1e9f, 1e9f}, umax[2] = {-1e9f, -1e9f};
		f->zmin = 1e9f, f->zmax = -1e9f;
		for (int s = 0; s < f->mesh.submesh_count && s < 4; s++) {
			SkmSubmesh *sm = &f->mesh.submeshes[s];
			f->tex[s] = objects_texture(sm->texture);
			for (uint32_t v = 0; v < sm->vertex_count; v++) {
				const SkmVertex *p = &sm->vertices[v];
				f->zmin = fminf(f->zmin, p->pos[2]), f->zmax = fmaxf(f->zmax, p->pos[2]);
				for (int k = 0; k < 2; k++)
					umin[k] = fminf(umin[k], p->uv[k]), umax[k] = fmaxf(umax[k], p->uv[k]);
			}
		}
		f->len = fmaxf(f->zmax - f->zmin, 0.05f);
		f->uv_c[0] = (umin[0] + umax[0]) * 0.5f, f->uv_c[1] = (umin[1] + umax[1]) * 0.5f;
		f->uv_e = fmaxf(fmaxf(umax[0] - umin[0], umax[1] - umin[1]) * 0.5f, 1e-3f);
		return f;
	}
	return NULL;
}

static float hash01(unsigned h) {
	h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
	return (h & 0xffffff) / 16777216.0f;
}

void fish_draw(float cam_x, float cam_z, float time_s) {
	ObjectsFish near[FISH_MAX];
	int n = objects_fish_near(cam_x, cam_z, FISH_RADIUS_M, near, FISH_MAX);
	int nd = 0, used = 0;
	for (int i = 0; i < n; i++) {
		FishModel *fm = model_for(near[i].model);
		if (!fm)
			continue;
		int need = 0;
		for (int s = 0; s < fm->mesh.submesh_count && s < 4; s++)
			need += fm->mesh.submeshes[s].vertex_count;
		if (used + need > verts_cap)
			break; // (more fish than the buffer: the farther ones wait)
		const ObjectsFish *f = &near[i];
		const float scale = sqrtf(f->m[0] * f->m[0] + f->m[3] * f->m[3] + f->m[6] * f->m[6]);
		// Its loop: radius, speed and direction of its own, starting anywhere on it
		const float r = FISH_LOOP_MIN_M + (FISH_LOOP_MAX_M - FISH_LOOP_MIN_M) * hash01(f->id);
		const float dir = hash01(f->id * 3u + 1u) < 0.5f ? -1.0f : 1.0f;
		const float speed = FISH_SPEED_MS * (0.75f + 0.5f * hash01(f->id * 7u + 2u));
		const float a = dir * time_s * speed / r + 6.2831853f * hash01(f->id * 11u + 3u);
		float cx = f->t[0] + r * cosf(a), cz = f->t[2] + r * sinf(a), cy = f->t[1];
		// Heading along the loop (the tangent), head at model +z
		float hx = -sinf(a) * dir, hz = cosf(a) * dir;
		float surface;
		if (objects_water_at(cx, cz, &surface) && cy > surface - 0.15f)
			cy = surface - 0.15f; // (under the surface, wherever the loop takes it)
		float pitch = 0.0f;
		if (fm->leaps) {
			float cycle = LEAP_EVERY_S * (0.8f + 0.4f * hash01(f->id * 13u + 4u));
			float u = fmodf(time_s + cycle * hash01(f->id * 17u + 5u), cycle) / LEAP_S;
			if (u < 1.0f) {
				cy += LEAP_HEIGHT_M * 4.0f * u * (1.0f - u); // a parabola out of the water
				pitch = (0.5f - u) * 1.6f;                   // nose up, then down
			}
		}
		cy += 0.04f * sinf(time_s * 1.3f + hash01(f->id * 19u + 6u) * 6.28f); // (a slow bob)
		const float cp = cosf(pitch), sp = sinf(pitch);
		const float q = 32767.0f / QUANT_M;
		const float beat = time_s * WIGGLE_HZ * 6.2831853f + hash01(f->id * 23u + 7u) * 6.28f;
		for (int s = 0; s < fm->mesh.submesh_count && s < 4 && nd < FISH_MAX * 4; s++) {
			const SkmSubmesh *sm = &fm->mesh.submeshes[s];
			ObjectVertex *out = verts + used;
			for (uint32_t v = 0; v < sm->vertex_count; v++) {
				const SkmVertex *p = &sm->vertices[v];
				// The wave travels from head to tail, growing toward the tail
				float from_head = (fm->zmax - p->pos[2]) / fm->len; // 0 head .. 1 tail
				float swing = WIGGLE_M * from_head * from_head *
				              sinf(beat - from_head * WIGGLE_WAVES * 6.2831853f);
				float lx = (p->pos[0] + swing) * scale, ly = p->pos[1] * scale, lz = p->pos[2] * scale;
				// Pitch about x, then heading: model +z -> (hx, hz)
				float y2 = ly * cp + lz * sp, z2 = -ly * sp + lz * cp;
				float wx = lx * hz + z2 * hx, wz = -lx * hx + z2 * hz;
				ObjectVertex *o = &out[v];
				o->x = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, wx * q));
				o->y = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, y2 * q));
				o->z = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, wz * q));
				o->pad = 0;
				o->u = (int16_t)((p->uv[0] - fm->uv_c[0]) / fm->uv_e * 32767.0f);
				o->v = (int16_t)((p->uv[1] - fm->uv_c[1]) / fm->uv_e * 32767.0f);
				float nx = p->n[0] / 127.0f, ny = p->n[1] / 127.0f, nz = p->n[2] / 127.0f;
				float n3[3] = {nx * hz + nz * hx, ny, -nx * hx + nz * hz};
				uint8_t rgb[3];
				lighting_bake(n3, p->col[0] / 255.0f, p->col[1] / 255.0f, p->col[2] / 255.0f, rgb);
				o->r = rgb[0], o->g = rgb[1], o->b = rgb[2], o->a = p->col[3];
			}
			ObjectsDynamic *d = &draws[nd++];
			d->vertices = out;
			d->vertex_count = (int)sm->vertex_count;
			d->indices = sm->indices;
			d->index_count = (int)sm->index_count;
			d->tex = fm->tex[s];
			d->pos[0] = cx, d->pos[1] = cy, d->pos[2] = cz, d->pos[3] = QUANT_M;
			d->uv[0] = fm->uv_c[0], d->uv[1] = fm->uv_c[1], d->uv[2] = fm->uv_e;
			d->alpha = (sm->flags & SKMESH_ALPHA_TEST) != 0;
			used += (int)sm->vertex_count;
		}
	}
	objects_draw_dynamic(draws, nd);
}
