#ifndef LOOT_H
#define LOOT_H

#include <stdint.h>

// Containers and loose items (inside buildings, and outdoors around the player) (the .skint sections after the swing doors,
// see converter/interiors.py): chests, barrels, sacks, urns... open with CIRCLE (their lids
// and doors swing open while you look through them); loose things are taken. Owned ones
// are stolen (no crime yet); locked ones are picked (lockpick.c).

void loot_init(const char *pack_dir);
// An interior's containers and loose items (NULL: none, e.g. outdoors)
// (returns the position after the section; sized: SKIT 5, containers' aim radius)
const uint8_t *loot_load(const uint8_t *p, const uint8_t *end, int sized);
// Outdoors, each frame: the player's cell (the loot of the 3x3 cells around it is loaded
// when it changes)
void loot_exterior(int cell_x, int cell_y);
void loot_world_changed(void); // entering or leaving a walled city
// What's under the crosshair (look ray from eye, dir unit) within reach (-1: nothing): its
// distance along the ray and its prompt
int loot_aim(const float eye[3], const float dir[3], float *along, const char **label);
// CIRCLE on it: a loose item goes into the inventory (1); a container opens: its handle
// (items.h) and name for the container menu (2); locked (0, a notice)
int loot_activate(int target, int *handle, const char **name);
// (testing) The nearest loose item by its name: its target for loot_activate (at: where), or -1
int loot_find(const char *name, const float near[3], float at[3]);
// A lock just picked (lockpick.c's target): it stays open; the container opens (as
// loot_activate)
int loot_unlocked(int target, int *handle, const char **name);
// The container menu closed: its lid closes
void loot_close(void);
// Something was dropped (items_drop): it's shown where it lies
void loot_refresh_dropped(void);
// A model moving this frame (an arrow in flight; objects model index): placed by m (row-major
// rotation) at t, lit with tint; drawn by the next loot_draw. 0 if it couldn't be
int loot_draw_moving(int model, const float m[9], const float t[3], const uint8_t tint[3]);
// Draws what's queued with loot_draw_moving now (the first-person pass: the arrow on the string)
void loot_draw_moving_now(void);
// A model's bounds (model space); 0 if it couldn't be loaded
int loot_model_extent(int model, float lo[3], float hi[3]);
// After objects_draw
void loot_draw(float cam_x, float cam_z);

// Someone's things taken loose since last asked: their worth (stolen: crime.c, if seen)
int loot_take_theft(void);
int loot_owned(int target); // (a container or loose thing aimed at: someone's)

#endif
