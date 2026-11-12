#include "vitals.h"

#include <math.h>

#define HEALTH_REGEN 0.007f  // of the maximum per second (Skyrim: 0.7%, out of combat)
#define MAGICKA_REGEN 0.03f  // 3%
#define STAMINA_REGEN 0.05f  // 5%
#define SPRINT_COST 7.0f     // stamina per second
#define SPRINT_AGAIN 10.0f   // after running out: sprinting again once stamina is back to this
#define FALL_SAFE_M 5.0f     // falls up to this are free...
#define FALL_FATAL_M 20.0f   // ...damage grows with the square of the rest; this far kills at 100 health
#define BREATH_S 20.0f       // Skyrim's fActorSwimBreathBase
#define BREATH_REFILL 4.0f   // x as fast back at the surface
#define DROWN_RATE 0.08f     // of the maximum health per second (fActorSwimBreathDamage)
#define HUD_LINGER_S 3.0f
#define HUD_FADE_S 0.6f

void vitals_init(Vitals *v) {
	v->max_health = v->max_magicka = v->max_stamina = 100.0f;
	v->health = v->magicka = v->stamina = 100.0f;
	v->exhausted = 0;
	v->max_breath = v->breath = BREATH_S;
	v->shown_for = 1000.0f; // (long faded out)
	v->rate_mult[0] = v->rate_mult[1] = v->rate_mult[2] = 1.0f;
	v->water_breathing = 0;
}

static float regen(float value, float max, float rate, float dt) {
	return fminf(max, value + max * rate * dt);
}

int vitals_update(Vitals *v, float dt, int sprinting) {
	if (v->exhausted && v->stamina >= SPRINT_AGAIN)
		v->exhausted = 0;
	int can_sprint = !v->exhausted && v->stamina > 0.0f;
	if (sprinting && can_sprint) {
		v->stamina = fmaxf(0.0f, v->stamina - SPRINT_COST * dt);
		if (v->stamina <= 0.0f)
			v->exhausted = 1;
	} else {
		v->stamina = regen(v->stamina, v->max_stamina, STAMINA_REGEN * v->rate_mult[2], dt);
	}
	if (v->health > 0.0f)
		v->health = regen(v->health, v->max_health, HEALTH_REGEN * v->rate_mult[0], dt);
	v->magicka = regen(v->magicka, v->max_magicka, MAGICKA_REGEN * v->rate_mult[1], dt);
	int full = v->health >= v->max_health && v->magicka >= v->max_magicka && v->stamina >= v->max_stamina &&
	           v->breath >= v->max_breath;
	v->shown_for = full ? v->shown_for + dt : 0.0f;
	return can_sprint;
}

int vitals_breathe(Vitals *v, float dt, int head_under) {
	if (!head_under || v->water_breathing) {
		v->breath = fminf(v->max_breath, v->breath + BREATH_REFILL * dt);
		return 0;
	}
	v->shown_for = 0.0f;
	v->breath = fmaxf(0.0f, v->breath - dt);
	if (v->breath > 0.0f || v->health <= 0.0f)
		return 0;
	v->health -= v->max_health * DROWN_RATE * dt;
	if (v->health <= 0.0f) {
		v->health = 0.0f;
		return 1;
	}
	return 0;
}

int vitals_fall(Vitals *v, float drop_m) {
	if (drop_m <= FALL_SAFE_M)
		return 0;
	float k = (drop_m - FALL_SAFE_M) / (FALL_FATAL_M - FALL_SAFE_M);
	v->health -= 100.0f * k * k;
	v->shown_for = 0.0f;
	if (v->health <= 0.0f) {
		v->health = 0.0f;
		return 1;
	}
	return 0;
}

float vitals_hud_alpha(const Vitals *v) {
	if (v->shown_for <= HUD_LINGER_S)
		return 1.0f;
	return fmaxf(0.0f, 1.0f - (v->shown_for - HUD_LINGER_S) / HUD_FADE_S);
}
