#include "college.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actors.h"
#include "creatures.h"
#include "gamelog.h"
#include "items.h"
#include "magic.h"
#include "questkit.h"
#include "quests.h"
#include "skills.h"
#include "ui.h"
#include "worlds.h"
#include "gametext.h"

#define HALL "winterholdcollegehalloftheelements"
#define QUARTERS "winterholdcollegearchmagequarters"
#define MIDDEN_DARK "winterholdcollegemidden02"
#define SAARTHAL "saarthal01"
#define SAARTHAL_DEEP "saarthal02"
#define FELLGLOW_TOP "fellglowkeep03"
#define MZULFT "mzulft01"
#define OCULORY "mzulft03"

#define FARALDA_HEARS_M 25.0f   // a spell cast (or the Voice) this near Faralda: her test seen
#define PICKUP_M 2.2f           // Saarthal's enchanted things taken this near their marks
#define VISION_M 4.0f           // Nerien's vision this near its mark
#define BOOKS_M 3.0f            // the Caller's books on her table this near
#define FLAME_M 5.0f            // the Augur's flame
#define GAVROS_M 6.0f
#define CHEST_M 2.5f            // the Oculory's key chest: the Focusing Crystal
#define ARMILLARY_M 14.0f       // a spell cast this near the Oculory's armillary: the beam focused
#define STAFF_M 6.0f            // the Staff of Magnus taken this near where Morokei stood
#define COURTYARD_M 60.0f       // the College's courtyard (Containment's anomalies, Tolfdir at the bridge)
#define TOLFDIR_GREETS_M 6.0f
#define MOROKEI_REF 0x00086CF4u

static const char *const NAMES[CG_COUNT] = {T_QUST_0001F251_FULL0, T_QUST_0001F252_FULL0, T_QUST_0001F253_FULL0, T_QUST_0001F254_FULL0,
                                              T_QUST_0001F256_FULL0, "Containment", T_QUST_0001F257_FULL0, T_QUST_0001F258_FULL0};
// Faralda's test (Skyrim's MG01: the spell of your best school - Alteration's Magelight, Conjuration's
// Flame Atronach, Destruction's Firebolt, Illusion's Fear, Restoration's Healing Hands)
static const char *const SCHOOL_KEY[5] = {"alteration", "conjuration", "destruction", "illusion", "restoration"};
static const char *const SCHOOL_ASK[5] = {"Cast a Magelight spell", T_QUST_0001F251_NNAM2, T_QUST_0001F251_NNAM3,
                                           T_QUST_0001F251_NNAM4, T_QUST_0001F251_NNAM5};

static int stage[CG_COUNT];
static int step, flags, advanced = -1, school = -1, items_found;
enum { F_VOICE = 1, F_GHOST = 2, F_ANOMALIES = 4 };
static QkEscort esc;
static QkFoes fo;

static void set_stage(int q, int s, const char *objective) {
	const int was = stage[q];
	stage[q] = s;
	qk_stage_notice(NAMES[q], was, s, objective);
	advanced = qk_advance_count();
}

static void done(int q) {
	set_stage(q, QUEST_DONE, "");
}

static const char *objective(int q) {
	const int s = stage[q];
	switch (q) {
	case CG_FIRST_LESSONS:
		return s >= 20 && s <= 24 ? SCHOOL_ASK[s - 20] : s == 30 ? T_QUST_0001F251_NNAM6 : s == 40 ? T_QUST_0001F251_NNAM7
		     : s == 50 ? T_QUST_0001F251_NNAM8 : T_QUST_0001F251_NNAM0;
	case CG_SAARTHAL:
		return s == 10 ? T_QUST_0001F252_NNAM0 : s == 20 || s == 40 || s == 60 ? T_QUST_0001F252_NNAM1 : s == 25 ? T_QUST_0001F252_NNAM2
		     : s == 30 ? "Search for magical artifacts" : s == 50 ? T_QUST_0001F252_NNAM6 : s == 65 ? T_QUST_0001F252_NNAM8
		     : T_QUST_0001F252_NNAM9;
	case CG_BOOKS:
		return s == 10 ? T_QUST_0001F253_NNAM0 : s == 20 ? T_QUST_0001F253_NNAM1 : s == 30 ? "Find the stolen books"
		     : T_QUST_0001F253_NNAM4;
	case CG_INTENTIONS:
		return s == 10 ? T_QUST_0001F254_NNAM0 : s == 20 ? T_QUST_0001F254_NNAM2 : s == 30 ? T_QUST_0001F254_NNAM3
		     : s == 40 ? T_QUST_0001F254_NNAM4 : T_QUST_0001F254_NNAM5;
	case CG_UNSEEN:
		return s == 10 ? T_QUST_0001F253_NNAM0 : s == 20 ? T_QUST_0001F256_NNAM1 : s == 25 ? T_QUST_0001F256_NNAM2
		     : s == 30 ? T_QUST_0001F256_NNAM3 : s == 32 ? T_QUST_0001F256_NNAM4 : s == 35 ? T_QUST_0001F256_NNAM5
		     : s == 40 ? "Focus the Oculory's beam" : "Return to the College";
	case CG_CONTAINMENT:
		return s == 10 ? T_QUST_0001F255_NNAM0 : s == 20 ? T_QUST_0001F255_NNAM1 : s == 30 ? T_QUST_0001F255_NNAM2
		     : s == 40 ? "Defeat the creatures infesting Winterhold" : T_QUST_0001F251_NNAM6;
	case CG_STAFF:
		return s == 10 ? T_QUST_0001F257_NNAM0 : s == 20 ? T_QUST_0001F257_NNAM1 : s == 30 ? T_QUST_0001F257_NNAM2
		     : s == 40 ? T_QUST_0001F257_NNAM3 : s == 50 ? "Exit Labyrinthian" : T_QUST_0001F257_NNAM7;
	default:
		return s == 10 ? T_QUST_0001F258_NNAM1 : s == 20 ? T_QUST_0001F258_NNAM2
		     : s == 30 ? T_QUST_0001F258_NNAM3 : s == 40 ? T_QUST_0001F258_NNAM4 : T_QUST_0001F254_NNAM3;
	}
}

