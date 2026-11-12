// Host test: the item database from the pack (Skyrim's items, leveled lists, containers):
// rolling containers, using potions, saving and loading the inventory.
#include <stdio.h>
#include <string.h>

#include "items.h"
#include "skills.h"
#include "perks.h"
#include "races.h"

#ifndef PACK
#define PACK "../../pack/"
#endif

void ui_notice(const char *text, float seconds) { (void)text, (void)seconds; }
void game_log(const char *fmt, ...) { (void)fmt; }
int perks_has_form(uint32_t form) { (void)form; return 0; }
float perks_apply(int entry_point, float value, const PerkContext *ctx) { (void)entry_point, (void)ctx; return value; }
const Race *races_get(int r) { (void)r; return NULL; }
int races_player(void) { return 0; }

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static int container_named(const char *name) {
	for (int c = 0; c < 400; c++)
		if (!strcmp(items_container_name(c), name))
			return c;
	return -1;
}

int main(void) {
	skills_init(PACK);
	items_init(PACK);
	CHECK(items_count() > 1000, "%d items", items_count());
	// Chests at level 1: some loot, the same every time for the same chest
	int chest = container_named("Chest");
	CHECK(chest >= 0, "no Chest container");
	int filled = 0, total = 0, its[64], counts[64];
	for (unsigned ref = 1; ref <= 40; ref++) {
		int h = items_open_container(0x100000u + ref, chest);
		int n = items_in_container(h, its, counts, 64);
		filled += n > 0, total += n;
		if (ref <= 3) {
			printf("chest %u:", ref);
			for (int i = 0; i < n; i++)
				printf(" %s x%d,", items_name(its[i]), counts[i]);
			printf("\n");
		}
	}
	CHECK(filled > 30, "only %d of 40 chests had anything", filled);
	int h = items_open_container(0x100001u, chest), n1 = items_in_container(h, its, counts, 64);
	int again = items_open_container(0x100001u, chest);
	CHECK(again == h, "the same chest opened twice rolled again");
	// Take everything from it
	for (int i = 0; i < n1; i++)
		items_take(h, its[i], counts[i]);
	CHECK(items_in_container(h, its, counts, 64) == 0, "chest not empty after taking all");
	printf("carrying %.1f weight, %d gold\n", items_carried(), items_gold());
	// A potion of minor healing
	int potion = -1;
	for (int i = 0; i < items_count(); i++)
		if (!strcmp(items_name(i), "Potion of Minor Healing"))
			potion = i;
	CHECK(potion >= 0, "no Potion of Minor Healing");
	int before = items_have(potion);
	items_add(potion, 2);
	Vitals v;
	vitals_init(&v);
	v.health = 50.0f;
	CHECK(items_use(potion, &v) && v.health == 75.0f && items_have(potion) == before + 1, "minor healing: health %.0f", v.health);
	// Save and load
	int had = items_have(potion), gold = items_gold();
	items_set_taken(0xABCDEFu);
	items_save();
	items_reset();
	CHECK(items_have(potion) == 0, "reset kept items");
	items_load();
	CHECK(items_have(potion) == had && items_gold() == gold && items_taken(0xABCDEFu), "save/load lost things");
	CHECK(items_in_container(items_open_container(0x100001u, chest), its, counts, 64) == 0, "emptied chest refilled after loading");
	// An NPC's pockets and the pickpocket chances (Skyrim's settings)
	int alvor = items_npc_pockets("alvor");
	CHECK(alvor >= 0, "no pockets for Alvor");
	int ph = items_open_container(0x80000001u, alvor), pn = items_in_container(ph, its, counts, 64);
	printf("Alvor's pockets:");
	for (int i = 0; i < pn; i++)
		printf(" %s x%d (%d%%),", items_name(its[i]), counts[i], items_pickpocket_chance(its[i], counts[i], 15, 15));
	printf("\n");
	int gold_item = -1;
	for (int i = 0; i < items_count(); i++)
		if (!strcmp(items_name(i), "Gold"))
			gold_item = i;
	CHECK(items_pickpocket_chance(gold_item, 10, 15, 15) > items_pickpocket_chance(gold_item, 200, 15, 15),
	      "more gold should be harder");
	CHECK(items_pickpocket_chance(gold_item, 10, 100, 15) == 90, "a master thief is capped at 90%%");
	// Skyrim's cell reset: a chest emptied 10 game days ago refills; one you keep things in doesn't
	items_new_game();
	items_set_clock(100.0f);
	int looted = items_open_container(0x200001u, chest), n_loot = items_in_container(looted, its, counts, 64);
	for (int i = 0; i < n_loot; i++)
		items_take(looted, its[i], counts[i]);
	int home = items_open_container(0x200002u, chest);
	items_put(home, items_find_gold(), 0);
	items_add(items_find_gold(), 5);
	items_put(home, items_find_gold(), 5);
	items_save(); // (the times kept)
	items_load();
	items_set_clock(200.0f);
	CHECK(items_reset_containers(240.0f) == 0, "reset before 10 days");
	items_set_clock(341.0f);
	CHECK(items_reset_containers(240.0f) == 1, "the looted chest wasn't reset");
	CHECK(items_in_container(items_open_container(0x200002u, chest), its, counts, 64) >= 1, "the kept chest was emptied");
	printf("a chest looted 10 days ago: %d things again\n",
	       items_in_container(items_open_container(0x200001u, chest), its, counts, 64));
	remove("opensky_items.dat");
	printf(failures ? "items_test: %d failures\n" : "items_test: all passed\n", failures);
	return failures != 0;
}
