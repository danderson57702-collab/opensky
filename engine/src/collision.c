#include "collision.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "terrain.h"

#define BUCKET_M 4.0f
#define WALKABLE_NY 0.5f // surfaces up to ~60 degrees can be stood on (Skyrim is generous too)
#define PLAYER_HEIGHT 1.8f // head clearance needed above the feet
#define GRAVITY PLAYER_GRAVITY
#define MAX_SUBSTEP_M 0.25f
#define SWIM_RISE_MS 0.8f // floating back up to the surface (after a dive, or falling in)
#define MIN_TRI_AREA2 1e-8f // (2 x area)^2 below this: degenerate, skipped

// One grid covering the region around the player (rebuilt when the player changes cell)
struct CollisionRegion {
	int valid;
	float ox, oz;      // grid origin (min x, min z)
	int nx, nz;        // buckets per axis
	float *tris;       // count * 9
	float *normals;    // count * 3
	int *tags;         // count, or NULL
	int count;
	int *bucket_start; // nx * nz + 1
	int *bucket_tris;
};
static CollisionRegion region; // the installed one
// Moving collision (doors), replaced every time it changes
#define DYN_MAX 2048
static float dyn_tris[DYN_MAX * 9], dyn_normals[DYN_MAX * 3];
static int dyn_count;
// ... in runs of DYN_RUN, each run's bounds (min x, min z, max x, max z, grown by DYN_MARGIN_M): a
// query skips the runs nowhere near it (Helgen Keep's gates and doors: every one tested by every
// step and camera ray - the keep's movement and camera ran 7-9 ms a frame on the Vita)
#define DYN_RUN 16
#define DYN_MARGIN_M 2.5f
static float dyn_box[(DYN_MAX / DYN_RUN + 1) * 4];

void collision_set_dynamic(const float *tris, int count) {
	dyn_count = 0;
	for (int i = 0; i < count && dyn_count < DYN_MAX; i++) {
		const float *v = &tris[i * 9];
		float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]}, e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
		float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (!(len * len > MIN_TRI_AREA2))
			continue;
		memcpy(&dyn_tris[dyn_count * 9], v, 9 * sizeof(float));
		for (int k = 0; k < 3; k++)
			dyn_normals[dyn_count * 3 + k] = n[k] / len;
		dyn_count++;
	}
	for (int r = 0; r * DYN_RUN < dyn_count; r++) {
		float *b = &dyn_box[r * 4];
		b[0] = b[1] = 1e30f, b[2] = b[3] = -1e30f;
		for (int i = r * DYN_RUN; i < dyn_count && i < (r + 1) * DYN_RUN; i++)
			for (int k = 0; k < 3; k++) {
				const float *q = &dyn_tris[i * 9 + k * 3];
				b[0] = fminf(b[0], q[0]), b[1] = fminf(b[1], q[2]), b[2] = fmaxf(b[2], q[0]), b[3] = fmaxf(b[3], q[2]);
			}
		b[0] -= DYN_MARGIN_M, b[1] -= DYN_MARGIN_M, b[2] += DYN_MARGIN_M, b[3] += DYN_MARGIN_M;
	}
}

int collision_dynamic_count(void) {
	return dyn_count;
}

// --- Building -------------------------------------------------------------------------

static int bucket_range(float lo, float hi, float origin, int n, int *b0, int *b1) {
	*b0 = (int)floorf((lo - origin) / BUCKET_M);
	*b1 = (int)floorf((hi - origin) / BUCKET_M);
	if (*b1 < 0 || *b0 >= n)
		return 0;
	if (*b0 < 0) *b0 = 0;
	if (*b1 >= n) *b1 = n - 1;
	return 1;
}

static void free_region(CollisionRegion *r) {
	free(r->tris);
	free(r->normals);
	free(r->tags);
	free(r->bucket_start);
	free(r->bucket_tris);
	memset(r, 0, sizeof(*r));
}

void collision_clear(void) {
	free_region(&region);
}

void collision_free(CollisionRegion *r) {
	if (r) {
		free_region(r);
		free(r);
	}
}

void collision_install(CollisionRegion *r) {
	free_region(&region);
	region = *r;
	free(r);
}

static CollisionBlockers blockers;

CollisionBlockers collision_last_blockers(void) {
	return blockers;
}

