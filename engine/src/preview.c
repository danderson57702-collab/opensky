#include "preview.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "atmosphere.h"
#include "fileio.h"
#include "objects.h"
#include "skmesh.h"

#define OVERLAY_DEPTH 0.002f
#define FIT_RADIUS_M 0.62f  // the item is scaled to this...
#define VIEW_DIST 3.4f      // ...this far in front of the camera...
#define VIEW_X 1.05f        // ...this far to the right and up (the upper right of the screen)
#define VIEW_Y 0.55f
#define BESIDE_X 1.62f      // beside a container's list (the menu's panel is in the middle)...
#define BESIDE_Y 0.2f
#define BESIDE_FIT 0.62f    // ...smaller, to fit the space
#define TURN_RATE 0.6f      // the sway's pace (radians per second)
#define SWAY_RAD 0.6f       // how far it turns either way (its front stays toward you)
#define QUANT_M 1.5f

static char pack[128];
static int model = -1, loaded = -1;
static SkmMesh mesh;
static unsigned tex[8];
static ObjectVertex *verts[8];
static float centre[3], scale = 1.0f, uv_c[2], uv_e = 1.0f;
// Where it's shown (preview_place): the inventory's upper right, or beside a container's list
static float view_x = VIEW_X, view_y = VIEW_Y, view_k = 1.0f;
// Looked at closely (preview_inspect): in the middle of the screen, bigger, turned by hand - a
// claw's signs, a note's seal: what the Vita's screen is too small to show in the corner
#define INSPECT_K 2.2f
#define INSPECT_DIST 2.7f
static int inspecting;
static float inspect_yaw, inspect_tilt;

void preview_inspect(int on) {
	inspecting = on;
	inspect_yaw = 0.0f, inspect_tilt = 0.25f;
}

int preview_inspecting(void) {
	return inspecting && model >= 0;
}

void preview_turn(float yaw, float tilt) {
	inspect_yaw += yaw;
	inspect_tilt = fmaxf(-1.4f, fminf(1.4f, inspect_tilt + tilt));
}
// Skyrim's inventory views (items/inventory.txt): per model, how it's turned to be shown
#define MAX_VIEWS 2048
static struct {
	int model;
	float m[9];
} views[MAX_VIEWS];
static int view_count;

void preview_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	char path[256];
	snprintf(path, sizeof(path), "%sitems/inventory.txt", pack);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line && view_count < MAX_VIEWS; line = strtok(NULL, "\n")) {
		float *m = views[view_count].m;
		if (sscanf(line, "%d %f %f %f %f %f %f %f %f %f", &views[view_count].model, &m[0], &m[1], &m[2], &m[3],
		           &m[4], &m[5], &m[6], &m[7], &m[8]) == 10)
			view_count++;
	}
	free(buf);
}

// Turns the loaded mesh as Skyrim's inventory shows the model (if it says how)
static void apply_view(void) {
	const float *m = NULL;
	for (int i = 0; i < view_count && !m; i++)
		if (views[i].model == model)
			m = views[i].m;
	if (!m)
		return;
	for (int s = 0; s < mesh.submesh_count; s++)
		for (uint32_t v = 0; v < mesh.submeshes[s].vertex_count; v++) {
			SkmVertex *x = &mesh.submeshes[s].vertices[v];
			float p[3] = {x->pos[0], x->pos[1], x->pos[2]}, n[3] = {x->n[0], x->n[1], x->n[2]};
			for (int k = 0; k < 3; k++) {
				x->pos[k] = m[k * 3] * p[0] + m[k * 3 + 1] * p[1] + m[k * 3 + 2] * p[2];
				x->n[k] = (int8_t)(m[k * 3] * n[0] + m[k * 3 + 1] * n[1] + m[k * 3 + 2] * n[2]);
			}
		}
}

void preview_place(int beside_list) {
	view_x = beside_list ? BESIDE_X : VIEW_X, view_y = beside_list ? BESIDE_Y : VIEW_Y;
	view_k = beside_list ? BESIDE_FIT : 1.0f;
}

void preview_set(int m) {
	model = m;
}

static void unload(void) {
	for (int s = 0; s < 8; s++)
		free(verts[s]), verts[s] = NULL;
	if (loaded >= 0)
		skmesh_free(&mesh);
	memset(&mesh, 0, sizeof(mesh));
	loaded = -1;
}

