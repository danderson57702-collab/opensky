#include "companions.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actors.h"
#include "beast.h"
#include "brawl.h"
#include "creatures.h"
#include "dtree.h"
#include "gamelog.h"
#include "interior.h"
#include "items.h"
#include "questkit.h"
#include "quests.h"
#include "sfx.h"
#include "skills.h"
#include "ui.h"
#include "gametext.h"

#define JORRVASKR "whiterunjorrvaskr"
#define LIVING_QUARTERS "whiterunjorrvaskrbasement"
#define UNDERFORGE "whiterununderforge"
#define DUSTMANS_CAIRN "dustmanscairn01"
#define GALLOWS_ROCK "gallowsrock01"
#define GLENMORIL "glenmorilcoven01"
#define DRIFTSHADE "driftshadesanctuary01"
#define DRIFTSHADE_LOWER "driftshadesanctuary02"
#define YSGRAMORS_TOMB "ysgramorstomb01"

#define TRAINING_NEAR_M 4.0f    // Vilkas's spar begins this near his mark in the yard
#define QUARTERS_NEAR_M 4.0f    // Farkas's INFO 000A3E9F this near the whelps' beds
#define INDUCTION_NEAR_M 12.0f  // the Circle gathers once you're this near the Skyforge's yard
#define DOOR_NEAR_M 2.6f        // the Underforge's rock door opened this near (Skyrim's: only to the Circle)
#define NIGHT_FROM_H 20.0f      // ("Meet with Skjor at night")
#define NIGHT_TO_H 6.0f
#define RAMPAGE_S 24.0f         // the ritual's first change: this long, then dark - and waking with Aela
#define AMBUSH_WOLF_S 90.0f     // Farkas as the beast at most this long
#define AMBUSH_NEAR_M 30.0f     // the Silver Hand's ambush: you this near its mark, with the fragment
#define FUNERAL_NEAR_M 14.0f
#define TABLE_NEAR_M 2.2f       // Kodlak's end table: his fragment and journal on it
#define BOX_NEAR_M 3.5f         // (Driftshade's box)
#define STATUE_NEAR_M 3.5f      // Wuuthrad returned to Ysgramor's statue this near
#define CHAMBER_NEAR_M 8.0f
#define FLAME_NEAR_M 3.5f       // the witch's head into the flame this near
#define ATTACK_NEAR_M 60.0f     // Jorrvaskr's attackers once you're this near (Blood's Honor's return)

static const char *const NAMES[CO_COUNT] = {T_QUST_0004B2D9_FULL0, T_QUST_0006E803_FULL0, "Brotherhood", T_QUST_0001CEF4_FULL0,
                                              T_QUST_0001CEF5_FULL0, T_QUST_0006E805_FULL0, T_QUST_0001CEF6_FULL0};
static const char *const CIRCLE[4] = {"aelathehuntress", "vilkas", "farkas", "skjor"};

static int stage[CO_COUNT];
static int step;        // (where in a stage's scenes: the conversation said, then what follows)
static int flags;       // (saved: what's been done along the way)
enum { F_WEAPON = 1, F_TOLD_SKJOR = 2, F_GAVE_HEAD = 4, F_CLEARED_GALLOWS = 8, F_SPAR_BROKEN = 16, F_AMBUSHED = 32 };
static int advanced = -1;
static int cairn_spawned; // (this session: Dustman's Cairn's Silver Hand come)
static float timer;

// --- People and foes (questkit.c's) ----------------------------------------------------------

static QkEscort esc;
static QkFoes fo;
#define foes fo.spawn
#define foe_n fo.n
#define foes_at fo.at
#define with_n esc.n
#define with_place esc.place

static void escort(const char *a, const char *b, const char *c) {
	qk_escort(&esc, a, b, c);
}
static void escort_end(void) {
	qk_escort_end(&esc);
}
static void escort_update(float dt, const float feet[3], int interior) {
	qk_escort_update(&esc, dt, feet, interior);
}
#define put qk_put
#define gone qk_gone
#define back qk_back
#define in qk_in
#define near_mark qk_at
static void spawn_foes(const char *prefix, int first, int last, const char *kind, const char *name, int interior) {
	qk_foes_spawn(&fo, prefix, first, last, kind, name, interior);
}
static int spawn_one(const char *key, const char *kind, const char *name, int interior, int ghost) {
	return qk_foes_one(&fo, key, kind, name, interior, ghost);
}
static int foes_left(void) {
	return qk_foes_left(&fo);
}
static void foes_clear(void) {
	qk_foes_clear(&fo);
}

// --- Stages ---------------------------------------------------------------------------------

static void set_stage(int q, int s, const char *objective) {
	const int was = stage[q];
	stage[q] = s;
	qk_stage_notice(NAMES[q], was, s, objective);
	advanced = qk_advance_count();
}

static const char *objective(int q) {
	const int s = stage[q];
	switch (q) {
	case CO_TAKE_UP_ARMS:
		return s == 10 ? T_QUST_0001CEF5_NNAM0 : s == 20 ? T_QUST_0004B2D9_NNAM1 : s == 30 ? T_QUST_0004B2D9_NNAM2
		     : s == 40 ? T_QUST_0004B2D9_NNAM3 : T_QUST_0004B2D9_NNAM4;
	case CO_PROVING_HONOR:
		return s == 10 ? T_QUST_0006E803_NNAM0 : s == 20 ? "Speak to Farkas" : s == 30 ? T_QUST_0006E803_NNAM2 : T_QUST_0006E803_NNAM3;
	case CO_BROTHERHOOD:
		return "Follow Farkas";
	case CO_SILVER_HAND:
		return s == 5 ? T_QUST_0001CEF4_NNAM0 : s == 10 ? T_QUST_0001CEF4_NNAM1 : s == 15 ? T_QUST_0001CEF4_NNAM2
		     : s == 20 ? T_QUST_0001CEF4_NNAM3 : s == 25 ? T_QUST_0001CEF4_NNAM4 : s == 30 ? T_QUST_0001CEF4_NNAM5
		     : T_QUST_0001CEF4_NNAM4;
	case CO_BLOODS_HONOR:
		return s == 10 ? T_QUST_0001CEF5_NNAM0 : s == 20 ? T_QUST_0001CEF5_NNAM1 : T_QUST_0001CEF5_NNAM3;
	case CO_PURITY:
		return s == 10 ? T_QUST_0006E805_NNAM0 : T_QUST_0006E803_NNAM3;
	default:
		return s == 10 ? T_QUST_0001CEF6_NNAM0 : s == 15 ? T_QUST_0001CEF6_NNAM1
		     : s == 17 ? T_QUST_0001CEF6_NNAM2 : s == 18 ? T_QUST_0001CEF6_NNAM3 : s == 20 ? T_QUST_0001CEF6_NNAM4
		     : s == 30 ? T_QUST_0001CEF6_NNAM5 : s == 35 ? T_QUST_0001CEF6_NNAM6
		     : s == 40 ? T_QUST_0001CEF6_NNAM7 : s == 50 ? T_QUST_0001CEF6_NNAM8 : s == 60 ? T_QUST_0001CEF6_NNAM9
		     : s == 65 ? T_QUST_0001CEF6_NNAM10 : T_QUST_0001CEF6_NNAM8;
	}
}