void collision_set_region(float min_x, float min_z, float max_x, float max_z, const float *tris,
                          const int *tags, int count) {
	CollisionRegion *r = collision_build(min_x, min_z, max_x, max_z, tris, tags, count);
	if (r)
		collision_install(r);
	else
		collision_clear();
}

// Pure (touches nothing global): safe to run on the streaming thread
CollisionRegion *collision_build(float min_x, float min_z, float max_x, float max_z, const float *tris,
                                 const int *tags, int count) {
	CollisionRegion *rp = calloc(1, sizeof(*rp));
	if (!rp)
		return NULL;
#define region (*rp)
	region.ox = min_x, region.oz = min_z;
	region.nx = (int)ceilf((max_x - min_x) / BUCKET_M) + 1;
	region.nz = (int)ceilf((max_z - min_z) / BUCKET_M) + 1;
	const int nb = region.nx * region.nz;

	// Keep only real triangles: Skyrim meshes contain zero-area ones, which have no
	// surface (and make the closest-point math divide 0 by 0)
	region.tris = malloc((count ? count : 1) * 9 * sizeof(float));
	region.normals = malloc((count ? count : 1) * 3 * sizeof(float));
	region.tags = tags ? malloc((count ? count : 1) * sizeof(int)) : NULL;
	if (!region.tris || !region.normals || (tags && !region.tags)) { // (out of memory: nothing)
		free_region(rp);
		free(rp);
		return NULL;
	}
	int kept = 0;
	for (int t = 0; t < count; t++) {
		const float *v = &tris[t * 9];
		float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]};
		float e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
		float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
		float len2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
		if (!(len2 > MIN_TRI_AREA2) || !isfinite(len2))
			continue;
		float len = sqrtf(len2);
		memcpy(&region.tris[kept * 9], v, 9 * sizeof(float));
		if (tags)
			region.tags[kept] = tags[t];
		region.normals[kept * 3] = n[0] / len, region.normals[kept * 3 + 1] = n[1] / len, region.normals[kept * 3 + 2] = n[2] / len;
		kept++;
	}
	region.count = kept;

	// Bucket membership (triangle AABB grown by the player radius), CSR layout in two passes
	region.bucket_start = calloc(nb + 1, sizeof(int));
	int *fill = NULL;
	if (!region.bucket_start) {
		free_region(rp);
		free(rp);
		return NULL;
	}
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			for (int b = 0; b < nb; b++)
				region.bucket_start[b + 1] += region.bucket_start[b];
			region.bucket_tris = malloc((region.bucket_start[nb] + 1) * sizeof(int));
			fill = calloc(nb, sizeof(int));
			if (!region.bucket_tris || !fill) {
				free(fill);
				free_region(rp);
				free(rp);
				return NULL;
			}
		}
		for (int t = 0; t < kept; t++) {
			const float *v = &region.tris[t * 9];
			float minx = fminf(v[0], fminf(v[3], v[6])) - PLAYER_RADIUS, maxx = fmaxf(v[0], fmaxf(v[3], v[6])) + PLAYER_RADIUS;
			float minz = fminf(v[2], fminf(v[5], v[8])) - PLAYER_RADIUS, maxz = fmaxf(v[2], fmaxf(v[5], v[8])) + PLAYER_RADIUS;
			int bx0, bx1, bz0, bz1;
			if (!bucket_range(minx, maxx, region.ox, region.nx, &bx0, &bx1) ||
			    !bucket_range(minz, maxz, region.oz, region.nz, &bz0, &bz1))
				continue;
			for (int bz = bz0; bz <= bz1; bz++)
				for (int bx = bx0; bx <= bx1; bx++) {
					int b = bz * region.nx + bx;
					if (pass == 0)
						region.bucket_start[b + 1]++;
					else
						region.bucket_tris[region.bucket_start[b] + fill[b]++] = t;
				}
		}
	}
	free(fill);
	region.valid = 1;
#undef region
	return rp;
}

// --- Queries ---------------------------------------------------------------------------

