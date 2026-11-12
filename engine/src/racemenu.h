#ifndef RACEMENU_H
#define RACEMENU_H
#include "chargen.h"
// Skyrim's race menu (a new character, the opening's INFO 0003F88B): its tabs along the top -
// Race, Body, Head, Face, Eyes, Brow, Mouth, Hair - L / R between them; down the left the tab's
// sliders (Up / Down to one, Left / Right moves it); the race list and its card on the Race tab;
// Start or Triangle when done. Built on the race's RACE data (chargen.h)
enum { RACEMENU_NONE, RACEMENU_DONE };

// The menu opened on a race, a sex and a look (look NULL or not version 1: the race's first preset)
void racemenu_open(int race, int female, const ChargenLook *look);
// The chosen character on the player: a new race or sex reloads the body (main.c's) and puts the look on
typedef void (*RacemenuSetBody)(int race, int female, ChargenLook *look);
// Each frame (pressed: new presses; held: the buttons down): RACEMENU_DONE when Done is pressed
int racemenu_update(unsigned pressed, unsigned held, float dt, RacemenuSetBody set_body);
void racemenu_close(void);
int racemenu_race(void);
int racemenu_female(void);
const ChargenLook *racemenu_look(void);
int racemenu_portrait(void);      // (the camera: close - the tabs after Race)
int racemenu_close_on_face(void); // (... and closer, on the face - the tabs after Body)
#endif