void companions_new_game(void) {
	beast_clear(); // (the blood of the beast: the Circle's gift, to come)
	memset(stage, 0, sizeof(stage));
	step = flags = 0, timer = 0.0f, with_n = 0, advanced = -1, cairn_spawned = 0;
	foes_clear();
}

int companions_stage(int q) {
	return q >= 0 && q < CO_COUNT ? stage[q] : 0;
}

void companions_set(int q, int s) {
	if (q < 0 || q >= CO_COUNT)
		return;
	for (int k = 0; k < q; k++) // (testing: the ones before it done)
		if (stage[k] < QUEST_DONE)
			stage[k] = QUEST_DONE;
	step = 0, foes_clear();
	if (q > CO_SILVER_HAND || (q == CO_SILVER_HAND && s >= 30))
		beast_give();
	set_stage(q, s, objective((stage[q] = s, q)));
}

int companions_advanced(void) {
	return advanced;
}

// --- Each quest -----------------------------------------------------------------------------

// Take Up Arms (C00): DIAL 000A3E56 - Kodlak and Vilkas's argument over you,
// INFO 000A3E73; Vilkas's spar in the yard; his sword to Eorlund at the Skyforge, and
// Eorlund's favour (Aela's shield); Aela, and Farkas called to show you to your bed
static void take_up_arms(const float feet[3], int interior) {
	const int st = stage[CO_TAKE_UP_ARMS];
	if (st == 10 && in(LIVING_QUARTERS, interior) && !step) { // (Kodlak and Vilkas in his quarters, as Skyrim's)
		put("kodlakwhitemane", "c00_kodlak_spot", interior);
		put("vilkas", "c00_vilkas_spot", interior);
	}
	if (step == 1 && !qk_busy()) // (INFO 000A3E7A) then Vilkas's INFO 000A3E74
		qk_play("c00_argue_player"), step = 2;
	else if (step == 2 && !qk_busy())
		qk_play("c00_kodlak_back"), step = 3; // (INFO 000A3E73)
	else if (step == 3 && !qk_busy()) {
		set_stage(CO_TAKE_UP_ARMS, 20, T_QUST_0004B2D9_NNAM1);
		back("kodlakwhitemane");
		step = 0;
	}
	if (st == 20) {
		if (step != 11)
			put("vilkas", "c00_training", interior); // (out in the yard: INFO 000A3E98)
		if (step == 10 && !qk_busy()) { // (INFO 000A3E99)
			const int v = qk_actor("vilkas");
			if (v >= 0)
				brawl_start_spar(v, skills_state()->player_level + 2);
			step = 11;
		}
	}
	if (step == 20 && !qk_busy()) { // (INFO 00074768)
		qk_give("c00_vilkas_sword", 1);
		set_stage(CO_TAKE_UP_ARMS, 30, T_QUST_0004B2D9_NNAM2);
		back("vilkas");
		step = 0;
	}
	if (step == 30 && !qk_busy()) { // (Eorlund: the sword taken, Aela's shield given)
		qk_give("c00_vilkas_sword", -1);
		qk_give("c00_aela_shield", 1);
		set_stage(CO_TAKE_UP_ARMS, 40, T_QUST_0004B2D9_NNAM3);
		step = 0;
	}
	if (step == 40 && !qk_busy()) // (Aela's shield: then INFO 000CC83E)
		qk_play("c00_aela_vilkas"), step = 41;
	else if (step == 41 && !qk_busy()) // ("Farkas!" INFO 000A3E69 INFO 000A3E82)
		qk_play("c00_farkas_call"), step = 42;
	else if (step == 42 && !qk_busy()) {
		qk_give("c00_aela_shield", -1);
		set_stage(CO_TAKE_UP_ARMS, 50, T_QUST_0004B2D9_NNAM4);
		escort("farkas", NULL, NULL);
		step = 43;
	}
	if (st == 50 && in(LIVING_QUARTERS, interior)) { // (Farkas leads you to the whelps' beds)
		const QPlace *q = qk_place("c00_farkas_end");
		const int f = qk_actor("farkas");
		if (step == 43 && q && f >= 0) {
			escort_end();
			actors_script(f, 1);
			actors_script_walk(f, q->pos, 0);
			qk_play("c00_farkas1"); // (INFO 000A3E9B)
			step = 44;
		}
		if (step == 44 && near_mark("c00_farkas_end", feet, interior, QUARTERS_NEAR_M) && !qk_busy())
			qk_play("c00_farkas4"), step = 45; // (INFO 000A3E9F)
	}
	if (step == 45 && !qk_busy()) {
		set_stage(CO_TAKE_UP_ARMS, QUEST_DONE, "");
		back("farkas");
		step = 0;
	}
}

// Proving Honor (C01): Skjor's errand - a fragment of Wuuthrad in Dustman's Cairn - with Farkas as
// your Shield-Brother; the Silver Hand's ambush, Farkas as the beast; back to Skjor. Then the
// Brotherhood (C02): Farkas brings you to the Circle at the Skyforge's yard, Kodlak's induction
#define AMBUSHERS 5 // (Skyrim's AmbushSH1..5: c01_sh1..5; the rest about the Cairn)
static int farkas_wolf = -1;
static float wolf_t;

