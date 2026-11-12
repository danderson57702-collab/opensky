#ifndef ANIM_H
#define ANIM_H

#include <stdint.h>

// Skyrim's character animations (pack/anims/*.skanim, see converter/animations.py): per
// clip and frame, each animated bone's transform relative to its parent (rotation
// quaternion + translation, engine space). Sampling interpolates frames and blending
// mixes poses joint by joint (limbs keep their length); anim_deltas then rebuilds the
// skeleton and gives each bone's skinning transform (its change from the bind pose).

#define ANIM_MAX_BONES 64
#define ANIM_MAX_CLIPS 192 // (the characters' set: 137 with the riders' - at 128 the left hand's swings, last, were dropped)

typedef struct {
	float q[4]; // x, y, z, w
	float t[3];
} BonePose;

typedef struct {
	char name[17];
	float duration;
	int frames;
	float vx, vz;     // velocity it covers (m/s, model space; forward is -Z)
	float speed;      // |velocity|
	uint64_t moved;   // bones whose translation is animated (bit per bone)
	int stride;       // bytes per frame
	const uint8_t *data; // per frame: bones x i16 rotation, then the moved bones' f32 translations
} Clip;

typedef struct {
	float r[9]; // row-major rotation
	float t[3];
} BoneDelta; // model space: a bind-pose point p goes to r p + t

typedef struct {
	int bones, count;
	int parent[ANIM_MAX_BONES];
	BoneDelta rest_inverse[ANIM_MAX_BONES]; // the bind pose's world transforms, inverted
	float rest_t[ANIM_MAX_BONES][3];        // the bind pose's local translations
	Clip clips[ANIM_MAX_CLIPS];
	void *file;
} ClipSet;

int anim_load(const char *path, ClipSet *set);
const Clip *anim_clip(const ClipSet *set, const char *name);
// The pose at time t (seconds, wrapping) into out[set->bones]
void anim_sample(const ClipSet *set, const Clip *clip, float t, BonePose *out);
// a = a blended toward b by w (0..1)
void anim_blend(BonePose *a, const BonePose *b, int bones, float w);
// Skinning transforms for a pose (local transforms) into out[set->bones]
void anim_deltas(const ClipSet *set, const BonePose *pose, BoneDelta *out);

#endif
