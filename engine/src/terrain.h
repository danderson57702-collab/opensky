#ifndef TERRAIN_H
#define TERRAIN_H

// Streams converted Skyrim exterior cells (pack/terrain/<world>_X_Y.skcell + .sktex)
// around the player: full detail within FULL_RADIUS cells, every 4th vertex out to
// LOD_RADIUS, every 8th vertex with a 32 px color map out to FAR_RADIUS.
// World space is meters, x east, y up, z south.

#define CELL_SIZE_M (4096.0f / 70.0f)
#define TERRAIN_FULL_RADIUS 1 // 3x3 full-detail cells
#define TERRAIN_LOD_RADIUS 4  // 9x9 cells at medium detail
#define TERRAIN_FAR_RADIUS 8  // 17x17 cells drawn in total (~470 m), at most
// How far the land is drawn (the performance setting): rings of cells, 3..TERRAIN_FAR_RADIUS
void terrain_set_far_radius(int rings);
int terrain_far_radius(void);

void terrain_init(const char *pack_dir, const char *world);
// Loads/unloads cells so everything within FAR_RADIUS of (x, z) is resident.
// Cheap when called every frame: it only does work when the player changes cell.
void terrain_update(float x, float z);
// The world changed: its land (worlds_terrain_prefix) streams in instead
void terrain_world_changed(void);
void terrain_draw(float cam_x, float cam_z);
void terrain_draw_water(void);
// Ground height at (x, z); returns 0 if that cell is not loaded.
int terrain_height_at(float x, float z, float *out);
// The cell's water level (lakes, the sea) at (x, z); 0 if it has none
int terrain_water_at(float x, float z, float *surface);
// Frees every loaded cell (going indoors: its video memory back); they stream in again outside
void terrain_release(void);
// Inside a building there is no terrain: heights are "far below" (objects carry the
// player) and every cell counts as in view. Loaded cells stay resident.
void terrain_set_active(int active);

// Horizontal view culling: set once per frame, then test cells against it.
void terrain_set_view(float cam_x, float cam_z, float yaw_deg, float half_fov_x_deg);
int terrain_cell_in_view(int cx, int cy);
// The same test for any ground rectangle (x0..x1, z0..z1, metres)
int terrain_rect_in_view(float x0, float z0, float x1, float z1);

// Cell grid <-> world helpers
int terrain_cell_x(float x);
int terrain_cell_y(float z);

#endif
