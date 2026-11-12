#include "effects.h"

#include <math.h>
#include <string.h>

#include "items.h"

#define AV_HEALTH 24
#define AV_STAMINA 26
#define AV_SPEED 30
#define AV_WATER_BREATHING 57
#define AV_SHOUT_RECOVERY 86 // (ShoutRecoveryMult: Fortify Shouts, % faster)
#define AV_MOVEMENT_NOISE 92 // (MovementNoiseMult: Muffle)
#define ARCH_CURE_POISON 29

typedef struct {
	const MagicEffect *m;
	float magnitude, left; // (left: seconds; over-time restores tick magnitude a second)
	int ticks;             // restores / damage over time: so much a second
	int fortify;           // the maximum raised (or lowered: a ravage) by `magnitude`
	int sign;              // -1 detrimental
} Active;

static Active active[EFFECTS_MAX];
static int active_count, cured_poison;

// Worn enchantments (enchant.c): what they add to each actor value; Fortify Health / Magicka /
// Stamina raise the maximums by worn_vitals
#define WORN_AVS 192
static float worn[WORN_AVS], worn_vitals[3];

void effects_reset(void) {
	active_count = 0;
	memset(worn, 0, sizeof(worn)), memset(worn_vitals, 0, sizeof(worn_vitals)); // (the vitals are fresh too)
}

static float *vital(Vitals *v, int av, float **max) {
	*max = av == AV_HEALTH ? &v->max_health : av == AV_HEALTH + 1 ? &v->max_magicka : &v->max_stamina;
	return av == AV_HEALTH ? &v->health : av == AV_HEALTH + 1 ? &v->magicka : &v->stamina;
}

static void end(int i, Vitals *v) {
	const Active *a = &active[i];
	const MagicEffect *m = a->m;
	if (a->fortify && m) {
		float *max, *value = vital(v, m->av, &max);
		*max -= a->sign * a->magnitude;
		*value = fminf(*value, *max);
	}
	active[i] = active[--active_count];
}

void effects_apply(int mgef, float magnitude, float seconds, Vitals *v) {
	effects_apply_effect(items_magic_effect(mgef), magnitude, seconds, v);
}

void effects_apply_effect(const MagicEffect *m, float magnitude, float seconds, Vitals *v) {
	if (!m)
		return;
	for (int i = 0; i < active_count; i++) // (the same again: replaces it)
		if (active[i].m == m)
			end(i--, v);
	if (m->archetype == ARCH_CURE_POISON) { // (what's draining you over time ends; a bite's poison too: main)
		for (int i = 0; i < active_count; i++)
			if (active[i].sign < 0 && active[i].ticks)
				end(i--, v);
		cured_poison = 1;
		return;
	}
	const int sign = m->flags & MGEF_DETRIMENTAL ? -1 : 1;
	const int vit = m->av >= AV_HEALTH && m->av <= AV_STAMINA &&
	                (m->archetype == ARCH_VALUE || m->archetype == ARCH_PEAK_VALUE);
	v->shown_for = 0.0f;
	if (seconds < 1.0f) { // Restore / Damage (a Ravage without seconds too): at once
		if (vit) {
			float *max, *value = vital(v, m->av, &max);
			*value = fmaxf(0.0f, fminf(*max, *value + sign * magnitude));
		}
		return; // (others without seconds - cures - change nothing here)
	}
	if (active_count >= EFFECTS_MAX)
		return;
	Active *a = &active[active_count++];
	memset(a, 0, sizeof(*a));
	a->m = m, a->magnitude = magnitude, a->left = seconds, a->sign = sign;
	if (vit && (m->flags & MGEF_RECOVER)) { // Fortify / Ravage: the maximum, till it ends
		float *max, *value = vital(v, m->av, &max);
		a->fortify = 1;
		*max += sign * magnitude;
		*value = fmaxf(0.0f, fminf(*max, *value + sign * magnitude));
	} else if (vit) {
		a->ticks = 1; // (so much a second)
	}
}

void effects_update(float dt, Vitals *v) {
	for (int i = 0; i < active_count; i++) {
		Active *a = &active[i];
		const float step = fminf(dt, a->left);
		if (a->ticks) {
			float *max, *value = vital(v, a->m->av, &max);
			*value = fmaxf(0.0f, fminf(*max, *value + a->sign * a->magnitude * step));
			v->shown_for = 0.0f;
		}
		a->left -= dt;
		if (a->left <= 0.0f)
			end(i--, v);
	}
}

