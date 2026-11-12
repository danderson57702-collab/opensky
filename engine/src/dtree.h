#pragma once

// Skyrim's conversations as it has them: the NPC's line, then the player's answers to choose from
// (converter/quests.py's TREES - the D lines of quests.txt), each with its own reply and answers
void dtree_init(const char *pack_dir);
int dtree_find(const char *key); // the tree for a scene's key, or -1
// Begins it with this person saying its first line; 0 if they're not about to say it
int dtree_start(int tree, int actor);
void dtree_update(void);
int dtree_busy(void);   // a conversation going on (speaking, or waiting on your answer)
int dtree_actor(void);  // who it's with, or -1
// The answers to choose from now (the line's been said): their count, 0 if none waiting
int dtree_choices(const char **opts, int max);
// The answer chosen (-1: Goodbye - the rest of what they had to say, then done)
void dtree_choose(int choice);
// The topic just reached (for its quest's effects), once: its editor id or NULL
const char *dtree_take_reached(void);
// The reply given to the topic last reached: its conditions ("o14:3:25:av17;...") and how many
// topics it links on to (0: the end of it - a yes, not a "but persuade me")
const char *dtree_reached_reply(int *links);
// A name for an alias in the lines ("{riverwoodfriend}": Gerdur)
void dtree_set_name(const char *alias, const char *name);
// Someone's own everyday conversations (DIAL 000BABDF): their trees; its opening line
int dtree_everyday(const char *speaker, int *out, int max);
const char *dtree_prompt(int tree);
const char *dtree_speaker(int tree); // (its speaker's id: "elenwen")
