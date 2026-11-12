#include "thieves.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actors.h"
#include "brawl.h"
#include "creatures.h"
#include "gamelog.h"
#include "items.h"
#include "questkit.h"
#include "quests.h"
#include "skills.h"
#include "ui.h"
#include "gametext.h"

#define FLAGON "riftenraggedflagon"
#define CISTERN "riftenthievesguildheadquarters"
#define GOLDENGLOW_BASEMENT "goldenglowestate03"
#define HONNINGBREW_BASEMENT "honningbrewmeadery02"
#define HONNINGBREW_BOILERY "honningbrewmeadery03"
#define HONNINGBREW "honningbrewmeadery01"
#define SNOW_VEIL "snowveilsanctum02"
#define MUSEUM "markarthwizardsquarters01"
#define MERCERS_HOUSE "riftenmercerfreyhouse"
#define NIGHTINGALE_HALL "nightingalehall01"
#define IRKNGTHAND_SANCTUARY "irkngthand04"
#define BRONZE_WATER "bronzewatercave"
#define SEPULCHER "twilightsepulcher01"
#define SEPULCHER_INNER "twilightsepulcher02"

#define TAKE_M 2.5f          // a quest's thing taken this near its mark (the strongbox, the safe, the plans)
#define PLANT_M 2.0f         // Madesi's ring slipped onto Brand-Shei this near him
#define HIVES_M 22.0f        // fire loosed this near Goldenglow's apiary: a hive burns
#define HIVE_EVERY_S 1.5f    // (one at a time)
#define GLYPH_M 1.6f
#define DEBT_GOLD 100        // each one's payment (Skyrim's: Brynjolf asks after 300 in all)
#define TONILIA_REWARD 0

static const char *const NAMES[TG_COUNT] = {T_QUST_00021556_FULL0, T_QUST_0001F326_FULL0, T_QUST_0002154E_FULL0,
                                              T_QUST_00084ABD_FULL0, T_QUST_0002154F_FULL0, T_QUST_00021550_FULL0, T_QUST_00021551_FULL0,
                                              T_QUST_00021552_FULL0, T_QUST_00021553_FULL0, T_QUST_00057F99_FULL0, T_QUST_00021554_FULL0,
                                              T_QUST_00021555_FULL0, T_QUST_000D7D69_FULL0};

static int stage[TG_COUNT];
static char objective_now[TG_COUNT][72];
static int step, flags, advanced = -1, hives, brawling = -1;
static float hive_t;
enum { F_KEERAVA = 1, F_BERSI = 2, F_HAELGA = 4, F_SAFE = 8 };
static QkEscort esc;
static QkFoes fo;

static void set_stage(int q, int s, const char *objective) {
	const int was = stage[q];
	stage[q] = s;
	snprintf(objective_now[q], sizeof(objective_now[q]), "%s", objective ? objective : "");
	qk_stage_notice(NAMES[q], was, s, objective);
	advanced = qk_advance_count();
}

static void done(int q) {
	set_stage(q, QUEST_DONE, "");
}

// --- The talks: who's spoken to at which stage, what they say (Skyrim's conversation), and what
//     follows once it's said (the stage after, its objective, anything given or begun) -------------

enum { A_NONE, A_BRAWL, A_NEXT, A_GIVE_POISON, A_TAKE_WINE, A_ESCORT_MERCER, A_TAKE_RUBBING, A_TAKE_PLANS,
       A_ESCORT_IRK, A_ARMOR, A_DEBTS, A_TAKE_BILL, A_KEY_DONE, A_LEADER_ARMOR };
typedef struct {
	int q, at;
	const char *who, *say, *play;
	int next; // (the stage after: QUEST_DONE ends it)
	const char *objective;
	int action, then_q, then_stage; // (A_NEXT: that quest begun at that stage)
	const char *then_objective;
} Talk;

