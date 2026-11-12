#include "shouts.h"
#include "quests.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "creatures.h"
#include "fileio.h"
#include "gamelog.h"
#include "sfx.h"
#include "spellfx.h"
#include "traps.h"
#include "ui.h"
#include "voice.h"
#include "gametext.h"

#define SHOUT_TIME_1 0.2f   // Skyrim's fShoutTime1: held this long, a second word
#define SHOUT_TIME_2 0.9f   // ... fShoutTime2: the third
#define FAST_RECOVERY_S 3.0f
#define FIRST_LINE_MAX_S 0.9f // "Fus..." - the rest follows when it's said (or after this)
#define MAX_VOICE_LINES 1024

#define SHOUT_DESC_LEN 160
typedef struct {
	char key[12], name[32];
	char word[3][12], meaning[3][16];
	float recovery[3], push[3], damage[3], slow, seconds;
	float duration[3], magnitude[3]; // each word's spell: its longest duration, its first effect's magnitude
	char summon[3][24]; // each word's creature (Call of Valor's heroes), "" none
	char desc[SHOUT_DESC_LEN]; // Skyrim's words for it (a line of theirs)
	int kind, known;
} Shout;

typedef struct {
	char key[12], voice[24], line[4], path[64];
} VoiceLine;

static Shout shouts[SHOUT_MAX];
static int shout_count;
static VoiceLine lines[MAX_VOICE_LINES];
static int line_count;
static char voice_type[24] = "maleeventoned";
static int female_voice;
static int equipped = -1;
static float recovery_left, hold_s;
static int holding, fast_recovery;
static float recovery_mult = 1.0f; // (Fortify Shouts)
// A shout under way: its words, the first line said, the rest (and the effect) to follow
static int pending = -1, pending_words;
static float pending_s;

static int split(char *line, char **f, int max) {
	int n = 0;
	for (char *p = line; n < max;) {
		f[n++] = p;
		char *bar = strchr(p, '|');
		if (!bar)
			break;
		*bar = 0, p = bar + 1;
	}
	return n;
}

void shouts_init(const char *pack_dir) {
	shout_count = line_count = 0;
	char path[256];
	snprintf(path, sizeof(path), "%sshouts.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	static const char *const kinds[SHOUT_KINDS] = {"force", "fire", "frost", "sprint", "ethereal", "disarm", "iceform",
	                                               "marked", "slowtime", "kynespeace", "animal", "fury", "aura",
	                                               "throwvoice", "drain", "clearskies", "stormcall", "dismay", "valor",
	                                               "calldragon", "dragonrend"};
	static char descs[SHOUT_KINDS][SHOUT_DESC_LEN]; // (Skyrim's words for each, its D line: before its S line)
	memset(descs, 0, sizeof(descs));
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[32];
		const int n = split(line, f, 32);
		if (f[0][0] == 'D' && n >= 3) {
			for (int k = 0; k < SHOUT_KINDS; k++)
				if (!strcmp(f[1], kinds[k]))
					snprintf(descs[k], sizeof(descs[k]), "%s", f[2]);
			continue;
		}
		if (f[0][0] == 'S' && n >= 20 && shout_count < SHOUT_MAX) {
			Shout *s = &shouts[shout_count];
			memset(s, 0, sizeof(*s));
			s->kind = -1;
			for (int k = 0; k < SHOUT_KINDS; k++)
				if (!strcmp(f[1], kinds[k]))
					s->kind = k;
			if (s->kind < 0)
				continue;
			snprintf(s->key, sizeof(s->key), "%s", f[1]);
			snprintf(s->name, sizeof(s->name), "%s", f[2]);
			for (int w = 0; w < 3; w++) {
				snprintf(s->word[w], sizeof(s->word[w]), "%s", f[3 + w]);
				snprintf(s->meaning[w], sizeof(s->meaning[w]), "%s", f[6 + w]);
				s->recovery[w] = (float)atof(f[9 + w]);
				s->push[w] = (float)atof(f[12 + w]);
				s->damage[w] = (float)atof(f[15 + w]);
			}
			s->slow = (float)atof(f[18]), s->seconds = (float)atof(f[19]);
			for (int w = 0; w < 3 && n >= 26; w++)
				s->duration[w] = (float)atof(f[20 + w]), s->magnitude[w] = (float)atof(f[23 + w]);
			if (n >= 27) { // (each word's creature: "a,b,c", "-" none)
				const char *t = f[26]; // (not strtok: the lines' own is under way)
				for (int w = 0; w < 3 && *t; w++) {
					const size_t len = strcspn(t, ",");
					if (len && !(len == 1 && t[0] == '-'))
						snprintf(s->summon[w], sizeof(s->summon[w]), "%.*s", (int)len, t);
					t += len + (t[len] == ',');
				}
			}
			s->known = 1;
			snprintf(s->desc, sizeof(s->desc), "%s", descs[s->kind]);
			shout_count++;
		} else if (f[0][0] == 'V' && n >= 5 && line_count < MAX_VOICE_LINES) {
			VoiceLine *v = &lines[line_count++];
			snprintf(v->key, sizeof(v->key), "%s", f[1]);
			snprintf(v->voice, sizeof(v->voice), "%s", f[2]);
			snprintf(v->line, sizeof(v->line), "%s", f[3]);
			snprintf(v->path, sizeof(v->path), "%s", f[4]);
		}
	}
	free(buf);
}