static void proving_honor(float dt, const float feet[3], int interior) {
	const int st = stage[CO_PROVING_HONOR];
	if (step == 51 && !qk_busy()) {
		set_stage(CO_PROVING_HONOR, 20, "Speak to Farkas");
		step = 0;
	} else if (step == 52 && !qk_busy()) { // (INFO 00071DE8)
		set_stage(CO_PROVING_HONOR, 30, T_QUST_0006E803_NNAM2);
		escort("farkas", NULL, NULL);
		step = 0;
	}
	if ((st == 30 || st == 40) && in(DUSTMANS_CAIRN, interior) && !cairn_spawned)
		spawn_foes("c01_sh", AMBUSHERS + 1, 31, T_NPC_0002AB87_FULL0, NULL, interior), cairn_spawned = 1; // (Skyrim's Silver Hand about the Cairn, from now)
	if (st == 30 && qk_has("c01_fragment")) {
		set_stage(CO_PROVING_HONOR, 40, T_QUST_0006E803_NNAM3);
		step = 0;
	}
	if (st == 40 && step == 0 && !(flags & F_AMBUSHED) && in(DUSTMANS_CAIRN, interior) &&
	    near_mark("c01_ambush", feet, interior, AMBUSH_NEAR_M)) {
		flags |= F_AMBUSHED;
		// (the ambush: INFO 000E9532 - and Farkas changes)
		foes_clear();
		spawn_foes("c01_sh", 1, AMBUSHERS, T_NPC_0002AB87_FULL0, NULL, interior);
		qk_play("c01_ambush");
		const int f = qk_actor("farkas");
		float at[3];
		if (f >= 0 && actors_where(f, at)) {
			escort_end();
			gone("farkas");
			farkas_wolf = creatures_summon_escort("werewolf", at, AMBUSH_WOLF_S);
			sfx_play("werewolf_change", 1.0f, 0.0f);
		}
		wolf_t = AMBUSH_WOLF_S, step = 53;
	}
	if (step == 53) {
		wolf_t -= dt;
		if ((foes_left() == 0 || wolf_t <= 0.0f) && !qk_busy()) { // (the hunters dead: Farkas himself again)
			creatures_dismiss_allies("werewolf");
			farkas_wolf = -1;
			escort("farkas", NULL, NULL);
			with_place = -999;
			step = 54;
		}
	}
	if (step == 54 && !qk_busy() && qk_actor("farkas") >= 0) { // (INFO 00081DE1)
		float at[3];
		if (actors_where(qk_actor("farkas"), at) && at[1] > -400.0f)
			qk_play("c01_farkas_after"), step = 55;
	}
	if (step == 55 && !qk_busy())
		step = 0; // (said: Skjor's to hear of it)
	if (step == 56 && !qk_busy()) { // (Skjor: INFO 000681EF)
		set_stage(CO_PROVING_HONOR, QUEST_DONE, "");
		escort_end();
		set_stage(CO_BROTHERHOOD, 10, "Follow Farkas");
		step = 0;
	}
}

static void brotherhood(const float feet[3], int interior) {
	const int st = stage[CO_BROTHERHOOD];
	if (st != 10)
		return;
	if (step == 70 && !qk_busy()) { // (INFO 000C07ED)
		const QPlace *q = qk_place("c02_farkas");
		const int f = qk_actor("farkas");
		if (q && f >= 0 && qk_place_here(q, interior)) {
			actors_script(f, 1);
			actors_script_walk(f, q->pos, 0);
		}
		step = 71;
	}
	if (step == 71 && near_mark("c02_kodlak", feet, interior, INDUCTION_NEAR_M)) { // (the Circle at the yard: the induction)
		static const char *const AT[5][2] = {{"kodlakwhitemane", "c02_kodlak"}, {"vilkas", "c02_vilkas"}, {"farkas", "c02_farkas"},
		                                     {"aelathehuntress", "c02_aela"}, {"skjor", "c02_skjor"}};
		for (int k = 0; k < 5; k++)
			put(AT[k][0], AT[k][1], interior);
		qk_play("c02_induction"); // (INFO 000734BA)
		step = 72;
	} else if (step == 72 && !qk_busy()) {
		qk_play("c02_kodlak"), step = 73; // (INFO 000734B5)
	} else if (step == 73 && !qk_busy()) {
		for (int k = 0; k < 4; k++)
			back(CIRCLE[k]);
		back("kodlakwhitemane");
		set_stage(CO_BROTHERHOOD, QUEST_DONE, "");
		step = 0;
	}
}

static int krev = -1; // (Krev the Skinner's spawn)