static const Talk TALKS[] = {
	{TG_CHANCE, 0, "brynjolf", T_DIAL_000352D1_FULL0, "tg00_brynjolf", 10, T_QUST_00021556_NNAM1},
	{TG_CHANCE, 30, "brynjolf", T_DIAL_0401BFB6_FULL0, "tg00_brynjolf_done", QUEST_DONE, "", A_NEXT, TG_BUSINESS, 10,
	 T_QUST_0001F326_NNAM0},
	{TG_BUSINESS, 10, "brynjolf", T_DIAL_00020530_FULL0, "tg01_brynjolf", 20, "Collect the debts: Keerava, Bersi, Haelga"},
	{TG_BUSINESS, 20, "keerava", T_DIAL_0002152F_FULL0, "tg01_keerava", 20, NULL, A_BRAWL},
	{TG_BUSINESS, 20, "bersi", T_DIAL_00021E73_FULL0, "tg01_bersi", 20, NULL, A_BRAWL},
	{TG_BUSINESS, 20, "haelga", T_DIAL_00021E73_FULL0, "tg01_haelga", 20, NULL, A_BRAWL},
	{TG_BUSINESS, 50, "brynjolf", "Here's what they owe.", "tg01_brynjolf_done", QUEST_DONE, "", A_DEBTS, TG_LOUD, 10,
	 T_QUST_0002154E_NNAM2},
	{TG_LOUD, 10, "brynjolf", T_DIAL_00025D47_FULL0, "tg02_brynjolf", 40, "Burn three bee hives and clear out Aringoth's safe"},
	{TG_LOUD, 60, "brynjolf", T_DIAL_0002BE46_FULL0, "tg02_brynjolf_done", QUEST_DONE, "", A_TAKE_BILL,
	 TG_SPIRITS, 10, T_QUST_0002154F_NNAM0},
	{TG_FAMILY, 10, "vex", T_DIAL_000847F5_FULL0, "tg02b_vex", 20, T_QUST_00084ABD_NNAM1},
	{TG_FAMILY, 20, "delvinmallory", T_DIAL_000847E1_FULL0, "tg02b_delvin", 30, T_QUST_00084ABD_NNAM2},
	{TG_FAMILY, 30, "tonilia", T_DIAL_000D33BC_FULL0, "tg02b_tonilia", QUEST_DONE, "", A_ARMOR},
	{TG_SPIRITS, 10, "maven", T_DIAL_0002BA69_FULL0, "tg03_maven", 20, T_QUST_0002154F_NNAM1},
	{TG_SPIRITS, 20, "mallusmaccius", T_DIAL_0002BA5A_FULL0, "tg03_mallus", 30, T_QUST_0002154F_NNAM2},
	{TG_SPIRITS, 30, "sabjorn", T_DIAL_0005412F_FULL0, "tg03_sabjorn", 45, T_QUST_0002154F_NNAM3, A_GIVE_POISON},
	{TG_SPIRITS, 60, "sabjorn", T_DIAL_000549CA_FULL0, "tg03_sabjorn_done", 90, T_QUST_0002154F_NNAM8},
	{TG_SPIRITS, 98, "maven", T_DIAL_0008139F_FULL0, "tg03_maven_done", 99, T_QUST_0001F326_NNAM7},
	{TG_SPIRITS, 99, "brynjolf", "Honningbrew's taken care of.", "tg03_brynjolf_done", QUEST_DONE, "", A_NEXT, TG_FOLLY, 10,
	 T_QUST_00021550_NNAM0},
	{TG_FOLLY, 10, "mercerfrey", T_DIAL_0002C2C4_FULL0, "tg04_mercer", 30, T_QUST_00021550_NNAM2},
	{TG_FOLLY, 30, "gulumei", T_DIAL_00018977_FULL0, "tg04_gulumei", 35, T_QUST_00021550_NNAM3},
	{TG_FOLLY, 37, "gulumei", T_DIAL_0004FEE9_FULL0, "tg04_gulumei_wine", 60, T_QUST_00021550_NNAM8, A_TAKE_WINE},
	{TG_FOLLY, 60, "mercerfrey", T_DIAL_0002CC32_FULL0, "tg04_mercer_done", QUEST_DONE, "", A_NEXT,
	 TG_SILENCE, 10, T_QUST_00021551_NNAM0},
	{TG_SILENCE, 10, "mercerfrey", "I'm here.", "tg05_mercer", 20, T_QUST_00021551_NNAM1, A_ESCORT_MERCER},
	{TG_SILENCE, 50, "karliah", T_DIAL_0001BB38_FULL0, "tg05_karliah", QUEST_DONE, "", A_NEXT, TG_ANSWERS, 10, T_QUST_00021552_NNAM0},
	{TG_ANSWERS, 10, "enthir", T_DIAL_00032D53_FULL0, "tg06_enthir", 20, T_QUST_00021552_NNAM1},
	{TG_ANSWERS, 20, "calcelmo", T_DIAL_00032D4B_FULL0, "tg06_calcelmo", 25, T_QUST_00021552_NNAM2},
	{TG_ANSWERS, 50, "enthir", T_DIAL_00089BA3_FULL0, "tg06_enthir_done", 70, T_QUST_00021551_NNAM3, A_TAKE_RUBBING},
	{TG_ANSWERS, 70, "karliah", T_DIAL_00032771_FULL0, "tg06_karliah", QUEST_DONE, "", A_NEXT, TG_PURSUIT, 10, T_QUST_00021553_NNAM0},
	{TG_PURSUIT, 10, "karliah", T_DIAL_000233C9_FULL0, "tg07_karliah", 30, T_QUST_00021553_NNAM2},
	{TG_PURSUIT, 30, "brynjolf", T_DIAL_00055FBE_FULL0, "tg07_brynjolf", 40, T_QUST_00021553_NNAM3},
	{TG_PURSUIT, 60, "brynjolf", T_DIAL_00037CCE_FULL0, "tg07_brynjolf_done", QUEST_DONE, "", A_TAKE_PLANS,
	 TG_TRINITY, 20, T_QUST_00057F99_NNAM1},
	{TG_TRINITY, 20, "karliah", T_DIAL_00058EB8_FULL0, "tg08a_karliah", 40, T_QUST_00057F99_NNAM3},
	{TG_TRINITY, 60, "karliah", T_DIAL_000587ED_FULL0, "tg08a_karliah_oath", 70, T_QUST_00021553_NNAM2},
	{TG_TRINITY, 70, "brynjolf", "What's the business?", "tg08a_brynjolf", QUEST_DONE, "", A_NEXT, TG_BLIND, 10, T_QUST_00021554_NNAM0},
	{TG_BLIND, 20, "karliah", T_DIAL_000233C9_FULL0, "tg08b_karliah", 30, T_QUST_00021554_NNAM2, A_ESCORT_IRK},
	{TG_BLIND, 60, "karliah", T_INFO_000658E6_NAM12, "tg08b_karliah_done", QUEST_DONE, "", A_NEXT, TG_DARKNESS, 10, T_QUST_00021555_NNAM0},
	{TG_DARKNESS, 20, "gallus", T_DIAL_0001A291_FULL0, "tg09_gallus_key", 30, T_QUST_00021555_NNAM3},
	{TG_DARKNESS, 60, "karliah", "The Key is back in its lock.", NULL, QUEST_DONE, "", A_KEY_DONE},
	{TG_LEADER, 10, "brynjolf", T_DIAL_000171DC_FULL0, "tgl_brynjolf", 20, T_QUST_000D7D69_NNAM1},
	{TG_LEADER, 50, "tonilia", T_DIAL_000AA770_FULL0, "tgl_tonilia", QUEST_DONE, "", A_LEADER_ARMOR},
};
#define TALK_COUNT ((int)(sizeof(TALKS) / sizeof(TALKS[0])))
static const Talk *pending; // (said: what follows once it's done being said)

