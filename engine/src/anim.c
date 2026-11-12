#include "anim.h"
#include "gamelog.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fileio.h"

#define SKAN_VERSION 3

static void quat_matrix(const float q[4], float r[9]) {
	float x = q[0], y = q[1], z = q[2], w = q[3];
	r[0] = 1 - 2 * (y * y + z * z), r[1] = 2 * (x * y - z * w), r[2] = 2 * (x * z + y * w);
	r[3] = 2 * (x * y + z * w), r[4] = 1 - 2 * (x * x + z * z), r[5] = 2 * (y * z - x * w);
	r[6] = 2 * (x * z - y * w), r[7] = 2 * (y * z + x * w), r[8] = 1 - 2 * (x * x + y * y);
}

// out = a * b
static void compose(const BoneDelta *a, const BoneDelta *b, BoneDelta *out) {
	BoneDelta o;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++)
			o.r[i * 3 + j] = a->r[i * 3] * b->r[j] + a->r[i * 3 + 1] * b->r[3 + j] + a->r[i * 3 + 2] * b->r[6 + j];
		o.t[i] = a->r[i * 3] * b->t[0] + a->r[i * 3 + 1] * b->t[1] + a->r[i * 3 + 2] * b->t[2] + a->t[i];
	}
	*out = o;
}

static void invert(const BoneDelta *a, BoneDelta *out) {
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out->r[i * 3 + j] = a->r[j * 3 + i];
	for (int i = 0; i < 3; i++)
		out->t[i] = -(out->r[i * 3] * a->t[0] + out->r[i * 3 + 1] * a->t[1] + out->r[i * 3 + 2] * a->t[2]);
}

// World transforms of a pose, down the hierarchy (parents come first)
static void anim_deltas_world(const ClipSet *set, const BonePose *pose, BoneDelta *world) {
	for (int b = 0; b < set->bones; b++) {
		BoneDelta local;
		quat_matrix(pose[b].q, local.r);
		memcpy(local.t, pose[b].t, sizeof(local.t));
		if (set->parent[b] < 0)
			world[b] = local;
		else
			compose(&world[set->parent[b]], &local, &world[b]);
	}
}

void anim_deltas(const ClipSet *set, const BonePose *pose, BoneDelta *out) {
	BoneDelta world[ANIM_MAX_BONES];
	anim_deltas_world(set, pose, world);
	for (int b = 0; b < set->bones; b++)
		compose(&world[b], &set->rest_inverse[b], &out[b]);
}

int anim_load(const char *path, ClipSet *set) {
	memset(set, 0, sizeof(*set));
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	if (!d)
		return 0;
	uint16_t version, bones;
	uint32_t count;
	if (size < 12 || memcmp(d, "SKAN", 4))
		goto bad;
	memcpy(&version, d + 4, 2);
	memcpy(&bones, d + 6, 2);
	memcpy(&count, d + 8, 4);
	if (version != SKAN_VERSION || bones > ANIM_MAX_BONES)
		goto bad;
	long p = 12;
	// The bind pose: parents and local transforms -> world transforms, kept inverted
	if (p + (long)bones * 32 > size)
		goto bad;
	BonePose rest[ANIM_MAX_BONES];
	for (int b = 0; b < bones; b++) {
		int32_t parent;
		memcpy(&parent, d + p, 4);
		memcpy(rest[b].q, d + p + 4, 16);
		memcpy(rest[b].t, d + p + 20, 12);
		memcpy(set->rest_t[b], rest[b].t, 12);
		set->parent[b] = parent >= 0 && parent < b ? parent : -1;
		p += 32;
	}
	set->bones = bones;
	BoneDelta world[ANIM_MAX_BONES];
	anim_deltas_world(set, rest, world);
	for (int b = 0; b < bones; b++)
		invert(&world[b], &set->rest_inverse[b]);
	if (count > ANIM_MAX_CLIPS) // (more than we keep: the last ones would be lost - say so)
		game_log("anim: %s has %u clips, only %d kept", path, (unsigned)count, ANIM_MAX_CLIPS);
	for (uint32_t i = 0; i < count && set->count < ANIM_MAX_CLIPS; i++) {
		Clip *c = &set->clips[set->count];
		if (p + 40 > size)
			goto bad;
		memcpy(c->name, d + p, 16);
		c->name[16] = 0;
		uint32_t frames;
		memcpy(&c->duration, d + p + 16, 4);
		memcpy(&frames, d + p + 20, 4);
		memcpy(&c->vx, d + p + 24, 4);
		memcpy(&c->vz, d + p + 28, 4);
		memcpy(&c->moved, d + p + 32, 8);
		p += 40;
		int moved = 0;
		for (int b = 0; b < bones; b++)
			moved += (int)((c->moved >> b) & 1);
		c->stride = bones * 8 + moved * 12;
		long bytes = (long)frames * c->stride;
		if (frames < 1 || p + bytes > size)
			goto bad;
		c->frames = (int)frames;
		c->speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
		c->data = d + p; // 4-byte aligned in the file
		p += bytes;
		set->count++;
	}
	set->file = d;
	return 1;
bad:
	free(d);
	memset(set, 0, sizeof(*set));
	return 0;
}

