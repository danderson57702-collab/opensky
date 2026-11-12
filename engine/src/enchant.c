#include "enchant.h"
#include <stdio.h>
#include "ui.h"

#include <math.h>
#include <string.h>

#include "creatures.h"
#include "effects.h"
#include "equip.h"
#include "items.h"
#include "sfx.h"
#include "spellfx.h"

// Skyrim's magic effect archetypes enchantments use (items.h has Value, Paralysis, Peak Value)
enum { ARCH_ABSORB = 4, ARCH_DUAL_VALUE = 5, ARCH_CALM = 6, ARCH_DEMORALIZE = 7, ARCH_TURN_UNDEAD = 24 };
#define AV_HEALTH 24
#define AV_MAGICKA 25
#define AV_STAMINA 26
#define RESIST_FIRE 41 // (the actor value resisting it: its element)
#define RESIST_SHOCK 42
#define RESIST_FROST 43
#define WORN_MAX 32

static int element_of(int resist) {
	return resist == RESIST_FIRE ? ELEMENT_FIRE : resist == RESIST_FROST ? ELEMENT_FROST
	       : resist == RESIST_SHOCK ? ELEMENT_SHOCK : -1;
}

static void gain(float *now, float max, float amount) {
	*now = fminf(max, *now + amount);
}

static Vitals *player_vitals;

void enchant_init(Vitals *v) {
	player_vitals = v;
}

int enchant_strike(int weapon) {
	Vitals *v = player_vitals;
	ItemEffect e[ITEM_EFFECTS_MAX];
	int resist[ITEM_EFFECTS_MAX];
	const int n = items_enchantment(weapon, e, resist, ITEM_EFFECTS_MAX);
	float at[3];
	if (!n || !v || !creatures_magic_struck(0.0f, at))
		return 0;
	if (!items_charge_use(weapon)) { // (its charge spent: the blade alone - Skyrim's, till a soul gem fills it)
		static int told_item = -1;
		if (told_item != weapon) {
			char text[96];
			snprintf(text, sizeof(text), "%s has no charge left", items_name(weapon));
			ui_notice(text, 3.0f);
			told_item = weapon;
		}
		return 0;
	}
	int element = -1;
	for (int i = 0; i < n; i++) {
		const MagicEffect *m = items_magic_effect(e[i].mgef);
		if (!m)
			continue;
		if (element < 0)
			element = element_of(resist[i]);
		switch (m->archetype) {
		case ARCH_VALUE:
		case ARCH_DUAL_VALUE:
		case ARCH_PEAK_VALUE: // (Fire, Frost, Shock Damage: its health, at once or over its seconds)
			if (m->av == AV_HEALTH && (m->flags & MGEF_DETRIMENTAL)) {
				if (e[i].seconds > 1.0f)
					creatures_bleed_struck(e[i].magnitude, e[i].seconds);
				else
					creatures_magic_struck(e[i].magnitude, at);
			}
			break;
		case ARCH_ABSORB: // (taken from it, given to you)
			if (m->av == AV_HEALTH) {
				creatures_magic_struck(e[i].magnitude, at);
				gain(&v->health, v->max_health, e[i].magnitude);
			} else if (m->av == AV_STAMINA) {
				gain(&v->stamina, v->max_stamina, e[i].magnitude);
			} else if (m->av == AV_MAGICKA) {
				gain(&v->magicka, v->max_magicka, e[i].magnitude);
			}
			v->shown_for = 0.0f;
			break;
		case ARCH_PARALYSIS:
			creatures_paralyze_struck(e[i].seconds);
			break;
		case ARCH_DEMORALIZE: // (its magnitude: the highest level it frightens)
			creatures_bewitch_struck(CSPELL_FEAR, e[i].magnitude, e[i].seconds);
			break;
		case ARCH_CALM:
			creatures_bewitch_struck(CSPELL_CALM, e[i].magnitude, e[i].seconds);
			break;
		case ARCH_TURN_UNDEAD:
			creatures_bewitch_struck(CSPELL_TURN, e[i].magnitude, e[i].seconds);
			break;
		}
	}
	if (element >= 0) { // (a flash of its element where it struck)
		spellfx_burst(at, element, 0.5f);
		sfx_play_at(element == ELEMENT_FIRE ? "mag_fire_hit" : element == ELEMENT_FROST ? "mag_frost_hit" : "mag_shock_hit",
		            at, 25.0f);
	}
	return 1;
}

void enchant_worn_update(Vitals *v) {
	static uint32_t was[WORN_MAX];
	static int was_n = -1;
	static float sums[EFFECTS_WORN_AVS];
	uint32_t forms[WORN_MAX];
	const int n = equip_save(forms, WORN_MAX);
	if (n != was_n || memcmp(forms, was, n * sizeof(uint32_t))) { // (what's worn changed: summed again)
		memcpy(was, forms, n * sizeof(uint32_t)), was_n = n;
		memset(sums, 0, sizeof(sums));
		for (int w = 0; w < n; w++) {
			const int item = items_by_form(forms[w]);
			if (item < 0 || items_type(item) != IT_ARMO)
				continue;
			ItemEffect e[ITEM_EFFECTS_MAX];
			const int ne = items_enchantment(item, e, NULL, ITEM_EFFECTS_MAX);
			for (int i = 0; i < ne; i++) {
				const MagicEffect *m = items_magic_effect(e[i].mgef);
				if (m && m->av >= 0 && m->av < EFFECTS_WORN_AVS &&
				    (m->archetype == ARCH_VALUE || m->archetype == ARCH_PEAK_VALUE || m->archetype == ARCH_DUAL_VALUE))
					sums[m->av] += (m->flags & MGEF_DETRIMENTAL) ? -e[i].magnitude : e[i].magnitude;
			}
		}
	}
	effects_set_worn(sums, v);
}

unsigned enchant_color(int item) {
	ItemEffect e[ITEM_EFFECTS_MAX];
	int resist[ITEM_EFFECTS_MAX];
	const int n = items_type(item) == IT_WEAP ? items_enchantment(item, e, resist, ITEM_EFFECTS_MAX) : 0;
	if (!n)
		return 0;
	for (int i = 0; i < n; i++)
		switch (element_of(resist[i])) {
		case ELEMENT_FIRE: return 0xff6a1a;
		case ELEMENT_FROST: return 0x6ac8ff;
		case ELEMENT_SHOCK: return 0xb48cff;
		}
	const MagicEffect *m = items_magic_effect(e[0].mgef);
	if (m && m->archetype == ARCH_ABSORB)
		return 0xe0203a; // (Absorb: blood red)
	if (m && m->archetype == ARCH_PARALYSIS)
		return 0x9ae05a;
	return 0xd8c060; // (the rest - Fear, Turn Undead, Soul Trap: Skyrim's golden shimmer)
}