// The Silver Hand (C03): Aela and Skjor's INFO 0002E2DB; Skjor at the Underforge's door at
// night; the blood ritual - Beast Form, a rampage, then dark; waking with Aela outside; Gallows
// Rock's werewolf hunters and Krev the Skinner; Skjor fallen
static void silver_hand(float dt, const float feet[3], int interior, float hours, Vitals *v) {
	const int st = stage[CO_SILVER_HAND];
	const int night = hours >= NIGHT_FROM_H || hours < NIGHT_TO_H;
	if (step == 81 && !qk_busy()) {
		set_stage(CO_SILVER_HAND, 5, T_QUST_0001CEF4_NNAM0);
		step = 0;
	} else if (step == 82 && !qk_busy()) { // (Skjor and Aela: INFO 00032885)
		set_stage(CO_SILVER_HAND, 10, T_QUST_0001CEF4_NNAM1);
		step = 0;
	}
	if (st == 10 && night && interior < 0)
		put("skjor", "c03_skjor_door", interior); // (by the Underforge's door, at night)
	if (step == 83 && !qk_busy()) { // (INFO 000AE906)
		set_stage(CO_SILVER_HAND, 15, T_QUST_0001CEF4_NNAM2);
		step = 0;
	}
	if ((st == 15 || stage[CO_GLORY] == 20) && interior < 0 && near_mark("underforge_door", feet, interior, DOOR_NEAR_M) &&
	    !qk_moving()) { // (the Skyforge's rock: open to the Circle)
		qk_move_to("c03_aela_inside");
		game_log("companions: into the Underforge");
	}
	if (st == 15 && in(UNDERFORGE, interior)) {
		put("skjor", "c03_skjor_inside", interior);
		put("aelathehuntress", "c03_aela_inside", interior);
		set_stage(CO_SILVER_HAND, 20, T_QUST_0001CEF4_NNAM3);
	}
	if (st == 20 && in(UNDERFORGE, interior)) {
		put("skjor", "c03_skjor_inside", interior);
		put("aelathehuntress", "c03_aela_inside", interior);
	}
	if (step == 84 && !qk_busy()) { // (INFO 0001D642 - the font's blood drunk: the beast)
		sfx_play("c03_cut", 1.0f, 0.0f);
		beast_give();
		beast_change(v, 1);
		set_stage(CO_SILVER_HAND, 25, T_QUST_0001CEF4_NNAM4);
		timer = RAMPAGE_S, step = 85;
	}
	if (step == 85 && (timer -= dt) <= 0.0f) { // (the rampage's end: dark, then waking outside with Aela)
		beast_revert(v);
		back("skjor");
		qk_move_to("c03_wake");
		step = 86;
	}
	if (step == 86 && !qk_moving() && interior < 0) {
		if (put("aelathehuntress", "c03_wake_aela", interior))
			step = 87;
	}
	if (step == 87 && !qk_busy() && near_mark("c03_wake_aela", feet, interior, 6.0f)) {
		qk_play("c03_aela_awake"), step = 88; // (INFO 0003D28D)
	} else if (step == 88 && !qk_busy()) {
		set_stage(CO_SILVER_HAND, 30, T_QUST_0001CEF4_NNAM5);
		escort("aelathehuntress", NULL, NULL);
		gone("skjor"); // (gone to Gallows Rock ahead of you: Skyrim's - he falls there)
		step = 0;
	}
	if (st == 30 && in(GALLOWS_ROCK, interior) && foes_at != interior) {
		foes_clear();
		spawn_foes("c03_sh", 1, 31, T_NPC_0002AB87_FULL0, NULL, interior);
		krev = spawn_one("c03_krev", T_NPC_0002AB87_FULL0, T_MESG_000B3E07_DESC0, interior, 0) >= 0 ? foes[foe_n - 1] : -1;
		game_log("companions: Gallows Rock - %d hunters", foe_n);
	}
	if (st == 30 && krev >= 0 && creatures_spawn_alive(krev) == 0) { // (Krev the Skinner dead: the hunters' end, as Skyrim's)
		flags |= F_CLEARED_GALLOWS;
		set_stage(CO_SILVER_HAND, 31, T_QUST_0001CEF4_NNAM4);
	}
	if (step == 89 && !qk_busy()) { // (INFO 00032886)
		escort_end();
		set_stage(CO_SILVER_HAND, QUEST_DONE, "");
		ui_notice("Beast Form: SELECT, once readied in the Magic menu", 5.0f);
		step = 0;
	}
	if (st >= 30) // (Skjor fallen at Gallows Rock)
		gone("skjor");
}

// Blood's Honor (C04): Kodlak on the beastblood and the Glenmoril witches; a witch's head; back to
// Jorrvaskr - the Silver Hand have been, Kodlak is dead
static int attack_seen; // (this session: Jorrvaskr's attackers come)

static void bloods_honor(const float feet[3], int interior) {
	const int st = stage[CO_BLOODS_HONOR];
	if (step == 101 && !qk_busy()) { // (INFO 000582ED)
		set_stage(CO_BLOODS_HONOR, 20, T_QUST_0001CEF5_NNAM1);
		step = 0;
	}
	if (st == 20 && in(GLENMORIL, interior) && foes_at != interior) {
		foes_clear();
		for (int n = 1; n <= 5; n++) {
			char key[16];
			snprintf(key, sizeof(key), "c04_witch%d", n);
			spawn_one(key, "hagraven", T_NPC_000A350A_FULL0, interior, 0);
		}
		foes_at = interior;
	}
	if (st == 20 && foes_at >= 0 && foe_n > 0 && foes_left() < foe_n && !(flags & F_GAVE_HEAD)) {
		qk_give("c04_witch_head", 1); // (a witch fallen: her head taken)
		flags |= F_GAVE_HEAD;
	}
	if (st == 20 && qk_has("c04_witch_head"))
		set_stage(CO_BLOODS_HONOR, 30, T_QUST_0001CEF5_NNAM3);
	if (st == 30 && interior < 0 && near_mark("c04_attacked1", feet, interior, ATTACK_NEAR_M) && !attack_seen) {
		// (Jorrvaskr: the Silver Hand's attack - two of them still on the steps, the crowd looking on)
		foes_clear();
		spawn_one("c04_attacked1", T_NPC_0002AB87_FULL0, NULL, interior, 0);
		spawn_one("c04_attacked2", T_NPC_0002AB87_FULL0, NULL, interior, 0);
		qk_play("c04_gawkers");
		attack_seen = 1;
	}
	if (st == 30) {
		gone("kodlakwhitemane");
		if (in(JORRVASKR, interior))
			put("vilkas", "jorrvaskr_centre", interior);
	}
	if (step == 103 && !qk_busy()) { // (Vilkas: INFO 000582E3)
		set_stage(CO_BLOODS_HONOR, QUEST_DONE, "");
		set_stage(CO_PURITY, 10, T_QUST_0006E805_NNAM0);
		escort("vilkas", NULL, NULL);
		step = 0;
	}
}

// Purity of Revenge (C05): Driftshade Refuge with Vilkas - the Silver Hand's lair, the fragments taken
// from Jorrvaskr; back to Jorrvaskr
static void purity(const float feet[3], int interior) {
	const int st = stage[CO_PURITY];
	if (st == 10 && (in(DRIFTSHADE, interior) || in(DRIFTSHADE_LOWER, interior)) && foes_at != interior) {
		foes_clear();
		spawn_foes(in(DRIFTSHADE, interior) ? "c05_sh" : "c05_shb", 1, 31, T_NPC_0002AB87_FULL0, NULL, interior);
	}
	if (st == 10 && qk_has("c05_fragments")) {
		set_stage(CO_PURITY, 20, T_QUST_0006E803_NNAM3);
	}
	if (step == 121 && !qk_busy()) { // (INFO 000582DF)
		escort_end();
		set_stage(CO_PURITY, QUEST_DONE, "");
		set_stage(CO_GLORY, 10, T_QUST_0001CEF6_NNAM0);
		step = 0;
	}
	(void)feet;
}