// Calls fn for every triangle bucketed at (x, z) in every collision cell covering it.
#define FOR_EACH_TRI_AT(x, z, ...)                                                                \
	if (region.valid) {                                                                           \
		int bx_ = (int)floorf(((x) - region.ox) / BUCKET_M), bz_ = (int)floorf(((z) - region.oz) / BUCKET_M); \
		if (bx_ >= 0 && bz_ >= 0 && bx_ < region.nx && bz_ < region.nz) {                         \
			int b_ = bz_ * region.nx + bx_;                                                         \
			for (int k_ = region.bucket_start[b_]; k_ < region.bucket_start[b_ + 1]; k_++) {         \
				int t = region.bucket_tris[k_];                                                     \
				const float *v = &region.tris[t * 9];                                               \
				const float *n = &region.normals[t * 3];                                            \
				__VA_ARGS__                                                                         \
			}                                                                                       \
		}                                                                                           \
	}                                                                                             \
	for (int d_ = 0; d_ < dyn_count; d_++) { /* moving things (doors): the runs near (x, z) */    \
		if (d_ % DYN_RUN == 0) {                                                                  \
			const float *b_ = &dyn_box[(d_ / DYN_RUN) * 4];                                       \
			if ((x) < b_[0] || (z) < b_[1] || (x) > b_[2] || (z) > b_[3]) {                       \
				d_ += DYN_RUN - 1;                                                                \
				continue;                                                                         \
			}                                                                                     \
		}                                                                                         \
		int t = -1;                                                                               \
		const float *v = &dyn_tris[d_ * 9];                                                       \
		const float *n = &dyn_normals[d_ * 3];                                                    \
		(void)t;                                                                                  \
		__VA_ARGS__                                                                               \
	}
#define TAG_OF(t) (region.tags && (t) >= 0 ? region.tags[t] : -2)

// Closest point on triangle abc to p (Ericson, Real-Time Collision Detection 5.1.5)
static void closest_on_triangle(const float *p, const float *a, const float *b, const float *c, float *out) {
	float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
	float ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
#define DOT(u, w) ((u)[0] * (w)[0] + (u)[1] * (w)[1] + (u)[2] * (w)[2])
#define SET(o, s, t)                                                                            \
	do {                                                                                        \
		(o)[0] = a[0] + ab[0] * (s) + ac[0] * (t);                                              \
		(o)[1] = a[1] + ab[1] * (s) + ac[1] * (t);                                              \
		(o)[2] = a[2] + ab[2] * (s) + ac[2] * (t);                                              \
	} while (0)
	float d1 = DOT(ab, ap), d2 = DOT(ac, ap);
	if (d1 <= 0 && d2 <= 0) { SET(out, 0, 0); return; }
	float bp[3] = {p[0] - b[0], p[1] - b[1], p[2] - b[2]};
	float d3 = DOT(ab, bp), d4 = DOT(ac, bp);
	if (d3 >= 0 && d4 <= d3) { SET(out, 1, 0); return; }
	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) { float v = d1 / (d1 - d3); SET(out, v, 0); return; }
	float cp[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
	float d5 = DOT(ab, cp), d6 = DOT(ac, cp);
	if (d6 >= 0 && d5 <= d6) { SET(out, 0, 1); return; }
	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) { float w = d2 / (d2 - d6); SET(out, 0, w); return; }
	float va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
		float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		SET(out, 1 - w, w);
		return;
	}
	float denom = 1.0f / (va + vb + vc);
	SET(out, vb * denom, vc * denom);
#undef SET
#undef DOT
}

// Height of triangle t's plane at (x, z) if (x, z) lies inside it (xz projection).
static int tri_height_at(const float *v, float x, float z, float *y) {
	float d = (v[5] - v[8]) * (v[0] - v[6]) + (v[6] - v[3]) * (v[2] - v[8]);
	if (fabsf(d) < 1e-8f)
		return 0;
	float l1 = ((v[5] - v[8]) * (x - v[6]) + (v[6] - v[3]) * (z - v[8])) / d;
	float l2 = ((v[8] - v[2]) * (x - v[6]) + (v[0] - v[6]) * (z - v[8])) / d;
	float l3 = 1.0f - l1 - l2;
	if (l1 < 0 || l2 < 0 || l3 < 0)
		return 0;
	*y = l1 * v[1] + l2 * v[4] + l3 * v[7];
	return 1;
}

// Lowest downward-facing surface (an underside: ceiling, overhang, rock belly) above
// min_y at (x, z); INFINITY if none. Skyrim's collision meshes are consistently wound
// outward, so the facing tells undersides from floors.
static float object_ceiling(float x, float z, float min_y) {
	float best = INFINITY;
	FOR_EACH_TRI_AT(x, z, {
		float y;
		if (n[1] < -0.05f && tri_height_at(v, x, z, &y) && y > min_y && y < best)
			best = y;
	})
	return best;
}

