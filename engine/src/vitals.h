#ifndef VITALS_H
#define VITALS_H

// The player's health, magicka and stamina, as in Skyrim: 100 each to start, regenerating
// over time (health slowly, magicka faster, stamina fastest), stamina spent sprinting, health
// lost to long falls. Under water you hold your breath (Skyrim: 20 s), then drown (8% of
// your health a second).

typedef struct {
	float health, magicka, stamina;
	float max_health, max_magicka, max_stamina;
	float breath, max_breath; // seconds of air left (under water), refilling at the surface
	int exhausted;     // stamina ran out: no sprinting until it has come back a little
	float shown_for;   // seconds since the bars last had a reason to be on screen
	float rate_mult[3]; // health, magicka, stamina regeneration x this (racial powers; 1)
	int water_breathing; // never runs out of air (an Argonian)
} Vitals;

void vitals_init(Vitals *v);
// Regeneration and the sprint's cost; returns whether sprinting is possible right now
int vitals_update(Vitals *v, float dt, int sprinting);
// Damage from a landing after falling `drop_m`; returns 1 if it was fatal
int vitals_fall(Vitals *v, float drop_m);
// Holding your breath while the head is under water (refills above it); drowning hurts.
// Returns 1 if it was fatal
int vitals_breathe(Vitals *v, float dt, int head_under);
// How visible the HUD bars should be (0..1): shown while anything isn't full or just
// changed, fading out a few seconds after
float vitals_hud_alpha(const Vitals *v);

#endif
