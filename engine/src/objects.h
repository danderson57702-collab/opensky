#ifndef OBJECTS_H
#define OBJECTS_H

#include <stdint.h>

// Streams the placed objects of converted cells (pack/objects/<world>_X_Y.skobj).
// Each cell's objects are transformed and lit once, then merged into static
// per-texture batches. Objects are drawn out to a distance set by their size:
// small/medium to OBJECTS_DETAIL_RADIUS cells, large (buildings, big rocks) to
// OBJECTS_LARGE_RADIUS, huge (city walls, rock formations) to OBJECTS_HUGE_RADIUS.

#define OBJECTS_DETAIL_RADIUS 1 // 3x3 cells
#define OBJECTS_LARGE_RADIUS 2  // 5x5 cells
#define OBJECTS_HUGE_RADIUS 4   // 9x9 cells (at most: the performance setting's)
// How far the biggest objects are drawn (rings of cells, OBJECTS_LARGE_RADIUS..OBJECTS_HUGE_RADIUS)
void objects_set_far_radius(int rings);
// Where small things (clutter, plants, fences) and people dissolve out (metres; at most 45..58)
void objects_set_near_fade(float start_m, float end_m);

void objects_init(const char *pack_dir, const char *world);
// Current time in seconds (drives fade-ins of newly built / re-detailed objects).
void objects_set_time(float time_s);
void objects_update(float x, float z);
// Opaque and alpha-tested geometry; call after terrain (uses its current matrices).
// Objects dissolve in/out by distance and between detail levels (dithered).
void objects_draw(float cam_x, float cam_y, float cam_z);
// Size of the render target being drawn to (the dither pattern follows its pixels).
void objects_set_render_size(int w, int h);
// Translucent water planes; call after everything opaque.
void objects_draw_water(float cam_x, float cam_z);
// The highest water surface (rivers, ponds) of the loaded cells at (x, z); 0 if none
int objects_water_at(float x, float z, float *surface);
// ... the water there that someone at height y is in or under: not water above a floor or
// ceiling between (a pool upstairs)
int objects_water_at_y(float x, float y, float z, float *surface);
// Fires and glows (animated additive sprites, faded out by the fog); call last.
// time_s drives the flipbook and flicker.
// Mesh name of a model index (for diagnostics), or "?".
const char *objects_model_name(int index);

void objects_draw_fx(float cam_x, float cam_z, float yaw_deg, float time_s);
// The held torch's flame at pos this frame (NULL: none)
void objects_fx_torch(const float pos[3]);

// Spell sprites (additive, depth-tested, not written; Skyrim's effect textures, see
// converter write_spell_textures)
enum {
	SPRITE_FIRE,   // the torch flame flipbook (8 frames)
	SPRITE_FLAME,  // Flames' fireballs (32 frames)
	SPRITE_GLOW,   // a warm soft glow
	SPRITE_WHITE,  // a white soft glow
	SPRITE_RAYS,   // a starburst
	SPRITE_SPARKS, // 16 sparks
	SPRITE_RING,   // a ring (lies level: flat)
	SPRITE_RUNE,   // Fire Rune's sigil (lies level)
	SPRITE_BOLT,   // a lightning strip (2 frames; stretched along a bolt's segment)
	SPRITE_ARCS,   // crackling arcs (2 x 2)
	SPRITE_KINDS
};
typedef struct {
	float pos[3];
	float size, angle;   // width (m); turned in the view plane (radians)
	uint8_t kind, frame; // SPRITE_*, flipbook frame
	uint8_t flat;        // lying level (rings) instead of facing the camera
	uint8_t rgba[4];
	float stretch;       // height x this (rain's streaks; 0: square)
} ObjectsSprite;
// Draws sprites facing a camera at yaw / pitch (degrees)
void objects_draw_sprites(const ObjectsSprite *s, int n, float yaw_deg, float pitch_deg);

// The vertex format objects are drawn with: position and UV quantized to 16 bits
// (normalized) around a per-draw offset and scale, baked light in the colour.
typedef struct {
	int16_t x, y, z, pad;
	int16_t u, v;
	uint8_t r, g, b, a;
} ObjectVertex;

