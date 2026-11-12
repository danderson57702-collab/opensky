// Shouts (real pack/shouts.txt): the four load with Skyrim's words and numbers; a tap is one
// word, a full hold all three (Skyrim's fShoutTime1 / 2); the recovery holds the next back
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "shouts.h"

// (what the shouts call: counted, not done)
static int pushes, breaths, said;
int creatures_shout_push(const float *f, const float *d, float r, float c, float p, float dmg, int k) {
	(void)f, (void)d, (void)r, (void)c, (void)p, (void)dmg, (void)k;
	return ++pushes;
}
int creatures_shout_breath(const float *f, const float *d, float r, float c, float dmg, float s, float sl, float st) {
	(void)f, (void)d, (void)r, (void)c, (void)dmg, (void)s, (void)sl, (void)st;
	return ++breaths;
}
void sfx_play(const char *n, float v, float p) { (void)n, (void)v, (void)p; }
void spellfx_wave(const float *f, const float *d, float r, float s, int e, float k) { (void)f, (void)d, (void)r, (void)s, (void)e, (void)k; }
void spellfx_shout(const float *m, const float *d, float r, int e, float k) { (void)m, (void)d, (void)r, (void)e, (void)k; }
void traps_fire_at(const float *p, float r) { (void)p, (void)r; }
void ui_notice(const char *t, float s) { (void)t, (void)s; }
int voice_play(const char *p) { (void)p; return ++said; }
int voice_playing(void) { return 0; }
int creatures_alert_at(const float pos[3], float radius, const float source[3]) { (void)pos, (void)radius, (void)source; return 0; }
int creatures_dragonrend(float seconds) { (void)seconds; return 0; }
int creatures_place_dragon(const char *kind_name, const float at[3], float heading, int calm) { (void)kind_name, (void)at, (void)heading, (void)calm; return -1; }
void creatures_set_name(int i, const char *name) { (void)i, (void)name; }
int creatures_shout_effect(int effect, const float from[3], const float dir[3], float range, float cone_cos, float magnitude, float seconds) {
	(void)effect, (void)from, (void)dir, (void)range, (void)cone_cos, (void)magnitude, (void)seconds; return 0;
}
int creatures_summon_spirit(const char *kind_name, const float at[3], float seconds) { (void)kind_name, (void)at, (void)seconds; return -1; }
void game_log(const char *fmt, ...) { (void)fmt; }
int quests_calls_dragon(void) { return 0; }
int sfx_play_at(const char *e, const float pos[3], float m) { (void)e, (void)pos, (void)m; return 0; }

static int hold(int frames) { // held this many frames, then let go: the words shouted
	const float eye[3] = {0, 1.7f, 0}, look[3] = {0, 0, -1}, feet[3] = {0, 0, 0};
	ShoutFx fx;
	for (int f = 0; f < frames + 4; f++) {
		shouts_update(1.0f / 60.0f, f < frames, eye, look, feet, &fx);
		if (fx.shouted)
			return fx.shouted;
	}
	return 0;
}

int main(void) {
	shouts_init("../../pack/");
	assert(shouts_count() >= 4);
	assert(!strcmp(shouts_name(0), "Unrelenting Force"));
	assert(!strcmp(shouts_words(0, 3), "Fus Ro Dah"));
	shouts_set_voice("NordRace", 0);
	shouts_set_known_all(3);
	shouts_equip(0);
	assert(hold(1) == 1 && pushes == 1 && said >= 1);          // a tap: "Fus!"
	assert(hold(1) == 0);                                       // recovering (15 s)
	shouts_set_fast_recovery(1);
	for (int f = 0; f < 60 * 4; f++)
		hold(0);
	assert(hold(70) == 3 && pushes == 2);                       // held: "Fus... Ro Dah!"
	shouts_equip(1);
	for (int f = 0; f < 60 * 4; f++)
		hold(0);
	assert(hold(20) == 2 && breaths == 1);                      // 0.33 s: two words of Fire Breath
	printf("shouts_test: all passed\n");
	return 0;
}
