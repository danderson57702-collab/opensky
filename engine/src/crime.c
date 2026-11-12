#include "crime.h"
#include "services.h"
#include "perks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "collision.h"
#include "equip.h"
#include "creatures.h"
#include "fileio.h"
#include "gamelog.h"
#include "items.h"
#include "menu.h"
#include "skills.h"
#include "ui.h"
#include "voice.h"
#include "gametext.h"

#define WITNESS_M 18.0f       // seen this far off (in the open, in sight)
#define WITNESS_SNEAK_M 6.0f  // ... sneaking
#define ATTACK_GOLD 1000      // Skyrim's iCrimeGoldAttack: guards attack on sight at this bounty
#define JAIL_GOLD_A_DAY 100   // a day in jail for each 100 of it (at least one)
#define MAX_LINES 96
#define LOCKPICK_FORM 0xAu   // (Skyrim.esm's Lockpick)
#define MAX_ACTOR_HOLDS 1024

typedef struct {
	char eid[40], name[24];
	int arrest, attack, murder, assault, trespass, pickpocket, escape, werewolf;
	float steal_mult;
	int release_place;
	float release[3], release_heading;
	int cell_place;             // its jail: the prison marker (Skyrim's PrisonMarker), -99 none here
	float cell[3], cell_heading;
	uint32_t things_chest, stolen_chest; // where your things wait (PLCN), the stolen goods go (STOL)
	uint32_t clothes[4];        // the jail's clothes (JOUT: forms)
	int clothes_n;
	int bounty, hostile;
} Hold;

typedef struct {
	char topic[8], voice[24], path[64], text[200];
	int sex, hold, part;
} Line;

// A guard's greeting (G lines: Skyrim's DialogueGuardsGeneral Hellos) - who may say it
#define MAX_HELLOS 3200
typedef struct {
	unsigned holds;     // (bits by hold; 0 any)
	int sex, part;
	char races[48];     // (";"-separated groups of ","-separated +race / -race; "" any)
	char voice[20], path[64], text[160];
} Hello;
static Hello *hellos;
static int hello_count;
static float hello_shown; // (a greeting's subtitle: gone a moment after its voice ends)

static Hold holds[CRIME_HOLDS];
static int hold_count;
static Line lines[MAX_LINES];
static int line_count;
static char prompts[4][96]; // pay, jail, resist (Skyrim's words: "<CrimeGold>" the bounty)
static struct { char id[40]; int hold; } actor_eids[MAX_ACTOR_HOLDS];
static int actor_eid_count;
static int *actor_hold; // (by actors.h's index, once they're loaded)
static int actor_hold_n = -1;
static float player_at[3];
static int player_sneaking;
static float player_visible = 1.0f;
static int arrest_guard = -1, arrest_hold = -1, arrest_female;
static int arrest_opts[4], arrest_n, arrest_asking; // (asking: the choices up once the guard's said it)
static char arrest_text[4][128]; // (pay, jail, resist - Bribery's bribe)
static unsigned rng = 0x2545f491u;
static int jailed = -1; // the hold whose cell you're in (crime_jail_enter), -1

static int split(char *line, char **f, int max) {
	int n = 0;
	f[n++] = line;
	for (char *p = line; *p && n < max; p++)
		if (*p == '|')
			*p = 0, f[n++] = p + 1;
	return n;
}

int crime_hold_index(const char *faction_eid) {
	for (int i = 0; i < hold_count; i++)
		if (!strcmp(holds[i].eid, faction_eid))
			return i;
	return -1;
}