static const char *objective(int q) {
	return objective_now[q][0] ? objective_now[q] : T_QUST_00021553_NNAM2;
}

void thieves_new_game(void) {
	memset(stage, 0, sizeof(stage));
	memset(objective_now, 0, sizeof(objective_now));
	step = flags = hives = 0, advanced = -1, brawling = -1, pending = NULL;
	esc.n = 0;
	qk_foes_clear(&fo);
}

void thieves_set(int q, int s) {
	if (q < 0 || q >= TG_COUNT)
		return;
	for (int k = 0; k < q; k++)
		if (k != TG_FAMILY || q > TG_LOUD)
			stage[k] = QUEST_DONE;
	step = 0, pending = NULL, qk_foes_clear(&fo);
	const char *obj = "";
	for (int t = 0; t < TALK_COUNT; t++)
		if (TALKS[t].q == q && TALKS[t].at == s)
			obj = TALKS[t].who;
	set_stage(q, s, obj);
}

int thieves_advanced(void) {
	return advanced;
}

static void follow_up(const Talk *t) { // (said: the stage after, what it gives)
	switch (t->action) {
	case A_BRAWL: { // (a debt: words first, then fists - Skyrim's)
		const int a = qk_actor(t->who);
		if (a >= 0)
			brawl_start(a, skills_state()->player_level), brawling = a;
		return;
	}
	case A_DEBTS:
		items_add(items_find_gold(), -3 * DEBT_GOLD); // (handed over: Brynjolf's)
		items_add(items_find_gold(), DEBT_GOLD);      // (and your cut)
		break;
	case A_TAKE_BILL:
		qk_give("tg02_bill", -1);
		break;
	case A_GIVE_POISON:
		qk_give("tg03_poison", 1);
		break;
	case A_TAKE_WINE:
		qk_give("tg04_wine", -1);
		break;
	case A_ESCORT_MERCER:
		qk_escort(&esc, "mercerfrey", NULL, NULL);
		break;
	case A_TAKE_RUBBING:
		qk_give("tg06_rubbing", -1);
		break;
	case A_TAKE_PLANS:
		qk_give("tg07_plans", -1);
		break;
	case A_ESCORT_IRK:
		qk_escort(&esc, "karliah", "brynjolf", NULL);
		break;
	case A_ARMOR:
		qk_give("tg02b_armor", 1);
		break;
	case A_KEY_DONE:
		qk_escort_end(&esc);
		break;
	case A_LEADER_ARMOR:
		qk_give("tgl_armor", 1);
		break;
	}
	if (t->next >= QUEST_DONE)
		done(t->q);
	else if (t->objective && t->next != stage[t->q])
		set_stage(t->q, t->next, t->objective);
	if (t->then_objective) {
		set_stage(t->then_q, t->then_stage, t->then_objective);
		if (t->then_q == TG_LOUD) // (Meet the Family: along with Loud and Clear)
			set_stage(TG_FAMILY, 10, T_QUST_00084ABD_NNAM0);
	}
	if (t->q == TG_DARKNESS && t->next >= QUEST_DONE)
		set_stage(TG_LEADER, 10, T_QUST_000D7D69_NNAM0);
}

