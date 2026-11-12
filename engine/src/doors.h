#ifndef DOORS_H
#define DOORS_H

#include <stdint.h>

// Doors that open in place (Skyrim's animated doors: wooden doors, gates, cage doors; inside,
// and outdoors: Valtheim Towers' way in). The converter splits each into its frame (drawn and collided with the rest
// of the interior) and moving parts with their swing from closed to open (Skyrim's "Open"
// sequence); here the parts swing, block while closed, open for you (CIRCLE) and for NPCs
// walking through.

void doors_init(const char *pack_dir);
// An interior's swing doors (its .skint section, see converter/interiors.py); NULL: none.
// Returns the position after the section (NULL if it was cut short)
const uint8_t *doors_load(const uint8_t *p, const uint8_t *end);
// More of them, kept with those there (outdoors: each nearby cell's, from its .skloot)
const uint8_t *doors_append(const uint8_t *p, const uint8_t *end);
// A container's moving lid / doors (the door-like part of its record, after its header):
// index (-1: nothing moves), the position after it
const uint8_t *doors_add_container(const uint8_t *p, const uint8_t *end, int *index);
// A scripted activator's moving part (activators.c moves it; it collides): index, the
// position after it
const uint8_t *doors_add_activator(const uint8_t *p, const uint8_t *end, int *index);
// A trap's moving parts (traps.c poses them; they don't collide): index, the position after
const uint8_t *doors_add_trap(const uint8_t *p, const uint8_t *end, int *index);
// A trap's pose: its parts turned angle x u about their axes, slid slide x u (any u)
void doors_set_pose(int door, float u);
// Where a trap's striking part is now (its farthest part's middle, world) and its size
int doors_part_blow(int door, float out[3], float *radius);
// Opens / closes one (a container's lid, a gate); at once (a gate already opened)
void doors_set_open(int door, int open);
void doors_set_open_now(int door, int open);
// A puzzle pillar's face at the start: this many steps already turned
void doors_set_turns(int door, float turns);
// A puzzle pillar's next step (one more turn); a lever's pull (down, then back up)
void doors_step(int door);
void doors_pull(int door);
int doors_moving(int door);
// The door under the crosshair (look ray from eye, dir unit) within reach, or -1; its
// distance along the ray; label: "Open door" / "Close door"
int doors_aim(const float eye[3], const float dir[3], float *along, const char **label);
int doors_position(int door, float out[3]); // (a swing door's place: its lock's)
int doors_is_open(int door);
void doors_toggle(int door);
void doors_remove(int door); // (taken away: a claw door opened - not drawn, no collision)
// Each frame: they swing; NPCs reaching a closed door open it (and it closes behind them)
void doors_update(float dt);
void doors_keep_npc_open(int keep); // (1: doors opened by people stay open - the opening's gate)
// After objects_draw
void doors_draw(float cam_x, float cam_z);
// Opens the doors and gates near a point (a script's: Helgen Keep's, Skyrim's levers and keys);
// how many
int doors_open_near(const float pos[3], float radius);
void doors_log_near(const float pos[3], float radius); // (testing: each door about a point, logged)
// A model a script moves (objects model index; lit by tint): its prop number, -1; placed at
// pos turned heading (radians, as the actors') and tilted pitch; gone
int doors_prop(int model, const uint8_t tint[3]);
void doors_prop_place(int prop, const float pos[3], float heading, float pitch);
void doors_prop_remove(int prop);

#endif