void crime_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%scrime.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[28];
		const int n = split(line, f, 28);
		if (f[0][0] == 'H' && n >= 17 && hold_count < CRIME_HOLDS) {
			Hold *h = &holds[hold_count++];
			memset(h, 0, sizeof(*h));
			snprintf(h->eid, sizeof(h->eid), "%s", f[1]);
			snprintf(h->name, sizeof(h->name), "%s", f[2]);
			h->arrest = atoi(f[3]), h->attack = atoi(f[4]), h->murder = atoi(f[5]), h->assault = atoi(f[6]);
			h->trespass = atoi(f[7]), h->pickpocket = atoi(f[8]), h->steal_mult = (float)atof(f[9]);
			h->escape = atoi(f[10]), h->werewolf = atoi(f[11]), h->release_place = atoi(f[12]);
			h->release[0] = (float)atof(f[13]), h->release[1] = (float)atof(f[14]), h->release[2] = (float)atof(f[15]);
			h->release_heading = (float)atof(f[16]);
			h->cell_place = n >= 25 ? atoi(f[17]) : -99;
			if (n >= 25) {
				h->cell[0] = (float)atof(f[18]), h->cell[1] = (float)atof(f[19]), h->cell[2] = (float)atof(f[20]);
				h->cell_heading = (float)atof(f[21]);
				h->things_chest = (uint32_t)strtoul(f[22], NULL, 16), h->stolen_chest = (uint32_t)strtoul(f[23], NULL, 16);
				for (char *c = f[24]; *c && *c != '-' && h->clothes_n < 4; c = strchr(c, ',') ? strchr(c, ',') + 1 : c + strlen(c))
					h->clothes[h->clothes_n++] = (uint32_t)strtoul(c, NULL, 16);
			}
		} else if (f[0][0] == 'A' && n >= 3 && actor_eid_count < MAX_ACTOR_HOLDS) {
			snprintf(actor_eids[actor_eid_count].id, sizeof(actor_eids[0].id), "%s", f[1]);
			actor_eids[actor_eid_count++].hold = crime_hold_index(f[2]);
		} else if (f[0][0] == 'P' && n >= 3) {
			const int t = !strcmp(f[1], "pay") ? 0 : !strcmp(f[1], "jail") ? 1 : !strcmp(f[1], "resist") ? 2 : -1;
			if (t >= 0)
				snprintf(prompts[t], sizeof(prompts[0]), "%s", f[2]);
		} else if (f[0][0] == 'G' && n >= 8) {
			if (!hellos)
				hellos = calloc(MAX_HELLOS, sizeof(Hello));
			if (!hellos || hello_count >= MAX_HELLOS)
				continue;
			Hello *h = &hellos[hello_count++];
			h->holds = 0;
			char *save2 = NULL;
			if (strcmp(f[1], "-"))
				for (char *t = strtok_r(f[1], ",", &save2); t; t = strtok_r(NULL, ",", &save2)) {
					const int k = crime_hold_index(t);
					if (k >= 0)
						h->holds |= 1u << k;
				}
			h->sex = atoi(f[2]), h->part = atoi(f[5]);
			snprintf(h->races, sizeof(h->races), "%s", strcmp(f[3], "-") ? f[3] : "");
			snprintf(h->voice, sizeof(h->voice), "%s", f[4]);
			snprintf(h->path, sizeof(h->path), "%s", strcmp(f[6], "-") ? f[6] : "");
			snprintf(h->text, sizeof(h->text), "%s", f[7]);
		} else if (f[0][0] == 'L' && n >= 8 && line_count < MAX_LINES) {
			Line *l = &lines[line_count++];
			snprintf(l->topic, sizeof(l->topic), "%s", f[1]);
			l->sex = atoi(f[2]), l->hold = strcmp(f[3], "-") ? crime_hold_index(f[3]) : -1;
			snprintf(l->voice, sizeof(l->voice), "%s", f[4]);
			l->part = atoi(f[5]);
			snprintf(l->path, sizeof(l->path), "%s", strcmp(f[6], "-") ? f[6] : "");
			snprintf(l->text, sizeof(l->text), "%s", f[7]);
		}
	}
	free(buf);
	game_log("crime: %d holds, %d people who report crimes, %d guard lines, %d greetings", hold_count, actor_eid_count,
	         line_count, hello_count);
}

void crime_clear(void) {
	for (int i = 0; i < hold_count; i++)
		holds[i].bounty = 0, holds[i].hostile = 0;
	arrest_guard = arrest_hold = -1;
}

int crime_hold_count(void) {
	return hold_count;
}