void effects_set_worn(const float *sums, Vitals *v) {
	for (int k = 0; k < 3; k++) {
		const float d = sums[AV_HEALTH + k] - worn_vitals[k];
		if (d == 0.0f)
			continue;
		float *max, *now = vital(v, AV_HEALTH + k, &max);
		*max += d, worn_vitals[k] = sums[AV_HEALTH + k];
		*now = fmaxf(1.0f, fminf(*max, *now + fmaxf(d, 0.0f))); // (put on: that much more; taken off: no more than the rest)
	}
	for (int av = 0; av < WORN_AVS; av++)
		worn[av] = av >= AV_HEALTH && av < AV_HEALTH + 3 ? 0.0f : sums[av];
}

float effects_av(int av) {
	float sum = av >= 0 && av < WORN_AVS ? worn[av] : 0.0f;
	for (int i = 0; i < active_count; i++) {
		const MagicEffect *m = active[i].m;
		if (m && m->av == av && !active[i].fortify && !active[i].ticks)
			sum += active[i].sign * active[i].magnitude;
	}
	return sum;
}

float effects_rate_mult(int which) {
	const float pct = effects_av(27 + which) + effects_av(155 + which); // (HealRate.., HealRateMult..)
	return fmaxf(0.0f, 1.0f + pct / 100.0f);
}

float effects_speed_mult(void) {
	for (int i = 0; i < active_count; i++) { // (Skyrim's Slow: half speed, whatever its magnitude)
		const MagicEffect *m = active[i].m;
		if (m && m->av == AV_SPEED && active[i].sign < 0)
			return 0.5f;
	}
	return fmaxf(0.2f, 1.0f + effects_av(AV_SPEED) / 100.0f);
}

static int has_archetype(int archetype) {
	for (int i = 0; i < active_count; i++) {
		const MagicEffect *m = active[i].m;
		if (m && m->archetype == archetype)
			return 1;
	}
	return 0;
}

int effects_paralysed(void) {
	return has_archetype(ARCH_PARALYSIS);
}

int effects_invisible(void) {
	return has_archetype(ARCH_INVISIBILITY);
}

void effects_end_invisibility(void) {
	for (int i = 0; i < active_count; i++) {
		const MagicEffect *m = active[i].m;
		if (m && m->archetype == ARCH_INVISIBILITY)
			active[i] = active[--active_count], i--;
	}
}

float effects_skill(int skill) {
	// (Skyrim keeps three: a potion's <skill>PowerModifier, 135 on; an enchantment's
	// <skill>Modifier, 96 on; and a few potions' points on the skill itself, 6 on)
	return skill < 0 || skill >= 18 ? 0.0f : effects_av(135 + skill) + effects_av(96 + skill) + effects_av(6 + skill);
}

float effects_shout_recovery_mult(void) {
	return fminf(1.0f, fmaxf(0.2f, 1.0f - effects_av(AV_SHOUT_RECOVERY) / 100.0f));
}

int effects_muffled(void) {
	return effects_av(AV_MOVEMENT_NOISE) != 0.0f;
}

int effects_take_cured_poison(void) {
	const int c = cured_poison;
	cured_poison = 0;
	return c;
}

int effects_water_breathing(void) {
	return effects_av(AV_WATER_BREATHING) > 0.0f;
}

void effects_unfortify(Vitals *v) {
	for (int k = 0; k < 3; k++) {
		float *max;
		vital(v, AV_HEALTH + k, &max);
		*max -= worn_vitals[k];
	}
	for (int i = 0; i < active_count; i++)
		if (active[i].fortify) {
			const MagicEffect *m = active[i].m;
			float *max;
			vital(v, m->av, &max);
			*max -= active[i].sign * active[i].magnitude;
		}
}

int effects_list(const char **names, float *seconds_left, int max) {
	int n = 0;
	for (int i = 0; i < active_count && n < max; i++) {
		const MagicEffect *m = active[i].m;
		if (m)
			names[n] = m->name, seconds_left[n++] = active[i].left;
	}
	return n;
}
