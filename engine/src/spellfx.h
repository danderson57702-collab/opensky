#ifndef SPELLFX_H
#define SPELLFX_H

#include "magic.h"

// What spells look like, with Skyrim's effect textures: the readied spell in the hands
// (a flame, a golden glow, a cool light), Flames' stream of fireballs flowing from the
// palms to where it stops (and burning there), Healing's glow and gleams in the hands,
// its circle at your feet in third person, and Candlelight's floating light.

// Each frame, after magic_update: palms (left, right; world), whether the body is seen
// (third person: the hand effects are drawn with the world; first person: over the arms),
// the feet (Healing's circle) and the time (s)
void spellfx_update(float dt, float time_s, const MagicFx *fx, const float palms[2][3], int third_person,
                    const float feet[3]);
// The stream, circle, light and (third person) hand effects: with the world
void spellfx_draw_world(float yaw_deg, float pitch_deg);
// The camera this frame (before spellfx_update: bolts are laid along their segments as seen)
void spellfx_set_view(float yaw_deg, float pitch_deg);
// First person: the hand effects, after the arms
void spellfx_draw_hands(float yaw_deg, float pitch_deg);

enum { ELEMENT_FIRE, ELEMENT_FROST, ELEMENT_SHOCK, ELEMENT_POISON, ELEMENT_FORCE };
// An explosion of an element here (a fireball's, a rune's), about radius across
void spellfx_burst(const float pos[3], int element, float radius);
// At most this many particles at once (the performance setting; up to 400)
void spellfx_set_particle_cap(int n);
// A dead dragon's soul: wisps off the body (within spread of from) streaming to the player (each frame)
void spellfx_soul(const float from[3], float spread, const float to[3], float dt);
// ... and its streams: bright heads running along curves from the body into the player (each frame; t: s since it began)
void spellfx_soul_stream(const float from[3], float spread, const float to[3], float t);
// A word wall's word: pale light drifting off the wall into the player (each frame)
void spellfx_word(const float from[3], const float to[3], float dt);
// A word wall's word glowing blue (each frame it's to glow; strength 0..1 - brighter as it's learned)
void spellfx_wall_glow(const float at[3], float strength);
// A Greybeard's word shouted onto the floor (High Hrothgar's trials): glowing where it lies
void spellfx_floor_word(const float at[3], float strength);
// Clairvoyance: a wisp sent along these points (n, x y z each) - the way to what you're after
void spellfx_guide_launch(const float *pts, int n);
// A cone rolling out from `from` along dir (unit) to range, spread (tan of its half angle):
// a shout - Unrelenting Force's rippling air (ELEMENT_FORCE), a breath of fire or frost;
// strength 0..1 (the shout's words)
void spellfx_wave(const float from[3], const float dir[3], float range, float spread, int element, float strength);

// A shout's look: an aura about the shouter (from mouth), a ring over the ground, rings of its
// element travelling out along dir to range; strength 0..1 (its words)
void spellfx_shout(const float mouth[3], const float dir[3], float range, int element, float strength);

#endif
