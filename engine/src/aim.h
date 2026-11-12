#ifndef AIM_H
#define AIM_H

// What the crosshair is on: a target (pos, a sphere of radius r) counts if it's within reach
// along the look ray from the eye (dir: unit) and the ray passes within r of it; of those,
// the nearest along the ray is what CIRCLE uses (as Skyrim's crosshair picks the first thing
// it meets).
static inline int aim_hits(const float eye[3], const float dir[3], const float pos[3], float radius, float reach,
                           float *along) {
	const float d[3] = {pos[0] - eye[0], pos[1] - eye[1], pos[2] - eye[2]};
	const float t = d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2];
	if (t < 0.0f || t > reach)
		return 0;
	const float p[3] = {d[0] - dir[0] * t, d[1] - dir[1] * t, d[2] - dir[2] * t};
	if (p[0] * p[0] + p[1] * p[1] + p[2] * p[2] > radius * radius)
		return 0;
	*along = t;
	return 1;
}

#endif
