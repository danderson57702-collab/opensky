#ifndef SKINVERT_H
#define SKINVERT_H

// One skinned vertex: its position through the blend of up to three bones' transforms, into
// quantized int16 (actors.c's skin). The NEON version keeps each bone as four columns (the
// rotation's three, then the translation) and does a vertex in a dozen vector multiply-adds;
// the Vita's scalar floats took most of a dungeon fight's frame (GCC won't vectorize floats
// for NEON without fast-math). The scalar one is the reference (and the host's without NEON).

#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define SKIN_NEON 1

typedef struct {
	float32x4_t c[4]; // rotation columns 0..2 (x, y, z, 0), translation
} SkinCols;

// From a row-major rotation and a translation
static inline void skin_cols(SkinCols *out, const float r[9], const float t[3]) {
	const float c0[4] = {r[0], r[3], r[6], 0.0f}, c1[4] = {r[1], r[4], r[7], 0.0f}, c2[4] = {r[2], r[5], r[8], 0.0f},
	            c3[4] = {t[0], t[1], t[2], 0.0f};
	out->c[0] = vld1q_f32(c0), out->c[1] = vld1q_f32(c1), out->c[2] = vld1q_f32(c2), out->c[3] = vld1q_f32(c3);
}

static inline void skin_vertex_neon(const SkinCols *b0, const SkinCols *b1, const SkinCols *b2, float w0, float w1,
                                    float w2, const float p[3], int16_t out[3]) {
	float32x4_t a0, a1, a2, a3;
	if (w0 > 0.996f) {
		a0 = b0->c[0], a1 = b0->c[1], a2 = b0->c[2], a3 = b0->c[3];
	} else {
		a0 = vmlaq_n_f32(vmulq_n_f32(b0->c[0], w0), b1->c[0], w1);
		a1 = vmlaq_n_f32(vmulq_n_f32(b0->c[1], w0), b1->c[1], w1);
		a2 = vmlaq_n_f32(vmulq_n_f32(b0->c[2], w0), b1->c[2], w1);
		a3 = vmlaq_n_f32(vmulq_n_f32(b0->c[3], w0), b1->c[3], w1);
		if (w2 > 0.004f) {
			a0 = vmlaq_n_f32(a0, b2->c[0], w2);
			a1 = vmlaq_n_f32(a1, b2->c[1], w2);
			a2 = vmlaq_n_f32(a2, b2->c[2], w2);
			a3 = vmlaq_n_f32(a3, b2->c[3], w2);
		}
	}
	float32x4_t o = vmlaq_n_f32(vmlaq_n_f32(vmlaq_n_f32(a3, a0, p[0]), a1, p[1]), a2, p[2]);
	o = vminq_f32(vmaxq_f32(o, vdupq_n_f32(-32767.0f)), vdupq_n_f32(32767.0f));
	const int32x4_t i = vcvtq_s32_f32(o);
	out[0] = (int16_t)vgetq_lane_s32(i, 0), out[1] = (int16_t)vgetq_lane_s32(i, 1), out[2] = (int16_t)vgetq_lane_s32(i, 2);
}
#endif

static inline void skin_vertex_scalar(const float *r0, const float *t0, const float *r1, const float *t1, const float *r2,
                                      const float *t2, float w0, float w1, float w2, const float p[3], int16_t out[3]) {
	const float *r, *t;
	float br[9], bt[3];
	if (w0 > 0.996f) {
		r = r0, t = t0;
	} else {
		for (int k = 0; k < 9; k++)
			br[k] = r0[k] * w0 + r1[k] * w1;
		for (int k = 0; k < 3; k++)
			bt[k] = t0[k] * w0 + t1[k] * w1;
		if (w2 > 0.004f) {
			for (int k = 0; k < 9; k++)
				br[k] += r2[k] * w2;
			for (int k = 0; k < 3; k++)
				bt[k] += t2[k] * w2;
		}
		r = br, t = bt;
	}
	for (int k = 0; k < 3; k++) {
		const float o = r[k * 3] * p[0] + r[k * 3 + 1] * p[1] + r[k * 3 + 2] * p[2] + t[k];
		out[k] = (int16_t)(o < -32767.0f ? -32767.0f : (o > 32767.0f ? 32767.0f : o));
	}
}

#endif
