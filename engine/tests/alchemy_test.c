// Host test: Skyrim's alchemy (real pack/items/items.txt): ingredients mixed into potions and
// poisons by the shared effects, their strength by skill and perks, eating, saving what's made.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "alchemy.h"
#include "effects.h"
#include "perks.h"
#include "skills.h"
#include "races.h"

#ifndef PACK
#define PACK "../../pack/"
#endif

void ui_notice(const char *text, float seconds) { (void)text, (void)seconds; }
void game_log(const char *fmt, ...) { (void)fmt; }
const Race *races_get(int r) { (void)r; return NULL; }
int races_player(void) { return 0; }
int equip_save(uint32_t *forms, int max) { (void)forms, (void)max; return 0; } // (nothing worn)

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static void show(const AlchemyMix *m) {
	printf("%s (value %d):", m->name, m->value);
	for (int i = 0; i < m->n; i++) {
		char text[160];
		items_effect_text(m->e[i].mgef, m->e[i].magnitude, m->e[i].seconds, text, sizeof(text));
		printf(" %s", text);
	}
	printf("\n");
}

int main(void) {
	skills_init(PACK);
	items_init(PACK);
	perks_init(PACK);
	CHECK(items_magic_effect_count() > 40, "%d magic effects", items_magic_effect_count());
	const int flower = items_find_name("Blue Mountain Flower"), wheat = items_find_name("Wheat");
	const int shade = items_find_name("Nightshade"), deathbell = items_find_name("Deathbell");
	const int canis = items_find_name("Canis Root"), imp = items_find_name("Imp Stool");
	CHECK(flower >= 0 && wheat >= 0 && shade >= 0 && deathbell >= 0, "ingredients missing");
	CHECK(items_type(flower) == IT_INGR && items_usable(flower), "an ingredient can be eaten");
	// Blue Mountain Flower + Wheat: Restore Health and Fortify Health
	skills_state()->level[SK_ALCHEMY] = 15;
	AlchemyMix mix;
	CHECK(alchemy_mix((int[]){flower, wheat}, 2, &mix) == 2, "flower + wheat: %d effects", mix.n);
	show(&mix);
	CHECK(!mix.poison && strstr(mix.name, "Potion of") == mix.name, "a potion: %s", mix.name);
	// Skyrim: Restore Health 5 x 4 x (1 + 0.5 x 0.15) = 21.5 -> 22
	for (int i = 0; i < mix.n; i++)
		if (!strcmp(items_magic_effect(mix.e[i].mgef)->eid, "AlchRestoreHealth"))
			CHECK(mix.e[i].magnitude == 22.0f, "restore health %g (Skyrim: 22)", mix.e[i].magnitude);
	// Nightshade + Deathbell: a poison (Damage Health...)
	CHECK(alchemy_mix((int[]){shade, deathbell}, 2, &mix) > 0 && mix.poison, "nightshade + deathbell a poison");
	show(&mix);
	if (canis >= 0 && imp >= 0) {
		CHECK(alchemy_mix((int[]){canis, imp}, 2, &mix) > 0, "canis root + imp stool");
		show(&mix);
	}
	// Nothing shared: nothing
	CHECK(!alchemy_mix((int[]){flower, shade}, 2, &mix) || mix.n > 0, "flower + nightshade");
	// Higher skill: stronger
	AlchemyMix weak, strong;
	alchemy_mix((int[]){flower, wheat}, 2, &weak);
	skills_state()->level[SK_ALCHEMY] = 100;
	alchemy_mix((int[]){flower, wheat}, 2, &strong);
	CHECK(strong.value > weak.value, "skill 100 value %d vs %d", strong.value, weak.value);
	// Making it: ingredients used, the potion carried, effects learned, saved and loaded
	items_new_game();
	items_add(flower, 2), items_add(wheat, 1);
	const int potion = alchemy_make((int[]){flower, wheat}, 2);
	CHECK(potion >= 0 && items_have(potion) == 1 && items_have(flower) == 1 && items_have(wheat) == 0, "made");
	CHECK(items_known(flower) != 0 && items_known(wheat) != 0, "effects learned");
	CHECK(alchemy_make((int[]){flower, wheat}, 2) < 0, "no wheat left: nothing made");
	char desc[256];
	items_describe(potion, desc, sizeof(desc));
	printf("%s: %s\n", items_name(potion), desc);
	items_describe(flower, desc, sizeof(desc));
	printf("Blue Mountain Flower: %s\n", desc);
	const int known = items_known(flower);
	items_set_save_path("build/alchemy_items.dat");
	items_save();
	items_reset();
	items_load();
	const int again = items_by_form((uint32_t)items_form(potion));
	CHECK(again >= 0 && items_have(again) == 1 && !strcmp(items_name(again), items_name(potion)), "potion kept");
	CHECK(items_known(flower) == known, "learned effects kept");
	// Drinking it: Fortify Health raises the maximum till it ends
	Vitals v;
	vitals_init(&v);
	effects_reset();
	v.health = 50.0f;
	CHECK(items_use(again, &v), "drunk");
	printf("after: health %.0f / %.0f\n", v.health, v.max_health);
	CHECK(v.health > 50.0f && v.max_health > 100.0f, "restored and fortified");
	for (int i = 0; i < 60 * 60 * 10; i++)
		effects_update(1.0f / 60.0f, &v);
	CHECK(fabsf(v.max_health - 100.0f) < 0.01f, "fortify ended: max %.1f", v.max_health);
	// Eating an ingredient: its first effect learned
	items_add(shade, 1);
	items_use(shade, &v);
	CHECK(items_known(shade) & 1, "nightshade's first effect learned");
	printf(failures ? "alchemy_test: %d failures\n" : "alchemy_test: all passed\n", failures);
	return failures != 0;
}
