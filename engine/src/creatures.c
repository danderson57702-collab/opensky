#include "creatures.h"
#include "interior.h"
#include "crime.h"

#include "gamelog.h"
#include "music.h"
#include "quests.h"
#include "voice.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "activators.h"
#include "actors.h"
#include "aim.h"
#include "collision.h"
#include "fileio.h"
#include "items.h"
#include "navmesh.h"
#include "projectiles.h"
#include "sfx.h"
#include "spellfx.h"
#include "perks.h"
#include "skills.h"
#include "lighting.h"
#include "terrain.h"
#include "gametext.h"

#define MAX_KINDS 512 // (the pack has ~260: people, beasts, dragons)
#define MAX_SPAWNS 8192 // (the pack's ~3400 - more as the map grows - and dragons met at random)
#define MAX_LIVE 20
#define WATER_CLEARANCE_M 0.4f // (a water creature goes only where the bed is this far below it)
#define WAKE_OUT_M 95.0f   // outdoors a spawn point this near has its creature there (beyond the fog,
#define SLEEP_OUT_M 125.0f //  where it can't be seen appearing) ... and gone again past this
#define WAKE_IN_M 45.0f    // inside
#define SLEEP_IN_M 70.0f
#define SEE_M 24.0f        // an enemy notices you within this (sneaking: half), if it can see you
#define HEAR_M 5.0f        // ... or this near, seen or not
#define GIVE_UP_M 60.0f    // ... and gives up past this from its spot
#define WANDER_M 7.0f      // idling: strolls about its spot
#define FLEE_M 16.0f       // prey runs from you within this
#define AMBUSH_WAKE_M 6.0f // the undead lying in wait rise when you come this near...
#define BOSS_WAKE_M 10.0f  // ...one that shouts (a barrow's master, in its sarcophagus) from further
enum { SH_FORCE = 1, SH_FROST = 2, SH_FIRE = 4, SH_DISARM = 8 };
#define HELLO_PASS_M 2.6f   // a guard you pass this close may greet you...
#define HELLO_EVERY_S 25.0f // ...no guard again sooner than this
#define TALK_HOLD_S 5.0f    // (and stops, facing you, this long)
static float hello_wait;
static int player_female;
static char player_race[32] = "NordRace";
#define SHOUT_FROM_M 0.0f  // a draugr shouts at you between these (up close too: at 2.5 m out it shouted once, then fought)
#define SHOUT_TO_M 12.0f
#define SHOUT_IN_S 0.8f    // (breathing in: its first word; then the rest, and the shout's done)
#define SHOUT_S 1.6f
#define SHOUT_EVERY_S 7.0f // (and again no sooner than this, give or take)
#define TURN_RATE 5.0f     // radians a second
#define HIT_AT 0.45f       // a blow lands this far into its attack clip
#define FLINCH_S 0.45f
#define BLOCK_CHANCE 0.25f     // a person at your blow raises its guard this often (fCombatBlockChanceWeaponMult)...
#define BLOCK_S 0.6f           // ...holding it this long
#define BLOCKED_SHARE 0.6f     // ...letting this much through (Skyrim's weapon block: 30% + 20% by skill, fBlockWeaponBase/Scaling)
// Skyrim's confidence (AIDT: cowardly, cautious, average, brave, foolhardy): hurt below this share
// of its health, it flees a while (fConfidence*: cowardly at once, foolhardy never)
static const float FLEE_BELOW[5] = {1.01f, 0.375f, 0.15f, 0.0375f, 0.0f};
#define FALL_S 0.7f        // falling over dead (no clip of its own)
#define MAX_LIE_RAD 1.7f   // ... how far over at most (flat ground: about 1.5, the floor falling away: more)
#define LIE_PREFER_RAD 0.15f // (the other way only if it lies that much flatter)
#define LIE_WALL_TEST_M 0.5f
#define LIE_CLEAR_M 0.05f
#define REPLAN_S 0.6f
#define MAX_PATH 16
#define CORPSE_REF 0xFD000000u // (a body's container: this | its spawn point)
// Diplomatic Immunity's Thalmor (converter/creatures.py): at peace till Malborn takes you out of
// the party (Skyrim's MQ201 SetEnemy ThalmorCombatFaction/PlayerFaction: our stage 55); the
// party hall's guards at peace; the two sent down to the dungeon once you're in it (stage 70)
enum { SIDE_THALMOR = 4, SIDE_ESCAPE = 5, SIDE_PARTY = 6 };
#define THALMOR_ALARM_STAGE 55
#define THALMOR_ESCAPE_STAGE 70
static uint32_t challenger_ref; // (the one a quest has challenge you first - the Solar's guard: not in the alarm)
#define RESPAWN_H 240.0f          // Skyrim's iHoursToRespawnCell: 10 days
#define RESPAWN_CLEARED_H 720.0f  // ... a cleared one's (iHoursToRespawnCellCleared): 30 days
#define REACH_M 3.0f           // searching a body
#define ARCHER_NEAR_M 7.0f     // archers keep to between these from you
#define ARCHER_FAR_M 22.0f
#define ARCHER_BASH_M 1.8f     // closer than this, a bow is swung, not drawn
#define DRAW_S 1.1f            // an archer's draw
#define SHOT_EVERY_S 2.2f
#define ARROW_SPEED_MS 45.0f
#define ARROW_DAMAGE 8.0f      // (an iron arrow's, added to the bow's)
#define BOLT_SPEED_MS 22.0f    // a mage's fire bolt
#define BOLT_DAMAGE 12.0f
#define CAST_EVERY_S 2.6f
#define HOLDS_BOW(k) ((k)->weapon == 7 || (k)->weapon == 9) // (a bow or crossbow - a mage with a dagger casts)
#define MAGE_SUMMON_M 25.0f    // a conjurer calls its atronach once you're this near, in a fight
#define MAGE_HEAL_BELOW 0.4f   // a mage heals itself below this share of its health...
#define MAGE_HEAL_EVERY_S 8.0f // ... at most this often
#define SIGHT_COS -0.2f     // what's in front of it: within about 100 degrees of where it faces
#define NOTICE_S 1.4f       // seen in the open close by, noticed after this (Skyrim's detection
                            //  grows over time: farther, darker, sneaking take longer)
#define FORGET_S 6.0f       // an unseen player is forgotten this fast (awareness a second / this)
#define ALERT_AT 0.35f      // awareness this high: it turns and comes to look
#define SEARCH_S 9.0f       // ... for this long before giving up
#define CALL_M 12.0f        // a fight draws in those this near who can see it (not the whole camp)
#define MELEE_SLOTS 3       // at most this many strike at you at once; the rest hold off
#define MAX_ENGAGED 5       // ... this many close in at all; a bigger crowd's rest keep their posts
#define SLOT_AT_POST -2
#define TAUNT_EVERY_S 9.0f  // a fighter taunts at most this often
#define HOLD_OFF_M 4.5f     // ... this far out, circling, waiting their turn
#define ARCHER_POST_M 6.0f  // archers keep to their post (a wall, a tower), within this
#define IRON_ARROW_FORM 0x0001397du
#define BOW_NPC_SPREAD_RAD (4.0f * 3.14159265f / 180.0f) // (Skyrim.esm fBowNPCSpreadAngle: 4 degrees)
#define PERSON_POWER_MULT 2.0f
#define SPIT_SPEED_MS 18.0f    // a frostbite spider's spit (an arcing glob)
#define SPIT_GRAVITY 0.5f
#define SPIT_NEAR_M 4.0f       // ... spat from between these (nearer, it bites)
#define SPIT_FAR_M 16.0f
#define SPIT_EVERY_S 4.0f
#define SPIT_WINDUP_S 0.45f
#define SHOUT_PUSH_MS 9.0f     // a shout's push of 1 flings someone this fast
#define KNOCK_FRICTION 3.5f    // a flung body's slide slowing (a second)
#define DEATH_PUSH_MS 2.2f     // a body killed by your blow: flung along it this fast (a Vita-sized ragdoll)...
#define DEATH_PUSH_POWER_MS 4.5f // ... by a power attack
#define DEATH_PUSH_ARROW_MS 1.4f // ... by an arrow
#define DEATH_PUSH_HEIGHT_M 1.8f // (bigger than a man: flung less, by its height)
#define ARVEL_CALLS_M 40.0f    // Arvel in the web calls for help this near
#define BODY_RAY_UP_M 0.9f     // a body's floor: looked for from this far over where it was...
#define BODY_RAY_DOWN_M 2.5f   // ... to this far under
#define BODY_SINK_OK_M 0.06f   // ... and none of it lower in it than this
#define KNOCK_DOWN_S 2.2f      // knocked down: lying this long before getting up
#define KNOCK_RISE_S 0.8f
#define SLOW_MULT 0.5f         // slowed: this fast
#define STUCK_REPLAN_S 0.6f    // blocked this long: a new path
#define STUCK_NUDGE_S 1.2f     // ... this long: nudged onto the navmesh ahead (2 s: the draugr's snags)
#define STUCK_NUDGE_M 0.8f
#define UNREACH_HOLD_S 6.0f    // ... and stays back this long before trying again
#define UNREACH_BACK_S 4.0f    // no way to you this long: it backs off to its post (Skyrim's unable-to-reach)

enum { IDLE, WANDER, CHASE, ATTACK, SHOOT, FLEE, FLINCH, DEAD, AMBUSH, ALERT, KNOCKED, DRAGON, ARREST, RISE, SHOUT };
enum { F_PERSON = 1, F_ARCHER = 2, F_UNDEAD = 4, F_NO_DEATH_CLIP = 8, F_WATER = 16, F_UPRIGHT = 32, F_DRAGON = 64,
       F_REMAINS = 128, F_GUARD = 256 };
#define ARREST_SEE_M 20.0f    // a guard after you: comes when it sees you this near
#define ARREST_TALK_M 2.2f    // ... and stops you here
#define ARREST_AGAIN_S 20.0f  // (answered: not again for this long)

typedef struct {
	char name[24], title[40], set[24];
	int actor_kind; // actors_beast_kind (-1 not loaded yet, -2 missing)
	float health, damage, reach, height, radius;
	int aggression, confidence, flags, weapon, corpse, armor;
	float walk_speed, run_speed;
	float power_mult;          // its strongest attack's damage multiplier (ours its power attack)
	float bite_mult, bite_dps, bite_s; // its poisoned bite (attack2): multiplier, poison a second, seconds
	float spit_dps, spit_s;    // a ranged poison spit (a frostbite spider's), 0: none
	float scale;               // drawn this much bigger than its skeleton (its clips' pace too)
	char voice[24];            // people: their voice type (grunts: "v_<voice>_hit"...), "" none
	int breath_element;        // dragons (G lines): ELEMENT_FIRE / ELEMENT_FROST, its breath a second,
	float breath_dps;          //  its level (random ones are picked by the player's), the kind
	int dragon_level, bones;   //  drawn where it died (its skeleton; -1 none)
	int shouts;                // (U lines) the shouts it knows: SH_FORCE, SH_FROST, SH_FIRE, SH_DISARM
	int mage_element;          // (M lines) a mage's bolt: spellfx.h's element (-1 none: the old fire bolt)...
	float mage_damage, mage_area; // ... its damage and burst (m: a Fireball's)
	char mage_summon[24];      // ... the creature its conjuration calls ("" none)
	float mage_heal;           // ... its healing on itself
	int hold;                  // a guard's (W lines): its hold (crime.h), -1
} Kind;

typedef struct {
	int kind, place, ambush;
	int side; // Skyrim's opening (Helgen Keep): 0 always; 1 with Ralof, 2 with Hadvar (the other side's);
	          // 3 not till it's over (the bandits who come later); Diplomatic Immunity's Thalmor
	          // (SIDE_*): at peace till you leave the party, its hall's guards, the escape scene's
	uint32_t ref; // (Skyrim's reference: a quest's own, found by it - the Solar's guard; 0 none)
	float pos[3], heading;
	int dead;  // killed (saved with the game)
	float dead_at; // ... when (game hours: Skyrim's cells reset 10 days on, cleared ones 30)
	int live;  // its creature (index in live[]), -1
	// Its leveling (Skyrim's): a multiple of the player's level (thousandths; 0: lv fixed) within
	// least..most (0: no most), its health at level 0 and a level (0, 0: the kind's)
	int lv_mult, lv, lv_min, lv_max;
	float health0, health_per;
	int transient; // (a dragon flying in at random: not in the pack's list, not saved)
	int quest;     // (... a quest's foe: kept where it was placed while you're away - till the quest lets it go)
	int route_first, route_n; // a guard's patrol (R lines: Skyrim's linked markers), in route_pts
} Spawn;

#define MAX_ROUTE_PTS 2048
static float route_pts[MAX_ROUTE_PTS][2]; // (x, z)
static int route_pt_count;

typedef struct {
	int used, spawn, kind, beast;
	float pos[3], heading, health, max_health; // (max: as leveled when it woke)
	int state, attacks, hostile;
	float clip_t, timer, target[2], fall, shot_t, flee_t;
	int hit_done;
	float pain; // damage since it last flinched
	float block_t; // its guard up against your blow (a FLINCH shown as its block)
	int arrow_item, arrows; // arrows in it (searching the body gives them back)
	float aware;          // 0 .. 1: how sure it is you're there (1: it fights)
	float goal_y;         // its path's goal's floor (go_to_level)
	float unreach_t;      // you out of its reach, no way to you, this long
	int victim;           // fighting: who it's after - -1 you, else a live ally of yours (a summons)
	float victim_t;       // ... chosen again after this
	float seen_at[2];     // where it last saw / heard you
	float strafe;         // holding off: which way it circles
	int hand_to_hand;     // (Helgen Keep's soldiers in the opening: they come at you, bows or not)
	int ally;             // yours (a summons, the dead raised): at your enemies, not at you
	float ally_left;      // ... for this long
	float ally_boost;     // ... striking harder for this long (Courage, Rally)
	float soul_left;      // Soul Trap on it: dying within this, its soul's taken
	int held;             // caught fast (Arvel the Swift in the spider's web): no fight in it till it's cut down
	float held_said;      // ... its pleas: seconds till the next
	int held_seen;        // ... you've come in (1), gone off (2)
	float cut_t;          // cut down: getting out of the web (Skyrim's clip), then off
	int called;           // (... calling for help from the web, once, as you come near)
	float rise_t;         // up out of its coffin, alcove, throne (Skyrim's clip), then at you
	float rise_len, rise_from[3], rise_to[3]; // ... the clip's length; carried out of the shelf to the floor in front
	float shout_cd, shout_t; // a shout: till the next; in one (breathing in, then out)
	int shout_word, shout_out;
	float talk_t;         // a guard talking to you: stopped, facing you
	int route_i, route_dir; // on patrol: the marker it's making for, which way along
	float doom_left;      // cut down and run off: it dies this long after (the barrow's dead have it)
	float vel[2];         // how it's moving (m/s, smoothed: arrows aimed for the player lead it)
	float lie;            // dead, falling over: how far it tips (radians: onto the floor, not through it)
	float knock[2];       // flung (a shout): sliding this fast (m/s), slowing
	float dot_dps, dot_left; // burning or freezing on (Frost Breath): damage a second, seconds
	float slow_left;      // slowed (Frost Breath's) for this long
	float bleed_dps, bleed_left; // bleeding (Hack and Slash, Limbsplitter): damage a second, for this long
	float calm_left;      // calmed (Calm, Pacify): not fighting for this long
	int tame;             // an ally only for a while (Animal Allegiance): itself again after, not dead
	float marked_left;    // Marked for Death: its armour down, it takes more, for this long
	float arrest_wait;    // a guard: not stopping you again for this long
	// A dragon (dragon_update): its phase (D_*), how long in it, flight (velocity: heading
	// and speed, climb), its banking, passes made, where it lands, grounded for good (hurt
	// badly: Skyrim's crash), its death's soul sequence, the bones left
	int dphase, passes, grounded, bones_beast, flyby_said, breath_on, last_act;
	float rend_t; // Dragonrend: forced to the ground, no flying, this long
	int calm;             // a quest's dragon held on its perch till woken (Alduin over Kynesgrove's mound)
	int parley;           // a quest holding it at peace (the Solar's guard, talking to you; fooled, going)
	float parley_go;      // ... fooled: walking off, gone after this
	float disguise_t;     // Diplomatic Immunity's Thalmor: your disguise looked at again after this
	int disguise_fooled;  // ... fooled by it (at peace while you're far enough off); -1 not looked yet
	const char *name;     // a quest's own name for it (Sahloknir), NULL: its kind's
	int summoned;         // a mage: its conjuration called, this fight
	int conjured;         // ... the one it called (Oblivion Binding banishes it)
	float heal_t;         // ... healing itself again after this
	float floor_share;    // a quest's: it can't be brought lower than this share of its health (Alduin on the mountain)
	int killmoved;        // dead by a killmove: its clip is Skyrim's paired victim's (held at its end), not its death
	float km_from[2], km_to[2], km_u; // (and eased to its place by the killer over a few frames: not snapped)
	float km_heading_from;
	float dtime, speed, climb, bank, pitch_now, roar_t, flap_t, breath_t, burn_t, fx_t;
	float land_at[3];
	int level;            // as leveled when it woke (the Illusion spells ask)
	int sloped;           // dead with its own death clip: laid along the ground (slope_pitch / roll)
	float slope_pitch, slope_roll;
	float stuck_t;        // how long it's been blocked every way it tried to step
	char clip[24];
	NavRef nav;
	int on_nav;
	float path[MAX_PATH * 3];
	int path_n, path_i;
	float replan, goal[2];
} Creature;

// A dragon's phases (dragon_update)
enum { D_PERCH, D_TAKEOFF, D_CIRCLE, D_STRAFE, D_HOVER, D_LAND, D_GROUND, D_FALL, D_CRASH, D_CRASH_UP, D_DYING, D_SOUL,
       D_BONES };
static float dragon_ground(float x, float z, float fallback);

static Kind kinds[MAX_KINDS];
static int kind_count;
static Spawn spawns[MAX_SPAWNS];
static int spawn_count, file_spawns; // (file_spawns: the pack's; random dragons' after them)
static int last_spawn_ok; // (loading: the last S line made a spawn - its R line goes with it)
static Creature live[MAX_LIVE];
static float damage_taken;
static int target = -1;
static float target_shown;
static unsigned rng = 0x51ed2705u;
static char aim_label[64];
static float player_noise = 1.0f; // (creatures_set_noise)
static float combat_clock; // seconds (for the taunts' pacing)
static float player_mult = 1.0f; // the difficulty's on the player's blows, arrows, spells
static int last_struck = -1;       // (creatures_ray's last: an arrow lodges in it)
static float armor_pierce;         // the share of their armour the player's next blows ignore (Bone Breaker)
static float arrow_recovery = 1.0f; // the chance of each arrow in a body coming back (Skyrim's 33%, Hunter's Discipline 66%)
static int hold_mood[CRIME_HOLDS];  // guards: 0 at peace, 1 to arrest you, 2 to attack (crime.c's)
static int arrest_ready = -1;       // the guard stopping you this frame (creatures_take_arrest)
static int struck_hold = -1, killed_hold = -1; // a guard struck / killed: its hold (crime.c reports it)
static float killed_at[3];

static int blame = 1; // (the blows struck now are yours: a guard's harm is your crime)
static float death_push[2]; // (the blow being struck: which way and how hard a body it kills is flung)
static int blow_killed = -1, blow_unseen;
static float blow_soul; // (the soul of what your last blow killed: Skyrim's soul gem charge for its level - Soul Siphon)
static int mages_read; // (the pack has the mages' own spells: M lines)
static int spell_element = -1; // (the Destruction spell striking now: its element - Intense Flames, Deep Freeze, Disintegrate)
#define PERK_INTENSE_FLAMES 0x000f392eu
#define PERK_DEEP_FREEZE 0x000f3933u
#define PERK_DISINTEGRATE 0x000f3f0eu
#define PERK_MASTER_OF_THE_MIND 0x00059b76u
#define LOW_HEALTH_SHARE 0.2f     // (Intense Flames, Deep Freeze: below this share of its health)
#define DISINTEGRATE_SHARE 0.15f  // (Disintegrate: below this, it's ash)
#define DEEP_FREEZE_S 15.0f       // (Skyrim's: paralyzed this long)
#define INTENSE_FLAMES_S 30.0f    // (... fleeing this long) // (a person your blow just killed - a killmove's; struck unseen)

void creatures_blame_player(int on) {
	blame = on;
}

static int guard_mood(const Kind *k) {
	return (k->flags & F_GUARD) && k->hold >= 0 && k->hold < CRIME_HOLDS ? hold_mood[k->hold] : 0;
}

void creatures_take_arrow(int item) {
	if (last_struck >= 0 && live[last_struck].used)
		live[last_struck].arrow_item = item, live[last_struck].arrows++;
}

void creatures_set_noise(float noise) {
	player_noise = noise;
}

// Its fight has begun: those of its kind of foe near it who can see it come too (Skyrim's
// allies answering a fight they see)
static void call_allies(int i);
static void draugr_wake(int i);

static float frand(void) {
	rng = rng * 1664525u + 1013904223u;
	return (rng >> 8) * (1.0f / 16777216.0f);
}

static float wrap(float a) {
	while (a > (float)M_PI)
		a -= 2.0f * (float)M_PI;
	while (a < -(float)M_PI)
		a += 2.0f * (float)M_PI;
	return a;
}

// '|'-separated fields of a line, in place
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

void creatures_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%screatures/creatures.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[24];
		const int n = split(line, f, 24);
		if (f[0][0] == 'K' && n >= 15 && kind_count < MAX_KINDS) {
			Kind *k = &kinds[kind_count];
			memset(k, 0, sizeof(*k));
			snprintf(k->name, sizeof(k->name), "%s", f[1]);
			snprintf(k->title, sizeof(k->title), "%s", f[2]);
			k->health = (float)atof(f[3]), k->damage = (float)atof(f[4]), k->reach = (float)atof(f[5]);
			k->aggression = atoi(f[6]), k->confidence = atoi(f[7]);
			k->height = (float)atof(f[8]), k->radius = (float)atof(f[9]);
			snprintf(k->set, sizeof(k->set), "%s", f[10]);
			k->flags = atoi(f[11]), k->weapon = atoi(f[12]), k->corpse = atoi(f[13]), k->armor = atoi(f[14]);
			k->power_mult = n >= 16 ? (float)atof(f[15]) : 2.0f, k->bite_mult = n >= 17 ? (float)atof(f[16]) : 1.0f;
			k->bite_dps = n >= 18 ? (float)atof(f[17]) : 0.0f, k->bite_s = n >= 19 ? (float)atof(f[18]) : 0.0f;
			k->spit_dps = n >= 20 ? (float)atof(f[19]) : 0.0f, k->spit_s = n >= 21 ? (float)atof(f[20]) : 0.0f;
			k->scale = n >= 22 && atof(f[21]) > 0.1 ? (float)atof(f[21]) : 1.0f;
			snprintf(k->voice, sizeof(k->voice), "%.22s", n >= 23 && strcmp(f[22], "-") ? f[22] : "");
			if (k->flags & F_PERSON) // (people: Skyrim's power attacks, whatever the race's numbers)
				k->power_mult = PERSON_POWER_MULT;
			k->actor_kind = -1; // (loaded when one first comes about)
			k->mage_element = -1;
			k->hold = -1;
			k->walk_speed = k->flags & F_PERSON ? 1.6f : 1.0f, k->run_speed = k->flags & F_PERSON ? 4.5f : 5.0f;
			kind_count++;
		} else if (f[0][0] == 'W' && n >= 3) { // a guard's hold
			for (int k = 0; k < kind_count; k++)
				if (!strcmp(kinds[k].name, f[1]))
					kinds[k].hold = crime_hold_index(f[2]);
		} else if (f[0][0] == 'M' && n >= 7) { // a mage's spells
			mages_read = 1;
			for (int k = 0; k < kind_count; k++)
				if (!strcmp(kinds[k].name, f[1])) {
					const int e = atoi(f[2]);
					kinds[k].mage_element = e == 1 ? ELEMENT_FIRE : e == 2 ? ELEMENT_FROST : e == 3 ? ELEMENT_SHOCK : -1;
					kinds[k].mage_damage = (float)atof(f[3]), kinds[k].mage_area = (float)atof(f[4]);
					snprintf(kinds[k].mage_summon, sizeof(kinds[k].mage_summon), "%s", strcmp(f[5], "-") ? f[5] : "");
					kinds[k].mage_heal = (float)atof(f[6]);
				}
		} else if (f[0][0] == 'U' && n >= 3) { // the shouts it knows (a draugr boss's)
			for (int k = 0; k < kind_count; k++)
				if (!strcmp(kinds[k].name, f[1]))
					kinds[k].shouts = atoi(f[2]);
		} else if (f[0][0] == 'G' && n >= 6) { // a dragon's breath, level and bones
			for (int k = 0; k < kind_count; k++)
				if (!strcmp(kinds[k].name, f[1])) {
					kinds[k].breath_element = atoi(f[2]) == 2 ? ELEMENT_FROST : ELEMENT_FIRE;
					kinds[k].breath_dps = (float)atof(f[3]), kinds[k].dragon_level = atoi(f[4]);
					kinds[k].bones = -1;
					for (int b = 0; b < kind_count; b++)
						if (!strcmp(kinds[b].name, f[5]))
							kinds[k].bones = b;
				}
		} else if (f[0][0] == 'R' && n >= 2 && last_spawn_ok && spawn_count > 0) { // the spawn's patrol: x,z;x,z...
			Spawn *sp = &spawns[spawn_count - 1];
			sp->route_first = route_pt_count, sp->route_n = 0;
			char *save = NULL;
			for (char *t = strtok_r(f[1], ";", &save); t && route_pt_count < MAX_ROUTE_PTS; t = strtok_r(NULL, ";", &save))
				if (sscanf(t, "%f,%f", &route_pts[route_pt_count][0], &route_pts[route_pt_count][1]) == 2)
					route_pt_count++, sp->route_n++;
		} else if (f[0][0] == 'S' && n >= 8 && spawn_count < MAX_SPAWNS) {
			Spawn *sp = &spawns[spawn_count];
			sp->kind = -1;
			for (int k = 0; k < kind_count; k++)
				if (!strcmp(kinds[k].name, f[1]))
					sp->kind = k;
			sp->pos[0] = (float)atof(f[2]), sp->pos[1] = (float)atof(f[3]), sp->pos[2] = (float)atof(f[4]);
			sp->heading = (float)atof(f[5]), sp->place = atoi(f[6]), sp->ambush = atoi(f[7]);
			if (n >= 14) {
				sp->lv_mult = atoi(f[8]), sp->lv = atoi(f[9]), sp->lv_min = atoi(f[10]), sp->lv_max = atoi(f[11]);
				sp->health0 = (float)atof(f[12]), sp->health_per = (float)atof(f[13]);
			}
			sp->side = n >= 15 ? atoi(f[14]) : 0;
			sp->ref = n >= 16 ? (uint32_t)strtoul(f[15], NULL, 16) : 0;
			sp->live = -1, sp->route_n = 0;
			last_spawn_ok = sp->kind >= 0;
			if (sp->kind >= 0)
				spawn_count++;
		}
	}
	free(buf);
	file_spawns = spawn_count;
}

// Its kind's clips, loaded the first time one comes about
static int kind_ready(Kind *k) {
	if (k->actor_kind >= 0)
		return 1;
	if (k->actor_kind == -2)
		return 0;
	k->actor_kind = actors_beast_kind(k->name, k->set);
	if (k->actor_kind < 0) {
		k->actor_kind = -2; // (missing: never tried again)
		return 0;
	}
	float speed = 0.0f; // (its clips carry it this fast, its size's stride)
	if (actors_beast_clip(k->actor_kind, "walk", &speed) > 0.0f && speed > 0.3f)
		k->walk_speed = speed * k->scale;
	if (actors_beast_clip(k->actor_kind, k->flags & F_PERSON ? "1hm_run" : "run", &speed) > 0.0f && speed > 0.5f)
		k->run_speed = speed * k->scale;
	return 1;
}

