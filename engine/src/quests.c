#include "quests.h"
#include "questkit.h"
#include "companions.h"
#include "college.h"
#include "thieves.h"
#include "wordfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "actors.h"
#include "brawl.h"
#include "creatures.h"
#include "crime.h"
#include "followers.h"
#include "music.h"
#include "dtree.h"
#include "equip.h"
#include "sfx.h"
#include "terrain.h"
#include "spellfx.h"
#include "shouts.h"
#include "topics.h"
#include "voice.h"
#include "fileio.h"
#include "furniture.h"
#include "navmesh.h"
#include "lighting.h"
#include "gamelog.h"
#include "interior.h"
#include "objects.h"
#include "intro.h"
#include "items.h"
#include "perks.h"
#include "locks.h"
#include "menu.h"
#include "races.h"
#include "services.h"
#include "skills.h"
#include "talk.h"
#include "ui.h"
#include "worldmap.h"
#include "worlds.h"
#include "gametext.h"

#define SOLAR_GUARD_REF 0x0006C0B5u // (the Embassy courtyard's Solar guard: Skyrim's reference - solar_update)

#define MAX_SCENES 384 // (the main quest's and the guilds')
#define SCENE_LINES 48 // (Alduin's Wall's at the Sleeping Giant: Skyrim's longest)
#define WATCHTOWER_M 140.0f // the dragon comes when you're this near the Western Watchtower
#define SANCTUM "bleakfallsbarrow02" // (Bleak Falls Barrow's last part: the Dragonstone's chest)

typedef struct {
	char key[48], speaker[48];
	int n;
	char *path[SCENE_LINES], *text[SCENE_LINES];
	char *who[SCENE_LINES]; // (a line's own speaker, for its subtitle - a scene of Skyrim's with many: NULL the scene's)
} Scene;

static Scene scenes[MAX_SCENES];
static int scene_count;
#define MAX_QITEMS 256
static struct { char name[40]; int kind, index; } qitems[MAX_QITEMS];
static int qitem_count;
static int stage[QUEST_COUNT];
static void late_reset(void); // (the last eight's: Elder Knowledge .. Dragonslayer)
static int bts_asked; // (Before the Storm talked over with Alvor / Gerdur: their question asked)
static int stone_given; // (this session: the Dragonstone put in a chest already)

// Places the later main quest happens at (Q lines: Skyrim's references)
#define MAX_QPLACES 512 // (the main quest's and the guilds')
static QPlace qplaces[MAX_QPLACES];
static int qplace_count;

static const QPlace *qplace(const char *key) {
	for (int i = 0; i < qplace_count; i++)
		if (!strcmp(qplaces[i].key, key))
			return &qplaces[i];
	return NULL;
}

// Whether its place is where you are: its interior, its land of its own ("@skuldafn": Skuldafn, Sovngarde,
// Dragonsreach's porch), or Tamriel's land outdoors
static int place_here(const QPlace *q, int interior) {
	if (q->cell[0] == '@')
		return interior < 0 && worlds_own_land() && !strcmp(worlds_prefix_of(worlds_current()), q->cell + 1);
	if (strcmp(q->cell, "-"))
		return interior >= 0 && interior == interior_find(q->cell);
	return interior < 0 && !worlds_own_land();
}

// Whether you're at it: in its place (place_here), within m
static int at_qplace(const char *key, const float feet[3], int interior, float m) {
	const QPlace *q = qplace(key);
	if (!q || !place_here(q, interior))
		return 0;
	return hypotf(q->pos[0] - feet[0], q->pos[2] - feet[2]) < m && fabsf(q->pos[1] - feet[1]) < (m > 40.0f ? m * 0.3f : 12.0f);
}

static const char *const NAMES[QUEST_COUNT] = {T_QUST_0004E50D_FULL0, T_QUST_000D0800_FULL0, T_QUST_0002610C_FULL0, "Unbound",
                                               T_QUST_000242BA_FULL0, T_QUST_000713DC_FULL0,
                                               T_QUST_00074A83_FULL0, "Uthgerd's Brawl", T_QUST_00039645_FULL0,
                                               T_QUST_00032926_FULL0, T_QUST_00035D5F_FULL0, T_QUST_00036191_FULL0,
                                               T_QUST_00036192_FULL0, T_QUST_0002D515_FULL0, T_QUST_0002D516_FULL0,
                                               T_QUST_00036193_FULL0, T_QUST_00044C56_FULL0, T_QUST_0002610A_FULL0, T_QUST_0003FA16_FULL0,
                                               T_QUST_00046EF0_FULL0, T_QUST_00046EF1_FULL0, T_QUST_00046EF2_FULL0};
#define HIGH_HROTHGAR "highhrothgar"
#define FAVOR_REWARD_MEDIUM 50 // (Skyrim's FavorRewardMedium: a favour's gold)
#define BRAWL_BET 100          // (Uthgerd's: INFO 00055A0D)
#define TOWER_WAIT_M 200.0f    // Irileth and her men put by the tower, waiting, once you're this near (out of sight)
#define TOWER_WAIT_OFF_M 24.0f // ... this far out from it, toward you
#define TOWER_MEN_M 90.0f      // ... her men by her once you're this near her (creatures further off are let go)
#define IRILETH_WATCH_M 80.0f  // Irileth faces a foe this near, ready (the dragon overhead)
#define TOWER_ARRIVE_M 45.0f   // Irileth and her men meet you this near the Western Watchtower (at its foot, as Skyrim's)
#define ESCORT_S 900.0f        // (her men: with you this long)
#define IRILETH_STRIKE_S 1.6f
#define IRILETH_REACH_M 3.0f
#define IRILETH_DAMAGE 18.0f
#define GREYBEARDS_HEARD_S 6.5f // (the call rolling over the hills: then INFO 0005EE3A)
#define GREYBEARDS_CALL_S 2.0f // the Greybeards' call this long after the men's talk of the Dragonborn

static int reopen_actor = -1;   // (someone to talk to again once the line's said)
static int brawl_pending = -1;  // (the brawl starts once Uthgerd's said her piece)
static int jarl_step;            // the Jarl: 1 seated on his throne, 2..5 over to Farengar with you and back
static int dr_step, wv_step;
static int di_step;  // Diplomatic Immunity: where in its scenes (Delphine's plan, Malborn, the stables)
static int di_satchel = -1; // ... Malborn's (items.h's container handle): what you gave him to smuggle in
enum { SOLAR_NONE, SOLAR_HALTING, SOLAR_ASKING, SOLAR_SENT_OFF, SOLAR_LEFT_BE };
static int solar_state, solar_guard = -1;
static int solar_closed; // (frames its menu's been shut without an answer: backed out of - walked away)
static int di_greeted; // (Elenwen's welcome said, this visit)
// The party's other distractions (Skyrim's MQ201Party): a friend among the guests asked (the one
// asked; whether they said yes - your persuasion won), Erikur's eye for Brelas (ER_*), the guests
// sent home once you've left it
enum { ER_NONE, ER_EYEING, ER_ASK_HER, ER_ASKED, ER_SCENE, ER_FREED, ER_NO };
static int er_step, friend_ok, guests_home;
#define WELCOME_AFTER_SHOUT_S 0.8f // (your Shout's voice done, then this: Arngeir's INFO 000649B5)
static float welcome_wait;
static int delphine_key_given; // (her secret door's key: given, this session)
static char friend_id[32];
static int move_pending, move_interior; // (a quest moving you: the carriage to the Embassy, behind black)
static float move_pos[3], move_yaw;
static int rat_step, wall_step, throat_step; // A Cornered Rat, Alduin's Wall, The Throat of the World: where in their scenes
static int escort_place = -2; // (Esbern with you: the place you were last - placed by you again on a new one)
static float storm_clear_t, throat_t; // (Clear Skies over the mountain path: seconds left)
static int paarthurnax_i = -1;
static int horn_step, blade_step, greet_step; // the Horn, A Blade in the Dark, the Greybeards' greeting: where in their scenes
static int alduin_i = -1, sahloknir_i = -1;  // (Kynesgrove's dragons: creatures' slots)
static float note_t, horn_t;    // Dragon Rising at the tower, the Way of the Voice: where in their scenes
static float greybeards_t = -1.0f, strike_t;
static int rumble; // (the Greybeards' "Dovahkiin!" just now: the view shakes)
static float heard_t; // (after it: the men's word on it)
static int irileth = -1;
// A scene with no one in sight to say it (the watchtower's men, Mirmulnir): its lines in turn
static const Scene *queued;
static int queued_i;
static char queued_title[48];

