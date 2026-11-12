#ifndef ENCHANT_H
#define ENCHANT_H

#include "vitals.h"

// Skyrim's enchanted weapons and armour (items.h's enchantments: converter/items.py E lines).
// A weapon's acts on each blow or arrow that lands - Fire, Frost and Shock damage, Absorb
// Health / Stamina / Magicka, Paralysis, Fear, Calm, Turn Undead - with a flash of its element
// on what it struck; armour's (rings, amulets, circlets too) last while worn: Fortify Health,
// resistances, skills, carry weight, Waterbreathing, Muffle... (effects.h asks about them).
// Each strike spends the weapon's charge (Skyrim's); run dry, it's the blade alone till recharged.

// The player's vitals (Absorb Health, Stamina, Magicka fill them)
void enchant_init(Vitals *v);
// After a blow or an arrow from this weapon (item) landed on a creature (creatures.h's last
// struck): its enchantment; 1 if it was enchanted
int enchant_strike(int weapon);
// Each frame: what the worn armour's enchantments add (effects_set_worn)
void enchant_worn_update(Vitals *v);
// A weapon's look: the colour of its enchantment's element (0xRRGGBB), 0 if none
unsigned enchant_color(int item);

#endif