// A role's clip (Skyrim's per creature; people: the characters', for their weapon)
static const char *clip_for(const Creature *c, const char *role) {
	const Kind *k = &kinds[c->kind];
	if (!(k->flags & F_PERSON))
		return role;
	const int w = k->weapon;
	const char *set = w == 0 ? "h2h" : w == 5 ? "w2hm" : w == 6 || w == 8 ? "w2hw" : w == 7 || w == 9 ? "bow" : "w1h";
	static char name[24];
	const int fighting = c->hostile && c->state != WANDER && c->state != IDLE;
	if (!strcmp(role, "idle") || !strcmp(role, "combat")) {
		if (!fighting || w == 0)
			return "idle";
		snprintf(name, sizeof(name), "%s_idle", set);
		return name;
	}
	if (!strcmp(role, "walk") || !strcmp(role, "run") || !strcmp(role, "trot")) {
		const int run = strcmp(role, "walk") != 0;
		if (!fighting)
			return run ? "run" : "walk";
		snprintf(name, sizeof(name), "%s_%s", w == 7 || w == 9 ? "bow" : "1hm", run ? "run" : "walk");
		return name;
	}
	if (!strcmp(role, "attack") || !strcmp(role, "attack2") || !strcmp(role, "power")) {
		if (!strcmp(role, "power") && strcmp(set, "h2h") && strcmp(set, "bow"))
			snprintf(name, sizeof(name), "%s_power", set);
		else
			snprintf(name, sizeof(name), "%s_atk_%c", !strcmp(set, "bow") ? "w1h" : set, !strcmp(role, "attack2") ? 'l' : 'r');
		return name;
	}
	if (!strcmp(role, "recoil"))
		return "w1h_hit";
	if (!strcmp(role, "block")) // (its guard up: its weapon's block)
		return w == 5 ? "w2hm_block" : w == 6 || w == 8 ? "w2hw_block" : "w1h_block";
	return role;
}

// Its sounds: "<clip set>_<what>" (converter/extract_sounds.py); people: their weapon's
static void voice(const Creature *c, const char *what) {
	const Kind *k = &kinds[c->kind];
	if (k->flags & F_PERSON) { // (people: their weapon's swing, and their grunts - Skyrim's per voice)
		const int swing = !strcmp(what, "attack") || !strcmp(what, "power");
		if (swing)
			sfx_play_at(k->weapon >= 5 ? "wpn_swing_2h" : k->weapon == 3 ? "wpn_swing_axe" : k->weapon == 0
			            ? "wpn_swing_unarmed" : "wpn_swing_blade", c->pos, 25.0f);
		// (a taunt now and then - Skyrim's combat dialogue isn't every swing)
		static float last_taunt[MAX_LIVE];
		const int idx = (int)(c - live);
		const float now = combat_clock;
		const int taunt = swing && now - last_taunt[idx] > TAUNT_EVERY_S && frand() < 0.5f;
		if (taunt)
			last_taunt[idx] = now;
		const char *grunt = !strcmp(what, "hurt") ? "hit" : !strcmp(what, "death") ? "death" : taunt ? "attack" : NULL;
		if (grunt && k->voice[0]) {
			char event[40];
			snprintf(event, sizeof(event), "v_%s_%s", k->voice, grunt);
			sfx_play_at(event, c->pos, 25.0f);
		}
		return;
	}
	char event[48];
	snprintf(event, sizeof(event), "%s_%s", k->set, what);
	sfx_play_at(event, c->pos, 30.0f);
}

static void play(Creature *c, const char *role) {
	const char *clip = clip_for(c, role);
	if (strcmp(c->clip, clip))
		snprintf(c->clip, sizeof(c->clip), "%s", clip), c->clip_t = 0.0f;
}

static float clip_len(const Creature *c, const char *clip, float *speed) {
	const float len = actors_beast_clip(kinds[c->kind].actor_kind, clip, speed);
	if (speed)
		*speed *= kinds[c->kind].scale;
	return len;
}

// The ground under a point off the navmesh: the terrain (inside: where it was)
static float ground_at(float x, float z, float fallback) {
	float h;
	if (terrain_height_at(x, z, &h) && h > -1e5f && fabsf(h - fallback) < 3.0f)
		return h;
	// (inside - or under an overhang: the floor below by the collision, not the terrain; an
	// alcove's draugr stepping out comes down to the floor rather than walking on air)
	const float top[3] = {x, fallback + 1.2f, z}, bottom[3] = {x, fallback - 8.0f, z};
	const float t = collision_ray_ground(top, bottom);
	return t < 1.0f ? top[1] + (bottom[1] - top[1]) * t : fallback;
}

// Onto the navmesh near where it stands (a spawn point beside a rock: on the walkable ground)
static void settle(Creature *c) {
	float at[3], h;
	c->on_nav = 0;
	if (kinds[c->kind].flags & F_WATER) // (a slaughterfish: swimming where it was placed, not on the riverbed)
		return;
	if (navmesh_locate(c->pos[0], c->pos[1], c->pos[2], &c->nav, &h)) {
		c->pos[1] = h, c->on_nav = 1;
	} else if (navmesh_nearest(c->pos[0], c->pos[1], c->pos[2], 6.0f, &c->nav, at)) {
		memcpy(c->pos, at, sizeof(at));
		c->on_nav = 1;
	} else {
		c->pos[1] = ground_at(c->pos[0], c->pos[2], c->pos[1]);
	}
}

// One step from where it is: on the navmesh (it won't walk into rocks and walls), else on the
// ground; returns 0 if the step was blocked
static int step_to(Creature *c, float nx, float nz) {
	if (kinds[c->kind].flags & F_WATER) { // (in the water only: where the bed is below its swimming depth)
		const float swim = spawns[c->spawn].pos[1];
		if (ground_at(nx, nz, swim) > swim - WATER_CLEARANCE_M)
			return 0;
		c->pos[0] = nx, c->pos[2] = nz, c->pos[1] = swim;
		return 1;
	}
	if (c->on_nav) {
		NavRef ref = c->nav;
		float h;
		if ((navmesh_track(&ref, nx, nz, &h) || navmesh_locate_level(nx, c->pos[1], nz, 1.0f, &ref, &h)) &&
		    fabsf(h - c->pos[1]) < 1.2f) { // (across a step's edge the walk finds the floor again)
			c->nav = ref, c->pos[0] = nx, c->pos[2] = nz, c->pos[1] = h;
			return 1;
		}
		return 0;
	}
	c->pos[0] = nx, c->pos[2] = nz;
	c->pos[1] = ground_at(nx, nz, c->pos[1]);
	float h;
	if (navmesh_locate(c->pos[0], c->pos[1], c->pos[2], &c->nav, &h))
		c->pos[1] = h, c->on_nav = 1;
	return 1;
}

// Turns toward (tx, tz) and moves at speed; returns the distance left
static float head_for(Creature *c, float tx, float tz, float speed, float dt) {
	const float dx = tx - c->pos[0], dz = tz - c->pos[2], d = hypotf(dx, dz);
	if (d < 0.05f)
		return d;
	const float want = atan2f(-dx, -dz), diff = wrap(want - c->heading), turn = TURN_RATE * dt;
	c->heading = wrap(c->heading + (fabsf(diff) < turn ? diff : copysignf(turn, diff)));
	if (c->slow_left > 0.0f) // (Frost Breath's chill)
		speed *= SLOW_MULT;
	const float step = fminf(d, speed * dt) * fmaxf(0.0f, cosf(diff)); // (turning first, then off)
	if (step > 0.0f && !step_to(c, c->pos[0] - sinf(c->heading) * step, c->pos[2] - cosf(c->heading) * step)) {
		// Blocked: along whichever side is open, nearest the way first (a rock's corner, a step's
		// edge, the slope of a hill)
		static const float turns[] = {0.6f, -0.6f, 1.2f, -1.2f, 1.9f, -1.9f};
		int moved = 0;
		const float side = c->strafe != 0.0f ? c->strafe : 1.0f;
		for (int k = 0; k < 6 && !moved; k++) {
			const float h = c->heading + turns[k] * side;
			moved = step_to(c, c->pos[0] - sinf(h) * step, c->pos[2] - cosf(h) * step);
		}
		c->stuck_t = moved ? fmaxf(0.0f, c->stuck_t - dt) : c->stuck_t + dt;
	} else if (step > 0.0f) {
		c->stuck_t = fmaxf(0.0f, c->stuck_t - dt * 2.0f);
	}
	return d;
}

// Stuck a while (blocked every way it tried): a fresh path, the next corner of the old one
// skipped; longer, put back on the navmesh a little way on (Skyrim's actors are nudged free so)
static void unstick(Creature *c, float gx, float gz) {
	if (c->stuck_t < STUCK_REPLAN_S)
		return;
	c->replan = 0.0f;
	if (c->path_i < c->path_n)
		c->path_i++;
	if (c->stuck_t < STUCK_NUDGE_S)
		return;
	// (toward its path's next point, not straight at its goal - through the wall it was caught on)
	const float *next = c->path_i < c->path_n ? &c->path[c->path_i * 3] : NULL;
	const float dx = (next ? next[0] : gx) - c->pos[0], dz = (next ? next[2] : gz) - c->pos[2], l = fmaxf(hypotf(dx, dz), 0.01f);
	NavRef ref;
	float at[3];
	if (navmesh_nearest(c->pos[0] + dx / l * STUCK_NUDGE_M, c->pos[1], c->pos[2] + dz / l * STUCK_NUDGE_M, 2.0f, &ref, at) &&
	    fabsf(at[1] - c->pos[1]) < 1.5f) {
		memcpy(c->pos, at, sizeof(at));
		c->nav = ref, c->on_nav = 1;
	}
	c->stuck_t = 0.0f, c->strafe = -c->strafe;
}

// Goes to (gx, gz) along the navmesh's path (straight on where there's none)
// To (gx, gz) on the floor at about gy - another level's (you up on a bridge, a ledge: the way up
// found, not the floor under you - the draugr below swung at you through it)
static float go_to_level(Creature *c, float gx, float gy, float gz, float speed, float dt) {
	c->replan -= dt;
	const float d = hypotf(gx - c->pos[0], gz - c->pos[2]);
	if (c->on_nav && (c->replan <= 0.0f || hypotf(gx - c->goal[0], gz - c->goal[1]) > 2.0f || fabsf(gy - c->goal_y) > 1.5f)) {
		c->replan = REPLAN_S, c->goal[0] = gx, c->goal[1] = gz, c->goal_y = gy, c->path_n = c->path_i = 0;
		NavRef to;
		float h, t[3] = {gx, gy, gz};
		int found = navmesh_locate_level(gx, gy, gz, 2.5f, &to, &h);
		if (found)
			t[1] = h;
		else
			found = navmesh_nearest_level(gx, gy, gz, 4.0f, 2.5f, &to, t);
		if (found) {
			const float f[3] = {c->pos[0], c->pos[1], c->pos[2]};
			c->path_n = navmesh_path(c->nav, f, to, t, c->path, MAX_PATH);
		}
	}
	if (c->path_i < c->path_n) {
		const float *p = &c->path[c->path_i * 3];
		if (head_for(c, p[0], p[2], speed, dt) < 0.5f)
			c->path_i++;
		unstick(c, gx, gz);
		return d;
	}
	head_for(c, gx, gz, speed, dt);
	unstick(c, gx, gz);
	return d;
}

static float go_to(Creature *c, float gx, float gz, float speed, float dt) {
	return go_to_level(c, gx, c->pos[1], gz, speed, dt);
}

static int dragon_for_level(void);

static void wake(int s) {
	Spawn *sp = &spawns[s];
	if ((kinds[sp->kind].flags & F_DRAGON) && sp->ambush == 2) { // (a lair's: Skyrim's are leveled)
		const int dk = dragon_for_level();
		if (dk >= 0)
			sp->kind = dk;
	}
	Kind *k = &kinds[sp->kind];
	if (!kind_ready(k))
		return;
	int slot = -1;
	for (int i = 0; i < MAX_LIVE && slot < 0; i++)
		if (!live[i].used)
			slot = i;
	if (slot < 0)
		return;
	const int beast = actors_beast_spawn(k->actor_kind);
	actors_beast_scale(beast, k->scale);
	if (beast < 0)
		return;
	Creature *c = &live[slot];
	memset(c, 0, sizeof(*c));
	c->used = 1, c->spawn = s, c->kind = sp->kind, c->beast = beast;
	c->route_i = sp->route_n > 0 ? s % sp->route_n : 0; // (a patrol: picked up somewhere along it)
	memcpy(c->pos, sp->pos, sizeof(c->pos));
	if (sp->route_n > 0) // (a patrol: picked up at one of its markers, not all of them at the barracks)
		c->pos[0] = route_pts[sp->route_first + c->route_i][0], c->pos[2] = route_pts[sp->route_first + c->route_i][1];
	settle(c);
	c->heading = sp->heading, c->health = k->health;
	if (sp->health0 > 0.0f && !(k->flags & F_DRAGON)) { // (leveled as it wakes: Skyrim's, by the player's level now)
		int level = sp->lv_mult ? (int)lroundf(sp->lv_mult / 1000.0f * (float)skills_state()->player_level) : sp->lv;
		if (level < sp->lv_min)
			level = sp->lv_min;
		if (sp->lv_max > 0 && level > sp->lv_max)
			level = sp->lv_max;
		c->health = sp->health0 + sp->health_per * (float)(level > 1 ? level : 1);
		c->level = level;
	} else {
		c->level = sp->lv > 0 ? sp->lv : skills_state()->player_level; // (unknown: the player's)
	}
	c->max_health = c->health;
	c->hostile = k->aggression >= 2;
	if (sp->side >= SIDE_THALMOR) // (Diplomatic Immunity's: hostile once you've left the party - the escape's from the first)
		c->hostile = sp->side == SIDE_ESCAPE || (sp->side == SIDE_THALMOR && sp->ref != challenger_ref &&
		                                         quests_stage(Q_DIPLOMATIC) >= THALMOR_ALARM_STAGE);
	c->parley = 0, c->parley_go = 0.0f, c->disguise_t = 0.0f, c->disguise_fooled = -1;
	c->state = sp->ambush ? AMBUSH : IDLE, c->timer = 2.0f + frand() * 4.0f;
	c->hand_to_hand = sp->side == 1 || sp->side == 2;
	if (sp->ambush == 5 || !strcmp(kinds[sp->kind].title, T_NPC_00039646_FULL0)) { // (Bleak Falls Barrow: in the web with the Golden Claw)
		c->held = 1, c->hostile = 0, c->aware = 0.0f, c->state = IDLE;
		memcpy(c->pos, sp->pos, sizeof(c->pos)); // (where Skyrim's web holds him: off the floor)
		c->heading = sp->heading;
	}
	if (sp->ambush == 4) // (one of Skyrim's placed dead - Gunjar: lying there from the first, to be searched)
		c->health = 0.0f, c->state = DEAD, c->clip[0] = 0, c->clip_t = 30.0f, c->hand_to_hand = 0,
		game_log("creatures: a body lies at %.0f %.0f %.0f (%s)", sp->pos[0], sp->pos[1], sp->pos[2], kinds[sp->kind].title); // (an archer at its post, out of sight down a passage, just stood there)
	c->bones_beast = -1;
	if (k->flags & F_DRAGON) { // (perched at its lair; one flying in: summon_dragon sets it going)
		c->state = DRAGON, c->hostile = 1, c->dphase = D_PERCH, c->roar_t = 4.0f + frand() * 4.0f;
		c->pos[0] = sp->pos[0], c->pos[2] = sp->pos[2];
		c->pos[1] = dragon_ground(sp->pos[0], sp->pos[2], sp->pos[1]);
		actors_beast_look(beast, 1, 0.0f);
	}
	c->strafe = frand() < 0.5f ? -1.0f : 1.0f;
	c->shout_cd = 2.0f + frand() * 2.0f;
	actors_beast_fall(beast, 0.0f, 0.0f);
	play(c, "idle");
	sp->live = slot;
}

static void sleep_creature(int i) {
	Creature *c = &live[i];
	actors_beast_remove(c->beast);
	if (c->bones_beast >= 0 && c->bones_beast != c->beast)
		actors_beast_remove(c->bones_beast);
	c->bones_beast = -1;
	spawns[c->spawn].live = -1;
	if (spawns[c->spawn].transient && !spawns[c->spawn].quest) // (a dragon met at random: flown off, or its bones left behind)
		spawns[c->spawn].kind = -1;
	if (target == i)
		target = -1;
	c->used = 0;
}

// Whether it can see the player from where it stands (nothing solid between their eyes)
static int sees(const Creature *c, const float player[3]) {
	const float head[3] = {player[0], player[1] + 1.5f, player[2]};
	const float dx = player[0] - c->pos[0], dz = player[2] - c->pos[2], d = hypotf(dx, dz);
	// (from its eyes, a little toward you: backed against a wall, its middle is in the wall's
	// face and the ray from there never got out - an archer at your elbow that never shot)
	const float lean = d > 0.01f ? fminf(kinds[c->kind].radius + 0.15f, d * 0.4f) / d : 0.0f;
	const float eye[3] = {c->pos[0] + dx * lean, c->pos[1] + kinds[c->kind].height * 0.8f, c->pos[2] + dz * lean};
	if (collision_ray(eye, head) >= 1.0f)
		return 1;
	const float chest[3] = {player[0], player[1] + 1.0f, player[2]}; // (or your middle, past a low beam)
	return collision_ray(eye, chest) >= 1.0f;
}

static float player_block[2]; // (the player moved out of those it walked into, since last asked)

void creatures_take_block(float out[2]) {
	out[0] = player_block[0], out[1] = player_block[1];
	player_block[0] = player_block[1] = 0.0f;
}

// They don't stand in each other, nor in the player: pushed apart (the player, out of them)
static void keep_apart(const float player[3]) {
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *a = &live[i];
		if (!a->used || a->state == DEAD || a->state == AMBUSH || a->state == RISE)
			continue;
		const float ra = kinds[a->kind].radius;
		for (int j = i + 1; j < MAX_LIVE; j++) {
			Creature *b = &live[j];
			if (!b->used || b->state == DEAD || b->state == AMBUSH || b->state == RISE)
				continue;
			const float dx = b->pos[0] - a->pos[0], dz = b->pos[2] - a->pos[2], d = hypotf(dx, dz);
			const float want = (ra + kinds[b->kind].radius) * 1.1f;
			if (d >= want || fabsf(a->pos[1] - b->pos[1]) > 2.0f)
				continue;
			const float push = (want - d) * 0.5f, ux = d > 0.01f ? dx / d : 1.0f, uz = d > 0.01f ? dz / d : 0.0f;
			step_to(a, a->pos[0] - ux * push, a->pos[2] - uz * push);
			step_to(b, b->pos[0] + ux * push, b->pos[2] + uz * push);
		}
		const float dx = a->pos[0] - player[0], dz = a->pos[2] - player[2], d = hypotf(dx, dz);
		const float want = ra + 0.4f;
		if (d < want && d > 0.01f && fabsf(a->pos[1] - player[1]) < 1.8f) {
			// (you're stopped by it, as by anyone in Skyrim - you could push a familiar about; a fight's
			// crowd still gives a little)
			const float give = a->hostile && a->state != IDLE ? 0.3f : 0.0f, over = want - d;
			if (give > 0.0f)
				step_to(a, a->pos[0] + dx / d * over * give, a->pos[2] + dz / d * over * give);
			player_block[0] -= dx / d * over * (1.0f - give), player_block[1] -= dz / d * over * (1.0f - give);
		}
	}
}

// Its blow landing on the player (if still in reach, in front, at its height)
static int struck_by_weapon = -2; // the last blow on the player: its weapon kind (-1 claws or teeth), -2 none

static void hurt(int i, float damage, int stagger);
static int cur_victim = -1; // (the one being updated is after: -1 you, else this ally - MAX_LIVE + n: friend n)

// People on your side (your follower, the allies a quest gives you): enemies go for them as for
// you - where they stand, the harm done them since asked (main / followers.c keep their health)
#define MAX_FRIENDS 8
static float friend_at[MAX_FRIENDS][3], friend_hurt[MAX_FRIENDS];
static int friend_on[MAX_FRIENDS];

void creatures_set_friend(int slot, const float pos[3], int on) {
	if (slot < 0 || slot >= MAX_FRIENDS)
		return;
	friend_on[slot] = on;
	if (on)
		memcpy(friend_at[slot], pos, sizeof(friend_at[slot]));
}

float creatures_take_friend_damage(int slot) {
	if (slot < 0 || slot >= MAX_FRIENDS)
		return 0.0f;
	const float d = friend_hurt[slot];
	friend_hurt[slot] = 0.0f;
	return d;
}

// Harm on whoever it's after but you: an ally of yours (a summons) or a friend
static void hurt_victim(int v, float damage, int stagger) {
	if (v >= MAX_LIVE) {
		if (v - MAX_LIVE < MAX_FRIENDS)
			friend_hurt[v - MAX_LIVE] += damage;
		return;
	}
	blame = 0, hurt(v, damage, stagger), blame = 1;
}

static const char *title_of(const Creature *c);
static int last_striker = -1, struck_power; // (the last blow on you: who, and a power attack's)

static void strike(Creature *c, const float player[3], float dist, float dx, float dz) {
	const Kind *k = &kinds[c->kind];
	const float facing = (-sinf(c->heading) * dx - cosf(c->heading) * dz) / fmaxf(dist, 0.01f);
	if (dist < k->reach + k->radius + 0.8f && facing > 0.3f && fabsf(player[1] - c->pos[1]) < fmaxf(k->height, 1.6f)) {
		// (Skyrim's attack data: a power attack, a creature's bite - poisoned, a spider's - and
		// its other swipes)
		const int power = !strcmp(c->clip, clip_for(c, "power")), bite = !strcmp(c->clip, clip_for(c, "attack2"));
		if (cur_victim >= 0) { // (your summons it's fighting: on it)
			hurt_victim(cur_victim, k->damage * (power ? k->power_mult : bite ? k->bite_mult : 1.0f), 1);
			return;
		}
		damage_taken += k->damage * (power ? k->power_mult : bite ? k->bite_mult : 1.0f);
		last_striker = (int)(c - live), struck_power |= power && (k->flags & (F_PERSON | F_UPRIGHT));
		struck_by_weapon = (k->flags & F_PERSON) ? k->weapon : k->weapon ? k->weapon : -1; // (main: its sound)
		if ((power || bite) && k->bite_dps > 0.0f)
			creatures_poison_player(k->bite_dps, k->bite_s);
	}
}

// The last blow on you was a power attack (a person's, a draugr's): it staggers you unless blocked
int creatures_take_power_struck(void) {
	const int p = struck_power;
	struck_power = 0;
	return p;
}

// Your raised guard took its blow: it recoils (Skyrim's: a blocked attack bounces off)
void creatures_recoil_striker(void) {
	if (last_striker < 0 || last_striker >= MAX_LIVE || !live[last_striker].used || live[last_striker].state != ATTACK)
		return;
	Creature *c = &live[last_striker];
	if (kinds[c->kind].flags & F_DRAGON)
		return;
	c->state = FLINCH, c->timer = FLINCH_S, c->clip[0] = 0, c->block_t = 0.0f;
	game_log("creatures: %s recoils off your guard", title_of(c));
	last_striker = -1;
}

int creatures_reflect_striker(float damage) {
	if (damage <= 0.0f || last_striker < 0 || last_striker >= MAX_LIVE || !live[last_striker].used || live[last_striker].state == DEAD)
		return 0;
	const int was = blame;
	blame = 1; // (its own blow, turned back on it: yours)
	hurt(last_striker, damage, 1);
	blame = was;
	game_log("creatures: %s takes its own blow back (%.0f)", title_of(&live[last_striker]), damage);
	return 1;
}

int creatures_hit_all(const float eye[3], const float dir[3], float reach, float cone_cos, float damage) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD || c->ally)
			continue;
		const float d[3] = {c->pos[0] - eye[0], 0.0f, c->pos[2] - eye[2]};
		const float flat = hypotf(d[0], d[2]);
		if (flat > reach + kinds[c->kind].radius + 0.4f || fabsf(c->pos[1] + 0.9f - eye[1]) > 2.5f)
			continue;
		if ((d[0] * dir[0] + d[2] * dir[2]) / (fmaxf(flat, 0.01f) * fmaxf(hypotf(dir[0], dir[2]), 0.01f)) < cone_cos &&
		    flat > kinds[c->kind].radius)
			continue;
		hurt(i, damage * player_mult, 1);
		n++;
	}
	return n;
}

int creatures_lose_track(const float player[3], float within) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (!c->used || c->state == DEAD || c->ally || !c->hostile || (kinds[c->kind].flags & F_DRAGON) ||
		    hypotf(c->pos[0] - player[0], c->pos[2] - player[2]) < within)
			continue;
		c->aware = 0.5f, c->state = ALERT, c->timer = 4.0f; // (where did you go? - it searches where it last saw you)
		c->seen_at[0] = player[0], c->seen_at[1] = player[2];
		n++;
	}
	return n;
}

static Creature *struck_live(void);

int creatures_banish_struck(void) {
	Creature *c = struck_live();
	if (!c || c->ally || !c->conjured)
		return 0;
	game_log("creatures: %s banished", title_of(c));
	hurt((int)(c - live), c->health + 1.0f, 0);
	return 1;
}

void creatures_set_spell_element(int element) {
	spell_element = element;
}

int creatures_take_struck(void) {
	const int w = struck_by_weapon;
	struck_by_weapon = -2;
	return w;
}

// The player's poison (a spider's bite or spit): damage a second, seconds left. Skyrim's
// poison is a magic effect: armour doesn't stop it; the same again restarts it
static float poison_dps, poison_left;

void creatures_poison_player(float dps, float seconds) {
	if (poison_left <= 0.0f || dps >= poison_dps)
		poison_dps = dps;
	poison_left = fmaxf(poison_left, seconds);
}

void creatures_cure_player_poison(void) {
	poison_left = 0.0f;
}

float creatures_take_poison(float dt) {
	if (poison_left <= 0.0f)
		return 0.0f;
	const float t = fminf(dt, poison_left);
	poison_left -= dt;
	return poison_dps * t;
}

int creatures_poisoned(void) {
	return poison_left > 0.0f;
}

static void hurt(int i, float damage, int stagger);
static int dragon_fell; // (a dragon killed, since last asked)
// Sneak attacks (Skyrim's: unseen, a blow x3, an arrow x2 - Backstab, Assassin's Blade, Deadly Aim more)
static const char *title_of(const Creature *c);
static float sneak_melee = 3.0f, sneak_ranged = 2.0f;
static int sneaking_now, ray_sneaks;
static float sneak_bonus(int i, float mult) {
	const Creature *c = &live[i];
	if (!sneaking_now || mult <= 1.0f || c->aware >= 1.0f || c->state == CHASE || c->state == ATTACK || c->state == SHOUT)
		return 1.0f;
	char text[48];
	snprintf(text, sizeof(text), "Sneak attack for %.1fx damage!", mult);
	ui_notice(text, 2.5f);
	game_log("creatures: sneak attack on %s x%.1f", title_of(c), mult);
	return mult;
}

