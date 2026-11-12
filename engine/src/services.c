#include "services.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actors.h"
#include "gamelog.h"
#include "items.h"
#include "lighting.h"
#include "menu.h"
#include "perks.h"
#include "quests.h"
#include "sfx.h"
#include "skills.h"
#include "talk.h"
#include "topics.h"
#include "ui.h"
#include "gametext.h"

#define TRAININGS_A_LEVEL 5   // Skyrim's iTrainingNumAllowedPerLevel
#define TRAINING_HOURS 2.0f   // a session's time
#define BRIBE_SCALE 0.5f      // Skyrim's fBribeScale, fBribeCostCurve, fBribeSpeechcraftMult
#define BRIBE_CURVE 1.4f
#define BRIBE_SPEECH_MULT 0.1f
#define SPEECH_USE_A_CHECK 15.0f // (a check won: Speech trains as on this much gold changing hands)
#define PERSUASION_EASIER 0.3f   // (the Persuasion perk: checks this much easier)
#define PERSUASION_PERK 0x001090a2u // (the tree's Persuasion, Update.esm's)
#define GATE_GUARD T_NPC_00108296_FULL0 // (a guard of the city's: his name)

static int trainings, trained_level = -1; // (sessions this character level, and which level that is)
static int gate_passed;
static int training_actor = -1, training_skill, training_max;
static int gate_door = -1, gate_asking, gate_open_menu;
static int gate_opts[6], gate_n;
static int gate_failed; // (bits: the speech checks tried and failed: not offered again)
static char texts[6][120];
static int menu_kind; // 0 none, 1 training, 2 the gate (its menu up)

// --- Speech checks ------------------------------------------------------------------------

static int speech(void) {
	return skills_state()->level[SK_SPEECH];
}

int speech_persuade(int difficulty) {
	const float need = difficulty * (perks_has_form(PERSUASION_PERK) ? 1.0f - PERSUASION_EASIER : 1.0f);
	const int ok = speech() >= need;
	if (ok)
		skills_use(SK_SPEECH, SPEECH_USE_A_CHECK * (1.0f + difficulty / 25.0f));
	game_log("speech: persuade (speech %d vs %.0f) %s", speech(), need, ok ? "won" : "failed");
	return ok;
}

int speech_intimidate(int difficulty, int their_level) {
	const float sp = perks_apply(EP_MOD_PLAYER_INTIMIDATION, (float)speech(), NULL); // (Intimidation: twice as effective)
	const int ok = sp >= difficulty || skills_state()->player_level > their_level;
	if (ok)
		skills_use(SK_SPEECH, SPEECH_USE_A_CHECK * (1.0f + difficulty / 25.0f));
	game_log("speech: intimidate (speech %d vs %d, level %d vs %d) %s", speech(), difficulty, skills_state()->player_level,
	         their_level, ok ? "won" : "failed");
	return ok;
}

int speech_bribe_cost(int their_level) {
	const float cost = 10.0f * BRIBE_SCALE * powf((float)(their_level > 1 ? their_level : 1), BRIBE_CURVE) *
	                   (1.0f - speech() * BRIBE_SPEECH_MULT / 100.0f);
	return cost < 10.0f ? 10 : (int)(cost + 0.5f);
}

// --- Training -----------------------------------------------------------------------------

// (gold for a level: Skyrim's rises with the square of the level trained to - about 180 at 16,
// 1350 at 51, 4100 at 90)
static int training_cost(int to) {
	return (int)(to * to * 0.5f + 54.0f);
}

static int trainings_left(void) {
	if (trained_level != skills_state()->player_level)
		trained_level = skills_state()->player_level, trainings = 0;
	return TRAININGS_A_LEVEL - trainings;
}

int services_topics(int actor, const char **opts, int max) {
	int skill, most;
	if (max < 1 || !topics_trainer(actor, &skill, &most))
		return 0;
	static char text[96];
	const char *p = topics_trainer_prompt(actor);
	if (p[0])
		snprintf(text, sizeof(text), "%s", p);
	else
		snprintf(text, sizeof(text), "I'd like training in %s.", skills_name(skill));
	opts[0] = text;
	return 1;
}

static void open_training(void) {
	const int level = skills_state()->level[training_skill], to = level + 1;
	static char title[64];
	snprintf(title, sizeof(title), "Training: %s %d", skills_name(training_skill), level);
	const char *opts[2];
	int n = 0;
	if (level >= training_max)
		snprintf(texts[0], sizeof(texts[0]), "(They can teach you no more)");
	else if (trainings_left() <= 0)
		snprintf(texts[0], sizeof(texts[0]), "(Trained five times this level: no more till your next)");
	else
		snprintf(texts[0], sizeof(texts[0]), "Train to %d  (%d gold; %d left this level)", to, training_cost(to), trainings_left());
	opts[n++] = texts[0];
	opts[n++] = "Done.";
	menu_open_dialogue(title, opts, n);
	menu_kind = 1;
}

void services_say(int actor, int k) {
	int skill, most;
	if (k != 0 || !topics_trainer(actor, &skill, &most))
		return;
	const int done = skills_state()->level[skill] >= most;
	topics_trainer_say(actor, done);
	training_actor = actor, training_skill = skill, training_max = most;
	if (done)
		training_actor = -1; // (INFO 000BD7DF)
}