int shouts_count(void) {
	return shout_count;
}

const char *shouts_name(int i) {
	return i >= 0 && i < shout_count ? shouts[i].name : "";
}

const char *shouts_summon(int i, int word) {
	return i >= 0 && i < shout_count && word >= 0 && word < 3 ? shouts[i].summon[word] : "";
}

const char *shouts_words(int i, int words) {
	static char text[96];
	text[0] = 0;
	if (i < 0 || i >= shout_count)
		return text;
	int n = 0;
	for (int w = 0; w < words && w < 3; w++)
		n += snprintf(text + n, sizeof(text) - n, "%s%s", w ? " " : "", shouts[i].word[w]);
	return text;
}

int shouts_known(int i) {
	return i >= 0 && i < shout_count ? shouts[i].known : 0;
}

// The words a soul count gives: each shout's first, then the second of each in turn, then the
// third (Skyrim's word walls teach them, a dragon's soul unlocks them: here the souls do both)
static int words_for(int i, int souls) {
	const int w = 1 + (souls > i) + (souls > shout_count + i);
	return w > 3 ? 3 : w;
}

void shouts_set_known_souls(int souls) {
	for (int i = 0; i < shout_count; i++)
		shouts[i].known = words_for(i, souls);
}

const char *shouts_soul_unlocks(int before, int after) {
	static char text[64];
	for (int i = 0; i < shout_count; i++)
		if (words_for(i, after) > words_for(i, before)) {
			const int w = words_for(i, after);
			snprintf(text, sizeof(text), "%s - %s", shouts[i].name, shouts[i].word[w - 1]);
			return text;
		}
	return NULL;
}

// Words: learned at a word wall (learned), then unlocked with a dragon's soul (known) - Skyrim's
static int learned_words[SHOUT_MAX], souls_pool;

void shouts_new_character(void) {
	for (int i = 0; i < shout_count; i++)
		shouts[i].known = 0, learned_words[i] = 0;
	souls_pool = 0, equipped = -1;
}

const char *shouts_learn_word(const char *word) {
	static char text[96];
	for (int i = 0; i < shout_count; i++)
		for (int k = 0; k < 3; k++)
			if (!strcasecmp(shouts[i].word[k], word)) {
				if (learned_words[i] > k)
					return NULL; // (known already)
				learned_words[i] = k + 1;
				snprintf(text, sizeof(text), "Word of Power learned: %s, %s (%s)", shouts[i].word[k], shouts[i].meaning[k],
				         shouts[i].name);
				return text;
			}
	return NULL;
}

