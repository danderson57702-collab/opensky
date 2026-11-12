#ifndef TOPICS_H
#define TOPICS_H

// Skyrim's set conversations and services (pack/topics.txt, converter/topics.py): the player's
// words for a topic and the answers in the speaker's own voice (the Whiterun gate guard, the
// brawls, the favours); the trainers - what each teaches, how far, and what they say.

void topics_init(const char *pack_dir);
const char *topics_prompt(const char *key); // the player's words ("" if none)
float topics_global(const char *name);      // a price the prompts name (Skyrim's: "horsecost"), 0
// One of the topic's answers said (its parts in turn): by someone in sight (actors.h's index:
// their lips move) or, actor -1, a voice with this name on the subtitle. 1 if there was one
int topics_say(const char *key, int actor, const char *title);
int topics_busy(void); // (an answer still being said)
void topics_update(void); // each frame: the next part of a speakerless answer

// Trainers: by actors.h's index - the skill (skills.h), the most they teach; 0 if not one
int topics_trainer(int actor, int *skill, int *max);
const char *topics_trainer_prompt(int actor);
void topics_trainer_say(int actor, int done); // (done: INFO 000BD7DF)

#endif