// Falling over (dead, or thrown down): how far the body tips, and which way. The engine tips
// people about their side (positive: forward onto the face - actors_beast_fall's pitch -e) and
// beasts about their length (positive: onto their left side). The floor is sampled along the
// way it falls, and it comes to rest along it - on flat ground flat, on stairs or a slope
// rising that way propped along them, over a drop past level - never through a step or a wall
static float lie_toward(const Creature *c, const Kind *k, float sign) {
	const int person = (k->flags & (F_PERSON | F_UPRIGHT)) != 0; // (two legs: back or face)
	const float hs = sinf(c->heading), hc = cosf(c->heading);
	const float bx = sign * (person ? -hs : -hc), bz = sign * (person ? -hc : hs);
	const float length = person ? k->height * 0.92f : fmaxf(k->height * 0.6f, k->radius);
	float a = MAX_LIE_RAD;
	for (int s = 1; s <= 4; s++) {
		const float d = length * s / 4.0f;
		const float top[3] = {c->pos[0] + bx * d, c->pos[1] + length + 0.3f, c->pos[2] + bz * d};
		const float bottom[3] = {top[0], c->pos[1] - length - 0.5f, top[2]};
		const float t = collision_ray_ground(top, bottom);
		const float rise = t < 1.0f ? top[1] + (bottom[1] - top[1]) * t - c->pos[1] : -length - 0.5f;
		// (the body's line from the feet, tipped by a, passes d out at d / tan(a) up: kept above
		// the floor there)
		a = fminf(a, atan2f(d, rise + LIE_CLEAR_M));
	}
	// (a wall that way: it slumps against it)
	const float from[3] = {c->pos[0], c->pos[1] + LIE_WALL_TEST_M, c->pos[2]};
	const float to[3] = {from[0] + bx * length, from[1], from[2] + bz * length};
	const float w = collision_ray(from, to) * length - LIE_CLEAR_M * 3.0f;
	if (w < length)
		a = fminf(a, asinf(fmaxf(0.0f, w) / length));
	return fmaxf(a, 0.0f);
}

// A body dying by its own clip, laid along the ground it falls on: the floor sampled ahead of
// and behind it, either side (stairs, a slope) - its pitch and roll, and its middle's height.
// (The clip lies flat at the spot it died on: on stairs the body stuck out over the steps.)
// The floor under a body: what's below it within reach (a tower's planks, a barrow's steps) - not the land
// under a building (a bandit killed on a tower's floor lay half through it, on the ground below)
static float body_floor(float x, float z, float y) {
	const float top[3] = {x, y + BODY_RAY_UP_M, z}, bottom[3] = {x, y - BODY_RAY_DOWN_M, z};
	const float t = collision_ray_ground(top, bottom);
	float h, best = t < 1.0f ? top[1] + (bottom[1] - top[1]) * t : -1e9f;
	if (terrain_height_at(x, z, &h) && h > -1e5f && h <= y + BODY_RAY_UP_M && h > best)
		best = h; // (the land, where it's higher than any floor found - out in the open)
	return best > -1e8f ? best : y;
}

// Laid along the floor where it lies: five points along its length, a line through them, and no part of it
// under any of them (two points 0.8 m out from its middle: down stairs it floated over the lower steps and
// sank into the upper; a fall one way lies mostly to that side)
static void body_slope(Creature *c, const Kind *k) {
	const float hs = sinf(c->heading), hc = cosf(c->heading);
	const float half = fmaxf(k->height * 0.55f, k->radius * 1.2f), side = fmaxf(k->radius * 0.8f, 0.3f);
	float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f, d[5], h[5];
	for (int i = 0; i < 5; i++) { // (from ahead to behind along its length)
		d[i] = half * (1.0f - i * 0.5f);
		h[i] = body_floor(c->pos[0] - hs * d[i], c->pos[2] - hc * d[i], c->pos[1]);
		sx += d[i], sy += h[i], sxx += d[i] * d[i], sxy += d[i] * h[i];
	}
	const float den = 5.0f * sxx - sx * sx;
	float slope = den > 1e-4f ? (5.0f * sxy - sx * sy) / den : 0.0f; // (height gained a metre ahead)
	slope = fmaxf(-0.85f, fminf(0.85f, slope));
	float mid = sy / 5.0f - slope * sx / 5.0f;
	for (int i = 0; i < 5; i++) // (resting on the highest of them: none of it in a step)
		mid = fmaxf(mid, h[i] - slope * d[i] - BODY_SINK_OK_M);
	const float hl = body_floor(c->pos[0] - hc * side, c->pos[2] + hs * side, c->pos[1]);
	const float hr = body_floor(c->pos[0] + hc * side, c->pos[2] - hs * side, c->pos[1]);
	c->slope_pitch = fmaxf(-0.7f, fminf(0.7f, atanf(slope))); // (ahead lower: nose down - as atan2(ahead - behind) was)
	c->slope_roll = fmaxf(-0.5f, fminf(0.5f, atan2f(hr - hl, side * 2.0f)));  // (left lower: over left)
	c->pos[1] = mid;
	c->sloped = 1;
}

// The way it falls (signed: see lie_toward) - whichever lets it lie down flattest, its usual
// way when both do (people forward, Skyrim's dead mostly crumple so; thrown, `prefer`)
static float lie_angle(const Creature *c, const Kind *k, float prefer) {
	const float a = lie_toward(c, k, prefer), b = lie_toward(c, k, -prefer);
	return b > a + LIE_PREFER_RAD ? -prefer * b : prefer * a;
}

// --- Dragons -------------------------------------------------------------------------------
// Skyrim's dragons (K flags 64): a lair's waits perched (its word wall's mound) till you come
// near, then takes off; one met at random flies in from afar. In the air it circles you,
// swoops over breathing fire (or frost) along its path, hovers to breathe, and in time lands
// near you; on the ground it turns to face you (it can't strike while turning, as in Skyrim),
// bites what's in front, whips its tail at what's behind, beats its wings at what's beside it,
// breathes at you further off, and takes off again. Flying, it banks into its turns and dives
// and climbs; hurt below a third of its health in the air it falls - its glide failing - and
// crashes, ploughing the ground, never to fly again. Dead, its skin burns away from its bones
// and its soul streams into you (a word of a shout unlocked).

#define DRAGON_WAKE_M 170.0f      // a lair's dragon is there from this far (perched)
#define DRAGON_SLEEP_M 700.0f     // ... and gone past this
#define DRAGON_ALERT_M 90.0f      // perched, it takes off when you come this near (or it's struck)
#define DRAGON_SPEED_MS 21.0f     // gliding (Skyrim's dragons: about 1500 units a second)
#define DRAGON_TURN_RAD 0.9f      // a second, at most
#define DRAGON_ORBIT_M 45.0f      // circling you this far out...
#define DRAGON_ALT_M 24.0f        // ... this high
#define DRAGON_PASS_ALT_M 11.0f   // swooping over you this high
#define DRAGON_BREATH_M 22.0f     // its breath reaches this far...
#define DRAGON_BREATH_COS 0.92f   // ... in a cone this wide (23 degrees either side)
#define DRAGON_CRASH_SHARE 0.32f  // hurt below this share of its health in the air, it crashes
#define DRAGON_GROUND_S 32.0f     // on the ground this long before it takes off again
#define DRAGON_BREATH_EVERY_S 7.0f
#define DRAGON_HIT_AT 0.45f       // its bite / tail / wing lands this far into the clip
#define DRAGON_MIN_LEVEL 3        // random dragons once the player's this far along...
#define DRAGON_EVERY_H 38.0f      // ... about this many game hours apart (outdoors; 30 was too often)
#define DRAGON_ARRIVE_M 320.0f    // ... flying in from this far

enum { ACT_NONE, ACT_TURN, ACT_BITE, ACT_TAIL, ACT_WING, ACT_INHALE, ACT_BREATHE, ACT_WALK };

static float breath_taken;     // the player's share of dragons' breath since asked
static int breath_element_taken;
static int souls_taken;        // dragon souls absorbed since asked
static float next_dragon_h = -1.0f;

static int airborne(const Creature *c) {
	return c->dphase >= D_TAKEOFF && c->dphase <= D_LAND && c->dphase != D_GROUND;
}

static float dragon_ground(float x, float z, float fallback) {
	// (the land, and what stands on it: steps, ruins, rocks - landing on Bleak Falls' steps it sank
	// into them, flying low it went through the barrow's walls)
	float h;
	const float land = terrain_height_at(x, z, &h) && h > -1e5f ? h : fallback;
	const float top[3] = {x, land + 40.0f, z}, bottom[3] = {x, land - 1.0f, z};
	const float t = collision_ray_ground(top, bottom);
	return t < 1.0f ? fmaxf(land, top[1] + (bottom[1] - top[1]) * t) : land;
}

// Its mouth: ahead of its middle (the body's length is about its radius / 0.35)
static void dragon_mouth(const Creature *c, float out[3]) {
	const Kind *k = &kinds[c->kind];
	const float ahead = k->radius * 1.15f;
	out[0] = c->pos[0] - sinf(c->heading) * ahead, out[2] = c->pos[2] - cosf(c->heading) * ahead;
	out[1] = c->pos[1] + k->height * (airborne(c) ? 0.45f : 0.6f);
}

// Steering in flight: toward (tx, ty, tz) at speed, turning at most so fast, banking into
// the turn and pitching with the climb; never into the ground
static void fly_toward(Creature *c, float tx, float ty, float tz, float speed, float dt) {
	const float want = atan2f(-(tx - c->pos[0]), -(tz - c->pos[2]));
	const float diff = wrap(want - c->heading);
	const float turn = fmaxf(-DRAGON_TURN_RAD * dt, fminf(DRAGON_TURN_RAD * dt, diff));
	c->heading = wrap(c->heading + turn);
	const float rate = dt > 0.0f ? turn / dt : 0.0f; // (+: turning left; it leans that way)
	c->bank += (fmaxf(-0.6f, fminf(0.6f, rate * 0.75f)) - c->bank) * fminf(1.0f, dt * 2.5f);
	c->speed += (speed - c->speed) * fminf(1.0f, dt * 0.9f);
	const float want_climb = fmaxf(-9.0f, fminf(7.0f, (ty - c->pos[1]) * 0.8f));
	c->climb += (want_climb - c->climb) * fminf(1.0f, dt * 1.5f);
	c->pos[0] += -sinf(c->heading) * c->speed * dt, c->pos[2] += -cosf(c->heading) * c->speed * dt;
	c->pos[1] += c->climb * dt;
	float floor = dragon_ground(c->pos[0], c->pos[2], c->pos[1] - 30.0f) + 4.0f;
	// (and the land ahead: a mountainside coming - up over it before it's there, not through it)
	for (int k = 1; k <= 3; k++) {
		const float ahead = c->speed * 0.8f * (float)k;
		const float ax = c->pos[0] - sinf(c->heading) * ahead, az = c->pos[2] - cosf(c->heading) * ahead;
		const float g = dragon_ground(ax, az, c->pos[1] - 30.0f) + 6.0f;
		if (g > c->pos[1])
			c->climb = fmaxf(c->climb, fminf(12.0f, (g - c->pos[1]) / (0.8f * (float)k)));
		floor = fmaxf(floor, g - 2.0f * (float)k);
	}
	if (c->pos[1] < floor)
		c->pos[1] = floor, c->climb = fmaxf(c->climb, 0.0f);
	c->pitch_now = atan2f(c->climb, fmaxf(c->speed, 4.0f)) * 0.7f;
}

// Its flight's clip: hovering, beating its wings (climbing), gliding (banked; hurt)
static const char *fly_clip(const Creature *c) {
	if (c->speed < 8.0f)
		return "hover";
	if (c->climb > 3.0f)
		return "flap";
	if (c->bank > 0.25f)
		return "glide_l";
	if (c->bank < -0.25f)
		return "glide_r";
	return c->health < c->max_health * 0.5f ? "glide_hurt" : "glide";
}

// The land between two points (a hill in the way): its height sampled along the line
static int terrain_between(const float a[3], const float b[3]) {
	for (int i = 1; i < 10; i++) {
		const float u = i / 10.0f;
		const float x = a[0] + (b[0] - a[0]) * u, y = a[1] + (b[1] - a[1]) * u, z = a[2] + (b[2] - a[2]) * u;
		float h;
		if (terrain_height_at(x, z, &h) && h > -1e5f && h > y + 0.2f)
			return 1;
	}
	return 0;
}

// Breathing: fire or frost from its mouth toward you (its head following you, within reason),
// the cone burning whatever's in it
static void dragon_breathe(Creature *c, const float player[3], float dt) {
	const Kind *k = &kinds[c->kind];
	float mouth[3];
	dragon_mouth(c, mouth);
	const float head[3] = {player[0], player[1] + 1.2f, player[2]};
	float d[3] = {head[0] - mouth[0], head[1] - mouth[1], head[2] - mouth[2]};
	const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-3f;
	const float fwd[3] = {-sinf(c->heading), 0.0f, -cosf(c->heading)};
	float dir[3] = {d[0] / len, d[1] / len, d[2] / len};
	if (dir[0] * fwd[0] + dir[2] * fwd[2] < 0.4f) // (you're not before it: ahead and down)
		dir[0] = fwd[0] * 0.9f, dir[1] = -0.44f, dir[2] = fwd[2] * 0.9f;
	if ((c->fx_t -= dt) <= 0.0f) {
		spellfx_wave(mouth, dir, DRAGON_BREATH_M, 0.3f, k->breath_element, 1.0f);
		c->fx_t = 0.12f;
	}
	const float cosang = (d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2]) / len;
	if (len < DRAGON_BREATH_M && cosang > DRAGON_BREATH_COS) {
		// Cover: your head, chest and legs each in the open or not (rocks, walls, a hill)
		int open = 0;
		for (int p = 0; p < 3; p++) {
			const float part[3] = {player[0], player[1] + 1.6f - p * 0.65f, player[2]};
			if (collision_ray(mouth, part) >= 1.0f && !terrain_between(mouth, part))
				open++;
		}
		breath_taken += k->breath_dps * dt * open / 3.0f, breath_element_taken = k->breath_element;
	}
	if (!c->breath_on) {
		c->breath_on = 1;
		sfx_play_at(k->breath_element == ELEMENT_FROST ? "shout_frost_3" : "shout_fire_3", mouth, 250.0f);
	}
}

// The nearest hostile dragon within reach of an ally's bow, -1
#define ALLY_DRAGON_M 70.0f
#define ALLY_VOLLEY_S 2.6f
static int dragon_near(const float pos[3], float radius) {
	int best = -1;
	float best_d = radius;
	for (int j = 0; j < MAX_LIVE; j++) {
		const Creature *o = &live[j];
		if (!o->used || o->state == DEAD || !o->hostile || !(kinds[o->kind].flags & F_DRAGON))
			continue;
		const float d = hypotf(o->pos[0] - pos[0], o->pos[2] - pos[2]);
		if (d < best_d)
			best = j, best_d = d;
	}
	return best;
}

// A draugr's shout reaching you (in front of it, in the open): Unrelenting Force shoves you back
// and hurts; Frost Breath its cold; Disarm your weapon out of your hand - Skyrim's
static float shout_taken, shout_push[2];
static int shout_disarm, shout_element;

static void shout_at_player(const Creature *c, const float player[3], float dist) {
	static const char *const OUT[4] = {"draugr_force_b", "draugr_frost_b", "draugr_frost_b", "draugr_disarm_b"};
	const int w = c->shout_word == SH_FORCE ? 0 : c->shout_word == SH_FROST ? 1 : c->shout_word == SH_FIRE ? 2 : 3;
	const float mouth[3] = {c->pos[0], c->pos[1] + kinds[c->kind].height * 0.85f, c->pos[2]};
	sfx_play_at(OUT[w], c->pos, 60.0f);
	const float dx = player[0] - c->pos[0], dz = player[2] - c->pos[2], d = fmaxf(dist, 0.01f);
	const float dir[3] = {dx / d, 0.0f, dz / d};
	if (w == 0) {
		spellfx_wave(mouth, dir, SHOUT_TO_M, 0.6f, ELEMENT_FORCE, 1.0f);
		sfx_play_at("shout_force_2", mouth, 60.0f);
	} else if (w <= 2) {
		spellfx_wave(mouth, dir, SHOUT_TO_M, 0.3f, w == 1 ? ELEMENT_FROST : ELEMENT_FIRE, 1.0f);
	}
	const float chest[3] = {player[0], player[1] + 1.2f, player[2]};
	if (collision_ray(mouth, chest) < 1.0f)
		return; // (round a pillar: it missed)
	const float near = 1.0f - fminf(1.0f, d / SHOUT_TO_M) * 0.5f;
	if (w == 0) {
		shout_taken += 20.0f * near, shout_element = ELEMENT_FORCE;
		shout_push[0] += dir[0] * 7.0f * near, shout_push[1] += dir[2] * 7.0f * near;
	} else if (w <= 2) {
		shout_taken += 30.0f * near, shout_element = w == 1 ? ELEMENT_FROST : ELEMENT_FIRE;
	} else {
		shout_disarm = 1;
	}
}

float creatures_take_shout(float push[2], int *element, int *disarm) {
	const float d = shout_taken;
	push[0] = shout_push[0], push[1] = shout_push[1];
	*element = shout_element, *disarm = shout_disarm;
	shout_taken = 0.0f, shout_push[0] = shout_push[1] = 0.0f, shout_disarm = 0;
	return d;
}

// Its blow landing (bite, tail, wing): you within reach on that side of it
static void dragon_strike(Creature *c, const float player[3], int act) {
	const Kind *k = &kinds[c->kind];
	const float dx = player[0] - c->pos[0], dz = player[2] - c->pos[2], dist = hypotf(dx, dz);
	const float facing = (-sinf(c->heading) * dx - cosf(c->heading) * dz) / fmaxf(dist, 0.01f);
	const int side_ok = act == ACT_BITE ? facing > 0.4f : act == ACT_TAIL ? facing < -0.3f : fabsf(facing) < 0.7f;
	if (dist < k->reach + k->radius * 0.6f && side_ok && fabsf(player[1] - c->pos[1]) < k->height) {
		damage_taken += k->damage * (act == ACT_BITE ? 1.0f : act == ACT_TAIL ? 1.3f : 0.8f);
		struck_by_weapon = -1;
	}
	sfx_play_at(act == ACT_BITE ? "dragon_bite" : act == ACT_TAIL ? "dragon_tail" : "dragon_wing", c->pos, 80.0f);
}

static void dragon_pose(Creature *c, float dt) {
	float t = c->clip_t;
	float speed;
	const float len = clip_len(c, c->clip, &speed);
	const int hold = c->dphase == D_DYING || c->dphase == D_SOUL || c->dphase == D_BONES ||
	                 (c->dphase == D_CRASH && c->state == DEAD);
	if (len > 0.0f)
		t = hold ? fminf(t, len - 0.01f) : fmodf(t, len);
	actors_beast_pose(c->beast, c->pos[0], c->pos[1], c->pos[2], c->heading, c->clip, t, NULL, 0.0f, 0.0f, dt);
	actors_beast_fall(c->beast, airborne(c) || c->dphase == D_FALL ? c->pitch_now : 0.0f,
	                  airborne(c) || c->dphase == D_FALL ? c->bank : 0.0f);
	if (c->bones_beast >= 0 && c->bones_beast != c->beast)
		actors_beast_pose(c->bones_beast, c->pos[0], c->pos[1], c->pos[2], c->heading, c->clip, t, NULL, 0.0f, 0.0f, dt);
}

static const char *title_of(const Creature *c) {
	return c->name ? c->name : kinds[c->kind].title;
}

static void dragon_start(Creature *c, int phase, const char *clip) {
	c->dphase = phase, c->dtime = 0.0f, c->attacks = ACT_NONE, c->hit_done = 0, c->breath_on = 0;
	if (phase >= D_FALL && phase != D_CRASH_UP)
		game_log("creatures: %s dragon phase %d at %.0f %.0f %.0f", title_of(c), phase, c->pos[0], c->pos[1], c->pos[2]);
	if (clip)
		snprintf(c->clip, sizeof(c->clip), "%s", clip), c->clip_t = 0.0f;
}

static void play_clip(Creature *c, const char *clip) {
	if (strcmp(c->clip, clip))
		snprintf(c->clip, sizeof(c->clip), "%s", clip), c->clip_t = 0.0f;
}