const char *shouts_next_word(const char *word) {
	for (int i = 0; i < shout_count; i++)
		for (int k = 0; k < 3; k++)
			if (!strcasecmp(shouts[i].word[k], word))
				return learned_words[i] < 3 ? shouts[i].word[learned_words[i]] : NULL;
	return NULL; // (a shout not here yet)
}

const char *shouts_desc(int i) {
	return i >= 0 && i < shout_count ? shouts[i].desc : "";
}

const char *shouts_word(int i, int word) {
	return i >= 0 && i < shout_count && word >= 0 && word < 3 ? shouts[i].word[word] : "";
}

const char *shouts_meaning(int i, int word) {
	return i >= 0 && i < shout_count && word >= 0 && word < 3 ? shouts[i].meaning[word] : "";
}

int shouts_learned(int i) {
	return i >= 0 && i < shout_count ? (learned_words[i] > shouts[i].known ? learned_words[i] : shouts[i].known) : 0;
}

int shouts_unlock(int i) {
	if (i < 0 || i >= shout_count || shouts[i].known >= learned_words[i] || souls_pool <= 0)
		return 0;
	shouts[i].known++, souls_pool--;
	return 1;
}

int shouts_souls(void) {
	return souls_pool;
}

void shouts_add_souls(int n) {
	souls_pool += n;
}

unsigned shouts_state(void) { // (saving: per shout, words unlocked and learned - 2 bits each)
	unsigned s = 0;
	for (int i = 0; i < shout_count && i < 4; i++)
		s |= (unsigned)(shouts[i].known & 3) << (i * 4) | (unsigned)(learned_words[i] & 3) << (i * 4 + 2);
	return s;
}

// A word is unlocked only once it's learned (Skyrim's): a save made while debug controls gave every
// word unlocked all of them, learned or not - loaded, only those learned stay
static void unlocked_only_learned(void) {
	int fixed = 0;
	for (int i = 0; i < shout_count; i++)
		if (shouts[i].known > learned_words[i])
			shouts[i].known = learned_words[i], fixed++;
	if (fixed)
		game_log("shouts: %d shouts had words unlocked that were never learned (debug controls) - put back", fixed);
}

void shouts_set_state(unsigned s, int souls) {
	for (int i = 0; i < shout_count; i++) // (the rest: none - not the words of the game played before the load)
		shouts[i].known = i < 4 ? s >> (i * 4) & 3 : 0, learned_words[i] = i < 4 ? s >> (i * 4 + 2) & 3 : 0;
	unlocked_only_learned();
	souls_pool = souls < 0 ? 0 : souls;
	if (equipped >= 0 && shouts[equipped].known <= 0)
		equipped = -1;
}

// (by key, not number: a pack's shouts in another order still load right)
static const char *const SAVE_KEYS[SHOUT_SAVE_SLOTS] = {
	"force", "fire", "frost", "sprint", "ethereal", "disarm", "iceform", "marked", "slowtime", "kynespeace", "animal", "fury",
	"aura", "throwvoice", "drain", "clearskies", // (version 15's sixteen; the rest in version 17's second sixteen)
	"stormcall", "dismay", "valor", "calldragon", "dragonrend"};

void shouts_save_words(uint8_t out[SHOUT_SAVE_SLOTS]) {
	memset(out, 0, SHOUT_SAVE_SLOTS);
	for (int i = 0; i < shout_count; i++)
		for (int k = 0; k < SHOUT_SAVE_SLOTS && SAVE_KEYS[k]; k++)
			if (!strcmp(shouts[i].key, SAVE_KEYS[k]))
				out[k] = (uint8_t)((shouts[i].known & 3) | (learned_words[i] & 3) << 2);
}

void shouts_load_words(const uint8_t in[SHOUT_SAVE_SLOTS]) {
	for (int i = 0; i < shout_count; i++)
		for (int k = 0; k < SHOUT_SAVE_SLOTS && SAVE_KEYS[k]; k++)
			if (!strcmp(shouts[i].key, SAVE_KEYS[k]))
				shouts[i].known = in[k] & 3, learned_words[i] = in[k] >> 2 & 3;
	unlocked_only_learned();
	if (equipped >= 0 && shouts[equipped].known <= 0)
		equipped = -1;
}

