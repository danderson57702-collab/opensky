#ifndef FOLLOWERS_H
#define FOLLOWERS_H

#include <stdint.h>

// Skyrim's followers (pack/followers.txt, converter/followers.py): the people who'll come with
// you - a sellsword hired (Jenassa: Skyrim's HirelingGold), or a friend asked (a line of theirs
// : Skyrim's relationship rank, won by a favour, a brawl, a quest). One at a
// time, as Skyrim's. Following they keep at your side - through doors, into towns and
// dungeons - and fight whatever comes at you; told to wait they stay put till you're back;
// they carry what you give them; parted with, they go home. Saved with the game.

void followers_init(const char *pack_dir);
void followers_clear(void); // (a new game)
int followers_is(int actor);       // one who could follow you (actors.h's index)
int followers_current(void);       // the one with you (actors.h's index), -1
int followers_is_friend(int actor); // (Skyrim's relationship: a friend now)
void followers_befriend(int actor); // a friend now (a favour done, a brawl won...): they'll follow when asked
void followers_assign(int actor);   // (given you - a housecarl: a friend, with you now)

// Talking to someone: what you can say to them about following (appended to opts; how many)
int followers_topics(int actor, const char **opts, int max);
// The k'th of those said. trade_handle: set to their things (items.h handle) when it's to trade
// - the caller opens the container menu with it; else -1
void followers_say(int actor, int k, int *trade_handle);

// Each frame: at your side (or where they were told to wait), fighting with you; place is
// interior.h's (a change: through a door with you)
void followers_update(float dt, const float feet[3], float yaw_deg, int place);

// Saving (version 16): who's with you, friends, sellswords paid - by their editor ids' hashes
#define FOLLOWERS_SAVED 32
typedef struct {
	uint32_t now, waiting;
	uint32_t friends[FOLLOWERS_SAVED], paid[FOLLOWERS_SAVED / 2];
} FollowersSave;
void followers_save(FollowersSave *out);
void followers_load(const FollowersSave *in);
// A save from before (its follower a number in followers.txt as it stood when it was made): read by
// the save's date (legacy/followers_eras.txt); lydia_earned: Dragon Rising done
void followers_load_old(const int32_t in[4], const char *save_date, int lydia_earned);

#endif
