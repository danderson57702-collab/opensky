// talk_test: the townspeople's lines (real pack/actors): walking up to someone they say hello,
// the talk button greets, every line's voice file is there
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fileio.h"
#include "talk.h"

#define PACK "../../pack/"

static char ids[1024][64];
static int count, played, speaker_at = -1;
static char last_played[128];

int actors_count(void) { return count; }
const char *actors_id(int i) { return i >= 0 && i < count ? ids[i] : ""; }
int actors_shown(int i, float pos[3]) { // (one person in front of you: the one asked about)
	pos[0] = pos[1] = pos[2] = 0.0f;
	if (i == speaker_at)
		pos[2] = -1.5f;
	else
		pos[0] = 500.0f;
	return 1;
}
int actors_asleep(int i) { (void)i; return 0; }
int actors_scripted(int i) { (void)i; return 0; }
void actors_wake(int i, float x, float z) { (void)i, (void)x, (void)z; }
void actors_attend(int i, float x, float z, float s) { (void)i, (void)x, (void)z, (void)s; }
void actors_set_mouth(int i, float o) { (void)i, (void)o; }
int voice_play(const char *path) {
	played++;
	snprintf(last_played, sizeof(last_played), "%s", path);
	return 1;
}
void voice_stop(void) {}
int voice_playing(void) { return 0; }
float voice_level(void) { return 0.0f; }
void voice_set_mix(float g, float p) { (void)g, (void)p; }
void ui_subtitle(const char *s, const char *t) { (void)s, (void)t; }
void music_duck(float l) { (void)l; }
void game_log(const char *fmt, ...) { (void)fmt; }

int main(void) {
	fileio_init(PACK);
	FILE *f = fopen(PACK "actors/actors.txt", "r");
	char line[96];
	while (f && count < 1024 && fgets(line, sizeof(line), f)) {
		line[strcspn(line, ".\r\n")] = 0;
		snprintf(ids[count++], sizeof(ids[0]), "%s", line);
	}
	if (f)
		fclose(f);
	talk_init(PACK);
	const char *who[] = {"nazeem", "ysolda", "carlottavalentia", "heimskr"};
	int failed = 0;
	for (int w = 0; w < 4; w++) {
		speaker_at = -1;
		for (int i = 0; i < count; i++)
			if (!strcmp(ids[i], who[w]))
				speaker_at = i;
		played = 0;
		for (int frame = 0; frame < 30 * 60 && !played; frame++) // (a minute standing by them)
			talk_update(1.0f / 30.0f, 0.0f, 0.0f, 0.0f, -1);
		const int hello = played;
		for (int k = 0; k < 30 * 10; k++) // (the line ends, the quiet after it)
			talk_update(1.0f / 30.0f, 0.0f, 0.0f, 0.0f, -1);
		played = 0;
		talk_update(1.0f / 30.0f, 0.0f, 0.0f, 0.0f, speaker_at);
		printf("%-18s index %3d: hello %s, greeting %s (%s)\n", who[w], speaker_at, hello ? "yes" : "NO",
		       played ? "yes" : "NO", last_played);
		failed |= speaker_at < 0 || !hello || !played;
	}
	puts(failed ? "talk_test: FAILED" : "talk_test: all passed");
	return failed;
}
