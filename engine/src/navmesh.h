#ifndef NAVMESH_H
#define NAVMESH_H

#include <stdint.h>

// Skyrim's navmeshes (pack/navmesh/<world>_X_Y.sknav, see converter/navmesh.py): the
// walkable surface NPCs path-find on. Cells within NAV_RADIUS of the player are kept
// loaded (streamed). Positions are engine space (metres, Y up).

#define NAV_RADIUS 2 // 5x5 cells: everything an NPC is drawn in (objects detail ring + 1)

typedef struct {
	int cx, cy; // cell
	int tri;    // triangle in that cell's navmesh, -1: none
} NavRef;

void navmesh_init(const char *pack_dir, const char *world);
void navmesh_update(float x, float z);
// Entering or leaving a walled city: the cells whose files change are reloaded
void navmesh_world_changed(void);

// Inside a building: every query uses that interior's navmesh (path: a .sknav under the
// pack, loaded now); NULL goes back to the streamed exterior cells.
void navmesh_set_interior(const char *path);

// Whether the navmesh of the cell containing (x, z) is loaded (or known to be absent).
int navmesh_ready(float x, float z);

// Finds the triangle under (x, z) nearest in height to y (within a few metres).
// Returns 1 and fills ref/height if found.
int navmesh_locate(float x, float y, float z, NavRef *ref, float *height);

// The walkable point nearest (x, y, z) within max_m (for spots off the navmesh: a bed, a
// table): 1 and its triangle and position, or 0
int navmesh_nearest(float x, float y, float z, float max_m, NavRef *ref, float out[3]);
// The same on one floor: only surfaces within max_dy of y (a loft above, a cellar below
// don't count)
int navmesh_locate_level(float x, float y, float z, float max_dy, NavRef *ref, float *height);
int navmesh_nearest_level(float x, float y, float z, float max_m, float max_dy, NavRef *ref, float out[3]);
// Follows a ref to the triangle under (x, z) through neighbouring triangles (cheap when
// the point moved a little). Returns 1 and the surface height, 0 if it fell off.
int navmesh_track(NavRef *ref, float x, float z, float *height);

// Path from (from, fx..fz) to (to, tx..tz) as corner points (x, y, z) ending at the goal,
// at most max_points. Returns the point count, or 0 if there is no path through the
// loaded cells (within a search budget).
int navmesh_indoors(void); // (an interior's navmesh, not the world's)
int navmesh_path(NavRef from, const float f[3], NavRef to, const float t[3], float *points, int max_points);

// Inside: the interior's navmesh (its walkable floor, for the local map): vertices (x, y, z),
// each triangle's three vertex indices at tri_verts + t * tri_stride; returns the triangles (0: none)
int navmesh_interior_mesh(const float **verts, int *vert_count, const uint16_t **tri_verts, int *tri_stride);

#endif