void shouts_set_known_all(int words) {
	for (int i = 0; i < shout_count; i++)
		shouts[i].known = words < 1 ? 1 : words > 3 ? 3 : words;
}

int shouts_equipped(void) {
	return equipped;
}

void shouts_equip(int i) {
	equipped = i >= 0 && i < shout_count ? i : -1;
}

void shouts_reset_recovery(void) {
	recovery_left = 0.0f;
}

float shouts_recovery_left(void) {
	return recovery_left;
}

int shouts_holding(void) {
	if (!holding || equipped < 0)
		return 0;
	const int words = 1 + (hold_s >= SHOUT_TIME_1) + (hold_s >= SHOUT_TIME_2);
	return words < shouts[equipped].known ? words : shouts[equipped].known;
}

void shouts_set_recovery_mult(float m) {
	recovery_mult = m;
}

void shouts_set_fast_recovery(int on) {
	fast_recovery = on;
	if (on && recovery_left > FAST_RECOVERY_S)
		recovery_left = FAST_RECOVERY_S;
}

const char *shouts_voice(void) {
	return voice_type;
}

void shouts_set_voice(const char *race_eid, int female) {
	const char *kind = "eventoned";
	if (!strcmp(race_eid, "HighElfRace") || !strcmp(race_eid, "DarkElfRace") || !strcmp(race_eid, "WoodElfRace"))
		kind = "elfhaughty";
	else if (!strcmp(race_eid, "OrcRace"))
		kind = "orc";
	else if (!strcmp(race_eid, "KhajiitRace"))
		kind = "khajiit";
	else if (!strcmp(race_eid, "ArgonianRace"))
		kind = "argonian";
	snprintf(voice_type, sizeof(voice_type), "%s%s", female ? "female" : "male", kind);
	female_voice = female;
}

// A line (1a, 1b, 2, 3) of key in the player's voice; else one of their sex's (Battle Cry's
// "Aaah!" is a Nord's)
static void say(const char *key, const char *line) {
	const VoiceLine *other = NULL;
	for (int i = 0; i < line_count; i++) {
		const VoiceLine *v = &lines[i];
		if (strcmp(v->key, key) || strcmp(v->line, line))
			continue;
		if (!strcmp(v->voice, voice_type)) {
			voice_play(v->path);
			return;
		}
		if (!other && !strncmp(v->voice, female_voice ? "female" : "male", female_voice ? 6 : 4))
			other = v;
	}
	if (other)
		voice_play(other->path);
}

void shouts_power_yell(const char *key) {
	say(key, "1a");
}