static void dragon_update(int i, float dt, const float player[3]) {
	Creature *c = &live[i];
	const Kind *k = &kinds[c->kind];
	c->clip_t += dt, c->dtime += dt, c->shot_t -= dt;
	const float dx = player[0] - c->pos[0], dz = player[2] - c->pos[2], dist = hypotf(dx, dz);
	const float dist3 = sqrtf(dx * dx + dz * dz + (player[1] - c->pos[1]) * (player[1] - c->pos[1]));
	const float ground = dragon_ground(c->pos[0], c->pos[2], c->pos[1]);
	float speed_unused;
	const float len = clip_len(c, c->clip, &speed_unused);
	const int clip_done = len > 0.0f && c->clip_t >= len;
	if (c->state == DEAD && c->dphase < D_FALL) { // killed: from the sky it falls, on the ground it dies
		sfx_play_at("dragon_death", c->pos, 300.0f);
		dragon_fell = 1; // (its last words now - INFO 000BCF14 - not after its soul's taken, drowned out)
		if (airborne(c) || c->pos[1] > ground + 1.5f)
			dragon_start(c, D_FALL, "glide_hurt");
		else
			dragon_start(c, D_DYING, "death");
	}
	if (c->rend_t > 0.0f) // (Dragonrend: brought down - out of the air at once, kept on the ground while it lasts)
		c->rend_t -= dt;
	if (c->state != DEAD && c->rend_t > 0.0f && airborne(c) && c->dphase != D_FALL) {
		dragon_start(c, D_FALL, "glide_hurt");
		sfx_play_at("dragon_injured", c->pos, 300.0f);
	}
	if (c->state != DEAD && !c->grounded && airborne(c) && c->health < c->max_health * DRAGON_CRASH_SHARE) {
		dragon_start(c, D_FALL, "glide_hurt"); // (its glide failing: down it comes)
		sfx_play_at("dragon_injured", c->pos, 300.0f);
	}
	if (c->state != DEAD && c->health < c->max_health * DRAGON_CRASH_SHARE)
		c->grounded = 1;
	// Its voice and wings
	if (c->state != DEAD && (c->roar_t -= dt) <= 0.0f) {
		sfx_play_at(dist3 > 150.0f ? "dragon_roar_far" : "dragon_roar", c->pos, 600.0f);
		c->roar_t = 12.0f + frand() * 10.0f;
	}
	if ((!strcmp(c->clip, "flap") || !strcmp(c->clip, "hover") || !strcmp(c->clip, "takeoff") ||
	     !strcmp(c->clip, "land")) && (c->flap_t -= dt) <= 0.0f)
		sfx_play_at("dragon_flap", c->pos, 150.0f), c->flap_t = 0.85f;
	if (airborne(c) && dist3 < 32.0f && !c->flyby_said)
		sfx_play_at("dragon_flyby", c->pos, 120.0f), c->flyby_said = 1;
	if (dist3 > 60.0f)
		c->flyby_said = 0;
	const int was_breathing = c->breath_on;
	c->breath_on = was_breathing && (c->attacks == ACT_BREATHE);

	switch (c->dphase) {
	case D_PERCH: // waiting at its lair: up when you come near (or it's struck)
		play_clip(c, "idle");
		if (c->calm) {
			c->heading = wrap(c->heading + fmaxf(-0.6f * dt, fminf(0.6f * dt, wrap(atan2f(-dx, -dz) - c->heading))));
			break; // (a scene's: looking on, till the scene wakes it)
		}
		if (dist < DRAGON_ALERT_M || c->health < c->max_health) {
			dragon_start(c, D_TAKEOFF, "takeoff");
			c->land_at[1] = c->pos[1]; // (the ground it rises from)
			sfx_play_at("dragon_roar", c->pos, 600.0f), c->roar_t = 6.0f;
			sfx_play_at("dragon_takeoff", c->pos, 200.0f);
		}
		break;
	case D_TAKEOFF: { // rising off the ground, wings beating (Skyrim's vertical takeoff)
		const float u = len > 0.0f ? fminf(1.0f, c->clip_t / len) : 1.0f, rise = fmaxf(0.0f, (u - 0.25f) / 0.75f);
		c->pos[1] = c->land_at[1] + 16.0f * rise * rise * (3.0f - 2.0f * rise);
		c->heading = wrap(c->heading + fmaxf(-0.5f * dt, fminf(0.5f * dt, wrap(atan2f(-dx, -dz) - c->heading))));
		if (clip_done) {
			dragon_start(c, D_CIRCLE, "flap");
			c->speed = 8.0f, c->climb = 4.0f, c->passes = 0;
		}
		break;
	}
	case D_CIRCLE: { // round you, high: then a pass, a hover, or down to land
		const float ax = c->pos[0] - player[0], az = c->pos[2] - player[2], al = fmaxf(hypotf(ax, az), 1.0f);
		const float a = atan2f(az, ax) + (dist > DRAGON_ORBIT_M * 2.0f ? 0.0f : 0.7f); // (a point ahead on the circle)
		const float r = dist > DRAGON_ORBIT_M * 2.0f ? 0.0f : DRAGON_ORBIT_M;     // (far off: straight at you)
		(void)al;
		fly_toward(c, player[0] + cosf(a) * r, dragon_ground(player[0], player[2], player[1]) + DRAGON_ALT_M,
		           player[2] + sinf(a) * r, DRAGON_SPEED_MS, dt);
		play_clip(c, fly_clip(c));
		if (dist < DRAGON_ORBIT_M * 2.0f && c->dtime > 7.0f + frand() * 3.0f) {
			const float roll = frand();
			if (c->passes >= 2 && roll < 0.45f) {
				// (somewhere near you, on its side of you, to land)
				const float ux = ax / al, uz = az / al;
				c->land_at[0] = player[0] + ux * 18.0f, c->land_at[2] = player[2] + uz * 18.0f;
				c->land_at[1] = dragon_ground(c->land_at[0], c->land_at[2], player[1]);
				dragon_start(c, D_LAND, NULL);
			} else if (roll < 0.7f) {
				dragon_start(c, D_STRAFE, NULL);
			} else {
				dragon_start(c, D_HOVER, NULL);
			}
		}
		break;
	}
	case D_STRAFE: { // a swoop over you, breathing along its way (first out for a run at you)
		const float facing = (-sinf(c->heading) * dx - cosf(c->heading) * dz) / fmaxf(dist, 0.01f);
		const float low = dragon_ground(player[0], player[2], player[1]) + DRAGON_PASS_ALT_M;
		if (c->attacks == ACT_NONE && !c->hit_done && dist < 80.0f && facing < 0.8f) { // (out, to turn and come in)
			const float ux = -dx / fmaxf(dist, 1.0f), uz = -dz / fmaxf(dist, 1.0f);
			fly_toward(c, player[0] + ux * 100.0f, low + 8.0f, player[2] + uz * 100.0f, DRAGON_SPEED_MS, dt);
		} else {
			c->hit_done = 1; // (coming in now)
			fly_toward(c, player[0], low, player[2], DRAGON_SPEED_MS, dt);
		}
		if (c->attacks == ACT_NONE) {
			play_clip(c, fly_clip(c));
			if (dist < 50.0f && facing > 0.6f)
				c->attacks = ACT_INHALE, play_clip(c, "fly_inhale");
			else if (c->dtime > 14.0f)
				dragon_start(c, D_CIRCLE, NULL);
		} else if (c->attacks == ACT_INHALE && clip_done) {
			c->attacks = ACT_BREATHE, play_clip(c, "fly_breathe");
		} else if (c->attacks == ACT_BREATHE) {
			dragon_breathe(c, player, dt);
			if (clip_done || (facing < -0.2f && dist > 15.0f)) {
				dragon_start(c, D_CIRCLE, NULL);
				c->passes++;
			}
		}
		break;
	}
	case D_HOVER: { // stopping in the air off to one side, breathing down at you
		const float ax = c->pos[0] - player[0], az = c->pos[2] - player[2], al = fmaxf(hypotf(ax, az), 1.0f);
		const float hx = player[0] + ax / al * 22.0f, hz = player[2] + az / al * 22.0f;
		const float hy = dragon_ground(hx, hz, player[1]) + 10.0f;
		const float near = hypotf(hx - c->pos[0], hz - c->pos[2]);
		fly_toward(c, hx, hy, hz, near < 10.0f ? 0.0f : fminf(DRAGON_SPEED_MS, near * 0.6f), dt);
		if (near < 12.0f) // (facing you while it hangs there)
			c->heading = wrap(c->heading + fmaxf(-1.2f * dt, fminf(1.2f * dt, wrap(atan2f(-dx, -dz) - c->heading))));
		if (c->attacks == ACT_NONE) {
			play_clip(c, fly_clip(c));
			if (near < 12.0f && c->speed < 6.0f)
				c->attacks = ACT_INHALE, play_clip(c, "hover_inhale");
			else if (c->dtime > 15.0f)
				dragon_start(c, D_CIRCLE, NULL);
		} else if (c->attacks == ACT_INHALE && clip_done) {
			c->attacks = ACT_BREATHE, play_clip(c, "hover_breathe");
		} else if (c->attacks == ACT_BREATHE) {
			dragon_breathe(c, player, dt);
			if (clip_done)
				dragon_start(c, D_CIRCLE, NULL), c->passes++;
		}
		break;
	}
	case D_LAND: { // down to its spot near you, wings flared at the last
		const float near = hypotf(c->land_at[0] - c->pos[0], c->land_at[2] - c->pos[2]);
		if (strcmp(c->clip, "land")) {
			fly_toward(c, c->land_at[0], c->land_at[1] + fminf(near * 0.4f, 14.0f), c->land_at[2],
			           fmaxf(6.0f, fminf(DRAGON_SPEED_MS, near * 0.5f)), dt);
			play_clip(c, fly_clip(c));
			if (near < 9.0f || c->dtime > 20.0f)
				play_clip(c, "land"), c->land_at[1] = dragon_ground(c->pos[0], c->pos[2], c->land_at[1]),
					c->pitch_now = c->bank = 0.0f, c->speed = 0.0f, c->climb = 0.0f, c->fall = c->pos[1];
		} else { // (settling over the clip)
			const float u = len > 0.0f ? fminf(1.0f, c->clip_t / len) : 1.0f;
			c->pos[1] = c->fall + (c->land_at[1] - c->fall) * u;
			if (clip_done) {
				c->pos[1] = c->land_at[1], c->fall = 0.0f; // (fall held its height: the body lies level)
				dragon_start(c, D_GROUND, "combat");
				sfx_play_at("dragon_land", c->pos, 200.0f);
			}
		}
		break;
	}
	case D_GROUND: { // facing you; bite, tail, wing, breath; then up again (unless too hurt)
		c->pos[1] = ground;
		const float want = atan2f(-dx, -dz), diff = wrap(want - c->heading);
		const float facing = (-sinf(c->heading) * dx - cosf(c->heading) * dz) / fmaxf(dist, 0.01f);
		if (c->attacks != ACT_NONE) {
			if ((c->attacks == ACT_BITE || c->attacks == ACT_TAIL || c->attacks == ACT_WING) && !c->hit_done &&
			    c->clip_t >= len * DRAGON_HIT_AT)
				c->hit_done = 1, dragon_strike(c, player, c->attacks);
			if (c->attacks == ACT_TURN) // (the turn carries it round)
				c->heading = wrap(c->heading + fmaxf(-0.9f * dt, fminf(0.9f * dt, diff)));
			if (c->attacks == ACT_WALK) {
				c->pos[0] += -sinf(c->heading) * k->walk_speed * dt, c->pos[2] += -cosf(c->heading) * k->walk_speed * dt;
				c->heading = wrap(c->heading + fmaxf(-0.4f * dt, fminf(0.4f * dt, diff)));
			}
			if (c->attacks == ACT_BREATHE)
				dragon_breathe(c, player, dt);
			if (c->attacks == ACT_INHALE && clip_done)
				c->attacks = ACT_BREATHE, play_clip(c, "breathe");
			else if (clip_done)
				c->attacks = ACT_NONE, c->hit_done = 0;
			break;
		}
		play_clip(c, "combat");
		const float near = k->reach + k->radius * 0.6f;
		const int again = c->last_act == ACT_TAIL || c->last_act == ACT_WING; // (after a swipe it turns to face you)
		if (dist < near * 1.1f && facing < -0.3f && !again) {
			c->attacks = c->last_act = ACT_TAIL, play_clip(c, "attack2"); // (behind it: its tail)
		} else if (dist < near && fabsf(facing) < 0.6f && !again) {
			c->attacks = c->last_act = ACT_WING, play_clip(c, "power"); // (beside it: a wing)
		} else if (fabsf(diff) > 0.45f) {
			c->last_act = ACT_TURN;
			c->attacks = ACT_TURN, play_clip(c, diff > 0.0f ? "turn_l" : "turn_r");
		} else if (dist < near) {
			c->attacks = c->last_act = ACT_BITE, play_clip(c, "attack");
		} else if (dist < 34.0f && c->shot_t <= 0.0f) {
			c->attacks = ACT_INHALE, play_clip(c, "inhale"), c->shot_t = DRAGON_BREATH_EVERY_S;
		} else if (dist > 40.0f) {
			c->attacks = ACT_WALK, play_clip(c, "walk");
		} else if (!c->grounded && c->rend_t <= 0.0f && c->dtime > DRAGON_GROUND_S) {
			c->land_at[1] = c->pos[1];
			dragon_start(c, D_TAKEOFF, "takeoff");
			sfx_play_at("dragon_takeoff", c->pos, 200.0f);
		}
		break;
	}
	case D_FALL: { // its glide failing: forward and down, till it strikes the ground
		c->climb -= 9.8f * 0.6f * dt;
		c->speed = fmaxf(c->speed, 12.0f);
		c->pos[0] += -sinf(c->heading) * c->speed * dt, c->pos[2] += -cosf(c->heading) * c->speed * dt;
		c->pos[1] += c->climb * dt;
		c->pitch_now = atan2f(c->climb, c->speed) * 0.7f;
		c->bank *= 1.0f - fminf(1.0f, dt);
		if (c->pos[1] <= ground + 0.3f) {
			c->pos[1] = ground;
			dragon_start(c, D_CRASH, "crash");
			sfx_play_at("dragon_crash", c->pos, 300.0f);
			c->grounded = 1;
		}
		break;
	}
	case D_CRASH: { // ploughing a furrow, slowing; dust and earth thrown up
		const float slow = fmaxf(0.0f, 1.0f - c->dtime / 2.2f);
		c->pos[0] += -sinf(c->heading) * c->speed * slow * dt, c->pos[2] += -cosf(c->heading) * c->speed * slow * dt;
		c->pos[1] = dragon_ground(c->pos[0], c->pos[2], c->pos[1]);
		c->pitch_now = c->bank = 0.0f;
		if (slow > 0.0f && (c->fx_t -= dt) <= 0.0f) {
			float mouth[3];
			dragon_mouth(c, mouth);
			mouth[1] = c->pos[1] + 0.5f;
			spellfx_burst(mouth, ELEMENT_FORCE, 2.5f);
			c->fx_t = 0.18f;
		}
		if (clip_done)
			c->state == DEAD ? dragon_start(c, D_SOUL, NULL) : dragon_start(c, D_CRASH_UP, "crash_up");
		break;
	}
	case D_CRASH_UP:
		c->pos[1] = ground;
		if (clip_done)
			dragon_start(c, D_GROUND, "combat");
		break;
	case D_DYING:
		c->pos[1] = ground;
		if (clip_done) // (down on the ground - Skyrim's ragdoll lays it there; its skin burns off, its bones stay so)
			dragon_start(c, D_SOUL, clip_len(c, "dead", NULL) > 0.0f ? "dead" : NULL);
		break;
	case D_SOUL: { // its skin burning off its bones; its soul streaming into you
		c->burn_t += dt;
		const float t = c->burn_t;
		if (t - dt <= 0.0f)
			sfx_play_at("dragon_soul_fire", c->pos, 300.0f);
		if (t < 6.0f && (c->fx_t -= dt) <= 0.0f) { // (embers all along it)
			const float along = (frand() - 0.5f) * k->radius * 2.2f, side = (frand() - 0.5f) * k->radius * 0.6f;
			const float at[3] = {c->pos[0] - sinf(c->heading) * along + cosf(c->heading) * side, c->pos[1] + frand() * 2.5f,
			                     c->pos[2] - cosf(c->heading) * along - sinf(c->heading) * side};
			spellfx_burst(at, ELEMENT_FIRE, 0.6f);
			c->fx_t = 0.1f;
		}
		if (t >= 1.0f && c->bones_beast < 0 && k->bones >= 0 && kind_ready(&kinds[k->bones])) { // (its bones, under)
			c->bones_beast = actors_beast_spawn(kinds[k->bones].actor_kind);
			if (c->bones_beast < 0 && t - dt < 1.0f)
				game_log("creatures: %s's bones couldn't be made (kind %d)", k->title, kinds[k->bones].actor_kind);
		}
		actors_beast_look(c->beast, 1, fminf(1.0f, fmaxf(0.0f, (t - 1.5f) / 5.0f)));
		if (c->bones_beast >= 0)
			actors_beast_look(c->bones_beast, 1, 0.0f);
		if (t >= 2.5f && t < 8.0f) {
			if (t - dt < 2.5f)
				sfx_play_at("dragon_soul_wind", c->pos, 300.0f);
			const float mid[3] = {c->pos[0], c->pos[1] + 1.0f, c->pos[2]}, chest[3] = {player[0], player[1] + 1.2f, player[2]};
			spellfx_soul(mid, k->radius * 0.8f, chest, dt * 0.4f); // (a few sparks besides)
			spellfx_soul_stream(mid, k->radius * 0.8f, chest, t - 2.5f);
		}
		if (t >= 8.0f) { // (absorbed)
			sfx_play("dragon_soul_burst", 1.0f, 0.0f);
			souls_taken++;
			if (c->bones_beast >= 0) { // (the skin gone: only its bones left)
				actors_beast_remove(c->beast);
				c->beast = c->bones_beast;
			}
			dragon_start(c, D_BONES, NULL);
		}
		break;
	}
	case D_BONES:
		break;
	}
	if (!c->breath_on && was_breathing) // (no longer breathing)
		c->fx_t = 0.0f;
	if (c->state != DEAD && !airborne(c) && c->dphase != D_FALL)
		c->pitch_now = c->bank = 0.0f;
	dragon_pose(c, dt);
}

// A dragon flying in from afar (one met at random: Skyrim's by your level)
// The dragon for the player's level: Skyrim's LCharDragon takes any of those at or below it
// (Dragon, Blood, Frost, Elder, Ancient) - a mix as you rise, not only the strongest; below the
// first, the first
static int dragon_for_level(void) {
	int pick[16], n = 0, lowest = -1;
	const int level = skills_state()->player_level;
	for (int k = 0; k < kind_count && n < 16; k++) {
		if (!(kinds[k].flags & F_DRAGON) || kinds[k].dragon_level <= 0 || kinds[k].health < 500.0f ||
		    !strcmp(kinds[k].name, "alduin")) // (the opening's own: never met at random)
			continue;
		if (lowest < 0 || kinds[k].dragon_level < kinds[lowest].dragon_level)
			lowest = k;
		if (kinds[k].dragon_level <= level)
			pick[n++] = k;
	}
	return n ? pick[(int)(frand() * n) % n] : lowest;
}

int creatures_take_dragon_fell(void) {
	const int f = dragon_fell;
	dragon_fell = 0;
	return f;
}

static int summon_dragon(const float player[3]) {
	const int best = dragon_for_level();
	if (best < 0)
		return 0;
	int s = -1;
	for (int i = file_spawns; i < spawn_count; i++) // (an old one's slot)
		if (spawns[i].transient && spawns[i].kind < 0)
			s = i;
	if (s < 0 && spawn_count < MAX_SPAWNS)
		s = spawn_count++;
	if (s < 0)
		return 0;
	Spawn *sp = &spawns[s];
	memset(sp, 0, sizeof(*sp));
	const float a = frand() * 6.2831853f;
	sp->kind = best, sp->transient = 1, sp->place = -1, sp->live = -1, sp->ambush = 3;
	sp->pos[0] = player[0] + cosf(a) * DRAGON_ARRIVE_M, sp->pos[2] = player[2] + sinf(a) * DRAGON_ARRIVE_M;
	sp->pos[1] = player[1] + 70.0f;
	wake(s);
	if (sp->live < 0) {
		sp->kind = -1;
		return 0;
	}
	Creature *c = &live[sp->live];
	memcpy(c->pos, sp->pos, sizeof(c->pos));
	c->heading = atan2f(-(player[0] - c->pos[0]), -(player[2] - c->pos[2]));
	c->speed = DRAGON_SPEED_MS;
	dragon_start(c, D_CIRCLE, "glide");
	c->roar_t = 3.0f;
	sfx_play("dragon_roar_far", 1.0f, 0.0f); // (heard coming, far off - it came in silent - and its fight's music)
	music_special("mus_combat_boss");
	return 1;
}

int creatures_summon_dragon(const float player[3]) {
	return summon_dragon(player);
}

void creatures_test_health(const float player[3], float radius) {
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (c->used && hypotf(c->pos[0] - player[0], c->pos[2] - player[2]) < radius)
			game_log("creatures: %s health %.0f/%.0f, %.1f m off, %.1f m up, state %d, slowed %.1f s", title_of(c), c->health,
			         c->max_health, hypotf(c->pos[0] - player[0], c->pos[2] - player[2]), c->pos[1] - player[1], c->state,
			         c->slow_left);
	}
}

int creatures_test_dragon_dies(const float player[3]) { // (testing: one brought down in front of you)
	if (!summon_dragon(player))
		return 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (!c->used || !(kinds[c->kind].flags & F_DRAGON) || c->state == DEAD)
			continue;
		c->pos[0] = player[0] + 12.0f, c->pos[2] = player[2];
		c->pos[1] = dragon_ground(c->pos[0], c->pos[2], player[1]);
		c->health = 0.01f;
		hurt(i, 1.0f, 0);
		return 1;
	}
	return 0;
}

float creatures_take_breath(int *element) {
	const float d = breath_taken;
	breath_taken = 0.0f;
	if (element)
		*element = breath_element_taken;
	return d;
}

int creatures_take_dragon_souls(void) {
	const int n = souls_taken;
	souls_taken = 0;
	return n;
}

static int dragon_about(void) {
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used && (kinds[live[i].kind].flags & F_DRAGON) && live[i].state != DEAD)
			return 1;
	return 0;
}

static float player_seen_mult = 1.0f; // (potions: Invisibility 0, Fortify Sneak less)
static int max_awake = MAX_LIVE; // (the performance setting's: creatures about at once)

static int awake_count(void) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++)
		n += live[i].used;
	return n;
}

static int spawns_held; // (the opening at Helgen: nobody else comes)
static int cur_place = -1; // (where the player is: creatures_update's)
static int trapped;        // (Soul Trap: souls taken, for main to put in a gem)
#define ALLY_FOLLOW_M 4.0f   // an ally keeps this near you...
#define ALLY_STOP_M 2.4f     // ... once following, stopping this near
#define ALLY_FETCH_M 32.0f   // ...and past this (or through a door) is brought to your side
#define ALLY_SEEK_M 20.0f    // it goes for the hostile within this of it
#define ALLY_BOOST 1.5f      // (rallied: its blows this much harder)
static int opening_side; // (the keep: 1 with Ralof, 2 with Hadvar; 0 the opening's over or not begun)

int creatures_opening_bodies(int place) {
	int n = 0;
	for (int s = 0; s < spawn_count; s++)
		n += spawns[s].place == place && spawns[s].ambush == 4 && spawns[s].kind >= 0;
	return n;
}

static int summon(const char *kind_name, const float at[3], float seconds, int only_one, int spirit);

int creatures_summon_ally(const char *kind_name, const float at[3], float seconds) {
	return summon(kind_name, at, seconds, 1, 0);
}

int creatures_summon_escort(const char *kind_name, const float at[3], float seconds) {
	return summon(kind_name, at, seconds, 0, 0);
}

int creatures_summon_spirit(const char *kind_name, const float at[3], float seconds) {
	return summon(kind_name, at, seconds, 1, 1);
}

const char *creatures_guard_kind(int hold) {
	for (int k = 0; k < kind_count; k++)
		if ((kinds[k].flags & F_GUARD) && kinds[k].hold == hold)
			return kinds[k].name;
	return NULL;
}

const char *creatures_guard_archer_kind(int hold) { // (one with a bow - the watchtower's against a dragon in the air)
	for (int k = 0; k < kind_count; k++)
		if ((kinds[k].flags & F_GUARD) && (kinds[k].flags & F_ARCHER) && kinds[k].hold == hold)
			return kinds[k].name;
	return creatures_guard_kind(hold);
}

static int summon_limit = 1, last_summoned = -1; // (allies at once: Twin Souls' two; the last one called)

void creatures_set_summon_limit(int n) {
	summon_limit = n < 1 ? 1 : n;
}

void creatures_strengthen_summons(float mult, float health) {
	if (last_summoned < 0 || !live[last_summoned].used || !live[last_summoned].ally)
		return;
	Creature *c = &live[last_summoned];
	c->max_health = c->max_health * mult + health, c->health = c->max_health;
	c->ally_boost = fmaxf(c->ally_boost, mult > 1.0f ? c->ally_left : 0.0f); // (it strikes harder too, while it's here)
	game_log("creatures: %s strengthened (x%.1f, +%.0f: %.0f)", title_of(c), mult, health, c->max_health);
}

static int summon(const char *kind_name, const float at[3], float seconds, int only_one, int spirit) {
	int kind = -1;
	for (int k = 0; k < kind_count && kind < 0; k++)
		if (!strcmp(kinds[k].name, kind_name))
			kind = k;
	for (int k = 0; k < kind_count && kind < 0; k++) // (not in the pack - its region hasn't one: a wolf, Skyrim's familiar)
		if (!strcmp(kinds[k].name, "wolf"))
			kind = k;
	if (kind < 0)
		return 0;
	for (;;) { // (one summons at a time - Twin Souls: two -: the oldest goes)
		int n = 0, oldest = -1;
		for (int i = 0; i < MAX_LIVE && only_one; i++)
			if (live[i].used && live[i].ally && spawns[live[i].spawn].transient) {
				n++;
				if (oldest < 0 || live[i].ally_left < live[oldest].ally_left)
					oldest = i;
			}
		if (n < summon_limit || oldest < 0)
			break;
		spawns[live[oldest].spawn].kind = -1;
		sleep_creature(oldest);
	}
	int s = -1;
	for (int i = file_spawns; i < spawn_count; i++)
		if (spawns[i].transient && spawns[i].kind < 0)
			s = i;
	if (s < 0 && spawn_count < MAX_SPAWNS)
		s = spawn_count++;
	if (s < 0)
		return 0;
	Spawn *sp = &spawns[s];
	memset(sp, 0, sizeof(*sp));
	sp->kind = kind, sp->transient = 1, sp->place = cur_place, sp->live = -1;
	sp->pos[0] = at[0], sp->pos[2] = at[2], sp->pos[1] = ground_at(at[0], at[2], at[1]);
	wake(s);
	if (sp->live < 0) {
		sp->kind = -1;
		return 0;
	}
	Creature *c = &live[sp->live];
	c->ally = 1, c->ally_left = seconds, c->hostile = 0, c->state = IDLE, c->aware = 0.0f;
	last_summoned = sp->live;
	if (spirit) // (a conjured familiar: a spirit, not a wild wolf)
		actors_beast_ghost(c->beast, 1);
	sfx_play_at("mag_conjure", c->pos, 30.0f);
	game_log("creatures: summoned %s (%s%s) at %.1f %.1f %.1f beast %d", kinds[kind].title, kind_name, spirit ? ", a spirit" : "", c->pos[0], c->pos[1], c->pos[2], c->beast);
	return 1;
}

int creatures_reanimate(const float eye[3], const float dir[3], float reach, float seconds) {
	int best = -1;
	float best_t = reach;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state != DEAD || (kinds[c->kind].flags & F_DRAGON))
			continue;
		const float v[3] = {c->pos[0] - eye[0], c->pos[1] + 0.3f - eye[1], c->pos[2] - eye[2]};
		const float t = v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2];
		if (t < 0.5f || t > best_t)
			continue;
		const float off[3] = {v[0] - dir[0] * t, v[1] - dir[1] * t, v[2] - dir[2] * t};
		if (sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]) < 1.8f)
			best = i, best_t = t;
	}
	if (best < 0)
		return 0;
	Creature *c = &live[best];
	c->state = IDLE, c->health = c->max_health, c->ally = 1, c->ally_left = seconds, c->hostile = 0, c->aware = 0.0f;
	c->fall = 0.0f, c->lie = 0.0f, c->clip_t = 0.0f, c->timer = 1.0f, c->soul_left = 0.0f, c->killmoved = 0;
	last_summoned = best;
	actors_beast_fall(c->beast, 0.0f, 0.0f);
	play(c, "idle");
	return 1;
}

int creatures_rally_allies(float seconds) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used && live[i].ally && live[i].state != DEAD)
			live[i].ally_boost = seconds, n++;
	return n;
}

int creatures_heal_allies(float health) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used && live[i].ally && live[i].state != DEAD && live[i].health < live[i].max_health)
			live[i].health = fminf(live[i].max_health, live[i].health + health), n++;
	return n;
}

int creatures_positions(const float from[3], float range, int undead, float out[][2], int max) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE && n < max; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD || !!(kinds[c->kind].flags & F_UNDEAD) != !!undead ||
		    hypotf(c->pos[0] - from[0], c->pos[2] - from[2]) > range)
			continue;
		out[n][0] = c->pos[0], out[n][1] = c->pos[2], n++;
	}
	return n;
}

int creatures_take_trapped(void) {
	const int n = trapped;
	trapped = 0;
	return n;
}

void creatures_set_opening_side(int side) {
	opening_side = side;
}

void creatures_hold_spawns(int on) {
	spawns_held = on;
}

void creatures_set_max_awake(int n) {
	max_awake = n < 4 ? 4 : n > MAX_LIVE ? MAX_LIVE : n;
}

static const char *title_of(const Creature *c);
// Who a creature fighting you is after: you, or one on your side nearer it - your summons, a
// thrall (Skyrim's combat picks its target among its enemies by how near and how threatening:
// they ignored the familiar and went round it for you). Kept a moment, not swapped every frame.
#define VICTIM_EVERY_S 1.0f
#define VICTIM_STRUCK_S 4.0f // (struck by your summons: after it this long at least)
static int victim_of(Creature *c, const float you[3], float dt) {
	if (c->ally || !c->hostile || !(c->state == CHASE || c->state == ATTACK || c->state == SHOOT || c->state == FLINCH || c->state == SHOUT)) {
		c->victim = -1;
		return -1;
	}
	const int v = c->victim;
	const int still = v < 0 || (v < MAX_LIVE && live[v].used && live[v].ally && live[v].state != DEAD) ||
	                  (v >= MAX_LIVE && friend_on[v - MAX_LIVE]);
	if ((c->victim_t -= dt) > 0.0f && still)
		return v;
	c->victim_t = VICTIM_EVERY_S;
	int best = -1;
	float best_d = hypotf(you[0] - c->pos[0], you[2] - c->pos[2]);
	for (int j = 0; j < MAX_LIVE; j++) {
		const Creature *a = &live[j];
		if (!a->used || !a->ally || a->state == DEAD || spawns[a->spawn].place != spawns[c->spawn].place ||
		    fabsf(a->pos[1] - c->pos[1]) > 4.0f)
			continue;
		const float d = hypotf(a->pos[0] - c->pos[0], a->pos[2] - c->pos[2]);
		if (d < best_d)
			best = j, best_d = d;
	}
	for (int f = 0; f < MAX_FRIENDS; f++) { // (your follower, a quest's allies: on their floor, nearer than you)
		if (!friend_on[f] || fabsf(friend_at[f][1] - c->pos[1]) > 4.0f)
			continue;
		const float d = hypotf(friend_at[f][0] - c->pos[0], friend_at[f][2] - c->pos[2]);
		if (d < best_d)
			best = MAX_LIVE + f, best_d = d;
	}
	if (best != v && best >= MAX_LIVE)
		game_log("creatures: %s goes for one with you (friend %d)", title_of(c), best - MAX_LIVE);
	else if (best != v && best >= 0)
		game_log("creatures: %s goes for your %s", title_of(c), title_of(&live[best]));
	return c->victim = best;
}

