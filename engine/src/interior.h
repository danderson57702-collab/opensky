#ifndef INTERIOR_H
#define INTERIOR_H

// Buildings and dungeons (pack/interiors, see converter/interiors.py): their doors, and
// switching the world between Tamriel and one interior. Inside, the interior replaces the
// exterior for drawing, collision, navmeshes and NPCs; the exterior stays loaded.

void interior_init(const char *pack_dir);
// Outdoors: -1 in Tamriel, -2 - n in walled city n (worlds.h's world n + 1); else the
// interior's index (>= 0)
int interior_current(void);
// Where the door interior_door_aim gave is (its place: a lock's); 0 if none
int interior_gate_door(const char *title, const char **label); // (Tamriel's door into a walled city: its index, -1)
int interior_door_pos(int i, float out[3]);
// In a walled city: its gates out to Tamriel (where you'd walk to them); 0 elsewhere
int interior_city_gates(float pos[][3], int max);
// The gate into a walled city by its name ("Whiterun"): out x, z; 0 if there's none
int interior_gate_of(const char *title, float out[2]);
// How far inside is drawn, at most (Fidelity's; the fog closes in to match)
void interior_set_draw_cap(float m);
// Inside: its fog colour (clear colour) and lowest point (a safety floor), else 0
int interior_fog(float rgb[3]);
float interior_floor(void);

// The door under the crosshair (look ray from eye, dir unit) within reach, or -1: its
// distance along the ray and where it leads
int interior_door_aim(const float eye[3], const float dir[3], float *along, const char **label);
// Goes through a door (loads the destination now); fills the arrival position/yaw.
// Returns 0 if the destination can't be loaded (nothing changes).
int interior_use_door(int door, float pos[3], float *yaw_deg);
// A walk-through door (a cave's mouth) you've walked into: its index, -1 none; disarm: just arrived
int interior_walk_door(const float feet[3]);
// A walk-through way out (a cave's mouth) this near, its label (INFO 00017E00); -1 none
int interior_walk_near(const float feet[3], float within, const char **label);
void interior_walk_disarm(void);
// Loading a game: into interior `index` (< 0: outdoors in that world) directly, with its way in
// (interior_entry). Returns 0 if it can't be loaded.
int interior_go(int index, const float entry[3], float entry_yaw_deg);
// Where the player came in (to put them back if they fall out of the world)
void interior_entry(float pos[3], float *yaw_deg);

// Where you are, for a save's description (INFO 00019F96, "Riverwood"): what the door you
// came through called it (a loaded game: what its save said)
const char *interior_place(void);
// An interior's place name ("Dustman's Cairn"; its cells share it), "" if unknown
const char *interior_title(int index);
// An interior's index by its editor id ("bleakfallsbarrow02"), -1
int interior_find(const char *editor_id);
// Whether every hostile in the place's interiors (by its name) has been killed: Skyrim's Cleared
int interior_location_cleared(const char *title);
const char *interior_id(void); // the interior's editor id ("whiterunbanneredmare"), "" outdoors
void interior_set_place(const char *name);

// Inside: its doors (where, where they lead) for the local map; how many
int interior_doors(float pos[][3], const char **labels, int max);

#endif
