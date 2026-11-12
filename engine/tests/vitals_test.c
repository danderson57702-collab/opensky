// Host test: health / stamina rules (vitals.c)
#include <math.h>
#include <stdio.h>
#include "vitals.h"

int main(void) {
	int ok = 1;
	#define CHECK(c, msg) do { int r_ = (c); printf("%-52s %s\n", msg, r_ ? "PASS" : "FAIL"); ok &= r_; } while (0)
	Vitals v;
	vitals_init(&v);
	CHECK(!vitals_fall(&v, 4.9f) && v.health == 100.0f, "a 5 m fall is free");
	vitals_init(&v);
	CHECK(!vitals_fall(&v, 10.0f) && v.health > 80.0f && v.health < 95.0f, "a 10 m fall hurts a little");
	vitals_init(&v);
	CHECK(vitals_fall(&v, 21.0f) && v.health == 0.0f, "a 21 m fall kills");
	vitals_init(&v);
	int frames = 0;
	while (vitals_update(&v, 1 / 60.f, 1) && frames < 6000) frames++;
	CHECK(frames > 60 * 13 && frames < 60 * 16, "about 14 s of sprinting on full stamina");
	CHECK(!vitals_update(&v, 1 / 60.f, 1), "no sprinting while exhausted");
	for (int i = 0; i < 60 * 3; i++) vitals_update(&v, 1 / 60.f, 0);
	CHECK(vitals_update(&v, 1 / 60.f, 0), "sprinting again after resting a few seconds");
	vitals_init(&v);
	v.health = 50.0f;
	for (int i = 0; i < 60 * 10; i++) vitals_update(&v, 1 / 60.f, 0);
	CHECK(fabsf(v.health - 57.0f) < 0.5f, "health regenerates 0.7% a second");
	vitals_init(&v);
	CHECK(vitals_hud_alpha(&v) == 0.0f, "bars hidden while everything is full");
	vitals_fall(&v, 10.0f);
	CHECK(vitals_hud_alpha(&v) == 1.0f, "bars shown after taking damage");
	vitals_init(&v);
	for (int i = 0; i < 60 * 19; i++) vitals_breathe(&v, 1 / 60.f, 1);
	CHECK(v.breath > 0.0f && v.health == 100.0f, "19 s under water: still holding your breath");
	for (int i = 0; i < 60 * 2; i++) vitals_breathe(&v, 1 / 60.f, 1);
	CHECK(v.breath == 0.0f && v.health < 100.0f && v.health > 85.0f, "out of air: drowning starts");
	frames = 0;
	while (!vitals_breathe(&v, 1 / 60.f, 1) && frames < 6000) frames++;
	CHECK(frames > 60 * 9 && frames < 60 * 13, "about 12 s more and you drown");
	vitals_init(&v);
	for (int i = 0; i < 60 * 10; i++) vitals_breathe(&v, 1 / 60.f, 1);
	for (int i = 0; i < 60 * 3; i++) vitals_breathe(&v, 1 / 60.f, 0);
	CHECK(v.breath == v.max_breath, "air back after a few seconds at the surface");
	printf(ok ? "ALL PASS\n" : "FAILURES\n");
	return ok ? 0 : 1;
}