void companions_container_opened(int interior, const float feet[3]) {
	if (stage[CO_PURITY] == 10 && in(DRIFTSHADE, interior) && near_mark("c05_fragments", feet, interior, BOX_NEAR_M) &&
	    !qk_has("c05_fragments"))
		qk_give("c05_fragments", 1);
}

// Glory of the Dead (C06): Kodlak's funeral at the Skyforge; the fragments to Eorlund, Kodlak's own
// from his end table; the Circle in the Underforge; Ysgramor's Tomb - Wuuthrad to his statue, the
// burial chamber, Kodlak's spirit, the witch's head in the flame, his wolf spirit slain
static int wolf_spirit = -1;

static void glory(const float feet[3], int interior) {
	const int st = stage[CO_GLORY];
	if (st == 10 && interior < 0 && near_mark("c06_funeral", feet, interior, FUNERAL_NEAR_M) && step == 0) {
		static const char *const AT[4][2] = {{"eorlundgraymane", "c06_eorlund_funeral"}, {"farkas", "c06_farkas_funeral"},
		                                     {"vilkas", "c06_vilkas_funeral"}, {"aelathehuntress", "c06_aela_funeral"}};
		for (int k = 0; k < 4; k++)
			put(AT[k][0], AT[k][1], interior);
		qk_play("c06_funeral"); // (INFO 000582E1 INFO 000582D3)
		step = 131;
	} else if (step == 131 && !qk_busy()) {
		for (int k = 0; k < 3; k++)
			back(CIRCLE[k]);
		set_stage(CO_GLORY, 15, T_QUST_0001CEF6_NNAM1);
		step = 0;
	}
	if (step == 132 && !qk_busy()) { // (INFO 000E308A)
		qk_give("c05_fragments", -1);
		set_stage(CO_GLORY, 17, T_QUST_0001CEF6_NNAM2);
		step = 0;
	}
	if (st == 17 && in(LIVING_QUARTERS, interior) && near_mark("c06_end_table", feet, interior, TABLE_NEAR_M)) {
		qk_give("c06_kodlak_fragment", 1);
		qk_give("c06_journal", 1);
		set_stage(CO_GLORY, 18, T_QUST_0001CEF6_NNAM3);
	}
	if (step == 133 && !qk_busy()) { // (INFO 000E4A2C)
		qk_give("c06_kodlak_fragment", -1);
		set_stage(CO_GLORY, 20, T_QUST_0001CEF6_NNAM4);
		step = 0;
	}
	if (st == 20 && in(UNDERFORGE, interior) && step == 0) {
		put("vilkas", "c06_vilkas_wait", interior);
		put("aelathehuntress", "c06_aela_wait", interior);
		put("farkas", "c06_farkas_wait", interior);
		qk_play("c06_underforge"); // (Vilkas and Aela's argument; Eorlund with Wuuthrad made whole)
		step = 134;
	} else if (step == 134 && !qk_busy()) {
		qk_give("c06_wuuthrad", 1);
		set_stage(CO_GLORY, 30, T_QUST_0001CEF6_NNAM5);
		escort("vilkas", "aelathehuntress", "farkas");
		step = 0;
	}
	if (st == 30 && in(YSGRAMORS_TOMB, interior)) {
		escort_end(); // (Vilkas and Aela wait by the statue; Farkas comes on)
		put("vilkas", "c06_vilkas_tomb", interior);
		put("aelathehuntress", "c06_aela_tomb", interior);
		escort("farkas", NULL, NULL);
		set_stage(CO_GLORY, 35, T_QUST_0001CEF6_NNAM6);
	}
	if (st == 35 && in(YSGRAMORS_TOMB, interior) && near_mark("c06_statue", feet, interior, STATUE_NEAR_M) && qk_has("c06_wuuthrad")) {
		qk_give("c06_wuuthrad", -1);
		ui_notice("Wuuthrad returned to Ysgramor", 3.0f);
		set_stage(CO_GLORY, 40, T_QUST_0001CEF6_NNAM7);
	}
	if (st == 40 && in(YSGRAMORS_TOMB, interior) && near_mark("c06_chamber", feet, interior, CHAMBER_NEAR_M))
		set_stage(CO_GLORY, 50, T_QUST_0001CEF6_NNAM8);
	if ((st == 50 || st == 60 || st == 65 || st == 70) && in(YSGRAMORS_TOMB, interior))
		put("kodlakwhitemane", "c06_kodlak", interior); // (his spirit by the flame)
	if (step == 135 && !qk_busy()) { // (a line of theirs)
		set_stage(CO_GLORY, 60, T_QUST_0001CEF6_NNAM9);
		step = 0;
	}
	if (st == 60 && near_mark("c06_flame", feet, interior, FLAME_NEAR_M) && qk_has("c04_witch_head")) {
		qk_give("c04_witch_head", -1);
		foes_clear();
		wolf_spirit = spawn_one("c06_flame", "werewolf", T_NPC_000F608C_FULL0, interior, 1);
		sfx_play("werewolf_howl", 1.0f, 0.0f);
		set_stage(CO_GLORY, 65, T_QUST_0001CEF6_NNAM10);
	}
	if (st == 65 && (foe_n == 0 || foes_left() == 0))
		set_stage(CO_GLORY, 70, T_QUST_0001CEF6_NNAM8);
	if (step == 136 && !qk_busy()) { // (INFO 000582D7 - he goes to Sovngarde)
		escort_end();
		back("vilkas"), back("aelathehuntress");
		qk_give("c06_wuuthrad", 1);
		qk_give("c06_shield", 1);
		set_stage(CO_GLORY, QUEST_DONE, "");
		step = 0;
	}
	if (st >= QUEST_DONE)
		gone("kodlakwhitemane");
}