static void train(void) {
	const int level = skills_state()->level[training_skill], to = level + 1, cost = training_cost(to);
	if (level >= training_max || trainings_left() <= 0)
		return;
	if (items_gold() < cost) {
		ui_notice("You don't have enough gold", 3.0f);
		return;
	}
	items_add(items_find_gold(), -cost);
	skills_raise(training_skill);
	trainings++;
	lighting_advance_hours(TRAINING_HOURS);
	sfx_play("itm_gold", 0.7f, 0.0f);
	char text[80];
	snprintf(text, sizeof(text), "%s increased to %d", skills_name(training_skill), to);
	ui_notice(text, 3.0f);
	game_log("training: %s to %d for %d gold (%d this level)", skills_name(training_skill), to, cost, trainings);
}

// --- The Whiterun gate --------------------------------------------------------------------

enum { G_NOTE, G_PERSUADE, G_INTIMIDATE, G_BRIBE, G_LATER };

int services_gate_stop(int door, const char *door_label, int place) {
	const int bts = quests_stage(Q_BEFORE_THE_STORM);
	if (gate_passed || place != -1 || !door_label || !strstr(door_label, T_DIAL_01002EFE_FULL0) || bts <= 0 || bts >= QUEST_DONE)
		return 0;
	gate_door = door, gate_asking = 1;
	topics_say("gate_halt", -1, GATE_GUARD);
	return 1;
}

int services_gate_open(const char *door_label, int place) {
	return place == -1 && door_label && strstr(door_label, T_DIAL_01002EFE_FULL0) &&
	       (gate_passed || quests_stage(Q_BEFORE_THE_STORM) >= QUEST_DONE);
}

static void open_gate_menu(void) {
	const char *opts[6];
	gate_n = 0;
	const int bribe = speech_bribe_cost(skills_state()->player_level);
	for (int g = G_NOTE; g <= G_LATER; g++) {
		if (g == G_NOTE && quests_stage(Q_BEFORE_THE_STORM) < 10)
			continue; // (Gerdur's word: once she's asked you)
		if (g != G_NOTE && g != G_LATER && (gate_failed & (1 << g)))
			continue;
		static const char *const KEYS[] = {"gate_note", "gate_persuade", "gate_intimidate", "gate_bribe", "gate_later"};
		const char *p = topics_prompt(KEYS[g]);
		const char *tag = strstr(p, "<bribecost>");
		if (tag)
			snprintf(texts[gate_n], sizeof(texts[0]), "%.*s%d%s", (int)(tag - p), p, bribe, tag + 11);
		else
			snprintf(texts[gate_n], sizeof(texts[0]), "%s", p[0] ? p : KEYS[g]);
		opts[gate_n] = texts[gate_n];
		gate_opts[gate_n++] = g;
	}
	menu_open_dialogue(GATE_GUARD, opts, gate_n);
	menu_kind = 2;
}

void services_update(void) {
	topics_update();
	if (gate_asking && !topics_busy()) // (the guard's had his say: your answer)
		gate_asking = 0, open_gate_menu();
	if (gate_open_menu && !topics_busy())
		gate_open_menu = 0, open_gate_menu();
	if (training_actor >= 0 && !menu_kind && talk_speaking() < 0) // (the trainer's line said: the training)
		open_training();
}

int services_open(void) {
	return menu_kind || gate_asking || gate_open_menu || training_actor >= 0;
}

int services_choice(int choice, int *go_door) {
	*go_door = -1;
	if (choice == -1 || !menu_kind)
		return 0;
	const int kind = menu_kind;
	menu_kind = 0;
	if (kind == 1) { // training
		if (choice == 0) {
			train();
			open_training(); // (again, for the next level)
		} else {
			training_actor = -1;
		}
		return 1;
	}
	// The gate
	const int g = choice >= 0 && choice < gate_n ? gate_opts[choice] : G_LATER;
	int in = 0;
	switch (g) {
	case G_NOTE:
		topics_say("gate_note", -1, GATE_GUARD), in = 1;
		break;
	case G_PERSUADE:
		if (speech_persuade(SPEECH_VERY_EASY))
			topics_say("gate_persuade", -1, GATE_GUARD), in = 1;
		else
			gate_failed |= 1 << G_PERSUADE, ui_notice("Persuasion failed", 3.0f), gate_open_menu = 1;
		break;
	case G_INTIMIDATE: // (Skyrim's: the gate guard doesn't scare - INFO 00041EF5)
		topics_say("gate_intimidate", -1, GATE_GUARD);
		gate_failed |= 1 << G_INTIMIDATE, gate_open_menu = 1;
		ui_notice("Intimidation failed", 3.0f);
		break;
	case G_BRIBE: {
		const int cost = speech_bribe_cost(skills_state()->player_level);
		if (items_gold() >= cost) {
			items_add(items_find_gold(), -cost);
			skills_use(SK_SPEECH, (float)cost);
			topics_say("gate_bribe", -1, GATE_GUARD), in = 1;
		} else {
			ui_notice("You don't have enough gold", 3.0f), gate_open_menu = 1;
		}
		break;
	}
	default:
		topics_say("gate_later", -1, GATE_GUARD);
	}
	if (in) {
		gate_passed = 1, *go_door = gate_door;
		game_log("gate: let into Whiterun");
	}
	return 1;
}

int32_t services_save(void) {
	return (int32_t)(gate_passed | (trainings & 0xFF) << 8 | ((trained_level < 0 ? 0 : trained_level) & 0xFFFF) << 16);
}

void services_load(int32_t v) {
	gate_passed = v & 1, trainings = (v >> 8) & 0xFF, trained_level = (v >> 16) & 0xFFFF;
	gate_failed = 0, training_actor = -1, menu_kind = 0, gate_asking = gate_open_menu = 0;
}