void college_new_game(void) {
	memset(stage, 0, sizeof(stage));
	step = flags = items_found = 0, school = -1, advanced = -1;
	esc.n = 0;
	qk_foes_clear(&fo);
}

void college_set(int q, int s) {
	if (q < 0 || q >= CG_COUNT)
		return;
	static const int ORDER[CG_COUNT] = {CG_FIRST_LESSONS, CG_SAARTHAL, CG_BOOKS, CG_INTENTIONS, CG_UNSEEN, CG_CONTAINMENT,
	                                     CG_STAFF, CG_EYE};
	for (int k = 0; k < CG_COUNT && ORDER[k] != q; k++) // (testing: the ones before it done, in the game's order)
		stage[ORDER[k]] = QUEST_DONE;
	step = 0, qk_foes_clear(&fo);
	stage[q] = s;
	if (q == CG_FIRST_LESSONS && s >= 20 && s <= 24)
		school = s - 20; // (Faralda's test of that school)
	set_stage(q, s, objective(q));
}

int college_advanced(void) {
	return advanced;
}

static int best_school(void) { // (Faralda's pick: the school you're most skilled in)
	const SkillState *sk = skills_state();
	int best = 2; // (Destruction, the College's own, on a tie)
	for (int k = 0; k < 5; k++)
		if (sk->level[SK_ALTERATION + k] > sk->level[SK_ALTERATION + best])
			best = k;
	return best;
}

static int near_actor(const char *who, const float feet[3], float m) {
	float at[3];
	const int a = qk_actor(who);
	return a >= 0 && actors_shown(a, at) && hypotf(at[0] - feet[0], at[2] - feet[2]) < m;
}

static int in_labyrinthian(int interior) { // (its ruins: Labyrinthian's lands of their own, its halls)
	if (interior >= 0)
		return qk_in("labyrinthian01", interior) || qk_in("labyrinthian03", interior);
	const char *p = worlds_own_land() ? worlds_prefix_of(worlds_current()) : "";
	return !strncmp(p, "lab", 3);
}

// First Lessons (MG01): Faralda at the bridge - DIAL 000AF0D8 - her test (a spell of your
// best school, cast where she can see it), persuasion, or the Dragonborn's Voice; Mirabelle's tour;
// Tolfdir's first lesson, the ward
static void first_lessons(const float feet[3], int interior) {
	const int st = stage[CG_FIRST_LESSONS];
	if (st >= 20 && st <= 24) { // (her test: that school's spell, near her)
		const int used = skills_take_used(SK_ALTERATION + school);
		if (used && near_actor("faralda", feet, FARALDA_HEARS_M) && !step)
			qk_play("mg01_cast_done"), step = 2; // (INFO 0001F811)
	}
	if (step == 1 && !qk_busy()) { // (her test: what she'd see cast)
		school = best_school();
		char key[40];
		snprintf(key, sizeof(key), "mg01_test_%s", SCHOOL_KEY[school]);
		qk_play(key);
		skills_take_used(SK_ALTERATION + school);
		set_stage(CG_FIRST_LESSONS, 20 + school, SCHOOL_ASK[school]);
		step = 0;
	} else if (step == 2 && !qk_busy()) { // (INFO 000B8119)
		qk_play("mg01_in"), step = 3;
	} else if (step == 3 && !qk_busy()) {
		set_stage(CG_FIRST_LESSONS, 30, T_QUST_0001F251_NNAM6);
		step = 0;
	} else if (step == 10 && !qk_busy()) { // (Mirabelle's tour: INFO 0002002C)
		set_stage(CG_FIRST_LESSONS, 40, T_QUST_0001F251_NNAM7);
		qk_play("mg01_tour"), step = 11;
	} else if (step == 11 && !qk_busy()) {
		set_stage(CG_FIRST_LESSONS, 50, T_QUST_0001F251_NNAM8);
		step = 0;
	}
	if (st == 50 && qk_in(HALL, interior) && !step) { // (the apprentices' first lesson, in the Hall of the Elements)
		qk_put("tolfdir", "mg_tolfdir_hall", interior);
		qk_play("mg01_classroom"), step = 12;
	} else if (step == 12 && !qk_busy()) {
		qk_play("mg01_tolfdir_ward"), step = 13; // (INFO 00021A6E)
	} else if (step == 13 && !qk_busy()) {
		if (magic_learn("WardLesser", T_SPEL_00013018_FULL0) == 1) // (INFO 00021A6C)
			ui_notice("Learned: Lesser Ward", 3.0f);
		qk_play("mg01_ward"), step = 14;
	} else if (step == 14 && !qk_busy()) {
		qk_back("tolfdir");
		done(CG_FIRST_LESSONS);
		set_stage(CG_SAARTHAL, 10, T_QUST_0001F252_NNAM0);
		step = 0;
	}
}

