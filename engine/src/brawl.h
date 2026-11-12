#ifndef BRAWL_H
#define BRAWL_H

// Skyrim's brawls (Favor017, DGIntimidateQuest): fists only - a weapon drawn or a spell cast
// and it's off - till one of you is beaten down (no one dies: it stops short of that).

enum { BRAWL_ON, BRAWL_WON, BRAWL_LOST, BRAWL_BROKEN, BRAWL_NONE };

void brawl_start(int actor, int their_level);
// A training bout (Vilkas in Jorrvaskr's yard, Take Up Arms): weapons allowed - a spell cast breaks it off
void brawl_start_spar(int actor, int their_level);
int brawl_spar(void); // (one going on)
int brawl_active(void);
int brawl_who(void); // (actors.h's index, -1)
// The player's punch landing (eye, unit look, reach, damage): 1 if it struck them
int brawl_strike(const float eye[3], const float dir[3], float reach, float damage);
// Each frame: their punches at you (health: yours, its maximum); armed: a weapon or spell in
// hand. BRAWL_ON while it goes on, then how it ended (once)
int brawl_update(float dt, const float feet[3], float *health, float max_health, int armed);

#endif