void companions_update(float dt, const float feet[3], int interior, float hours, Vitals *v, const char *reached) {
	if (reached) { // (a turn of a conversation reached: its quest moves on)
		if (!strcmp(reached, "C01SkjorSendoff"))
			step = 51;
		else if (!strcmp(reached, "C01FollowMeTopic"))
			step = 52;
		else if (!strcmp(reached, "C01GetReport"))
			step = 56;
		else if (!strcmp(reached, "C02QuestgiverWaitWhat"))
			step = 70;
		else if (!strcmp(reached, "C03SkjorLayItOnMe"))
			step = 84;
		else if (!strcmp(reached, "C03SkjorYeahWhatever") || !strcmp(reached, "C03SkjorAgainIAskUnderwhat") ||
		         !strcmp(reached, "C03SkjorUnderforgeEntryBranchTopic"))
			step = stage[CO_SILVER_HAND] == 10 ? 83 : step;
		else if (!strcmp(reached, "C04KodlakSeeYa") || !strcmp(reached, "C04KodlakNoFateButWhatWeMake"))
			step = stage[CO_BLOODS_HONOR] == 10 ? 101 : step;
		else if (!strcmp(reached, "C06EorlundMyPleasure") || !strcmp(reached, "C06EorlundForCryinOutLoud"))
			step = stage[CO_GLORY] == 15 ? 132 : step;
		else if (!strcmp(reached, "C06EorlundHereTakeIt"))
			step = 133;
		else if (!strcmp(reached, "C06KodlakCanBeCured"))
			step = stage[CO_GLORY] == 50 ? 135 : step;
		else if (!strncmp(reached, "C00Eorlund", 10) && strstr(reached, "lease") && !(flags & F_WEAPON)) {
			static const char *const WEAPON[5][2] = {{"Sword", "c00_skyforge_sword"}, {"Dagger", "c00_skyforge_dagger"},
			                                         {"Battleaxe", "c00_skyforge_battleaxe"}, {"Greatsword", "c00_skyforge_greatsword"},
			                                         {"Waraxe", "c00_skyforge_waraxe"}};
			for (int k = 0; k < 5; k++)
				if (strstr(reached, WEAPON[k][0])) // (DIAL 0004B6FE - Skyforge steel, for a Companion)
					qk_give(WEAPON[k][1], 1), flags |= F_WEAPON;
		} else if (!strcmp(reached, "C00EorlundWaraxeReward") && !(flags & F_WEAPON))
			qk_give("c00_skyforge_waraxe", 1), flags |= F_WEAPON;
	}
	escort_update(dt, feet, interior);
	if (qk_moving())
		return;
	take_up_arms(feet, interior);
	proving_honor(dt, feet, interior);
	brotherhood(feet, interior);
	silver_hand(dt, feet, interior, hours, v);
	bloods_honor(feet, interior);
	purity(feet, interior);
	glory(feet, interior);
	if (stage[CO_SILVER_HAND] >= 30)
		beast_give(); // (a game loaded: the blood's still in you)
}

int companions_brawl_over(int result) {
	if (stage[CO_TAKE_UP_ARMS] != 20 || step != 11)
		return 0;
	if (result == BRAWL_BROKEN) { // (INFO 0007476A)
		qk_play("c00_vilkas_magic");
		step = 0;
	} else {
		qk_play("c00_vilkas_over"); // (INFO 000A3E7F)
		step = 20;
	}
	return 1;
}

// --- Talking --------------------------------------------------------------------------------

enum { T_JOIN, T_KODLAK_JOIN, T_TRAIN, T_EORLUND_SWORD, T_AELA_SHIELD, T_EORLUND_WEAPON, T_SKJOR_WORK, T_FARKAS_GO,
       T_SKJOR_BACK, T_FARKAS_WHERE, T_AELA_WORK, T_SKJOR_SEE, T_SKJOR_DOOR, T_SKJOR_RITUAL, T_AELA_END, T_TELL_SKJOR,
       T_KODLAK_SEE, T_VILKAS_ATTACK, T_VILKAS_BACK, T_EORLUND_FRAGMENTS, T_EORLUND_LAST, T_VILKAS_TOMB, T_KODLAK_GHOST,
       T_KODLAK_END, T_COUNT };
static const char *const SAY[T_COUNT] = {
	T_INFO_000231C8_RNAM0, T_DIAL_000A3E56_FULL0, T_DIAL_000A3E4C_FULL0,
	T_DIAL_000A3E4A_FULL0, T_DIAL_000A3E42_FULL0, T_DIAL_0004B6FF_FULL0,
	T_DIAL_00053439_FULL0, T_DIAL_0006F6B4_FULL0, T_DIAL_000681E8_FULL0, T_DIAL_00071DE0_FULL0,
	T_DIAL_00053439_FULL0, T_DIAL_000171DC_FULL0, "I'm here.", T_DIAL_0002C182_FULL0, "What happened to Skjor?",
	T_DIAL_000B3DFD_FULL0, T_DIAL_000171DC_FULL0, T_DIAL_00021574_FULL0, "The Silver Hand are dead.",
	T_DIAL_000582B3_FULL0, T_DIAL_000E4A1F_FULL0, T_DIAL_000956B0_FULL0, T_DIAL_000582B2_FULL0,
	T_DIAL_000582B1_FULL0}; // (Skyrim's player lines: C00 .. C06's topics)
static const char *const PLAY[T_COUNT] = {NULL, "c00_kodlak", "c00_vilkas_train", "c00_eorlund", "c00_aela_shield",
                                          "c00_eorlund_weapon", "c01_skjor", "c01_farkas", "c01_skjor_back", "c02_farkas",
                                          "c03_aela", "c03_skjor_aela", "c03_skjor_door", "c03_skjor_ritual", "c03_aela_end",
                                          "c03_kodlak_skjor", "c04_kodlak", "c04_vilkas", "c05_vilkas_back", "c06_eorlund",
                                          "c06_eorlund_last", "c06_vilkas_tomb", "c06_kodlak_ghost", "c06_kodlak_end"};

static int is(int actor, const char *id) {
	return !strcmp(actors_id(actor), id);
}

static int circle(int actor) {
	for (int k = 0; k < 4; k++)
		if (is(actor, CIRCLE[k]))
			return 1;
	return 0;
}

