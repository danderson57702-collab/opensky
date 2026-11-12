#ifndef AUTOTEST_H
#define AUTOTEST_H

#include <psp2/ctrl.h>

// A scripted run for testing in Vita3K (or on the Vita): when <pack>/autotest.txt is there,
// its lines - "<seconds> <command> <args>", seconds from when the game loop starts - press
// buttons, move the sticks, put you somewhere, take pictures of the screen and quit. What
// happens goes to the log (ux0:data/opensky_log.txt) as usual.
//
//   5 press cross          a button pressed (cross circle square triangle up down left right
//                          l r start select)
//   5 hold r 2.5           ... held this long
//   8 stick 0 -1 3         the left stick (x y, -1..1; -1 y: forward) this long
//   8 look 1 0 0.5         the right stick
//   10 city whiterun 406 60   into a walled city's streets at x, z (metres)
//   10 goto 100 -200       out to Tamriel at x, z
//   12 near nazeem         beside someone, facing them
//   14 shot market         the screen to ux0:data/opensky_shot_market.ppm
//   15 log some words      a line in the log
//   16 say balgruufthegreater 0   a quest topic said to someone (quests.h: its number now)
//   17 barter belethor     a merchant's wares
//   18 station 1           a crafting station's menu (furniture.h: 1 forge .. 7 enchanter)
//   19 kit                 the debug crafting kit into your pack
//   20 enter whiterundragonsreach   inside an interior, at its first door
//   20 opening                     Skyrim's opening from the cart (intro.c), as a new game
//   21 perk Power Bash     a perk taken by its name (no points)
//   21 follow hirelingjenassa   following you (as hired), wherever they are loaded
//   22 strike nazeem 40    a blow on a townsperson (people.c), wherever you are
//   23 qgo time_wound      moved to a quest's place (quests.c's keys)
//   23 floorword ro        a Word of Power's letters on the floor before you (wordfx.c)
//   22 spell both Firebolt a spell into both hands, readied
//   24 walkto 12 -30 20    walked toward x, z (metres) till there or stuck (logged), at most 20 s
//   30 quit

enum { AT_NONE, AT_CITY, AT_GOTO, AT_NEAR, AT_SHOT, AT_QUIT, AT_SAY, AT_BARTER, AT_STATION, AT_KIT, AT_ENTER, AT_OPENING, AT_WEAR, AT_SPELL, AT_WARP, AT_GIVE, AT_PULL, AT_LEFT, AT_EQUIP, AT_GOD, AT_SLAY, AT_TAKE, AT_BEFRIEND, AT_SLEEP, AT_TALK, AT_QUEST, AT_USEDOOR, AT_UNEQUIP, AT_WORD, AT_RIDE, AT_LOADSAVE, AT_TRAVEL, AT_DRAGON, AT_HAIR, AT_SEARCH, AT_WATCH, AT_STATE, AT_LOOKAT, AT_SKILL, AT_HP, AT_PERK, AT_FOLLOW, AT_STRIKE, AT_FLOORWORD, AT_QGO, AT_DEBUG, AT_MEASURE, AT_WALKTO };
typedef struct {
	int kind;
	char arg[64];
	float x, z;
} AutotestAction;

// Whether a script was found
int autotest_init(const char *pack_dir);
int autotest_active(void);
int autotest_auto_race(void);
int autotest_title(void); // ("title" in the script: the run starts at the title menu)
// Each frame (t: seconds since the loop began): the pad as the script has it (over the real one)
void autotest_pad(float t, SceCtrlData *pad);
// The script's next action due by t (one a call), AT_NONE when there's none yet
AutotestAction autotest_next(float t);

#endif