int thieves_brawl_over(int result) {
	if (brawling < 0 || result == BRAWL_NONE)
		return 0;
	const char *who = actors_id(brawling);
	brawling = -1;
	actors_script(qk_actor(who), 0);
	if (result != BRAWL_WON) {
		ui_notice(result == BRAWL_LOST ? "They won't pay. Try again" : "Fists only: no weapons, no magic", 3.0f);
		return 1;
	}
	flags |= !strcmp(who, "keerava") ? F_KEERAVA : !strcmp(who, "bersi") ? F_BERSI : F_HAELGA;
	items_add(items_find_gold(), DEBT_GOLD); // (a line of theirs)
	char text[48];
	snprintf(text, sizeof(text), "%d Gold added", DEBT_GOLD);
	ui_notice(text, 3.0f);
	if ((flags & (F_KEERAVA | F_BERSI | F_HAELGA)) == (F_KEERAVA | F_BERSI | F_HAELGA))
		set_stage(TG_BUSINESS, 50, T_QUST_0001F326_NNAM7);
	return 1;
}

// --- Each quest's own ------------------------------------------------------------------------

static void the_quests(float dt, const float feet[3], int interior) {
	const int *s = stage;
	// A Chance Arrangement: Madesi's ring from his strongbox, onto Brand-Shei
	if (s[TG_CHANCE] == 10 && qk_at("tg00_strongbox", feet, interior, TAKE_M) && !qk_has("tg00_ring")) {
		qk_give("tg00_ring", 1);
		set_stage(TG_CHANCE, 20, T_QUST_00021556_NNAM2);
	}
	if (s[TG_CHANCE] == 20 && qk_has("tg00_ring")) {
		float at[3];
		const int b = qk_actor("brandshei");
		if (b >= 0 && actors_shown(b, at) && hypotf(at[0] - feet[0], at[2] - feet[2]) < PLANT_M) {
			qk_give("tg00_ring", -1);
			ui_notice("Madesi's ring planted on Brand-Shei", 3.0f);
			set_stage(TG_CHANCE, 30, T_QUST_00021553_NNAM2);
		}
	}
	// Taking Care of Business: Brynjolf in the Flagon
	if (s[TG_BUSINESS] == 10)
		qk_put("brynjolf", "tg_flagon_brynjolf", interior);
	// Loud and Clear: Goldenglow's hives (fire loosed at the apiary) and Aringoth's safe
	if (s[TG_LOUD] == 40) {
		hive_t -= dt;
		if (qk_at("tg02_hives", feet, interior, HIVES_M) && skills_take_used(SK_DESTRUCTION) && hives < 3 && hive_t <= 0.0f) {
			char text[48];
			snprintf(text, sizeof(text), "Bee hive burned (%d/3)", ++hives);
			ui_notice(text, 2.5f);
			hive_t = HIVE_EVERY_S;
		}
		if (qk_at("tg02_safe", feet, interior, TAKE_M) && !(flags & F_SAFE)) {
			qk_give("tg02_bill", 1);
			flags |= F_SAFE;
		}
		if (hives >= 3 && (flags & F_SAFE))
			set_stage(TG_LOUD, 60, T_QUST_0001F326_NNAM7);
	}
	if (s[TG_LOUD] == 10 && qk_in(CISTERN, interior)) {
		qk_put("brynjolf", "tg_cistern_brynjolf", interior);
		qk_put("mercerfrey", "tg_cistern_mercer", interior);
	}
	// Dampened Spirits: the nest, the vat, Sabjorn's chest
	if (s[TG_SPIRITS] == 45 && qk_in(HONNINGBREW_BASEMENT, interior) && qk_at("tg03_nest", feet, interior, TAKE_M * 1.5f))
		set_stage(TG_SPIRITS, 50, T_QUST_0002154F_NNAM4);
	if (s[TG_SPIRITS] == 50 && qk_in(HONNINGBREW_BOILERY, interior) && qk_at("tg03_vat", feet, interior, TAKE_M * 1.5f)) {
		qk_give("tg03_poison", -1);
		ui_notice("The vat poisoned", 2.5f);
		set_stage(TG_SPIRITS, 60, T_QUST_0002154F_NNAM5);
	}
	if (s[TG_SPIRITS] == 90 && qk_at("tg03_chest", feet, interior, TAKE_M)) {
		qk_give("tg03_letter", 1);
		set_stage(TG_SPIRITS, 98, T_QUST_0002154F_NNAM9); // (Skyrim's 100 and 110: under QUEST_DONE here)
	}
	// Scoundrel's Folly: the Firebrand Wine
	if (s[TG_FOLLY] == 35 && qk_at("tg04_wine", feet, interior, TAKE_M)) {
		qk_give("tg04_wine", 1);
		set_stage(TG_FOLLY, 37, T_QUST_00021550_NNAM4);
	}
	if (s[TG_FOLLY] == 10 && qk_in(CISTERN, interior))
		qk_put("mercerfrey", "tg_cistern_mercer", interior);
	// Speaking With Silence: Mercer outside Snow Veil; within, Karliah's arrow - waking outside with her
	if (s[TG_SILENCE] == 10 && interior < 0)
		qk_put("mercerfrey", "tg05_mercer", interior);
	if (s[TG_SILENCE] == 20 && qk_in(SNOW_VEIL, interior) && !step) {
		qk_escort_end(&esc);
		ui_notice("An arrow - and then darkness", 3.0f);
		qk_gone("mercerfrey");
		qk_move_to("tg05_wake");
		step = 130;
	} else if (step == 130 && !qk_moving() && interior < 0) {
		qk_put("karliah", "tg05_karliah", interior);
		qk_back("mercerfrey");
		set_stage(TG_SILENCE, 50, T_QUST_00021551_NNAM3);
		step = 0;
	}
	// Hard Answers: Calcelmo's museum, his stone
	if (s[TG_ANSWERS] == 25 && qk_in(MUSEUM, interior))
		set_stage(TG_ANSWERS, 40, T_QUST_00021552_NNAM5);
	if (s[TG_ANSWERS] == 40 && qk_at("tg06_stone", feet, interior, TAKE_M * 1.5f)) {
		qk_give("tg06_rubbing", 1);
		set_stage(TG_ANSWERS, 50, T_QUST_00021552_NNAM6);
	}
	// The Pursuit: Karliah in the Flagon; Mercer's plans in his house
	if (s[TG_PURSUIT] == 10)
		qk_put("karliah", "tg07_karliah", interior);
	if (s[TG_PURSUIT] == 40 && qk_in(MERCERS_HOUSE, interior) && qk_at("tg07_plans", feet, interior, TAKE_M)) {
		qk_give("tg07_plans", 1);
		set_stage(TG_PURSUIT, 60, T_QUST_00021553_NNAM2);
	}
	// Trinity Restored: Karliah at the stone; the armour; the glyph - the Oath
	if (s[TG_TRINITY] == 20 && interior < 0)
		qk_put("karliah", "tg08a_karliah", interior);
	if (s[TG_TRINITY] == 40 && qk_in(NIGHTINGALE_HALL, interior) && qk_at("tg08a_armor_stone", feet, interior, TAKE_M)) {
		static const char *const SET[4] = {"tg08_ng_cuirass", "tg08_ng_boots", "tg08_ng_gloves", "tg08_ng_hood"};
		for (int k = 0; k < 4; k++)
			qk_give(SET[k], 1);
		set_stage(TG_TRINITY, 50, T_QUST_00057F99_NNAM6);
	}
	if (s[TG_TRINITY] == 50 && qk_in(NIGHTINGALE_HALL, interior)) {
		qk_put("karliah", "tg08a_karliah_glyph", interior);
		qk_put("brynjolf", "tg08a_brynjolf_glyph", interior);
		if (qk_at("tg08a_glyph", feet, interior, GLYPH_M)) {
			ui_notice("The Oath to Nocturnal is made", 3.0f); // (Skyrim's: her voice from the shadows)
			set_stage(TG_TRINITY, 60, T_QUST_00021551_NNAM3);
		}
	}
	if (s[TG_TRINITY] == 60 || s[TG_TRINITY] == 70) {
		qk_put("karliah", "tg08a_karliah_glyph", interior);
		qk_put("brynjolf", "tg08a_brynjolf_glyph", interior);
	}
	// Blindsighted: Irkngthand - Karliah and Brynjolf; Mercer; the Key; out
	if (s[TG_BLIND] == 10 && interior >= 0 && (qk_in("irkngthand01", interior) || qk_in("irkngthand02", interior)))
		set_stage(TG_BLIND, 20, T_QUST_00021551_NNAM3);
	if (s[TG_BLIND] == 20) {
		qk_put("karliah", "tg08b_karliah", interior);
		qk_put("brynjolf", "tg08b_brynjolf", interior);
	}
	if (s[TG_BLIND] == 30 && qk_in(IRKNGTHAND_SANCTUARY, interior) && fo.at != interior) { // (Mercer at the statue's eyes)
		qk_gone("mercerfrey");
		qk_foes_clear(&fo);
		qk_foes_one(&fo, "tg08b_mercer", T_NPC_0001B07C_FULL0, T_NPC_0001B07C_FULL0, interior, 0);
		fo.at = interior;
		qk_play("tg08b_mercer");
		set_stage(TG_BLIND, 40, T_QUST_00021554_NNAM3);
	}
	if (s[TG_BLIND] >= 30)
		qk_gone("mercerfrey");
	if (s[TG_BLIND] == 40 && fo.n > 0 && qk_foes_left(&fo) == 0)
		set_stage(TG_BLIND, 45, T_QUST_00021554_NNAM4);
	if (s[TG_BLIND] == 45 && qk_at("tg08b_mercer", feet, interior, TAKE_M * 2.0f)) {
		qk_give("tg08_key", 1);
		set_stage(TG_BLIND, 50, T_QUST_00021554_NNAM5);
	}
	if (s[TG_BLIND] == 50 && (qk_in(BRONZE_WATER, interior) || interior < 0)) {
		qk_escort_end(&esc);
		set_stage(TG_BLIND, 60, T_QUST_00021551_NNAM3);
	}
	if (s[TG_BLIND] == 60 && !qk_put("karliah", "tg08b_karliah_end", interior) && interior < 0 && !step) {
		qk_beside("karliah", feet, 1.5f); // (out another way: she finds you)
		step = 210;
	}
	// Darkness Returns: the Sepulcher - Gallus, the Pilgrim's Path, the Ebonmere
	if (s[TG_DARKNESS] == 10 && qk_in(SEPULCHER, interior))
		set_stage(TG_DARKNESS, 20, T_QUST_00021555_NNAM1);
	if (s[TG_DARKNESS] == 20)
		qk_put("gallus", "tg09_gallus", interior);
	if (s[TG_DARKNESS] == 30 && qk_in(SEPULCHER_INNER, interior))
		set_stage(TG_DARKNESS, 40, T_QUST_00021555_NNAM4);
	if (s[TG_DARKNESS] == 40 && qk_at("tg09_ebonmere", feet, interior, TAKE_M * 1.5f)) {
		qk_give("tg08_key", -1);
		ui_notice("The Skeleton Key returned to the Ebonmere", 3.0f);
		qk_beside("karliah", feet, 1.5f);
		set_stage(TG_DARKNESS, 60, T_QUST_00021551_NNAM3);
	}
	// Under New Management: the Cistern's centre
	if (s[TG_LEADER] == 20 && qk_at("tg_cistern_centre", feet, interior, TAKE_M)) {
		ui_notice("Guild Master of the Thieves Guild", 3.0f);
		set_stage(TG_LEADER, 50, T_QUST_000D7D69_NNAM4);
	}
}