static int topics_of(int actor, int *out) {
	int n = 0;
	const int *s = stage;
	const int busy = step != 0;
	if (circle(actor) && !s[CO_TAKE_UP_ARMS])
		out[n++] = T_JOIN;
	if (is(actor, "kodlakwhitemane") && s[CO_TAKE_UP_ARMS] <= 10 && !busy)
		out[n++] = T_KODLAK_JOIN;
	if (is(actor, "vilkas") && s[CO_TAKE_UP_ARMS] == 20 && !busy)
		out[n++] = T_TRAIN;
	if (is(actor, "eorlundgraymane") && s[CO_TAKE_UP_ARMS] == 30 && qk_has("c00_vilkas_sword") && !busy)
		out[n++] = T_EORLUND_SWORD;
	if (is(actor, "aelathehuntress") && s[CO_TAKE_UP_ARMS] == 40 && !busy)
		out[n++] = T_AELA_SHIELD;
	if (is(actor, "eorlundgraymane") && s[CO_BROTHERHOOD] >= QUEST_DONE && !(flags & F_WEAPON))
		out[n++] = T_EORLUND_WEAPON;
	if (is(actor, "skjor") && s[CO_TAKE_UP_ARMS] >= QUEST_DONE && s[CO_PROVING_HONOR] <= 10 && !busy)
		out[n++] = T_SKJOR_WORK;
	if (is(actor, "farkas") && s[CO_PROVING_HONOR] == 20 && !busy)
		out[n++] = T_FARKAS_GO;
	if (is(actor, "skjor") && s[CO_PROVING_HONOR] == 40 && !busy)
		out[n++] = T_SKJOR_BACK;
	if (is(actor, "farkas") && s[CO_BROTHERHOOD] == 10 && !busy)
		out[n++] = T_FARKAS_WHERE;
	if (is(actor, "aelathehuntress") && s[CO_BROTHERHOOD] >= QUEST_DONE && !s[CO_SILVER_HAND] && !busy)
		out[n++] = T_AELA_WORK;
	if (is(actor, "skjor") && s[CO_SILVER_HAND] == 5 && !busy)
		out[n++] = T_SKJOR_SEE;
	if (is(actor, "skjor") && s[CO_SILVER_HAND] == 10 && !busy)
		out[n++] = T_SKJOR_DOOR;
	if (is(actor, "skjor") && s[CO_SILVER_HAND] == 20 && !busy)
		out[n++] = T_SKJOR_RITUAL;
	if (is(actor, "aelathehuntress") && s[CO_SILVER_HAND] == 31 && !busy)
		out[n++] = T_AELA_END;
	if (is(actor, "kodlakwhitemane") && s[CO_SILVER_HAND] >= QUEST_DONE && !(flags & F_TOLD_SKJOR) && s[CO_BLOODS_HONOR] < 30)
		out[n++] = T_TELL_SKJOR;
	if (is(actor, "kodlakwhitemane") && s[CO_SILVER_HAND] >= QUEST_DONE && s[CO_BLOODS_HONOR] <= 10 && !busy)
		out[n++] = T_KODLAK_SEE;
	if (is(actor, "vilkas") && s[CO_BLOODS_HONOR] == 30 && !busy)
		out[n++] = T_VILKAS_ATTACK;
	if (is(actor, "vilkas") && s[CO_PURITY] == 20 && !busy)
		out[n++] = T_VILKAS_BACK;
	if (is(actor, "eorlundgraymane") && s[CO_GLORY] == 15 && !busy)
		out[n++] = T_EORLUND_FRAGMENTS;
	if (is(actor, "eorlundgraymane") && s[CO_GLORY] == 18 && !busy)
		out[n++] = T_EORLUND_LAST;
	if (is(actor, "vilkas") && s[CO_GLORY] == 35)
		out[n++] = T_VILKAS_TOMB;
	if (is(actor, "kodlakwhitemane") && s[CO_GLORY] == 50 && !busy)
		out[n++] = T_KODLAK_GHOST;
	if (is(actor, "kodlakwhitemane") && s[CO_GLORY] == 70 && !busy)
		out[n++] = T_KODLAK_END;
	return n;
}

int companions_topics(int actor, const char **lines, int max) {
	int t[16];
	const int n = topics_of(actor, t);
	int m = 0;
	for (int i = 0; i < n && m < max; i++)
		lines[m++] = SAY[t[i]];
	return m;
}

void companions_say(int actor, int topic) {
	int t[16];
	const int n = topics_of(actor, t);
	if (topic < 0 || topic >= n)
		return;
	const int k = t[topic];
	if (k == T_JOIN) { // (INFO 000231C8)
		char key[48];
		snprintf(key, sizeof(key), "c00_ask_%s", actors_id(actor));
		qk_play(key);
		set_stage(CO_TAKE_UP_ARMS, 10, T_QUST_0001CEF5_NNAM0);
		return;
	}
	if (PLAY[k])
		qk_play(PLAY[k]);
	switch (k) {
	case T_KODLAK_JOIN:
		if (!stage[CO_TAKE_UP_ARMS])
			stage[CO_TAKE_UP_ARMS] = 10;
		step = 1;
		break;
	case T_TRAIN:
		step = 10;
		break;
	case T_EORLUND_SWORD:
		step = 30;
		break;
	case T_AELA_SHIELD:
		step = 40;
		break;
	case T_SKJOR_WORK: // (each conversation's end moves its quest on, whichever answers you chose)
		if (!stage[CO_PROVING_HONOR])
			stage[CO_PROVING_HONOR] = 10;
		step = 51;
		break;
	case T_FARKAS_GO:
		step = 52;
		break;
	case T_SKJOR_BACK:
		step = 56;
		break;
	case T_FARKAS_WHERE:
		step = 70;
		break;
	case T_SKJOR_DOOR:
		step = 83;
		break;
	case T_EORLUND_FRAGMENTS:
		step = 132;
		break;
	case T_EORLUND_LAST:
		step = 133;
		break;
	case T_KODLAK_GHOST:
		step = 135;
		break;
	case T_AELA_WORK:
		step = 81;
		break;
	case T_SKJOR_SEE:
		step = 82;
		break;
	case T_AELA_END:
		step = 89;
		break;
	case T_TELL_SKJOR:
		flags |= F_TOLD_SKJOR;
		break;
	case T_KODLAK_SEE:
		if (!stage[CO_BLOODS_HONOR])
			stage[CO_BLOODS_HONOR] = 10;
		step = 101;
		break;
	case T_VILKAS_ATTACK:
		step = 103;
		break;
	case T_VILKAS_BACK:
		step = 121;
		break;
	case T_KODLAK_END:
		step = 136;
		break;
	}
}