void college_shouted(const float feet[3]) {
	if ((flags & F_VOICE) && stage[CG_FIRST_LESSONS] < 30 && !step && near_actor("faralda", feet, FARALDA_HEARS_M * 2.0f)) {
		qk_play("mg01_voice"), step = 2; // (INFO 000B811D)
		flags &= ~F_VOICE;
	}
}

// Under Saarthal (MG02): Tolfdir and the apprentices at the dig; Arniel's enchanted finds; the
// amulet; Nerien's vision; the danger - Jyrik Gauldurson - and the Eye; the Arch-Mage told
static void saarthal(const float feet[3], int interior) {
	const int st = stage[CG_SAARTHAL];
	if (st == 10 && interior < 0)
		qk_put("tolfdir", "mg02_tolfdir_out", interior);
	if (step == 50 && !qk_busy()) { // (INFO 000233CA)
		set_stage(CG_SAARTHAL, 20, T_QUST_0001F252_NNAM1);
		qk_escort(&esc, "tolfdir", NULL, NULL);
		step = 0;
	}
	if (st == 20 && qk_in(SAARTHAL, interior)) {
		qk_escort_end(&esc);
		qk_put("tolfdir", "mg02_tolfdir_in", interior);
	}
	if (step == 57 && !qk_busy()) // (INFO 00025142)
		qk_play("mg02_tolfdir_nomore"), step = 51;
	else if (step == 51 && !qk_busy()) { // (INFO 00025142)
		set_stage(CG_SAARTHAL, 25, T_QUST_0001F252_NNAM2);
		step = 0;
	}
	if (st == 25 || st == 30)
		qk_put("arnielgane", "mg02_arniel", interior);
	if (step == 52 && !qk_busy()) { // (INFO 0004B4B6)
		set_stage(CG_SAARTHAL, 30, "Search for magical artifacts");
		step = 0;
	}
	if (st == 30 && qk_in(SAARTHAL, interior)) {
		for (int k = 0; k < 3; k++) { // (the three enchanted things about the dig)
			char key[16];
			snprintf(key, sizeof(key), "mg02_item%d", k + 1);
			if (!(items_found >> k & 1) && qk_at(key, feet, interior, PICKUP_M)) {
				items_found |= 1 << k;
				qk_give("mg02_ring", 1);
			}
		}
		if (qk_at("mg02_amulet", feet, interior, PICKUP_M) && !qk_has("mg02_amulet")) { // (the amulet off the wall: the trap)
			qk_give("mg02_amulet", 1);
			qk_play("mg02_amulet");
			set_stage(CG_SAARTHAL, 40, T_QUST_0001F252_NNAM1);
			qk_put("tolfdir", "mg02_tolfdir_trap", interior);
		}
	}
	if (st == 40 && qk_at("mg02_vision", feet, interior, VISION_M) && !step) { // (INFO 0002617F)
		qk_play("mg02_vision"), step = 53;
	} else if (step == 53 && !qk_busy()) {
		set_stage(CG_SAARTHAL, 50, T_QUST_0001F252_NNAM6);
		step = 0;
	}
	if (st == 50)
		qk_put("tolfdir", "mg02_tolfdir_trap", interior);
	if (step == 54 && !qk_busy()) {
		set_stage(CG_SAARTHAL, 60, T_QUST_0001F252_NNAM1);
		qk_escort(&esc, "tolfdir", NULL, NULL);
		step = 0;
	}
	if (st == 60 && qk_in(SAARTHAL_DEEP, interior))
		set_stage(CG_SAARTHAL, 65, T_QUST_0001F252_NNAM8);
	if (st == 65 && creatures_title_killed(T_NPC_0001BB28_FULL0) && !step) { // (Jyrik down: the Eye before you)
		qk_escort_end(&esc);
		qk_put("tolfdir", "mg02_tolfdir_boss", interior);
		qk_play("mg02_eye"), step = 55;
	} else if (step == 55 && !qk_busy()) {
		set_stage(CG_SAARTHAL, 70, T_QUST_0001F252_NNAM9);
		qk_back("tolfdir");
		step = 0;
	}
	if (step == 56 && !qk_busy()) { // (Savos: INFO 000263D9)
		done(CG_SAARTHAL);
		set_stage(CG_BOOKS, 10, T_QUST_0001F253_NNAM0);
		step = 0;
	}
}

// Hitting the Books (MG03): Mirabelle, Urag on the stolen books; Fellglow Keep - Orthorn, the Caller;
// the books back to the Arcanaeum
static void books(const float feet[3], int interior) {
	const int st = stage[CG_BOOKS];
	if (step == 100 && !qk_busy()) {
		set_stage(CG_BOOKS, 20, T_QUST_0001F253_NNAM1);
		step = 0;
	} else if (step == 101 && !qk_busy()) { // (INFO 0002B36F)
		set_stage(CG_BOOKS, 30, "Find the stolen books");
		step = 0;
	}
	if (st == 30 && qk_in(FELLGLOW_TOP, interior) && fo.at != interior) { // (the Caller, on the top floor)
		qk_foes_clear(&fo);
		qk_foes_one(&fo, "mg03_caller", T_NPC_0006D22E_FULL0, T_NPC_0004D246_FULL0, interior, 0);
		fo.at = interior;
		qk_play("mg03_caller");
	}
	if (st == 30 && qk_at("mg03_books", feet, interior, BOOKS_M)) { // (on her table: the three)
		qk_give("mg03_book1", 1), qk_give("mg03_book2", 1), qk_give("mg03_book3", 1);
		set_stage(CG_BOOKS, 60, T_QUST_0001F253_NNAM4);
	}
	if (step == 102 && !qk_busy()) {
		for (int k = 1; k <= 3; k++) {
			char key[16];
			snprintf(key, sizeof(key), "mg03_book%d", k);
			qk_give(key, -1);
		}
		done(CG_BOOKS);
		set_stage(CG_INTENTIONS, 10, T_QUST_0001F254_NNAM0);
		step = 0;
	}
}

