#ifndef FRAMETIME_H
#define FRAMETIME_H

// This frame's game time in seconds (main.c, by the frame rate setting: 1/60, 1/30, or
// smoothed between 1/60 and 1/22) - for timers kept outside the game's dt
float frame_seconds(void);

#endif