// Geometry rebuilt every frame (animated characters), drawn like objects: same shader,
// fog and distance dissolve (the full-detail band). Vertex and index data stay in RAM.
typedef struct {
	const ObjectVertex *vertices;
	int vertex_count;
	const uint16_t *indices;
	int index_count;
	unsigned int tex;
	float pos[4]; // world = position * pos[3] + pos[0..2]
	float uv[3];  // uv = texcoord * uv[2] + uv[0..1]
	int alpha;    // alpha-tested and two-sided (hair, lashes); ALPHA_GLOW: added as light; 3 and
	              // up: cut at its own alpha/255 (a mammoth's fur, 10)
	float fade;   // 0: the usual distance dissolve; < 0 none (a dragon seen from afar); 0..1: that
	              // much of it dithered away (a dead dragon's skin burning off)
	// Posed on the GPU (objects_skinning): its own vertices (32 bytes each, actors.c's SrcVertex)
	// instead of vertices, the pose (64 bones x 3 rows of 4 floats), pos[0..2] its origin; its
	// colour times tint_mul plus tint_add (0..1: a poisoned or enchanted weapon's)
	const void *skin_src;
	unsigned int skin_vbo, skin_ibo; // (its vertices and indices in GPU buffers: no copy each frame)
	const float *bones;
	float tint_mul[3], tint_add[3];
	uint8_t is_hair;
	// Turned on the GPU about pos[0..2] (row-major 3x3; NULL: none): a moving prop (the opening's
	// carts) keeps its vertices as they are instead of having them all turned again each frame
	const float *rot;
} ObjectsDynamic;

#define ALPHA_GLOW 2 // (a bound weapon's ghostly light)
#define ALPHA_CUT(a) ((a) == 1 || (a) > ALPHA_GLOW)
// Call after objects_draw (same frame and matrices).
void objects_draw_dynamic(const ObjectsDynamic *items, int count);
// Whether characters can be posed on the GPU (its programs built)
int objects_skinning(void);
// Per frame since last asked: the world's and the characters' draws, triangles, bytes copied
int objects_stats(char *out, int size);
unsigned objects_frame_tris(void); // the world's triangles drawn last frame
// Skyrim's ambient fish (salmon effect models): the converter's bake leaves them out and
// fish.c swims them. objects_fish_near: those placed within radius of (x, z), up to max.
typedef struct {
	int model;
	float m[9], t[3]; // placement (row-major rotation x scale, translation)
	unsigned id;      // stable per placement (for its own swim pattern)
} ObjectsFish;
int objects_is_fish_model(const char *name);
int objects_fish_near(float x, float z, float radius, ObjectsFish *out, int max);
// A pack texture by name (loaded on first use; render thread). 0 if missing.
unsigned int objects_texture(const char *name);
// Frees the textures only the world's objects used that nothing shown uses now (outside_too:
// the outside's objects first, going indoors); waits for the streaming. How many were freed
int objects_release(int outside_too);
// Textures load no larger than this from now on (the performance setting; 0: full size)
void objects_set_texture_cap(int max_dim);
// Nothing farther than this (metres) is drawn: a city's fog hides it (0: no limit)
void objects_set_view_limit(float m);

// Interiors (interior.c parses the file): while one is loaded, objects_draw and
// objects_draw_water draw it instead of the exterior cells (which stay loaded) and its
// collision replaces the exterior's.
void objects_interior_begin(float draw_radius_m);
void objects_interior_set_radius(float draw_radius_m); // (inside: how far is drawn)
// One tile: its bounds and a .skbat file's bytes (uploaded now). Returns 0 on bad data.
int objects_interior_tile(const float min[3], const float max[3], int room, const void *skbat, long size);
// Skyrim's room bounds (min, max each), the portals between them (28 bytes: min, max, room a, b)
// and the rooms open to each other (pairs): what's out of sight isn't drawn
// Whether a point indoors is in a room in sight this frame (or in no room): who's drawn there
int objects_point_in_sight(float x, float y, float z);
void objects_interior_rooms(const float *boxes, int rooms, const uint8_t *portal_data, int nportals,
                            const int16_t *links, int nlinks);
// Collision from the interior's placements (.skobj entries, 52 bytes each): kept, and built
// for the area around the player by objects_interior_update.
void objects_interior_collision(const void *entries, int count, const float min[3], const float max[3]);
// Inside, before each step: the collision around (x, z) (at once on arriving; walking on,
// streamed ahead)
void objects_interior_update(float x, float z);
// Inside: whether a fire (a hearth, a brazier) is within radius of (x, y, z) (people don't
// stand in it)
int objects_fire_near(float x, float y, float z, float radius);
// Back outside: frees the interior; the exterior collision is rebuilt.
void objects_interior_end(void);
// Entering or leaving a walled city (worlds_set): the cells whose files change are reloaded.
void objects_world_changed(void);
// World switches (converter/esm.py SWITCHES: 1 Helgen as Alduin's attack leaves it, 2 its
// ruins after): what's shown with them on or off; all of them (saving), restored
void objects_set_switch(int number, int on);
uint32_t objects_switches(void);
void objects_set_switches(uint32_t bits);

#endif