const char *crime_hold_name(int hold) {
	return hold >= 0 && hold < hold_count ? holds[hold].name : "";
}

int crime_bounty(int hold) {
	return hold >= 0 && hold < hold_count ? holds[hold].bounty : 0;
}

int crime_actor_hold(int actor) {
	if (actor_hold_n != actors_count()) { // (the townspeople's holds, once they're loaded)
		free(actor_hold);
		actor_hold_n = actors_count();
		actor_hold = calloc(actor_hold_n > 0 ? actor_hold_n : 1, sizeof(int));
		for (int i = 0; i < actor_hold_n && actor_hold; i++) {
			actor_hold[i] = -1;
			for (int k = 0; k < actor_eid_count; k++)
				if (!strcmp(actor_eids[k].id, actors_id(i)))
					actor_hold[i] = actor_eids[k].hold;
		}
	}
	return actor_hold && actor >= 0 && actor < actor_hold_n ? actor_hold[actor] : -1;
}

void crime_set_player(const float feet[3], int sneaking, float visibility) {
	memcpy(player_at, feet, sizeof(player_at));
	player_sneaking = sneaking, player_visible = visibility;
}

// Who saw (a place): the hold of the first townsperson or guard in sight of it, -1 no one
static int witness(const float at[3]) {
	const float range = (player_sneaking ? WITNESS_SNEAK_M : WITNESS_M) * player_visible;
	if (range <= 0.0f)
		return -1; // (invisible)
	for (int i = 0; i < actors_count(); i++) {
		float p[3];
		const int h = crime_actor_hold(i);
		if (h < 0 || !actors_shown(i, p) || actors_asleep(i) || fabsf(p[1] - at[1]) > 4.0f ||
		    hypotf(p[0] - at[0], p[2] - at[2]) > range)
			continue;
		const float eye[3] = {p[0], p[1] + 1.6f, p[2]}, to[3] = {at[0], at[1] + 1.0f, at[2]};
		if (collision_ray(eye, to) >= 1.0f)
			return h;
	}
	return creatures_guard_sees(at, range);
}

int crime_seen(const float at[3]) {
	return witness(at) >= 0;
}

static void say(const char *topic, int hold, int female) {
	// One of Skyrim's lines for it in the guard's voice (another hold's only if they're all
	// that are left), its parts one after the other (the subtitle shows them all)
	const char *voice = arrest_guard >= 0 ? creatures_voice(arrest_guard) : "";
	int pick[MAX_LINES], n = 0;
	for (int pass = 0; pass < 2 && !n; pass++)
		for (int i = 0; i < line_count; i++) {
			const Line *l = &lines[i];
			if (l->part || strcmp(l->topic, topic) || (l->sex && l->sex != (female ? 2 : 1)) ||
			    (l->hold >= 0 && l->hold != hold) || (!pass && strcmp(l->voice, voice)))
				continue;
			pick[n++] = i;
		}
	if (!n)
		return;
	rng = rng * 1664525u + 1013904223u;
	const int first = pick[(rng >> 8) % n];
	char text[400] = "";
	for (int i = first; i < line_count && (i == first || lines[i].part); i++)
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s", i == first ? "" : " ", lines[i].text);
	if (lines[first].path[0] && !strcmp(lines[first].voice, voice))
		voice_play(lines[first].path);
	ui_subtitle(arrest_guard >= 0 ? creatures_title(arrest_guard) : "Guard", text);
}

static void add_bounty(int hold, int gold, const char *what) {
	Hold *h = &holds[hold];
	h->bounty += gold;
	char text[96];
	snprintf(text, sizeof(text), "%s reported: %d gold bounty in %s", what, h->bounty, h->name);
	ui_notice(text, 3.0f);
	game_log("crime: %s in %s, +%d -> %d", what, h->name, gold, h->bounty);
}