const Clip *anim_clip(const ClipSet *set, const char *name) {
	for (int i = 0; i < set->count; i++)
		if (!strcmp(set->clips[i].name, name))
			return &set->clips[i];
	return NULL;
}

static void nlerp(float out[4], const float a[4], const float b[4], float w) {
	float s = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0f ? -1.0f : 1.0f;
	float len = 0.0f;
	for (int k = 0; k < 4; k++) {
		out[k] = a[k] + (s * b[k] - a[k]) * w;
		len += out[k] * out[k];
	}
	len = len > 1e-12f ? 1.0f / sqrtf(len) : 0.0f;
	for (int k = 0; k < 4; k++)
		out[k] *= len;
}

void anim_sample(const ClipSet *set, const Clip *clip, float t, BonePose *out) {
	int n = set->bones;
	float frame = 0.0f;
	if (clip->frames > 1 && clip->duration > 0.0f) {
		float u = fmodf(t, clip->duration);
		if (u < 0.0f)
			u += clip->duration;
		frame = u / clip->duration * (clip->frames - 1);
	}
	int f0 = (int)frame, f1 = f0 + 1 < clip->frames ? f0 + 1 : f0;
	float w = frame - f0;
	const uint8_t *fa = clip->data + (size_t)f0 * clip->stride, *fb = clip->data + (size_t)f1 * clip->stride;
	const int16_t *qa = (const int16_t *)fa, *qb = (const int16_t *)fb;
	const float *ta = (const float *)(fa + n * 8), *tb = (const float *)(fb + n * 8);
	for (int i = 0; i < n; i++) {
		float a[4], b[4];
		for (int k = 0; k < 4; k++)
			a[k] = qa[i * 4 + k] * (1.0f / 32767.0f), b[k] = qb[i * 4 + k] * (1.0f / 32767.0f);
		nlerp(out[i].q, a, b, w);
		if ((clip->moved >> i) & 1) {
			for (int k = 0; k < 3; k++)
				out[i].t[k] = ta[k] + (tb[k] - ta[k]) * w;
			ta += 3, tb += 3;
		} else {
			memcpy(out[i].t, set->rest_t[i], sizeof(out[i].t));
		}
	}
}

void anim_blend(BonePose *a, const BonePose *b, int bones, float w) {
	if (w <= 0.0f)
		return;
	for (int i = 0; i < bones; i++) {
		float q[4];
		nlerp(q, a[i].q, b[i].q, w > 1.0f ? 1.0f : w);
		memcpy(a[i].q, q, sizeof(q));
		for (int k = 0; k < 3; k++)
			a[i].t[k] += (b[i].t[k] - a[i].t[k]) * (w > 1.0f ? 1.0f : w);
	}
}
