#ifndef COLLISION_H
#define COLLISION_H

// Player-vs-world collision against Skyrim's own (Havok) collision meshes of the
// static objects around the player. One region (the player's 3x3 cells) is rebuilt
// when the player changes cell; the player is a vertical cylinder (tested as spheres)
// pushed out of walls, standing on the highest walkable surface within step height.

#define PLAYER_RADIUS 0.35f
#define PLAYER_STEP_HEIGHT 0.6f

// Replaces the collision region. tris: count * 9 floats (three xyz vertices), world
// space, copied. Triangles outside [min, max] are ignored.
// tags: optional per-triangle id (e.g. model index) reported by collision_last_blockers().
void collision_set_region(float min_x, float min_z, float max_x, float max_z, const float *tris,
                          const int *tags, int count);
void collision_clear(void);
// Moving collision (doors): world-space triangles (count * 9 floats), copied, tested along
// with the region's until replaced (count 0: none)
int collision_dynamic_count(void); // (doors' moving triangles now)
void collision_set_dynamic(const float *tris, int count);

// The same in two steps, for building on the streaming thread: collision_build is pure
// (any thread, returns NULL on allocation failure); collision_install swaps the result
// in (render thread) and takes ownership; collision_free drops an unused one.
typedef struct CollisionRegion CollisionRegion;
CollisionRegion *collision_build(float min_x, float min_z, float max_x, float max_z, const float *tris,
                                 const int *tags, int count);
void collision_install(CollisionRegion *r);
void collision_free(CollisionRegion *r);

typedef struct {
	float x, feet, z; // feet = height of the soles
	float vy;         // vertical speed (jumping/falling)
	int grounded;     // standing on terrain or a walkable surface this frame
	int in_water;     // set before collision_move: there is water here, its surface at water
	float water;
	int swimming;     // in water deeper than SWIM_DEPTH this frame: at the surface or under it
	float swim_dy;    // set before collision_move: swimming up (+) or down (-) this frame, metres;
	                  // 0 drifts back up to the surface
} Player;

#define SWIM_DEPTH 1.3f // feet this far under the surface while swimming (head above it)

#define PLAYER_JUMP_SPEED 6.0f // ~0.9 m jump (Skyrim's is about 1 m)
#define PLAYER_GRAVITY 20.0f   // m/s^2 (a jump is in the air 2 * speed / gravity)

// Starts a jump if the player is standing on something.
void collision_jump(Player *p);

// Moves the player by (dx, dz) this frame, resolving walls, steps, slopes and falls.
// Returns 0 (and doesn't move) if the destination has no loaded terrain.
int collision_move(Player *p, float dx, float dz, float dt);

// How far along the segment a -> b (0..1) the first collision triangle is, or 1 if
// none is hit (keeps the third-person camera out of walls).
float collision_ray(const float a[3], const float b[3]);
// The same, the ground (terrain) counting too: spells aimed at the ground, line of sight
float collision_ray_ground(const float a[3], const float b[3]);

// Debugging: what affected the last collision_move. wall_tag = tag of the last
// triangle that pushed the player sideways (-1 if none); ledge_tag/ledge_y = the last
// surface that was too high to step onto (-1 if none).
typedef struct {
	int wall_tag;
	float wall_y; // height of that contact
	int ledge_tag;
	float ledge_y;
} CollisionBlockers;
CollisionBlockers collision_last_blockers(void);

#endif
