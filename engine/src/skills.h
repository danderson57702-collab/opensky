#ifndef SKILLS_H
#define SKILLS_H

// Skyrim's 18 skills and the player's level (pack/skills.txt, see converter/skills.py).
// Using a skill earns experience (use_mult x usage + use_offset); a skill's next level
// takes improve_mult x level^1.95 + improve_offset; every skill level gained gives that
// many points toward the player's level, whose next takes 75 + 25 x level. Each player
// level is a choice of +10 health, magicka or stamina.

#define SKILL_COUNT 18
enum { // actor value order
	SK_ONE_HANDED, SK_TWO_HANDED, SK_ARCHERY, SK_BLOCK, SK_SMITHING, SK_HEAVY_ARMOR, SK_LIGHT_ARMOR,
	SK_PICKPOCKET, SK_LOCKPICKING, SK_SNEAK, SK_ALCHEMY, SK_SPEECH, SK_ALTERATION, SK_CONJURATION,
	SK_DESTRUCTION, SK_ILLUSION, SK_RESTORATION, SK_ENCHANTING
};

typedef struct {
	int level[SKILL_COUNT];
	float xp[SKILL_COUNT];  // toward the next level
	int player_level;
	float player_xp;        // toward the next player level
	int pending;            // level-ups not yet chosen (health / magicka / stamina)
	int perk_points;        // one per level, spent in the skill trees (perks.c)
} SkillState;

void skills_init(const char *pack_dir);
SkillState *skills_state(void); // (saving, loading)
const char *skills_name(int skill);
const char *skills_desc(int skill); // (Skyrim's words for it: the skills menu's)
// A use worth `usage` (Skyrim's units: magicka spent, seconds unseen...)
void skills_use(int skill, float usage);
int skills_take_used(int skill); // (that skill used since last asked: a spell of its school cast)
// A Guardian Stone's blessing: the group (0 combat, 1 stealth, 2 magic: six skills each, in
// order) whose skills improve a fifth faster; -1 none
void skills_set_stone(int group);
// One level more (a skill book read the first time)
void skills_raise(int skill);
float skills_progress(int skill);  // 0..1 toward the next level
float skills_player_progress(void);
// Takes a pending level-up (the caller applies the chosen +10); 0 if none was pending
int skills_take_level_up(void);
// 1 once after the player's level went up (Skyrim restores health, magicka and stamina then)
int skills_take_leveled(void);

#endif