void creatures_update(float dt, const float player[3], int sneaking, int place) {
	sneaking_now = sneaking;
	hello_wait = fmaxf(0.0f, hello_wait - dt); // (the guards' greetings: one in a while)
	cur_place = place;
	arrest_ready = -1;
	combat_clock += dt;
	if (target_shown > 0.0f)
		target_shown -= dt;
	const float wake_m = place == -1 ? WAKE_OUT_M : WAKE_IN_M, sleep_m = place == -1 ? SLEEP_OUT_M : SLEEP_IN_M;
	// A dragon now and then, outdoors, once the watchtower's is slain (Skyrim's after its first)
	{
		const float now = lighting_game_hours();
		if (next_dragon_h < 0.0f || next_dragon_h > now + DRAGON_EVERY_H * 2.0f)
			next_dragon_h = now + DRAGON_EVERY_H * (0.5f + frand());
		if (now >= next_dragon_h) {
			next_dragon_h = now + DRAGON_EVERY_H * (0.6f + 0.8f * frand());
			// (none before the Western Watchtower's - Skyrim's dragons are loosed on the world once
			// Dragon Rising's is dead; one came by before Bleak Falls Barrow)
			if (place == -1 && quests_stage(Q_DRAGON_RISING) >= 40 && skills_state()->player_level >= DRAGON_MIN_LEVEL &&
			    !dragon_about() && !spawns_held)
				summon_dragon(player);
		}
	}
	// Spawn points coming into range, nearest first, while there's room (the performance
	// setting's cap: the rest wait at their spots)
	for (int k = awake_count(); k < max_awake && !spawns_held; k++) {
		int best = -1;
		float best_d = 1e9f;
		for (int s = 0; s < spawn_count; s++) {
			const Spawn *sp = &spawns[s];
			if (sp->kind < 0 || sp->place != place || sp->live >= 0 || sp->dead || (kinds[sp->kind].flags & F_REMAINS))
				continue;
			if ((sp->side == 1 || sp->side == 2) ? sp->side != opening_side : sp->side == 3 && opening_side)
				continue; // (the keep's: the side you're not on, while you escape; its bandits after)
			if (sp->side == SIDE_ESCAPE && quests_stage(Q_DIPLOMATIC) < THALMOR_ESCAPE_STAGE)
				continue; // (the escape scene's: not till you're in the dungeon)
			const int dragon = (kinds[sp->kind].flags & F_DRAGON) != 0;
			if (dragon && sp->ambush == 2 && quests_stage(Q_DRAGON_RISING) < 40)
				continue; // (a lair's: empty till the watchtower's dragon is slain, as Skyrim's are)
			if (!dragon && fabsf(sp->pos[1] - player[1]) >= 40.0f)
				continue;
			const float d = hypotf(sp->pos[0] - player[0], sp->pos[2] - player[2]);
			if (d < (dragon ? DRAGON_WAKE_M : wake_m) && d < best_d)
				best = s, best_d = d;
		}
		if (best < 0)
			break;
		wake(best);
		if (spawns[best].live < 0) // (no room after all)
			break;
	}
	// The melee slots: the nearest MELEE_SLOTS fighting hand to hand press in (0 straight on,
	// then either side), the rest wait their turn (-1)
	int melee_slot[MAX_LIVE];
	{
		float d[MAX_LIVE];
		for (int i = 0; i < MAX_LIVE; i++) {
			const Creature *c = &live[i];
			const int fighting = c->used && (c->state == CHASE || c->state == ATTACK || c->state == FLINCH || c->state == SHOUT);
			const int shooter = c->used && !c->hand_to_hand && ((kinds[c->kind].flags & F_ARCHER) ||
			                                ((kinds[c->kind].flags & F_PERSON) && kinds[c->kind].weapon == 0));
			d[i] = fighting && !shooter ? hypotf(c->pos[0] - player[0], c->pos[2] - player[2]) : -1.0f;
			melee_slot[i] = -1;
		}
		for (int s = 0; s < MAX_ENGAGED; s++) { // (the nearest MELEE_SLOTS strike; up to MAX_ENGAGED close in)
			int best = -1;
			for (int i = 0; i < MAX_LIVE; i++)
				if (d[i] >= 0.0f && melee_slot[i] < 0 && (best < 0 || d[i] < d[best]))
					best = i;
			if (best >= 0)
				melee_slot[best] = s;
		}
		for (int i = 0; i < MAX_LIVE; i++) // (the rest of a crowd keep their posts)
			if (d[i] >= 0.0f && melee_slot[i] < 0)
				melee_slot[i] = SLOT_AT_POST;
			else if (melee_slot[i] >= MELEE_SLOTS)
				melee_slot[i] = -1; // (closing in, holding off: their turn's next)
	}
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (!c->used)
			continue;
		const float was[2] = {c->pos[0], c->pos[2]};
		const Kind *k = &kinds[c->kind];
		const Spawn *sp = &spawns[c->spawn];
		const float *const you = player;
		float vic_at[3];
		cur_victim = c->used && c->state != DEAD ? victim_of(c, you, dt) : -1;
		if (cur_victim >= MAX_LIVE)
			memcpy(vic_at, friend_at[cur_victim - MAX_LIVE], sizeof(vic_at));
		else if (cur_victim >= 0)
			memcpy(vic_at, live[cur_victim].pos, sizeof(vic_at));
		const float *player = cur_victim >= 0 ? vic_at : you; // (who it's after: its fight's "you" below)
		const float dx = player[0] - c->pos[0], dz = player[2] - c->pos[2], dist = hypotf(dx, dz);
		if (c->ally && c->state != DEAD && (sp->place != place || dist > ALLY_FETCH_M)) { // (yours: with you, wherever you've gone)
			spawns[c->spawn].place = place;
			c->pos[0] = player[0] + 1.4f, c->pos[2] = player[2] + 1.4f;
			c->pos[1] = ground_at(c->pos[0], c->pos[2], player[1]);
			c->on_nav = 0;
			continue;
		}
		if (sp->place != place || dist > ((k->flags & F_DRAGON) ? DRAGON_SLEEP_M : sleep_m)) {
			sleep_creature(i);
			continue;
		}
		if (k->flags & F_DRAGON) { // (its own: flying, landing, breathing)
			if (c->dot_left > 0.0f && c->state != DEAD) { // (a storm's cold, a breath's: on dragons too)
				c->dot_left -= dt;
				hurt(i, c->dot_dps * dt, 0);
			}
			dragon_update(i, dt, player);
			continue;
		}
		c->clip_t += dt, c->timer -= dt, c->shot_t -= dt;
		if (c->shout_cd > 0.0f)
			c->shout_cd -= dt;
		if (sp->side == SIDE_THALMOR && !c->ally && !c->parley && c->state != DEAD && sp->ref != challenger_ref &&
		    quests_stage(Q_DIPLOMATIC) >= THALMOR_ALARM_STAGE && (c->disguise_t -= dt) <= 0.0f) {
			// You've left the party: the Thalmor's enemy now - unless disguised (Skyrim's cut
			// MQ201ThalmorEmbassyDisguiseScript, restored: every 4 s, not in a fight already, a
			// Thalmor hood and robe worn and you further off than your race lets you pass at)
			c->disguise_t = 4.0f;
			const int fighting = c->hostile && c->aware >= 1.0f && c->state != IDLE && c->state != WANDER;
			if (!fighting) {
				const float range = quests_thalmor_disguise_m();
				const int fooled = range >= 0.0f && dist > range;
				if (fooled != c->disguise_fooled)
					game_log("creatures: %s %s", title_of(c), fooled ? "fooled by your disguise" : range >= 0.0f ? "sees through your disguise" : "turns on you (you left the party)");
				c->disguise_fooled = fooled, c->hostile = !fooled;
				if (fooled)
					c->aware = 0.0f;
			}
		}
		if (c->parley && c->parley_go > 0.0f && c->state != DEAD) { // (fooled: off to the party, then gone)
			if ((c->parley_go -= dt) <= 0.0f) {
				spawns[c->spawn].dead = 1, spawns[c->spawn].dead_at = lighting_game_hours();
				game_log("creatures: %s gone (fooled)", title_of(c));
				sleep_creature(i);
				continue;
			}
		} else if (c->parley && !c->hostile && c->state != DEAD) { // (talking to you: stood, facing you)
			c->state = IDLE, c->timer = 1.0f;
			head_for(c, player[0], player[2], 0.0f, dt);
		}
		if (c->slow_left > 0.0f)
			c->slow_left -= dt;
		if (c->soul_left > 0.0f)
			c->soul_left -= dt;
		if (c->doom_left > 0.0f && c->state != DEAD && (c->doom_left -= dt) <= 0.0f) {
			c->health = 0.01f; // (he ran on into the barrow: its dead had him - the claw's on his body, ahead)
			hurt(i, 1.0f, 0);
		}
		if (c->marked_left > 0.0f)
			c->marked_left -= dt;
		if (c->bleed_left > 0.0f && c->state != DEAD) { // (a war axe's wound)
			const float step = fminf(dt, c->bleed_left);
			c->bleed_left -= dt;
			c->health -= c->bleed_dps * step;
			if (c->floor_share > 0.0f)
				c->health = fmaxf(c->health, c->max_health * c->floor_share);
			if (c->health <= 0.0f)
				c->health = 0.01f, hurt(i, 1.0f, 0); // (bled out: its death, as a blow's)
		}
		if (c->calm_left > 0.0f && c->state != DEAD) { // (Calm: it won't fight till it wears off)
			c->calm_left -= dt;
			c->hostile = 0, c->aware = 0.0f;
			if (c->state == CHASE || c->state == ATTACK || c->state == SHOOT || c->state == ALERT || c->state == SHOUT)
				c->state = IDLE, c->timer = 1.0f;
			if (c->calm_left <= 0.0f)
				c->hostile = kinds[c->kind].aggression >= 2;
		}
		if (c->dot_left > 0.0f && c->state != DEAD) { // (Frost Breath: its cold, by the second)
			c->dot_left -= dt;
			hurt(i, c->dot_dps * dt, 0);
		}
		if (c->knock[0] != 0.0f || c->knock[1] != 0.0f) { // (flung: sliding back along the floor)
			if (!step_to(c, c->pos[0] + c->knock[0] * dt, c->pos[2] + c->knock[1] * dt))
				c->knock[0] = c->knock[1] = 0.0f; // (into a wall: stopped there)
			const float slow = fmaxf(0.0f, 1.0f - KNOCK_FRICTION * dt);
			c->knock[0] *= slow, c->knock[1] *= slow;
			if (hypotf(c->knock[0], c->knock[1]) < 0.2f)
				c->knock[0] = c->knock[1] = 0.0f;
		}
		const float from_home = hypotf(c->pos[0] - sp->pos[0], c->pos[2] - sp->pos[2]);
		const float see = sneaking ? SEE_M * 0.5f : SEE_M;
		const int near_level = fabsf(player[1] - c->pos[1]) < 6.0f;
		// (the keep's soldiers in the opening: at you once you're near, whatever they were at - two
		// stood by while the rest fought)
		if (c->hand_to_hand && near_level && (c->state == IDLE || c->state == WANDER || c->state == ALERT) &&
		    hypotf(c->pos[0] - player[0], c->pos[2] - player[2]) < 18.0f)
			c->hostile = 1, c->aware = 1.0f, c->state = CHASE;
		if (c->held && c->state != DEAD) { // Caught in the web: in Skyrim's web clips, begging to be cut down (CIRCLE)
			c->hostile = 0, c->state = IDLE, c->aware = 0.0f;
			memcpy(c->pos, sp->pos, sizeof(c->pos)), c->heading = sp->heading;
			if (c->clip_t > 4.0f && frand() < dt * 0.25f)
				play(c, "web_thrash");
			else if (strcmp(c->clip, "web_thrash") || c->clip_t > clip_len(c, "web_thrash", NULL))
				play(c, "web_idle");
			// (his own words: calling for help as you come near - Skyrim's MS13BleakFallsBarrowArvelShouting -, then as
			// you come in - the spider alive or dead -, his pleas; as you leave)
			if (dist < ARVEL_CALLS_M && dist >= 14.0f && !c->held_seen && !c->called && !quests_busy())
				quests_voice("arvel_calling"), c->called = 1;
			if (dist < 14.0f && !c->held_seen)
				quests_claw_learned(); // (the claw's quest, if Lucan never told you of it)
			if (dist < 14.0f && !c->held_seen) {
				int spider = 0;
				for (int j = 0; j < MAX_LIVE; j++)
					spider |= live[j].used && live[j].state != DEAD && j != i && strstr(kinds[live[j].kind].title, "Spider") &&
					          hypotf(live[j].pos[0] - c->pos[0], live[j].pos[2] - c->pos[2]) < 30.0f;
				quests_voice(spider ? "arvel_spider" : "arvel_killed");
				c->held_seen = 1, c->held_said = 14.0f;
			} else if (c->held_seen && dist < 14.0f && (c->held_said -= dt) <= 0.0f && !quests_busy()) {
				static int line;
				quests_voice(line++ % 2 ? "arvel_plea2" : "arvel_plea1");
				c->held_said = 14.0f;
			} else if (c->held_seen == 1 && dist > 18.0f && !quests_busy()) {
				quests_voice("arvel_plea3"); // (INFO 000993E8)
				c->held_seen = 2;
			} else if (c->held_seen == 2 && dist < 12.0f) {
				c->held_seen = 1;
			}
			goto posed;
		}
		if (c->cut_t > 0.0f && c->state != DEAD) { // (cut down: out of the web, then away with the claw)
			c->cut_t -= dt;
			play(c, "web_exit");
			if (c->cut_t <= 0.0f) {
				c->state = FLEE, c->flee_t = 20.0f, c->timer = 0.0f, c->hostile = 1, c->aware = 1.0f;
				quests_voice("arvel_flee"); // (a line of theirs)
			}
			goto posed;
		}
		if (c->ally)
			c->hostile = 0; // (on your side, whatever's struck it)
		if (c->ally && c->state != DEAD && c->state != KNOCKED) { // Yours: at the hostile nearest it, else at your side
			if ((c->ally_left -= dt) <= 0.0f) { // (its time's up: a summons gone, the raised dead down again)
				if (c->tame) { // (Animal Allegiance over: a wild thing again)
					c->ally = 0, c->tame = 0, c->hostile = kinds[c->kind].aggression >= 2, c->aware = 0.0f;
					goto posed;
				}
				if (sp->transient) {
					spawns[c->spawn].kind = -1;
					sleep_creature(i);
					continue;
				}
				c->ally = 0, c->health = 0.01f;
				hurt(i, 1.0f, 0);
				goto posed;
			}
			if (c->ally_boost > 0.0f)
				c->ally_boost -= dt;
			int foe = -1, dragon_foe;
			float foe_d = ALLY_SEEK_M;
			for (int j = 0; j < MAX_LIVE; j++) {
				const Creature *o = &live[j];
				if (!o->used || j == i || o->ally || o->state == DEAD || !o->hostile || (kinds[o->kind].flags & F_DRAGON) ||
				    fabsf(o->pos[1] - c->pos[1]) > 5.0f)
					continue;
				const float d = hypotf(o->pos[0] - c->pos[0], o->pos[2] - c->pos[2]);
				if (d < foe_d)
					foe = j, foe_d = d;
			}
			float run = 0.0f;
			clip_len(c, clip_for(c, "run"), &run);
			if (run <= 0.1f)
				run = 3.5f;
			if (c->state == ATTACK) {
				const float len = clip_len(c, c->clip, NULL);
				if (foe >= 0)
					head_for(c, live[foe].pos[0], live[foe].pos[2], 0.0f, dt);
				if (!c->hit_done && c->clip_t >= len * HIT_AT) {
					c->hit_done = 1;
					if (foe >= 0 && foe_d < k->reach + k->radius + kinds[live[foe].kind].radius + 0.8f) {
						blame = 0, hurt(foe, k->damage * (c->ally_boost > 0.0f ? ALLY_BOOST : 1.0f), 1), blame = 1;
						if (live[foe].hostile && !live[foe].ally && live[foe].victim != i) { // (bitten by your summons: it turns on it)
							live[foe].victim = i, live[foe].victim_t = VICTIM_STRUCK_S;
							game_log("creatures: %s turns on your %s", title_of(&live[foe]), title_of(c));
						}
					}
				}
				if (c->clip_t >= len || len <= 0.0f)
					c->state = CHASE, c->timer = 0.4f + frand() * 0.5f;
			} else if (foe >= 0) {
				const float reach = k->reach + k->radius + kinds[live[foe].kind].radius + 0.3f;
				if (foe_d > reach) {
					go_to(c, live[foe].pos[0], live[foe].pos[2], run, dt);
					play(c, "run");
					c->state = CHASE;
				} else if (c->timer <= 0.0f) {
					head_for(c, live[foe].pos[0], live[foe].pos[2], 0.0f, dt);
					play(c, "attack");
					c->state = ATTACK, c->hit_done = 0;
					voice(c, "attack");
				} else {
					head_for(c, live[foe].pos[0], live[foe].pos[2], 0.0f, dt);
					play(c, "combat");
				}
			} else if ((k->flags & F_PERSON) && (dragon_foe = dragon_near(c->pos, ALLY_DRAGON_M)) >= 0) {
				// (a dragon: Irileth's men at the watchtower loose arrows at it, as Skyrim's soldiers do)
				const Creature *dr = &live[dragon_foe];
				head_for(c, dr->pos[0], dr->pos[2], 0.0f, dt);
				const int bow = (k->flags & F_ARCHER) != 0;
				// (with a bow: drawn and held on it, loosed, drawn again - Skyrim's bow clips; they stood in
				// their sword's stance with arrows leaving them. Without one: ready, facing it)
				play(c, !bow ? "combat" : c->shot_t > DRAW_S ? "bow_release" : "bow_draw");
				if (bow && c->shot_t <= 0.0f) {
					const float from[3] = {c->pos[0], c->pos[1] + 1.5f, c->pos[2]};
					const float to[3] = {dr->pos[0], dr->pos[1] + kinds[dr->kind].height * 0.4f, dr->pos[2]};
					float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
					const float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
					for (int a = 0; a < 3; a++)
						d[a] = d[a] / fmaxf(dl, 0.1f) + (frand() - 0.5f) * 0.04f;
					d[1] += dl * 0.004f; // (a little high: the arrow drops on the way)
					const float out[3] = {from[0] + d[0] * (k->radius + 0.6f), from[1] + d[1] * (k->radius + 0.6f),
					                      from[2] + d[2] * (k->radius + 0.6f)}; // (from clear of its own body)
					float speed, gravity;
					const int model = items_flight(items_by_form(IRON_ARROW_FORM), &speed, &gravity);
					projectiles_fire(out, d, ARROW_SPEED_MS, 0.35f, model, k->damage + ARROW_DAMAGE, 0, 0, -1);
					sfx_play_at("bow_fire", c->pos, 30.0f);
					c->shot_t = ALLY_VOLLEY_S * (0.7f + 0.6f * frand()) + DRAW_S;
				}
				c->state = CHASE;
			} else if (dist > ALLY_FOLLOW_M || (c->state == CHASE && dist > ALLY_STOP_M)) {
				// (to your side at its own walk's pace, or its run's when far: the walk was played at half
				// a run's speed, sliding; and it set off and stopped again on the same mark, stuttering)
				const int far = dist > 9.0f;
				float speed = 0.0f;
				play(c, far ? "run" : "walk");
				clip_len(c, c->clip, &speed);
				go_to(c, player[0], player[2], speed > 0.3f ? speed : far ? run : k->walk_speed, dt);
				c->state = CHASE;
			} else {
				play(c, "idle");
				c->state = IDLE;
				if (c->ally_left > 0.0f && strstr(k->set, "wolf") && frand() < dt / 6.0f) // (a wolf's breath at your side)
					voice(c, "breathe");
			}
			goto posed;
		}
		if ((k->flags & F_GUARD) && !c->hostile && !c->ally && c->state != DEAD && c->state != ARREST) {
			// A guard at peace: now and then a word as you pass close (Skyrim's Hellos)
			if (dist < HELLO_PASS_M && near_level && c->talk_t <= 0.0f && hello_wait <= 0.0f && !voice_playing() &&
			    frand() < dt * 0.6f && creatures_guard_talk(i))
				hello_wait = HELLO_EVERY_S;
			if (c->talk_t > 0.0f) { // (stopped to say it, facing you)
				c->talk_t -= dt;
				c->heading = atan2f(-dx, -dz);
				play(c, "idle");
				goto posed;
			}
		}
		switch (c->state) {
		case AMBUSH: { // lying in wait (in a sarcophagus, an alcove, on a throne): up when you come near
			// (a sarcophagus: lying, out with Skyrim's clip; an alcove: stood in it, straight out at
			// you - its lying-down clip had every alcove's draugr resting, and stuck a while getting up)
			// (lying - a burial shelf's, a coffin's: flat, Skyrim's laying clip, up and out the side)
			static const char *const WAIT[4][2] = {{"sarc_idle", "sarc_exit"}, {"alcove_idle", "alcove_exit"},
			                                       {"sit_idle", "sit_exit"}, {"lay_idle", "lay_exit"}};
			const int how = sp->ambush >= 6 && sp->ambush <= 9 ? sp->ambush - 6 : -1;
			const int lying = how >= 0 && clip_len(c, WAIT[how][0], NULL) > 0.0f;
			if (lying)
				memcpy(c->pos, sp->pos, sizeof(c->pos)), c->heading = sp->heading;
			play(c, lying ? WAIT[how][0] : "idle");
			const float wake = k->shouts ? BOSS_WAKE_M : AMBUSH_WAKE_M;
			if (dist < (sneaking ? wake * 0.5f : wake) && near_level)
				draugr_wake(i);
			break;
		}
		case RISE: { // (getting out of its coffin: the clip's own way up)
			c->rise_t -= dt;
			// (and out onto the floor as it comes - Skyrim's clip carries it out of the shelf; left
			// in the wall's niche, it snagged there working its way out)
			const float f = 1.0f - fmaxf(0.0f, c->rise_t) / fmaxf(c->rise_len, 0.01f), e = f * f * (3.0f - 2.0f * f);
			for (int a = 0; a < 3; a++)
				c->pos[a] = c->rise_from[a] + (c->rise_to[a] - c->rise_from[a]) * e;
			if (c->rise_t <= 0.0f) {
				memcpy(c->pos, c->rise_to, sizeof(c->pos));
				NavRef nr;
				float out[3];
				if (navmesh_nearest_level(c->pos[0], c->pos[1], c->pos[2], 2.5f, 2.0f, &nr, out)) {
					memcpy(c->pos, out, sizeof(c->pos));
					c->nav = nr, c->on_nav = 1;
				} else {
					settle(c);
				}
				c->shout_cd = 2.0f + frand() * 2.0f;
				c->state = CHASE;
			}
			break;
		}
		case SHOUT: { // (the shout: in, then out at you)
			c->shout_t -= dt;
			c->heading = atan2f(-dx, -dz);
			if (c->shout_t > SHOUT_S - SHOUT_IN_S) {
				play(c, clip_len(c, "shout_in", NULL) > 0.0f ? "shout_in" : "idle");
			} else {
				play(c, clip_len(c, "shout", NULL) > 0.0f ? "shout" : "idle");
				if (!c->shout_out) {
					c->shout_out = 1;
					shout_at_player(c, player, dist);
				}
			}
			if (c->shout_t <= 0.0f) {
				c->shout_cd = SHOUT_EVERY_S * (0.8f + 0.6f * frand());
				c->state = CHASE;
			}
			break;
		}
		case IDLE:
		case WANDER:
		case ALERT: {
			if (k->flags & F_GUARD) { // (a guard: after you if its hold has a bounty on you - crime.c)
				const int mood = guard_mood(k);
				if (c->arrest_wait > 0.0f)
					c->arrest_wait -= dt;
				if (mood == 2)
					c->hostile = 1;
				else if (mood == 1 && !c->hostile && c->arrest_wait <= 0.0f && near_level && dist < ARREST_SEE_M &&
				         sees(c, player)) {
					c->state = ARREST;
					break;
				}
			}
			// Noticing you: Skyrim's detection grows while it can see you (in front of it, in
			// the open, nearer quicker, sneaking far slower) or hear you (running, fighting)
			const float facing = (-sinf(c->heading) * dx - cosf(c->heading) * dz) / fmaxf(dist, 0.01f);
			float gain = 0.0f;
			if (c->hostile && near_level && dist < see && (facing > SIGHT_COS || dist < 2.0f) && player_seen_mult > 0.0f &&
			    sees(c, player))
				gain = (1.0f - dist / see) * (sneaking ? 0.35f : 1.0f) * player_seen_mult / NOTICE_S * 2.0f;
			if (c->hostile && near_level && dist < HEAR_M * player_noise)
				gain = fmaxf(gain, (1.0f - dist / (HEAR_M * player_noise)) / NOTICE_S * 2.0f);
			if (c->hand_to_hand && c->hostile && near_level && dist < 16.0f)
				gain = fmaxf(gain, 4.0f / NOTICE_S); // (the keep's soldiers are looking for you: sneaking or not, seen)
			if (gain > 0.0f) {
				c->aware += gain * dt;
				c->seen_at[0] = player[0], c->seen_at[1] = player[2];
			} else {
				c->aware -= dt / FORGET_S;
			}
			c->aware = fmaxf(0.0f, fminf(1.0f, c->aware));
			if (c->aware >= 1.0f) {
				c->state = CHASE, c->timer = 0.0f;
				voice(c, "growl");
				call_allies(i);
				break;
			}
			if (c->aware >= ALERT_AT && c->state != ALERT && c->hostile) {
				c->state = ALERT, c->timer = SEARCH_S; // (it heard something: comes to look)
				break;
			}
			if (c->state == ALERT) { // to where it saw you, looking about; then back to its spot
				play(c, "walk");
				if (go_to(c, c->seen_at[0], c->seen_at[1], k->walk_speed, dt) < 1.0f)
					play(c, "combat");
				if (c->timer <= 0.0f || c->aware <= 0.05f)
					c->state = WANDER, c->target[0] = sp->pos[0], c->target[1] = sp->pos[2], c->timer = 0.0f;
				break;
			}
			if (k->aggression == 0 && !(k->flags & F_GUARD) && dist < (sneaking ? FLEE_M * 0.4f : FLEE_M) && near_level) {
				c->state = FLEE, c->flee_t = 4.0f + frand() * 3.0f;
				break;
			}
			if (c->state == IDLE && (k->flags & F_GUARD) && sp->route_n >= 2) { // (a guard on patrol: marker to marker)
				play(c, "idle");
				if (c->timer <= 0.0f) {
					const int n = sp->route_n;
					c->route_i += c->route_dir ? -1 : 1;
					if (c->route_i >= n) // (to the end and back: Skyrim's patrols that don't close)
						c->route_i = n - 2, c->route_dir = 1;
					else if (c->route_i < 0)
						c->route_i = 1, c->route_dir = 0;
					c->target[0] = route_pts[sp->route_first + c->route_i][0];
					c->target[1] = route_pts[sp->route_first + c->route_i][1];
					c->state = WANDER, c->timer = 0.0f;
				}
			} else if (c->state == IDLE && (k->flags & F_GUARD)) { // (a guard: at its post, facing its way - no strolls into walls)
				play(c, "idle");
				if (from_home > 1.5f)
					c->state = WANDER, c->target[0] = sp->pos[0], c->target[1] = sp->pos[2], c->timer = 0.0f;
				else
					head_for(c, c->pos[0] - sinf(sp->heading), c->pos[2] - cosf(sp->heading), 0.0f, dt);
			} else if (c->state == IDLE) {
				play(c, "idle");
				if (c->timer <= 0.0f) { // a stroll somewhere about its spot
					const float a = frand() * 6.2832f, r = WANDER_M * sqrtf(frand());
					c->target[0] = sp->pos[0] + cosf(a) * r, c->target[1] = sp->pos[2] + sinf(a) * r;
					c->state = WANDER, c->timer = 0.0f;
				}
			} else {
				play(c, "walk");
				if (go_to(c, c->target[0], c->target[1], k->walk_speed, dt) < 0.6f ||
				    c->timer < (sp->route_n >= 2 ? -60.0f : -12.0f)) // (a patrol's leg can be long)
					c->state = IDLE, c->timer = sp->route_n >= 2 ? 2.0f + frand() * 5.0f : 3.0f + frand() * 6.0f;
			}
			break;
		}
		case ARREST: { // a guard stopping you (crime.c): to you, then waiting for your answer
			const int mood = guard_mood(k);
			if (mood != 1 || c->hostile) {
				c->state = mood == 2 || c->hostile ? CHASE : IDLE, c->hostile = mood == 2 || c->hostile, c->timer = 1.0f;
				break;
			}
			if (dist > ARREST_SEE_M * 2.0f || !near_level) { // (you got away: back to its post)
				c->state = WANDER, c->target[0] = sp->pos[0], c->target[1] = sp->pos[2], c->timer = 0.0f;
				break;
			}
			if (dist > ARREST_TALK_M) {
				const int far = dist > 8.0f;
				go_to(c, player[0], player[2], far ? k->run_speed * 0.8f : k->walk_speed, dt);
				play(c, far ? "run" : "walk");
			} else {
				head_for(c, player[0], player[2], 0.0f, dt);
				play(c, "idle");
				if (arrest_ready < 0)
					arrest_ready = i;
			}
			break;
		}
		case FLEE: { // away from you, at a run
			play(c, "run");
			// Calculate escape vectors strictly within local navmesh boundaries (A* pathfinding)
			// rather than blindly inverting the player vector into solid dungeon walls
			const float cur_dist = hypotf(c->pos[0] - c->target[0], c->pos[2] - c->target[1]);
			if ((c->target[0] == 0.0f && c->target[1] == 0.0f) || c->timer <= 0.0f || cur_dist < 1.2f) {
				c->timer = 1.5f + frand() * 0.5f;
				const float away = atan2f(-dx, -dz);
				static const float ANGLES[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -1.5f, 2.0f, -2.0f};
				static const float DISTS[] = {12.0f, 8.0f, 5.0f};
				int found = 0;
				float best_x = c->pos[0], best_z = c->pos[2], best_pdist = dist;
				for (size_t di = 0; di < sizeof(DISTS) / sizeof(DISTS[0]) && !found; di++) {
					const float d_try = DISTS[di];
					for (size_t ai = 0; ai < sizeof(ANGLES) / sizeof(ANGLES[0]); ai++) {
						const float a = away + ANGLES[ai];
						const float px = c->pos[0] + sinf(a) * d_try, pz = c->pos[2] + cosf(a) * d_try;
						NavRef nr;
						float h, cand[3] = {px, c->pos[1], pz};
						if (navmesh_locate_level(px, c->pos[1] + 0.5f, pz, 2.0f, &nr, &h)) {
							const float pdist = hypotf(px - player[0], pz - player[2]);
							if (pdist > best_pdist) {
								best_pdist = pdist, best_x = px, best_z = pz;
								found = 1;
								break;
							}
						} else if (navmesh_nearest_level(px, c->pos[1], pz, 2.0f, 1.5f, &nr, cand)) {
							const float pdist = hypotf(cand[0] - player[0], cand[2] - player[2]);
							if (pdist > best_pdist) {
								best_pdist = pdist, best_x = cand[0], best_z = cand[2];
								found = 1;
								break;
							}
						}
					}
				}
				c->target[0] = best_x, c->target[1] = best_z;
			}
			if (c->target[0] != 0.0f || c->target[1] != 0.0f)
				go_to(c, c->target[0], c->target[1], k->run_speed, dt);
			else
				head_for(c, player[0], player[2], 0.0f, dt);
			if ((c->flee_t -= dt) <= 0.0f && dist > FLEE_M)
				c->state = IDLE, c->timer = 2.0f, c->target[0] = c->target[1] = 0.0f;
			break;
		}
		case CHASE: {
			// (a mage casts from afar - unarmed and without a spell, a brawler comes at you; a pack from
			// before mages' spells were read: every unarmed one cast)
			const int shooter = !c->hand_to_hand && ((k->flags & F_ARCHER) ||
			                                         ((k->flags & F_PERSON) && k->weapon == 0 && (k->mage_element >= 0 || !mages_read)));
			if (k->mage_summon[0] && !c->summoned && c->hostile && c->aware >= 1.0f && dist < MAGE_SUMMON_M) {
				c->summoned = 1; // (its conjuration: an atronach beside it, at you)
				const float at[3] = {c->pos[0] + cosf(c->heading) * 1.8f, c->pos[1], c->pos[2] - sinf(c->heading) * 1.8f};
				const int called = creatures_place_dragon(k->mage_summon, at, c->heading, 0);
				if (called >= 0)
					live[called].conjured = 1;
				if (called >= 0)
					spellfx_burst(at, ELEMENT_FORCE, 1.5f), sfx_play_at("mag_rune_impact", at, 30.0f);
				game_log("creatures: %s conjures %s -> %d", title_of(c), k->mage_summon, called);
			}
			if (k->mage_heal > 0.0f && c->health < c->max_health * MAGE_HEAL_BELOW && (c->heal_t -= dt) <= 0.0f) {
				c->health = fminf(c->max_health, c->health + k->mage_heal); // (its healing on itself)
				c->heal_t = MAGE_HEAL_EVERY_S;
				spellfx_burst((const float[3]){c->pos[0], c->pos[1] + 1.0f, c->pos[2]}, ELEMENT_FORCE, 0.8f);
				game_log("creatures: %s heals itself (%.0f)", title_of(c), c->health);
			}
			if (from_home > GIVE_UP_M && dist > 10.0f) { // (back to its spot)
				c->state = WANDER, c->target[0] = sp->pos[0], c->target[1] = sp->pos[2], c->timer = 0.0f;
				c->hostile = k->aggression >= 2;
				break;
			}
			if (k->shouts && c->hostile && c->shout_cd <= 0.0f && dist > SHOUT_FROM_M && dist < SHOUT_TO_M &&
			    near_level && sees(c, player)) {
				int words[4], nw = 0; // (one of those it knows)
				for (int b = 0; b < 4; b++)
					if (k->shouts & (1 << b))
						words[nw++] = 1 << b;
				c->shout_word = nw ? words[(int)(frand() * nw) % nw] : SH_FORCE;
				c->shout_t = SHOUT_S, c->shout_out = 0;
				c->state = SHOUT;
				game_log("creatures: %s shouts (%d) at %.1f m", k->title, c->shout_word, dist);
				static const char *const IN[4] = {"draugr_force_a", "draugr_frost_a", "draugr_frost_a", "draugr_disarm_a"};
				sfx_play_at(IN[c->shout_word == SH_FORCE ? 0 : c->shout_word == SH_FROST ? 1 : c->shout_word == SH_FIRE ? 2 : 3],
				            c->pos, 40.0f);
				break;
			}
			if (k->spit_dps > 0.0f && dist > SPIT_NEAR_M && dist < SPIT_FAR_M && c->shot_t <= 0.0f && sees(c, player)) {
				c->state = SHOOT, c->timer = SPIT_WINDUP_S; // (a frostbite spider rears and spits)
				play(c, "attack2");
				break;
			}
			if (shooter && HOLDS_BOW(k) && dist < ARCHER_BASH_M && c->timer <= 0.0f) { // (at its elbow: a bow bash, as in Skyrim)
				head_for(c, player[0], player[2], 0.0f, dt);
				play(c, "attack");
				c->state = ATTACK, c->hit_done = 0;
				voice(c, "attack");
				target = i, target_shown = 6.0f;
				break;
			}
			if (shooter && dist < ARCHER_FAR_M && dist >= (HOLDS_BOW(k) ? ARCHER_BASH_M : 1.2f) && c->shot_t <= 0.0f && sees(c, player)) {
				c->state = SHOOT, c->timer = DRAW_S;
				play(c, HOLDS_BOW(k) ? "bow_draw" : "idle");
				break;
			}
			// To a spot beside you of its own (they come from all sides, not in a line); only the
			// nearest few press in, the rest hold off circling until one of them falls
			const int slot = melee_slot[i];
			const int other_level = fabsf(player[1] - c->pos[1]) > fmaxf(k->height, 1.6f); // (you above it, or below: no blow reaches)
			const float spread = slot <= 0 ? 0.0f : (slot % 2 ? 1.0f : -1.0f) * (0.8f + 0.4f * (float)((slot - 1) / 2));
			const float from = atan2f(-dx, -dz) + spread, reach = (k->reach + k->radius) * 0.8f;
			const float gx = player[0] + sinf(from) * reach, gz = player[2] + cosf(from) * reach;
			if (shooter) { // archers and mages: from their post, backing off if you close in
				float px = c->pos[0], pz = c->pos[2];
				if (dist < ARCHER_NEAR_M && HOLDS_BOW(k)) { // (backing off, unless a wall is behind it)
					const float back = 1.5f / fmaxf(dist, 0.1f);
					const float from[3] = {c->pos[0], c->pos[1] + 0.8f, c->pos[2]};
					const float to[3] = {c->pos[0] - dx * back, c->pos[1] + 0.8f, c->pos[2] - dz * back};
					if (collision_ray(from, to) >= 1.0f)
						px -= dx, pz -= dz;
				}
				else if (!sees(c, player) || dist > ARCHER_FAR_M)
					px += dx, pz += dz;
				const float ox = px - sp->pos[0], oz = pz - sp->pos[2], o = hypotf(ox, oz);
				if (o > ARCHER_POST_M) // (not off the wall)
					px = sp->pos[0] + ox / o * ARCHER_POST_M, pz = sp->pos[2] + oz / o * ARCHER_POST_M;
				if (hypotf(px - c->pos[0], pz - c->pos[2]) > 0.5f) {
					play(c, "walk");
					go_to(c, px, pz, k->walk_speed, dt);
				} else {
					play(c, "combat");
				}
				head_for(c, player[0], player[2], 0.0f, dt);
			} else if (slot == SLOT_AT_POST) { // more than the fight needs: at its post (a wall, a gate), facing you
				if (hypotf(sp->pos[0] - c->pos[0], sp->pos[2] - c->pos[2]) > 1.0f) {
					play(c, "walk");
					go_to(c, sp->pos[0], sp->pos[2], k->walk_speed, dt);
				} else {
					play(c, "combat");
					head_for(c, player[0], player[2], 0.0f, dt);
				}
			} else if (slot < 0) { // waiting its turn: circling at a distance, facing you
				const float wait = atan2f(c->pos[0] - player[0], c->pos[2] - player[2]) + c->strafe * dt * 0.5f;
				const float wx = player[0] + sinf(wait) * HOLD_OFF_M, wz = player[2] + cosf(wait) * HOLD_OFF_M;
				play(c, fabsf(dist - HOLD_OFF_M) > 1.5f ? "run" : "walk");
				go_to(c, wx, wz, fabsf(dist - HOLD_OFF_M) > 1.5f ? k->run_speed : k->walk_speed, dt);
				if (dist < HOLD_OFF_M + 1.0f)
					head_for(c, player[0], player[2], 0.0f, dt);
			} else if (dist > k->reach + k->radius + 0.3f || other_level) {
				// (to you - on your floor: up the stairs to the bridge, not under it swinging)
				play(c, dist > 6.0f || other_level ? "run" : "trot");
				float speed = 0.0f;
				clip_len(c, c->clip, &speed);
				if (c->unreach_t < 0.0f) { // (no way to you: backed off to its post, as Skyrim's unable-to-reach -
					// out of your reach a while, then it tries again; it had stood under you)
					c->unreach_t = fminf(0.0f, c->unreach_t + dt);
					play(c, "walk");
					if (go_to(c, sp->pos[0], sp->pos[2], k->walk_speed, dt) < 1.5f)
						head_for(c, player[0], player[2], 0.0f, dt), play(c, "combat");
				} else {
					go_to_level(c, gx, player[1], gz, speed > 0.3f ? speed : k->run_speed, dt);
					const int no_way = other_level && c->on_nav && c->path_n == 0;
					c->unreach_t = no_way ? c->unreach_t + dt : 0.0f;
					if (no_way && c->unreach_t > UNREACH_BACK_S) {
						c->unreach_t = -UNREACH_HOLD_S, c->replan = 0.0f;
						game_log("creatures: %s can't reach you: back to its post", title_of(c));
					}
					else if (no_way && dist < 3.0f) // (no way up yet: facing you, waiting)
						head_for(c, player[0], player[2], 0.0f, dt), play(c, "combat");
				}
			} else if (c->timer <= 0.0f) { // in reach: face you and strike (a moment between blows)
				head_for(c, player[0], player[2], 0.0f, dt);
				const int power = ++c->attacks % 4 == 0;
				play(c, power ? "power" : c->attacks % 2 ? "attack" : "attack2");
				c->state = ATTACK, c->hit_done = 0;
				voice(c, power ? "power" : "attack");
				target = i, target_shown = 6.0f;
			} else {
				head_for(c, player[0], player[2], 0.0f, dt);
				play(c, "combat");
			}
			break;
		}
		case ATTACK: {
			const float len = clip_len(c, c->clip, NULL);
			head_for(c, player[0], player[2], 0.0f, dt);
			if (!strcmp(c->clip, clip_for(c, "power")) && !(k->flags & F_PERSON)) { // (a lunge: on forward)
				float speed = 0.0f;
				clip_len(c, c->clip, &speed);
				head_for(c, player[0], player[2], dist > 0.8f ? speed : 0.0f, dt);
			}
			if (!c->hit_done && c->clip_t >= len * HIT_AT) {
				c->hit_done = 1;
				strike(c, player, dist, dx, dz);
			}
			if (c->clip_t >= len || len <= 0.0f)
				c->state = CHASE, c->timer = 0.3f + frand() * 0.7f; // (a breath before the next blow)
			break;
		}
		case SHOOT: { // an archer's draw and release, a mage's fire bolt, a spider's spit, at you
			c->heading = atan2f(-dx, -dz);
			if (c->timer <= 0.0f && k->spit_dps > 0.0f) {
				// (from its mouth, lobbed: allowing for the arc on the way)
				const float from[3] = {c->pos[0] - sinf(c->heading) * k->radius, c->pos[1] + k->height * 0.45f,
				                       c->pos[2] - cosf(c->heading) * k->radius};
				float d[3] = {player[0] - from[0], player[1] + 1.0f - from[1], player[2] - from[2]};
				const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), t = l / SPIT_SPEED_MS;
				d[1] += SPIT_GRAVITY * 9.81f * 0.5f * t * t;
				const float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				for (int a = 0; a < 3; a++)
					d[a] /= fmaxf(dl, 0.1f);
				projectiles_fire_spit(from, d, SPIT_SPEED_MS, SPIT_GRAVITY, k->spit_dps, k->spit_s);
				if (cur_victim >= 0) // (at your summons: on it - the spit's only yours to dodge)
					hurt_victim(cur_victim, k->spit_dps * k->spit_s, 0);
				sfx_play_at("spider_spit", c->pos, 30.0f);
				c->shot_t = SPIT_EVERY_S + frand() * 2.0f;
				c->state = CHASE;
				target = i, target_shown = 6.0f;
				break;
			}
			if (c->timer <= 0.0f) {
				const float from[3] = {c->pos[0] - sinf(c->heading) * 0.6f, c->pos[1] + 1.4f, c->pos[2] - cosf(c->heading) * 0.6f};
				float d[3] = {player[0] - from[0], player[1] + 1.2f - from[1], player[2] - from[2]};
				const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				if (HOLDS_BOW(k)) // (aimed a little high: an arrow falls on its way)
					d[1] += 0.35f * 9.81f * 0.5f * (l / ARROW_SPEED_MS) * (l / ARROW_SPEED_MS);
				const float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
				for (int a = 0; a < 3; a++)
					d[a] /= fmaxf(dl, 0.1f);
				if (HOLDS_BOW(k)) { // (Skyrim's fBowNPCSpreadAngle: anywhere within 4 degrees of the aim)
					const float r = sqrtf(frand()) * BOW_NPC_SPREAD_RAD, a = frand() * 6.2831853f;
					const float yaw = r * cosf(a), pitch = r * sinf(a), cy = cosf(yaw), sy = sinf(yaw);
					const float x = d[0] * cy - d[2] * sy, z = d[0] * sy + d[2] * cy;
					d[0] = x, d[2] = z, d[1] += pitch;
					const float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
					for (int q = 0; q < 3; q++)
						d[q] /= n;
				}
				if (HOLDS_BOW(k)) {
					float speed, gravity; // (Skyrim's iron arrow in flight)
					const int model = items_flight(items_by_form(IRON_ARROW_FORM), &speed, &gravity);
					projectiles_fire(from, d, ARROW_SPEED_MS, 0.35f, model, k->damage + ARROW_DAMAGE, 0, 1,
					                 items_by_form(IRON_ARROW_FORM));
					play(c, "bow_release");
					sfx_play_at("bow_fire", c->pos, 30.0f);
				} else if (k->mage_element >= 0) { // (its own spell: Firebolt, Ice Spike, Thunderbolt... - Skyrim's numbers)
					projectiles_fire_hostile_bolt(from, d, BOLT_SPEED_MS, k->mage_element, k->mage_damage, k->mage_area);
					game_log("creatures: %s casts (element %d, %.0f)", title_of(c), k->mage_element, k->mage_damage);
					sfx_play_at(k->mage_element == ELEMENT_FIRE ? "mag_fire_hit" : k->mage_element == ELEMENT_FROST ? "mag_frost_hit"
					            : "mag_shock_hit", c->pos, 30.0f);
				} else {
					projectiles_fire(from, d, BOLT_SPEED_MS, 0.0f, -2, BOLT_DAMAGE, 0, 1, -1);
					sfx_play_at("mag_rune_impact", c->pos, 30.0f);
				}
				if (cur_victim >= 0) // (at your summons: the arrow, the bolt on it)
					hurt_victim(cur_victim, HOLDS_BOW(k) ? k->damage + ARROW_DAMAGE : k->mage_element >= 0 ? k->mage_damage : BOLT_DAMAGE, 0);
				c->shot_t = HOLDS_BOW(k) ? SHOT_EVERY_S : CAST_EVERY_S;
				c->state = CHASE;
				target = i, target_shown = 6.0f;
			}
			break;
		}
		case FLINCH:
			play(c, c->block_t > 0.0f ? "block" : "recoil");
			c->block_t -= dt;
			if (c->timer <= 0.0f)
				c->state = k->aggression == 0 && !(k->flags & F_GUARD) ? FLEE : CHASE, c->flee_t = 5.0f, c->timer = 0.0f;
			break;
		case KNOCKED: { // thrown down (Unrelenting Force): over, lying a moment, up again
			if (c->timer > KNOCK_RISE_S) // (going over, then lying)
				c->fall = fminf(1.0f, c->fall + dt / (FALL_S * 0.6f));
			else // (getting up)
				c->fall = fmaxf(0.0f, c->fall - dt / KNOCK_RISE_S);
			const float e = c->fall * c->fall * (3.0f - 2.0f * c->fall) * c->lie;
			if (k->flags & (F_PERSON | F_UPRIGHT))
				actors_beast_fall(c->beast, -e, 0.0f);
			else
				actors_beast_fall(c->beast, 0.0f, e);
			play(c, "idle");
			if (c->timer <= 0.0f) {
				actors_beast_fall(c->beast, 0.0f, 0.0f);
				c->fall = 0.0f, c->state = k->aggression == 0 && !(k->flags & F_GUARD) ? FLEE : CHASE, c->flee_t = 5.0f, c->timer = 0.3f;
				c->hostile = k->aggression >= 1 || (k->flags & F_GUARD);
			}
			break;
		}
		case DEAD:
			if (c->killmoved) { // (Skyrim's paired clip, through to its end and held there)
				actors_beast_fall(c->beast, 0.0f, 0.0f);
				if (c->km_u < 1.0f) { // (eased into place over the first moments)
					c->km_u = fminf(1.0f, c->km_u + dt / 0.3f);
					const float e = c->km_u * c->km_u * (3.0f - 2.0f * c->km_u);
					c->pos[0] = c->km_from[0] + (c->km_to[0] - c->km_from[0]) * e;
					c->pos[2] = c->km_from[1] + (c->km_to[1] - c->km_from[1]) * e;
				}
			} else if (k->flags & F_NO_DEATH_CLIP) { // (it falls over: people backwards, beasts onto their side)
				if (c->fall <= 0.0f) {
					if (!c->on_nav) // (on the floor, not where it last stood: off the navmesh, an alcove, a tower's planks)
						c->pos[1] = body_floor(c->pos[0], c->pos[2], c->pos[1]);
					c->lie = lie_angle(c, k, 1.0f);
				}
				c->fall = fminf(1.0f, c->fall + dt / FALL_S);
				const float e = c->fall * c->fall * (3.0f - 2.0f * c->fall) * c->lie;
				if (k->flags & (F_PERSON | F_UPRIGHT))
					actors_beast_fall(c->beast, -e, 0.0f);
				else
					actors_beast_fall(c->beast, 0.0f, e);
				play(c, "idle");
			} else {
				if (!c->sloped || c->knock[0] != 0.0f || c->knock[1] != 0.0f) // (flung: laid along wherever it slides to)
					body_slope(c, k);
				// (laid along the ground as it falls; knocked down when it died: upright again)
				c->fall = fmaxf(0.0f, c->fall - dt * 3.0f);
				const float dl = clip_len(c, clip_for(c, "death"), NULL);
				const float u = dl > 0.0f ? fminf(1.0f, c->clip_t / dl) : 1.0f;
				actors_beast_fall(c->beast, c->slope_pitch * u, c->slope_roll * u + c->fall * c->lie);
				play(c, "death");
			}
			break;
		}
	posed:;
		if (dt > 0.0f) // (how it's moving, smoothed)
			for (int k2 = 0; k2 < 2; k2++)
				c->vel[k2] += ((c->pos[k2 * 2] - was[k2]) / dt - c->vel[k2]) * fminf(1.0f, dt * 6.0f);
		// The pose: its clip (dying, striking and recoiling hold the last moment; a draw, full draw)
		const float len = clip_len(c, c->clip, NULL);
		const int once = c->state == DEAD || c->state == ATTACK || c->state == FLINCH || c->state == RISE || c->state == SHOUT || !strcmp(c->clip, "bow_release");
		const float t = once ? fminf(c->clip_t, fmaxf(len - 0.01f, 0.0f))
		              : !strcmp(c->clip, "bow_draw") ? fminf(c->clip_t, len * 0.85f) : c->clip_t;
		actors_beast_pose(c->beast, c->pos[0], c->pos[1], c->pos[2], c->heading, c->clip, t, NULL, 0.0f, 0.0f, dt);
	}
	keep_apart(player);
}