// Good Intentions (MG04): Tolfdir and the orb - Ancano breaks in; Quaranir in the Arch-Mage's quarters
// (time stopped about him); the Augur of Dunlain in the Midden Dark; Savos told
static void intentions(const float feet[3], int interior) {
	const int st = stage[CG_INTENTIONS];
	if (st == 10 && qk_in(HALL, interior)) {
		qk_put("tolfdir", "mg_tolfdir_hall", interior);
		qk_put("ancano", "mg04_ancano_hall", interior);
	}
	if (step == 130 && !qk_busy()) { // (Tolfdir on the orb: then Ancano - INFO 0002C8D3)
		qk_play("mg04_ancano"), step = 131;
	} else if (step == 131 && !qk_busy()) {
		set_stage(CG_INTENTIONS, 20, T_QUST_0001F254_NNAM2);
		qk_back("tolfdir");
		qk_move_to("mg04_ancano_quarters");
		step = 132;
	} else if (step == 132 && !qk_moving() && qk_in(QUARTERS, interior)) {
		qk_put("ancano", "mg04_ancano_quarters", interior);
		qk_put("quaranir", "mg_savos", interior);
		set_stage(CG_INTENTIONS, 30, T_QUST_0001F254_NNAM3);
		qk_play("mg04_quaranir_open"), step = 0;
	}
	if (st == 30 && qk_in(QUARTERS, interior) && step != 132) { // (there whenever you are: a game loaded at it)
		qk_put("ancano", "mg04_ancano_quarters", interior);
		qk_put("quaranir", "mg_savos", interior);
	}
	if (step == 133 && !qk_busy()) { // (Quaranir: INFO 0007726E)
		qk_back("ancano"), qk_back("quaranir");
		set_stage(CG_INTENTIONS, 40, T_QUST_0001F254_NNAM4);
		step = 0;
	}
	if (st == 40 && qk_in(MIDDEN_DARK, interior) && qk_at("mg04_augur_flame", feet, interior, FLAME_M) && !step) {
		qk_play("mg04_augur_lines"), step = 134; // (INFO 0002C8DA)
	} else if (step == 134 && !qk_busy()) {
		set_stage(CG_INTENTIONS, 50, T_QUST_0001F254_NNAM5);
		step = 0;
	}
	if (step == 135 && !qk_busy()) {
		done(CG_INTENTIONS);
		set_stage(CG_UNSEEN, 10, T_QUST_0001F253_NNAM0);
		step = 0;
	}
}

// Revealing the Unseen (mg06): Mirabelle on the Staff and the Synod; Mzulft - Gavros dying, Paratus
// in the Oculory, the Focusing Crystal, the beam focused: Labyrinthian; back at the College, the Eye
// has been loosed (Containment)
static void unseen(const float feet[3], int interior) {
	const int st = stage[CG_UNSEEN];
	if (step == 160 && !qk_busy()) {
		set_stage(CG_UNSEEN, 20, T_QUST_0001F256_NNAM1);
		step = 0;
	}
	if (st == 20 && qk_in(MZULFT, interior))
		set_stage(CG_UNSEEN, 25, T_QUST_0001F256_NNAM2);
	if (st == 25 && qk_at("mg06_gavros", feet, interior, GAVROS_M) && !step) { // (Gavros: a line of theirs)
		qk_play("mg06_gavros"), step = 161;
	} else if (step == 161 && !qk_busy()) {
		set_stage(CG_UNSEEN, 30, T_QUST_0001F256_NNAM3);
		step = 0;
	}
	if ((st == 25 || st == 30) && qk_in(OCULORY, interior))
		qk_put("paratusdecimius", "mg06_paratus", interior);
	if (step == 162 && !qk_busy()) { // (Paratus: the crystal)
		set_stage(CG_UNSEEN, 32, T_QUST_0001F256_NNAM4);
		step = 0;
	}
	if (st == 32 && qk_at("mg06_crystal_chest", feet, interior, CHEST_M)) {
		qk_give("mg06_crystal", 1);
		set_stage(CG_UNSEEN, 35, T_QUST_0001F256_NNAM5);
	}
	if (step == 163 && !qk_busy()) { // (INFO 0009F79A - to the armillary)
		qk_give("mg06_crystal", -1);
		qk_put("paratusdecimius", "mg06_paratus_explain", interior);
		qk_play("mg06_explain");
		set_stage(CG_UNSEEN, 40, "Focus the Oculory's beam");
		for (int k = 0; k < 5; k++)
			skills_take_used(SK_ALTERATION + k);
		step = 0;
	}
	if (st == 40 && qk_at("mg06_armillary", feet, interior, ARMILLARY_M) && !step) { // (a spell into the lenses: the beam focused)
		int cast = 0;
		for (int k = 0; k < 5; k++)
			cast |= skills_take_used(SK_ALTERATION + k);
		if (cast)
			qk_play("mg06_oculory_end"), step = 164;
	} else if (step == 164 && !qk_busy()) {
		qk_play("mg06_vision"), step = 165; // (the map: Labyrinthian)
	} else if (step == 165 && !qk_busy()) {
		qk_back("paratusdecimius");
		set_stage(CG_UNSEEN, 60, "Return to the College");
		step = 0;
	}
	if (st == 60 && qk_in(HALL, interior) && !step) { // (the Eye loosed: INFO 0004D863)
		qk_put("mirabelleervine", "mg06_mirabelle", interior);
		qk_play("mg06_mirabelle_scene"), step = 166;
	} else if (step == 166 && !qk_busy()) {
		done(CG_UNSEEN);
		set_stage(CG_CONTAINMENT, 10, T_QUST_0001F255_NNAM0);
		step = 0;
	}
}

