#include "talk.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>

#include "actors.h"
#include "aim.h"
#include "fileio.h"
#include "music.h"
#include "ui.h"
#include "voice.h"
#include "gamelog.h"

#define SKTK_VERSION 1
#define MAX_TALKERS 1200
#define MAX_RESPONSES 4
enum { KIND_HELLO, KIND_IDLE, KIND_GREETING };

#define TALK_RANGE_M 3.5f       // talk button: someone this close...
#define PERSON_AIM_M 0.55f      // ...and the crosshair this near their chest
#define HELLO_RANGE_M 2.6f      // walking up to someone: they say hello
#define HELLO_COOLDOWN_S 75.0f  // per NPC
#define QUIET_AFTER_S 6.0f      // between any two lines
#define HEAR_FULL_M 2.5f        // full volume within this...
#define HEAR_MAX_M 20.0f        // ...fading to silence here (the line stops beyond)
#define MUSIC_UNDER_SPEECH 0.35f
#define SUBTITLE_LINGER_S 1.2f

typedef struct {
	char path[96];
	char *text;
} Response;

typedef struct {
	int kind, count;
	Response *r; // (count of them)
} Line;
#define SCENE_MAX 12 // a quest's scene: this many responses at most

typedef struct {
	char name[64];
	Line *lines;
	int count;
	int last;         // last line said (not repeated twice in a row)
	float cooldown;   // until the next passing hello
} Talker;

static Talker talkers[MAX_TALKERS];
static int speaker = -1, response;
static const Line *line;
static float quiet, linger;
static unsigned rng = 12345u;

static unsigned next_random(void) {
	rng = rng * 1664525u + 1013904223u;
	return rng >> 8;
}

static void load_talker(const char *pack_dir, const char *id, Talker *t) {
	char path[256];
	snprintf(path, sizeof(path), "%sactors/%s.sktalk", pack_dir, id);
	long size = 0;
	uint8_t *d = file_read_all(path, &size);
	t->last = -1;
	if (!d)
		return;
	const uint8_t *p = d, *end = d + size;
#define NEED(n) \
	if (p + (n) > end) \
		goto bad
	NEED(9);
	if (memcmp(p, "SKTK", 4) || *(const uint16_t *)(p + 4) != SKTK_VERSION)
		goto bad;
	int count = p[6] | p[7] << 8, name_len = p[8];
	p += 9;
	NEED(name_len);
	snprintf(t->name, sizeof(t->name), "%.*s", name_len, (const char *)p);
	p += name_len;
	t->lines = calloc(count ? count : 1, sizeof(Line));
	if (!t->lines)
		goto bad;
	for (int i = 0; i < count; i++) {
		Line *l = &t->lines[i];
		NEED(2);
		l->kind = p[0];
		int n = p[1];
		p += 2;
		l->r = calloc(n < MAX_RESPONSES ? (n ? n : 1) : MAX_RESPONSES, sizeof(Response));
		if (!l->r)
			goto bad;
		for (int k = 0; k < n; k++) {
			NEED(1);
			int plen = *p++;
			NEED(plen + 2);
			Response *r = k < MAX_RESPONSES ? &l->r[k] : NULL;
			if (r)
				snprintf(r->path, sizeof(r->path), "%.*s", plen, (const char *)p);
			p += plen;
			int tlen = p[0] | p[1] << 8;
			p += 2;
			NEED(tlen);
			if (r) {
				r->text = malloc(tlen + 1);
				if (r->text) {
					memcpy(r->text, p, tlen);
					r->text[tlen] = 0;
				}
			}
			p += tlen;
		}
		l->count = n < MAX_RESPONSES ? n : MAX_RESPONSES;
		t->count++;
	}
	free(d);
	return;
bad:
	sceClibPrintf("talk: bad file %s\n", path);
	free(d);
#undef NEED
}

void talk_init(const char *pack_dir) {
	int lines = 0;
	for (int i = 0; i < actors_count() && i < MAX_TALKERS; i++) {
		load_talker(pack_dir, actors_id(i), &talkers[i]);
		talkers[i].cooldown = 10.0f + (next_random() % 1000) * 0.02f;
		lines += talkers[i].count;
	}
	int speakers = 0;
	for (int i = 0; i < actors_count() && i < MAX_TALKERS; i++)
		speakers += talkers[i].count > 0;
	game_log("talk: %d lines, %d of %d people with something to say", lines, speakers, actors_count());
}

// A random line of one kind (not the last one said), or NULL
static const Line *pick(Talker *t, int kind) {
	int options[64], n = 0;
	for (int i = 0; i < t->count && n < 64; i++)
		if (t->lines[i].kind == kind && t->lines[i].count && (i != t->last || n == 0))
			options[n++] = i;
	if (!n)
		return NULL;
	int i = options[next_random() % (unsigned)n];
	t->last = i;
	return &t->lines[i];
}

static void say_response(void) {
	const Response *r = &line->r[response];
	voice_play(r->path);
	ui_subtitle(talkers[speaker].name, r->text ? r->text : "");
}

static int start_line(int who, int kind) {
	Talker *t = &talkers[who];
	const Line *l = pick(t, kind);
	if (!l && kind == KIND_GREETING)
		l = pick(t, KIND_HELLO); // nothing personal to say: a plain hello
	static int logged;
	if (logged++ < 20) // (the first few: to see people speak)
		game_log("talk: %s %s (%s)", talk_name(who), kind == KIND_GREETING ? "greets you" : "says hello",
		         l ? "a line" : "nothing to say");
	if (!l)
		return 0;
	speaker = who, line = l, response = 0;
	say_response();
	music_duck(MUSIC_UNDER_SPEECH);
	t->cooldown = HELLO_COOLDOWN_S;
	return 1;
}