static void draugr_wake(int i) {
	Creature *c = &live[i];
	if (!c->used || c->state != AMBUSH)
		return;
	const Spawn *sp = &spawns[c->spawn];
	const Kind *k = &kinds[c->kind];
	static const char *const WAIT[4][2] = {{"sarc_idle", "sarc_exit"}, {"alcove_idle", "alcove_exit"},
	                                       {"sit_idle", "sit_exit"}, {"lay_idle", "lay_exit"}};
	const int how = sp->ambush >= 6 && sp->ambush <= 9 ? sp->ambush - 6 : -1;
	const int lying = how >= 0 && clip_len(c, WAIT[how][0], NULL) > 0.0f;
	const float rise = lying && WAIT[how][1][0] ? clip_len(c, WAIT[how][1], NULL) : 0.0f;
	c->hostile = 1, c->aware = 1.0f, c->timer = 0.0f;
	voice(c, "growl");
	if (rise > 0.1f) {
		c->state = RISE;
		c->rise_t = c->rise_len = rise;
		play(c, WAIT[how][1]);
		memcpy(c->rise_from, c->pos, sizeof(c->rise_from));
		memcpy(c->rise_to, c->pos, sizeof(c->rise_to));
		NavRef nr;
		float out[3]; // (the floor before it: where it stands once up)
		if (how != 2 && navmesh_nearest_level(c->pos[0], c->pos[1], c->pos[2], 2.5f, 2.0f, &nr, out))
			memcpy(c->rise_to, out, sizeof(c->rise_to)), c->nav = nr, c->on_nav = 1;
	} else {
		c->state = CHASE;
	}
	if (lying && (how == 0 || how == 3)) // (in a sarcophagus: its lid pushed off as it comes up)
		activators_open_lids(c->pos, 1.6f);
	game_log("creatures: %s rises (%s, %.1f s)", k->title, lying ? WAIT[how][1] : "standing", rise);
	c->shout_cd = 2.0f + frand() * 2.0f;
	call_allies(i);
}

static void call_allies(int i) {
	const Creature *c = &live[i];
	const Kind *k = &kinds[c->kind];
	for (int j = 0; j < MAX_LIVE; j++) {
		Creature *o = &live[j];
		if (j == i || !o->used || o->state == DEAD || (!o->hostile && o->state != AMBUSH) ||
		    (o->state != IDLE && o->state != WANDER && o->state != ALERT && o->state != AMBUSH))
			continue;
		const Kind *ok = &kinds[o->kind];
		if ((ok->flags & (F_PERSON | F_UNDEAD)) != (k->flags & (F_PERSON | F_UNDEAD)))
			continue; // (bandits answer bandits, the dead the dead, beasts their pack)
		if (hypotf(o->pos[0] - c->pos[0], o->pos[2] - c->pos[2]) > CALL_M || fabsf(o->pos[1] - c->pos[1]) > 4.0f)
			continue;
		const float a[3] = {o->pos[0], o->pos[1] + 1.5f, o->pos[2]}, b[3] = {c->pos[0], c->pos[1] + 1.2f, c->pos[2]};
		if (collision_ray(a, b) < 1.0f)
			continue; // (round a corner, behind a wall: it doesn't know)
		o->aware = fmaxf(o->aware, 0.7f); // (it comes, and fights when it sees you)
		o->seen_at[0] = c->pos[0], o->seen_at[1] = c->pos[2];
		if (o->state == AMBUSH)
			draugr_wake(j);
	}
}

// Damage to a creature (it flinches if stagger, and once in a while if not: a spell's stream)
static void hurt(int i, float damage, int stagger) {
	Creature *c = &live[i];
	if (c->state == DEAD)
		return;
	const Kind *k = &kinds[c->kind];
	damage *= 1.0f - fminf(0.8f, k->armor * (1.0f - armor_pierce) * 0.0012f); // (their armour turns some aside, as yours does)
	if (c->marked_left > 0.0f) // (Marked for Death: its armour stripped - Skyrim's -25 armour a word, here a quarter more)
		damage *= 1.25f;
	c->health -= damage;
	if (c->floor_share > 0.0f && c->health < c->max_health * c->floor_share) // (a quest's: held there - it flees, or is trapped)
		c->health = c->max_health * c->floor_share;
	target = i, target_shown = 6.0f;
	if ((k->flags & F_GUARD) && blame && !c->hostile && !c->ally) // (a guard at peace struck by you: assault - crime.c)
		struck_hold = k->hold;
	if ((k->flags & F_GUARD) && blame && !c->ally && c->health <= 0.0f) // (... killed by you: murder, if someone saw)
		killed_hold = k->hold, memcpy(killed_at, c->pos, sizeof(killed_at));
	if (c->health <= 0.0f) { // (knocked down already: it stays down)
		c->fall = c->state == KNOCKED ? c->fall : 0.0f;
		c->health = 0.0f, c->state = DEAD, c->clip[0] = 0, c->clip_t = 0.0f;
		spawns[c->spawn].dead = 1, spawns[c->spawn].dead_at = lighting_game_hours();
		if (c->soul_left > 0.0f)
			trapped++, c->soul_left = 0.0f;
		voice(c, "death");
		if ((death_push[0] != 0.0f || death_push[1] != 0.0f) && !(k->flags & F_DRAGON)) { // (flung along the blow)
			const float s = fminf(1.0f, DEATH_PUSH_HEIGHT_M / fmaxf(k->height * k->scale, 0.5f));
			c->knock[0] = death_push[0] * s, c->knock[1] = death_push[1] * s;
		}
		if (blame && (k->flags & F_PERSON) && !c->ally)
			blow_killed = i;
		if (blame && !c->ally && !(k->flags & F_UNDEAD) && !strstr(k->name, "dwarven")) // (a soul to have: not the dead's, not a machine's)
			blow_soul = c->level <= 4 ? 250.0f : c->level <= 16 ? 500.0f : c->level <= 28 ? 1000.0f : c->level <= 38 ? 2000.0f : 3000.0f;
		return;
	}
	if (k->flags & F_DRAGON) { // (its own way: dragon_update)
		c->pain = 0.0f;
		return;
	}
	if (spell_element >= 0 && blame && c->health < c->max_health * LOW_HEALTH_SHARE) { // (the Destruction perks' extra effects)
		if (spell_element == ELEMENT_SHOCK && c->health < c->max_health * DISINTEGRATE_SHARE && perks_has_form(PERK_DISINTEGRATE)) {
			game_log("creatures: %s disintegrated", title_of(c));
			hurt(i, c->health + 1.0f, 0);
			return;
		}
		if (spell_element == ELEMENT_FROST && perks_has_form(PERK_DEEP_FREEZE) && c->state != KNOCKED) {
			c->state = KNOCKED, c->timer = DEEP_FREEZE_S + KNOCK_RISE_S, c->fall = 0.0f, c->knock[0] = c->knock[1] = 0.0f;
			c->lie = lie_angle(c, k, -1.0f);
			game_log("creatures: %s frozen stiff", title_of(c));
			return;
		}
		if (spell_element == ELEMENT_FIRE && perks_has_form(PERK_INTENSE_FLAMES) && c->state != FLEE && !(k->flags & F_UNDEAD)) {
			c->state = FLEE, c->flee_t = INTENSE_FLAMES_S, c->timer = 0.0f;
			game_log("creatures: %s flees the flames", title_of(c));
			return;
		}
	}
	if ((k->aggression >= 1 || (k->flags & F_GUARD)) && !c->ally)
		c->hostile = 1; // (hurt: it fights back - not one on your side: Irileth's men, hit in the fight, turned on you)
	c->pain += damage;
	if (c->state == KNOCKED) // (down: it stays down till it gets up)
		return;
	if (c->state == AMBUSH) {
		draugr_wake(i);
		return;
	}
	if (c->state == RISE) {
		if (frand() < 0.6f)
			voice(c, "hurt");
		return;
	}
	if (c->state == SHOUT && (stagger || c->pain > c->max_health * 0.3f)) {
		c->shout_t = 0.0f;
		c->shout_cd = 3.0f + frand() * 2.0f;
		c->state = FLINCH, c->timer = FLINCH_S, c->clip[0] = 0;
		if (frand() < 0.6f)
			voice(c, "hurt");
		game_log("creatures: %s shout interrupted by damage", title_of(c));
		return;
	}
	if (c->hostile && !c->ally && c->health > 0.0f && c->state != FLEE && !(k->flags & F_DRAGON) && k->aggression > 0 &&
	    k->confidence >= 0 && k->confidence < 5 && c->health < c->max_health * FLEE_BELOW[k->confidence]) {
		c->state = FLEE, c->flee_t = 8.0f + frand() * 4.0f, c->timer = 0.0f; // (its nerve gone: it runs)
		voice(c, "hurt");
		game_log("creatures: %s flees (%.0f of %.0f)", title_of(c), c->health, c->max_health);
		return;
	}
	if (stagger || c->pain > c->max_health * 0.3f) {
		c->pain = 0.0f;
		if (c->state != ATTACK || c->clip_t > 0.15f) // (a blow already going lands)
			c->state = FLINCH, c->timer = FLINCH_S, c->clip[0] = 0;
		if (frand() < 0.6f)
			voice(c, "hurt");
	} else if (c->state == IDLE || c->state == WANDER || c->state == ALERT || c->state == ARREST) {
		c->state = k->aggression == 0 && !(k->flags & F_GUARD) ? FLEE : CHASE; // (hurt from afar: it comes for you, or runs)
		c->flee_t = 5.0f, c->timer = 0.0f;
	}
	if ((k->aggression >= 1 || (k->flags & F_GUARD)) && c->aware < 1.0f) {
		c->aware = 1.0f;
		call_allies(i);
	}
}