// Containment (MG05): the Hall's damage; Savos Aren dying in the courtyard; Winterhold's anomalies;
// Mirabelle - DIAL 00080101
static void containment(const float feet[3], int interior) {
	const int st = stage[CG_CONTAINMENT];
	if (st == 10 && interior < 0 && qk_at("mg05_savos", feet, interior, COURTYARD_M) && !step) {
		set_stage(CG_CONTAINMENT, 20, T_QUST_0001F255_NNAM1);
		qk_put("tolfdir", "mg05_tolfdir", interior);
		qk_play("mg05_courtyard"), step = 190; // (Tolfdir by the Arch-Mage)
	} else if (step == 190 && !qk_busy()) {
		qk_gone("savosaren"); // (dead)
		qk_play("mg05_tolfdir"), step = 191; // (INFO 00055BF7)
	} else if (step == 191 && !qk_busy()) {
		set_stage(CG_CONTAINMENT, 30, T_QUST_0001F255_NNAM2);
		qk_back("tolfdir");
		step = 0;
	}
	if (st >= 20)
		qk_gone("savosaren");
	if ((st == 30 || st == 40) && interior < 0 && !(flags & F_ANOMALIES) && qk_at("mg05_anomaly1", feet, interior, 120.0f)) {
		qk_foes_clear(&fo);
		qk_foes_spawn(&fo, "mg05_anomaly", 1, 10, T_NPC_00079915_FULL0, NULL, interior);
		flags |= F_ANOMALIES;
		set_stage(CG_CONTAINMENT, 40, "Defeat the creatures infesting Winterhold");
	}
	if (st == 40 && (flags & F_ANOMALIES) && fo.n > 0 && qk_foes_left(&fo) == 0)
		set_stage(CG_CONTAINMENT, 50, T_QUST_0001F251_NNAM6);
	if (st == 50 && qk_in(HALL, interior))
		qk_put("mirabelleervine", "mg05_mirabelle", interior);
	if (step == 192 && !qk_busy()) { // (DIAL 00080101)
		qk_back("mirabelleervine");
		done(CG_CONTAINMENT);
		set_stage(CG_STAFF, 10, T_QUST_0001F257_NNAM0);
		step = 0;
	}
}

// The Staff of Magnus (MG07): Labyrinthian - Morokei, the staff; back to Tolfdir at the College's bridge
static void staff(const float feet[3], int interior) {
	const int st = stage[CG_STAFF];
	if (st == 10 && in_labyrinthian(interior))
		set_stage(CG_STAFF, 20, T_QUST_0001F257_NNAM1);
	if (st == 20 && worlds_own_land() && !strcmp(worlds_prefix_of(worlds_current()), "labyrinthian4"))
		set_stage(CG_STAFF, 30, T_QUST_0001F257_NNAM2);
	if (st == 30 && creatures_spawn_alive(creatures_spawn_of_ref(MOROKEI_REF)) == 0)
		set_stage(CG_STAFF, 40, T_QUST_0001F257_NNAM3);
	if (st == 40 && qk_at("mg07_morokei", feet, interior, STAFF_M)) {
		qk_give("mg07_staff", 1);
		set_stage(CG_STAFF, 50, "Exit Labyrinthian");
	}
	if (st == 50 && !in_labyrinthian(interior) && interior < 0 && !worlds_own_land())
		set_stage(CG_STAFF, 80, T_QUST_0001F257_NNAM7);
	if (st == 80 && interior < 0) {
		qk_put("tolfdir", "mg07_tolfdir_bridge", interior);
		if (!step && near_actor("tolfdir", feet, TOLFDIR_GREETS_M))
			qk_play("mg07_tolfdir"), step = 220; // (INFO 000130DA)
	}
	if (step == 220 && !qk_busy()) {
		qk_back("tolfdir");
		qk_gone("mirabelleervine"); // (INFO 000BF448)
		done(CG_STAFF);
		set_stage(CG_EYE, 10, T_QUST_0001F258_NNAM1);
		step = 0;
	}
	if (st >= 80 || stage[CG_STAFF] >= QUEST_DONE)
		qk_gone("mirabelleervine");
}

