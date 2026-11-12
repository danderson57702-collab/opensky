#ifndef NAME_H
#define NAME_H

// The player's name: asked with the Vita's own keyboard (its system dialog) when Hadvar asks
// INFO 0003F88B; kept with the save.

// Opens the keyboard (title over it, the current name in it); 0 if it can't
int name_ask(const char *title);
// Each frame while it's open: 1 still asking (the frame must be shown with the system's dialog
// drawn over it: vglSwapBuffers(GL_TRUE)); 0 done (the name taken, or kept if cancelled)
int name_asking(void);
const char *name_get(void);
void name_set(const char *name); // (loading a game; "" or NULL: "Prisoner")

#endif