static int load(void) {
	unload();
	char path[400];
	snprintf(path, sizeof(path), "%smeshes/%s.skmesh", pack, objects_model_name(model));
	if (skmesh_load(path, &mesh) != 0) {
		memset(&mesh, 0, sizeof(mesh));
		return 0;
	}
	apply_view();
	float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}, umin[2] = {1e9f, 1e9f}, umax[2] = {-1e9f, -1e9f};
	for (int s = 0; s < mesh.submesh_count && s < 8; s++) {
		const SkmSubmesh *sm = &mesh.submeshes[s];
		tex[s] = objects_texture(sm->texture);
		verts[s] = malloc(sm->vertex_count * sizeof(ObjectVertex));
		for (uint32_t v = 0; v < sm->vertex_count; v++) {
			for (int k = 0; k < 3; k++)
				lo[k] = fminf(lo[k], sm->vertices[v].pos[k]), hi[k] = fmaxf(hi[k], sm->vertices[v].pos[k]);
			for (int k = 0; k < 2; k++)
				umin[k] = fminf(umin[k], sm->vertices[v].uv[k]), umax[k] = fmaxf(umax[k], sm->vertices[v].uv[k]);
		}
	}
	float r = 0.0f;
	for (int k = 0; k < 3; k++)
		centre[k] = (lo[k] + hi[k]) * 0.5f, r = fmaxf(r, (hi[k] - lo[k]) * 0.5f);
	scale = r > 1e-4f ? FIT_RADIUS_M / r : 1.0f;
	uv_c[0] = (umin[0] + umax[0]) * 0.5f, uv_c[1] = (umin[1] + umax[1]) * 0.5f;
	uv_e = fmaxf(fmaxf(umax[0] - umin[0], umax[1] - umin[1]) * 0.5f, 1e-3f);
	loaded = model;
	return 1;
}

void preview_draw(float time_s) {
	if (model < 0)
		return;
	if (loaded != model && !load()) {
		model = -1;
		return;
	}
	// Swaying about the vertical, tipped toward the viewer a little; lit from the front left
	const float a = inspecting ? inspect_yaw : sinf(time_s * TURN_RATE) * SWAY_RAD, ca = cosf(a), sa = sinf(a),
	            tilt = inspecting ? inspect_tilt : 0.25f, ct = cosf(tilt), st = sinf(tilt);
	const float light[3] = {-0.45f, 0.55f, 0.70f}, q = 32767.0f / QUANT_M;
	static ObjectsDynamic draws[8];
	int nd = 0;
	for (int s = 0; s < mesh.submesh_count && s < 8; s++) {
		const SkmSubmesh *sm = &mesh.submeshes[s];
		for (uint32_t v = 0; v < sm->vertex_count; v++) {
			const SkmVertex *src = &sm->vertices[v];
			const float k = scale * (inspecting ? INSPECT_K : view_k);
			float p[3] = {(src->pos[0] - centre[0]) * k, (src->pos[1] - centre[1]) * k, (src->pos[2] - centre[2]) * k};
			float x = p[0] * ca + p[2] * sa, z = -p[0] * sa + p[2] * ca, y = p[1];
			float y2 = y * ct - z * st, z2 = y * st + z * ct;
			float n0 = src->n[0] / 127.0f, n1 = src->n[1] / 127.0f, n2 = src->n[2] / 127.0f;
			float nx = n0 * ca + n2 * sa, nz = -n0 * sa + n2 * ca;
			float ny2 = n1 * ct - nz * st, nz2 = n1 * st + nz * ct;
			float lit = 0.45f + 0.75f * fmaxf(0.0f, nx * light[0] + ny2 * light[1] + nz2 * light[2]);
			ObjectVertex *o = &verts[s][v];
			o->x = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, x * q));
			o->y = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, y2 * q));
			o->z = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, z2 * q));
			o->pad = 0;
			o->u = (int16_t)((src->uv[0] - uv_c[0]) / uv_e * 32767.0f);
			o->v = (int16_t)((src->uv[1] - uv_c[1]) / uv_e * 32767.0f);
			o->r = (uint8_t)fminf(255.0f, src->col[0] * lit * 0.55f);
			o->g = (uint8_t)fminf(255.0f, src->col[1] * lit * 0.55f);
			o->b = (uint8_t)fminf(255.0f, src->col[2] * lit * 0.55f);
			o->a = src->col[3];
		}
		ObjectsDynamic *d = &draws[nd++];
		d->vertices = verts[s];
		d->vertex_count = (int)sm->vertex_count;
		d->indices = sm->indices;
		d->index_count = (int)sm->index_count;
		d->tex = tex[s];
		d->pos[0] = inspecting ? 0.0f : view_x, d->pos[1] = inspecting ? 0.0f : view_y;
		d->pos[2] = inspecting ? -INSPECT_DIST : -VIEW_DIST, d->pos[3] = QUANT_M;
		d->uv[0] = uv_c[0], d->uv[1] = uv_c[1], d->uv[2] = uv_e;
		d->alpha = (sm->flags & SKMESH_ALPHA_TEST) != 0;
	}
	glClear(GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluPerspective(38.0, 960.0 / 544.0, 0.1, 20.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	atmosphere_set_camera(0.0f, 0.0f, 0.0f);
	atmosphere_set_torch(NULL, 0.0f, NULL);
	atmosphere_set_clear(1);
	glDisable(GL_CULL_FACE); // (single-sided cloth and blades from behind)
	// (in the front sliver of the depth range: always over the world, whatever its depth
	// buffer holds - in a cave the walls were nearer than the clear left them)
	glDepthRangef(0.0f, OVERLAY_DEPTH);
	objects_draw_dynamic(draws, nd);
	glDepthRangef(0.0f, 1.0f);
	glEnable(GL_CULL_FACE);
	atmosphere_set_clear(0);
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}