// --- The compass, the journal, saving ------------------------------------------------------

int companions_marker(float out[2]) {
	const char *who = NULL, *mark = NULL, *place = NULL;
	const int *s = stage;
	if (s[CO_TAKE_UP_ARMS] > 0 && s[CO_TAKE_UP_ARMS] < QUEST_DONE) {
		const int st = s[CO_TAKE_UP_ARMS];
		who = st == 10 ? "kodlakwhitemane" : st == 20 ? "vilkas" : st == 30 ? "eorlundgraymane" : st == 40 ? "aelathehuntress" : NULL;
		mark = st == 50 ? "c00_farkas_end" : NULL, place = T_DIAL_01002EFE_FULL0;
	} else if (s[CO_PROVING_HONOR] > 0 && s[CO_PROVING_HONOR] < QUEST_DONE) {
		const int st = s[CO_PROVING_HONOR];
		who = st == 10 || st == 40 ? "skjor" : st == 20 ? "farkas" : NULL;
		mark = st == 30 ? "c01_fragment" : NULL, place = st == 30 ? T_LCTN_00019173_FULL0 : T_DIAL_01002EFE_FULL0;
	} else if (s[CO_BROTHERHOOD] == 10) {
		mark = "c02_kodlak", place = T_DIAL_01002EFE_FULL0;
	} else if (s[CO_SILVER_HAND] > 0 && s[CO_SILVER_HAND] < QUEST_DONE) {
		const int st = s[CO_SILVER_HAND];
		who = st == 5 || st == 20 ? "skjor" : st == 25 || st == 31 ? "aelathehuntress" : NULL;
		mark = st == 10 ? "c03_skjor_door" : st == 15 ? "underforge_door" : st == 30 ? "c03_krev" : NULL;
		place = st == 30 ? T_LCTN_00019187_FULL0 : T_DIAL_01002EFE_FULL0;
	} else if (s[CO_BLOODS_HONOR] > 0 && s[CO_BLOODS_HONOR] < QUEST_DONE) {
		const int st = s[CO_BLOODS_HONOR];
		who = st == 10 ? "kodlakwhitemane" : NULL;
		mark = st == 20 ? "c04_witch1" : st == 30 ? "c04_attacked1" : NULL, place = st == 20 ? T_LCTN_00019189_FULL0 : T_DIAL_01002EFE_FULL0;
	} else if (s[CO_PURITY] > 0 && s[CO_PURITY] < QUEST_DONE) {
		mark = s[CO_PURITY] == 10 ? "c05_fragments" : NULL, who = s[CO_PURITY] == 20 ? "vilkas" : NULL;
		place = s[CO_PURITY] == 10 ? T_CELL_00015288_FULL0 : T_DIAL_01002EFE_FULL0;
	} else if (s[CO_GLORY] > 0 && s[CO_GLORY] < QUEST_DONE) {
		const int st = s[CO_GLORY];
		who = st == 15 || st == 18 ? "eorlundgraymane" : st == 50 || st == 70 ? "kodlakwhitemane" : NULL;
		mark = st == 10 ? "c06_funeral" : st == 17 ? "c06_end_table" : st == 20 ? "underforge_door" : st == 35 ? "c06_statue"
		     : st == 40 ? "c06_chamber" : st == 60 || st == 65 ? "c06_flame" : NULL;
		place = st >= 30 && st != 15 && st != 17 && st != 18 ? T_LCTN_00019265_FULL0 : T_DIAL_01002EFE_FULL0;
	}
	if (who && qk_who_marker(who, out))
		return 1;
	if (mark)
		return qk_place_marker(mark, place, out);
	return place && qk_marker_named(place, out);
}

int companions_journal(const char **names, const char **objectives, int *done, int max) {
	int n = 0;
	for (int q = 0; q < CO_COUNT && n < max; q++) {
		if (!stage[q])
			continue;
		names[n] = NAMES[q], objectives[n] = stage[q] >= QUEST_DONE ? T_GMST_00017026_DATA0 : objective(q);
		done[n++] = stage[q] >= QUEST_DONE;
	}
	return n;
}

void companions_save(int out[16]) {
	memset(out, 0, 16 * sizeof(int));
	for (int q = 0; q < CO_COUNT; q++)
		out[q] = stage[q];
	out[CO_COUNT] = flags;
}

void companions_load(const int saved[16]) {
	memset(stage, 0, sizeof(stage)); // (not beast_clear's: beast_load has it)
	step = flags = 0, timer = 0.0f, with_n = 0, advanced = -1;
	foes_clear();
	for (int q = 0; q < CO_COUNT; q++)
		stage[q] = saved[q];
	flags = saved[CO_COUNT];
	// (mid-scene when saved: from where the stage begins - its people with you again)
	if (stage[CO_TAKE_UP_ARMS] == 50)
		escort("farkas", NULL, NULL), step = 43;
	if (stage[CO_PROVING_HONOR] == 30 || stage[CO_PROVING_HONOR] == 40)
		escort("farkas", NULL, NULL);
	if (stage[CO_BROTHERHOOD] == 10)
		step = 70;
	if (stage[CO_SILVER_HAND] == 25)
		stage[CO_SILVER_HAND] = 20; // (the ritual again: the change, the waking)
	if (stage[CO_SILVER_HAND] == 30 || stage[CO_SILVER_HAND] == 31)
		escort("aelathehuntress", NULL, NULL);
	if (stage[CO_PURITY] == 10 || stage[CO_PURITY] == 20)
		escort("vilkas", NULL, NULL);
	if (stage[CO_GLORY] == 30)
		escort("vilkas", "aelathehuntress", "farkas");
	if (stage[CO_GLORY] >= 35 && stage[CO_GLORY] < QUEST_DONE)
		escort("farkas", NULL, NULL);
	if (stage[CO_GLORY] == 65)
		stage[CO_GLORY] = 60, qk_give("c04_witch_head", qk_has("c04_witch_head") ? 0 : 1); // (the wolf spirit again)
}
