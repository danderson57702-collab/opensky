#include "brawl.h"

#include <math.h>
#include <stdlib.h>

#include "actors.h"
#include "gamelog.h"
#include "sfx.h"
#include "ui.h"

#define PUNCH_EVERY_S 1.5f   // their swing this often...
#define PUNCH_LANDS_S 0.4f   // ...landing this far into it...
#define PUNCH_REACH_M 2.2f   // ...if you're this near...
#define PUNCH_DAMAGE 7.0f    // ...this hard (Skyrim's fists: a Nord's, a little more with level)
#define BEATEN_SHARE 0.2f    // beaten: down to this share of your health (it stops there)
#define HIT_RADIUS_M 0.5f

static int who = -1, result = BRAWL_NONE, their_lvl;
static float their_health, punch_t, land_t = -1.0f;
static int side, spar;

void brawl_start_spar(int actor, int their_level) {
	brawl_start(actor, their_level);
	spar = 1; // (after brawl_start: it clears it)
}

int brawl_spar(void) {
	return brawl_active() && spar;
}

void brawl_start(int actor, int their_level) {
	spar = 0;
	who = actor, their_lvl = their_level > 1 ? their_level : 1;
	their_health = 50.0f + 5.0f * their_lvl;
	punch_t = 1.0f, land_t = -1.0f, result = BRAWL_ON;
	actors_script(actor, 1);
	actors_script_follow(actor, 1);
	game_log("brawl: begun (their health %.0f)", their_health);
}

int brawl_active(void) {
	return who >= 0 && result == BRAWL_ON;
}

int brawl_who(void) {
	return who;
}

int brawl_strike(const float eye[3], const float dir[3], float reach, float damage) {
	float at[3];
	if (!brawl_active() || !actors_where(who, at))
		return 0;
	const float c[3] = {at[0] - eye[0], at[1] + 1.2f - eye[1], at[2] - eye[2]};
	const float along = c[0] * dir[0] + c[1] * dir[1] + c[2] * dir[2];
	const float off = sqrtf(fmaxf(0.0f, c[0] * c[0] + c[1] * c[1] + c[2] * c[2] - along * along));
	if (along < 0.0f || along > reach + HIT_RADIUS_M || off > 0.8f)
		return 0;
	their_health -= damage;
	sfx_play_at(spar ? "wpn_block_hit" : "wpn_hit_unarmed", at, 20.0f); // (a training bout: steel on their guard)
	game_log("brawl: you hit them for %.0f (%.0f left)", damage, their_health);
	return 1;
}

static void end(int how) {
	result = how;
	actors_script_follow(who, 0);
	actors_script_clip(who, "", 0);
	game_log("brawl: over - %s", how == BRAWL_WON ? "you won" : how == BRAWL_LOST ? "you lost" : "broken off");
}

int brawl_update(float dt, const float feet[3], float *health, float max_health, int armed) {
	if (who < 0)
		return BRAWL_NONE;
	if (result != BRAWL_ON) { // (told once, then forgotten: their script let go by the caller)
		const int r = result;
		result = BRAWL_NONE, who = -1;
		return r;
	}
	if (armed) {
		end(BRAWL_BROKEN);
		return BRAWL_ON;
	}
	if (their_health <= 0.0f) {
		end(BRAWL_WON);
		return BRAWL_ON;
	}
	float at[3];
	if (!actors_where(who, at))
		return BRAWL_ON;
	const float d = hypotf(at[0] - feet[0], at[2] - feet[2]);
	if (d < 4.0f)
		actors_script_face(who, atan2f(-(feet[0] - at[0]), -(feet[2] - at[2])));
	if ((punch_t -= dt) <= 0.0f && d < PUNCH_REACH_M + 0.6f) {
		punch_t = PUNCH_EVERY_S * (0.8f + 0.4f * (float)rand() / RAND_MAX);
		actors_script_clip(who, spar ? "w1h_power" : (side ^= 1) ? "h2h_atk_r" : "h2h_atk_l", 1);
		land_t = PUNCH_LANDS_S;
	}
	if (land_t >= 0.0f && (land_t -= dt) < 0.0f) {
		actors_script_clip(who, "", 0);
		if (d < PUNCH_REACH_M) {
			*health -= PUNCH_DAMAGE * (1.0f + their_lvl / 20.0f);
			sfx_play("wpn_hit_unarmed", 0.8f, 0.0f);
		}
	}
	if (*health <= max_health * BEATEN_SHARE) { // (beaten: down, not dead)
		*health = max_health * BEATEN_SHARE;
		end(BRAWL_LOST);
	}
	return BRAWL_ON;
}