void thieves_update(float dt, const float feet[3], int interior, const char *reached) {
	(void)reached;
	if (pending && !qk_busy()) { // (said: what follows)
		const Talk *t = pending;
		pending = NULL;
		follow_up(t);
	}
	qk_escort_update(&esc, dt, feet, interior);
	if (qk_moving())
		return;
	the_quests(dt, feet, interior);
}

// --- Talking --------------------------------------------------------------------------------

static int topics_of(int actor, int *out) {
	int n = 0;
	const char *id = actors_id(actor);
	if (pending || step)
		return 0;
	for (int t = 0; t < TALK_COUNT && n < 8; t++) {
		const Talk *k = &TALKS[t];
		if (strcmp(k->who, id) || stage[k->q] != k->at)
			continue;
		if (k->q == TG_BUSINESS && k->at == 20) { // (each debt once)
			const int f = !strcmp(id, "keerava") ? F_KEERAVA : !strcmp(id, "bersi") ? F_BERSI : F_HAELGA;
			if (flags & f)
				continue;
		}
		if (k->q == TG_CHANCE && k->at == 0 && stage[TG_CHANCE]) // (begun already)
			continue;
		out[n++] = t;
	}
	return n;
}

int thieves_topics(int actor, const char **lines, int max) {
	int t[8];
	const int n = topics_of(actor, t);
	int m = 0;
	for (int i = 0; i < n && m < max; i++)
		lines[m++] = TALKS[t[i]].say;
	return m;
}