// Highest walkable object surface under (x, z) at or below max_y; -INFINITY if none.
// If above_tag is given, it receives the tag of the lowest walkable surface *above*
// max_y (the ledge that was too high), or -1.
static float object_ground_at(float x, float z, float max_y, int *above_tag, float *above_y, float *above) {
	float best = -INFINITY;
	FOR_EACH_TRI_AT(x, z, {
		// Upward-facing only: the underside of a rock is not a floor
		float y;
		if (n[1] < WALKABLE_NY || !tri_height_at(v, x, z, &y))
			continue;
		if (y <= max_y && y > best)
			best = y;
		else if (y > max_y && y < *above && above_tag) {
			*above = y;
			*above_tag = TAG_OF(t);
			*above_y = y;
		}
	})
	return best;
}

// The feet have a footprint: the highest surface under the centre or a little around it.
// (Stair treads are often separate blocks with hairline gaps between them; a single point
// fell through the gap onto the floor below and couldn't step back up.)
#define FOOT_RADIUS 0.15f
static float object_ground_ex(float x, float z, float max_y, int *above_tag, float *above_y) {
	static const float ring[5][2] = {{0, 0}, {FOOT_RADIUS, 0}, {-FOOT_RADIUS, 0}, {0, FOOT_RADIUS}, {0, -FOOT_RADIUS}};
	float best = -INFINITY, above = INFINITY;
	if (above_tag)
		*above_tag = -1;
	for (int i = 0; i < 5; i++)
		best = fmaxf(best, object_ground_at(x + ring[i][0], z + ring[i][1], max_y, above_tag, above_y, &above));
	return best;
}

static float object_ground(float x, float z, float max_y) {
	return object_ground_ex(x, z, max_y, NULL, NULL);
}

// Pushes (x, z) out of non-floor triangles touching spheres along the player's body.
static void push_out(float *x, float *z, float feet) {
	static const float heights[3] = {0.6f, 1.1f, 1.6f};
	for (int iter = 0; iter < 3; iter++) {
		int moved = 0;
		for (int h = 0; h < 3; h++) {
			float p[3] = {*x, feet + heights[h], *z};
			FOR_EACH_TRI_AT(*x, *z, {
				float q[3];
				closest_on_triangle(p, v, v + 3, v + 6, q);
				float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
				float d2 = dx * dx + dy * dy + dz * dz;
				if (!(d2 < PLAYER_RADIUS * PLAYER_RADIUS)) // also rejects NaN
					continue;
				// Anything touched below step height (floors, plank edges, stair risers) is
				// the ground query's job: it steps up or blocks. Pushing against it here
				// makes the player snag on every small lip.
				if (q[1] <= feet + PLAYER_STEP_HEIGHT)
					continue;
				float hl = sqrtf(dx * dx + dz * dz);
				float depth = PLAYER_RADIUS - sqrtf(d2);
				// One-sided, like Skyrim's (outward-wound) collision: a face whose back
				// the sphere centre is on belongs to the far side of the object and would
				// push the wrong way (into thin rails, or into the middle of a rock)
				if (dx * n[0] + dy * n[1] + dz * n[2] < 0.0f)
					continue;
				if (hl < 1e-4f) {
					// Contact point straight above/below: push along the surface normal
					dx = n[0], dz = n[2];
					hl = sqrtf(dx * dx + dz * dz);
					if (hl < 1e-4f)
						continue;
				}
				p[0] += dx / hl * depth;
				p[2] += dz / hl * depth;
				moved = 1;
				blockers.wall_tag = TAG_OF(t);
				blockers.wall_y = q[1];
			})
			*x = p[0], *z = p[2];
		}
		if (!moved)
			break;
	}
}

static float dot3(const float *u, const float *w) {
	return u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
}

