#ifndef STONES_H
#define STONES_H

// Skyrim's standing stones (pack/stones.txt: converter - the region's Doomstone activators): the
// Guardian Stones by Riverwood. One blessing at a time: the Warrior, the Thief or the Mage - its
// six skills improve a fifth faster (skills_set_stone).

void stones_init(const char *pack_dir);
// The stone you're standing at (within reach): its index and prompt, or -1
int stones_near(const float feet[3], const char **label);
// Takes its blessing: 1 and what to tell the player; 0 if it's one whose blessing isn't here yet
int stones_take(int stone, const char **notice);
int stones_active(void);        // (saving: the stone taken + 1, 0 none)
void stones_set_active(int v);  // (a game loaded)
// Skyrim's word walls (pack/walls.txt): the one you've come up to (place: -1 outdoors, else the
// interior) with a word still to teach you - its shout's next (word); -1 none. Each teaches once
#include <stdint.h>
int walls_near(const float feet[3], int place, const char **word);
void walls_spend(int wall);
int walls_glowing(const float feet[3], int place, float out[3]); // (a wall near with a word for you: where; -1)
int walls_pos(int wall, float out[3]);
void walls_save(uint32_t out[2]);
void walls_restore(const uint32_t in[2]);

#endif