// The Eye of Magnus (MG08): into the Hall of the Elements with the staff; Ancano; Tolfdir after; Quaranir
static void eye(const float feet[3], int interior) {
	const int st = stage[CG_EYE];
	if (st == 10 && interior < 0 && qk_at("mg08_faralda", feet, interior, COURTYARD_M) && qk_has("mg07_staff"))
		set_stage(CG_EYE, 20, T_QUST_0001F258_NNAM2);
	if (st == 20 && qk_in(HALL, interior) && fo.at != interior) { // (Ancano at the Eye: his taunt, the fight)
		qk_gone("ancano");
		qk_foes_clear(&fo);
		qk_foes_one(&fo, "mg08_ancano", T_NPC_0001E7D7_FULL0, T_NPC_0001E7D7_FULL0, interior, 0);
		fo.at = interior;
		qk_play("mg08_taunt");
		set_stage(CG_EYE, 30, T_QUST_0001F258_NNAM3);
	}
	if (st == 30 && fo.n > 0 && qk_foes_left(&fo) == 0) {
		set_stage(CG_EYE, 40, T_QUST_0001F258_NNAM4);
		qk_play("mg08_after");
	}
	if (st >= 20)
		qk_gone("ancano");
	if ((st == 40 || st == 50) && qk_in(HALL, interior)) {
		qk_put("tolfdir", "mg08_tolfdir", interior);
		qk_put("quaranir", "mg08_quaranir", interior);
	}
	if (step == 240 && !qk_busy()) {
		set_stage(CG_EYE, 50, T_QUST_0001F254_NNAM3);
		step = 0;
	}
	if (step == 241 && !qk_busy()) { // (the Psijics take the Eye: the Arch-Mage's chair yours)
		qk_back("tolfdir"), qk_back("quaranir");
		qk_give("mg08_robes", 1);
		done(CG_EYE);
		step = 0;
	}
}

void college_update(float dt, const float feet[3], int interior, const char *reached) {
	(void)dt;
	if (reached) { // (a conversation's turn: its quest moves on)
		if (!strcmp(reached, "MG01FaraldaEntryTakeTest"))
			step = 1;
		else if (!strcmp(reached, "MG01FaraldaYoureInBranchTopic") && stage[CG_FIRST_LESSONS] < 30)
			step = 3; // (persuaded: in)
		else if (!strcmp(reached, "MG01NiryaEntryDragonborn"))
			flags |= F_VOICE; // (INFO 000B811F)
		else if (!strcmp(reached, "MG01MirabelleStage30TakeTour"))
			step = 10;
		else if (!strcmp(reached, "MG02TolfdirStage10BranchTopic"))
			step = 50;
		else if (!strcmp(reached, "MG02TolfdirStage20NoMoreQuestions"))
			step = 51;
		else if (!strcmp(reached, "MG02ArnielInitialNoTalkContinue") || !strcmp(reached, "MG02ArnielStage30BranchTopic"))
			step = 52;
		else if (!strcmp(reached, "MG02TolfdirStage50VisionInfo1") || !strcmp(reached, "MG02TolfdirStage50ResponseFollow"))
			step = stage[CG_SAARTHAL] == 50 ? 54 : step;
		else if (!strcmp(reached, "MG02SavosFollowUp"))
			step = 56;
		else if (!strcmp(reached, "MG03MirabelleStage10FollowUp"))
			step = 100;
		else if (!strcmp(reached, "MG03Stage20UragFollowUp"))
			step = 101;
		else if (!strcmp(reached, "MG04TolfdirStage10FollowUp"))
			step = 130;
		else if (!strncmp(reached, "MG04Stage30QuaranirAugur", 24))
			step = 133;
		else if (!strncmp(reached, "MG04Stage50Aren", 15) && (strstr(reached, "Final") || strstr(reached, "End")))
			step = 135;
		else if (!strncmp(reached, "MG06Stage10Mirabelle", 20) && strstr(reached, "Staff"))
			step = 160;
		else if (!strcmp(reached, "MG06ParatusCrystalBranchTopic") || !strcmp(reached, "MG06ParatusInitialResponse1"))
			step = stage[CG_UNSEEN] == 30 ? 162 : step;
		else if (!strcmp(reached, "MG06WhatsCrystalFor") || !strcmp(reached, "MG06CrystalHelp"))
			step = stage[CG_UNSEEN] == 35 ? 163 : step;
		else if (!strcmp(reached, "MG05Stage50LabyrinthianTopic") || !strcmp(reached, "MG05Stage50MirabelleResponse3"))
			step = 192;
		else if (!strcmp(reached, "MG08Stage40TolfdirBranchTopic"))
			step = 240;
	}
	qk_escort_update(&esc, dt, feet, interior);
	if (qk_moving())
		return;
	first_lessons(feet, interior);
	saarthal(feet, interior);
	books(feet, interior);
	intentions(feet, interior);
	unseen(feet, interior);
	containment(feet, interior);
	staff(feet, interior);
	eye(feet, interior);
}

// --- Talking --------------------------------------------------------------------------------

enum { T_ENTER, T_MIRABELLE_SENT, T_TOLFDIR_GO, T_TOLFDIR_NOW, T_ARNIEL, T_TOLFDIR_VISION, T_SAVOS_SAARTHAL,
       T_MIRABELLE_CLASS, T_URAG, T_ORTHORN, T_URAG_BOOKS, T_TOLFDIR_URAG, T_QUARANIR, T_SAVOS_AUGUR, T_MIRABELLE_STAFF,
       T_PARATUS, T_PARATUS_CRYSTAL, T_MIRABELLE_SAFE, T_TOLFDIR_AFTER, T_QUARANIR_END, T_COUNT };
static const char *const SAY[T_COUNT] = {
	T_DIAL_000AF0D8_FULL0, T_DIAL_00020026_FULL0, T_DIAL_000233C9_FULL0, T_DIAL_00025135_FULL0,
	T_DIAL_0004B2B2_FULL0, T_DIAL_000202D5_FULL0, T_DIAL_000263D6_FULL0,
	T_DIAL_0002A387_FULL0, T_DIAL_0002B36B_FULL0,
	T_DIAL_00044BFA_FULL0, T_DIAL_0002B966_FULL0, T_DIAL_00082A0F_FULL0,
	T_DIAL_0002C8BF_FULL0, T_DIAL_00027565_FULL0,
	T_DIAL_000274ED_FULL0, T_DIAL_0001F4AC_FULL0, T_DIAL_0009F795_FULL0,
	T_DIAL_0005310C_FULL0, T_DIAL_0003982B_FULL0, T_DIAL_0003982B_FULL0}; // (Skyrim's player lines: MG01 .. MG08)
