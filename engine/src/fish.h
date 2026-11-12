#ifndef FISH_H
#define FISH_H

// Skyrim's ambient salmon, swimming (fish.c). fish_draw after objects_draw, each frame
// outdoors; it finds the placed fish near the camera itself (objects_fish_near).
void fish_init(const char *pack_dir);
void fish_draw(float cam_x, float cam_z, float time_s);

#endif