float creatures_ray(const float eye[3], const float dir[3], float reach, float radius, float damage) {
	int best = -1;
	float best_t = reach;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD)
			continue;
		const Kind *k = &kinds[c->kind];
		const float r = k->radius + radius;
		for (int s = 0; s < 3; s++) { // (the body as spheres up an upright capsule)
			const float h = k->height * (0.25f + 0.25f * s);
			const float d[3] = {c->pos[0] - eye[0], c->pos[1] + h - eye[1], c->pos[2] - eye[2]};
			const float along = d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2];
			if (along < 0.0f || along > best_t + r)
				continue;
			const float off2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - along * along;
			if (off2 < r * r) {
				const float t = fmaxf(0.0f, along - sqrtf(r * r - off2));
				if (t < best_t)
					best = i, best_t = t;
			}
		}
	}
	if (best < 0)
		return -1.0f;
	last_struck = best;
	if (damage > 0.0f) {
		const float dl = fmaxf(hypotf(dir[0], dir[2]), 0.01f); // (an arrow: a body it kills knocked back a little)
		death_push[0] = dir[0] / dl * DEATH_PUSH_ARROW_MS, death_push[1] = dir[2] / dl * DEATH_PUSH_ARROW_MS;
		hurt(best, damage * player_mult * (ray_sneaks ? sneak_bonus(best, sneak_ranged) : 1.0f), 0);
		death_push[0] = death_push[1] = 0.0f;
	}
	return best_t;
}

int creatures_area(const float pos[3], float radius, float damage) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		// (someone on your side striking - a follower, Irileth: only those against you, not their
		// own; Irileth's blows had her men dying around her at the watchtower)
		if (!blame && (c->ally || !c->hostile))
			continue;
		if (c->used && c->state != DEAD && fabsf(c->pos[1] - pos[1]) < radius + 1.0f &&
		    hypotf(c->pos[0] - pos[0], c->pos[2] - pos[2]) < radius)
			hurt(i, damage, 1), n++;
	}
	return n;
}

int creatures_area_dot(const float pos[3], float radius, float dps, float seconds, float slow_seconds) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (c->used && c->state != DEAD && fabsf(c->pos[1] - pos[1]) < radius + 1.0f &&
		    hypotf(c->pos[0] - pos[0], c->pos[2] - pos[2]) < radius) {
			c->dot_dps = fmaxf(c->dot_left > 0.0f ? c->dot_dps : 0.0f, dps), c->dot_left = fmaxf(c->dot_left, seconds);
			c->slow_left = fmaxf(c->slow_left, slow_seconds);
			hurt(i, 0.0f, 0); // (it knows it's been struck: turns on you)
			n++;
		}
	}
	return n;
}

int creatures_near(const float pos[3], float radius) {
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (c->used && c->state != DEAD && fabsf(c->pos[1] - pos[1]) < 1.5f &&
		    hypotf(c->pos[0] - pos[0], c->pos[2] - pos[2]) < radius + kinds[c->kind].radius)
			return 1;
	}
	return 0;
}

static int blow_power; // (the player's next blow is a power attack: it breaks a guard)

int creatures_hit(const float eye[3], const float dir[3], float reach, float cone_cos, float damage) {
	int best = -1;
	float best_d = 1e9f;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD)
			continue;
		const Kind *k = &kinds[c->kind];
		const float mid[3] = {c->pos[0], c->pos[1] + fminf(k->height * 0.5f, 1.2f), c->pos[2]};
		const float d[3] = {mid[0] - eye[0], mid[1] - eye[1], mid[2] - eye[2]};
		const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		const float flat = hypotf(d[0], d[2]);
		if (len > reach + k->radius + 0.4f || len < 0.01f)
			continue;
		// (in front, along the look, ignoring how far down it's looked: some are low)
		const float along = (d[0] * dir[0] + d[2] * dir[2]) / (fmaxf(flat, 0.01f) * fmaxf(hypotf(dir[0], dir[2]), 0.01f));
		if (along < cone_cos && flat > k->radius)
			continue;
		if (len < best_d)
			best = i, best_d = len;
	}
	if (best < 0)
		return 0;
	Creature *b = &live[best];
	const Kind *bk = &kinds[b->kind];
	// (a person at your blow, ready for it - facing you, not mid-swing: its guard up a share of the
	// time; a power attack breaks through, staggering it)
	const float to_eye = atan2f(-(eye[0] - b->pos[0]), -(eye[2] - b->pos[2]));
	const float facing = cosf(to_eye - b->heading);
	if ((bk->flags & F_PERSON) && bk->weapon >= 1 && bk->weapon <= 6 && !b->hand_to_hand && b->hostile && b->aware >= 1.0f &&
	    b->state == CHASE && facing > 0.5f && !blow_power && frand() < BLOCK_CHANCE) {
		hurt(best, damage * player_mult * BLOCKED_SHARE, 0);
		b->state = FLINCH, b->timer = BLOCK_S, b->block_t = BLOCK_S, b->clip[0] = 0; // (its guard held up, not a flinch)
		sfx_play_at("wpn_block", b->pos, 25.0f);
		game_log("creatures: %s blocks your blow", title_of(b));
		last_struck = best;
		blow_power = 0;
		return 2;
	}
	const int power = blow_power;
	blow_power = 0;
	const float sb = sneak_bonus(best, sneak_melee), dl = fmaxf(hypotf(dir[0], dir[2]), 0.01f);
	const float push = power ? DEATH_PUSH_POWER_MS : DEATH_PUSH_MS;
	death_push[0] = dir[0] / dl * push, death_push[1] = dir[2] / dl * push, blow_unseen = sb > 1.0f;
	hurt(best, damage * player_mult * sb, 1);
	death_push[0] = death_push[1] = 0.0f;
	last_struck = best;
	return 1;
}

float creatures_take_blow_soul(void) {
	const float s = blow_soul;
	blow_soul = 0.0f;
	return s;
}

// Whether the blow you're about to swing would bring down the last foe about you: it (index), -1 not.
// (Skyrim's killmove begins as the lethal attack does, not after it landed.)
int creatures_killmove_pick(const float eye[3], const float dir[3], float reach, float cone_cos, float damage,
                            const float feet[3], float last_m, int *unseen) {
	int best = -1;
	float best_d = 1e9f;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD)
			continue;
		const Kind *k = &kinds[c->kind];
		const float mid[3] = {c->pos[0], c->pos[1] + fminf(k->height * 0.5f, 1.2f), c->pos[2]};
		const float d[3] = {mid[0] - eye[0], mid[1] - eye[1], mid[2] - eye[2]};
		const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		const float flat = hypotf(d[0], d[2]);
		if (len > reach + k->radius + 0.4f || len < 0.01f)
			continue;
		const float along = (d[0] * dir[0] + d[2] * dir[2]) / (fmaxf(flat, 0.01f) * fmaxf(hypotf(dir[0], dir[2]), 0.01f));
		if (along < cone_cos && flat > k->radius)
			continue;
		if (len < best_d)
			best = i, best_d = len;
	}
	if (best < 0)
		return -1;
	const Creature *b = &live[best];
	const Kind *bk = &kinds[b->kind];
	if ((bk->flags & F_DRAGON) || b->held || b->state == KNOCKED)
		return -1;
	const int hidden = sneaking_now && sneak_melee > 1.0f && b->aware < 1.0f && b->state != CHASE && b->state != ATTACK && b->state != SHOUT;
	if (b->health > damage * player_mult * (hidden ? sneak_melee : 1.0f))
		return -1;
	for (int i = 0; i < MAX_LIVE; i++) { // (the last of them: nobody else hostile near)
		const Creature *c = &live[i];
		if (i != best && c->used && c->state != DEAD && c->hostile && !c->ally && c->state != IDLE && c->state != WANDER &&
		    c->state != AMBUSH && hypotf(c->pos[0] - feet[0], c->pos[2] - feet[2]) < last_m)
			return -1;
	}
	if (unseen)
		*unseen = hidden;
	return best;
}

int creatures_killmove_clip_ok(int i, const char *clip) {
	return i >= 0 && i < MAX_LIVE && live[i].used && actors_beast_clip(kinds[live[i].kind].actor_kind, clip, NULL) > 0.0f;
}

// The blow of a killmove: it falls where it stands (no flinging) - its paired clip is set next
int creatures_killmove_strike(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used || live[i].state == DEAD)
		return 0;
	death_push[0] = death_push[1] = 0.0f;
	blow_unseen = 0;
	hurt(i, live[i].health + 1.0f, 0);
	return live[i].state == DEAD;
}

int creatures_take_blow_kill(int *unseen) {
	const int i = blow_killed;
	blow_killed = -1;
	if (unseen)
		*unseen = blow_unseen;
	return i >= 0 && live[i].used && live[i].state == DEAD ? i : -1;
}

int creatures_killmove(int i, const char *clip, const float pos[3], float heading) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used || live[i].state != DEAD)
		return 0;
	Creature *c = &live[i];
	if (actors_beast_clip(kinds[c->kind].actor_kind, clip, NULL) <= 0.0f)
		return 0;
	c->km_from[0] = c->pos[0], c->km_from[1] = c->pos[2], c->km_to[0] = pos[0], c->km_to[1] = pos[2], c->km_u = 0.0f;
	c->km_heading_from = c->heading;
	c->pos[1] = ground_at(pos[0], pos[2], pos[1]);
	c->heading = heading, c->knock[0] = c->knock[1] = 0.0f, c->fall = 0.0f;
	c->killmoved = 1, c->sloped = 1, c->slope_pitch = c->slope_roll = 0.0f;
	snprintf(c->clip, sizeof(c->clip), "%s", clip), c->clip_t = 0.0f;
	game_log("creatures: %s killmove %s", title_of(c), clip);
	return 1;
}

int creatures_killmove_done(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return 1;
	Creature *c = &live[i];
	if (c->state != DEAD)
		return 0;
	if (c->health <= 0.0f) {
		const float dl = clip_len(c, c->clip, NULL);
		// Released instantly when death animation finishes, transitions to ragdoll (c->fall > 0), or lethal blow landed
		if (c->fall > 0.0f || (dl > 0.0f && c->clip_t >= fminf(dl, 1.2f)) || c->clip_t >= 1.5f)
			return 1;
	}
	return 0;
}

void creatures_set_blow_power(int power) {
	blow_power = power;
}

void creatures_set_sneak_attack(float melee, float ranged) {
	sneak_melee = melee, sneak_ranged = ranged;
}

void creatures_set_ray_sneaks(int arrow) {
	ray_sneaks = arrow;
}

float creatures_take_damage(int blocking, float block_share) {
	float d = damage_taken;
	damage_taken = 0.0f;
	return blocking ? d * block_share : d;
}

int creatures_target(const char **name, float *health) {
	if (target < 0 || !live[target].used || target_shown <= 0.0f)
		return 0;
	const Creature *c = &live[target];
	if (c->state == DEAD && target_shown < 4.0f)
		return 0;
	*name = title_of(c);
	*health = c->health / fmaxf(c->max_health, 1.0f);
	return 1;
}

int creatures_aim(const float eye[3], const float dir[3], float *along, const char **label) {
	int best = -1;
	float best_t = 1e9f;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (c->used && (kinds[c->kind].flags & F_GUARD) && !c->hostile && !c->ally && c->state != DEAD &&
		    c->state != ARREST) { // (a guard at peace: "Talk to Whiterun Guard")
			const float mid[3] = {c->pos[0], c->pos[1] + 1.2f, c->pos[2]};
			float t;
			if (aim_hits(eye, dir, mid, 0.6f, REACH_M + 0.5f, &t) && t < best_t)
				best = i, best_t = t;
			continue;
		}
		if (c->used && c->held && c->state != DEAD) { // (Arvel in the web: "Cut Down")
			const float mid[3] = {c->pos[0], c->pos[1] + 1.0f, c->pos[2]};
			float t;
			if (aim_hits(eye, dir, mid, 0.9f, REACH_M + 1.0f, &t) && t < best_t)
				best = i, best_t = t;
			continue;
		}
		if (!c->used || c->state != DEAD || kinds[c->kind].corpse < 0)
			continue;
		// (anywhere along the body as it lies: fallen over, the way it fell; a beast's own death,
		// along its length)
		const Kind *k = &kinds[c->kind];
		const int upright = (k->flags & (F_PERSON | F_UPRIGHT)) != 0, fell = c->fall > 0.0f;
		const float hs = sinf(c->heading), hc = cosf(c->heading), sign = c->lie < 0.0f ? -1.0f : 1.0f;
		float bx, bz, length;
		if (fell)
			bx = sign * (upright ? -hs : -hc), bz = sign * (upright ? -hc : hs),
			length = upright ? k->height * 0.92f : fmaxf(k->height * 0.6f, k->radius);
		else
			bx = -hs, bz = -hc, length = fmaxf(k->radius * 2.0f, 0.5f); // (its length: head to tail)
		for (int s2 = 0; s2 <= 4; s2++) {
			const float f = fell ? s2 / 4.0f : s2 / 4.0f - 0.5f;
			const float mid[3] = {c->pos[0] + bx * length * f, c->pos[1] + 0.3f, c->pos[2] + bz * length * f};
			float t;
			if (aim_hits(eye, dir, mid, fmaxf(k->radius, 0.5f) + 0.3f, REACH_M, &t) && t < best_t)
				best = i, best_t = t;
		}
	}
	if (best < 0)
		return -1;
	*along = best_t;
	snprintf(aim_label, sizeof(aim_label), "%s %s", live[best].held ? "Cut Down" : live[best].state != DEAD ? "Talk to" : "Search",
	         kinds[live[best].kind].title);
	*label = aim_label;
	return best;
}

int creatures_search(int i, const char **name) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used || live[i].state != DEAD)
		return -1;
	const Kind *k = &kinds[live[i].kind];
	*name = title_of(&live[i]);
	const int h = items_open_container(CORPSE_REF | (uint32_t)live[i].spawn, k->corpse);
	if (live[i].arrows > 0) { // (the arrows that struck it: some broke)
		int back = 0;
		for (int a = 0; a < live[i].arrows; a++)
			back += frand() < arrow_recovery;
		if (back > 0)
			items_container_add(h, live[i].arrow_item, back);
		live[i].arrows = 0;
	}
	return h;
}

void creatures_strip(int i) { // (a person's body, all it wore taken: bare under it)
	if (i >= 0 && i < MAX_LIVE && live[i].used && (kinds[live[i].kind].flags & F_PERSON))
		actors_beast_strip(live[i].beast);
}

int creatures_hostile_near(const float pos[3], float radius) {
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (c->used && c->state != DEAD && c->hostile && !c->ally && c->state != IDLE && c->state != WANDER &&
		    c->state != AMBUSH && hypotf(c->pos[0] - pos[0], c->pos[2] - pos[2]) < radius)
			return 1;
	}
	return 0;
}

// The nearest one against you still standing (any mood) within range of from: its feet; 0 if none
int creatures_nearest_foe(const float from[3], float range, float out[3]) {
	float best = range;
	int found = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD || !c->hostile || c->ally)
			continue;
		const float d = hypotf(c->pos[0] - from[0], c->pos[2] - from[2]);
		if (d < best)
			best = d, found = 1, memcpy(out, c->pos, sizeof(c->pos));
	}
	return found;
}

void creatures_clear(void) {
	poison_left = 0.0f; // (somewhere else now: a load, a door)
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used)
			sleep_creature(i);
	spawn_count = file_spawns; // (dragons met at random: gone)
}

// A spawn point's key that stays when the map grows (its number in creatures.txt doesn't: saves kept
// those, and each wider map's numbers put their dead on others - whole camps empty): its kind, its
// place's name and where it stands (to half a metre)
uint32_t creatures_spawn_key(int s) {
	if (s < 0 || s >= spawn_count)
		return 0;
	const Spawn *sp = &spawns[s];
	char text[160];
	snprintf(text, sizeof(text), "%s|%s|%d|%d|%d", kinds[sp->kind].name, sp->place >= 0 ? interior_title(sp->place) : "",
	         (int)lroundf(sp->pos[0] * 2.0f), (int)lroundf(sp->pos[1] * 2.0f), (int)lroundf(sp->pos[2] * 2.0f));
	uint32_t h = 2166136261u;
	for (const char *c = text; *c; c++)
		h = (h ^ (uint8_t)*c) * 16777619u;
	return h ? h : 1;
}

int creatures_spawn_by_key(uint32_t key) {
	static uint32_t *keys;
	static int keyed;
	if (keyed != file_spawns) { // (made once for the pack's spawns)
		free(keys);
		keys = malloc(sizeof(uint32_t) * (file_spawns > 0 ? file_spawns : 1));
		for (int s = 0; keys && s < file_spawns; s++)
			keys[s] = creatures_spawn_key(s);
		keyed = file_spawns;
	}
	for (int s = 0; keys && key && s < file_spawns; s++)
		if (keys[s] == key)
			return s;
	return -1;
}

int creatures_save(uint32_t *dead, float *dead_at, int max) {
	int n = 0;
	for (int s = 0; s < file_spawns && n < max; s++) // (not dragons met at random)
		if (spawns[s].dead)
			dead_at[n] = spawns[s].dead_at, dead[n++] = (uint32_t)s;
	return n;
}

void creatures_load(const uint32_t *dead, const float *dead_at, int n) {
	creatures_clear();
	for (int s = 0; s < spawn_count; s++)
		spawns[s].dead = 0;
	for (int i = 0; i < n; i++)
		if (dead[i] < (uint32_t)spawn_count)
			spawns[dead[i]].dead = 1, spawns[dead[i]].dead_at = dead_at ? dead_at[i] : lighting_game_hours();
}

// Every spawn point of a place dead: it's cleared (Skyrim waits longer to fill it again)
static int place_cleared(int place) {
	for (int s = 0; s < spawn_count; s++)
		if (spawns[s].place == place && !spawns[s].dead)
			return 0;
	return 1;
}

int creatures_respawn(int place, const float player[3]) {
	const float now = lighting_game_hours();
	int n = 0, checked_place = 0x7fffffff, cleared = 0;
	for (int s = 0; s < spawn_count; s++) {
		Spawn *sp = &spawns[s];
		if (!sp->dead || sp->live >= 0)
			continue;
		if (sp->place != checked_place)
			checked_place = sp->place, cleared = place_cleared(sp->place);
		if (now - sp->dead_at < (cleared ? RESPAWN_CLEARED_H : RESPAWN_H))
			continue;
		// (outdoors, not where you'd see it come back)
		if (sp->place == -1 && place == -1 && hypotf(sp->pos[0] - player[0], sp->pos[2] - player[2]) < SLEEP_OUT_M)
			continue;
		sp->dead = 0;
		items_forget_container(CORPSE_REF | (uint32_t)s); // (its body's things go with it)
		n++;
	}
	return n;
}

int creatures_aim_assist(const float eye[3], const float dir[3], float cone_cos, float range, float speed, float out[3]) {
	int best = -1;
	float best_cos = cone_cos;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD || c->ally || c->held)
			continue; // (not those on your side, nor Arvel in his web)
		const Kind *k = &kinds[c->kind];
		const float mid[3] = {c->pos[0], c->pos[1] + k->height * 0.55f, c->pos[2]};
		const float d[3] = {mid[0] - eye[0], mid[1] - eye[1], mid[2] - eye[2]};
		const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (l < 0.5f || l > range)
			continue;
		const float cs = (d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2]) / l;
		if (cs > best_cos && collision_ray(eye, mid) >= 1.0f) {
			best = i, best_cos = cs, memcpy(out, mid, sizeof(mid));
			if (speed > 0.0f) // (where it will be when the arrow gets there)
				for (int k2 = 0; k2 < 2; k2++)
					out[k2 * 2] += c->vel[k2] * fminf(l / speed, 1.5f);
		}
	}
	return best >= 0;
}

// In the cone in front of `from` (dir: unit, flat), within range, in sight: each, nearest first
static int in_cone(const Creature *c, const float from[3], const float dir[3], float range, float cone_cos, float *d) {
	if (!c->used || c->state == DEAD)
		return 0;
	const Kind *k = &kinds[c->kind];
	const float mid[3] = {c->pos[0], c->pos[1] + k->height * 0.5f, c->pos[2]};
	const float v[3] = {mid[0] - from[0], mid[1] - from[1], mid[2] - from[2]};
	const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > range + k->radius || fabsf(v[1]) > range * 0.6f + k->height)
		return 0;
	const float flat = hypotf(v[0], v[2]), along = (v[0] * dir[0] + v[2] * dir[2]) / fmaxf(flat, 0.01f);
	if (flat > k->radius + 0.5f && along < cone_cos)
		return 0;
	if (collision_ray(from, mid) < 1.0f)
		return 0;
	*d = l;
	return 1;
}

static int bewitch(Creature *c, int effect, float max_level, float seconds);

// Animals (Skyrim's ActorTypeAnimal: Kyne's Peace, Animal Allegiance) - by their kinds' names
static int animal(const Kind *k) {
	static const char *const ANIMALS[] = {"wolf", "bear", "sabrecat", "deer", "elk", "fox", "goat", "horker", "mudcrab",
	                                      "skeever", "slaughterfish", "mammoth", "chicken", "hare", "dog", "horse", "cow",
	                                      "spider", "chaurus", "troll", NULL};
	if (k->flags & (F_PERSON | F_UNDEAD | F_DRAGON))
		return 0;
	for (int a = 0; ANIMALS[a]; a++)
		if (!strncmp(k->name, ANIMALS[a], strlen(ANIMALS[a])))
			return 1;
	return 0;
}

static int bewitch(Creature *c, int effect, float max_level, float seconds);

// Dragonrend (Joor Zah Frul): the dragons before you made to know mortality - out of the sky, and
// held to the ground this long. How many it reached
int creatures_dragonrend(const float from[3], const float dir[3], float range, float seconds) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (!c->used || c->state == DEAD || !(kinds[c->kind].flags & F_DRAGON))
			continue;
		const float d[3] = {c->pos[0] - from[0], c->pos[1] - from[1], c->pos[2] - from[2]};
		const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (l > range || (l > 1.0f && (d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2]) / l < 0.6f))
			continue;
		c->rend_t = fmaxf(c->rend_t, seconds);
		game_log("creatures: %s brought down by Dragonrend for %.0f s", title_of(c), seconds);
		n++;
	}
	return n;
}

// Storm Call (Strun Bah Qo): a bolt on one of your foes about pos within radius - its harm; where it
// struck. 0 if there's none
int creatures_storm_strike(const float pos[3], float radius, float damage, float at[3]) {
	int pick[MAX_LIVE], np = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (c->used && c->state != DEAD && !c->ally && (c->hostile || kinds[c->kind].aggression >= 2) &&
		    hypotf(c->pos[0] - pos[0], c->pos[2] - pos[2]) < radius)
			pick[np++] = i;
	}
	if (!np)
		return 0;
	const int i = pick[rand() % np];
	memcpy(at, live[i].pos, 3 * sizeof(float));
	last_struck = i;
	hurt(i, damage * player_mult, 1);
	game_log("creatures: Storm Call's lightning on %s (%.0f left)", title_of(&live[i]), live[i].health);
	return 1;
}

int creatures_shout_effect(const float from[3], const float dir[3], float range, float cone_cos, int what, float magnitude,
                           float seconds) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		float l;
		if (!in_cone(c, from, dir, range, cone_cos, &l) || c->state == DEAD)
			continue;
		const Kind *k = &kinds[c->kind];
		switch (what) {
		case CSHOUT_DISARM: // (Zun Haal Viik: their weapons out of their hands - fists after)
			if (!(k->flags & (F_PERSON | F_UPRIGHT)) || k->weapon <= 0 || c->state == DRAGON || c->hand_to_hand) // (anyone armed: draugr too)
				continue;
			c->hand_to_hand = 1;
			break;
		case CSHOUT_ICE_FORM: // (Iiz Slen Nus: frozen solid, and the cold's harm)
			if (c->state == DRAGON)
				continue;
			hurt(i, magnitude * player_mult, 0);
			if (c->state == DEAD)
				break;
			c->state = KNOCKED, c->timer = seconds + KNOCK_RISE_S, c->fall = 0.0f, c->lie = 0.0f; // (stood stiff, not thrown)
			c->knock[0] = c->knock[1] = 0.0f;
			c->slow_left = fmaxf(c->slow_left, seconds);
			break;
		case CSHOUT_MARK: // (Krii Lun Aus: its armour down, its life draining)
		case CSHOUT_DRAIN: // (Gaan Lah Haas: its life drained away)
			c->bleed_dps = fmaxf(c->bleed_dps, magnitude), c->bleed_left = fmaxf(c->bleed_left, seconds);
			if (what == CSHOUT_MARK)
				c->marked_left = fmaxf(c->marked_left, seconds);
			if (c->ally == 0)
				c->hostile = 1, c->aware = 1.0f;
			break;
		case CSHOUT_CALM_ANIMALS: // (Kaan Drem Ov: the animals at peace)
			if (!animal(k))
				continue;
			bewitch(c, CSPELL_CALM, 0.0f, seconds);
			break;
		case CSHOUT_FEAR: // (Dismay - Faas Ru Maar: those up to its level flee)
			if (c->state == DRAGON || !bewitch(c, CSPELL_FEAR, magnitude, seconds))
				continue;
			break;
		case CSHOUT_TAME_ANIMALS: // (Raan Mir Tah: the animals on your side)
			if (!animal(k) || c->ally)
				continue;
			c->ally = 1, c->tame = 1, c->ally_left = seconds, c->hostile = 0, c->aware = 0.0f, c->calm_left = 0.0f;
			if (c->state == CHASE || c->state == FLEE || c->state == SHOUT)
				c->state = IDLE;
			break;
		default:
			continue;
		}
		n++;
	}
	return n;
}

int creatures_shout_push(const float from[3], const float dir[3], float range, float cone_cos, float push, float damage,
                         int knock_down) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		float l;
		if (!in_cone(c, from, dir, range, cone_cos, &l))
			continue;
		const Kind *k = &kinds[c->kind];
		hurt(i, damage * player_mult, 1);
		n++;
		if (k->flags & F_DRAGON) // (too big to throw)
			continue;
		if (c->state == DEAD && !knock_down)
			continue;
		if (c->state == RISE)
			continue;
		// (flung away from you: harder the nearer and lighter - a mammoth barely shifts)
		const float heft = fmaxf(1.0f, k->height * k->radius * 1.6f);
		const float speed = push * SHOUT_PUSH_MS * fmaxf(0.3f, 1.0f - l / (range + 1.0f)) / heft;
		const float ax = c->pos[0] - from[0], az = c->pos[2] - from[2], al = fmaxf(hypotf(ax, az), 0.01f);
		c->knock[0] = ax / al * speed, c->knock[1] = az / al * speed;
		if (c->state == DEAD)
			continue;
		if (knock_down && heft < 3.0f) { // (down: Skyrim's ragdoll - over backwards, away from you)
			c->heading = atan2f(ax, az); // (facing you: it falls away)
			c->state = KNOCKED, c->timer = KNOCK_DOWN_S + KNOCK_RISE_S, c->fall = 0.0f;
			c->lie = lie_angle(c, k, -1.0f); // (over backwards: away from you)
		} else {
			c->state = FLINCH, c->timer = FLINCH_S, c->clip[0] = 0;
		}
	}
	return n;
}

