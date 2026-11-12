#ifndef ACTIVATORS_H
#define ACTIVATORS_H

// Skyrim's scripted activators inside (the interior's .skint section, see
// converter/interiors.py), run by name as their Papyrus scripts do: a puzzle pillar turns
// to its next face (defaultPuzzlePillarScript); a pillar puzzle's lever opens what it
// guards when every pillar shows its solved face, else springs the traps
// (defaultPillarPuzzleLever); a lever or pull chain opens / closes what it's linked to
// (TrapLever...); a gate is what they open (default2StateActivator). A gate opened stays
// open (remembered with the unlocked containers).

#include <stdint.h>

// An interior's activators (after its loot section); NULL: none. Returns the position after
const uint8_t *activators_load(const uint8_t *p, const uint8_t *end);
// Outdoors: the cells' activators added (a puzzle by the road); cleared with the cells
const uint8_t *activators_append(const uint8_t *p, const uint8_t *end);
void activators_clear(void);
void activators_log_near(const float pos[3], float radius); // (testing)
int activators_force_open_near(const float pos[3], float radius);
int activators_open_near(const float pos[3], float radius); // (a script's gates: opened)
// The one under the crosshair within reach (-1: none): its distance along the ray, its label
int activators_aim(const float eye[3], const float dir[3], float *along, const char **label);
// Uses it (CIRCLE)
// A sarcophagus's lid near pos pushed off (the draugr in it rising); how many
int activators_open_lids(const float pos[3], float radius);
void activators_use(int index);
int activators_collapse_near(const float pos[3], float radius);
// A blow, an arrow or a spell along from..from + dir * len (dir unit): the spider webs it meets cut
// down for good; how many
int activators_strike(const float from[3], const float dir[3], float len);
int activators_web_down_near(const float pos[3], float radius); // (Arvel cut down: his web gone with him) // (a bridge brought down: Helgen Keep's, behind you)
int activators_pull_near(const float pos[3], float radius); // (testing: the nearest lever pulled)
// The nearest lever or chain that works something (not `skip`), within radius: its index (out:
// where), or -1
int activators_lever_near(const float pos[3], float radius, float out[3], int skip);
// Damage from traps sprung since last asked (darts: main takes it off the health)
float activators_take_damage(void);

#endif