static const char *const PLAY[T_COUNT] = {
	"mg01_faralda", "mg01_mirabelle", "mg02_tolfdir_go", "mg02_tolfdir_now", "mg02_arniel", "mg02_tolfdir_vision",
	"mg02_savos", "mg03_mirabelle", "mg03_urag", "mg03_orthorn", "mg03_urag_back", "mg04_tolfdir", "mg04_quaranir",
	"mg04_savos", "mg06_mirabelle", "mg06_paratus", "mg06_paratus_crystal", "mg05_mirabelle", "mg08_tolfdir_after",
	"mg08_quaranir"};

static int is(int actor, const char *id) {
	return !strcmp(actors_id(actor), id);
}

static int topics_of(int actor, int *out) {
	int n = 0;
	const int *s = stage;
	const int busy = step != 0;
	if (is(actor, "faralda") && s[CG_FIRST_LESSONS] < 20 && !busy)
		out[n++] = T_ENTER;
	if (is(actor, "mirabelleervine") && s[CG_FIRST_LESSONS] == 30 && !busy)
		out[n++] = T_MIRABELLE_SENT;
	if (is(actor, "tolfdir") && s[CG_SAARTHAL] == 10 && !busy)
		out[n++] = T_TOLFDIR_GO;
	if (is(actor, "tolfdir") && s[CG_SAARTHAL] == 20 && !busy)
		out[n++] = T_TOLFDIR_NOW;
	if (is(actor, "arnielgane") && s[CG_SAARTHAL] == 25 && !busy)
		out[n++] = T_ARNIEL;
	if (is(actor, "tolfdir") && s[CG_SAARTHAL] == 50 && !busy)
		out[n++] = T_TOLFDIR_VISION;
	if (is(actor, "savosaren") && s[CG_SAARTHAL] == 70 && !busy)
		out[n++] = T_SAVOS_SAARTHAL;
	if (is(actor, "mirabelleervine") && s[CG_BOOKS] == 10 && !busy)
		out[n++] = T_MIRABELLE_CLASS;
	if (is(actor, "uraggroshub") && s[CG_BOOKS] == 20 && !busy)
		out[n++] = T_URAG;
	if (is(actor, "orthorn") && s[CG_BOOKS] == 30)
		out[n++] = T_ORTHORN;
	if (is(actor, "uraggroshub") && s[CG_BOOKS] == 60 && !busy)
		out[n++] = T_URAG_BOOKS;
	if (is(actor, "tolfdir") && s[CG_INTENTIONS] == 10 && !busy)
		out[n++] = T_TOLFDIR_URAG;
	if (is(actor, "quaranir") && s[CG_INTENTIONS] == 30 && !busy)
		out[n++] = T_QUARANIR;
	if (is(actor, "savosaren") && s[CG_INTENTIONS] == 50 && !busy)
		out[n++] = T_SAVOS_AUGUR;
	if (is(actor, "mirabelleervine") && s[CG_UNSEEN] == 10 && !busy)
		out[n++] = T_MIRABELLE_STAFF;
	if (is(actor, "paratusdecimius") && s[CG_UNSEEN] == 30 && !busy)
		out[n++] = T_PARATUS;
	if (is(actor, "paratusdecimius") && s[CG_UNSEEN] == 35 && !busy)
		out[n++] = T_PARATUS_CRYSTAL;
	if (is(actor, "mirabelleervine") && s[CG_CONTAINMENT] == 50 && !busy)
		out[n++] = T_MIRABELLE_SAFE;
	if (is(actor, "tolfdir") && s[CG_EYE] == 40 && !busy)
		out[n++] = T_TOLFDIR_AFTER;
	if (is(actor, "quaranir") && s[CG_EYE] == 50 && !busy)
		out[n++] = T_QUARANIR_END;
	return n;
}

int college_topics(int actor, const char **lines, int max) {
	int t[16];
	const int n = topics_of(actor, t);
	int m = 0;
	for (int i = 0; i < n && m < max; i++)
		lines[m++] = SAY[t[i]];
	return m;
}

void college_say(int actor, int topic) {
	int t[16];
	const int n = topics_of(actor, t);
	if (topic < 0 || topic >= n)
		return;
	const int k = t[topic];
	game_log("college: %s says \"%s\" (%s)", actors_id(actor), SAY[k], PLAY[k]);
	qk_play(PLAY[k]);
	switch (k) {
	case T_ENTER:
		if (!stage[CG_FIRST_LESSONS])
			set_stage(CG_FIRST_LESSONS, 10, T_QUST_0001F251_NNAM0);
		break;
	case T_TOLFDIR_GO: // (each conversation's end moves its quest on, whichever answers you chose)
		step = 50;
		break;
	case T_ARNIEL:
		step = 52;
		break;
	case T_TOLFDIR_VISION:
		step = 54;
		break;
	case T_SAVOS_SAARTHAL:
		step = 56;
		break;
	case T_MIRABELLE_CLASS:
		step = 100;
		break;
	case T_URAG:
		step = 101;
		break;
	case T_TOLFDIR_URAG:
		step = 130;
		break;
	case T_QUARANIR:
		step = 133;
		break;
	case T_SAVOS_AUGUR:
		step = 135;
		break;
	case T_MIRABELLE_STAFF:
		step = 160;
		break;
	case T_PARATUS:
		step = 162;
		break;
	case T_MIRABELLE_SAFE:
		step = 192;
		break;
	case T_TOLFDIR_AFTER:
		step = 240;
		break;
	case T_TOLFDIR_NOW: // (then Skyrim's next topic of his: DIAL 00025132)
		step = 57;
		break;
	case T_PARATUS_CRYSTAL:
		step = 163;
		break;
	case T_URAG_BOOKS:
		step = 102;
		break;
	case T_QUARANIR_END:
		step = 241;
		break;
	}
}