static void stop_line(void) {
	speaker = -1, line = NULL;
	linger = SUBTITLE_LINGER_S;
	quiet = QUIET_AFTER_S;
	music_duck(1.0f);
}

int talk_aim(const float eye[3], const float dir[3], float *along) {
	int best = -1, n = actors_count() < MAX_TALKERS ? actors_count() : MAX_TALKERS;
	float best_t = 1e9f;
	for (int i = 0; i < n; i++) {
		float pos[3], t;
		if (!actors_shown(i, pos))
			continue;
		pos[1] += actors_corpse(i) ? 0.25f : actors_asleep(i) ? 0.6f : 1.2f; // (the chest; lying in bed, lower; dead, on the ground)
		if (aim_hits(eye, dir, pos, PERSON_AIM_M, TALK_RANGE_M, &t) && t < best_t)
			best = i, best_t = t;
	}
	if (best >= 0)
		*along = best_t;
	return best;
}

int talk_named(int i) {
	return i >= 0 && i < MAX_TALKERS && talkers[i].name[0];
}

const char *talk_name(int i) {
	return i >= 0 && i < MAX_TALKERS && talkers[i].name[0] ? talkers[i].name : actors_id(i);
}

int talk_update(float dt_s, float px, float pz, float yaw_deg, int talk_to) {
	int n = actors_count() < MAX_TALKERS ? actors_count() : MAX_TALKERS;
	for (int i = 0; i < n; i++)
		if (talkers[i].cooldown > 0.0f)
			talkers[i].cooldown -= dt_s;
	if (quiet > 0.0f)
		quiet -= dt_s;
	if (linger > 0.0f && (linger -= dt_s) <= 0.0f && speaker < 0)
		ui_subtitle(NULL, NULL);

	const float yaw = yaw_deg * (float)M_PI / 180.0f;
	const float right_x = cosf(yaw), right_z = sinf(yaw);

	if (speaker >= 0) {
		float pos[3];
		int visible = actors_shown(speaker, pos);
		float dx = pos[0] - px, dz = pos[2] - pz, d = hypotf(dx, dz);
		if (!visible || d > HEAR_MAX_M) {
			voice_stop();
			ui_subtitle(NULL, NULL);
			stop_line();
			return 0;
		}
		float gain = d <= HEAR_FULL_M ? 1.0f : 1.0f - (d - HEAR_FULL_M) / (HEAR_MAX_M - HEAR_FULL_M);
		float pan = d > 0.3f ? (dx * right_x + dz * right_z) / d : 0.0f;
		voice_set_mix(gain, pan);
		// Words open the jaw (the level leads the buffer being heard by ~1 buffer)
		float open = voice_level();
		actors_set_mouth(speaker, open > 0.08f ? 0.25f + 0.75f * open : open * 2.0f);
		if (d < 8.0f)
			actors_attend(speaker, px, pz, 1.5f); // keeps facing you while talking
		if (!voice_playing()) {
			if (++response < line->count)
				say_response();
			else
				stop_line();
		}
		return 0;
	}

	// The talk button on someone (the crosshair's pick): wake them if they're asleep, greet
	if (talk_to >= 0) {
		if (actors_asleep(talk_to))
			actors_wake(talk_to, px, pz); // (sleepers can be woken by talking to them)
		if (start_line(talk_to, KIND_GREETING)) {
			actors_attend(talk_to, px, pz, 2.0f);
			return 1;
		}
		return 0;
	}

	// Walking up to someone: a passing hello (not too often)
	if (quiet > 0.0f)
		return 0;
	for (int i = 0; i < n; i++) {
		float pos[3];
		// (not those a script has - Irileth in the fight, a follower at your side: their a line of theirs
		// at every step was nonsense)
		if (talkers[i].cooldown > 0.0f || !actors_shown(i, pos) || actors_asleep(i) || actors_scripted(i))
			continue;
		if (hypotf(pos[0] - px, pos[2] - pz) < HELLO_RANGE_M) {
			if (start_line(i, KIND_HELLO))
				actors_attend(i, px, pz, 2.0f);
			return 0;
		}
	}
	return 0;
}

static Response scene_r[SCENE_MAX];
static Line scene_line = {KIND_GREETING, 0, scene_r};

int talk_scene(int who, const char *const *paths, const char *const *texts, int n) {
	if (who < 0 || who >= MAX_TALKERS || n <= 0)
		return 0;
	if (speaker >= 0) // (whatever they were saying: cut off)
		voice_stop();
	scene_line.count = n < SCENE_MAX ? n : SCENE_MAX;
	for (int i = 0; i < scene_line.count; i++) {
		snprintf(scene_r[i].path, sizeof(scene_r[i].path), "%s", paths[i]);
		scene_r[i].text = (char *)texts[i];
	}
	speaker = who, line = &scene_line, response = 0, quiet = 0.0f;
	say_response();
	music_duck(MUSIC_UNDER_SPEECH);
	talkers[who].cooldown = HELLO_COOLDOWN_S;
	return 1;
}

void talk_hush(float seconds) {
	quiet = quiet > seconds ? quiet : seconds;
}

int talk_speaking(void) {
	return speaker;
}