int crime_commit(int crime, int value, const float at[3], int victim_hold) {
	const int hold = victim_hold >= 0 ? victim_hold : witness(at);
	if (hold < 0 || hold >= hold_count)
		return 0;
	const Hold *h = &holds[hold];
	switch (crime) {
	case CRIME_THEFT: {
		const int gold = (int)ceilf(value * h->steal_mult);
		add_bounty(hold, gold > 0 ? gold : 1, "Theft");
		break;
	}
	case CRIME_PICKPOCKET: add_bounty(hold, h->pickpocket, T_MESG_00106BD6_FULL0); break;
	case CRIME_TRESPASS: add_bounty(hold, h->trespass, "Trespassing"); break;
	case CRIME_ASSAULT: add_bounty(hold, h->assault, "Assault"); break;
	case CRIME_MURDER: add_bounty(hold, h->murder, "Murder"); break;
	}
	return 1;
}

void crime_update(float dt) {
	if (hello_shown > 0.0f && !voice_playing() && (hello_shown -= dt) <= 0.0f)
		ui_subtitle(NULL, NULL);
	(void)dt;
	if (arrest_guard >= 0 && arrest_asking && !voice_playing()) { // (the guard's had their say: your answer)
		const char *opts[4];
		for (int i = 0; i < arrest_n; i++)
			opts[i] = arrest_text[i];
		menu_open_dialogue(creatures_title(arrest_guard), opts, arrest_n);
		arrest_asking = 0;
	}
	for (int i = 0; i < hold_count; i++) {
		const Hold *h = &holds[i];
		creatures_set_hold_mood(i, i == jailed ? 0 : h->hostile || h->bounty >= ATTACK_GOLD ? 2 : h->bounty > 0 ? 1 : 0);
	}
	// (the guards' own: struck, or one of them killed where someone saw)
	int hold = creatures_take_guard_struck();
	if (hold >= 0 && hold < hold_count)
		crime_commit(CRIME_ASSAULT, 0, player_at, hold);
	float where[3];
	hold = creatures_take_guard_killed(where);
	if (hold >= 0 && hold < hold_count && witness(where) >= 0)
		crime_commit(CRIME_MURDER, 0, where, hold);
}

int crime_arresting(void) {
	return arrest_guard >= 0;
}

int crime_arrest_hold(void) {
	return arrest_guard >= 0 ? arrest_hold : -1;
}

#define PERK_BRIBERY 0x00058f72u
static int bribe_cost(void);

void crime_arrest_begin(int guard, int hold, int player_female) {
	if (hold < 0 || hold >= hold_count)
		return;
	arrest_guard = guard, arrest_hold = hold, arrest_female = player_female;
	game_log("crime: %s stops you (bounty %d in %s)", creatures_title(guard), holds[hold].bounty, holds[hold].name);
	say("greet", hold, player_female);
	arrest_n = 0, arrest_asking = 1;
	const int bounty = holds[hold].bounty;
	for (int t = 0; t < 3; t++) {
		if (t == 0 && items_gold() < bounty)
			continue; // (can't pay: Skyrim doesn't offer it)
		const char *src = prompts[t][0] ? prompts[t] : t == 0 ? "Pay (<CrimeGold> gold)" : t == 1 ? "Go to jail" : "Resist";
		const char *tag = strstr(src, "<CrimeGold>");
		if (tag)
			snprintf(arrest_text[arrest_n], sizeof(arrest_text[0]), "%.*s%d%s", (int)(tag - src), src, bounty, tag + 11);
		else
			snprintf(arrest_text[arrest_n], sizeof(arrest_text[0]), "%s", src);
		arrest_opts[arrest_n++] = t;
	}
	if (perks_has_form(PERK_BRIBERY) && bounty > 0) { // (Bribery: the guard paid to look away - Skyrim's bribe, no goods taken)
		const int cost = bribe_cost();
		if (items_gold() >= cost && arrest_n < 4) {
			snprintf(arrest_text[arrest_n], sizeof(arrest_text[0]), "Here, take this gold and forget you saw anything. (%d gold)", cost);
			arrest_opts[arrest_n++] = 3;
		}
	}
}