void thieves_say(int actor, int topic) {
	int t[8];
	const int n = topics_of(actor, t);
	if (topic < 0 || topic >= n)
		return;
	const Talk *k = &TALKS[t[topic]];
	if (k->play)
		qk_play(k->play);
	if (k->q == TG_CHANCE && k->at == 0)
		set_stage(TG_CHANCE, 5, T_QUST_00021553_NNAM2);
	pending = k;
}

// --- The compass, the journal, saving ------------------------------------------------------

int thieves_marker(float out[2]) {
	static const struct { int q, at; const char *mark, *place; } MARKS[] = {
		{TG_CHANCE, 10, "tg00_strongbox", T_DIAL_01002EFC_FULL0}, {TG_BUSINESS, 10, "tg_flagon_brynjolf", T_DIAL_01002EFC_FULL0},
		{TG_LOUD, 40, "tg02_hives", T_LCTN_00018E32_FULL0}, {TG_SPIRITS, 45, "tg03_nest", T_LCTN_00047259_FULL0},
		{TG_SPIRITS, 50, "tg03_vat", T_LCTN_00047259_FULL0}, {TG_SPIRITS, 90, "tg03_chest", T_LCTN_00047259_FULL0},
		{TG_FOLLY, 35, "tg04_wine", "Solitude"}, {TG_SILENCE, 10, "tg05_mercer", T_LCTN_000192AF_FULL0},
		{TG_SILENCE, 20, "tg05_door", T_LCTN_000192AF_FULL0}, {TG_ANSWERS, 25, "tg06_stone", T_DIAL_01002EF9_FULL0},
		{TG_ANSWERS, 40, "tg06_stone", T_DIAL_01002EF9_FULL0}, {TG_PURSUIT, 40, "tg07_plans", T_DIAL_01002EFC_FULL0},
		{TG_TRINITY, 20, "tg08a_karliah", T_LCTN_00039AA9_FULL0}, {TG_TRINITY, 40, "tg08a_armor_stone", T_LCTN_00039AA9_FULL0},
		{TG_TRINITY, 50, "tg08a_glyph", T_LCTN_00039AA9_FULL0}, {TG_BLIND, 10, "tg08b_start", T_LCTN_0001919D_FULL0},
		{TG_BLIND, 30, "tg08b_mercer", T_LCTN_0001919D_FULL0}, {TG_BLIND, 40, "tg08b_mercer", T_LCTN_0001919D_FULL0},
		{TG_BLIND, 45, "tg08b_mercer", T_LCTN_0001919D_FULL0}, {TG_DARKNESS, 10, "tg09_gallus", T_LCTN_0001914B_FULL0},
		{TG_DARKNESS, 30, "tg09_ebonmere", T_LCTN_0001914B_FULL0}, {TG_DARKNESS, 40, "tg09_ebonmere", T_LCTN_0001914B_FULL0},
		{TG_LEADER, 20, "tg_cistern_centre", T_DIAL_01002EFC_FULL0}};
	for (int q = 0; q < TG_COUNT; q++) {
		const int st = stage[q];
		if (!st || st >= QUEST_DONE)
			continue;
		for (unsigned k = 0; k < sizeof(MARKS) / sizeof(MARKS[0]); k++)
			if (MARKS[k].q == q && MARKS[k].at == st)
				return qk_place_marker(MARKS[k].mark, MARKS[k].place, out);
		for (int t = 0; t < TALK_COUNT; t++) // (else whoever's to be spoken to)
			if (TALKS[t].q == q && TALKS[t].at == st && qk_who_marker(TALKS[t].who, out))
				return 1;
	}
	return 0;
}

