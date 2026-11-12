#include "beast.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "actors.h"
#include "fileio.h"
#include "gamelog.h"
#include "sfx.h"
#include "ui.h"
#include "gametext.h"

#define LEVELS 12
#define BEAST_DAY_H 24.0f     // (a power: once a day)
#define STRIKE_S 0.55f        // a claw's blow drawn this long (its clip)
#define POWER_S 0.9f          // ... a power attack's (the two-clawed maul)
#define RUN_FROM_MS 0.4f      // (its run above this pace; else it stands)

static int have_data;
static float claws = 20.0f, reach_m = 2.1f, height = 1.13f; // (the werewolf race's: 20 unarmed, 150 units' reach)
static int level_top[LEVELS], level_count;
static float level_bonus[LEVELS];
static float health_bonus = 50.0f, stamina_bonus = 100.0f, feed_health = 50.0f;
static int no_heal = 1;
static float standard_s = 150.0f, feed_more_s = 30.0f, warn_s = 15.0f;

static int known, readied, active, kind = -1, body = -1;
static float cooldown_h, left_s, anim_t, strike_t = -1.0f;
static int strike_power, strike_side, warned;
static int fed[8], fed_count; // (bodies fed on this time: each once)

void beast_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%sbeast.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		game_log("beast: no beast.txt");
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		int top, heal;
		float a, b, c, d;
		if (sscanf(line, "C\t%f\t%f\t%f", &a, &b, &c) == 3)
			claws = a, reach_m = b, height = c, have_data = 1;
		else if (sscanf(line, "L\t%d\t%f", &top, &a) == 2 && level_count < LEVELS)
			level_top[level_count] = top, level_bonus[level_count++] = a;
		else if (sscanf(line, "A\t%f\t%f\t%d\t%f", &a, &b, &heal, &d) == 4)
			health_bonus = a, stamina_bonus = b, no_heal = heal, feed_health = d;
		else if (sscanf(line, "T\t%f\t%f\t%f", &a, &b, &c) == 3)
			standard_s = a, feed_more_s = b, warn_s = c;
	}
	free(buf);
	game_log("beast: claws %.0f (+%d levels), %.0f s", claws, level_count, standard_s);
}

static void unspawn(void) {
	if (body >= 0)
		actors_beast_remove(body), body = -1;
}

void beast_clear(void) {
	unspawn();
	known = readied = active = 0, cooldown_h = 0.0f, left_s = 0.0f, fed_count = 0;
}

int beast_known(void) {
	return known;
}

void beast_give(void) {
	if (!known)
		game_log("beast: the blood of the beast");
	known = 1;
}

int beast_ready(void) {
	return known && cooldown_h <= 0.0f && !active;
}

int beast_readied(void) {
	return known && readied;
}

void beast_ready_it(int on) {
	readied = on && known;
}

int beast_active(void) {
	return active;
}

float beast_left(void) {
	return active ? left_s : 0.0f;
}

int beast_change(Vitals *v, int free) {
	if (!known || active || (!free && cooldown_h > 0.0f))
		return 0;
	active = 1, left_s = standard_s, warned = 0, fed_count = 0, strike_t = -1.0f;
	if (!free)
		cooldown_h = BEAST_DAY_H;
	v->max_health += health_bonus, v->health += health_bonus;
	v->max_stamina += stamina_bonus, v->stamina += stamina_bonus, v->shown_for = 0.0f;
	sfx_play("werewolf_change", 1.0f, 0.0f);
	ui_notice(T_MGEF_00092C45_FULL0, 3.0f);
	game_log("beast: changed (%.0f s)%s", left_s, free ? " - the ritual's" : "");
	return 1;
}

void beast_revert(Vitals *v) {
	if (!active)
		return;
	active = 0, left_s = 0.0f;
	v->max_health -= health_bonus, v->max_stamina -= stamina_bonus;
	if (v->health > v->max_health)
		v->health = v->max_health;
	if (v->stamina > v->max_stamina)
		v->stamina = v->max_stamina;
	if (v->health < 1.0f)
		v->health = 1.0f; // (the change back doesn't kill you)
	unspawn();
	ui_notice("You return to your normal form", 3.0f);
	game_log("beast: back");
}