// Stolen goods: taken (Skyrim's guards confiscate them, whether you pay or serve) - into the
// hold's evidence chest (STOL), where they can be stolen back
static void confiscate(int hold) {
	const int chest = hold >= 0 && holds[hold].stolen_chest ? items_open_container(holds[hold].stolen_chest, -1) : -1;
	int list[512];
	const int n = items_inventory(list, 512);
	for (int i = 0; i < n; i++) {
		const int s = items_stolen(list[i]);
		if (s > 0) {
			items_mark_stolen(list[i], -s);
			if (chest >= 0)
				items_put(chest, list[i], s);
			else
				items_add(list[i], -s);
		}
	}
}

static int bribe_cost(void) { // (Skyrim's bribe for a guard of about your level: fBribeScale, fBribeCostCurve)
	return speech_bribe_cost(skills_state()->player_level);
}

int crime_arrest_choice(int choice) {
	if (arrest_guard < 0)
		return ARREST_NONE;
	const int hold = arrest_hold, t = choice >= 0 && choice < arrest_n ? arrest_opts[choice] : 2;
	// (leaving without an answer: resisting, as in Skyrim)
	Hold *h = &holds[hold];
	int result = ARREST_NONE;
	if (t == 3) { // (bribed: the bounty gone, your things kept)
		items_add(items_find_gold(), -bribe_cost());
		h->bounty = 0, h->hostile = 0;
		result = ARREST_PAID;
	} else if (t == 0) {
		say("pay", hold, arrest_female);
		items_add(items_find_gold(), -h->bounty);
		confiscate(hold);
		h->bounty = 0, h->hostile = 0;
		result = ARREST_PAID;
	} else if (t == 1) {
		say("jail", hold, arrest_female);
		result = ARREST_JAIL;
	} else {
		say("resist", hold, arrest_female);
		h->hostile = 1;
		result = ARREST_RESIST;
	}
	creatures_arrest_done(arrest_guard, result == ARREST_RESIST);
	game_log("crime: arrest in %s -> %s", h->name, result == ARREST_PAID ? "paid" : result == ARREST_JAIL ? "jail" : "resisted");
	arrest_guard = -1;
	return result;
}

int crime_jail(int hold, int *place, float pos[3], float *heading) {
	if (hold < 0 || hold >= hold_count)
		return 0;
	Hold *h = &holds[hold];
	int days = h->bounty / JAIL_GOLD_A_DAY;
	if (days < 1)
		days = 1;
	// Time served: Skyrim's - progress toward the next level lost in a skill a day (the
	// furthest along first)
	for (int d = 0; d < days; d++) {
		SkillState *st = skills_state();
		int best = -1;
		float most = 0.0f;
		for (int s = 0; s < SKILL_COUNT; s++)
			if (st->xp[s] > 0.0f && skills_progress(s) < 1.0f && skills_progress(s) > most)
				most = skills_progress(s), best = s;
		if (best >= 0)
			st->xp[best] = 0.0f;
	}
	confiscate(hold);
	if (jailed == hold && h->things_chest) { // (your things back from the evidence chest, as Skyrim gives them)
		const int chest = items_open_container(h->things_chest, -1);
		int its[256], counts[256];
		const int k = items_in_container(chest, its, counts, 256);
		for (int i = 0; i < k; i++)
			items_take(chest, its[i], counts[i]);
	}
	jailed = -1;
	h->bounty = 0, h->hostile = 0;
	*place = h->release_place;
	memcpy(pos, h->release, sizeof(h->release));
	*heading = h->release_heading;
	game_log("crime: %d days in %s's jail; out at place %d (%.1f %.1f %.1f) with %d gold", days, h->name, *place, pos[0], pos[1],
	         pos[2], items_gold());
	return days;
}