// The shout's effect, its words said: on what's before you (or you, a sprint)
static void shout(const Shout *s, int words, const float eye[3], const float look[3], const float feet[3], ShoutFx *fx) {
	const int w = words - 1;
	float dir[3] = {look[0], 0.0f, look[2]};
	const float fl = hypotf(dir[0], dir[2]);
	dir[0] = fl > 1e-3f ? dir[0] / fl : 0.0f, dir[2] = fl > 1e-3f ? dir[2] / fl : -1.0f;
	const float mouth[3] = {eye[0] + look[0] * 0.4f, eye[1] - 0.1f, eye[2] + look[2] * 0.4f};
	char sound[24];
	fx->shouted = words, fx->kind = s->kind;
	game_log("shouts: %s, %d words", s->name, words);
	switch (s->kind) {
	case SHOUT_FORCE: { // Unrelenting Force: a cone of force; two words throw them down, three far
		static const float range[3] = {7.0f, 10.0f, 14.0f};
		creatures_shout_push(mouth, dir, range[w], 0.8f, s->push[w], s->damage[w], words >= 2);
		spellfx_wave(mouth, look, range[w], 0.4f, ELEMENT_FORCE, words / 3.0f);
		spellfx_shout(mouth, dir, range[w], ELEMENT_FORCE, words / 3.0f);
		snprintf(sound, sizeof(sound), "shout_force_%d", words);
		sfx_play(sound, 1.0f, 0.0f);
		break;
	}
	case SHOUT_FIRE:
	case SHOUT_FROST: { // a breath: fire (all at once) or frost (over seconds, slowing)
		static const float range[3] = {6.0f, 8.0f, 10.0f};
		const int fire = s->kind == SHOUT_FIRE;
		creatures_shout_breath(mouth, dir, range[w], 0.85f, s->damage[w], fire ? 1.0f : fmaxf(s->seconds, 1.0f),
		                       fire ? 0.0f : s->seconds, s->push[w]);
		spellfx_wave(mouth, look, range[w], 0.3f, fire ? ELEMENT_FIRE : ELEMENT_FROST, words / 3.0f);
		spellfx_shout(mouth, dir, range[w], fire ? ELEMENT_FIRE : ELEMENT_FROST, words / 3.0f);
		if (fire) { // (oil in the way catches)
			const float mid[3] = {mouth[0] + look[0] * range[w] * 0.5f, mouth[1] + look[1] * range[w] * 0.5f,
			                      mouth[2] + look[2] * range[w] * 0.5f};
			traps_fire_at(mid, range[w] * 0.5f);
		}
		snprintf(sound, sizeof(sound), "shout_%s_%d", fire ? "fire" : "frost", words);
		sfx_play(sound, 1.0f, 0.0f);
		break;
	}
	case SHOUT_ETHEREAL: // Become Ethereal: a ghost - nothing harms you, you harm nothing
		fx->ethereal_s = s->duration[w] > 0.0f ? s->duration[w] : 8.0f + 5.0f * w;
		sfx_play("shout_frost_1", 0.6f, 0.0f);
		spellfx_wave(mouth, look, 2.0f, 0.4f, ELEMENT_FROST, words / 3.0f);
		break;
	case SHOUT_SLOWTIME: // Slow Time: the world slows about you
		fx->slow_s = s->duration[w] > 0.0f ? s->duration[w] : 8.0f + 4.0f * w;
		fx->slow_mult = s->magnitude[w] > 0.0f && s->magnitude[w] < 1.0f ? s->magnitude[w] : 0.3f;
		sfx_play("shout_force_1", 0.5f, 0.0f);
		break;
	case SHOUT_FURY: // Elemental Fury: the air's speed in your weapon
		fx->fury_s = s->duration[w] > 0.0f ? s->duration[w] : 15.0f;
		fx->fury_mult = s->magnitude[w] > 1.0f ? s->magnitude[w] : 1.3f + 0.2f * w;
		sfx_play("shout_sprint", 0.6f, 0.0f);
		break;
	case SHOUT_AURA: // Aura Whisper: the life about you, heard
		fx->aura_s = s->duration[w] > 0.0f ? s->duration[w] : 10.0f * words;
		sfx_play("shout_frost_1", 0.4f, 0.0f);
		break;
	case SHOUT_CLEARSKIES: // Clear Skies: the clouds, the fog - and the Throat of the World's storm - blown away
		fx->clear_s = s->duration[w] > 0.0f ? s->duration[w] : 20.0f * words;
		spellfx_wave(mouth, look, 12.0f, 0.4f, ELEMENT_FORCE, words / 3.0f);
		sfx_play("shout_sprint", 0.8f, 0.0f);
		break;
	case SHOUT_DISMAY: { // Dismay (Faas Ru Maar): those before you, up to its level, flee - and a shove
		static const float range[3] = {10.0f, 15.0f, 20.0f};
		const float secs = s->duration[w] > 0.0f ? s->duration[w] : 30.0f;
		const int took = creatures_shout_effect(mouth, dir, range[w], 0.6f, CSHOUT_FEAR, fmaxf(s->magnitude[w], 7.0f), secs);
		creatures_shout_push(mouth, dir, range[w], 0.6f, 2.0f, 0.0f, 0);
		spellfx_wave(mouth, look, range[w], 0.5f, ELEMENT_FORCE, words / 3.0f);
		sfx_play("shout_force_1", 0.7f, 0.0f);
		game_log("shouts: Dismay - %d flee", took);
		break;
	}
	case SHOUT_STORMCALL: // Storm Call (Strun Bah Qo): a storm overhead, its lightning on your foes (outdoors: main)
		fx->storm_s = s->duration[w] > 0.0f ? s->duration[w] : 60.0f * words;
		fx->storm_harm = s->magnitude[w] > 0.0f ? s->magnitude[w] : 20.0f;
		sfx_play("shout_sprint", 0.8f, 0.0f);
		break;
	case SHOUT_VALOR: { // Call of Valor (Hun Kaal Zoor): a hero of Sovngarde at your side (the word's: Gormlaith,
		// Felldir, Hakon), a spirit, for its time
		const float at[3] = {feet[0] + dir[0] * 2.5f, feet[1], feet[2] + dir[2] * 2.5f};
		const char *hero = s->summon[w][0] ? s->summon[w] : s->summon[0];
		const float secs = s->duration[w] > 0.0f ? s->duration[w] : 60.0f;
		if (!hero[0] || !creatures_summon_spirit(hero, at, secs))
			ui_notice("No hero answers you here", 3.0f);
		sfx_play("shout_force_1", 0.7f, 0.0f);
		break;
	}
	case SHOUT_CALLDRAGON: { // Call Dragon (Od Ah Viing): Odahviing comes to you - outdoors only, as Skyrim's
		if (quests_calls_dragon()) // (Dragonsreach's porch in The Fallen: he comes to the trap - quests.c's)
			break;
		const float at[3] = {feet[0] + dir[0] * 25.0f, feet[1], feet[2] + dir[2] * 25.0f};
		const int d = creatures_place_dragon(NULL, at, atan2f(dir[0], -dir[2]) + 3.14159f, 1);
		if (d >= 0)
			creatures_set_name(d, T_NPC_00045920_FULL0);
		else
			ui_notice("No dragon can come here", 3.0f);
		sfx_play("shout_force_1", 0.8f, 0.0f);
		break;
	}
	case SHOUT_DRAGONREND: { // Dragonrend (Joor Zah Frul): the dragons before you, out of the sky and held to the ground
		const float secs = s->duration[w] > 0.0f ? s->duration[w] : 10.0f + 2.5f * w;
		const int took = creatures_dragonrend(mouth, look, 60.0f, secs);
		spellfx_wave(mouth, look, 30.0f, 0.5f, ELEMENT_FORCE, words / 3.0f);
		sfx_play("shout_force_1", 0.9f, 0.0f);
		game_log("shouts: Dragonrend - %d dragons brought down for %.0f s", took, secs);
		break;
	}
	case SHOUT_THROWVOICE: { // Throw Voice: your voice where you look - those after you go there
		float at[3] = {feet[0] + dir[0] * 20.0f, feet[1], feet[2] + dir[2] * 20.0f};
		creatures_alert_at(at, 25.0f, at);
		sfx_play_at("shout_force_1", at, 40.0f);
		break;
	}
	case SHOUT_DISARM:
	case SHOUT_ICEFORM:
	case SHOUT_MARKED:
	case SHOUT_DRAIN:
	case SHOUT_KYNESPEACE:
	case SHOUT_ANIMAL: { // on those before you
		static const float range[3] = {8.0f, 12.0f, 16.0f};
		const int what = s->kind == SHOUT_DISARM ? CSHOUT_DISARM : s->kind == SHOUT_ICEFORM ? CSHOUT_ICE_FORM
		               : s->kind == SHOUT_MARKED ? CSHOUT_MARK : s->kind == SHOUT_DRAIN ? CSHOUT_DRAIN
		               : s->kind == SHOUT_KYNESPEACE ? CSHOUT_CALM_ANIMALS : CSHOUT_TAME_ANIMALS;
		const int calm = what == CSHOUT_CALM_ANIMALS || what == CSHOUT_TAME_ANIMALS;
		const float secs = s->duration[w] > 0.0f ? s->duration[w] : 15.0f * words;
		// (harm: Ice Form's cold all at once, Marked for Death's and Drain Vitality's a second)
		const float harm = what == CSHOUT_ICE_FORM ? 20.0f * words : what == CSHOUT_MARK ? fmaxf(s->magnitude[w], 1.0f)
		                 : what == CSHOUT_DRAIN ? fmaxf(s->magnitude[w], 1.0f) * words : 0.0f;
		const int took = creatures_shout_effect(mouth, dir, calm ? range[w] * 3.0f : range[w], calm ? -1.0f : 0.8f, what, harm, secs);
		game_log("shouts: %s (%d words) took on %d, %.0f s", s->name, words, took, secs);
		spellfx_wave(mouth, look, range[w], 0.4f, what == CSHOUT_ICE_FORM ? ELEMENT_FROST : ELEMENT_FORCE, words / 3.0f);
		spellfx_shout(mouth, dir, range[w], what == CSHOUT_ICE_FORM ? ELEMENT_FROST : ELEMENT_FORCE, words / 3.0f);
		snprintf(sound, sizeof(sound), "shout_%s_%d", what == CSHOUT_ICE_FORM ? "frost" : "force", words);
		sfx_play(sound, 1.0f, 0.0f);
		break;
	}
	case SHOUT_SPRINT: { // Whirlwind Sprint: you, flung ahead along the ground
		// (Skyrim's reach, measured in the game - UESP's talk page: Wuld about 930 units, Nah about 1525;
		// Kest carried on at that step. Its records don't hold it: the dash is its behaviour's. Was 7/14/20 m -
		// half Wuld's, and Ustengrav's gates, built for Wuld alone, couldn't be passed)
		static const float dist[3] = {13.3f, 21.8f, 30.3f}, secs[3] = {0.35f, 0.5f, 0.65f};
		fx->dash = 1, fx->dash_dir[0] = dir[0], fx->dash_dir[1] = dir[2];
		fx->dash_speed = dist[w] / secs[w], fx->dash_s = secs[w];
		sfx_play("shout_sprint", 1.0f, 0.0f);
		spellfx_shout(mouth, dir, dist[w], ELEMENT_FORCE, words / 3.0f * 0.5f);
		break;
	}
	}
	(void)feet;
}