// Segment / triangle (Moller-Trumbore, both sides): the hit's fraction along a -> b, or > 1
static float segment_triangle(const float *a, const float *d, const float *v) {
	float e1[3], e2[3], p[3], q[3], s[3];
	for (int k = 0; k < 3; k++)
		e1[k] = v[3 + k] - v[k], e2[k] = v[6 + k] - v[k], s[k] = a[k] - v[k];
	p[0] = d[1] * e2[2] - d[2] * e2[1], p[1] = d[2] * e2[0] - d[0] * e2[2], p[2] = d[0] * e2[1] - d[1] * e2[0];
	float det = dot3(e1, p);
	if (fabsf(det) < 1e-9f)
		return 2.0f;
	float inv = 1.0f / det, u = dot3(s, p) * inv;
	if (u < 0.0f || u > 1.0f)
		return 2.0f;
	q[0] = s[1] * e1[2] - s[2] * e1[1], q[1] = s[2] * e1[0] - s[0] * e1[2], q[2] = s[0] * e1[1] - s[1] * e1[0];
	float w = dot3(d, q) * inv;
	if (w < 0.0f || u + w > 1.0f)
		return 2.0f;
	float t = dot3(e2, q) * inv;
	return t >= 0.0f ? t : 2.0f;
}

float collision_ray(const float a[3], const float b[3]) {
	float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, best = 1.0f;
	float len = sqrtf(d[0] * d[0] + d[2] * d[2]);
	int steps = (int)(len / (BUCKET_M * 0.5f)) + 2;
	int last_bucket = -1;
	for (int i = 0; i <= steps; i++) { // the buckets the segment crosses
		float f = (float)i / steps, x = a[0] + d[0] * f, z = a[2] + d[2] * f;
		int bx = (int)floorf((x - region.ox) / BUCKET_M), bz = (int)floorf((z - region.oz) / BUCKET_M);
		if (!region.valid || bx < 0 || bz < 0 || bx >= region.nx || bz >= region.nz || bz * region.nx + bx == last_bucket)
			continue;
		last_bucket = bz * region.nx + bx;
		const int saved = dyn_count; // (moving triangles: tested once, below)
		dyn_count = 0;
		FOR_EACH_TRI_AT(x, z, {
			(void)n;
			float t = segment_triangle(a, d, v);
			if (t < best)
				best = t;
		});
		dyn_count = saved;
	}
	const float lo_x = fminf(a[0], b[0]), hi_x = fmaxf(a[0], b[0]), lo_z = fminf(a[2], b[2]), hi_z = fmaxf(a[2], b[2]);
	for (int i = 0; i < dyn_count; i++) {
		if (i % DYN_RUN == 0) { // (the runs the segment's bounds don't touch: skipped)
			const float *r = &dyn_box[(i / DYN_RUN) * 4];
			if (hi_x < r[0] || hi_z < r[1] || lo_x > r[2] || lo_z > r[3]) {
				i += DYN_RUN - 1;
				continue;
			}
		}
		float t = segment_triangle(a, d, &dyn_tris[i * 9]);
		if (t < best)
			best = t;
	}
	return best;
}

float collision_ray_ground(const float a[3], const float b[3]) {
	float best = collision_ray(a, b);
	const float len = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
	const int steps = (int)(len / 0.4f) + 1;
	float prev = 0.0f;
	for (int i = 1; i <= steps; i++) { // marched in short steps, the crossing refined
		const float f = (float)i / steps * best;
		float ground;
		if (!terrain_height_at(a[0] + (b[0] - a[0]) * f, a[2] + (b[2] - a[2]) * f, &ground))
			continue;
		if (a[1] + (b[1] - a[1]) * f < ground) {
			float lo = prev, hi = f;
			for (int k = 0; k < 8; k++) {
				const float m = (lo + hi) * 0.5f;
				float g;
				if (terrain_height_at(a[0] + (b[0] - a[0]) * m, a[2] + (b[2] - a[2]) * m, &g) &&
				    a[1] + (b[1] - a[1]) * m < g)
					hi = m;
				else
					lo = m;
			}
			return hi;
		}
		prev = f;
	}
	return best;
}

void collision_jump(Player *p) {
	if (p->grounded) {
		p->vy = PLAYER_JUMP_SPEED;
		p->grounded = 0;
	}
}

static int player_finite(const Player *p) {
	return isfinite(p->x) && isfinite(p->z) && isfinite(p->feet) && isfinite(p->vy);
}