// --- The compass, the journal, saving ------------------------------------------------------

int college_marker(float out[2]) {
	static const int ORDER[CG_COUNT] = {CG_FIRST_LESSONS, CG_SAARTHAL, CG_BOOKS, CG_INTENTIONS, CG_UNSEEN, CG_CONTAINMENT,
	                                     CG_STAFF, CG_EYE};
	for (int k = 0; k < CG_COUNT; k++) {
		const int q = ORDER[k], st = stage[q];
		if (!st || st >= QUEST_DONE)
			continue;
		const char *who = NULL, *mark = NULL, *place = T_FACT_0001F259_FULL0;
		switch (q) {
		case CG_FIRST_LESSONS:
			who = st < 30 ? "faralda" : st == 30 ? "mirabelleervine" : "tolfdir";
			break;
		case CG_SAARTHAL:
			who = st == 70 ? "savosaren" : st == 25 ? "arnielgane" : st == 10 || st == 50 ? "tolfdir" : NULL;
			mark = st == 30 ? "mg02_amulet" : st == 40 ? "mg02_vision" : st == 65 ? "mg02_orb" : st == 10 ? "mg02_tolfdir_out" : NULL;
			place = st == 70 ? T_FACT_0001F259_FULL0 : T_LCTN_00019299_FULL0;
			break;
		case CG_BOOKS:
			who = st == 10 ? "mirabelleervine" : st == 20 || st == 60 ? "uraggroshub" : NULL;
			mark = st == 30 ? "mg03_books" : NULL, place = st == 30 ? T_LCTN_00019177_FULL0 : place;
			break;
		case CG_INTENTIONS:
			who = st == 10 ? "tolfdir" : st == 30 ? "quaranir" : st == 50 ? "savosaren" : st == 20 ? "ancano" : NULL;
			mark = st == 40 ? "mg04_augur_flame" : NULL;
			break;
		case CG_UNSEEN:
			who = st == 10 ? "mirabelleervine" : st == 30 || st == 35 ? "paratusdecimius" : NULL;
			mark = st == 25 ? "mg06_gavros" : st == 32 ? "mg06_crystal_chest" : st == 40 ? "mg06_armillary" : NULL;
			place = st >= 20 && st < 60 ? T_LCTN_00019281_FULL0 : place;
			break;
		case CG_CONTAINMENT:
			who = st == 50 ? "mirabelleervine" : NULL;
			mark = st <= 20 ? "mg05_savos" : st == 30 || st == 40 ? "mg05_anomaly1" : NULL;
			place = st == 30 || st == 40 ? T_DIAL_01002EFA_FULL0 : place;
			break;
		case CG_STAFF:
			who = st == 80 ? "tolfdir" : NULL, mark = st >= 20 && st <= 40 ? "mg07_morokei" : NULL;
			place = st < 80 ? "Labyrinthian" : place;
			break;
		default:
			who = st == 40 ? "tolfdir" : st == 50 ? "quaranir" : NULL, mark = st == 30 ? "mg08_ancano" : NULL;
			break;
		}
		if (who && qk_who_marker(who, out))
			return 1;
		if (mark)
			return qk_place_marker(mark, place, out);
		return qk_marker_named(place, out);
	}
	return 0;
}

int college_journal(const char **names, const char **objectives, int *done_out, int max) {
	int n = 0;
	for (int q = 0; q < CG_COUNT && n < max; q++) {
		if (!stage[q])
			continue;
		names[n] = NAMES[q], objectives[n] = stage[q] >= QUEST_DONE ? T_GMST_00017026_DATA0 : objective(q);
		done_out[n++] = stage[q] >= QUEST_DONE;
	}
	return n;
}

void college_save(int out[16]) {
	memset(out, 0, 16 * sizeof(int));
	for (int q = 0; q < CG_COUNT; q++)
		out[q] = stage[q];
	out[CG_COUNT] = flags, out[CG_COUNT + 1] = school, out[CG_COUNT + 2] = items_found;
}

void college_load(const int saved[16]) {
	college_new_game();
	for (int q = 0; q < CG_COUNT; q++)
		stage[q] = saved[q];
	flags = saved[CG_COUNT] & ~F_ANOMALIES, school = saved[CG_COUNT + 1], items_found = saved[CG_COUNT + 2];
	if (school < 0 || school > 4)
		school = best_school();
	// (mid-scene when saved: the stage's people again)
	if (stage[CG_SAARTHAL] == 20 || stage[CG_SAARTHAL] == 60)
		qk_escort(&esc, "tolfdir", NULL, NULL);
	if (stage[CG_INTENTIONS] == 20)
		stage[CG_INTENTIONS] = 10; // (Ancano's summons again)
	if (stage[CG_EYE] == 30)
		stage[CG_EYE] = 20; // (Ancano again)
}
