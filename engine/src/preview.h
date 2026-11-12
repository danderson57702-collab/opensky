#ifndef PREVIEW_H
#define PREVIEW_H

// The inventory's 3D view of the selected item (its own model, Skyrim's), turning slowly in
// the upper right, lit from the front; drawn over the game, under the menu's panels.

void preview_init(const char *pack_dir);
void preview_set(int model); // objects model index; -1: nothing
// Where: the inventory's upper right (0), or beside a container's list (1: smaller)
void preview_place(int beside_list);
void preview_draw(float time_s);
// Looking closely at it: in the middle, bigger, turned by hand (radians a call)
void preview_inspect(int on);
int preview_inspecting(void);
void preview_turn(float yaw, float tilt);

#endif