int thieves_journal(const char **names, const char **objectives, int *done_out, int max) {
	int n = 0;
	for (int q = 0; q < TG_COUNT && n < max; q++) {
		if (!stage[q])
			continue;
		names[n] = NAMES[q], objectives[n] = stage[q] >= QUEST_DONE ? T_GMST_00017026_DATA0 : objective(q);
		done_out[n++] = stage[q] >= QUEST_DONE;
	}
	return n;
}

void thieves_save(int out[24]) {
	memset(out, 0, 24 * sizeof(int));
	for (int q = 0; q < TG_COUNT; q++)
		out[q] = stage[q];
	out[TG_COUNT] = flags, out[TG_COUNT + 1] = hives;
}

void thieves_load(const int saved[24]) {
	thieves_new_game();
	for (int q = 0; q < TG_COUNT; q++) {
		stage[q] = saved[q];
		if (stage[q] > 0 && stage[q] < QUEST_DONE) // (its objective again: the talk or the deed at that stage)
			for (int t = 0; t < TALK_COUNT; t++)
				if (TALKS[t].q == q && TALKS[t].next == stage[q] && TALKS[t].objective)
					snprintf(objective_now[q], sizeof(objective_now[q]), "%s", TALKS[t].objective);
	}
	flags = saved[TG_COUNT], hives = saved[TG_COUNT + 1];
	if (stage[TG_SILENCE] == 20)
		qk_escort(&esc, "mercerfrey", NULL, NULL);
	if (stage[TG_BLIND] >= 30 && stage[TG_BLIND] < 50)
		qk_escort(&esc, "karliah", "brynjolf", NULL);
	if (stage[TG_BLIND] == 40)
		stage[TG_BLIND] = 30; // (Mercer again)
}