int collision_move(Player *pl, float dx, float dz, float dt) {
	if (!isfinite(dx) || !isfinite(dz))
		return 0;
	const Player before = *pl;
	blockers.wall_tag = blockers.ledge_tag = -1;
	float dist = sqrtf(dx * dx + dz * dz);
	int steps = (int)ceilf(dist / MAX_SUBSTEP_M);
	if (steps < 1)
		steps = 1;
	for (int s = 0; s < steps; s++) {
		float x = pl->x + dx / steps, z = pl->z + dz / steps;
		push_out(&x, &z, pl->feet);
		if (!isfinite(x) || !isfinite(z))
			continue;
		// Try the full move, then slide along the obstacle (each axis alone)
		const float cand[3][2] = {{x, z}, {x, pl->z}, {pl->x, z}};
		for (int c = 0; c < 3; c++) {
			float terrain;
			if (!terrain_height_at(cand[c][0], cand[c][1], &terrain))
				continue; // edge of the converted world
			int ledge_tag;
			float ledge_y = 0.0f;
			float ground = fmaxf(terrain, object_ground_ex(cand[c][0], cand[c][1], pl->feet + PLAYER_STEP_HEIGHT,
			                                               &ledge_tag, &ledge_y));
			if (ground > pl->feet + PLAYER_STEP_HEIGHT) {
				blockers.ledge_tag = -3; // terrain itself too steep/high
				blockers.ledge_y = ground;
				continue;
			}
			// The body must fit above the new ground (no stepping into a rock's belly or
			// under a low overhang). Undersides within step height don't count: stair
			// treads are often stacked slabs whose bottoms sit just above the tread below,
			// and those stopped the player dead at the foot of a flight
			if (object_ceiling(cand[c][0], cand[c][1], fmaxf(ground, pl->feet) + PLAYER_STEP_HEIGHT) <
			    ground + PLAYER_HEIGHT)
				continue;
			if (ledge_tag != -1) { // an object surface just above step height here (informational)
				blockers.ledge_tag = ledge_tag;
				blockers.ledge_y = ledge_y;
			}
			pl->x = cand[c][0], pl->z = cand[c][1];
			break;
		}
	}

	// Vertical: stick to the ground when close (walking down slopes/stairs), else fall
	float terrain = 0.0f;
	terrain_height_at(pl->x, pl->z, &terrain);
	float ground = fmaxf(terrain, object_ground(pl->x, pl->z, pl->feet + PLAYER_STEP_HEIGHT));
	// Water deeper than SWIM_DEPTH here: float at that depth (rising back up after a dive
	// in from above), until the bed comes up under the feet again
	float float_feet = pl->in_water ? pl->water - SWIM_DEPTH : -INFINITY;
	pl->swimming = 0;
	if (float_feet > ground && pl->feet <= float_feet + 0.05f && pl->vy <= 0.0f) {
		// Diving: down to the bed, back up to the surface; left alone, you float up
		float next = pl->swim_dy != 0.0f ? pl->feet + pl->swim_dy : pl->feet + SWIM_RISE_MS * dt;
		pl->feet = fmaxf(ground, fminf(float_feet, next));
		pl->vy = 0.0f;
		pl->grounded = 0;
		pl->swimming = 1;
	} else
	// (Only from the ground: in the air, snapping down from step height cut the last
	// half metre of every jump's fall)
	if (pl->vy <= 0.0f && pl->grounded && pl->feet - ground <= PLAYER_STEP_HEIGHT) {
		pl->feet = ground;
		pl->vy = 0.0f;
		pl->grounded = 1;
	} else {
		pl->grounded = 0;
		pl->vy -= GRAVITY * dt;
		float next = pl->feet + pl->vy * dt;
		if (pl->vy > 0.0f) {
			// Rising: the head stops at the first underside above it
			float ceiling = object_ceiling(pl->x, pl->z, pl->feet + PLAYER_HEIGHT - 0.05f);
			if (next + PLAYER_HEIGHT > ceiling) {
				next = fmaxf(pl->feet, ceiling - PLAYER_HEIGHT);
				pl->vy = 0.0f;
			}
		}
		pl->feet = next;
		if (pl->feet <= ground) {
			pl->feet = ground;
			pl->vy = 0.0f;
			pl->grounded = 1;
		}
	}
	// Never let a bad value stick: an unresolvable move is simply discarded
	if (!player_finite(pl)) {
		*pl = before;
		pl->vy = 0.0f;
		return 0;
	}
	return 1;
}
