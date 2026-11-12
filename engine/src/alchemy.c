#include "alchemy.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "effects.h"
#include "perks.h"
#include "skills.h"

#define INGREDIENT_INIT_MULT 4.0f // Skyrim's fAlchemyIngredientInitMult
#define SKILL_FACTOR 1.5f         // ... fAlchemySkillFactor
#define MIX_MAX 3

// Skyrim's alchemy perks (form ids)
static const uint32_t ALCHEMIST[5] = {0x000be127, 0x000c07ca, 0x000c07cb, 0x000c07cc, 0x000c07cd};
#define PHYSICIAN 0x00058215u
#define BENEFACTOR 0x00058216u
#define POISONER 0x00058217u
static const uint32_t EXPERIMENTER[3] = {0x00058218, 0x00105f2a, 0x00105f2b};
#define GREEN_THUMB 0x00105f2eu
#define PURITY 0x0005821du

float alchemy_effect_value(int mgef, float magnitude, float seconds) {
	const MagicEffect *m = items_magic_effect(mgef);
	if (!m)
		return 0.0f;
	const float mag = magnitude > 0.0f ? magnitude : 1.0f, dur = seconds > 0.0f ? seconds / 10.0f : 1.0f;
	return floorf(m->cost * powf(mag * dur, 1.1f)); // (UESP: floor(base cost x (mag x dur)^1.1))
}

// The potion's strength for one effect (perks that depend on potion / poison: given `poison`,
// -1 before it's known)
static float power(const MagicEffect *m, int poison) {
	float alchemist = 1.0f;
	for (int r = 0; r < 5; r++)
		if (perks_has_form(ALCHEMIST[r]))
			alchemist = 1.2f + 0.2f * r;
	float p = INGREDIENT_INIT_MULT * (1.0f + (SKILL_FACTOR - 1.0f) * skills_state()->level[SK_ALCHEMY] / 100.0f) *
	          (1.0f + effects_skill(SK_ALCHEMY) / 100.0f) * alchemist;
	if ((m->keywords & MGEF_KW_RESTORE) && perks_has_form(PHYSICIAN))
		p *= 1.25f;
	if (poison == 0 && (m->keywords & MGEF_KW_BENEFICIAL) && perks_has_form(BENEFACTOR))
		p *= 1.25f;
	if (poison == 1 && (m->keywords & MGEF_KW_HARMFUL) && perks_has_form(POISONER))
		p *= 1.25f;
	return p;
}

static ItemEffect strengthen(ItemEffect e, int poison) {
	const MagicEffect *m = items_magic_effect(e.mgef);
	if (!m)
		return e;
	const float p = power(m, poison);
	if (m->flags & MGEF_POWER_DURATION)
		e.seconds = roundf(e.seconds * p);
	if ((m->flags & MGEF_POWER_MAGNITUDE) || !(m->flags & MGEF_POWER_DURATION))
		e.magnitude = roundf(e.magnitude * p);
	return e;
}

int alchemy_mix(const int *ingredients, int n, AlchemyMix *out) {
	memset(out, 0, sizeof(*out));
	if (n < 2 || n > MIX_MAX)
		return 0;
	ItemEffect have[MIX_MAX][ITEM_EFFECTS_MAX], base[ITEM_EFFECTS_MAX];
	int count[MIX_MAX], shared = 0;
	for (int i = 0; i < n; i++)
		count[i] = items_effects(ingredients[i], have[i], ITEM_EFFECTS_MAX);
	// The shared effects, each at its strongest ingredient's
	for (int i = 0; i < n; i++)
		for (int k = 0; k < count[i]; k++) {
			const int mgef = have[i][k].mgef;
			int seen = 0, others = 0;
			for (int s = 0; s < shared; s++)
				seen |= base[s].mgef == mgef;
			if (seen || mgef < 0)
				continue;
			ItemEffect best = have[i][k];
			ItemEffect best_strong = strengthen(best, -1);
			for (int j = i + 1; j < n; j++)
				for (int q = 0; q < count[j]; q++)
					if (have[j][q].mgef == mgef) {
						others++;
						const ItemEffect strong = strengthen(have[j][q], -1);
						if (alchemy_effect_value(mgef, strong.magnitude, strong.seconds) >
						    alchemy_effect_value(mgef, best_strong.magnitude, best_strong.seconds))
							best = have[j][q], best_strong = strong;
					}
			if (others && shared < ITEM_EFFECTS_MAX)
				base[shared++] = best;
		}
	if (!shared)
		return 0;
	// Potion or poison: the most valuable effect says
	float top = -1.0f;
	int poison = 0;
	for (int s = 0; s < shared; s++) {
		const ItemEffect strong = strengthen(base[s], -1);
		const float v = alchemy_effect_value(base[s].mgef, strong.magnitude, strong.seconds);
		if (v > top)
			top = v, poison = (items_magic_effect(base[s].mgef)->flags & MGEF_HOSTILE) != 0;
	}
	const int purity = perks_has_form(PURITY);
	float values[ITEM_EFFECTS_MAX];
	float total = 0.0f;
	for (int s = 0; s < shared; s++) {
		const int harmful = (items_magic_effect(base[s].mgef)->flags & MGEF_HOSTILE) != 0;
		if (purity && harmful != poison)
			continue;
		const ItemEffect e = strengthen(base[s], poison);
		const float v = alchemy_effect_value(e.mgef, e.magnitude, e.seconds);
		int at = out->n; // (strongest first)
		while (at > 0 && values[at - 1] < v)
			out->e[at] = out->e[at - 1], values[at] = values[at - 1], at--;
		out->e[at] = e, values[at] = v, out->n++;
		total += v;
	}
	if (!out->n)
		return 0;
	out->poison = poison, out->value = (int)total;
	snprintf(out->name, sizeof(out->name), "%s of %.37s", poison ? "Poison" : "Potion",
	         items_magic_effect(out->e[0].mgef)->name);
	return out->n;
}

int alchemy_make(const int *ingredients, int n) {
	AlchemyMix mix;
	if (!alchemy_mix(ingredients, n, &mix))
		return -1;
	for (int i = 0; i < n; i++)
		if (items_have(ingredients[i]) <= 0)
			return -1;
	const int item = items_create_potion(mix.name, mix.poison, mix.value, mix.e, mix.n);
	if (item < 0)
		return -1;
	// The effects they share are learned (in each of them)
	for (int i = 0; i < n; i++) {
		ItemEffect mine[ITEM_EFFECTS_MAX], other[ITEM_EFFECTS_MAX];
		const int c = items_effects(ingredients[i], mine, ITEM_EFFECTS_MAX);
		for (int k = 0; k < c; k++)
			for (int j = 0; j < n; j++) {
				if (j == i)
					continue;
				const int oc = items_effects(ingredients[j], other, ITEM_EFFECTS_MAX);
				for (int q = 0; q < oc; q++)
					if (other[q].mgef == mine[k].mgef)
						items_learn(ingredients[i], 1 << k);
			}
	}
	for (int i = 0; i < n; i++)
		items_add(ingredients[i], -1);
	items_add(item, 1);
	skills_use(SK_ALCHEMY, (float)mix.value);
	return item;
}

int alchemy_eat_reveals(void) {
	int n = 1;
	for (int r = 0; r < 3; r++)
		if (perks_has_form(EXPERIMENTER[r]))
			n = 2 + r;
	return n;
}

int alchemy_harvest_count(void) {
	return perks_has_form(GREEN_THUMB) ? 2 : 1;
}