void beast_unbonus(Vitals *v) {
	if (!active)
		return;
	v->max_health -= health_bonus, v->max_stamina -= stamina_bonus;
}

float beast_claw_damage(int level) {
	float bonus = 0.0f;
	for (int i = 0; i < level_count; i++)
		if (level <= level_top[i]) {
			bonus = level_bonus[i];
			break;
		}
	return claws + bonus; // (Skyrim's: the race's unarmed and its level's Fortify Unarmed Damage)
}

float beast_reach_m(void) {
	return reach_m;
}

void beast_strike(int power) {
	if (!active)
		return;
	strike_t = 0.0f, strike_power = power, strike_side ^= 1;
	sfx_play(power ? "werewolf_power" : "werewolf_attack", 0.9f, 0.0f);
}

int beast_fed_on(int b, Vitals *v) {
	if (!active)
		return 0;
	for (int i = 0; i < fed_count; i++)
		if (fed[i] == b)
			return 0;
	if (fed_count < (int)(sizeof(fed) / sizeof(fed[0])))
		fed[fed_count++] = b;
	left_s += feed_more_s, warned = 0;
	v->health = fminf(v->max_health, v->health + feed_health), v->shown_for = 0.0f; // (WerewolfFeed: health back)
	sfx_play("werewolf_feed", 1.0f, 0.0f);
	game_log("beast: fed (%.0f s left)", left_s);
	return 1;
}

void beast_pass_hours(float hours) {
	if (cooldown_h > 0.0f && (cooldown_h -= hours) < 0.0f)
		cooldown_h = 0.0f;
}

void beast_rates(Vitals *v) {
	if (active && no_heal)
		v->rate_mult[0] = 0.0f; // (AbWerewolf: health doesn't come back but by feeding)
}

int beast_update(float dt, const float feet[3], float heading_deg, float speed, int place, Vitals *v) {
	(void)place;
	if (!active)
		return 0;
	if ((left_s -= dt) <= 0.0f) {
		beast_revert(v);
		return 1;
	}
	if (!warned && left_s < warn_s) {
		warned = 1;
		ui_notice("Your beast form is waning", 3.0f); // (Skyrim's warning: 15 s before)
	}
	if (kind < 0)
		kind = actors_beast_kind("werewolf", "werewolfbeast");
	if (body < 0 && kind >= 0) {
		body = actors_beast_spawn(kind);
		if (body >= 0)
			actors_beast_scale(body, height);
		game_log("beast: its body %d (kind %d) at %.1f %.1f %.1f", body, kind, feet[0], feet[1], feet[2]);
	}
	if (body < 0)
		return 0;
	anim_t += dt;
	const char *clip = speed > RUN_FROM_MS ? "run" : "idle";
	float t = anim_t, clip_speed = 0.0f;
	if (strike_t >= 0.0f) { // (a blow: its clip through, then the gait again)
		strike_t += dt;
		const float len = strike_power ? POWER_S : STRIKE_S;
		if (strike_t < len)
			clip = strike_power ? "power" : strike_side ? "attack2" : "attack", t = strike_t;
		else
			strike_t = -1.0f;
	} else if (speed > RUN_FROM_MS && actors_beast_clip(kind, "run", &clip_speed) > 0.0f && clip_speed > 0.5f) {
		t = anim_t * speed / clip_speed; // (its strides paced to the ground covered)
	}
	actors_beast_pose(body, feet[0], feet[1], feet[2], -heading_deg * (float)M_PI / 180.0f, clip, t, NULL, 0.0f, 0.0f, dt);
	return 0;
}

void beast_save(float out[2]) {
	out[0] = (float)known, out[1] = cooldown_h;
}

void beast_load(const float in[2]) {
	unspawn();
	known = in[0] > 0.5f, cooldown_h = in[1] > 0.0f ? in[1] : 0.0f;
	active = readied = 0;
}
