#ifndef EFFECTS_H
#define EFFECTS_H

#include "vitals.h"

// The magic effects on the player (Skyrim's active effects): from potions, food and
// ingredients (items.h's magic effects). Restore and Damage health / magicka / stamina act at
// once (or so much a second, for their seconds); a Fortify raises the maximum (and the value)
// till it ends, a Ravage lowers it; the rest (Fortify Carry Weight, Resist Fire, Regenerate
// Stamina, Slow, Invisibility, Paralysis, Waterbreathing...) last their seconds and are asked
// about by what they change (effects_av). The same effect again replaces the one before, as
// in Skyrim.

#define EFFECTS_MAX 32
#define AV_RESIST_POISON 40
#define AV_MAGIC_RESIST 44 // (Magic Resistance: a share of spells turned aside)

void effects_reset(void); // (a new game, a load, death: none)
void effects_apply(int mgef, float magnitude, float seconds, Vitals *v);
struct MagicEffect;
// ... any magic effect (a spell's: Invisibility, Waterbreathing; kept by pointer)
void effects_apply_effect(const struct MagicEffect *m, float magnitude, float seconds, Vitals *v);
// Each frame (unpaused): over-time effects tick, the ended ones go (their fortifies undone)
void effects_update(float dt, Vitals *v);
// What the active effects add to an actor value (Skyrim's: 32 carry weight, 40 poison resist,
// 41 fire, 42 shock, 43 frost, 44 magic, 30 speed, 155-157 regeneration %, skills' modifiers...);
// detrimental ones subtract
float effects_av(int av);
// Worn enchantments (enchant.c), each frame: what they add to each actor value (EFFECTS_WORN_AVS
// of them; Fortify Health / Magicka / Stamina raise the maximums)
#define EFFECTS_WORN_AVS 192
void effects_set_worn(const float *sums, Vitals *v);
float effects_rate_mult(int which); // regeneration: 0 health, 1 magicka, 2 stamina (1: none)
float effects_speed_mult(void);     // Slow: 0.5
int effects_paralysed(void);
int effects_invisible(void);
void effects_end_invisibility(void); // (acting - a blow, using something - ends it, as in Skyrim)
int effects_water_breathing(void);
// A skill's Fortify, % (a potion's, an enchantment's, a few potions' points on the skill alike;
// skills.h's order: 0 One-Handed .. 17 Enchanting); negative: weakened
float effects_skill(int skill);
float effects_shout_recovery_mult(void); // Fortify Shouts: the recovery's share left (1: none)
int effects_muffled(void);               // Muffle (an enchantment's): footsteps quiet
int effects_take_cured_poison(void);     // Cure Poison drunk since last asked (a bite's poison ends too)
// A potion's fortify raises the vitals' maximums: these are them without it (for saving)
void effects_unfortify(Vitals *v);
// The active effects for the magic menu: their names and seconds left; how many
int effects_list(const char **names, float *seconds_left, int max);

#endif
