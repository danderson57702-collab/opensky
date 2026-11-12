// Host test: skill experience and the player's level, with the real pack/skills.txt.
#include <stdio.h>
#include <string.h>

#include "skills.h"

#ifndef PACK
#define PACK "../../pack/" // relative to engine/tests
#endif

static int notices;
void ui_notice(const char *text, float seconds) {
	(void)text, (void)seconds;
	notices++;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

int main(void) {
	skills_init(PACK);
	SkillState *s = skills_state();
	CHECK(!strcmp(skills_name(SK_DESTRUCTION), "Destruction"), "name %s", skills_name(SK_DESTRUCTION));
	CHECK(s->level[SK_TWO_HANDED] == 25 && s->level[SK_DESTRUCTION] == 15, "Nord starting skills");
	CHECK(s->player_level == 1 && s->pending == 0, "starts at level 1");

	// A little use: some progress, no level
	skills_use(SK_DESTRUCTION, 14.0f);
	float p = skills_progress(SK_DESTRUCTION);
	CHECK(p > 0.0f && p < 0.2f && s->level[SK_DESTRUCTION] == 15, "one second of Flames: %.3f", p);

	// Flames for a minute (14 magicka/s): a few levels, each announced
	int seconds = 0;
	while (s->level[SK_DESTRUCTION] < 18 && seconds < 600)
		skills_use(SK_DESTRUCTION, 14.0f), seconds++;
	printf("Destruction 15 -> 18 in %d s of Flames\n", seconds);
	CHECK(seconds > 30 && seconds < 300, "Destruction pace %d s", seconds);
	CHECK(notices == 3, "%d notices", notices);
	CHECK(s->player_xp == 16 + 17 + 18 || s->player_level > 1, "player xp %.0f", s->player_xp);

	// Keep going: player levels come with a pending choice each
	while (s->player_level < 3)
		skills_use(SK_RESTORATION, 50.0f);
	CHECK(s->pending == 2, "pending %d", s->pending);
	CHECK(skills_take_level_up() && skills_take_level_up() && !skills_take_level_up(), "taking level-ups");

	// Capped at 100
	for (int i = 0; i < 100000; i++)
		skills_use(SK_ALTERATION, 1000.0f);
	CHECK(s->level[SK_ALTERATION] == 100 && skills_progress(SK_ALTERATION) == 1.0f, "cap %d", s->level[SK_ALTERATION]);
	printf("player level %d after maxing Alteration\n", s->player_level);

	printf(failures ? "skills_test: %d failures\n" : "skills_test: all passed\n", failures);
	return failures != 0;
}