int crime_jail_enter(int hold, int *place, float pos[3], float *heading) {
	if (hold < 0 || hold >= hold_count || holds[hold].cell_place <= -99 || !holds[hold].things_chest)
		return 0;
	Hold *h = &holds[hold];
	// Skyrim's: the stolen goods to the evidence chest, everything else of yours to the chest
	// for your things - but one lockpick, if you had any - and the jail's clothes on
	confiscate(hold);
	const int chest = items_open_container(h->things_chest, -1), pick = items_by_form(LOCKPICK_FORM);
	int list[512];
	const int n = items_inventory(list, 512);
	for (int i = 0; i < n; i++)
		items_put(chest, list[i], items_have(list[i]) - (list[i] == pick ? 1 : 0));
	if (items_gold() > 0)
		items_put(chest, items_find_gold(), items_gold());
	equip_drop_uncarried();
	for (int c = 0; c < h->clothes_n; c++) {
		const int it = items_by_form(h->clothes[c]);
		if (it >= 0) {
			items_add(it, 1);
			if (!equip_worn(it))
				equip_toggle(it);
		}
	}
	jailed = hold;
	*place = h->cell_place;
	memcpy(pos, h->cell, sizeof(h->cell));
	*heading = h->cell_heading;
	game_log("crime: into %s's jail cell (place %d, %.1f %.1f %.1f): %d gold, %d lockpicks left on you", h->name, *place, pos[0],
	         pos[1], pos[2], items_gold(), items_have(pick));
	return 1;
}

int crime_jailed(void) {
	return jailed;
}

int crime_jail_place(void) {
	return jailed >= 0 ? holds[jailed].cell_place : -99;
}

void crime_escaped(void) {
	if (jailed < 0)
		return;
	Hold *h = &holds[jailed];
	jailed = -1;
	add_bounty((int)(h - holds), h->escape > 0 ? h->escape : 1, "Escape from jail"); // (Skyrim's jailbreak: your things still in the chest)
}

void crime_save(int32_t bounty[CRIME_HOLDS], uint8_t hostile[CRIME_HOLDS], int32_t *jailed_hold) {
	for (int i = 0; i < CRIME_HOLDS; i++)
		bounty[i] = i < hold_count ? holds[i].bounty : 0, hostile[i] = i < hold_count ? (uint8_t)holds[i].hostile : 0;
	*jailed_hold = jailed;
}

void crime_load(const int32_t bounty[CRIME_HOLDS], const uint8_t hostile[CRIME_HOLDS], int jailed_hold) {
	for (int i = 0; i < hold_count; i++)
		holds[i].bounty = bounty[i], holds[i].hostile = hostile[i];
	jailed = jailed_hold >= 0 && jailed_hold < hold_count ? jailed_hold : -1;
	arrest_guard = -1;
}

// Your race against a greeting's (its groups each want one of theirs: "+argonianrace", or not
// "-highelfrace")
static int race_fits(const char *races, const char *eid) {
	char buf[48], *save = NULL;
	snprintf(buf, sizeof(buf), "%s", races);
	for (char *g = strtok_r(buf, ";", &save); g; g = strtok_r(NULL, ";", &save)) {
		int any = 0;
		char grp[48], *s2 = NULL;
		snprintf(grp, sizeof(grp), "%s", g);
		for (char *r = strtok_r(grp, ",", &s2); r && !any; r = strtok_r(NULL, ",", &s2))
			any = (r[0] == '+') == !strcasecmp(r + 1, eid);
		if (!any)
			return 0;
	}
	return 1;
}

int crime_guard_hello(int guard, int hold, const char *voice, int female, const char *race_eid) {
	int pick[64], n = 0;
	for (int i = 0; i < hello_count && n < 64; i++) {
		const Hello *h = &hellos[i];
		if (h->part || strcmp(h->voice, voice) || (h->holds && (hold < 0 || !(h->holds >> hold & 1))) ||
		    (h->sex && h->sex != (female ? 2 : 1)) || (h->races[0] && !race_fits(h->races, race_eid)))
			continue;
		pick[n++] = i;
	}
	if (!n)
		return 0;
	rng = rng * 1664525u + 1013904223u;
	const int first = pick[(rng >> 8) % n];
	char text[400] = "";
	for (int i = first; i < hello_count && (i == first || hellos[i].part); i++)
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s", i == first ? "" : " ", hellos[i].text);
	if (hellos[first].path[0])
		voice_play(hellos[first].path);
	ui_subtitle(creatures_title(guard), text);
	hello_shown = 1.2f;
	return 1;
}
