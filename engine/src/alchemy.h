#ifndef ALCHEMY_H
#define ALCHEMY_H

#include "items.h"

// Skyrim's alchemy: two or three ingredients mixed at an alchemy lab make a potion (or a
// poison) of every effect at least two of them share. Each effect's strength is Skyrim's:
// the ingredient's magnitude (or duration, as the effect says) x fAlchemyIngredientInitMult
// (4) x (1 + (fAlchemySkillFactor (1.5) - 1) x Alchemy / 100) x Fortify Alchemy x the perks
// (Alchemist 1.2 .. 2, Physician 1.25 for restoring health / magicka / stamina, Benefactor
// 1.25 for a potion's good effects, Poisoner 1.25 for a poison's bad ones), rounded; where
// ingredients share an effect at different strengths the strongest is kept. An effect's
// value is its base cost x magnitude^1.1 x (duration / 10)^1.1; the most valuable names it
// ("Potion of Restore Health", "Poison of Paralysis") and makes it a poison if it's harmful.
// Purity leaves out a potion's harmful effects and a poison's good ones. The Alchemy skill
// grows by the potion's value. Mixing teaches the shared effects; eating an ingredient its
// first (Experimenter: two, three, all four).

typedef struct {
	int n, poison, value;
	ItemEffect e[ITEM_EFFECTS_MAX]; // strongest first
	char name[48];
} AlchemyMix;

// What the ingredients (2 or 3 item indices) would make: its effects; 0 if they share none
int alchemy_mix(const int *ingredients, int n, AlchemyMix *out);
// Makes it: the ingredients used up, the potion carried, its effects learned, the skill used;
// the potion's item, -1 if nothing was made
int alchemy_make(const int *ingredients, int n);
// An effect's gold value at a strength (Skyrim's)
float alchemy_effect_value(int mgef, float magnitude, float seconds);
// How many of an ingredient's effects eating it reveals (1, Experimenter 2..4)
int alchemy_eat_reveals(void);
// Harvesting a plant (Green Thumb: two)
int alchemy_harvest_count(void);

#endif
