// Host test: Skyrim's perk trees from the real pack/perks.txt - taking perks (levels, the
// perks before them, points), and their effects through the entry points (Armsman's damage
// with a sword, Novice Destruction's cost, Agile Defender with light armour worn...).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "equip.h"
#include "items.h"
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

// (what's worn: perks ask; none here)
int equip_save(uint32_t *forms, int max) {
	(void)forms, (void)max;
	return 0;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

// The tree node of a skill whose first rank is the named perk, -1
static int node_named(int skill, const char *name) {
	for (int i = 0; i < perks_node_count(skill); i++) {
		float x, y;
		const int *links, *ranks;
		int nl, nr;
		if (perks_node(skill, i, &x, &y, &links, &nl, &ranks, &nr) && nr && !strcmp(perks_name(ranks[0]), name))
			return i;
	}
	return -1;
}

int main(void) {
	skills_init(PACK);
	items_init(PACK);
	perks_init(PACK);
	SkillState *st = skills_state();
	int total = 0;
	for (int s = 0; s < SKILL_COUNT; s++) {
		for (int i = 0; i < perks_node_count(s); i++) {
			float x, y;
			const int *links, *ranks;
			int nl, nr;
			if (perks_node(s, i, &x, &y, &links, &nl, &ranks, &nr))
				total += nr;
		}
	}
	printf("perks in the trees: %d\n", total);
	CHECK(total == 251, "251 perks with their ranks, got %d", total);

	// Armsman: One-handed's root perk, five ranks, 20% more damage a rank with one-handed weapons
	const int armsman = node_named(SK_ONE_HANDED, "Armsman");
	CHECK(armsman >= 0, "Armsman in One-handed's tree");
	char why[64];
	st->level[SK_ONE_HANDED] = 15; // (a Nord starts at 20)
	st->perk_points = 0;
	CHECK(!perks_can_take(SK_ONE_HANDED, armsman, why, sizeof(why)), "no points: can't take it");
	st->perk_points = 3;
	CHECK(perks_can_take(SK_ONE_HANDED, armsman, why, sizeof(why)), "Armsman rank 1 needs nothing: %s", why);
	CHECK(perks_take(SK_ONE_HANDED, armsman), "took Armsman");
	CHECK(st->perk_points == 2, "a point spent");
	CHECK(!perks_can_take(SK_ONE_HANDED, armsman, why, sizeof(why)), "rank 2 needs One-handed 20 (have 15)");
	printf("  rank 2 at One-handed 15: %s\n", why);
	const uint32_t sword[1] = {0x0001e711u}, bow[1] = {0x0001e715u}; // WeapTypeSword, WeapTypeBow
	PerkContext ctx = {sword, 1, -1, 0, 0, 0, 0, 0};
	float d = perks_apply(EP_MOD_ATTACK_DAMAGE, 10.0f, &ctx);
	printf("  a 10-damage sword with Armsman 1: %.1f\n", d);
	CHECK(fabsf(d - 12.0f) < 0.01f, "Armsman 1: x1.2 (%.2f)", d);
	PerkContext bctx = {bow, 1, -1, 0, 0, 0, 0, 0};
	CHECK(fabsf(perks_apply(EP_MOD_ATTACK_DAMAGE, 10.0f, &bctx) - 10.0f) < 0.01f, "Armsman doesn't help a bow");
	st->level[SK_ONE_HANDED] = 20;
	CHECK(perks_take(SK_ONE_HANDED, armsman), "Armsman rank 2 at 20");
	d = perks_apply(EP_MOD_ATTACK_DAMAGE, 10.0f, &ctx);
	printf("  with Armsman 2: %.1f\n", d);
	CHECK(fabsf(d - 14.0f) < 0.01f, "Armsman 2: x1.4, not 1.2 x 1.4 (%.2f)", d);

	// Novice Destruction: Flames costs half (its casting perk is the perk's condition)
	const int novice = node_named(SK_DESTRUCTION, "Novice Destruction");
	CHECK(novice >= 0 && perks_take(SK_DESTRUCTION, novice), "took Novice Destruction");
	const uint32_t fire[1] = {0x0001ceadu}; // MagicDamageFire
	PerkContext flames = {fire, 1, 20, 0x000f2ca8u, 0, 0, 0, 0}; // (Destruction, DestructionNovice00)
	const float cost = perks_apply(EP_MOD_SPELL_COST, 14.0f, &flames);
	printf("  Flames with Novice Destruction: %.1f magicka a second\n", cost);
	CHECK(fabsf(cost - 7.0f) < 0.01f, "half cost (%.2f)", cost);
	PerkContext rune = {fire, 1, 20, 0x000c44bfu, 0, 0, 0, 0}; // (DestructionApprentice25: not halved)
	CHECK(fabsf(perks_apply(EP_MOD_SPELL_COST, 234.0f, &rune) - 234.0f) < 0.01f, "Fire Rune still full cost");

	// A perk out of reach: one deeper in the tree needs the one before it
	const int fighting = node_named(SK_ONE_HANDED, "Fighting Stance");
	st->level[SK_ONE_HANDED] = 100;
	st->perk_points = 5;
	CHECK(fighting >= 0 && perks_can_take(SK_ONE_HANDED, fighting, why, sizeof(why)), "Fighting Stance after Armsman: %s",
	      why);
	const int savage = node_named(SK_ONE_HANDED, "Savage Strike");
	CHECK(savage >= 0 && !perks_can_take(SK_ONE_HANDED, savage, why, sizeof(why)), "Savage Strike needs Fighting Stance");
	printf("  Savage Strike before Fighting Stance: %s\n", why);

	// Saving and loading the perks taken
	uint8_t bits[PERKS_MAX / 8];
	perks_save(bits);
	const int taken = perks_taken_count();
	perks_reset();
	CHECK(perks_taken_count() == 0, "reset");
	perks_load(bits);
	CHECK(perks_taken_count() == taken, "loaded back %d of %d", perks_taken_count(), taken);
	d = perks_apply(EP_MOD_ATTACK_DAMAGE, 10.0f, &ctx);
	CHECK(fabsf(d - 14.0f) < 0.01f, "Armsman 2 after loading");

	printf(failures ? "perks_test: %d FAILED\n" : "perks_test: all passed\n", failures);
	return failures != 0;
}
