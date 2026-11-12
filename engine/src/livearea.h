#ifndef LIVEAREA_H
#define LIVEAREA_H

// The LiveArea's background from the game's own view (livearea.c)
int livearea_save(void);     // the frame as drawn now (call before the menus and HUD go over it): 1 when written
void livearea_restore(void); // at start: the kept picture back into the app after a reinstall

#endif