int creatures_shout_breath(const float from[3], const float dir[3], float range, float cone_cos, float damage, float seconds,
                           float slow_seconds, float stagger) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		float l;
		if (!in_cone(c, from, dir, range, cone_cos, &l))
			continue;
		n++;
		if (seconds > 1.0f) { // (Frost Breath: its damage over seconds, and the slow)
			c->dot_dps = damage * player_mult / seconds, c->dot_left = seconds;
			c->slow_left = fmaxf(c->slow_left, slow_seconds);
			hurt(i, 0.0f, stagger >= 0.5f);
		} else {
			hurt(i, damage * player_mult, stagger >= 0.5f);
		}
	}
	return n;
}

int creatures_frighten(const float pos[3], float radius, float seconds) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (!c->used || c->state == DEAD || c->state == KNOCKED || c->state == DRAGON ||
		    hypotf(c->pos[0] - pos[0], c->pos[2] - pos[2]) > radius || fabsf(c->pos[1] - pos[1]) > 4.0f)
			continue;
		c->state = FLEE, c->flee_t = seconds, c->timer = 0.0f;
		n++;
	}
	return n;
}

// An Illusion spell (or Paralyze, Turn Undead) on one creature, if it isn't above the level
// Skyrim's undead, Daedra and Dwarven automatons: the mind spells don't take on them (Master of the Mind: they do)
static int mindless(const Kind *k) {
	static const char *const NONE[] = {"atronach", "dremora", "dwarven", "centurion", "sphere", "ghost", "wisp", NULL};
	if (k->flags & F_UNDEAD)
		return 1;
	for (int n = 0; NONE[n]; n++)
		if (strstr(k->name, NONE[n]))
			return 1;
	return 0;
}

static int bewitch(Creature *c, int effect, float max_level, float seconds) {
	const Kind *k = &kinds[c->kind];
	if (c->state == DEAD || c->state == DRAGON || c->state == RISE || (max_level > 0.0f && (float)c->level > max_level))
		return 0; // (dragons: none of it takes)
	if ((effect == CSPELL_FEAR || effect == CSPELL_CALM || effect == CSPELL_FRENZY) && mindless(k) &&
	    !perks_has_form(PERK_MASTER_OF_THE_MIND))
		return 0;
	if (effect == CSPELL_TURN && !(k->flags & F_UNDEAD))
		return 0;
	switch (effect) {
	case CSPELL_FEAR:
	case CSPELL_TURN:
		c->state = FLEE, c->flee_t = seconds, c->timer = 0.0f, c->calm_left = 0.0f;
		break;
	case CSPELL_CALM:
		c->calm_left = seconds;
		break;
	case CSPELL_SOULTRAP: // (its soul yours if it dies within the spell's time)
		c->soul_left = seconds;
		break;
	case CSPELL_FRENZY: // (a Frenzy poison: in a rage, at whoever's nearest - here, that's you)
		c->hostile = 1, c->calm_left = 0.0f, c->aware = 1.0f, c->timer = 0.0f;
		if (c->state != KNOCKED)
			c->state = CHASE;
		break;
	case CSPELL_PARALYZE:
		c->state = KNOCKED, c->timer = seconds + KNOCK_RISE_S, c->fall = 0.0f;
		c->lie = lie_angle(c, k, -1.0f);
		c->knock[0] = c->knock[1] = 0.0f;
		break;
	}
	return 1;
}

int creatures_spell_ray(const float eye[3], const float dir[3], float reach, int effect, float max_level, float seconds,
                        float area, float hit[3]) {
	const float t = creatures_ray(eye, dir, reach, 0.2f, 0.0f);
	if (t < 0.0f)
		return 0;
	for (int k = 0; k < 3; k++)
		hit[k] = eye[k] + dir[k] * t;
	int n = bewitch(&live[last_struck], effect, max_level, seconds);
	if (area > 0.0f) // (Rout, Pacify: those about it too)
		for (int i = 0; i < MAX_LIVE; i++)
			if (i != last_struck && live[i].used && hypotf(live[i].pos[0] - hit[0], live[i].pos[2] - hit[2]) < area &&
			    fabsf(live[i].pos[1] - hit[1]) < 4.0f)
				n += bewitch(&live[i], effect, max_level, seconds);
	return n;
}

int creatures_spell_about(const float pos[3], float radius, int effect, float max_level, float seconds) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used && live[i].state != DEAD && !live[i].ally && hypotf(live[i].pos[0] - pos[0], live[i].pos[2] - pos[2]) < radius &&
		    fabsf(live[i].pos[1] - pos[1]) < 4.0f)
			n += bewitch(&live[i], effect, max_level, seconds);
	return n;
}

int creatures_hurt_where(int (*inside)(const float pos[3], void *ctx), void *ctx, float damage) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (c->used && c->state != DEAD && inside(c->pos, ctx))
			hurt(i, damage, 0), n++;
	}
	return n;
}

int creatures_hurt_struck(float damage) {
	if (damage <= 0.0f || last_struck < 0 || last_struck >= MAX_LIVE || !live[last_struck].used || live[last_struck].state == DEAD)
		return 0;
	hurt(last_struck, damage, 0);
	sfx_play_at("itm_poisonuse", live[last_struck].pos, 20.0f);
	return 1;
}

enum { ARCH_POISON_FEAR = 7, ARCH_POISON_FRENZY = 8 }; // (Skyrim's Demoralize, Frenzy: its magnitude the highest level)

int creatures_poison_struck(int poison) {
	if (poison < 0 || last_struck < 0 || last_struck >= MAX_LIVE || !live[last_struck].used ||
	    live[last_struck].state == DEAD)
		return 0;
	creatures_hurt_struck(items_poison_damage(poison));
	Creature *c = &live[last_struck];
	if (c->state == DRAGON) // (no poison stops a dragon)
		return 1;
	ItemEffect e[ITEM_EFFECTS_MAX];
	const int n = items_effects(poison, e, ITEM_EFFECTS_MAX);
	for (int i = 0; i < n && c->state != DEAD; i++) {
		const MagicEffect *m = items_magic_effect(e[i].mgef);
		if (m && m->archetype == ARCH_PARALYSIS && e[i].seconds > 0.0f) { // (down, stiff, for its seconds)
			c->state = KNOCKED, c->timer = e[i].seconds + KNOCK_RISE_S, c->fall = 0.0f;
			c->lie = lie_angle(c, &kinds[c->kind], -1.0f);
			c->knock[0] = c->knock[1] = 0.0f;
		} else if (m && m->av == 30 && (m->flags & MGEF_DETRIMENTAL)) { // Slow
			c->slow_left = fmaxf(c->slow_left, e[i].seconds);
		} else if (m && (m->archetype == ARCH_POISON_FEAR || m->archetype == ARCH_POISON_FRENZY)) { // Fear, Frenzy
			bewitch(c, m->archetype == ARCH_POISON_FEAR ? CSPELL_FEAR : CSPELL_FRENZY, e[i].magnitude, e[i].seconds);
		}
	}
	return 1;
}

int creatures_alert_at(const float pos[3], float radius, const float source[3]) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (!c->used || !c->hostile || (c->state != IDLE && c->state != WANDER && c->state != ALERT && c->state != AMBUSH) ||
		    hypotf(c->pos[0] - pos[0], c->pos[2] - pos[2]) > radius || fabsf(c->pos[1] - pos[1]) > 4.0f)
			continue;
		if (c->state == AMBUSH) {
			draugr_wake(i);
			n++;
			continue;
		}
		// (a sound nearby - an arrow striking: it comes looking the way it came, not straight at you)
		c->aware = fmaxf(c->aware, ALERT_AT);
		c->seen_at[0] = (pos[0] + source[0]) * 0.5f, c->seen_at[1] = (pos[2] + source[2]) * 0.5f;
		c->state = ALERT, c->timer = SEARCH_S;
		n++;
	}
	return n;
}

void creatures_set_player_visibility(float m) {
	player_seen_mult = m;
}

void creatures_set_player_damage_mult(float m) {
	player_mult = m;
}

// The player's perks on a blow or an arrow that just landed (on creatures_ray / hit's last)
static Creature *struck_live(void) {
	if (last_struck < 0 || last_struck >= MAX_LIVE || !live[last_struck].used || live[last_struck].state == DEAD)
		return NULL;
	return &live[last_struck];
}

void creatures_set_armor_pierce(float share) {
	armor_pierce = fminf(1.0f, fmaxf(0.0f, share));
}

void creatures_set_arrow_recovery(float chance) {
	arrow_recovery = fminf(1.0f, fmaxf(0.0f, chance));
}

int creatures_paralyze_struck(float seconds) {
	Creature *c = struck_live();
	if (!c || (kinds[c->kind].flags & F_DRAGON)) // (nothing stops a dragon)
		return 0;
	c->state = KNOCKED, c->timer = seconds + KNOCK_RISE_S, c->fall = 0.0f;
	c->lie = lie_angle(c, &kinds[c->kind], -1.0f);
	c->knock[0] = c->knock[1] = 0.0f;
	return 1;
}

int creatures_bleed_struck(float dps, float seconds) {
	Creature *c = struck_live();
	if (!c || (kinds[c->kind].flags & F_DRAGON))
		return 0;
	c->bleed_dps = fmaxf(dps * player_mult, c->bleed_left > 0.0f ? c->bleed_dps : 0.0f);
	c->bleed_left = fmaxf(c->bleed_left, seconds);
	return 1;
}

int creatures_stagger_struck(void) {
	Creature *c = struck_live();
	const Kind *k = c ? &kinds[c->kind] : NULL;
	if (!c || (k->flags & F_DRAGON) || k->height > 3.5f || c->state == KNOCKED || c->state == RISE) // (all but the largest: giants, mammoths)
		return 0;
	if (c->state == SHOUT) {
		c->shout_t = 0.0f;
		c->shout_cd = 3.0f + frand() * 2.0f;
		c->state = FLINCH, c->timer = FLINCH_S, c->clip[0] = 0;
		game_log("creatures: %s shout interrupted by stagger", title_of(c));
		return 1;
	}
	if (c->state != ATTACK || c->clip_t > 0.15f)
		c->state = FLINCH, c->timer = FLINCH_S, c->clip[0] = 0;
	return 1;
}

// A bash (Skyrim's): staggered even mid-swing - a bash breaks a blow, a power attack too; a
// power bash may take the weapon from their hand (Disarming Bash)
int creatures_bash_struck(int disarm) {
	Creature *c = struck_live();
	const Kind *k = c ? &kinds[c->kind] : NULL;
	if (!c || (k->flags & F_DRAGON) || k->height > 3.5f || c->state == KNOCKED || c->state == RISE)
		return 0;
	if (c->state == SHOUT) {
		c->shout_t = 0.0f;
		c->shout_cd = 3.0f + frand() * 2.0f;
		game_log("creatures: %s shout interrupted by bash", title_of(c));
	}
	c->state = FLINCH, c->timer = FLINCH_S, c->clip[0] = 0, c->hit_done = 1;
	if (disarm && (k->flags & (F_PERSON | F_UPRIGHT)) && k->weapon > 0 && !c->hand_to_hand) {
		c->hand_to_hand = 1;
		game_log("creatures: %s disarmed by a bash", title_of(c));
		return 2;
	}
	return 1;
}

// One of them winding up a power attack at you, within reach (Quick Reflexes: time slows)
int creatures_power_attack_on_you(const float player[3]) {
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state != ATTACK || c->ally || c->victim >= 0 || c->hit_done ||
		    strcmp(c->clip, clip_for(c, "power")))
			continue;
		const Kind *k = &kinds[c->kind];
		if (hypotf(c->pos[0] - player[0], c->pos[2] - player[2]) < k->reach + k->radius + 1.5f &&
		    fabsf(c->pos[1] - player[1]) < 2.5f)
			return 1;
	}
	return 0;
}

int creatures_bewitch_struck(int effect, float max_level, float seconds) {
	Creature *c = struck_live();
	return c ? bewitch(c, effect, max_level, seconds) : 0;
}

int creatures_magic_struck(float damage, float pos[3]) {
	Creature *c = struck_live();
	if (!c)
		return 0;
	const Kind *k = &kinds[c->kind];
	pos[0] = c->pos[0], pos[1] = c->pos[1] + fminf(k->height * 0.5f, 1.2f), pos[2] = c->pos[2];
	if (damage > 0.0f)
		hurt(last_struck, damage * player_mult, 0);
	return 1;
}

int creatures_place_hostiles(int place, int *alive) {
	int n = 0;
	*alive = 0;
	for (int i = 0; i < spawn_count; i++) {
		const Spawn *sp = &spawns[i];
		if (sp->place != place || sp->kind < 0 || sp->transient || kinds[sp->kind].aggression < 2)
			continue;
		n++;
		*alive += !sp->dead;
	}
	return n;
}

// --- Guards (crime.c) ---------------------------------------------------------------------

void creatures_set_hold_mood(int hold, int mood) {
	if (hold < 0 || hold >= CRIME_HOLDS || hold_mood[hold] == mood)
		return;
	const int was = hold_mood[hold];
	hold_mood[hold] = mood;
	if (mood || !was)
		return;
	for (int i = 0; i < MAX_LIVE; i++) { // (at peace again: the bounty paid or served)
		Creature *c = &live[i];
		if (!c->used || c->state == DEAD || !(kinds[c->kind].flags & F_GUARD) || kinds[c->kind].hold != hold)
			continue;
		c->hostile = 0, c->aware = 0.0f;
		if (c->state == CHASE || c->state == ATTACK || c->state == SHOOT || c->state == ALERT || c->state == ARREST)
			c->state = WANDER, c->target[0] = spawns[c->spawn].pos[0], c->target[1] = spawns[c->spawn].pos[2], c->timer = 0.0f;
	}
}

int creatures_take_arrest(int *hold) {
	const int i = arrest_ready;
	arrest_ready = -1;
	if (i < 0 || !live[i].used || live[i].state != ARREST)
		return -1;
	*hold = kinds[live[i].kind].hold;
	return i;
}

void creatures_arrest_done(int i, int resisted) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return;
	Creature *c = &live[i];
	c->arrest_wait = ARREST_AGAIN_S;
	if (resisted)
		c->hostile = 1, c->aware = 1.0f, c->state = CHASE, c->timer = 0.5f;
	else if (c->state == ARREST)
		c->state = WANDER, c->target[0] = spawns[c->spawn].pos[0], c->target[1] = spawns[c->spawn].pos[2], c->timer = 0.0f;
}

int creatures_guard_sees(const float at[3], float range) {
	for (int i = 0; i < MAX_LIVE; i++) {
		const Creature *c = &live[i];
		if (!c->used || c->state == DEAD || !(kinds[c->kind].flags & F_GUARD) || kinds[c->kind].hold < 0 ||
		    fabsf(c->pos[1] - at[1]) > 4.0f || hypotf(c->pos[0] - at[0], c->pos[2] - at[2]) > range)
			continue;
		if (sees(c, at))
			return kinds[c->kind].hold;
	}
	return -1;
}

int creatures_take_guard_struck(void) {
	const int h = struck_hold;
	struck_hold = -1;
	return h;
}

int creatures_take_guard_killed(float at[3]) {
	const int h = killed_hold;
	killed_hold = -1;
	if (h >= 0)
		memcpy(at, killed_at, sizeof(killed_at));
	return h;
}

const char *creatures_title(int i) {
	return i >= 0 && i < MAX_LIVE && live[i].used ? title_of(&live[i]) : "";
}

const char *creatures_voice(int i) {
	return i >= 0 && i < MAX_LIVE && live[i].used ? kinds[live[i].kind].voice : "";
}

int creatures_guard_count(void) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++)
		n += live[i].used && live[i].state != DEAD && (kinds[live[i].kind].flags & F_GUARD);
	return n;
}

int creatures_position(int i, float pos[3]) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return 0;
	memcpy(pos, live[i].pos, sizeof(live[i].pos));
	return 1;
}

int creatures_cut_down(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used || !live[i].held || live[i].state == DEAD)
		return 0;
	Creature *c = &live[i];
	c->held = 0, c->clip[0] = 0;
	const float len = clip_len(c, "web_exit", NULL);
	c->cut_t = len > 0.2f ? len : 1.0f;
	play(c, "web_exit");
	quests_voice("arvel_free"); // (INFO 0003BC5E)
	activators_web_down_near(c->pos, 2.5f); // (the web he hung in, cut away with him: the way on open)
	sfx_play_at("web_cut", c->pos, 15.0f);
	game_log("creatures: %s cut down", kinds[c->kind].title);
	return 1;
}

void creatures_set_player_identity(int female, const char *race_eid) {
	player_female = female;
	snprintf(player_race, sizeof(player_race), "%s", race_eid ? race_eid : "NordRace");
}

int creatures_guard_talk(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used || !(kinds[live[i].kind].flags & F_GUARD) || live[i].hostile ||
	    live[i].state == DEAD)
		return 0;
	Creature *c = &live[i];
	const Kind *k = &kinds[c->kind];
	if (!crime_guard_hello(i, k->hold, k->voice, player_female, player_race))
		return 0;
	c->talk_t = TALK_HOLD_S;
	return 1;
}

int creatures_find_titled(const char *title, float out[3]) {
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used && !strcasecmp(kinds[live[i].kind].title, title)) {
			memcpy(out, live[i].pos, sizeof(live[i].pos));
			return 1;
		}
	return 0;
}

void creatures_release_near(const float pos[3], float radius) {
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used && live[i].held && live[i].state != DEAD &&
		    hypotf(live[i].pos[0] - pos[0], live[i].pos[2] - pos[2]) < radius && fabsf(live[i].pos[1] - pos[1]) < 4.0f)
			creatures_cut_down(i);
}

int creatures_dismiss_allies(const char *kind_name) {
	int n = 0;
	for (int i = 0; i < MAX_LIVE; i++) {
		Creature *c = &live[i];
		if (!c->used || !c->ally || strcmp(kinds[c->kind].name, kind_name) || !spawns[c->spawn].transient)
			continue;
		spawns[c->spawn].kind = -1;
		sleep_creature(i);
		n++;
	}
	return n;
}

int creatures_place_dragon(const char *kind_name, const float pos[3], float heading, int calm) {
	int kind = -1;
	if (kind_name)
		for (int k = 0; k < kind_count && kind < 0; k++)
			if (!strcmp(kinds[k].name, kind_name))
				kind = k;
	if (kind < 0)
		kind = dragon_for_level();
	if (kind < 0)
		return -1;
	int s = -1;
	for (int i = file_spawns; i < spawn_count; i++)
		if (spawns[i].transient && spawns[i].kind < 0)
			s = i;
	if (s < 0 && spawn_count < MAX_SPAWNS)
		s = spawn_count++;
	if (s < 0)
		return -1;
	Spawn *sp = &spawns[s];
	memset(sp, 0, sizeof(*sp));
	sp->kind = kind, sp->transient = 1, sp->place = cur_place, sp->live = -1, sp->ambush = 0; // (this one, not leveled: Alduin himself)
	memcpy(sp->pos, pos, sizeof(sp->pos));
	sp->heading = heading;
	wake(s);
	if (sp->live < 0) {
		game_log("creatures: %s could not be placed (ready %d)", kinds[kind].title, kinds[kind].actor_kind);
		sp->kind = -1;
		return -1;
	}
	Creature *c = &live[sp->live];
	c->pos[0] = pos[0], c->pos[2] = pos[2], c->pos[1] = dragon_ground(pos[0], pos[2], pos[1]);
	c->heading = heading, c->calm = calm, c->hostile = !calm; // (looking on: no one's foe yet)
	dragon_start(c, D_PERCH, NULL);
	game_log("creatures: %s placed at %.0f %.0f %.0f%s", kinds[kind].title, pos[0], pos[1], pos[2], calm ? " (calm)" : "");
	return sp->live;
}

// A quest's foe (the Silver Hand's ambush in Dustman's Cairn, Kodlak's wolf spirit): a kind by its
// editor name or title, placed at pos facing heading and coming for you; ghost: a spirit's look
int creatures_place_foe(const char *kind_name, const float pos[3], float heading, int ghost, int place) {
	int kind = -1;
	for (int k = 0; k < kind_count && kind < 0; k++)
		if (!strcmp(kinds[k].name, kind_name))
			kind = k;
	for (int k = 0; k < kind_count && kind < 0; k++)
		if (!strcmp(kinds[k].title, kind_name) && !(kinds[k].flags & F_GUARD))
			kind = k;
	if (kind < 0)
		return -1;
	int s = -1;
	for (int i = file_spawns; i < spawn_count; i++)
		if (spawns[i].transient && spawns[i].kind < 0)
			s = i;
	if (s < 0 && spawn_count < MAX_SPAWNS)
		s = spawn_count++;
	if (s < 0)
		return -1;
	Spawn *sp = &spawns[s];
	memset(sp, 0, sizeof(*sp));
	sp->kind = kind, sp->transient = 1, sp->quest = 1, sp->place = place, sp->live = -1; // (the place you're in now: a door just
	                                                                                    //  passed, before creatures_update has it)
	memcpy(sp->pos, pos, sizeof(sp->pos));
	sp->heading = heading;
	wake(s);
	if (sp->live < 0) {
		sp->kind = -1;
		return -1;
	}
	Creature *c = &live[sp->live];
	c->hostile = 1, c->aware = 1.0f, c->state = CHASE, c->timer = 0.0f;
	if (ghost)
		actors_beast_ghost(c->beast, 1);
	game_log("creatures: %s placed for a quest at %.0f %.0f %.0f%s", kinds[kind].title, pos[0], pos[1], pos[2], ghost ? " (a spirit)" : "");
	return sp->live;
}

// A quest's foe drawn as someone in particular (an actor's own model, pack actors/<model>.sknpc,
// on the people's clips for that sex): the keep's Imperial Captain - she fought as a man in a
// trooper's armour. 1 done
int creatures_live_look_as(int i, const char *model, int female) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return 0;
	char name[64];
	snprintf(name, sizeof(name), "actors/%s", model);
	const int ak = actors_beast_kind(name, female ? "@female" : "@male");
	const int b = ak >= 0 ? actors_beast_spawn(ak) : -1;
	if (b < 0)
		return 0;
	actors_beast_remove(live[i].beast);
	live[i].beast = b;
	game_log("creatures: %s drawn as %s", kinds[live[i].kind].title, model);
	return 1;
}

int creatures_kind_title_live(int i, const char *title) { // (a live one of that title: the quest's foe still up)
	return i >= 0 && i < MAX_LIVE && live[i].used && live[i].state != DEAD && !strcmp(kinds[live[i].kind].title, title);
}

int creatures_title_killed(const char *title) { // (a placed one of that name killed: Saarthal's Jyrik Gauldurson)
	for (int s = 0; s < spawn_count; s++)
		if (spawns[s].kind >= 0 && spawns[s].dead && !strcmp(kinds[spawns[s].kind].title, title))
			return 1;
	for (int i = 0; i < MAX_LIVE; i++)
		if (live[i].used && live[i].state == DEAD && !strcmp(kinds[live[i].kind].title, title))
			return 1;
	return 0;
}

int creatures_spawn_of_ref(uint32_t ref) { // (its spawn point by Skyrim's reference: Morokei)
	for (int s = 0; s < spawn_count && ref; s++)
		if (spawns[s].ref == ref)
			return s;
	return -1;
}

int creatures_live_spawn(int i) {
	return i >= 0 && i < MAX_LIVE && live[i].used ? live[i].spawn : -1;
}

int creatures_spawn_where(int s, float out[3]) { // (a quest's foe by its spawn, up and awake: its feet)
	if (s < 0 || s >= spawn_count || spawns[s].kind < 0 || spawns[s].live < 0 || live[spawns[s].live].state == DEAD)
		return 0;
	memcpy(out, live[spawns[s].live].pos, sizeof(live[spawns[s].live].pos));
	return 1;
}

int creatures_spawn_body(int s, float out[3]) { // (a quest's foe by its spawn, alive or dead - its body still lying there: its feet)
	if (s < 0 || s >= spawn_count || spawns[s].kind < 0 || spawns[s].live < 0)
		return 0;
	memcpy(out, live[spawns[s].live].pos, sizeof(live[spawns[s].live].pos));
	return 1;
}

int creatures_spawn_alive(int s) { // (a quest's foe by its spawn: 1 alive - awake or asleep where it was - 0 dead, -1 gone)
	if (s < 0 || s >= spawn_count || spawns[s].kind < 0)
		return -1;
	if (spawns[s].dead)
		return 0;
	return spawns[s].live < 0 || live[spawns[s].live].state != DEAD;
}

void creatures_forget_spawn(int s) { // (a quest done with its foe: gone from the world)
	if (s < 0 || s >= spawn_count || !spawns[s].quest)
		return;
	if (spawns[s].live >= 0)
		sleep_creature(spawns[s].live);
	spawns[s].kind = -1, spawns[s].quest = 0;
}

void creatures_set_name(int i, const char *name) {
	if (i >= 0 && i < MAX_LIVE && live[i].used)
		live[i].name = name;
}

void creatures_set_floor(int i, float share) {
	if (i >= 0 && i < MAX_LIVE && live[i].used)
		live[i].floor_share = share;
}

float creatures_health_share(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return -1.0f;
	return live[i].state == DEAD ? 0.0f : live[i].health / fmaxf(live[i].max_health, 1.0f);
}

void creatures_dragon_wake(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return;
	Creature *c = &live[i];
	c->calm = 0, c->hostile = 1;
	if (c->dphase == D_PERCH) {
		dragon_start(c, D_TAKEOFF, "takeoff");
		c->land_at[1] = c->pos[1];
		sfx_play_at("dragon_roar", c->pos, 600.0f), c->roar_t = 6.0f;
		sfx_play_at("dragon_takeoff", c->pos, 200.0f);
	}
}

void creatures_dragon_away(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return;
	sfx_play_at("dragon_takeoff", live[i].pos, 300.0f);
	spawns[live[i].spawn].kind = -1;
	sleep_creature(i);
}

int creatures_by_ref(uint32_t ref) {
	for (int s = 0; s < spawn_count && ref; s++)
		if (spawns[s].ref == ref)
			return spawns[s].live;
	return -1;
}

void creatures_parley(int i, int on) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return;
	live[i].parley = on;
	if (on)
		live[i].hostile = 0, live[i].aware = 0.0f, live[i].state = IDLE;
}

void creatures_parley_go(int i, const float to[2], float seconds) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used)
		return;
	Creature *c = &live[i];
	c->parley = 1, c->parley_go = seconds, c->hostile = 0;
	c->state = WANDER, c->target[0] = to[0], c->target[1] = to[1], c->timer = seconds;
}

void creatures_turn_on(int i) {
	if (i < 0 || i >= MAX_LIVE || !live[i].used || live[i].state == DEAD)
		return;
	Creature *c = &live[i];
	c->parley = 0, c->hostile = 1, c->aware = 1.0f, c->state = CHASE, c->timer = 0.3f;
}

int creatures_hostile(int i) {
	return i >= 0 && i < MAX_LIVE && live[i].used && live[i].state != DEAD && live[i].hostile;
}

int creatures_body_side(uint32_t container_ref) {
	if ((container_ref & 0xFF000000u) != CORPSE_REF)
		return -1;
	const int s = (int)(container_ref & 0x00FFFFFFu);
	return s >= 0 && s < spawn_count ? spawns[s].side : -1;
}

void creatures_set_challenger(uint32_t ref) {
	challenger_ref = ref;
}
