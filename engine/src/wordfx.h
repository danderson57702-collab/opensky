#ifndef WORDFX_H
#define WORDFX_H

// A Word of Power shouted onto the floor (the Greybeards' trials, Paarthurnax's Yol): its letters
// in the dragon alphabet, glowing blue, as Skyrim's fxgreybeardshout<word>.nif lays them
// (pack/objects/words.txt, converter/greybeard_words.py) - growing in, then fading.

void wordfx_init(const char *pack_dir);
// The word ("ro", "wuld"...) on the floor at, turned to yaw (radians: its reading side toward you)
void wordfx_show(const char *word, const float at[3], float yaw, float seconds);
void wordfx_update(float dt);
void wordfx_draw(void); // (in the world's view, after the world: glowing over the floor)

#endif
