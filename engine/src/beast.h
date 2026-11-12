#ifndef BEAST_H
#define BEAST_H

#include "vitals.h"

// Beast Form (Skyrim's werewolf: the Companions' gift, The Silver Hand's blood ritual). You as
// the werewolf: its body drawn for yours and seen from behind, its claws your weapons (Skyrim's
// werewolf race - 20 unarmed - and more by your level: PlayerWerewolfLvl..Ability), AbWerewolf's
// health and stamina, no health coming back by itself; faster on your feet. Bodies fed on
// (a kill's): health back and more time (WerewolfFeed). No talking, no spells, shouts or things
// picked up in it. 150 s, then back (PlayerWerewolfQuest's times); once a day, from SELECT as a
// power. Its numbers from pack/beast.txt (converter/companions.py).

void beast_init(const char *pack_dir);
void beast_clear(void); // (a new game)

int beast_known(void);          // the blood of the beast in you
void beast_give(void);          // (the ritual's)
int beast_ready(void);          // known and not used today
int beast_readied(void);        // SELECT's power now (the magic menu's)
void beast_ready_it(int on);
int beast_active(void);
// Into it (SELECT, the ritual): 1 if done - free: no day's use spent (the ritual's)
int beast_change(Vitals *v, int free);
void beast_revert(Vitals *v);   // back (time's up, or a quest's)
void beast_unbonus(Vitals *v);  // (its health and stamina taken off: saving you as yourself)
float beast_left(void);         // seconds left

float beast_claw_damage(int level); // (a blow's, at your level)
float beast_reach_m(void);
void beast_strike(int power);   // a blow begun: its clip (left, right, a power attack)
int beast_fed_on(int body, Vitals *v); // feeding on a body (creatures.h's index): 1 if it was fed on now - health, more time
void beast_pass_hours(float hours); // (the day's use back)

// Each frame: the body where you are (feet; heading as actors_player_heading_deg's), its clip
// from your pace (m/s); place is interior.h's. Time's up: back (1 returned once, the frame it is)
int beast_update(float dt, const float feet[3], float heading_deg, float speed, int place, Vitals *v);
// (the vitals' regeneration this frame: health's none while in it)
void beast_rates(Vitals *v);

// Saving: known, hours till the power's back
void beast_save(float out[2]);
void beast_load(const float in[2]);

#endif