void shouts_update(float dt, int held, const float eye[3], const float look[3], const float feet[3], ShoutFx *fx) {
	memset(fx, 0, sizeof(*fx));
	if (recovery_left > 0.0f)
		recovery_left -= dt;
	if (pending >= 0) { // "Fus..." said: then the rest, and it goes off
		pending_s += dt;
		if (!voice_playing() || pending_s >= FIRST_LINE_MAX_S) {
			const Shout *s = &shouts[pending];
			say(s->key, pending_words == 2 ? "2" : "3");
			shout(s, pending_words, eye, look, feet, fx);
			pending = -1;
		}
		return;
	}
	static int was_held;
	const int press = held && !was_held;
	was_held = held;
	if (equipped < 0) {
		holding = 0;
		return;
	}
	const Shout *s = &shouts[equipped];
	if (press && !holding) {
		if (recovery_left > 0.0f) {
			ui_notice("You need to recover before you can shout again", 2.0f);
			game_log("shouts: recovering, %.1f s left", recovery_left);
			return;
		}
		holding = 1, hold_s = 0.0f;
	}
	if (!holding)
		return;
	hold_s += dt;
	const int words = shouts_holding();
	// Let go, or every word known gathered: shouted
	const float full = s->known >= 3 ? SHOUT_TIME_2 : s->known == 2 ? SHOUT_TIME_1 : 0.0f;
	if (held && hold_s < full)
		return;
	holding = 0;
	recovery_left = fast_recovery ? fminf(s->recovery[words - 1], FAST_RECOVERY_S) : s->recovery[words - 1] * recovery_mult;
	char text[80];
	snprintf(text, sizeof(text), "%s", shouts_words(equipped, words));
	ui_notice(text, 2.0f);
	if (words == 1) {
		say(s->key, "1a");
		shout(s, 1, eye, look, feet, fx);
	} else {
		say(s->key, "1b");
		pending = equipped, pending_words = words, pending_s = 0.0f;
	}
}