void quests_init(const char *pack_dir) {
	creatures_set_challenger(SOLAR_GUARD_REF); // (his challenge first, not the alarm)
	char path[256];
	snprintf(path, sizeof(path), "%squests.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size);
	if (!text)
		return;
	for (char *line = text, *end = text + size; line < end;) {
		char *nl = memchr(line, '\n', end - line);
		if (!nl)
			nl = end;
		*nl = 0;
		char *f[4] = {line, NULL, NULL, NULL};
		for (int k = 1; k < 4; k++) { // (the last field keeps its tabs-free rest)
			char *tab = f[k - 1] ? strchr(f[k - 1], '\t') : NULL;
			if (tab)
				*tab = 0, f[k] = tab + 1;
		}
		if (f[0][0] == 'S' && f[2] && scene_count < MAX_SCENES) {
			Scene *s = &scenes[scene_count++];
			snprintf(s->key, sizeof(s->key), "%s", f[1]);
			snprintf(s->speaker, sizeof(s->speaker), "%s", f[2]);
		} else if (f[0][0] == 'R' && f[3] && scene_count > 0) {
			Scene *s = &scenes[scene_count - 1];
			char *who = strchr(f[3], '\t'); // (its speaker, after the words: a scene of Skyrim's)
			if (who)
				*who++ = 0;
			if (s->n < SCENE_LINES && !strcmp(s->key, f[1]))
				s->who[s->n] = who && who[0] ? strdup(who) : NULL, s->path[s->n] = strdup(f[2]), s->text[s->n] = strdup(f[3]), s->n++;
		} else if (f[0][0] == 'Q' && f[3] && qplace_count < MAX_QPLACES) { // (a quest's place: Ustengrav's horn, Kynesgrove's mound)
			QPlace *q = &qplaces[qplace_count];
			snprintf(q->key, sizeof(q->key), "%s", f[1]);
			snprintf(q->cell, sizeof(q->cell), "%s", f[2]);
			if (sscanf(f[3], "%f\t%f\t%f\t%f", &q->pos[0], &q->pos[1], &q->pos[2], &q->heading) == 4)
				qplace_count++;
		} else if (f[0][0] == 'I' && f[3] && qitem_count < MAX_QITEMS) {
			snprintf(qitems[qitem_count].name, sizeof(qitems[0].name), "%s", f[1]);
			qitems[qitem_count].kind = f[2][0], qitems[qitem_count].index = atoi(f[3]);
			qitem_count++;
		}
		line = nl + 1;
	}
	free(text);
	game_log("quests: %d scenes, %d items, %d places", scene_count, qitem_count, qplace_count);
	if (scene_count >= MAX_SCENES || qitem_count >= MAX_QITEMS || qplace_count >= MAX_QPLACES)
		game_log("quests: FULL - some left out (raise MAX_*)"); // (they had been dropped without a word)
	dtree_init(pack_dir);
}

static const Scene *scene(const char *key) {
	for (int i = 0; i < scene_count; i++)
		if (!strcmp(scenes[i].key, key))
			return &scenes[i];
	return NULL;
}

static int qitem(const char *name, int *kind) {
	for (int i = 0; i < qitem_count; i++)
		if (!strcmp(qitems[i].name, name)) {
			if (kind)
				*kind = qitems[i].kind;
			return qitems[i].index;
		}
	return -1;
}

int quests_item(const char *name) {
	int kind = 0;
	const int index = qitem(name, &kind);
	return kind == 'I' ? index : -1;
}

static int actor_named(const char *id) {
	for (int i = 0; i < actors_count(); i++)
		if (!strcmp(actors_id(i), id))
			return i;
	return -1;
}

static int marker_named(const char *name, float out[2]) {
	for (int i = 0; i < worldmap_count(); i++) {
		int kind, disc, in_region;
		const char *n;
		float x, z;
		if (worldmap_marker(i, &kind, &n, &x, &z, &disc, &in_region) && !strcmp(n, name)) {
			out[0] = x, out[1] = z;
			return 1;
		}
	}
	// Places Skyrim's quests point at with no map marker of ours: a walled city (its own world -
	// its gate, the door in from Tamriel), the Western Watchtower (MQ104's FortCenterMarker,
	// 0xd907c: the quest's own target - Skyrim gives it no map marker). Without these the Jarl's
	// and Dragon Rising's objectives pointed nowhere: no compass mark, nothing for Clairvoyance
	if (interior_gate_of(name, out))
		return 1;
	if (!strcmp(name, T_LCTN_000D9079_FULL0)) {
		out[0] = 6.564f, out[1] = 209.019f; // (Skyrim's WhiterunWatchtowerSandboxMarker, 0xD907C)
		return 1;
	}
	return 0;
}

// Jarl Balgruuf on his throne by day (Skyrim's sits him in it: a line of theirs
// , as Farengar says). After Helgen's news, "Come, let's go find Farengar": up and over
// to his table with you, INFO 000D50F1; once Farengar's asked you
// for the tablet, INFO 000D8B72, and back to his seat
enum { JARL_FREE, JARL_SEATED, JARL_RISE, JARL_TO_FARENGAR, JARL_WAIT, JARL_BACK };
#define JARL_BESIDE_M 1.7f // (stops this far from Farengar)
#define JARL_HEAR_M 6.0f   // (says his piece once you're this near)
#define JARL_WALK_MAX_S 25.0f // (walking to Farengar this long - stuck on the stairs: he says it where he is)

static void play(const char *key);

static void jarl_seat(int j, int throne, const float near[3]) {
	float pos[3], heading;
	const char *in, *idle, *out;
	if (!furniture_seat(throne, near[0], near[2], pos, &heading, &in, &idle, &out))
		return;
	// (on the floor under the seat, as you sit: Skyrim's sitting clip lowers the body from
	// standing height - at the marker's own height he crouched on top of the throne)
	NavRef nr;
	float floor_y;
	const float fx = pos[0] - sinf(heading) * 0.9f, fz = pos[2] - cosf(heading) * 0.9f; // (the floor before it)
	if (navmesh_locate_level(fx, pos[1], fz, 1.5f, &nr, &floor_y) && floor_y < pos[1])
		pos[1] = floor_y;
	actors_script(j, 1), actors_script_place(j, pos, heading), actors_script_clip(j, idle, 0);
	furniture_set_taken(throne, 1);
	jarl_step = JARL_SEATED;
	game_log("quest: the Jarl on his throne (%.1f %.1f %.1f)", pos[0], pos[1], pos[2]);
}

static void set_stage(int q, int s, const char *objective);
// Farengar on the Dragonstone: Bleak Falls Barrow. The new objective - its notice and its sound - once he's done telling
// you, as Skyrim's: set as he began, both came and went under his first words (the user, Nov 10: no quest sound, no notice)
static int stone_told; // (he's telling you now: the objective when the talk's over)
static void project_told(void) {
	if (stone_told)
		return;
	play("mq103_farengar");
	stone_told = 1;
}

static void project_told_step(void) {
	if (!stone_told || queued != NULL || talk_speaking() >= 0 || topics_busy() || dtree_busy())
		return;
	stone_told = 0;
	if (stage[Q_BLEAK_FALLS_BARROW] == 10)
		set_stage(Q_BLEAK_FALLS_BARROW, 20, "Retrieve the Dragonstone from Bleak Falls Barrow");
}

static void jarl_update(const float feet[3], float dt) {
	const int j = actor_named("balgruufthegreater"), throne = furniture_find("Sit (Throne)");
	float at[3], fa[3];
	if (j < 0 || throne < 0 || !actors_shown(j, at)) { // (out of Dragonsreach: his day goes on)
		if (jarl_step != JARL_FREE && j >= 0)
			actors_script_release(j);
		if (throne >= 0)
			furniture_set_taken(throne, 0);
		jarl_step = JARL_FREE;
		return;
	}
	const float h = lighting_sky()->hours;
	const int day = h >= 8.0f && h < 20.0f;
	const int f = actor_named("farengarsecretfire");
	switch (jarl_step) {
	case JARL_FREE:
		if (day && !actors_scripted(j))
			jarl_seat(j, throne, at);
		break;
	case JARL_SEATED:
		if (!day && !quests_busy() && talk_speaking() != j)
			actors_script_release(j), furniture_set_taken(throne, 0), jarl_step = JARL_FREE;
		break;
	case JARL_RISE: // (his words said: up, and over)
		if (quests_busy())
			break;
		if (f < 0 || !actors_where(f, fa)) {
			jarl_step = JARL_BACK;
			break;
		}
		{
			const float dx = at[0] - fa[0], dz = at[2] - fa[2], d = fmaxf(hypotf(dx, dz), 0.01f);
			const float to[3] = {fa[0] + dx / d * JARL_BESIDE_M, fa[1], fa[2] + dz / d * JARL_BESIDE_M};
			actors_script(j, 1); // (his own day's walk set aside: he's the scene's - not seated, he wasn't)
			actors_script_clip(j, NULL, 0), actors_script_walk(j, to, 0);
			furniture_set_taken(throne, 0);
		}
		jarl_step = JARL_TO_FARENGAR;
		game_log("quest: the Jarl takes you to Farengar");
		break;
	case JARL_TO_FARENGAR: { // (there - or near enough, his walk stuck on the stairs: he speaks; you with him)
		static float walk_t;
		walk_t = actors_script_walking(j) ? walk_t + dt : 0.0f;
		const int there = f >= 0 && actors_where(f, fa) && hypotf(at[0] - fa[0], at[2] - fa[2]) < JARL_BESIDE_M + 1.2f;
		if ((actors_script_walking(j) && !there && walk_t < JARL_WALK_MAX_S) ||
		    hypotf(at[0] - feet[0], at[2] - feet[2]) > JARL_HEAR_M || quests_busy())
			break;
		walk_t = 0.0f;
		if (actors_script_walking(j)) // (stopped where he is: speaking)
			actors_script_walk(j, at, 0);
		if (f >= 0 && actors_where(f, fa)) {
			actors_script_face(j, atan2f(-(fa[0] - at[0]), -(fa[2] - at[2])));
			fa[1] += 1.6f;
			actors_script_look_at(j, fa);
		}
		play("mq103_jarl_intro"); // (INFO 000D50F1)
		jarl_step = JARL_WAIT;
		break;
	}
	case JARL_WAIT: // (beside him while Farengar tells you of the tablet)
		if (stage[Q_BLEAK_FALLS_BARROW] == 10 && !stone_told && !quests_busy() && f >= 0) {
			// (the Jarl's words said: Farengar takes it up - INFO 000D50E7 - as Skyrim's
			// scene has him, not waiting to be spoken to)
			if (actors_where(f, fa))
				actors_script_face(f, atan2f(-(feet[0] - fa[0]), -(feet[2] - fa[2])));
			project_told();
			break;
		}
		if (stage[Q_BLEAK_FALLS_BARROW] >= 20 && !quests_busy()) {
			play("mq103_jarl_leave"); // (INFO 000D8B72)
			jarl_step = JARL_BACK;
		}
		break;
	case JARL_BACK:
		if (quests_busy() || actors_script_walking(j))
			break;
		{
			float pos[3], heading;
			const char *in, *idle, *out;
			if (!furniture_seat(throne, at[0], at[2], pos, &heading, &in, &idle, &out)) {
				actors_script_release(j), jarl_step = JARL_FREE;
				break;
			}
			if (hypotf(pos[0] - at[0], pos[2] - at[2]) > 1.0f) {
				actors_script_look_at(j, NULL);
				actors_script_walk(j, pos, 0);
				break;
			}
			jarl_seat(j, throne, at);
		}
		break;
	}
}

// A stage reached: the journal's line, as Skyrim's notices
#define CAMPFIRE_HEARD_S 3.0f // (Bleak Falls Temple: the bandits by the fire heard this long in)
static int advances, main_advanced;
static Vitals *vitals_now; // (yours: the Companions' blood ritual changes you) // (stages reached, any quest's; the main quest's last - its marker or the latest's)

void qk_stage_notice(const char *quest, int was, int s, const char *objective) {
	char text[128];
	if (!was && s == 1 && !strcmp(quest, "Unbound")) { // (Skyrim's opening begins in black, no quest named: it shows once you're free)
		game_log("quest: %s stage %d (silent)", quest, s);
		advances++;
		return;
	}
	if (s >= QUEST_DONE)
		snprintf(text, sizeof(text), "Completed: %s", quest);
	else if (!was)
		snprintf(text, sizeof(text), "%s: %s", quest, objective);
	else
		snprintf(text, sizeof(text), "%s", objective);
	if (s >= QUEST_DONE) { // (Skyrim's reward sting; a new quest's, a new objective's sounds - heard as the notice
		// shows: a talk holds it back, and the sound with it)
		static const char *const STING[4] = {"mus_reward_01", "mus_reward_02", "mus_reward_03", "mus_reward_04"};
		ui_notice(text, 4.0f);
		music_special(STING[(unsigned)rand() % 4u]);
	} else {
		ui_notice_sound(text, 4.0f, !was ? "ui_quest_new" : "ui_objective");
	}
	game_log("quest: %s stage %d", quest, s);
	advances++;
}

int qk_advance_count(void) {
	return advances;
}

static void set_stage(int q, int s, const char *objective) {
	const int was = stage[q];
	stage[q] = s;
	qk_stage_notice(NAMES[q], was, s, objective);
	main_advanced = advances;
}

// Whose voice a scene of no one's is in (its first line's voice folder): the soldiers', a dragon's, the Greybeards'
static const char *voice_title(const char *v) { // (a line said by whoever's voice it was recorded in: by its voice)
	return !strncmp(v, "cruniquealduin", 14) ? T_NPC_00032B94_FULL0 : !strncmp(v, "crdragonvoice", 13) ? T_NPC_00032D9B_FULL0
	       : !strncmp(v, "maleuniquearngeir", 17) ? T_FACT_0002C6C8_FULL0 : !strncmp(v, "femaleuniqueelenwen", 19) ? T_NPC_00013269_FULL0
	       : !strncmp(v, "maledrunk", 9) ? T_NPC_000368C8_FULL0 : !strncmp(v, "maleuniquemgaugur", 17) ? T_NPC_0002BCE8_FULL0
	       : T_NPC_000215D5_FULL0; // (the Embassy's drunk: the party's scene; the Augur in his flame)
}

static void play(const char *key) {
	char women[48];
	snprintf(women, sizeof(women), "%s_f", key); // (a woman's version of a scene: INFO 000734B4)
	const Scene *s = actors_player_female() && scene(women) ? scene(women) : scene(key);
	const int tree = dtree_find(key); // (a conversation with no scene of its own: its speaker's)
	const int who = s ? actor_named(s->speaker) : tree >= 0 ? actor_named(dtree_speaker(tree)) : -1;
	float at[3];
	if (who >= 0 && actors_shown(who, at) && dtree_start(tree, who)) // (as a conversation: your answers to choose)
		return;
	if (!s && tree < 0) // (nothing of that name: quests.txt's has it not)
		game_log("quest: %s - no such scene or conversation", key);
	if (tree >= 0 && !s) // (a conversation with no one here to have it: said by no one - logged, it had been silent)
		game_log("quest: %s - %s not here to say it (%d)", key, dtree_speaker(tree), who);
	if (who >= 0 && actors_shown(who, at)) {
		talk_scene(who, (const char *const *)s->path, (const char *const *)s->text, s->n);
		return;
	}
	if (!s || !s->n)
		return;
	queued = s, queued_i = 0; // (said as voices about you: the next line when the last's done)
	snprintf(queued_title, sizeof(queued_title), "%s", !strcmp(s->speaker, "mq104dragon") ? T_NPC_0001CA05_FULL0
	         : !strcmp(s->speaker, "-") ? voice_title(s->n ? s->path[0] : "") : !strcmp(s->speaker, "ms13arvel") ? T_NPC_00039646_FULL0 : !strcmp(s->speaker, "paarthurnax") ? T_QUST_0003FA16_FULL0 : who >= 0 ? talk_name(who) : s->speaker);
	if (queued_title[0] >= 'a' && queued_title[0] <= 'z') // (a dragon's own id, Odahviing's: as a name)
		queued_title[0] = (char)(queued_title[0] - 'a' + 'A');
}

static void play_queued(void) {
	if (!queued || (queued_i > 0 && voice_playing()))
		return;
	if (queued_i >= queued->n) {
		queued = NULL;
		ui_subtitle(NULL, NULL); // (said: its words off the screen)
		return;
	}
	voice_play(queued->path[queued_i]);
	ui_subtitle(queued->who[queued_i] ? queued->who[queued_i] : !strcmp(queued->speaker, "-") ? voice_title(queued->path[queued_i]) : queued_title,
	            queued->text[queued_i]);
	queued_i++;
}

int quests_take_rumble(void) {
	const int r = rumble;
	rumble = 0;
	return r;
}

void quests_voice(const char *key) {
	play(key);
}

int quests_busy(void) {
	return queued != NULL || talk_speaking() >= 0 || topics_busy() || dtree_busy();
}

int quests_stage(int q) {
	return q >= 0 && q < QUEST_COUNT ? stage[q] : 0;
}

// A reward from a leveled list (the armour of the skill you're better at, as Skyrim's)
static void reward(const char *heavy, const char *light) {
	const SkillState *sk = skills_state();
	const char *which = sk->level[SK_HEAVY_ARMOR] > sk->level[SK_LIGHT_ARMOR] ? heavy : light;
	int kind = 0, first = -1, got = 0;
	const int gold_before = items_gold();
	// (a leveled list: rolled again if it came up empty - a chance of none, nothing at your level -
	// then the other armour's list; once given nothing at all and nothing said of it)
	for (int t = 0; t < 12 && !got; t++) {
		const int index = qitem(t < 8 ? which : which == heavy ? light : heavy, &kind);
		got = index >= 0 && items_give_rolled(kind, index, 1, &first) && first >= 0;
	}
	if (got) {
		char text[96];
		if (items_gold() > gold_before) // (a purse: how much)
			snprintf(text, sizeof(text), "%d Gold added", items_gold() - gold_before);
		else
			snprintf(text, sizeof(text), "%s added", items_name(first));
		ui_notice(text, 4.0f);
		sfx_play("ui_item_get", 0.8f, 0.0f);
		game_log("quest reward: %s", items_name(first));
	} else {
		game_log("quest reward: nothing from %s / %s at level %d", heavy, light, sk->player_level);
	}
}

static const char *objective(int q) {
	switch (q) {
	case Q_UNBOUND: // (the opening, intro.c: 11/21 with Ralof, 12/22 with Hadvar)
		return stage[q] == 1 ? "Survive Helgen" : stage[q] == 10 ? "Escape Helgen: follow Ralof or Hadvar into the keep"
		     : stage[q] == 11 ? "Follow Ralof" : stage[q] == 12 ? "Follow Hadvar"
		     : stage[q] == 21 ? "Escape Helgen Keep with Ralof" : stage[q] == 22 ? "Escape Helgen Keep with Hadvar" : "";
	case Q_BEFORE_THE_STORM:
		return stage[q] == 5 ? "Go with Ralof to Riverwood" : stage[q] == 6 ? "Go with Hadvar to Riverwood"
		     : stage[q] == 7 ? "Talk to Gerdur" : stage[q] == 8 ? "Talk to Alvor"
		     : stage[q] == 10 ? "Speak to Jarl Balgruuf in Dragonsreach" : "";
	case Q_BLEAK_FALLS_BARROW:
		return stage[q] == 10 ? "Speak to Farengar" : stage[q] == 20 ? "Retrieve the Dragonstone from Bleak Falls Barrow"
		     : stage[q] == 30 ? T_QUST_000D0800_NNAM2 : "";
	case Q_DRAGON_RISING:
		return stage[q] == 10 ? "Speak to Jarl Balgruuf" : stage[q] == 20 ? "Go to the Western Watchtower"
		     : stage[q] == 30 ? T_QUST_0002610C_NNAM2 : stage[q] == 40 ? T_QUST_0002610C_NNAM4 : "";
	case Q_WAY_OF_THE_VOICE:
		return stage[q] == 10 ? "Travel to High Hrothgar" : stage[q] == 20 ? T_QUST_000242BA_NNAM2
		     : stage[q] == 30 ? "Shout at the Greybeards (Unrelenting Force)"
		     : stage[q] == 40 ? "Learn the Word of Power from Master Borri" : "";
	case Q_HORN:
		return stage[q] == 10 ? "Retrieve the Horn of Jurgen Windcaller from Ustengrav" : stage[q] == 20 ? T_QUST_000713DC_NNAM1
		     : stage[q] == 30 ? "Meet with whoever took the horn (the Sleeping Giant Inn, Riverwood)"
		     : stage[q] == 40 ? T_QUST_000713DC_NNAM3 : "";
	case Q_BLADE:
		return stage[q] == 10 ? T_QUST_00032926_NNAM0 : stage[q] == 20 ? "Locate the dragon burial site near Kynesgrove"
		     : stage[q] == 30 ? T_QUST_00032926_NNAM2 : stage[q] == 40 ? T_QUST_00032926_NNAM0 : "";
	case Q_DIPLOMATIC:
		return stage[q] == 5 ? T_QUST_00035D5F_NNAM0 : stage[q] == 10 ? T_QUST_00035D5F_NNAM1
		     : stage[q] == 20 ? T_QUST_00035D5F_NNAM2 : stage[q] == 30 ? T_QUST_00035D5F_NNAM3
		     : stage[q] == 40 ? T_QUST_00035D5F_NNAM4 : stage[q] == 50 ? T_QUST_00035D5F_NNAM5
		     : stage[q] == 55 ? "Follow Malborn" : stage[q] == 60 ? T_QUST_00035D5F_NNAM7
		     : stage[q] == 70 ? T_QUST_00035D5F_NNAM8 : stage[q] == 80 ? T_QUST_00035D5F_NNAM9
		     : stage[q] == 90 ? T_QUST_00032926_NNAM0 : "";
	case Q_RAT: // (Skyrim's MQ202 objectives)
		return stage[q] == 10 ? T_QUST_0002154E_NNAM2 : stage[q] == 20 ? T_QUST_00036191_NNAM1
		     : stage[q] == 30 ? T_QUST_00036191_NNAM2 : stage[q] == 40 ? T_QUST_00036191_NNAM3 : "";
	case Q_WALL: // (MQ203's)
		return stage[q] == 10 ? T_QUST_00036192_NNAM0 : stage[q] == 20 ? T_QUST_00036191_NNAM3
		     : stage[q] == 30 ? T_QUST_00036192_NNAM2 : stage[q] == 40 ? T_QUST_00036192_NNAM3 : "";
	case Q_THROAT: // (MQ204's)
		return stage[q] == 10 ? T_QUST_0002D515_NNAM0 : stage[q] == 20 ? T_QUST_0002D515_NNAM1
		     : stage[q] == 25 ? T_QUST_0002D515_NNAM2 : stage[q] == 30 ? T_QUST_0002D515_NNAM3
		     : stage[q] == 40 ? T_QUST_0002D515_NNAM4
		     : stage[q] == 50 ? T_QUST_0002D515_NNAM5 : stage[q] == 60 ? T_QUST_0002D515_NNAM3 : "";
	case Q_ELDER: // (MQ205's, with Discerning the Transmundane's Septimus between)
		return stage[q] == 10 ? T_QUST_0002D516_NNAM0 : stage[q] == 20 ? T_QUST_0002D512_NNAM1
		     : stage[q] == 50 ? T_QUST_0002D516_NNAM3 : "";
	case Q_BANE: // (MQ206's)
		return stage[q] == 10 ? T_QUST_00036193_NNAM0
		     : stage[q] == 20 ? T_QUST_00036193_NNAM1 : stage[q] == 30 ? T_QUST_00036193_NNAM2 : "";
	case Q_FALLEN: // (MQ301's)
		return stage[q] == 10 ? T_QUST_0002D515_NNAM3 : stage[q] == 20 ? T_QUST_00044C56_NNAM3
		     : stage[q] == 25 ? T_QUST_00044C56_NNAM4 : stage[q] == 30 ? T_QUST_00044C56_NNAM5
		     : stage[q] == 40 ? T_QUST_00044C56_NNAM6 : stage[q] == 50 ? T_QUST_00044C56_NNAM7
		     : stage[q] == 60 ? T_QUST_00044C56_NNAM8 : stage[q] == 70 ? T_QUST_00044C56_NNAM9 : "";
	case Q_SEASON: // (MQ302's)
		return stage[q] == 10 ? T_QUST_0002D515_NNAM0 : stage[q] == 20 ? T_QUST_00045923_NNAM1
		     : stage[q] == 30 ? T_QUST_00045923_NNAM2 : stage[q] == 40 ? T_QUST_0002D515_NNAM0
		     : stage[q] == 50 ? T_QUST_00045923_NNAM4 : stage[q] == 60 ? T_QUST_00045923_NNAM5 : "";
	case Q_PAARTHURNAX: // (MQPaarthurnax's)
		return stage[q] == 10 ? T_QUST_0003FA16_NNAM0 : stage[q] == 20 ? T_QUST_00032926_NNAM0 : "";
	case Q_EYRIE: // (MQ303's)
		return stage[q] == 10 ? T_QUST_00046EF0_NNAM0 : stage[q] == 20 ? T_QUST_00046EF0_NNAM1
		     : stage[q] == 30 ? T_QUST_00046EF0_NNAM2 : stage[q] == 40 ? T_QUST_00046EF0_NNAM3 : "";
	case Q_SOVNGARDE: // (MQ304's)
		return stage[q] == 10 ? T_QUST_00046EF1_NNAM0 : stage[q] == 20 ? T_QUST_00046EF1_NNAM1
		     : stage[q] == 30 ? T_QUST_00046EF1_NNAM2 : "";
	case Q_DRAGONSLAYER: // (MQ305's, and MQ306's way home)
		return stage[q] == 10 ? T_QUST_00046EF2_NNAM0 : stage[q] == 20 ? T_QUST_00036193_NNAM2
		     : stage[q] == 30 ? T_QUST_00046EF3_NNAM0 : "";
	case Q_LOVELY_LETTER:
		return stage[q] == 10 ? T_QUST_00074A83_NNAM2 : stage[q] == 20 ? T_QUST_00074A83_NNAM0
		     : stage[q] == 30 ? T_QUST_00074A83_NNAM5 : stage[q] == 40 ? T_QUST_00074A83_NNAM4 : "";
	case Q_BRAWL:
		return stage[q] == 10 ? "Beat Uthgerd in a brawl" : stage[q] == 20 ? "Collect your winnings from Uthgerd" : "";
	case Q_GOLDEN_CLAW: // (Skyrim's MS13 objectives: 12 - heard of it from the bandits, or Arvel - and 35, found without Lucan)
		return stage[q] == 10 || stage[q] == 12 ? T_QUST_00039645_NNAM0 : stage[q] == 20 ? T_QUST_00039645_NNAM4
		     : stage[q] == 35 ? T_QUST_00039645_NNAM3 : "";
	}
	return "";
}

void quests_new_game(int opening) {
	memset(stage, 0, sizeof(stage));
	dr_step = wv_step = 0, greybeards_t = -1.0f, reopen_actor = brawl_pending = -1, queued = NULL, welcome_wait = 0.0f;
	horn_step = blade_step = greet_step = 0, alduin_i = sahloknir_i = -1;
	di_step = 0, di_satchel = -1, di_greeted = 0, move_pending = 0, solar_state = SOLAR_NONE, solar_guard = -1;
	er_step = ER_NONE, friend_ok = 0, guests_home = 0, friend_id[0] = 0, delphine_key_given = 0;
	stone_given = 0;
	rat_step = wall_step = throat_step = 0, escort_place = -2, storm_clear_t = throat_t = 0.0f, paarthurnax_i = -1;
	late_reset();
	companions_new_game();
	college_new_game();
	thieves_new_game();
	if (!opening) // (without Helgen: straight to the Jarl)
		set_stage(Q_BEFORE_THE_STORM, 10, "Speak to Jarl Balgruuf in Dragonsreach");
}

void quests_set(int q, int s) {
	if (q < 0 || q >= QUEST_COUNT || stage[q] == s)
		return;
	const int was = stage[q];
	stage[q] = s;
	const char *text = objective(q);
	stage[q] = was;
	set_stage(q, s, text);
}

void quests_save(int stages[16], int more[16]) {
	memset(stages, 0, 16 * sizeof(int));
	memset(more, 0, 16 * sizeof(int));
	memcpy(stages, stage, QUESTS_IN_FIRST * sizeof(int));
	memcpy(more, stage + QUESTS_IN_FIRST, (QUEST_COUNT - QUESTS_IN_FIRST) * sizeof(int));
}

void quests_load(const int stages[16], const int more[16]) {
	memcpy(stage, stages, QUESTS_IN_FIRST * sizeof(int));
	memcpy(stage + QUESTS_IN_FIRST, more, (QUEST_COUNT - QUESTS_IN_FIRST) * sizeof(int));
	for (int q = QUESTS_IN_FIRST; q < QUEST_COUNT; q++)
		if (stage[q] < 0 || stage[q] > QUEST_DONE)
			stage[q] = 0;
	late_reset();
	if (stage[Q_THROAT] >= QUEST_DONE && !stage[Q_ELDER]) // (finished the mountain before Elder Knowledge was)
		stage[Q_ELDER] = 10;
	rat_step = wall_step = throat_step = 0, escort_place = -2, storm_clear_t = throat_t = 0.0f, paarthurnax_i = -1;
	for (int q = Q_RAT; q <= Q_THROAT; q++) // (saved before these were: the slots held nothing)
		if (stage[q] < 0 || stage[q] > QUEST_DONE)
			stage[q] = 0;
	if (stage[Q_DIPLOMATIC] >= QUEST_DONE && !stage[Q_RAT])
		stage[Q_RAT] = 10;
	// (saved mid-scene on the mountain: the scene again, or on from where its effect is)
	throat_step = stage[Q_THROAT] == 20 ? 2 : stage[Q_THROAT] == 50 ? 8 : stage[Q_THROAT] == 60 ? 9 : 0;
	if (stage[Q_THROAT] == 40)
		stage[Q_THROAT] = 30;
	dr_step = wv_step = 0, greybeards_t = -1.0f, reopen_actor = brawl_pending = -1, queued = NULL, welcome_wait = 0.0f;
	if (irileth >= 0)
		actors_script_release(irileth), irileth = -1;
	for (int q = 0; q < QUEST_COUNT; q++) // (a quest's stage the save can't have reached mid-scene: back to its start)
		if (q == Q_DRAGON_RISING && stage[q] == 30)
			stage[q] = 20; // (the dragon wasn't saved: it comes again)
	stone_given = 0;
	int any = 0;
	for (int q = 0; q < QUEST_COUNT; q++)
		any |= stage[q] != 0;
	if (!any) // (a save from before quests: they begin)
		stage[Q_BEFORE_THE_STORM] = 10;
	if (stage[Q_UNBOUND] > 0 && stage[Q_UNBOUND] < 20) // (saved in Helgen, before the keep: on at the Jarl's)
		stage[Q_UNBOUND] = 0, stage[Q_BEFORE_THE_STORM] = stage[Q_BEFORE_THE_STORM] ? stage[Q_BEFORE_THE_STORM] : 10;
}

static float last_feet[3]; // (where you were last: quests_update's)
static const float BOSS_AT[2] = {-102.396f, 6.179f}; // (Skyrim's LvlDraugrAmbushBossMale in Bleak Falls Sanctum, 0x9BCD6)
#define BOSS_NEAR_M 16.0f

static int actor_beside(const char *id, const float feet[3], float side_m) {
	const int a = actor_named(id);
	if (a < 0)
		return -1;
	actors_script(a, 1);
	const float at[3] = {feet[0] + side_m, feet[1], feet[2] + 1.0f};
	actors_script_place(a, at, 0.0f);
	actors_script_follow(a, 1);
	return a;
}

static void ally_fights_by(int ally, float dt, const float feet[3], float *timer);
static void ally_fights(int ally, float dt, const float feet[3]) {
	ally_fights_by(ally, dt, feet, &strike_t);
}

// Irileth in the fight: at your side, striking whatever's at her (as followers do)
static void ally_fights_by(int ally, float dt, const float feet[3], float *timer) {
	float at[3];
	if (ally < 0 || !actors_where(ally, at) || !creatures_hostile_near(feet, 40.0f) || (*timer -= dt) > 0.0f)
		return;
	*timer = IRILETH_STRIKE_S;
	const float eye[3] = {at[0], at[1] + 1.4f, at[2]}, ahead[3] = {0.0f, 0.0f, -1.0f};
	float foe[3];
	if (creatures_aim_assist(eye, ahead, -1.0f, IRILETH_REACH_M + 3.0f, 0.0f, foe)) {
		actors_script_face(ally, atan2f(-(foe[0] - at[0]), -(foe[2] - at[2])));
		actors_script_clip(ally, "w1h_power", 1);
		creatures_blame_player(0);
		const int struck = creatures_area(at, IRILETH_REACH_M + 2.0f, IRILETH_DAMAGE);
		creatures_blame_player(1);
		if (struck > 0)
			sfx_play_at("wpn_hit_blade", at, 30.0f);
	} else if (creatures_aim_assist(eye, ahead, -1.0f, IRILETH_WATCH_M, 0.0f, foe)) { // (the dragon in the air: faced,
		actors_script_face(ally, atan2f(-(foe[0] - at[0]), -(foe[2] - at[2]))); //  ready for it - she stood idle)
		actors_script_clip(ally, "w1h_idle", 0);
	} else {
		actors_script_clip(ally, "", 0);
	}
}

static void irileth_fights(float dt, const float feet[3]) {
	ally_fights(irileth, dt, feet);
}

// The Greybeards' trials: about you in a ring in the hall (Skyrim's INFO 000C4369), and the
// word each shouts onto the floor for you to take in
#define FLOOR_WORD_S 5.0f
#define HH_CENTRE_M 3.5f // (this near the hall's centre - Skyrim's meditation spot: the trial begins)
#define FLOOR_WORD_AHEAD_M 1.4f // (the word on the floor this far before you, toward the one who shouted it)
#define SWITCH_USTENGRAV 3 // (converter/esm.py SWITCHES_INSIDE: MQ105EnableDungeonMarker)
#define HORN_NEAR_M 5.0f  // (at the horn place in Ustengrav: the note)
#define BLADE_WAIT_M 150.0f // (this near Kynesgrove mound: Delphine waiting on the hill)
#define BLADE_MEET_M 8.0f   // ... this near her, or
#define BLADE_NEAR_M 30.0f  // ... this near the mound: Alduin there
#define GATHER_M 3.6f
static float floor_word[3], floor_word_t;
static const char *const GREYBEARDS[4] = {"arngeir", "einarth", "borri", "wulfgar"};

// (at their marks in the main hall for the trial - Skyrim's MQ105<name>Marker1..3: set; else
// about you in a ring, where you are; the teacher at his own spot, before you)
static void greybeards_gather(int set, const char *teacher, const float feet[3]) {
	int placed = 0;
	for (int k = 0; k < 4; k++) {
		const int g = actor_named(GREYBEARDS[k]);
		char key[32];
		const QPlace *q = NULL;
		if (teacher && !strcmp(GREYBEARDS[k], teacher)) {
			snprintf(key, sizeof(key), "hh_teach_%s", teacher);
			q = qplace(key);
		}
		for (int s = set; !q && s >= 1; s--) {
			snprintf(key, sizeof(key), "hh_%s%d", GREYBEARDS[k], s);
			q = qplace(key);
		}
		if (g < 0 || !q)
			continue;
		actors_script(g, 1);
		actors_script_place(g, q->pos, q->heading);
		actors_script_clip(g, NULL, 0);
		placed++;
	}
	if (placed) {
		game_log("quest: the Greybeards at their marks in the hall (trial %d)", set);
		return;
	}
	for (int k = 0; k < 4; k++) {
		const int g = actor_named(GREYBEARDS[k]);
		if (g < 0)
			continue;
		const float a = (float)k * 1.5708f + 0.785f;
		float at[3] = {feet[0] + cosf(a) * GATHER_M, feet[1], feet[2] + sinf(a) * GATHER_M};
		NavRef nr;
		float h;
		if (navmesh_locate_level(at[0], at[1], at[2], 3.0f, &nr, &h))
			at[1] = h;
		actors_script(g, 1);
		actors_script_place(g, at, atan2f(-(feet[0] - at[0]), -(feet[2] - at[2])));
		actors_script_clip(g, NULL, 0);
	}
	game_log("quest: the Greybeards gather about you");
}

static void greybeards_release(void) {
	for (int k = 0; k < 4; k++)
		actors_script_release(actor_named(GREYBEARDS[k]));
}

static void floor_word_at(const float feet[3], const char *who, const char *word) { // (on the floor between you and the one who shouted it)
	float g[3];
	const int a = actor_named(who);
	float dx = 0.0f, dz = -1.0f;
	if (a >= 0 && actors_where(a, g) && hypotf(g[0] - feet[0], g[2] - feet[2]) > 0.5f) {
		const float d = hypotf(g[0] - feet[0], g[2] - feet[2]);
		dx = (g[0] - feet[0]) / d, dz = (g[2] - feet[2]) / d;
	}
	floor_word[0] = feet[0] + dx * FLOOR_WORD_AHEAD_M, floor_word[1] = feet[1], floor_word[2] = feet[2] + dz * FLOOR_WORD_AHEAD_M;
	NavRef nr;
	float h;
	if (navmesh_locate_level(floor_word[0], feet[1] + 0.5f, floor_word[2], 1.5f, &nr, &h))
		floor_word[1] = h; // (on the floor itself, before you)
	floor_word_t = FLOOR_WORD_S;
	wordfx_show(word, floor_word, atan2f(dx, -dz), FLOOR_WORD_S); // (its letters - Skyrim's fxgreybeardshout<word> - facing you)
	music_special("mus_special_wordofpower_01");
	sfx_play("wordwall_glow", 0.8f, 0.0f);
}

// The Horn of Jurgen Windcaller: at its place in Ustengrav, a note where the horn should be;
// at the Sleeping Giant, the attic room asked for - Delphine, the horn, and Kynesgrove
static void horn_update(float dt, const float feet[3], int interior) {
	if (stage[Q_HORN] == 10 && at_qplace("ustengrav_horn", feet, interior, HORN_NEAR_M)) {
		const int note = qitem("horn_note", NULL);
		if (note >= 0)
			items_add(note, 1);
		static char text[400];
		char book[600];
		if (note >= 0 && items_book_text(note, book, sizeof(book)) && book[0]) { // (the note, read where it lies: its lines run on)
			int k = 0;
			for (const char *c = book; *c && k < (int)sizeof(text) - 1; c++) {
				const char ch = *c == '\n' || *c == '\f' ? ' ' : *c;
				if (ch == ' ' && (k == 0 || text[k - 1] == ' '))
					continue;
				text[k++] = ch;
			}
			text[k] = 0;
		} else {
			snprintf(text, sizeof(text), "The horn is gone. A note lies where it should be.");
		}
		ui_subtitle(note >= 0 ? items_name(note) : "A note", text);
		set_stage(Q_HORN, 20, T_QUST_000713DC_NNAM1);
		note_t = 9.0f;
	}
	if (stage[Q_HORN] == 20 && note_t > 0.0f && (note_t -= dt) <= 0.0f) {
		ui_subtitle(NULL, NULL);
		set_stage(Q_HORN, 30, "Meet with whoever took the horn (the Sleeping Giant Inn, Riverwood)");
	}
	// Delphine: once she's let you the room, to you - INFO 00032902 - and the horn
	const int d = actor_named("delphine");
	float at[3];
	if (horn_step == 1 && !quests_busy() && d >= 0 && actors_where(d, at)) {
		actors_script(d, 1);
		const float to[3] = {feet[0] + 1.2f, feet[1], feet[2]};
		actors_script_walk(d, to, 0);
		horn_t = 0.0f, horn_step = 2;
	} else if (horn_step == 2 && d >= 0 && actors_where(d, at)) {
		horn_t += dt;
		if ((hypotf(at[0] - feet[0], at[2] - feet[2]) < 2.6f || horn_t > 20.0f) && !quests_busy()) {
			actors_script_face(d, atan2f(-(feet[0] - at[0]), -(feet[2] - at[2])));
			play("mq106_intro"); // (INFO 00032902 INFO 000C65A2 ... "Kynesgrove.")
			const int horn = qitem("horn", NULL);
			if (horn >= 0 && !items_have(horn)) {
				items_add(horn, 1);
				char text[96];
				snprintf(text, sizeof(text), "%s added", items_name(horn));
				ui_notice(text, 3.0f);
			}
			horn_step = 3;
		}
	} else if (horn_step == 3 && !quests_busy()) {
		set_stage(Q_HORN, 40, T_QUST_000713DC_NNAM3);
		set_stage(Q_BLADE, 20, "Locate the dragon burial site near Kynesgrove");
		if (d >= 0)
			actors_script_release(d); // (off to get into her travelling gear)
		horn_step = 4;
	}
}

// A Blade in the Dark at Kynesgrove's mound: Delphine there before you, Alduin raising
// Sahloknir (INFO 000C8952), the fight; its soul yours - INFO 00061D73
static void blade_update(float dt, const float feet[3], int interior, int souls) {
	const int d = actor_named("delphine");
	if (stage[Q_BLADE] == 20 && !blade_step && at_qplace("kynesgrove_mound", feet, interior, BLADE_WAIT_M)) {
		const QPlace *hide = qplace("kynesgrove_delphine"); // (Delphine on the hill above it before you, waiting)
		if (d >= 0 && hide) {
			actors_script(d, 1);
			actors_script_place(d, hide->pos, hide->heading);
		}
		blade_step = 10;
	} else if (blade_step == 10) { // (up to her - or on to the mound without her: INFO 00046812)
		float at[3];
		const int by_her = d >= 0 && actors_shown(d, at) && hypotf(at[0] - feet[0], at[2] - feet[2]) < BLADE_MEET_M;
		if (!by_her && !at_qplace("kynesgrove_mound", feet, interior, BLADE_NEAR_M))
			return;
		const QPlace *land = qplace("kynesgrove_alduin");
		if (d >= 0)
			actors_script_follow(d, 1);
		if (land)
			alduin_i = creatures_place_dragon("alduin", land->pos, land->heading, 1);
		play("mq106_arrive"); // (INFO 00046812)
		blade_step = 1;
		game_log("quest: Kynesgrove - Alduin at the mound");
	} else if (blade_step == 1 && !quests_busy()) {
		play("mq106_alduin_raise"); // (INFO 000C8952)
		const QPlace *rise = qplace("kynesgrove_rise");
		if (rise) {
			sahloknir_i = creatures_place_dragon(NULL, rise->pos, rise->heading, 1); // (up out of the mound)
			creatures_set_name(sahloknir_i, T_NPC_00032D9B_FULL0);
			spellfx_burst((const float[3]){rise->pos[0], rise->pos[1] + 1.0f, rise->pos[2]}, ELEMENT_FIRE, 3.0f);
		}
		rumble = 1;
		blade_step = 2;
	} else if (blade_step == 2 && !quests_busy()) {
		play("mq106_sahloknir"); // (INFO 00035B4A)
		blade_step = 3;
	} else if (blade_step == 3 && !quests_busy()) {
		play("mq106_alduin_reply"); // (INFO 00035B4B INFO 000C8EA1)
		blade_step = 4;
	} else if (blade_step == 4 && !quests_busy()) {
		creatures_dragon_away(alduin_i), alduin_i = -1; // (Alduin away; Sahloknir up at you)
		creatures_dragon_wake(sahloknir_i);
		set_stage(Q_BLADE, 30, T_QUST_00032926_NNAM2);
		play("mq106_fight"); // (INFO 000CA631)
		blade_step = 5;
	}
	if (stage[Q_BLADE] == 30 && !blade_step && at_qplace("kynesgrove_mound", feet, interior, BLADE_WAIT_M)) {
		const QPlace *rise = qplace("kynesgrove_rise"); // (a game loaded mid-fight: Sahloknir up again)
		if (rise)
			sahloknir_i = creatures_place_dragon(NULL, rise->pos, rise->heading, 0);
			creatures_set_name(sahloknir_i, T_NPC_00032D9B_FULL0);
		blade_step = 5;
	}
	if (stage[Q_BLADE] == 30)
		ally_fights(d, dt, feet);
	if (stage[Q_BLADE] == 30 && souls > 0) {
		play("mq106_after"); // (INFO 00061D73)
		set_stage(Q_BLADE, QUEST_DONE, "");
		blade_step = 6;
	} else if (blade_step == 6 && !quests_busy()) {
		if (d >= 0)
			actors_script_release(d);
		blade_step = 7;
		if (!stage[Q_DIPLOMATIC]) // (INFO 0003B495: Diplomatic Immunity)
			set_stage(Q_DIPLOMATIC, 5, T_QUST_00035D5F_NNAM0);
	}
}

// Diplomatic Immunity (MQ201): Delphine's plan in her room at the Sleeping Giant, Malborn at the
// Winking Skeever (what you can't live without, smuggled in), Delphine at the Solitude stables
// with your invitation - your things left with her, the party clothes on - and the party at the
// Thalmor Embassy (Skyrim's MQ201 objectives 40-90): Elenwen's welcome, Malborn's bottle for
// Razelan, Razelan's scene, the kitchen and your gear, Elenwen's chest in her Solar (the Esbern
// dossier), Etienne in the dungeon, out through the Reeking Cave, back to Delphine in Riverwood
#define MALBORN_SATCHEL_REF 0xFB000201u // (his satchel's made-up reference: the gear he takes in)
#define DELPHINE_GEAR_REF 0xFB000202u   // (... and what you left with Delphine: MQ201PlayerContainerNoReset's)
#define DI_NEAR_M 4.0f
// (the Embassy's chests, by Skyrim's references: MQ201GearContainerREF, MQ201ElenwenOfficeContainerREF,
// the interrogation chamber's chest)
#define EMBASSY_GEAR_CHEST 0x00039CF1u
#define EMBASSY_OFFICE_CHEST 0x00039F1Cu
// The Solar's guard in the Embassy courtyard (MQ201ThalmorEmbassy's SolarGuard alias: his
// challenge, your persuasion - Skyrim's Speech against SpeechAverage (50), or an Amulet of Articulation
// worn - or walking away), and where, fooled, he goes: the party's door
#define SOLAR_CHALLENGE_M 6.0f
#define SOLAR_AGAIN_M 12.0f // (walked away from him: challenged again once you've been this far off)
static const float PARTY_DOOR_OUTSIDE[2] = {-1141.545f, -1635.374f}; // (Skyrim's thalmorembassyfrontdoor, 0x361EE)

int quests_take_move(int *interior, float pos[3], float *yaw_deg) {
	if (!move_pending)
		return 0;
	move_pending = 0;
	*interior = move_interior;
	memcpy(pos, move_pos, sizeof(move_pos));
	*yaw_deg = move_yaw;
	return 1;
}

static int di_near(const char *where, const float feet[3], int interior, float within) {
	const QPlace *q = qplace(where);
	return q && strcmp(q->cell, "-") != 0 && interior >= 0 && interior == interior_find(q->cell) &&
	       hypotf(feet[0] - q->pos[0], feet[2] - q->pos[2]) < within && fabsf(feet[1] - q->pos[1]) < 2.5f;
}

int quests_list_worn(const char *list) {
	const size_t n = strlen(list);
	for (int i = 0; i < qitem_count; i++) // (its things: "tgamuletofarticulationlist0", ...1)
		if (!strncmp(qitems[i].name, list, n) && qitems[i].name[n] >= '0' && qitems[i].name[n] <= '9' &&
		    qitems[i].kind == 'I' && equip_worn(qitems[i].index))
			return 1;
	return 0;
}

static int articulation_worn(void) { // (Skyrim's TGAmuletofArticulationList: any of them worn)
	return quests_list_worn("tgamuletofarticulationlist");
}

int quests_relationship(int actor) {
	if (actor < 0)
		return 0;
	const char *id = actors_id(actor);
	// (Skyrim's FavorJarlsMakeFriends: a Jarl whose Thane you are is your friend - Balgruuf, after Dragon Rising)
	if (!strcmp(id, "balgruufthegreater") && stage[Q_DRAGON_RISING] >= QUEST_DONE)
		return 1;
	return followers_is_friend(actor) ? 1 : 0;
}

// The Thalmor disguise (Skyrim's cut MQ201ThalmorEmbassyDisguiseScript, restored): a hood of its
// head list and a robe or armour of its body list worn, the Thalmor see through it only nearer
// than this (Skyrim units / 70: its fRangeHuman 1280, fRangeBeast 1600, fRangeElf 512; a High
// Elf never) - metres, or -1 not disguised
float quests_thalmor_disguise_m(void) {
	if (!quests_list_worn("mq201thalmordisguiseheaditems") || !quests_list_worn("mq201thalmordisguisebodyitems"))
		return -1.0f;
	const Race *r = races_get(races_player());
	const char *e = r ? r->eid : "";
	if (!strcmp(e, "HighElfRace"))
		return 0.0f;
	if (!strcmp(e, "NordRace") || !strcmp(e, "ImperialRace") || !strcmp(e, "BretonRace") || !strcmp(e, "RedguardRace"))
		return 1280.0f / 70.0f;
	if (!strcmp(e, "WoodElfRace") || !strcmp(e, "DarkElfRace"))
		return 512.0f / 70.0f;
	return 1600.0f / 70.0f; // (Orc, Khajiit, Argonian - and the script's own fallback)
}

// The Solar's guard: from the time you're out of the party till you're out of the Embassy -
// near him in the courtyard, his challenge (INFO 0006B1A7) and your
// answers (menu: quests_choice)
static void solar_update(const float feet[3], int interior) {
	const int st = stage[Q_DIPLOMATIC];
	const int g = creatures_by_ref(SOLAR_GUARD_REF);
	float at[3];
	if (g < 0 || !creatures_position(g, at) || creatures_hostile(g) || st < 55 || st >= 90 || interior >= 0) {
		if (solar_state == SOLAR_HALTING || solar_state == SOLAR_ASKING)
			solar_state = SOLAR_NONE;
		return;
	}
	const float d = hypotf(at[0] - feet[0], at[2] - feet[2]);
	if (solar_state == SOLAR_LEFT_BE && d > SOLAR_AGAIN_M)
		solar_state = SOLAR_NONE, creatures_parley(g, 0);
	if (solar_state == SOLAR_NONE && d < SOLAR_CHALLENGE_M && fabsf(at[1] - feet[1]) < 2.5f && !quests_busy() && !menu_is_open()) {
		solar_guard = g, solar_state = SOLAR_HALTING;
		creatures_parley(g, 1);
		topics_say("solar_halt", -1, creatures_title(g));
		game_log("quest: the Solar's guard stops you");
	} else if (solar_state == SOLAR_HALTING && !topics_busy() && !menu_is_open()) {
		static const char *const KEYS[2] = {"solar_trick", "solar_walk"};
		const char *opts[2];
		for (int i = 0; i < 2; i++)
			opts[i] = topics_prompt(KEYS[i])[0] ? topics_prompt(KEYS[i]) : KEYS[i];
		menu_open_dialogue(creatures_title(g), opts, 2);
		solar_state = SOLAR_ASKING, solar_closed = 0;
	} else if (solar_state == SOLAR_ASKING && !menu_is_open() && ++solar_closed > 3) {
		solar_state = SOLAR_LEFT_BE;
	}
}

int quests_choice(int choice) {
	if (solar_state != SOLAR_ASKING || choice == -1)
		return 0;
	const int g = solar_guard;
	const char *title = creatures_title(g);
	if (choice == 0) { // (DIAL 0006F167)
		if (speech_persuade(SPEECH_AVERAGE) || articulation_worn()) { // (Skyrim's: Speech against SpeechAverage)
			topics_say("solar_trick", -1, title); // (INFO 0006F176)
			creatures_parley_go(g, PARTY_DOOR_OUTSIDE, 9.0f);
			solar_state = SOLAR_SENT_OFF;
		} else {
			ui_notice("Persuasion failed", 3.0f);
			topics_say("solar_trick_fail", -1, title); // (INFO 0006F180)
			creatures_turn_on(g);
			solar_state = SOLAR_NONE;
		}
	} else { // (DIAL 0006F157 - INFO 0006F169)
		topics_say("solar_walk", -1, title);
		solar_state = SOLAR_LEFT_BE; // (at his post still: stopping you again if you come back)
	}
	return 1;
}

// Your things with Delphine (everything carried, gold too), the party clothes on and the
// invitation in hand; and the carriage's ride: into the Embassy's hall
static void di_to_party(void) {
	const int chest = items_open_container(DELPHINE_GEAR_REF, -1);
	int list[512];
	const int n = items_inventory(list, 512);
	for (int i = 0; i < n; i++)
		items_put(chest, list[i], items_have(list[i]));
	if (items_gold() > 0)
		items_put(chest, items_find_gold(), items_gold());
	equip_drop_uncarried();
	static const char *const WEAR[] = {"party_outfit", "party_boots"};
	for (int i = 0; i < 2; i++) {
		const int it = qitem(WEAR[i], NULL);
		if (it >= 0) {
			items_add(it, 1);
			if (!equip_worn(it))
				equip_toggle(it);
		}
	}
	const int inv = qitem("invitation", NULL);
	if (inv >= 0 && items_have(inv) <= 0)
		items_add(inv, 1);
	const QPlace *q = qplace("embassy_party");
	if (q && interior_find(q->cell) >= 0) {
		move_interior = interior_find(q->cell);
		memcpy(move_pos, q->pos, sizeof(move_pos));
		move_yaw = q->heading * 180.0f / (float)M_PI;
		move_pending = 1;
	}
	di_greeted = 0;
	game_log("quest: your things with Delphine (%d kinds); to the Embassy%s", n, q ? "" : " (no place for it)");
}
static void di_place(const char *who, const char *where, int interior) {
	const QPlace *q = qplace(where);
	const int i = actor_named(who);
	float at[3];
	const int inside = q && strcmp(q->cell, "-") != 0;
	if (!q || i < 0 || (inside && (interior < 0 || interior != interior_find(q->cell))) || (!inside && interior >= 0))
		return;
	if (actors_where(i, at) && hypotf(at[0] - q->pos[0], at[2] - q->pos[2]) < 1.5f && fabsf(at[1] - q->pos[1]) < 2.0f)
		return;
	actors_script(i, 1);
	actors_script_place(i, q->pos, q->heading);
	game_log("quest: %s placed at %s", who, where);
}

// The Embassy's doors as Skyrim's MQ201 opens them: the bar's back room and its door out
// (KitchenDoor, the party's exit - locked with no key) unlocked as Malborn takes you out (stage
// 55); after you're out, its front gate a lock to pick (SetLockLevel 50) and the barracks' easy (0)
#define EMBASSY_BACKROOM_DOOR 0x00033F0Fu
#define EMBASSY_KITCHEN_DOOR 0x00033F4Bu
#define EMBASSY_FRONT_GATE 0x0007DF48u
#define EMBASSY_BARRACKS_DOOR 0x00074069u
// Helgen's north gate: Skyrim's MQ101 closes it behind you and unlocks it at its last stage
// (1000: Unbound done - Fragment_19's Lock(false)); it had stayed locked with no key for good
#define HELGEN_NORTH_GATE 0x000A9D96u
static void helgen_gate(void) {
	const int l = stage[Q_UNBOUND] >= QUEST_DONE ? locks_find_ref(HELGEN_NORTH_GATE) : -1;
	if (l >= 0 && locks_locked(l))
		locks_unlock(l), game_log("quest: Helgen's north gate unlocked (Unbound done)");
}

// Delphine's secret door at the Sleeping Giant (her room below): Skyrim's MQ106 unlocks it as
// she takes you down (its stage 32: here, once A Blade in the Dark's begun) and gives you its key
// at its end (200) - it had stayed locked, her key nowhere
#define DELPHINE_SECRET_DOOR 0x00093BCDu
#define DELPHINE_SECRET_KEY 0x000990DFu
static void delphine_door(void) {
	const int l = stage[Q_BLADE] >= 10 ? locks_find_ref(DELPHINE_SECRET_DOOR) : -1;
	if (l >= 0 && locks_locked(l))
		locks_unlock(l), game_log("quest: Delphine's secret door unlocked (A Blade in the Dark)");
	const int key = items_by_form(DELPHINE_SECRET_KEY);
	if (stage[Q_BLADE] >= QUEST_DONE && !delphine_key_given && key >= 0) {
		if (items_have(key) <= 0) {
			items_add(key, 1);
			char text[96];
			snprintf(text, sizeof(text), "%s added", items_name(key));
			ui_notice(text, 3.0f);
		}
		delphine_key_given = 1;
	}
}

static void di_doors(int st) {
	if (st >= 55) {
		const uint32_t open[2] = {EMBASSY_BACKROOM_DOOR, EMBASSY_KITCHEN_DOOR};
		for (int i = 0; i < 2; i++) {
			const int l = locks_find_ref(open[i]);
			if (l >= 0 && locks_locked(l))
				locks_unlock(l), game_log("quest: the Embassy's door %x unlocked (Malborn's way out)", open[i]);
		}
	}
	if (st >= 90) {
		locks_set_level(locks_find_ref(EMBASSY_FRONT_GATE), 50);
		locks_set_level(locks_find_ref(EMBASSY_BARRACKS_DOOR), 1); // (Skyrim's 0: the easiest lock - Novice)
	}
}

// The party's guests (Skyrim's MQ201Party MoveToParty: the Jarls, the Emperor's cousin, Maven,
// Ondolemar, Erikur...) about the hall: Erikur at his marker, the bard at hers, the rest by
// Skyrim's sandbox markers, a step apart
static const char *const GUESTS[] = {"erikur", "generaltullius", "elisifthefair", "balgruufthegreater", "idgrodravencrone",
                                     "igmund", "siddgeir", "maven", "ondolemar", "orthusendario", "proventusavenicci",
                                     "vittoriavici", "illdi"};
#define GUEST_COUNT ((int)(sizeof(GUESTS) / sizeof(GUESTS[0])))
static void di_place_guest(int g, int interior) {
	static const char *const SPOTS[] = {"embassy_sandbox2", "embassy_sandbox3", "embassy_sandbox4"};
	const char *where = !strcmp(GUESTS[g], "erikur") ? "embassy_erikur" : !strcmp(GUESTS[g], "illdi") ? "embassy_bard" : SPOTS[g % 3];
	const QPlace *q = qplace(where);
	const int i = actor_named(GUESTS[g]);
	float at[3];
	if (!q || i < 0 || interior < 0 || interior != interior_find(q->cell))
		return;
	const float ang = (float)g * 2.39996f, r = (where == SPOTS[g % 3]) ? 1.4f : 0.0f;
	const float pos[3] = {q->pos[0] + cosf(ang) * r, q->pos[1], q->pos[2] + sinf(ang) * r};
	if (actors_where(i, at) && actors_scripted(i) && hypotf(at[0] - pos[0], at[2] - pos[2]) < 3.0f && fabsf(at[1] - pos[1]) < 2.0f)
		return;
	actors_script(i, 1);
	actors_script_place(i, pos, atan2f(q->pos[0] - pos[0], -(q->pos[2] - pos[2])) + (r > 0.0f ? 0.0f : q->heading));
}

static int di_friend_asked(void) { // (a friend's yes - or the persuasion that won one - just reached)
	return friend_ok;
}

static void di_update(const float feet[3], int interior) {
	(void)feet;
	const int st = stage[Q_DIPLOMATIC];
	if (st == 5 || st == 90)
		di_place("delphine", "riverwood_delphine_room", interior); // (waiting in her room)
	else if (st == 10 || st == 20)
		di_place("malborn", "skeever_malborn", interior);
	else if (st == 30)
		di_place("delphine", "solitude_stables", interior);
	if (st >= 40 && st <= 50) { // (the party: its host, its drunk, Malborn behind the bar, its guests)
		di_place("elenwen", "embassy_elenwen", interior);
		di_place("razelan", "embassy_razelan", interior);
		for (int g = 0; g < GUEST_COUNT; g++)
			di_place_guest(g, interior);
		if (er_step < ER_SCENE)
			di_place("brelas", "embassy_brelas", interior);
		const int er = actor_named("erikur"), br = actor_named("brelas");
		float ea[3], ba[3];
		if (er_step == ER_NONE && er >= 0 && br >= 0 && actors_where(er, ea) && actors_where(br, ba) && !quests_busy() &&
		    hypotf(ea[0] - feet[0], ea[2] - feet[2]) < 9.0f) { // (Erikur's eye on the serving girl - Skyrim's ErikurIntroScene)
			play("mq201_erikur_intro");
			er_step = ER_EYEING;
		}
		if (er_step == ER_ASKED && st == 50 && er >= 0 && actors_where(er, ea) && !quests_busy() &&
		    hypotf(ea[0] - feet[0], ea[2] - feet[2]) < 2.5f) { // (INFO 00069F30 - his, as you come up)
			play("mq201_erikur_back");
			er_step = ER_ASKED + 100; // (waiting on your answer: dtree_take_reached)
		}
		if (di_step < 13)
			di_place("malborn", "embassy_malborn", interior);
	} else if (st == 60) {
		di_place("malborn", "embassy_kitchen", interior);
		if (er_step == ER_SCENE)
			di_place("brelas", "embassy_torture", interior); // (taken to Rulindil: in the dungeon)
	} else if (st == 70) {
		di_place("etiennerarnis", "embassy_etienne", interior);
		if (er_step == ER_SCENE)
			di_place("brelas", "embassy_torture", interior);
	}
	if (st == 40 && !di_greeted && !quests_busy()) { // (Elenwen comes to greet a new face)
		const int e = actor_named("elenwen");
		float at[3];
		if (e >= 0 && actors_where(e, at) && interior >= 0 && hypotf(at[0] - feet[0], at[2] - feet[2]) < DI_NEAR_M &&
		    fabsf(at[1] - feet[1]) < 2.0f) {
			play("mq201_elenwen"); // (INFO 000410FB)
			di_greeted = 1;
		}
	}
	solar_update(feet, interior);
	di_doors(st);
	helgen_gate();
	delphine_door();
	if (st >= 55 && !guests_home) { // (out of the party: its guests home - Skyrim's MoveHome)
		for (int g = 0; g < GUEST_COUNT; g++) {
			const int i = actor_named(GUESTS[g]);
			if (i >= 0 && actors_scripted(i))
				actors_script_release(i);
		}
		guests_home = 1;
	}
	if (er_step == ER_ASKED + 100 && !quests_busy()) // (walked off without an answer: he'll ask again)
		er_step = ER_ASKED;
	if (st == 60) { // (Elenwen's chest: the dossier taken)
		const int dossier = qitem("esbern_dossier", NULL);
		if (dossier >= 0 && items_have(dossier) > 0)
			set_stage(Q_DIPLOMATIC, 70, T_QUST_00035D5F_NNAM8);
	} else if (st == 80 && interior < 0) { // (out of the Reeking Cave: Etienne off to Riften)
		const int et = actor_named("etiennerarnis");
		if (et >= 0)
			actors_script_release(et);
		set_stage(Q_DIPLOMATIC, 90, T_QUST_00032926_NNAM0);
	}
	if (di_step == 1 && !quests_busy()) {
		set_stage(Q_DIPLOMATIC, 10, T_QUST_00035D5F_NNAM1);
		const int d = actor_named("delphine");
		if (d >= 0)
			actors_script_release(d);
		di_step = 0;
	} else if (di_step == 2 && !quests_busy()) { // (his satchel: what you give him)
		if (di_satchel < 0)
			di_satchel = items_open_container(MALBORN_SATCHEL_REF, -1);
		set_stage(Q_DIPLOMATIC, 20, T_QUST_00035D5F_NNAM2);
		menu_open_container(di_satchel, T_NPC_00036194_FULL0);
		di_step = 3;
	} else if (di_step == 3 && !menu_is_open()) { // (the satchel closed: anything in it, he's off)
		int its[4], counts[4];
		if (items_in_container(di_satchel, its, counts, 4) > 0) {
			play("mq201_malborn_ready"); // (INFO 000361DF)
			set_stage(Q_DIPLOMATIC, 30, T_QUST_00035D5F_NNAM3);
			const int m = actor_named("malborn");
			if (m >= 0)
				actors_script_release(m);
		}
		di_step = 0;
	} else if (di_step == 4 && !quests_busy()) {
		const int d = actor_named("delphine");
		if (d >= 0)
			actors_script_release(d);
		di_to_party();
		set_stage(Q_DIPLOMATIC, 40, T_QUST_00035D5F_NNAM4);
		di_step = 0;
	} else if (di_step == 10 && !quests_busy()) { // (Malborn's bottle, for whoever's thirsty)
		set_stage(Q_DIPLOMATIC, 50, T_QUST_00035D5F_NNAM5);
		di_step = 0;
	} else if (di_step == 11 && !quests_busy()) { // (left before asking him: ask again)
		di_step = 0;
	} else if (di_step == 12 && !quests_busy()) { // (Razelan's: INFO 00063B53)
		play("mq201_distraction");
		di_step = 13;
	} else if (di_step == 40 && !quests_busy()) { // (a friend asked: their scene with Razelan, or no)
		if (di_friend_asked()) {
			char key[64];
			snprintf(key, sizeof(key), "mq201_friend_scene_%s", friend_id);
			play(key); // (Skyrim's DistractionFriendScene: your friend at Razelan, Elenwen's INFO 000665DA)
			game_log("quest: %s makes your distraction", friend_id);
			di_step = 41;
		} else {
			di_step = 0;
		}
		friend_ok = 0;
	} else if ((di_step == 41 || di_step == 42) && !quests_busy()) { // (the scene said: on with Malborn)
		di_step = 13;
	} else if (di_step == 43 && !quests_busy()) { // (Brelas, talked to: Erikur's to ask now)
		er_step = ER_ASKED, di_step = 0;
	} else if (di_step == 13 && !quests_busy()) { // (Malborn slips out to the kitchen: follow him)
		const int m = actor_named("malborn");
		const QPlace *q = qplace("embassy_kitchen");
		if (m >= 0 && q) {
			actors_script(m, 1);
			actors_script_walk(m, q->pos, 0);
		}
		set_stage(Q_DIPLOMATIC, 55, "Follow Malborn");
		di_step = 14;
	} else if (di_step == 14 && di_near("embassy_kitchen", feet, interior, DI_NEAR_M) && !quests_busy()) {
		play("mq201_kitchen"); // (INFO 000362C3 INFO 000362C6)
		set_stage(Q_DIPLOMATIC, 60, T_QUST_00035D5F_NNAM7);
		di_step = 0;
	} else if (di_step == 20 && !quests_busy()) { // (Etienne: with you, out the way the bodies go)
		const int et = actor_named("etiennerarnis");
		if (et >= 0)
			actors_script_follow(et, 1);
		set_stage(Q_DIPLOMATIC, 80, T_QUST_00035D5F_NNAM9);
		di_step = 0;
	} else if (di_step == 30 && !quests_busy()) { // (your things back from Delphine: on to Riften, for Esbern)
		const int chest = items_open_container(DELPHINE_GEAR_REF, -1);
		int its[512], counts[512];
		const int k = items_in_container(chest, its, counts, 512);
		for (int i = 0; i < k; i++)
			items_take(chest, its[i], counts[i]);
		if (k > 0)
			ui_notice("Your gear has been returned", 3.0f);
		const int d = actor_named("delphine");
		if (d >= 0)
			actors_script_release(d);
		set_stage(Q_DIPLOMATIC, QUEST_DONE, "");
		if (!stage[Q_RAT]) // ("You'd better get to Riften. Ask around for Esbern": A Cornered Rat)
			set_stage(Q_RAT, 10, T_QUST_0002154E_NNAM2);
		di_step = 0;
	}
}

// The horn back at High Hrothgar: Wulfgar's "Dah" on the floor, then the Greybeards' greeting
static void greet_update(float dt, const float feet[3]) {
	if (!greet_step || quests_busy() || floor_word_t > 0.0f)
		return;
	switch (greet_step) {
	case 1:
		greybeards_gather(3, "wulfgar", feet);
		play("mq105_dah"); // (Wulfgar: "Dah...")
		floor_word_at(feet, "wulfgar", "dah");
		greet_step = 2;
		break;
	case 2: {
		const char *w = shouts_learn_word("Dah");
		shouts_add_souls(1);
		shouts_unlock(SHOUT_FORCE);
		if (w)
			ui_notice(w, 4.0f);
		play("mq105_stand"); // (INFO 000C5228)
		greet_step = 3;
		break;
	}
	case 3:
		play("mq105_chant"); // (the Greybeards' Voice: INFO 000C43AF)
		rumble = 1;
		greet_step = 4;
		break;
	case 4:
		play("mq105_greeted"); // (INFO 0007E96D)
		greet_step = 5;
		break;
	case 5:
		greybeards_release();
		set_stage(Q_HORN, QUEST_DONE, "");
		greet_step = 0;
		break;
	}
	(void)dt;
}

// A Cornered Rat (MQ202): Brynjolf in Riften's market on the old man, Vekel at the Ragged Flagon
// (INFO 0006C861), Esbern through his door ("Go away!" - till Delphine's a line of theirs
// , or Dragonborn), his door unbarred, Esbern within. Then Alduin's Wall (MQ203): Esbern
// with you to the Sleeping Giant, Delphine and him in its cellar (Skyrim's MQ203RiverwoodScene),
// on to Karthspire, Sky Haven Temple's blood seal, Alduin's Wall and what it shows
#define ESBERN_DOOR_REF 0x0003B8B6u // (Skyrim's MQ202EsbernsDoorREF: barred from within)
#define RAT_NEAR_M 4.0f
#define KARTHSPIRE_MEET_M 90.0f // (Delphine and Esbern there, waiting, once you're this near: out of sight - their mark's 70 m from the map's)
#define SEAL_NEAR_M 4.0f
#define WALL_NEAR_M 7.0f

int quests_door(int lock) {
	if (lock < 0 || lock != locks_find_ref(ESBERN_DOOR_REF) || stage[Q_RAT] != 30 || rat_step)
		return 0;
	const int e = actor_named("esbern");
	float at[3];
	if (e < 0 || !actors_where(e, at))
		return 0;
	play("mq202_esbern_door"); // ("Go away!" - and your answers)
	rat_step = 3;
	return 1;
}

static void escort(const char *who, const char *also, int interior, const float feet[3]) { // (by you again, in a new place: they came along)
	if (interior == escort_place)
		return;
	escort_place = interior;
	if (actor_beside(who, feet, 1.2f) >= 0)
		game_log("quest: %s with you (%s)", who, interior >= 0 ? interior_title(interior) : "outdoors");
	if (also)
		actor_beside(also, feet, -1.2f);
}

static void rat_update(const float feet[3], int interior) {
	if (rat_step == 1 && !quests_busy()) {
		set_stage(Q_RAT, 20, T_QUST_00036191_NNAM1);
		rat_step = 0;
	} else if (rat_step == 2 && !quests_busy()) {
		set_stage(Q_RAT, 30, T_QUST_00036191_NNAM2);
		rat_step = 0;
	} else if (rat_step == 3 && !quests_busy()) { // (sent away: the door again)
		rat_step = 0;
	} else if (rat_step == 4 && !quests_busy()) { // (a line of theirs - his locks undone)
		play("mq202_open_door");
		rat_step = 5;
	} else if (rat_step == 5 && !quests_busy()) {
		locks_unlock(locks_find_ref(ESBERN_DOOR_REF));
		set_stage(Q_RAT, 40, T_QUST_00036191_NNAM3);
		rat_step = 0;
	} else if (rat_step == 6 && !quests_busy()) { // (INFO 00036360)
		set_stage(Q_RAT, QUEST_DONE, "");
		set_stage(Q_WALL, 10, T_QUST_00036192_NNAM0);
		escort_place = -2;
		rat_step = 7;
	}
	(void)feet, (void)interior;
}

static void stay(const char *who) { // (put somewhere by the scene: there, not after you)
	const int a = actor_named(who);
	if (a >= 0)
		actors_script_follow(a, 0);
}

static void wall_update(const float feet[3], int interior) {
	const int st = stage[Q_WALL];
	if (st == 10 || (st == 30 && wall_step >= 3 && wall_step < 5)) { // (Esbern - and Delphine, after - with you)
		escort("esbern", st == 30 ? "delphine" : NULL, interior, feet);
	}
	if (st == 10 && interior >= 0 && interior == interior_find("riverwoodsleepinggiantinn") && !wall_step) {
		di_place("esbern", "inn_esbern", interior); // (the cellar: Delphine's secret room - INFO 00037BDB)
		di_place("delphine", "inn_delphine", interior);
		stay("esbern"), stay("delphine");
		play("mq203_riverwood");
		wall_step = 1;
	} else if (st == 20 && wall_step < 2 && interior >= 0 && interior == interior_find("riverwoodsleepinggiantinn")) {
		di_place("esbern", "inn_esbern", interior); // (in the cellar still: a game loaded since had him home)
		stay("esbern");
	} else if (st == 10 && wall_step == 1 && !quests_busy()) {
		set_stage(Q_WALL, 20, T_QUST_00036191_NNAM3);
	} else if (st == 20 && wall_step == 2 && !quests_busy()) { // (INFO 0010323D)
		set_stage(Q_WALL, 30, T_QUST_00036192_NNAM2);
		const int d = actor_named("delphine"), e = actor_named("esbern");
		if (d >= 0)
			actors_script_release(d);
		if (e >= 0)
			actors_script_release(e);
		wall_step = 0;
	} else if (st == 30 && !wall_step && interior < 0) {
		const QPlace *w = qplace("karthspire_wait");
		if (w && hypotf(w->pos[0] - feet[0], w->pos[2] - feet[2]) < KARTHSPIRE_MEET_M) { // (there before you)
			const int d = actor_named("delphine"), e = actor_named("esbern");
			if (d >= 0)
				actors_script(d, 1), actors_script_place(d, w->pos, w->heading), actors_script_follow(d, 1);
			if (e >= 0) {
				const float by[3] = {w->pos[0] + 1.2f, w->pos[1], w->pos[2]};
				actors_script(e, 1), actors_script_place(e, by, w->heading), actors_script_follow(e, 1);
			}
			play("mq203_karthspire"); // (INFO 0010323B)
			escort_place = interior;
			wall_step = 3;
		}
	} else if (st == 30 && (wall_step == 3 || !wall_step) && at_qplace("skyhaven_seal", feet, interior, SEAL_NEAR_M)) {
		play("mq203_entry"); // (a line of theirs)
		wall_step = 4;
	} else if (st == 30 && wall_step == 4 && !quests_busy()) {
		ui_notice("Your blood drips onto the seal", 3.0f);
		sfx_play("door_open", 0.9f, 0.0f);
		set_stage(Q_WALL, 40, T_QUST_00036192_NNAM3);
		di_place("esbern", "skyhaven_wall_esbern", interior);
		di_place("delphine", "skyhaven_wall_delphine", interior);
		stay("esbern"), stay("delphine");
		wall_step = 5;
	} else if (st == 40 && (wall_step == 5 || !wall_step) && at_qplace("skyhaven_wall", feet, interior, WALL_NEAR_M)) {
		di_place("esbern", "skyhaven_wall_esbern", interior);
		di_place("delphine", "skyhaven_wall_delphine", interior);
		stay("esbern"), stay("delphine");
		play("mq203_wall"); // (INFO 00016F8A)
		wall_step = 51;
	} else if (st == 40 && wall_step == 51 && !quests_busy()) {
		wall_step = 6; // (Delphine: INFO 0002C2DA)
	} else if (st == 40 && wall_step == 7 && !quests_busy()) { // (DIAL 00016EE8)
		set_stage(Q_WALL, QUEST_DONE, "");
		set_stage(Q_THROAT, 10, T_QUST_0002D515_NNAM0);
		wall_step = 8;
	}
}

// The Throat of the World (MQ204): Arngeir (Dragonrend - INFO 0003C1A7), the
// Greybeards' Clear Skies in the courtyard (Lok, Vah, Koor), the storm on the path cleared by it,
// Paarthurnax at the summit - INFO 0003F88B - his Yol, your Fire Breath on him, and Dragonrend
#define STORM_REACH_M 30.0f    // the path's storm (Skyrim's MQ204 StormTarget and its cold): within this of it
#define STORM_DPS 12.0f        // ... its cold a second, not cleared
#define CLEAR_SKIES_S 30.0f    // ... cleared this long by Clear Skies shouted near it
#define SUMMIT_NEAR_M 45.0f    // Paarthurnax lands before you this near his perch
#define PAAR_HEAR_M 40.0f      // ... and hears your Thu'um this near

static void throat_update(float dt, const float feet[3], int interior) {
	const int st = stage[Q_THROAT];
	if (storm_clear_t > 0.0f)
		storm_clear_t -= dt;
	if (throat_step == 1 && !quests_busy()) { // (a line of theirs)
		set_stage(Q_THROAT, 20, T_QUST_0002D515_NNAM1);
		throat_step = 2;
	} else if (st == 20 && throat_step == 2 && interior < 0 && at_qplace("hh_courtyard", feet, interior, 20.0f)) {
		play("mq204_clear_skies"); // (INFO 000B3976 Lok, Vah, Koor)
		floor_word_at(feet, "arngeir", "lok");
		throat_step = 3;
	} else if (st == 20 && throat_step == 3 && !quests_busy()) {
		const char *words[3] = {"Lok", "Vah", "Koor"};
		for (int w = 0; w < 3; w++) {
			const char *got = shouts_learn_word(words[w]);
			shouts_add_souls(1); // (the Greybeards' own understanding: no soul of yours spent)
			shouts_unlock(SHOUT_CLEARSKIES);
			if (got)
				ui_notice(got, 4.0f);
		}
		game_log("quest: the Greybeards' gift - Clear Skies (%d words)", shouts_known(SHOUT_CLEARSKIES));
		set_stage(Q_THROAT, 25, T_QUST_0002D515_NNAM2);
		throat_step = 4;
	}
	// (the path's storm: its cold on you till Clear Skies blows it away, a while)
	const QPlace *storm = qplace("throat_storm");
	if (st > 0 && st < QUEST_DONE && interior < 0 && storm && storm_clear_t <= 0.0f &&
	    hypotf(storm->pos[0] - feet[0], storm->pos[2] - feet[2]) < STORM_REACH_M && fabsf(storm->pos[1] - feet[1]) < 20.0f) {
		creatures_poison_player(STORM_DPS, 1.0f);
		if ((throat_t -= dt) <= 0.0f)
			ui_notice("The freezing wind drives you back", 2.5f), throat_t = 5.0f;
	}
	const QPlace *perch = qplace("throat_paarthurnax");
	if (st == 30 && throat_step < 6 && interior < 0 && perch &&
	    hypotf(perch->pos[0] - feet[0], perch->pos[2] - feet[2]) < SUMMIT_NEAR_M) {
		const QPlace *on = qplace("throat_perch");
		paarthurnax_i = creatures_place_dragon("dragon", (on ? on : perch)->pos, (on ? on : perch)->heading, 1);
		creatures_set_name(paarthurnax_i, T_QUST_0003FA16_FULL0);
		play("mq204_paar_intro"); // (INFO 0003F88B ... INFO 00055989)
		throat_step = 6;
	} else if (st == 30 && throat_step == 6 && !quests_busy()) { // (INFO 000B89E0)
		set_stage(Q_THROAT, 40, T_QUST_0002D515_NNAM4);
		play("mq204_paar_yol");
		floor_word_at(feet, "-", "yol");
		throat_step = 7;
	} else if (st == 40 && throat_step == 7 && !quests_busy()) {
		const char *got = shouts_learn_word(shouts_known(SHOUT_FIRE) < 1 ? "Yol" : shouts_known(SHOUT_FIRE) < 2 ? "Toor" : "Shul");
		shouts_add_souls(1); // (his own understanding of it given you)
		shouts_unlock(SHOUT_FIRE);
		if (got)
			ui_notice(got, 4.0f);
		set_stage(Q_THROAT, 50, T_QUST_0002D515_NNAM5);
		throat_step = 8;
	} else if (throat_step == 9 && !quests_busy()) { // (his answer to your Thu'um: then Dragonrend)
		set_stage(Q_THROAT, 60, T_QUST_0002D515_NNAM3);
		play("mq204_dragonrend"); // (INFO 0003FA40 ... Tiid krent - the Elder Scroll)
		throat_step = 10;
	} else if (throat_step == 10 && !quests_busy()) {
		set_stage(Q_THROAT, QUEST_DONE, "");
		set_stage(Q_ELDER, 10, T_QUST_0002D516_NNAM0); // (INFO 0001C5FC)
		throat_step = 11;
	}
	if (st >= 30 && st < 60 && throat_step >= 6 && paarthurnax_i < 0 && perch && interior < 0 &&
	    hypotf(perch->pos[0] - feet[0], perch->pos[2] - feet[2]) < SUMMIT_NEAR_M) { // (a game loaded at the summit: him again)
		paarthurnax_i = creatures_place_dragon("dragon", perch->pos, perch->heading, 1);
		creatures_set_name(paarthurnax_i, T_QUST_0003FA16_FULL0);
	}
}

// The main quest's last eight, from the Elder Scroll to Sovngarde (Skyrim's MQ205 .. MQ306 and
// MQPaarthurnax): Urag and Septimus Signus, the Tower of Mzark; the Time-Wound's vision and Alduin
// driven off the mountain; Dragonsreach's trap and Odahviing, the council at High Hrothgar; the
// Blades' price; Skuldafn and its portal; Tsun's bridge, the Hall of Valor, Alduin's mist and his end
#define TIME_WOUND_M 8.0f        // the Elder Scroll read this near the Time-Wound
#define ALDUIN_FLEES_AT 0.5f     // Alduin on the mountain: driven off at this share of his health (he can't be slain there)
#define ODAHVIING_TRAPPED_AT 0.35f // Odahviing on the porch: down into the trap at this share
#define PORCH_TRAP_M 14.0f       // ... talked to this near the trap
#define PORCH_SHOUT_M 40.0f      // Call Dragon shouted this near the trap: he comes to it
#define SCROLL_TAKE_M 4.0f       // the Elder Scroll taken this near its place in the Tower of Mzark
#define PORTAL_M 6.0f            // Skuldafn's portal stepped into this near
#define TSUN_MEET_M 20.0f        // Tsun's challenge at the bridge, this near
#define HEROES_NEEDED 3          // Clear Skies shouted with the heroes this many times: the mist gone, Alduin down
#define URGE_S 25.0f
#define MOVE_SETTLE_S 1.5f       // after a quest's move: this long till its people and dragons are placed
#define VISION_S 190.0f          // the heroes of old seen this long (MQ206BattleScene said through)             // Paarthurnax's INFO 00098218 at most this often
static int letter_has(const char *name);
static void letter_take(const char *name, int count);
static int elder_step, bane_step, fallen_step, season_step, paar_step, eyrie_step, sov_step, slayer_step;
static int alduin_q = -1, odahviing_q = -1, mist_cleared;
static float urge_t, hero_strike[3], move_settle; // (move_settle: a quest's move just made - nothing placed till you're there)
static const char *const HEROES[3] = {"gormlaithgoldenhilt", "hakononeeye", "felldirtheold"};
static const char *const COUNCIL[][2] = {{"ulfric", "council_ulfric"}, {"generaltullius", "council_tullius"},
                                         {"elenwen", "council_elenwen"}, {"rikke", "council_rikke"},
                                         {"galmar", "council_galmar"}, {"elisifthefair", "council_elisif"},
                                         {"balgruufthegreater", "council_balgruuf"}, {"arngeir", "council_arngeir"},
                                         {"esbern", "council_esbern"}, {"delphine", "council_delphine"}};
static const char *const COUNCIL_SCENES[] = {"mq302_intro", "mq302_begin", "mq302_s1", "mq302_s2a", "mq302_s2b",
                                             "mq302_s4", "mq302_s5", "mq302_end", "mq302_outro"};
#define COUNCIL_COUNT ((int)(sizeof(COUNCIL) / sizeof(COUNCIL[0])))
#define COUNCIL_SCENE_COUNT ((int)(sizeof(COUNCIL_SCENES) / sizeof(COUNCIL_SCENES[0])))

static void late_reset(void) {
	elder_step = bane_step = fallen_step = season_step = paar_step = eyrie_step = sov_step = slayer_step = 0;
	alduin_q = odahviing_q = -1, mist_cleared = 0, urge_t = 0.0f;
}

static void learn_words(const char *const words[3], int shout, const char *from) { // (taught whole: no soul of yours spent)
	for (int w = 0; w < 3; w++) {
		const char *got = shouts_learn_word(words[w]);
		shouts_add_souls(1);
		shouts_unlock(shout);
		if (got)
			ui_notice(got, 4.0f);
	}
	game_log("quest: %s - %d words", from, shouts_known(shout));
}

static void move_to_place(const char *key) { // (a quest taking you there: Odahviing's flight, the portal, Tsun's)
	const QPlace *q = qplace(key);
	if (!q)
		return;
	const int w = q->cell[0] == '@' ? worlds_find(q->cell + 1) : 0;
	move_interior = q->cell[0] == '@' ? (w > 0 ? -1 - w : -1) : strcmp(q->cell, "-") ? interior_find(q->cell) : -1;
	memcpy(move_pos, q->pos, sizeof(move_pos));
	move_yaw = q->heading * 180.0f / (float)M_PI;
	move_pending = 1, move_settle = MOVE_SETTLE_S;
	game_log("quest: to %s (%s)", key, q->cell);
}

int quests_test_goto(const char *key) { // (testing: "qgo time_wound")
	if (!qplace(key))
		return 0;
	move_to_place(key);
	return 1;
}

static int summit_near(const float feet[3], int interior) { // (the Throat of the World's summit: Paarthurnax's)
	return at_qplace("throat_paarthurnax", feet, interior, SUMMIT_NEAR_M);
}

static void paarthurnax_at_summit(const float feet[3], int interior) { // (on his perch whenever you're up there)
	int begun = stage[Q_THROAT] >= QUEST_DONE; // (the mountain climbed, or the quests after it begun)
	for (int q = Q_ELDER; q <= Q_DRAGONSLAYER; q++)
		begun |= stage[q] > 0;
	if (!begun || stage[Q_PAARTHURNAX] >= 20 || !summit_near(feet, interior))
		return;
	if (paarthurnax_i >= 0 && creatures_health_share(paarthurnax_i) < 0.0f)
		paarthurnax_i = -1; // (his slot let go while you were away)
	if (paarthurnax_i >= 0)
		return;
	const QPlace *perch = qplace("throat_paarthurnax");
	paarthurnax_i = creatures_place_dragon("dragon", perch->pos, perch->heading, 1);
	creatures_set_name(paarthurnax_i, T_QUST_0003FA16_FULL0);
	creatures_set_floor(paarthurnax_i, stage[Q_PAARTHURNAX] == 10 ? 0.0f : 1.0f); // (not to be slain but by the Blades' price)
}

// Elder Knowledge (MQ205): Urag in the Arcanaeum - INFO 0003277F - Septimus in
// his outpost on the ice (Discerning the Transmundane's: the attunement sphere, the lexicon, a line of theirs
// ), the Elder Scroll in the Tower of Mzark, under Blackreach
static void elder_update(const float feet[3], int interior) {
	const int st = stage[Q_ELDER];
	if (elder_step == 1 && !quests_busy()) {
		set_stage(Q_ELDER, 20, T_QUST_0002D512_NNAM1);
		elder_step = 0;
	} else if (elder_step == 2 && !quests_busy()) { // (INFO 0001C6AD)
		static const char *const GIVEN[] = {"sphere", "lexicon"};
		for (int i = 0; i < 2; i++)
			if (!letter_has(GIVEN[i]))
				letter_take(GIVEN[i], 1);
		set_stage(Q_ELDER, 50, T_QUST_0002D516_NNAM3);
		elder_step = 0;
	} else if (st == 50 && at_qplace("mzark_scroll", feet, interior, SCROLL_TAKE_M)) {
		if (!letter_has("elder_scroll"))
			letter_take("elder_scroll", 1);
		set_stage(Q_ELDER, QUEST_DONE, "");
		set_stage(Q_BANE, 10, T_QUST_00036193_NNAM0);
	}
}

// Alduin's Bane (MQ206): the Elder Scroll read at the Time-Wound - the heroes' battle of old seen
// (MQ206BattleScene), Dragonrend learned from it - then Alduin come for you (MQ206PresentDayScene),
// brought down with Dragonrend and fought till he flees
static void bane_alduin(void) {
	const QPlace *a = qplace("alduin_approach"), *w = qplace("time_wound");
	if (!w)
		return;
	alduin_q = creatures_place_dragon("alduin", (a ? a : w)->pos, (a ? a : w)->heading, 0);
	creatures_set_name(alduin_q, T_NPC_00032B94_FULL0);
	creatures_set_floor(alduin_q, ALDUIN_FLEES_AT);
	creatures_dragon_wake(alduin_q);
	game_log("quest: Alduin on the mountain (slot %d)", alduin_q);
}

static void bane_update(float dt, const float feet[3], int interior) {
	const int st = stage[Q_BANE];
	if (st == 10 && !bane_step && at_qplace("time_wound", feet, interior, TIME_WOUND_M) && letter_has("elder_scroll")) {
		ui_notice("You read the Elder Scroll", 3.0f);
		play("mq206_vision"); // (Felldir, Gormlaith and Hakon against Alduin: INFO 0004422A)
		const QPlace *w = qplace("time_wound");
		for (int h = 0; h < 3 && w; h++) { // (the heroes of old seen at the Time-Wound, as spirits, while it lasts)
			const float a = w->heading + (h - 1) * 0.6f;
			const float at[3] = {w->pos[0] + sinf(a) * 7.0f, w->pos[1], w->pos[2] - cosf(a) * 7.0f};
			creatures_summon_spirit(shouts_summon(SHOUT_VALOR, h), at, VISION_S);
		}
		set_stage(Q_BANE, 20, T_QUST_00036193_NNAM1);
		rumble = 1;
		bane_step = 1;
	} else if (st == 10 && !bane_step && summit_near(feet, interior) && letter_has("elder_scroll") && !quests_busy()) {
		static int told;
		if (!told)
			play("mq206_paar_scroll"), told = 1; // (INFO 000C64EC)
	} else if (bane_step == 1 && !quests_busy()) {
		static const char *const WORDS[3] = {"Joor", "Zah", "Frul"};
		learn_words(WORDS, SHOUT_DRAGONREND, "the heroes' Dragonrend");
		bane_alduin();
		play("mq206_present"); // (INFO 00044C4F)
		set_stage(Q_BANE, 30, T_QUST_00036193_NNAM2);
		bane_step = 2, urge_t = URGE_S * 0.5f;
	} else if (bane_step == 2) {
		const float h = creatures_health_share(alduin_q);
		if (h >= 0.0f && h <= ALDUIN_FLEES_AT + 0.01f) { // (INFO 00044CED)
			play("mq206_alduin_flees");
			bane_step = 3;
		} else if ((urge_t -= dt) <= 0.0f && !quests_busy()) {
			play("mq206_dragonrend"); // (INFO 00044969)
			urge_t = URGE_S;
		}
	} else if (bane_step == 3 && !quests_busy()) {
		creatures_dragon_away(alduin_q), alduin_q = -1;
		set_stage(Q_BANE, QUEST_DONE, "");
		set_stage(Q_FALLEN, 10, T_QUST_0002D515_NNAM3);
		bane_step = 4;
	}
	if (st == 30 && !bane_step && summit_near(feet, interior)) { // (a game loaded mid-fight: Alduin again)
		bane_alduin();
		bane_step = 2;
	}
}

// The Fallen (MQ301): Paarthurnax - one of Alduin's allies could tell where he's gone; Dragonsreach,
// built to hold a dragon; the Jarl (no, not with the war on: Season Unending first); Paarthurnax's
// Call Dragon - Od Ah Viing; the Jarl's men at the trap; Odahviing called from the Great Porch,
// fought down into the yoke, and asked
static void odahviing_trapped(void) {
	const QPlace *t = qplace("porch_trap");
	if (!t)
		return;
	odahviing_q = creatures_place_dragon("dragon", t->pos, t->heading, 1);
	creatures_set_name(odahviing_q, T_NPC_00045920_FULL0);
	creatures_set_floor(odahviing_q, 1.0f); // (held fast: not to be slain in the yoke)
}

static void fallen_update(const float feet[3], int interior) {
	const int st = stage[Q_FALLEN];
	if (st == 10 && !fallen_step && summit_near(feet, interior) && !quests_busy()) {
		play("mq301_paar_intro"); // (INFO 00045919 ... Dragonsreach)
		fallen_step = 1;
	} else if (fallen_step == 1 && !quests_busy()) {
		set_stage(Q_FALLEN, 20, T_QUST_00044C56_NNAM3);
		fallen_step = 0;
	} else if (fallen_step == 2 && !quests_busy()) { // (INFO 00045D93)
		set_stage(Q_FALLEN, 25, T_QUST_00044C56_NNAM4);
		if (!stage[Q_SEASON])
			set_stage(Q_SEASON, 10, T_QUST_0002D515_NNAM0);
		fallen_step = 0;
	} else if (st == 25 && stage[Q_SEASON] >= QUEST_DONE) {
		set_stage(Q_FALLEN, 30, T_QUST_00044C56_NNAM5);
	} else if (st == 30 && !fallen_step && summit_near(feet, interior) && !quests_busy()) {
		play("mq301_paar_call"); // (INFO 000D2CEF)
		fallen_step = 3;
	} else if (fallen_step == 3 && !quests_busy()) {
		static const char *const WORDS[3] = {"Od", "Ah", "Viing"};
		learn_words(WORDS, SHOUT_CALLDRAGON, "Paarthurnax's Call Dragon");
		set_stage(Q_FALLEN, 40, T_QUST_00044C56_NNAM6);
		fallen_step = 0;
	} else if (fallen_step == 4 && !quests_busy()) { // (INFO 000D2540)
		set_stage(Q_FALLEN, 50, T_QUST_00044C56_NNAM7);
		fallen_step = 0;
	} else if (st == 60 && fallen_step == 5) {
		const float h = creatures_health_share(odahviing_q);
		if (h >= 0.0f && h <= ODAHVIING_TRAPPED_AT + 0.01f) { // (down on the porch, into the yoke: "Got him!")
			creatures_dragon_away(odahviing_q);
			odahviing_trapped();
			play("mq301_trapped");
			set_stage(Q_FALLEN, 70, T_QUST_00044C56_NNAM9);
			fallen_step = 6;
		}
	} else if (st == 70 && (fallen_step == 6 || !fallen_step) && at_qplace("porch_trap", feet, interior, PORCH_TRAP_M) && !quests_busy()) {
		play("mq301_odahviing"); // (INFO 00048F05)
		fallen_step = 7;
	} else if (fallen_step == 7 && !quests_busy()) {
		set_stage(Q_FALLEN, QUEST_DONE, "");
		set_stage(Q_EYRIE, 10, T_QUST_00046EF0_NNAM0);
		fallen_step = 8;
	}
	// (on the porch: Odahviing in the yoke, there till he's set free - a game loaded there, or back)
	if (((st >= 70 && st < QUEST_DONE) || (stage[Q_EYRIE] > 0 && stage[Q_EYRIE] < 30)) && odahviing_q < 0 &&
	    at_qplace("porch_trap", feet, interior, 120.0f))
		odahviing_trapped();
	if (stage[Q_FALLEN] == 60 && fallen_step < 5 && at_qplace("porch_trap", feet, interior, PORCH_SHOUT_M)) { // (a game loaded mid-fight: called again)
		set_stage(Q_FALLEN, 50, T_QUST_00044C56_NNAM7);
	}
	if (odahviing_q >= 0 && creatures_health_share(odahviing_q) < 0.0f)
		odahviing_q = -1;
}

int quests_calls_dragon(void) {
	return stage[Q_FALLEN] == 50 && at_qplace("porch_trap", last_feet, interior_current(), PORCH_SHOUT_M);
}

// Season Unending (MQ302): Arngeir agrees to the council; Tullius and Ulfric each to come; at High
// Hrothgar, everyone at their chairs, the council said through (Skyrim's council scenes)
static void season_update(const float feet[3], int interior) {
	const int st = stage[Q_SEASON];
	const int hh = interior >= 0 && interior == interior_find(HIGH_HROTHGAR);
	if (season_step == 1 && !quests_busy()) {
		set_stage(Q_SEASON, 20, T_QUST_00045923_NNAM1);
		season_step = 0;
	} else if (season_step == 2 && !quests_busy()) {
		set_stage(Q_SEASON, 30, T_QUST_00045923_NNAM2);
		season_step = 0;
	} else if (season_step == 3 && !quests_busy()) {
		set_stage(Q_SEASON, 40, T_QUST_0002D515_NNAM0);
		season_step = 0;
	} else if ((st == 40 || st == 50 || st == 60) && hh) {
		for (int i = 0; i < COUNCIL_COUNT; i++) { // (each at their chair; there, not about their days)
			di_place(COUNCIL[i][0], COUNCIL[i][1], interior);
			stay(COUNCIL[i][0]);
		}
		if (st == 40)
			set_stage(Q_SEASON, 50, T_QUST_00045923_NNAM4);
	}
	if (st == 50 && at_qplace("council_player", feet, interior, 2.5f) && !quests_busy()) {
		set_stage(Q_SEASON, 60, T_QUST_00045923_NNAM5);
		season_step = 10; // (the council's scenes in turn: 10 + each)
	}
	if (season_step >= 10 && !quests_busy()) {
		const int k = season_step - 10;
		if (k < COUNCIL_SCENE_COUNT) {
			play(COUNCIL_SCENES[k]);
			season_step++;
		} else {
			for (int i = 0; i < COUNCIL_COUNT; i++) {
				const int a = actor_named(COUNCIL[i][0]);
				if (a >= 0)
					actors_script_release(a);
			}
			set_stage(Q_SEASON, QUEST_DONE, "");
			season_step = 0;
		}
	}
	if (st == 60 && season_step < 10) // (a game loaded mid-council: from its start)
		set_stage(Q_SEASON, 50, T_QUST_00045923_NNAM4);
}

// Paarthurnax (MQPaarthurnax): the Blades' price - he was Alduin's lieutenant; slain on his mountain,
// or not (Skyrim's is never forced: left open)
static void paar_update(const float feet[3], int interior) {
	const int st = stage[Q_PAARTHURNAX];
	if (paar_step == 1 && !quests_busy()) {
		set_stage(Q_PAARTHURNAX, 10, T_QUST_0003FA16_NNAM0);
		if (paarthurnax_i >= 0)
			creatures_set_floor(paarthurnax_i, 0.0f);
		paar_step = 0;
	} else if (st == 10 && paarthurnax_i >= 0 && summit_near(feet, interior)) {
		const float h = creatures_health_share(paarthurnax_i);
		static int roused;
		if (h > 0.0f && h < 0.999f && !roused) // (struck: he fights back)
			creatures_dragon_wake(paarthurnax_i), roused = 1;
		if (h == 0.0f) {
			set_stage(Q_PAARTHURNAX, 20, T_QUST_00032926_NNAM0);
			roused = 0;
		}
	} else if (paar_step == 2 && !quests_busy()) {
		set_stage(Q_PAARTHURNAX, QUEST_DONE, "");
		paar_step = 0;
	}
}

// The World-Eater's Eyrie (MQ303): the trap opened (the Jarl's man at the yoke), Odahviing's word
// kept - flown to Skuldafn; up through it to Alduin's portal, into Sovngarde
static void eyrie_update(const float feet[3], int interior) {
	const int st = stage[Q_EYRIE];
	if (eyrie_step == 1 && !quests_busy()) { // (INFO 0004DE40)
		play("mq303_free");
		set_stage(Q_EYRIE, 20, T_QUST_00046EF0_NNAM1);
		eyrie_step = 2;
	} else if (st == 20 && eyrie_step <= 2 && at_qplace("porch_trap", feet, interior, PORCH_TRAP_M) && !quests_busy()) {
		play("mq303_fly"); // (INFO 00109E39)
		eyrie_step = 3;
	} else if (eyrie_step == 3 && !quests_busy()) {
		creatures_dragon_away(odahviing_q), odahviing_q = -1;
		move_to_place("skuldafn_start");
		set_stage(Q_EYRIE, 30, T_QUST_00046EF0_NNAM2);
		eyrie_step = 4;
	} else if (eyrie_step == 4 && at_qplace("skuldafn_start", feet, interior, 40.0f) && !quests_busy()) {
		const QPlace *o = qplace("skuldafn_odahviing");
		if (o) { // (INFO 000E16F6)
			const int d = creatures_place_dragon("dragon", o->pos, o->heading, 1);
			creatures_set_name(d, T_NPC_00045920_FULL0);
			creatures_set_floor(d, 1.0f);
		}
		play("mq303_landed");
		eyrie_step = 5;
	} else if (st == 30 && at_qplace("skuldafn_portal", feet, interior, PORTAL_M * 4.0f)) {
		set_stage(Q_EYRIE, 40, T_QUST_00046EF0_NNAM3);
	} else if (st == 40 && at_qplace("skuldafn_portal", feet, interior, PORTAL_M)) {
		set_stage(Q_EYRIE, QUEST_DONE, "");
		set_stage(Q_SOVNGARDE, 10, T_QUST_00046EF1_NNAM0);
		move_to_place("sovngarde_start");
	}
}

// Sovngarde (MQ304): Tsun at the Whalebone Bridge (INFO 0005039A), the Hall of Valor,
// the heroes who fought Alduin of old. Dragonslayer (MQ305, MQ306): out into the mist with them,
// Clear Skies shouted together till Alduin can't hide in it, his end; Tsun sends you home
static void sovngarde_update(float dt, const float feet[3], int interior) {
	const int st = stage[Q_SOVNGARDE], sl = stage[Q_DRAGONSLAYER];
	if (st == 10 && at_qplace("sovngarde_tsun", feet, interior, TSUN_MEET_M) && !quests_busy()) {
		play("mq304_tsun_bridge"); // (INFO 0004FA20)
		set_stage(Q_SOVNGARDE, 20, T_QUST_00046EF1_NNAM1);
	} else if (sov_step == 1 && !quests_busy()) { // (his trial: INFO 0005039E)
		play("mq304_tsun_worthy");
		sov_step = 2;
	} else if (sov_step == 2 && !quests_busy()) {
		set_stage(Q_SOVNGARDE, 30, T_QUST_00046EF1_NNAM2);
		move_to_place("sovngarde_bridge_end");
		sov_step = 0;
	} else if (sov_step == 3 && !quests_busy()) { // (INFO 00096FF8)
		set_stage(Q_SOVNGARDE, QUEST_DONE, "");
		set_stage(Q_DRAGONSLAYER, 10, T_QUST_00046EF2_NNAM0);
		move_to_place("sovngarde_battle");
		sov_step = 4;
	}
	if (sl == 10 || sl == 20) { // (the heroes with you, out in the mist; fighting with you)
		static int placed_place = -99;
		const int here = at_qplace("sovngarde_battle", feet, interior, 200.0f);
		if (here && placed_place != interior) {
			for (int h = 0; h < 3; h++)
				actor_beside(HEROES[h], feet, -2.0f + 2.0f * h);
			placed_place = interior;
			if (sl == 10 && !mist_cleared)
				play("mq305_mist"); // (INFO 00050398)
		}
		if (!here)
			placed_place = -99;
		for (int h = 0; h < 3 && sl == 20; h++)
			ally_fights_by(actor_named(HEROES[h]), dt, feet, &hero_strike[h]);
	}
	if (sl == 20) {
		const float h = creatures_health_share(alduin_q);
		if (alduin_q < 0 || h < 0.0f) { // (a game loaded mid-fight, or he's not about: down from the sky again)
			const QPlace *l = qplace("sovngarde_alduin_land");
			if (l && at_qplace("sovngarde_battle", feet, interior, 200.0f)) {
				alduin_q = creatures_place_dragon("alduin", l->pos, l->heading, 0);
				creatures_set_name(alduin_q, T_NPC_00032B94_FULL0);
				creatures_dragon_wake(alduin_q);
			}
		} else if (h == 0.0f && !slayer_step) { // (his end: INFO 0005195A)
			play("mq305_outro");
			slayer_step = 1;
		}
	}
	if (slayer_step == 1 && !quests_busy()) {
		play("mq305_tsun_after");
		set_stage(Q_DRAGONSLAYER, 30, T_QUST_00046EF3_NNAM0);
		alduin_q = -1;
		slayer_step = 2;
	} else if (slayer_step == 3 && !quests_busy()) { // (home: the summit, the dragons gathered - MQ306's)
		move_to_place("sovngarde_return");
		slayer_step = 4;
	} else if (slayer_step == 4 && summit_near(feet, interior) && !quests_busy()) {
		play("mq306_dragons");
		set_stage(Q_DRAGONSLAYER, QUEST_DONE, "");
		for (int h = 0; h < 3; h++) {
			const int a = actor_named(HEROES[h]);
			if (a >= 0)
				actors_script_release(a);
		}
		slayer_step = 5;
	}
}

static void late_shouted(int kind, const float feet[3], int interior) {
	if (kind == SHOUT_CALLDRAGON && stage[Q_FALLEN] == 50 && at_qplace("porch_trap", feet, interior, PORCH_SHOUT_M)) {
		const QPlace *c = qplace("porch_call"); // (Odahviing: INFO 000D2D70 - from where Skyrim's starts him, off the porch)
		const QPlace *t = qplace("porch_trap");
		const float at[3] = {(c ? c : t)->pos[0], (c ? c : t)->pos[1], (c ? c : t)->pos[2]};
		odahviing_q = creatures_place_dragon("dragon", at, c->heading, 0);
		creatures_set_name(odahviing_q, T_NPC_00045920_FULL0);
		creatures_set_floor(odahviing_q, ODAHVIING_TRAPPED_AT);
		creatures_dragon_wake(odahviing_q); // (up and at you)
		play("mq301_odahviing_comes");
		set_stage(Q_FALLEN, 60, T_QUST_00044C56_NNAM8);
		fallen_step = 5;
	}
	if (kind == SHOUT_CLEARSKIES && stage[Q_DRAGONSLAYER] == 10 && at_qplace("sovngarde_battle", feet, interior, 200.0f)) {
		ui_notice("The mist thins", 3.0f); // (with the heroes' own: Lok Vah Koor, three together)
		if (++mist_cleared < HEROES_NEEDED) {
			play("mq305_again");
		} else {
			const QPlace *l = qplace("sovngarde_alduin_land");
			if (l) {
				alduin_q = creatures_place_dragon("alduin", l->pos, l->heading, 0);
				creatures_set_name(alduin_q, T_NPC_00032B94_FULL0);
				creatures_dragon_wake(alduin_q);
			}
			play("mq305_face"); // (INFO 000EDF4B)
			set_stage(Q_DRAGONSLAYER, 20, T_QUST_00036193_NNAM2);
		}
	}
}

static void late_update(float dt, const float feet[3], int interior) {
	if (move_pending) {
		move_settle = MOVE_SETTLE_S;
		return;
	}
	if (move_settle > 0.0f && (move_settle -= dt) > 0.0f)
		return;
	paarthurnax_at_summit(feet, interior);
	elder_update(feet, interior);
	bane_update(dt, feet, interior);
	fallen_update(feet, interior);
	season_update(feet, interior);
	paar_update(feet, interior);
	eyrie_update(feet, interior);
	sovngarde_update(dt, feet, interior);
}

static void shouted_at(int kind, const float feet[3], int interior) {
	late_shouted(kind, feet, interior);
	const QPlace *storm = qplace("throat_storm"), *perch = qplace("throat_paarthurnax");
	if (kind == SHOUT_CLEARSKIES && interior < 0 && storm &&
	    hypotf(storm->pos[0] - feet[0], storm->pos[2] - feet[2]) < STORM_REACH_M * 2.0f) { // (INFO 000B5E1B)
		storm_clear_t = CLEAR_SKIES_S;
		ui_notice("The storm clears", 3.0f);
		if (stage[Q_THROAT] == 25)
			set_stage(Q_THROAT, 30, T_QUST_0002D515_NNAM3);
	}
	if (kind == SHOUT_FIRE && stage[Q_THROAT] == 50 && throat_step == 8 && interior < 0 && perch &&
	    hypotf(perch->pos[0] - feet[0], perch->pos[2] - feet[2]) < PAAR_HEAR_M) {
		play("mq204_paar_after"); // (INFO 0005586A)
		throat_step = 9;
	}
}

void quests_update(float dt, const float feet[3], int interior, int souls) {
	play_queued();
	if (welcome_wait > 0.0f && !voice_playing() && (welcome_wait -= dt) <= 0.0f && !wv_step) {
		play("mq105_welcome"); // (INFO 000649B5)
		wv_step = 1;
	}
	dtree_update();
	project_told_step(); // (Farengar's done telling you of the Dragonstone: the objective now)
	{ // (a conversation's turn reached: its quest moves on - Riverwood's ask for help from the Jarl)
		const char *reached = dtree_take_reached();
		companions_update(dt, feet, interior, lighting_sky()->hours, vitals_now, reached); // (the Companions': companions.c)
		college_update(dt, feet, interior, reached);                                        // (the College's: college.c)
		thieves_update(dt, feet, interior, reached);                                        // (the Thieves Guild's: thieves.c)
		if (reached && (!strcmp(reached, "MQ102GerdurHelpD1") || !strcmp(reached, "MQ102AlvorHelpD1")) &&
		    (stage[Q_BEFORE_THE_STORM] == 7 || stage[Q_BEFORE_THE_STORM] == 8))
			bts_asked = 0, set_stage(Q_BEFORE_THE_STORM, 10, "Speak to Jarl Balgruuf in Dragonsreach");
		if (reached && rat_step == 3 && (!strcmp(reached, "MQ202EsbernThroughDoorC3") || !strcmp(reached, "MQ202EsbernThroughDoorC2")))
			rat_step = 4; // ("Delphine said to 'remember the 30th of Frostfall'" - or Dragonborn: INFO 0001C0EA)
		if (reached && rat_step == 3 && !strcmp(reached, "MQ202EsbernThroughDoorC4")) {
			int l = 0;
			if (strstr(dtree_reached_reply(&l), "14:")) // (DIAL 00098D39 persuaded: Speech, as Skyrim's check)
				rat_step = 4, skills_use(SK_SPEECH, 30.0f);
		}
		if (reached && !strcmp(reached, "MQ201PartyDrunkHowToHelp") && stage[Q_DIPLOMATIC] == 50)
			di_step = 12; // (DIAL 00036D47 INFO 0001897B)
		int links = 0;
		const char *cond = reached ? dtree_reached_reply(&links) : "";
		if (reached && di_step == 40 && !strcmp(reached, "MQ201PartyFriendDistractionYes") && links == 0 && !strstr(cond, "14:"))
			friend_ok = 1; // (a friend's yes: Skyrim's reply that asks nothing more)
		if (reached && di_step == 40 && (!strncmp(reached, "MQ201PartyDistractionPersuade", 29) ||
		                                 !strcmp(reached, "MQ201PartyOndolomarDistractionYes")) && strstr(cond, "14:"))
			friend_ok = 1, skills_use(SK_SPEECH, 30.0f); // (persuaded: Speech against SpeechAverage, or an Amulet of Articulation)
		if (reached && !strcmp(reached, "MQ201ErikurBrelasA1") && er_step == ER_EYEING)
			er_step = ER_ASK_HER; // (DIAL 00069F14)
		if (reached && !strcmp(reached, "MQ201PartyErikurA2") && er_step == ER_ASKED + 100) {
			er_step = ER_SCENE; // (DIAL 00069F11 - Skyrim's ErikurBrelasScene)
			play("mq201_erikur_scene");
			di_step = 42;
		}
		if (reached && (!strcmp(reached, "MQ201PartyErikurA3") || !strcmp(reached, "MQ201ErikurBrelasA2")))
			er_step = ER_NO;
		if (reached && !strcmp(reached, "MQ201BrelasPrisonerA1") && er_step == ER_SCENE) { // (DIAL 000539C2)
			const int b = actor_named("brelas");
			if (b >= 0)
				actors_script_release(b);
			er_step = ER_FREED;
		}
	}
	topics_update();
	if (brawl_pending >= 0 && !quests_busy()) { // (INFO 00055A0B)
		brawl_start(brawl_pending, skills_state()->player_level);
		set_stage(Q_BRAWL, 10, "Beat Uthgerd in a brawl");
		brawl_pending = -1;
	}
	jarl_update(feet, dt);
	// The Dragonstone found (picked up however): to Farengar with it
	const int stone = qitem("dragonstone", NULL);
	if (stage[Q_BLEAK_FALLS_BARROW] == 20 && stone >= 0 && items_have(stone) > 0)
		set_stage(Q_BLEAK_FALLS_BARROW, 30, T_QUST_000D0800_NNAM2);
	if (stage[Q_BLEAK_FALLS_BARROW] == 30 && stone >= 0 && items_have(stone) <= 0) // (lost it: back to finding it)
		stage[Q_BLEAK_FALLS_BARROW] = 20, stone_given = 0;
	memcpy(last_feet, feet, sizeof(last_feet));
	{ // The Golden Claw (Skyrim's MS13): heard of from the bandits by the fire in Bleak Falls Temple - a line of theirs
	  // (its stage 12: how you learn of it without Lucan) - or
	  // from Arvel in the web; found (Arvel's, his journal with it): back to Lucan, or to find its owner
		const int claw = items_find_name(T_MISC_00039647_FULL0);
		static float temple_t;
		const char *here = interior_id();
		if (here && !strcmp(here, "bleakfallsbarrow01") && !stage[Q_GOLDEN_CLAW]) {
			if ((temple_t += dt) > CAMPFIRE_HEARD_S && !quests_busy()) {
				play("bfb_campfire");
				quests_claw_learned();
			}
		} else {
			temple_t = 0.0f;
		}
		if (claw >= 0 && items_have(claw) > 0 && stage[Q_GOLDEN_CLAW] < 20) {
			const int journal = quests_item("arvel_journal");
			if (journal >= 0 && items_have(journal) <= 0) { // (in Arvel's pockets with it: how the claw opens the door)
				items_add(journal, 1);
				ui_notice("Arvel's Journal added", 3.0f);
			}
			if (stage[Q_GOLDEN_CLAW] == 10)
				set_stage(Q_GOLDEN_CLAW, 20, T_QUST_00039645_NNAM4);
			else
				set_stage(Q_GOLDEN_CLAW, 35, T_QUST_00039645_NNAM3);
		}
	}
	// (found before Farengar asked for it: straight to bringing it to him, as Skyrim's)
	if (stage[Q_BLEAK_FALLS_BARROW] < 20 && stage[Q_BLEAK_FALLS_BARROW] != QUEST_DONE && stone >= 0 && items_have(stone) > 0 &&
	    stage[Q_BEFORE_THE_STORM] >= QUEST_DONE)
		set_stage(Q_BLEAK_FALLS_BARROW, 30, T_QUST_000D0800_NNAM2);
	// The Western Watchtower: Irileth and her men meet you there; the tower's last guard warns
	// you; Mirmulnir comes back. Its soul yours: "Dragonborn...", then the Greybeards' call
	float tower[2];
	const int at_tower = interior < 0 && marker_named(T_LCTN_000D9079_FULL0, tower) &&
	                     hypotf(feet[0] - tower[0], feet[2] - tower[1]) < TOWER_ARRIVE_M;
	// (Irileth and her men there before you: waiting by the tower as you come over the hill, not
	// appearing at your side when you got there - out of sight still when they're put there)
	static int tower_waiting; // (1 Irileth there, 2 her men too)
	static float waiting_at[3], waiting_dir[2];
	const int near_tower = interior < 0 && marker_named(T_LCTN_000D9079_FULL0, tower) &&
	                       hypotf(feet[0] - tower[0], feet[2] - tower[1]) < TOWER_WAIT_M;
	if (stage[Q_DRAGON_RISING] == 20 && near_tower && !at_tower && !dr_step && !tower_waiting) {
		const int ir = actor_named("irileth");
		float dx = feet[0] - tower[0], dz = feet[2] - tower[1];
		const float d = fmaxf(hypotf(dx, dz), 0.1f);
		dx /= d, dz /= d;
		float at[3] = {tower[0] + dx * TOWER_WAIT_OFF_M, feet[1], tower[1] + dz * TOWER_WAIT_OFF_M}, ground;
		if (terrain_height_at(at[0], at[2], &ground))
			at[1] = ground;
		if (ir >= 0)
			actors_script(ir, 1), actors_script_place(ir, at, atan2f(dx, dz)), actors_script_follow(ir, 0);
		tower_waiting = 1, memcpy(waiting_at, at, sizeof(waiting_at)), waiting_dir[0] = dx, waiting_dir[1] = dz;
		game_log("quest: Irileth waiting by the watchtower");
	}
	if (tower_waiting == 1 && stage[Q_DRAGON_RISING] == 20 && hypotf(feet[0] - waiting_at[0], feet[2] - waiting_at[2]) < TOWER_MEN_M) {
		// (her men by her: once you're near enough that they'd stay - creatures further off are let go -
		// beyond the fog still); archers, to shoot at a dragon in the air
		const char *guard = creatures_guard_archer_kind(crime_hold_index("CrimeFactionWhiterun"));
		const float dx = waiting_dir[0], dz = waiting_dir[1];
		for (int k = 0; k < 3 && guard; k++) {
			const float by[3] = {waiting_at[0] - dz * (2.0f * k - 2.0f) - dx * 2.5f, waiting_at[1],
			                     waiting_at[2] + dx * (2.0f * k - 2.0f) - dz * 2.5f};
			creatures_summon_escort(guard, by, ESCORT_S);
		}
		tower_waiting = 2;
	}
	if (stage[Q_DRAGON_RISING] != 20)
		tower_waiting = 0;
	if (stage[Q_DRAGON_RISING] == 20 && at_tower && !dr_step) {
		irileth = actor_named("irileth");
		if (irileth >= 0 && tower_waiting) // (from where she waited: with you now)
			actors_script(irileth, 1), actors_script_follow(irileth, 1);
		else
			irileth = actor_beside("irileth", feet, 2.5f);
		const char *guard = creatures_guard_archer_kind(crime_hold_index("CrimeFactionWhiterun"));
		for (int k = 0; k < 3 && guard && tower_waiting < 2; k++) {
			const float at[3] = {feet[0] - 2.0f + 2.0f * k, feet[1], feet[2] + 3.0f};
			creatures_summon_escort(guard, at, ESCORT_S);
		}
		play("mq104_irileth_tower"); // (INFO 000A3F50)
		dr_step = 1;
		game_log("quest: Irileth and her men at the watchtower");
	} else if (dr_step == 1 && !quests_busy()) {
		play("mq104_tower_guard"); // (INFO 000A27B1)
		dr_step = 2;
	} else if (dr_step == 2 && !quests_busy()) {
		play("mq104_here_he_comes");
		creatures_summon_dragon(feet);
		set_stage(Q_DRAGON_RISING, 30, T_QUST_0002610C_NNAM2);
		dr_step = 3;
	} else if (dr_step == 3 && !quests_busy()) {
		play("mq104_fight"); // (Irileth: INFO 0005DD52)
		dr_step = 4;
	}
	if (stage[Q_DRAGON_RISING] == 20 && at_tower && dr_step >= 3) // (a game loaded mid-fight: it comes again)
		dr_step = 0;
	if (stage[Q_DRAGON_RISING] == 30)
		irileth_fights(dt, feet);
	if (stage[Q_DRAGON_RISING] == 30 && creatures_take_dragon_fell()) // (INFO 000BCF14: as it falls, as Skyrim's)
		play("mq104_dragon_dies");
	if (stage[Q_DRAGON_RISING] == 30 && souls > 0) {
		dr_step = 5;
		set_stage(Q_DRAGON_RISING, 40, T_QUST_0002610C_NNAM4);
	} else if (dr_step == 5 && !quests_busy()) {
		play("mq104_dragonborn"); // (INFO 000C28C6)
		dr_step = 6;
	} else if (dr_step == 6 && !quests_busy()) {
		if (irileth >= 0)
			actors_script_clip(irileth, "", 0);
		play("mq104_irileth_after"); // (INFO 0005DD4F)
		dr_step = 7;
	} else if (dr_step == 7 && !quests_busy()) {
		greybeards_t = GREYBEARDS_CALL_S; // (all said: the mountain answers - then the men wonder at it)
		dr_step = 8;
	} else if (dr_step == 9 && (heard_t -= dt) <= 0.0f && !quests_busy()) {
		play("mq104_greybeards_heard"); // (INFO 0005EE3A)
		dr_step = 10;
	}
	if (greybeards_t > 0.0f && (greybeards_t -= dt) <= 0.0f) { // ("Dovahkiin!": the Greybeards, from the Throat of the World)
		sfx_play("greybeard_call", 1.0f, 0.0f);
		sfx_play("greybeard_rumble", 0.8f, 0.0f);
		rumble = 1; // (the ground shakes with it: main's camera)
		game_log("quest: the Greybeards' call");
		if (dr_step == 8)
			dr_step = 9, heard_t = GREYBEARDS_HEARD_S;
	}
	if (irileth >= 0 && stage[Q_DRAGON_RISING] >= 40 && (interior >= 0 || !at_tower)) { // (back to Dragonsreach)
		actors_script_release(irileth);
		irileth = -1;
		const char *guard = creatures_guard_kind(crime_hold_index("CrimeFactionWhiterun"));
		if (guard) // (her men stay at the tower: they'd followed you on to High Hrothgar)
			creatures_dismiss_allies(guard);
	}
	// The world switches the main quest's stages throw (converter/esm.py SWITCHES_INSIDE): Ustengrav's
	// cave-in gone once the Horn's asked for (MQ105's fragment 42) - kept in step with the stage, so a
	// save from before this and the saved switch bits can't disagree
	objects_set_switch(SWITCH_USTENGRAV, stage[Q_HORN] >= 10);
	horn_update(dt, feet, interior);
	blade_update(dt, feet, interior, souls);
	di_update(feet, interior);
	rat_update(feet, interior);
	wall_update(feet, interior);
	throat_update(dt, feet, interior);
	late_update(dt, feet, interior);
	greet_update(dt, feet);
	// The Way of the Voice: Arngeir greets you at High Hrothgar; you Shout for them; Einarth
	// gives you "Ro", Borri "Wuld"; then the Horn
	const int in_hrothgar = interior >= 0 && interior == interior_find(HIGH_HROTHGAR);
	if (stage[Q_WAY_OF_THE_VOICE] == 10 && in_hrothgar && !quests_busy()) {
		play("mq105_arrive"); // (INFO 0002F7A5)
		set_stage(Q_WAY_OF_THE_VOICE, 20, T_QUST_000242BA_NNAM2);
	}
	if (stage[Q_WAY_OF_THE_VOICE] == 40 && in_hrothgar && !wv_step)
		wv_step = 3; // (a game loaded after Ro: on with Borri)
	{ // (Skyrim's MQ105 holds the Greybeards in the main hall at their marks - Arngeir to greet you, the
	  // masters at their meditation spots - from your arrival till the trials: they'd walked their own
	  // days about the place, Arngeir and Einarth to be hunted for)
		static int held;
		const int wv = stage[Q_WAY_OF_THE_VOICE];
		const int want = in_hrothgar && wv >= 10 && wv < QUEST_DONE && !wv_step && !horn_step;
		if (want && !held)
			greybeards_gather(1, NULL, feet), held = 1;
		else if (!in_hrothgar && held)
			greybeards_release(), held = 0;
		else if (!want)
			held = in_hrothgar && held;
	}
	// (the word a Greybeard has shouted onto the floor before you: glowing, then drifting into you)
	if (floor_word_t > 0.0f) {
		floor_word_t -= dt;
		const float chest[3] = {feet[0], feet[1] + 1.2f, feet[2]};
		spellfx_floor_word(floor_word, 1.0f); // (lying on the floor before you - it had glowed at knee height)
		if (floor_word_t < FLOOR_WORD_S - 1.0f)
			spellfx_word((const float[3]){floor_word[0], floor_word[1] + 0.1f, floor_word[2]}, chest, dt);
	}
	if (wv_step && !quests_busy() && floor_word_t <= 0.0f) {
		switch (wv_step) {
		case 1:
			if (!at_qplace("hh_centre", feet, interior, HH_CENTRE_M) && qplace("hh_centre"))
				break; // (INFO 000C5228: the trial when you're there - not at the door)
			greybeards_gather(1, "einarth", feet);
			play("mq105_ro"); // (Einarth: "Ro..." - shouted onto the floor before you)
			floor_word_at(feet, "einarth", "ro");
			wv_step = 2;
			break;
		case 2: {
			const char *w = shouts_learn_word("Ro");
			shouts_add_souls(1); // (Einarth's own understanding of it: no dragon's soul of yours spent)
			shouts_unlock(SHOUT_FORCE);
			if (w)
				ui_notice(w, 4.0f);
			game_log("quest: Einarth's gift - %s (Unrelenting Force: %d words)", w ? w : "Ro known already", shouts_known(SHOUT_FORCE));
			play("mq105_ro_learned");
			set_stage(Q_WAY_OF_THE_VOICE, 40, "Learn the Word of Power from Master Borri");
			wv_step = 3;
			break;
		}
		case 3:
			play("mq105_borri");
			wv_step = 4;
			break;
		case 4:
			greybeards_gather(2, "borri", feet);
			play("mq105_wuld"); // (Borri: "Wuld...")
			floor_word_at(feet, "borri", "wuld");
			wv_step = 5;
			break;
		case 5: {
			const char *w = shouts_learn_word("Wuld");
			shouts_add_souls(1);
			shouts_unlock(SHOUT_SPRINT);
			if (w)
				ui_notice(w, 4.0f);
			game_log("quest: Borri's gift - %s (Whirlwind Sprint: %d words)", w ? w : "Wuld known already", shouts_known(SHOUT_SPRINT));
			play("mq105_horn"); // (INFO 00030315)
			wv_step = 6;
			break;
		}
		case 6:
			greybeards_release();
			set_stage(Q_WAY_OF_THE_VOICE, QUEST_DONE, "");
			set_stage(Q_HORN, 10, "Retrieve the Horn of Jurgen Windcaller from Ustengrav");
			wv_step = 0;
			break;
		}
	}
}

void quests_shouted(int kind, int interior) {
	shouted_at(kind, last_feet, interior); // (Clear Skies on the mountain's path, Fire Breath at Paarthurnax)
	college_shouted(last_feet);            // (Faralda hears the Voice: the College's gate)
	if (stage[Q_WAY_OF_THE_VOICE] == 30 && interior >= 0 && interior == interior_find(HIGH_HROTHGAR) && !wv_step)
		welcome_wait = WELCOME_AFTER_SHOUT_S; // (Arngeir's answer once your Shout's been heard out - it had cut it off)
}

void quests_brawl_over(int result) {
	if (companions_brawl_over(result) || thieves_brawl_over(result)) // (Vilkas's spar; a debt collected with fists)
		return;
	const int u = actor_named("uthgerd");
	if (u >= 0)
		actors_script(u, 0);
	if (result == BRAWL_WON) {
		topics_say("brawl_won", u, NULL); // (INFO 000DB892)
		set_stage(Q_BRAWL, 20, "Collect your winnings from Uthgerd");
	} else if (result == BRAWL_LOST) {
		items_add(items_find_gold(), -BRAWL_BET);
		ui_notice("Uthgerd won the brawl. 100 gold lost", 4.0f);
		stage[Q_BRAWL] = 0;
	} else if (result == BRAWL_BROKEN) {
		ui_notice("The brawl is off: just fists, no weapons, no magic", 4.0f);
		stage[Q_BRAWL] = 0;
	}
}

int quests_take_reopen(void) {
	if (reopen_actor < 0 || quests_busy())
		return -1;
	const int a = reopen_actor;
	reopen_actor = -1;
	return a;
}

// The player's lines for the quest topics someone has now
enum { T_HELGEN, T_PROJECT, T_STONE, T_DRAGON, T_KILLED, T_ARNGEIR,
       T_SVEN_INTRO, T_SVEN_GIVE, T_SVEN_TELL, T_SVEN_REWARD, T_FAENDAL_INTRO, T_FAENDAL_GIVE, T_FAENDAL_TELL,
       T_FAENDAL_REWARD, T_CAMILLA_LIE_SVEN, T_CAMILLA_TRUTH_SVEN, T_CAMILLA_LIE_FAENDAL, T_CAMILLA_TRUTH_FAENDAL,
       T_BRAWL_GIVE, T_BRAWL_ACCEPT, T_BRAWL_REJECT, T_BRAWL_PAID, T_CLAW_ASK, T_CLAW_VOLUNTEER, T_CLAW_RETURN,
       T_BTS_HELP, T_BTS_FRIEND, T_RENT_ATTIC, T_HORN_RETURN, T_DI_DELPHINE, T_DI_MALBORN, T_DI_STABLES,
       T_DI_PARTY_MALBORN, T_DI_RAZELAN, T_DI_ETIENNE, T_DI_OUTRO, T_DI_FRIEND, T_ER_BRELAS, T_ER_TALK_BRELAS,
       T_BRELAS_FREE, T_RAT_BRYNJOLF, T_RAT_VEKEL, T_RAT_ESBERN, T_WALL_ESBERN, T_WALL_DELPHINE, T_THROAT_ARNGEIR,
       T_ELDER_URAG, T_ELDER_SEPTIMUS, T_FALLEN_ARNGEIR, T_FALLEN_JARL, T_FALLEN_READY, T_SEASON_ARNGEIR,
       T_SEASON_TULLIUS, T_SEASON_ULFRIC, T_PAAR_DELPHINE, T_PAAR_DONE, T_EYRIE_FREE, T_TSUN, T_HERO, T_TSUN_HOME, T_INVEST,
       T_COUNT };
#define PERK_INVESTOR 0x00058f7bu
// (the topics' keys in topics.txt: the player's words and the answers - the favours' and the brawl's)
static const char *const KEY[T_COUNT] = {
	[T_SVEN_INTRO] = "letter_sven_intro", [T_SVEN_GIVE] = "letter_sven_give", [T_SVEN_TELL] = "letter_sven_tell",
	[T_SVEN_REWARD] = "letter_sven_reward", [T_FAENDAL_INTRO] = "letter_faendal_intro",
	[T_FAENDAL_GIVE] = "letter_faendal_give", [T_FAENDAL_TELL] = "letter_faendal_tell",
	[T_FAENDAL_REWARD] = "letter_faendal_reward", [T_CAMILLA_LIE_SVEN] = "letter_camilla_lie_sven",
	[T_CAMILLA_TRUTH_SVEN] = "letter_camilla_truth_sven", [T_CAMILLA_LIE_FAENDAL] = "letter_camilla_lie_faendal",
	[T_CAMILLA_TRUTH_FAENDAL] = "letter_camilla_truth_faendal", [T_BRAWL_GIVE] = "brawl_give",
	[T_BRAWL_ACCEPT] = "brawl_accept", [T_BRAWL_REJECT] = "brawl_reject", [T_BRAWL_PAID] = "brawl_paid",
	[T_CLAW_ASK] = "claw_ask", [T_CLAW_VOLUNTEER] = "claw_volunteer", [T_CLAW_RETURN] = "claw_return"};

static int letter_has(const char *name) {
	const int it = qitem(name, NULL);
	return it >= 0 && items_have(it) > 0;
}

static void letter_take(const char *name, int count) {
	const int it = qitem(name, NULL);
	if (it < 0)
		return;
	items_add(it, count);
	if (count > 0) {
		char text[96];
		snprintf(text, sizeof(text), "%s added", items_name(it));
		ui_notice(text, 3.0f);
	}
}

static int topics_of(int actor, int *out) {
	const char *id = actors_id(actor);
	const int letter = stage[Q_LOVELY_LETTER];
	int n = 0;
	if (!strcmp(id, "delphine")) { // (the note: "Rent the attic room at the Sleeping Giant Inn")
		if (stage[Q_HORN] == 30 && !horn_step)
			out[n++] = T_RENT_ATTIC;
	} else if (!strcmp(id, "arngeir") && stage[Q_HORN] == 40 && !greet_step) {
		out[n++] = T_HORN_RETURN;
	}
	if (!strcmp(id, "brynjolf") && stage[Q_RAT] == 10 && !rat_step) // (A Cornered Rat)
		out[n++] = T_RAT_BRYNJOLF;
	if (!strcmp(id, "vekel") && stage[Q_RAT] == 20 && !rat_step)
		out[n++] = T_RAT_VEKEL;
	if (!strcmp(id, "esbern") && stage[Q_RAT] == 40 && !rat_step)
		out[n++] = T_RAT_ESBERN;
	if (!strcmp(id, "esbern") && stage[Q_WALL] == 20 && wall_step < 2) // (Alduin's Wall)
		out[n++] = T_WALL_ESBERN;
	if (!strcmp(id, "delphine") && stage[Q_WALL] == 40 && wall_step == 6)
		out[n++] = T_WALL_DELPHINE;
	// (the main quest's last eight: Urag, Septimus; Arngeir, the Jarl; the council's; the Blades';
	// the trap's; Tsun and the heroes in Sovngarde)
	if (!strcmp(id, "uraggroshub") && stage[Q_ELDER] == 10 && !elder_step)
		out[n++] = T_ELDER_URAG;
	if (!strcmp(id, "da04septimus") && stage[Q_ELDER] == 20 && !elder_step)
		out[n++] = T_ELDER_SEPTIMUS;
	if (!strcmp(id, "arngeir") && stage[Q_FALLEN] == 10 && !fallen_step)
		out[n++] = T_FALLEN_ARNGEIR;
	if (!strcmp(id, "balgruufthegreater") && stage[Q_FALLEN] == 20 && !fallen_step)
		out[n++] = T_FALLEN_JARL;
	if (!strcmp(id, "balgruufthegreater") && stage[Q_FALLEN] == 40 && !fallen_step)
		out[n++] = T_FALLEN_READY;
	if (!strcmp(id, "balgruufthegreater") && stage[Q_EYRIE] == 10 && !eyrie_step)
		out[n++] = T_EYRIE_FREE;
	if (!strcmp(id, "arngeir") && stage[Q_SEASON] == 10 && !season_step)
		out[n++] = T_SEASON_ARNGEIR;
	if (!strcmp(id, "generaltullius") && stage[Q_SEASON] == 20 && !season_step)
		out[n++] = T_SEASON_TULLIUS;
	if (!strcmp(id, "ulfric") && stage[Q_SEASON] == 30 && !season_step)
		out[n++] = T_SEASON_ULFRIC;
	if (!strcmp(id, "delphine") && !stage[Q_PAARTHURNAX] && stage[Q_FALLEN] >= QUEST_DONE && !paar_step)
		out[n++] = T_PAAR_DELPHINE;
	if (!strcmp(id, "delphine") && stage[Q_PAARTHURNAX] == 20 && !paar_step)
		out[n++] = T_PAAR_DONE;
	if (!strcmp(id, "tsun") && stage[Q_SOVNGARDE] == 20 && !sov_step)
		out[n++] = T_TSUN;
	if (!strcmp(id, "tsun") && stage[Q_DRAGONSLAYER] == 30 && slayer_step < 3) // (a game loaded since: 0)
		out[n++] = T_TSUN_HOME;
	for (int h = 0; h < 3; h++)
		if (!strcmp(id, HEROES[h]) && stage[Q_SOVNGARDE] == 30 && !sov_step)
			out[n++] = T_HERO;
	{ // (Investor: a merchant's store, 500 gold into it)
		const int v = items_vendor(id);
		if (v >= 0 && perks_has_form(PERK_INVESTOR) && !items_vendor_invested(v) && items_gold() >= 500)
			out[n++] = T_INVEST;
	}
	if (!strcmp(id, "arngeir") && stage[Q_THROAT] == 10 && !throat_step) // (The Throat of the World)
		out[n++] = T_THROAT_ARNGEIR;
	if (!strcmp(id, "delphine") && stage[Q_DIPLOMATIC] == 5) // (Diplomatic Immunity: her plan)
		out[n++] = T_DI_DELPHINE;
	else if (!strcmp(id, "delphine") && stage[Q_DIPLOMATIC] == 30)
		out[n++] = T_DI_STABLES;
	else if (!strcmp(id, "malborn") && (stage[Q_DIPLOMATIC] == 10 || stage[Q_DIPLOMATIC] == 20))
		out[n++] = T_DI_MALBORN;
	else if (!strcmp(id, "malborn") && stage[Q_DIPLOMATIC] == 40 && !di_step)
		out[n++] = T_DI_PARTY_MALBORN;
	else if (!strcmp(id, "razelan") && stage[Q_DIPLOMATIC] == 50 && !di_step)
		out[n++] = T_DI_RAZELAN;
	else if (!strcmp(id, "etiennerarnis") && stage[Q_DIPLOMATIC] == 70 && !di_step)
		out[n++] = T_DI_ETIENNE;
	else if (!strcmp(id, "delphine") && stage[Q_DIPLOMATIC] == 90 && !di_step)
		out[n++] = T_DI_OUTRO;
	else if (!strcmp(id, "erikur") && stage[Q_DIPLOMATIC] == 50 && er_step == ER_EYEING && !di_step)
		out[n++] = T_ER_BRELAS;
	else if (!strcmp(id, "brelas") && stage[Q_DIPLOMATIC] == 50 && er_step == ER_ASK_HER && !di_step)
		out[n++] = T_ER_TALK_BRELAS;
	else if (!strcmp(id, "brelas") && stage[Q_DIPLOMATIC] >= 60 && stage[Q_DIPLOMATIC] < 90 && er_step == ER_SCENE)
		out[n++] = T_BRELAS_FREE;
	if (stage[Q_DIPLOMATIC] == 50 && !di_step) { // (a friend among the guests: Skyrim's IsInFriendStateWithPlayer - Vittoria needs none)
		char key[64];
		snprintf(key, sizeof(key), "mq201_friend_%s", id);
		if (dtree_find(key) >= 0 && (!strcmp(id, "vittoriavici") || quests_relationship(actor) >= 1))
			out[n++] = T_DI_FRIEND;
	}
	if (!strcmp(id, "balgruufthegreater")) {
		if (stage[Q_BEFORE_THE_STORM] == 10)
			out[n++] = T_HELGEN;
		if (stage[Q_DRAGON_RISING] == 10)
			out[n++] = T_DRAGON;
		if (stage[Q_DRAGON_RISING] == 40)
			out[n++] = T_KILLED;
	} else if (!strcmp(id, "farengarsecretfire")) {
		if (stage[Q_BLEAK_FALLS_BARROW] == 10)
			out[n++] = T_PROJECT;
		if (stage[Q_BLEAK_FALLS_BARROW] == 30)
			out[n++] = T_STONE;
	} else if (!strcmp(id, "arngeir")) {
		if (stage[Q_WAY_OF_THE_VOICE] == 20)
			out[n++] = T_ARNGEIR;
	} else if (!strcmp(id, "sven")) { // A Lovely Letter
		if (letter == 0 || letter == 2)
			out[n++] = T_SVEN_INTRO;
		if (letter == 1 || letter == 3)
			out[n++] = T_SVEN_GIVE;
		if (letter == 20 && letter_has("letter_faendal"))
			out[n++] = T_SVEN_TELL;
		if (letter == 30)
			out[n++] = T_SVEN_REWARD;
	} else if (!strcmp(id, "faendal")) {
		if (letter == 0 || letter == 1)
			out[n++] = T_FAENDAL_INTRO;
		if (letter == 2 || letter == 3)
			out[n++] = T_FAENDAL_GIVE;
		if (letter == 10 && letter_has("letter_sven"))
			out[n++] = T_FAENDAL_TELL;
		if (letter == 40)
			out[n++] = T_FAENDAL_REWARD;
	} else if (!strcmp(id, "camillavalerius")) {
		if (letter == 10 && letter_has("letter_sven"))
			out[n++] = T_CAMILLA_LIE_SVEN, out[n++] = T_CAMILLA_TRUTH_SVEN;
		if (letter == 20 && letter_has("letter_faendal"))
			out[n++] = T_CAMILLA_LIE_FAENDAL, out[n++] = T_CAMILLA_TRUTH_FAENDAL;
	} else if ((!strcmp(id, "alvor") && stage[Q_BEFORE_THE_STORM] == 8) ||
	           (!strcmp(id, "gerdur") && stage[Q_BEFORE_THE_STORM] == 7)) {
		// (Before the Storm's introductions missed - the opening's scene gone by in an older save:
		// told it all the same, as Skyrim's own topic does - INFO 00041F16)
		if (!intro_active())
			out[n++] = bts_asked ? T_BTS_FRIEND : T_BTS_HELP;
	} else if (!strcmp(id, "lucanvalerius")) { // The Golden Claw
		const int claw = items_find_name(T_MISC_00039647_FULL0);
		if (stage[Q_GOLDEN_CLAW] == 0)
			out[n++] = T_CLAW_ASK;
		if (stage[Q_GOLDEN_CLAW] == 5)
			out[n++] = T_CLAW_VOLUNTEER;
		if (stage[Q_GOLDEN_CLAW] < QUEST_DONE && claw >= 0 && items_have(claw) > 0)
			out[n++] = T_CLAW_RETURN;
	} else if (!strcmp(id, "uthgerd") && !brawl_active()) { // Uthgerd's brawl
		if (stage[Q_BRAWL] == 0)
			out[n++] = T_BRAWL_GIVE;
		if (stage[Q_BRAWL] == 5)
			out[n++] = T_BRAWL_ACCEPT, out[n++] = T_BRAWL_REJECT;
		if (stage[Q_BRAWL] == 20)
			out[n++] = T_BRAWL_PAID;
	}
	return n;
}

int quests_topics(int actor, const char **lines, int max) {
	static const char *const SAY[] = {T_DIAL_000D07FC_FULL0,
	                                  T_DIAL_000D50BB_FULL0,
	                                  T_DIAL_000D8B5C_FULL0, T_DIAL_0004D84E_FULL0,
	                                  T_DIAL_0005DD3B_FULL0, T_DIAL_0002F2C3_FULL0};
	int t[8];
	const int n = topics_of(actor, t);
	for (int i = 0; i < n && i < max; i++) {
		const int alvor = !strcmp(actors_id(actor), "alvor");
		const char *p = t[i] <= T_ARNGEIR ? SAY[t[i]]
		              : t[i] == T_BTS_HELP ? topics_prompt(alvor ? "bts_alvor" : "bts_gerdur")
		              : t[i] == T_BTS_FRIEND ? topics_prompt(alvor ? "bts_alvor_friend" : "bts_gerdur_friend")
		              : t[i] == T_RENT_ATTIC ? "I'd like to rent the attic room."
		              : t[i] == T_HORN_RETURN ? T_DIAL_000341B8_FULL0
		              : t[i] == T_DI_DELPHINE ? "You wanted to talk?"
		              : t[i] == T_DI_MALBORN ? (stage[Q_DIPLOMATIC] == 10 ? "Delphine sent me." : "I have the equipment.")
		              : t[i] == T_DI_STABLES ? "I'm ready to go to the party."
		              : t[i] == T_DI_PARTY_MALBORN ? T_DIAL_0002C182_FULL0 // (Skyrim's player lines: MQ201's topics)
		              : t[i] == T_DI_RAZELAN ? T_DIAL_000368D4_FULL0
		              : t[i] == T_DI_ETIENNE ? T_DIAL_00098D3E_FULL0
		              : t[i] == T_DI_OUTRO ? T_DIAL_0003BC88_FULL0
		              : t[i] == T_DI_FRIEND ? T_DIAL_000665CA_FULL0 // (Skyrim's MQ201PartyFriendDistractionTopic)
		              : t[i] == T_ER_BRELAS ? T_DIAL_00069F15_FULL0
		              : t[i] == T_ER_TALK_BRELAS ? T_DIAL_00069F8D_FULL0
		              : t[i] == T_BRELAS_FREE ? T_DIAL_0001F337_FULL0
		              : t[i] == T_RAT_BRYNJOLF ? "I'm looking for an old guy, probably hiding from the Thalmor." // (MQ202's)
		              : t[i] == T_RAT_VEKEL ? "I'm looking for an old guy, hiding out somewhere in the Ratway."
		              : t[i] == T_RAT_ESBERN ? T_DIAL_0003171E_FULL0
		              : t[i] == T_WALL_ESBERN ? T_DIAL_000ADBF4_FULL0 // (MQ203's)
		              : t[i] == T_WALL_DELPHINE ? T_DIAL_00016F5B_FULL0
		              : t[i] == T_THROAT_ARNGEIR ? T_DIAL_0003C1A5_FULL0 // (MQ204's)
		              : t[i] == T_ELDER_URAG ? T_DIAL_000E2801_FULL0 // (the Arcanaeum's)
		              : t[i] == T_ELDER_SEPTIMUS ? T_DIAL_000E2801_FULL0 // (DA04's)
		              : t[i] == T_FALLEN_ARNGEIR ? "How can I find out where Alduin went?" // (MQ301's)
		              : t[i] == T_FALLEN_JARL ? "I need to trap a dragon in Dragonsreach."
		              : t[i] == T_FALLEN_READY ? "I'm ready to trap the dragon."
		              : t[i] == T_EYRIE_FREE ? "Open the trap. I'm going to free the dragon." // (MQ303's)
		              : t[i] == T_SEASON_ARNGEIR ? "Will you help negotiate a truce?" // (MQ302's)
		              : t[i] == T_SEASON_TULLIUS ? "The Greybeards are holding a peace council at High Hrothgar."
		              : t[i] == T_SEASON_ULFRIC ? "The Greybeards are holding a peace council at High Hrothgar."
		              : t[i] == T_PAAR_DELPHINE ? "What did you want to talk about?" // (MQPaarthurnax's)
		              : t[i] == T_PAAR_DONE ? T_DIAL_0007734A_FULL0
		              : t[i] == T_TSUN ? T_DIAL_0001F4AC_FULL0 // (MQ304's)
		              : t[i] == T_HERO ? "Let's go. Alduin awaits."
		              : t[i] == T_TSUN_HOME ? "I'm ready to return to Skyrim." // (MQ305's)
		              : t[i] == T_INVEST ? "I'd like to invest in your store."
		              : topics_prompt(KEY[t[i]]);
		lines[i] = p[0] ? p : "...";
	}
	int everyday[8], m = n < max ? n : max; // (and their own everyday conversations, Skyrim's: after the quests')
	m += companions_topics(actor, lines + m, max - m); // (the Companions')
	m += college_topics(actor, lines + m, max - m);    // (the College's)
	m += thieves_topics(actor, lines + m, max - m);    // (the Thieves Guild's)
	const int e = dtree_everyday(actors_id(actor), everyday, 8);
	for (int k = 0; k < e && m < max; k++)
		lines[m++] = dtree_prompt(everyday[k]);
	return m;
}

static void letter_reward(int actor) {
	const int gold = FAVOR_REWARD_MEDIUM; // (Skyrim's FreeformRiverwood01: FavorRewardMedium)
	items_add(items_find_gold(), gold);
	char text[64];
	snprintf(text, sizeof(text), "%d Gold added", gold);
	ui_notice(text, 3.0f);
	followers_befriend(actor); // (a friend now: they'll follow you if you ask)
	set_stage(Q_LOVELY_LETTER, QUEST_DONE, "");
}

void quests_say(int actor, int topic) {
	int t[8];
	const int n = topics_of(actor, t);
	const char *theirs[16];
	const int c0 = companions_topics(actor, theirs, 16), c1 = college_topics(actor, theirs, 16),
	          c2 = thieves_topics(actor, theirs, 16), c = c0 + c1 + c2;
	if (topic >= n && topic < n + c0) { // (the Companions')
		companions_say(actor, topic - n);
		return;
	}
	if (topic >= n + c0 && topic < n + c0 + c1) { // (the College's)
		college_say(actor, topic - n - c0);
		return;
	}
	if (topic >= n + c0 + c1 && topic < n + c) { // (the Thieves Guild's)
		thieves_say(actor, topic - n - c0 - c1);
		return;
	}
	if (topic >= n + c) { // (one of their everyday conversations)
		int everyday[8];
		const int e = dtree_everyday(actors_id(actor), everyday, 8);
		if (topic - n - c < e)
			dtree_start(everyday[topic - n - c], actor);
		return;
	}
	if (topic < 0)
		return;
	if (t[topic] > T_ARNGEIR && t[topic] != T_BRAWL_ACCEPT && t[topic] < T_BTS_HELP)
		topics_say(KEY[t[topic]], actor, talk_name(actor));
	switch (t[topic]) {
	case T_HELGEN: // (Before the Storm: the Jarl hears of Helgen, rewards you, sends you to Farengar)
		play("mq102_jarl");
		if (jarl_step == JARL_SEATED || jarl_step == JARL_FREE) // (INFO 000D553E - seated or not: asked
			jarl_step = JARL_RISE; // before he'd sat down (the user's Nov 7 game: on his throne 2 minutes after), he stayed put
		reward("reward_heavy", "reward_light");
		set_stage(Q_BEFORE_THE_STORM, QUEST_DONE, "");
		set_stage(Q_BLEAK_FALLS_BARROW, 10, "Speak to Farengar");
		break;
	case T_PROJECT:
		if (jarl_step == JARL_RISE || jarl_step == JARL_TO_FARENGAR) // (asked before the Jarl got there: his line's not wanted after)
			jarl_step = JARL_WAIT;
		project_told();
		break;
	case T_STONE: {
		play("mq103_farengar_stone");
		const int stone = qitem("dragonstone", NULL);
		if (stone >= 0)
			items_add(stone, -items_have(stone));
		set_stage(Q_BLEAK_FALLS_BARROW, QUEST_DONE, "");
		set_stage(Q_DRAGON_RISING, 10, "Speak to Jarl Balgruuf");
		break;
	}
	case T_DRAGON: // (Dragon Rising: the Jarl's gift for the stone, to the watchtower)
		play("mq104_jarl");
		reward("gift_heavy", "gift_light");
		set_stage(Q_DRAGON_RISING, 20, "Go to the Western Watchtower");
		break;
	case T_KILLED: { // (Thane of Whiterun, Lydia your housecarl; the Greybeards' summons)
		play("mq104_jarl_outro");
		set_stage(Q_DRAGON_RISING, QUEST_DONE, "");
		ui_notice("You are now Thane of Whiterun", 4.0f);
		reward("thane_axe", "thane_axe"); // (and the Jarl's weapon: MQ104's JarlWeapon, FavorRewardEnchAxeBest)
		const int lydia = actor_named("housecarlwhiterun");
		if (lydia >= 0)
			followers_assign(lydia);
		set_stage(Q_WAY_OF_THE_VOICE, 10, "Travel to High Hrothgar");
		break;
	}
	case T_RENT_ATTIC: // (INFO 000A56D4)
		play("mq106_rent");
		horn_step = 1;
		break;
	case T_DI_DELPHINE: // (INFO 00035D52 ... INFO 0005F702)
		play("mq201_intro");
		di_step = 1;
		break;
	case T_DI_MALBORN: // (INFO 000361DD INFO 000361E5)
		if (stage[Q_DIPLOMATIC] == 10)
			play("mq201_malborn");
		di_step = 2;
		break;
	case T_DI_STABLES: // (INFO 000361E3 INFO 000361DE)
		play("mq201_stables");
		di_step = 4;
		break;
	case T_DI_PARTY_MALBORN: // (INFO 000362CB)
		play("mq201_party_malborn");
		di_step = 10;
		break;
	case T_DI_RAZELAN: // (DIAL 000368D4 - his answers: DIAL 00036D48)
		play("mq201_razelan");
		di_step = 11; // (till DIAL 00036D47: dtree_take_reached)
		break;
	case T_DI_ETIENNE: // (INFO 00098F7F)
		play("mq201_etienne");
		di_step = 20;
		break;
	case T_DI_OUTRO: // (INFO 000A791E)
		play("mq201_outro");
		di_step = 30;
		break;
	case T_DI_FRIEND: { // (INFO 000C0892 - their tree: yes, no, or your persuasion)
		char key[64];
		snprintf(friend_id, sizeof(friend_id), "%s", actors_id(actor));
		snprintf(key, sizeof(key), "mq201_friend_%s", friend_id);
		friend_ok = 0;
		play(key);
		di_step = 40;
		break;
	}
	case T_ER_BRELAS: // (INFO 00069F26)
		play("mq201_erikur");
		break;
	case T_ER_TALK_BRELAS: // (INFO 00069FA1)
		play("mq201_brelas");
		di_step = 43;
		break;
	case T_BRELAS_FREE: // (INFO 00053A57 - free her, or not)
		play("mq201_brelas_prisoner");
		break;
	case T_RAT_BRYNJOLF: // (INFO 0003B69A)
		play("mq202_brynjolf");
		rat_step = 1;
		break;
	case T_RAT_VEKEL: // (his price, your persuasion or your fists: INFO 0006C861)
		play("mq202_vekel");
		rat_step = 2;
		break;
	case T_RAT_ESBERN: // (INFO 000318F9 - a line of theirs)
		play("mq202_esbern");
		rat_step = 6;
		break;
	case T_WALL_ESBERN: // (INFO 000ADD12)
		play("mq203_esbern");
		wall_step = 2;
		break;
	case T_WALL_DELPHINE: // (INFO 00016F9E)
		play("mq203_delphine_wall");
		wall_step = 7;
		break;
	case T_ELDER_URAG: // (INFO 0003277F)
		play("da04_urag");
		elder_step = 1;
		break;
	case T_ELDER_SEPTIMUS: // (INFO 0001A302)
		play("da04_septimus");
		elder_step = 2;
		break;
	case T_FALLEN_ARNGEIR: // (INFO 00026741)
		play("mq301_arngeir");
		break;
	case T_FALLEN_JARL: // (INFO 00045D05)
		play("mq301_jarl");
		fallen_step = 2;
		break;
	case T_FALLEN_READY: // (INFO 000D2540)
		play("mq301_jarl_ready");
		fallen_step = 4;
		break;
	case T_EYRIE_FREE: // (INFO 0004DE41)
		play("mq303_gatekeeper");
		eyrie_step = 1;
		break;
	case T_SEASON_ARNGEIR:
		play("mq302_arngeir");
		season_step = 1;
		break;
	case T_SEASON_TULLIUS:
		play("mq302_tullius");
		season_step = 2;
		break;
	case T_SEASON_ULFRIC:
		play("mq302_ulfric");
		season_step = 3;
		break;
	case T_PAAR_DELPHINE: // (INFO 00042847)
		play("mqp_delphine");
		paar_step = 1;
		break;
	case T_PAAR_DONE:
		play("mqp_delphine_done");
		paar_step = 2;
		break;
	case T_TSUN: // (INFO 0005195B)
		play("mq304_tsun");
		sov_step = 1;
		break;
	case T_HERO:
		play("mq304_hero");
		sov_step = 3;
		break;
	case T_TSUN_HOME:
		slayer_step = 3;
		break;
	case T_INVEST:
		if (items_vendor_invest(items_vendor(actors_id(actor))))
			ui_notice("You invested 500 gold in the store", 3.0f);
		break;
	case T_THROAT_ARNGEIR: // (INFO 0003C1A7)
		play("mq204_arngeir");
		throat_step = 1;
		break;
	case T_HORN_RETURN: // (a line of theirs)
		play("mq105_horn_return");
		{
			const int horn = qitem("horn", NULL);
			if (horn >= 0)
				items_add(horn, -items_have(horn));
		}
		greet_step = 1;
		break;
	case T_ARNGEIR: // (INFO 0002F2C5)
		play("mq105_intro");
		set_stage(Q_WAY_OF_THE_VOICE, 30, "Shout at the Greybeards (Unrelenting Force)");
		break;
	// A Lovely Letter: Sven and Faendal, each with a forgery for Camilla
	case T_SVEN_INTRO:
		stage[Q_LOVELY_LETTER] = stage[Q_LOVELY_LETTER] == 2 ? 3 : 1, reopen_actor = actor;
		break;
	case T_FAENDAL_INTRO:
		stage[Q_LOVELY_LETTER] = stage[Q_LOVELY_LETTER] == 1 ? 3 : 2, reopen_actor = actor;
		break;
	case T_SVEN_GIVE:
		letter_take("letter_sven", 1);
		set_stage(Q_LOVELY_LETTER, 10, T_QUST_00074A83_NNAM2);
		break;
	case T_FAENDAL_GIVE:
		letter_take("letter_faendal", 1);
		set_stage(Q_LOVELY_LETTER, 20, T_QUST_00074A83_NNAM0);
		break;
	case T_FAENDAL_TELL: // (Sven's letter shown to Faendal: his own instead)
		letter_take("letter_sven", -1);
		letter_take("letter_faendal", 1);
		set_stage(Q_LOVELY_LETTER, 20, T_QUST_00074A83_NNAM0);
		break;
	case T_SVEN_TELL:
		letter_take("letter_faendal", -1);
		letter_take("letter_sven", 1);
		set_stage(Q_LOVELY_LETTER, 10, T_QUST_00074A83_NNAM2);
		break;
	case T_CAMILLA_LIE_SVEN: // (she believes Faendal wrote it: Sven's won)
	case T_CAMILLA_TRUTH_FAENDAL:
		letter_take(t[topic] == T_CAMILLA_LIE_SVEN ? "letter_sven" : "letter_faendal", -1);
		set_stage(Q_LOVELY_LETTER, 30, T_QUST_00074A83_NNAM5);
		break;
	case T_CAMILLA_TRUTH_SVEN:
	case T_CAMILLA_LIE_FAENDAL:
		letter_take(t[topic] == T_CAMILLA_TRUTH_SVEN ? "letter_sven" : "letter_faendal", -1);
		set_stage(Q_LOVELY_LETTER, 40, T_QUST_00074A83_NNAM4);
		break;
	case T_SVEN_REWARD:
	case T_FAENDAL_REWARD:
		letter_reward(actor);
		break;
	// Uthgerd's brawl: a hundred gold on it
	case T_BRAWL_GIVE:
		stage[Q_BRAWL] = 5, reopen_actor = actor;
		break;
	case T_BRAWL_ACCEPT:
		if (items_gold() < BRAWL_BET) { // (INFO 00055A0C)
			topics_say("brawl_poor", actor, talk_name(actor));
			stage[Q_BRAWL] = 0;
		} else {
			topics_say("brawl_accept", actor, talk_name(actor)); // (INFO 00055A0B)
			brawl_pending = actor;
		}
		break;
	case T_BRAWL_REJECT:
		stage[Q_BRAWL] = 0;
		break;
	// The Golden Claw: Lucan's break-in; his claw back from Bleak Falls Barrow
	case T_CLAW_ASK:
		stage[Q_GOLDEN_CLAW] = 5, reopen_actor = actor;
		break;
	case T_CLAW_VOLUNTEER: // (INFO 0008AFA1)
		set_stage(Q_GOLDEN_CLAW, 10, T_QUST_00039645_NNAM0);
		break;
	case T_CLAW_RETURN: {
		const int claw = items_find_name(T_MISC_00039647_FULL0);
		if (claw >= 0)
			items_add(claw, -1);
		reward("claw_reward", "claw_reward"); // (Skyrim's MS13: his reward, LvlQuestReward02Medium - rolled at your level)
		if (!stage[Q_GOLDEN_CLAW])
			stage[Q_GOLDEN_CLAW] = 20; // (found before he asked: done all the same)
		set_stage(Q_GOLDEN_CLAW, QUEST_DONE, "");
		break;
	}
	case T_BTS_HELP: // (INFO 00041F16)
		topics_say(!strcmp(actors_id(actor), "alvor") ? "bts_alvor" : "bts_gerdur", actor, talk_name(actor));
		if (dtree_busy()) // (the whole of it as a conversation: to the Jarl once it's got there - INFO 000D2327)
			break;
		bts_asked = 1, reopen_actor = actor;
		break;
	case T_BTS_FRIEND: // (DIAL 00041F0C) - and to the Jarl for help
		topics_say(!strcmp(actors_id(actor), "alvor") ? "bts_alvor_friend" : "bts_gerdur_friend", actor, talk_name(actor));
		bts_asked = 0;
		set_stage(Q_BEFORE_THE_STORM, 10, "Speak to Jarl Balgruuf in Dragonsreach");
		break;
	case T_BRAWL_PAID:
		items_add(items_find_gold(), BRAWL_BET);
		ui_notice("100 Gold added", 3.0f);
		followers_befriend(actor); // (INFO 000C0B77)
		set_stage(Q_BRAWL, QUEST_DONE, "");
		break;
	}
}

// The Dragonstone: on Bleak Falls Sanctum's last draugr (the one that climbs out of the
// sarcophagus by the word wall) - in his body when it's searched, or the chest by him

void quests_body_searched(int handle, int interior) {
	if (creatures_body_side(items_container_ref(handle)) == 5) { // (the escape scene's guards: a trapdoor key each - Skyrim's EscapeKey1/2)
		const int key = qitem("exit_key", NULL);
		int its[64], counts[64], has = 0;
		const int k = items_in_container(handle, its, counts, 64);
		for (int j = 0; j < k; j++)
			has |= its[j] == key;
		if (key >= 0 && !has)
			items_container_add(handle, key, 1);
	}
	const int stone = qitem("dragonstone", NULL);
	if (stage[Q_BLEAK_FALLS_BARROW] < 30 && !stone_given && stone >= 0 && interior >= 0 &&
	    interior == interior_find(SANCTUM) && items_have(stone) <= 0 &&
	    hypotf(last_feet[0] - BOSS_AT[0], last_feet[2] - BOSS_AT[1]) < BOSS_NEAR_M) {
		items_container_add(handle, stone, 1);
		stone_given = 1;
	}
}

void quests_container_opened(int handle, int interior) {
	companions_container_opened(interior, last_feet); // (Driftshade's box: the fragments of Wuuthrad)
	const int stone = qitem("dragonstone", NULL);
	if (stage[Q_BLEAK_FALLS_BARROW] < 30 && !stone_given && stone >= 0 && interior >= 0 && items_have(stone) <= 0 &&
	    hypotf(last_feet[0] - BOSS_AT[0], last_feet[2] - BOSS_AT[1]) < BOSS_NEAR_M &&
	    interior == interior_find(SANCTUM)) {
		items_container_add(handle, stone, 1); // (in the chest, as Skyrim puts it there)
		stone_given = 1;
	}
	if (stage[Q_DIPLOMATIC] >= 55 && stage[Q_DIPLOMATIC] < QUEST_DONE && items_container_ref(handle) == EMBASSY_GEAR_CHEST) {
		const int satchel = items_open_container(MALBORN_SATCHEL_REF, -1); // (what Malborn smuggled in: in the larder's chest)
		int its[64], counts[64];
		const int k = items_in_container(satchel, its, counts, 64);
		for (int i = 0; i < k; i++) {
			items_take(satchel, its[i], counts[i]);
			items_put(handle, its[i], counts[i]);
		}
	}
	if (stage[Q_DIPLOMATIC] == 60 && items_container_ref(handle) == EMBASSY_OFFICE_CHEST) {
		static const char *const PAPERS[] = {"esbern_dossier", "elenwen_note"}; // (Elenwen's chest: MQ201ElenwenOfficeChest's)
		for (int i = 0; i < 2; i++) {
			const int it = qitem(PAPERS[i], NULL);
			int its[64], counts[64], has = 0;
			const int k = items_in_container(handle, its, counts, 64);
			for (int j = 0; j < k; j++)
				has |= its[j] == it;
			if (it >= 0 && !has && items_have(it) <= 0)
				items_container_add(handle, it, 1);
		}
	}
}

// A quest place's marker: it where you are (its interior, or outdoors for an outdoor one), else the
// map's marker of the place it's in
static int qplace_marker(const char *key, const char *place, float out[2]) {
	const QPlace *q = qplace(key);
	const int here = q && place_here(q, interior_current());
	if (here) {
		out[0] = q->pos[0], out[1] = q->pos[2];
		return 1;
	}
	return marker_named(place, out);
}

static int quests_who_marker(const char *who, float out[2]) {
	float at[3];
	const int i = actor_named(who);
	if (i >= 0 && actors_shown(i, at)) {
		out[0] = at[0], out[1] = at[2];
		return 1;
	}
	return 0;
}

static int main_marker(float out[2]);
void quests_claw_learned(void) { // (heard of the claw: the bandits' talk, Arvel in the web - Skyrim's MS13 stage 12)
	if (!stage[Q_GOLDEN_CLAW])
		set_stage(Q_GOLDEN_CLAW, 12, T_QUST_00039645_NNAM0);
}

static int marker_only = -1; // (main_marker for that quest alone: the journal's markers, one per active quest)
#define MS(q) (marker_only < 0 || marker_only == (q) ? stage[q] : -1)

// Skyrim's journal: a quest set inactive (X in the Quests page) has no marker; by its name's hash, kept with
// the game (save.h quests_inactive) - any quest, the guilds' too
static uint32_t inactive[QUESTS_INACTIVE_MAX];

static uint32_t name_hash(const char *s) {
	uint32_t h = 2166136261u;
	for (; s && *s; s++)
		h = (h ^ (uint8_t)*s) * 16777619u;
	return h ? h : 1;
}

int quests_is_active(const char *name) {
	const uint32_t h = name_hash(name);
	for (int i = 0; i < QUESTS_INACTIVE_MAX; i++)
		if (inactive[i] == h)
			return 0;
	return 1;
}

void quests_toggle_active(const char *name) {
	const uint32_t h = name_hash(name);
	for (int i = 0; i < QUESTS_INACTIVE_MAX; i++)
		if (inactive[i] == h) {
			inactive[i] = 0;
			game_log("quest: %s active again", name);
			return;
		}
	for (int i = 0; i < QUESTS_INACTIVE_MAX; i++)
		if (!inactive[i]) {
			inactive[i] = h;
			game_log("quest: %s inactive", name);
			return;
		}
}

void quests_save_inactive(uint32_t out[QUESTS_INACTIVE_MAX]) {
	memcpy(out, inactive, sizeof(inactive));
}

void quests_load_inactive(const uint32_t *in) { // (NULL: an older save - every quest active)
	if (in)
		memcpy(inactive, in, sizeof(inactive));
	else
		memset(inactive, 0, sizeof(inactive));
}

// The current quest of a guild's line (its first not done), active?
static int guild_active(int (*journal)(const char **, const char **, int *, int)) {
	const char *names[16], *objectives[16];
	int done[16];
	const int n = journal(names, objectives, done, 16);
	for (int i = 0; i < n; i++)
		if (!done[i])
			return quests_is_active(names[i]);
	return 1;
}

// Every active quest's objective marker (Skyrim's journal: each one on the compass and the map), the same place once
int quests_markers(float out[][2], int max) {
	int n = 0;
	for (int q = 0; q < QUEST_COUNT && n < max; q++) {
		if (!stage[q] || stage[q] >= QUEST_DONE || !quests_is_active(NAMES[q]))
			continue;
		marker_only = q;
		float at[2];
		if (main_marker(at)) {
			int dup = 0;
			for (int k = 0; k < n; k++)
				dup |= hypotf(out[k][0] - at[0], out[k][1] - at[1]) < 1.0f;
			if (!dup)
				out[n][0] = at[0], out[n][1] = at[1], n++;
		}
	}
	marker_only = -1;
	int (*const marker[3])(float[2]) = {companions_marker, college_marker, thieves_marker};
	int (*const journal[3])(const char **, const char **, int *, int) = {companions_journal, college_journal, thieves_journal};
	for (int g = 0; g < 3 && n < max; g++)
		if (guild_active(journal[g]) && marker[g](out[n]))
			n++;
	return n;
}

int quests_marker(float out[2]) { // (one: Clairvoyance's - the first active quest's, the main quest's before the guilds')
	float m[8][2];
	if (quests_markers(m, 8) <= 0)
		return 0;
	out[0] = m[0][0], out[1] = m[0][1];
	return 1;
}

static int main_marker(float out[2]) {
	const char *who = NULL, *place = NULL;
	if (MS(Q_UNBOUND) == 11 || MS(Q_UNBOUND) == 21)
		who = "ralof";
	else if (MS(Q_UNBOUND) == 12 || MS(Q_UNBOUND) == 22)
		who = "hadvar";
	else if (MS(Q_BEFORE_THE_STORM) == 5 || MS(Q_BEFORE_THE_STORM) == 6)
		place = T_LCTN_00013163_FULL0;
	else if (MS(Q_BEFORE_THE_STORM) == 7)
		who = "gerdur";
	else if (MS(Q_BEFORE_THE_STORM) == 8)
		who = "alvor";
	else if (MS(Q_BEFORE_THE_STORM) == 10 || MS(Q_DRAGON_RISING) == 10 || MS(Q_DRAGON_RISING) == 40)
		who = "balgruufthegreater";
	else if (MS(Q_BLEAK_FALLS_BARROW) == 10 || MS(Q_BLEAK_FALLS_BARROW) == 30)
		who = "farengarsecretfire";
	else if (MS(Q_BLEAK_FALLS_BARROW) == 20)
		place = T_QUST_000D0800_FULL0;
	else if (MS(Q_DRAGON_RISING) == 20 || MS(Q_DRAGON_RISING) == 30)
		place = T_LCTN_000D9079_FULL0;
	else if (MS(Q_BLADE) == 20 || MS(Q_BLADE) == 30) { // (Kynesgrove's dragon mound)
		const QPlace *q = qplace("kynesgrove_mound");
		if (q) {
			out[0] = q->pos[0], out[1] = q->pos[2];
			return 1;
		}
		place = T_LCTN_00018A4E_FULL0;
	} else if (MS(Q_DIPLOMATIC) == 5)
		who = "delphine";
	else if (MS(Q_DIPLOMATIC) == 10 || MS(Q_DIPLOMATIC) == 20)
		who = "malborn";
	else if (MS(Q_DIPLOMATIC) == 30) {
		const QPlace *q = qplace("solitude_stables");
		if (q) {
			out[0] = q->pos[0], out[1] = q->pos[2];
			return 1;
		}
		place = T_LCTN_0001705C_FULL0;
	} else if (MS(Q_HORN) == 10)
		place = T_LCTN_000192BF_FULL0;
	else if (MS(Q_HORN) == 30)
		who = "delphine";
	else if (MS(Q_HORN) == 40)
		who = "arngeir";
	else if (MS(Q_WAY_OF_THE_VOICE) == 10)
		place = T_LCTN_00018E34_FULL0; // (the Greybeards' summons: up the 7,000 Steps)
	else if (wv_step == 1 && qplace("hh_centre")) { // (INFO 000C5228)
		const QPlace *q = qplace("hh_centre");
		out[0] = q->pos[0], out[1] = q->pos[2];
		return 1;
	}
	else if (MS(Q_WAY_OF_THE_VOICE) >= 20 && MS(Q_WAY_OF_THE_VOICE) < QUEST_DONE)
		who = MS(Q_WAY_OF_THE_VOICE) == 40 ? "borri" : MS(Q_WAY_OF_THE_VOICE) == 20 ? "arngeir" : "einarth";
	else if (MS(Q_RAT) == 10) // (A Cornered Rat: Riften's market, the Ragged Flagon, the Warrens' door)
		who = "brynjolf";
	else if (MS(Q_RAT) == 20)
		who = "vekel";
	else if (MS(Q_RAT) == 30)
		return qplace_marker("warrens_door", T_DIAL_01002EFC_FULL0, out);
	else if (MS(Q_RAT) == 40 || (MS(Q_WALL) == 20 && wall_step < 2))
		who = "esbern";
	else if (MS(Q_WALL) == 10) // (Alduin's Wall: the Sleeping Giant, Karthspire, the Wall)
		place = T_LCTN_00013163_FULL0;
	else if (MS(Q_WALL) == 30)
		return interior_current() >= 0 ? qplace_marker("skyhaven_seal", T_LCTN_0001925F_FULL0, out) : marker_named(T_LCTN_0001925F_FULL0, out);
	else if (MS(Q_WALL) == 40)
		return wall_step == 6 ? quests_who_marker("delphine", out) : qplace_marker("skyhaven_wall", T_LCTN_0001925F_FULL0, out);
	else if (MS(Q_THROAT) == 10)
		who = "arngeir";
	else if (MS(Q_THROAT) == 20) // (the Throat of the World: the courtyard, the path's storm, Paarthurnax)
		return qplace_marker("hh_courtyard", T_LCTN_00018E34_FULL0, out);
	else if (MS(Q_THROAT) == 25)
		return qplace_marker("throat_storm", T_LCTN_00018E34_FULL0, out);
	else if (MS(Q_THROAT) >= 30 && MS(Q_THROAT) < QUEST_DONE)
		return qplace_marker("throat_paarthurnax", T_LCTN_000192BB_FULL0, out);
	else if (MS(Q_ELDER) == 10) // (Elder Knowledge: the Arcanaeum, Septimus's outpost, the Tower of Mzark)
		who = "uraggroshub";
	else if (MS(Q_ELDER) == 20)
		return interior_current() >= 0 ? quests_who_marker("da04septimus", out) || qplace_marker("septimus", T_LCTN_0002D51C_FULL0, out)
		                               : qplace_marker("septimus_outside", T_LCTN_0002D51C_FULL0, out);
	else if (MS(Q_ELDER) == 50)
		return qplace_marker("mzark_scroll", T_LCTN_00018EE1_FULL0, out);
	else if (MS(Q_BANE) >= 10 && MS(Q_BANE) < QUEST_DONE) // (Alduin's Bane: the Time-Wound)
		return qplace_marker("time_wound", T_LCTN_000192BB_FULL0, out);
	else if (MS(Q_FALLEN) == 10 || MS(Q_FALLEN) == 30)
		return qplace_marker("throat_paarthurnax", T_LCTN_000192BB_FULL0, out);
	else if (MS(Q_SEASON) == 10 || MS(Q_SEASON) == 40)
		who = "arngeir";
	else if (MS(Q_SEASON) == 20)
		who = "generaltullius";
	else if (MS(Q_SEASON) == 30)
		who = "ulfric";
	else if (MS(Q_SEASON) == 50 || MS(Q_SEASON) == 60)
		return qplace_marker("council_player", T_LCTN_00018E34_FULL0, out);
	else if (MS(Q_FALLEN) == 20 || MS(Q_FALLEN) == 40 || MS(Q_EYRIE) == 10)
		who = "balgruufthegreater";
	else if (MS(Q_FALLEN) >= 50 && MS(Q_FALLEN) < QUEST_DONE)
		return qplace_marker("porch_trap", T_DIAL_01002EFE_FULL0, out);
	else if (MS(Q_EYRIE) == 20)
		return qplace_marker("porch_trap", T_DIAL_01002EFE_FULL0, out);
	else if (MS(Q_EYRIE) == 30 || MS(Q_EYRIE) == 40)
		return qplace_marker("skuldafn_portal", T_LCTN_000192AB_FULL0, out);
	else if (MS(Q_SOVNGARDE) == 10 || MS(Q_SOVNGARDE) == 20 || MS(Q_DRAGONSLAYER) == 30)
		return qplace_marker("sovngarde_tsun", "", out);
	else if (MS(Q_SOVNGARDE) == 30)
		return interior_current() >= 0 ? qplace_marker("hall_heroes", "", out) : qplace_marker("sovngarde_bridge_end", "", out);
	else if (MS(Q_DRAGONSLAYER) == 10 || MS(Q_DRAGONSLAYER) == 20)
		return qplace_marker("sovngarde_battle", "", out);
	else if (MS(Q_PAARTHURNAX) == 10)
		return qplace_marker("throat_paarthurnax", T_LCTN_000192BB_FULL0, out);
	else if (MS(Q_PAARTHURNAX) == 20)
		who = "delphine";
	else if (MS(Q_GOLDEN_CLAW) == 10 || MS(Q_GOLDEN_CLAW) == 12) { // (Arvel - or his body - when you're in the barrow with him)
		float at[3];
		const char *here = interior_id();
		if (here && !strncmp(here, "bleakfallsbarrow", 16) && creatures_find_titled(T_NPC_00039646_FULL0, at)) {
			out[0] = at[0], out[1] = at[2];
			return 1;
		}
		place = T_QUST_000D0800_FULL0;
	} else if (MS(Q_GOLDEN_CLAW) == 20 || MS(Q_GOLDEN_CLAW) == 35)
		who = "lucanvalerius";
	if (who) { // (in sight: them; else where they live)
		float at[3];
		const int i = actor_named(who);
		if (i >= 0 && actors_shown(i, at)) {
			out[0] = at[0], out[1] = at[2];
			return 1;
		}
		place = !strcmp(who, "brynjolf") || !strcmp(who, "vekel") || (!strcmp(who, "esbern") && MS(Q_RAT) < QUEST_DONE) ? T_DIAL_01002EFC_FULL0
		      : !strcmp(who, "esbern") ? T_LCTN_00013163_FULL0
		      : !strcmp(who, "gerdur") || !strcmp(who, "alvor") || !strcmp(who, "lucanvalerius") || !strcmp(who, "delphine") ? T_LCTN_00013163_FULL0
		      : !strcmp(who, "ralof") || !strcmp(who, "hadvar") ? NULL
		      : !strcmp(who, "arngeir") || !strcmp(who, "einarth") || !strcmp(who, "borri") ? T_LCTN_00018E34_FULL0
		      : !strcmp(who, "generaltullius") ? "Solitude" : !strcmp(who, "ulfric") ? T_DIAL_01002EFD_FULL0
		      : !strcmp(who, "uraggroshub") ? T_FACT_0001F259_FULL0 : T_DIAL_01002EFE_FULL0;
	}
	return place && marker_named(place, out);
}

int quests_journal(const char **names, const char **objectives, int *done, int max) {
	int n = 0;
	for (int q = 0; q < QUEST_COUNT && n < max; q++) {
		if (!stage[q])
			continue;
		names[n] = NAMES[q], objectives[n] = stage[q] >= QUEST_DONE ? T_GMST_00017026_DATA0 : objective(q);
		done[n++] = stage[q] >= QUEST_DONE;
	}
	n += companions_journal(names + n, objectives + n, done + n, max - n); // (the Companions')
	n += college_journal(names + n, objectives + n, done + n, max - n); // (the College's)
	return n + thieves_journal(names + n, objectives + n, done + n, max - n); // (the Thieves Guild's)
}

void quests_set_vitals(Vitals *v) {
	vitals_now = v;
}

// --- The kit the side quests' files share (questkit.h) ----------------------------------------

const QPlace *qk_place(const char *key) {
	return qplace(key);
}

int qk_place_here(const QPlace *q, int interior) {
	return place_here(q, interior);
}

int qk_at(const char *key, const float feet[3], int interior, float m) {
	return at_qplace(key, feet, interior, m);
}

int qk_place_marker(const char *key, const char *place, float out[2]) {
	return qplace_marker(key, place, out);
}

int qk_marker_named(const char *name, float out[2]) {
	return marker_named(name, out);
}

int qk_who_marker(const char *who, float out[2]) {
	return quests_who_marker(who, out);
}

void qk_play(const char *key) {
	play(key);
}

int qk_busy(void) {
	return quests_busy();
}

int qk_actor(const char *id) {
	return actor_named(id);
}

int qk_beside(const char *id, const float feet[3], float side_m) {
	return actor_beside(id, feet, side_m);
}

void qk_ally_fights(int ally, float dt, const float feet[3], float *timer) {
	ally_fights_by(ally, dt, feet, timer);
}

void qk_move_to(const char *key) {
	move_to_place(key);
}

int qk_moving(void) {
	return move_pending || move_settle > 0.0f;
}

int qk_item(const char *name) {
	return qitem(name, NULL);
}

int qk_has(const char *name) {
	return letter_has(name);
}

void qk_give(const char *name, int count) {
	game_log("quest: %s %+d", name, count);
	if (count > 0) {
		letter_take(name, count);
		return;
	}
	const int it = qitem(name, NULL);
	if (it >= 0 && count < 0 && items_have(it) > 0)
		items_add(it, count);
}

void qk_reward(const char *heavy, const char *light) {
	reward(heavy, light);
}
