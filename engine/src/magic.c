#include "magic.h"
#include "activators.h"
#include "gamelog.h"

#include <math.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#include "actors.h"
#include "collision.h"
#include "creatures.h"
#include "people.h"
#include "effects.h"
#include "fileio.h"
#include "items.h"
#include "equip.h"
#include "projectiles.h"
#include "quests.h"
#include "spellfx.h"
#include "perks.h"
#include "traps.h"
#include "sfx.h"
#include "skills.h"
#include "ui.h"
#include "gametext.h"

#define CANDLE_FOLLOW 3.0f   // per second: the light drifting after you
#define FLAME_REACH_M 3.5f   // from the hand (aimed from the eye: a little farther)
#define SPARK_REACH_M 8.0f
#define RUNE_REACH_M 20.0f   // laid where you aim, this far at most
#define RUNE_TRIGGER_M 1.0f  // someone this close sets it off
#define RUNE_BURST_M 3.4f    // its explosion (Skyrim's TrapExplosionRuneFire01: 240 units)
#define SPELL_RADIUS_M 0.3f  // a stream's width, for striking creatures
#define SCROLL_MIN_CHARGE_S 0.6f // a scroll held this long at least before it's let go (Skyrim's
                                 //  scrolls have their own charge: Fireball's 0, Blizzard's 3)
#define FIRE_BOLT_SPEED_MS 26.0f // a Fireball in flight (Skyrim's FireballProjectile ~1800 units/s)
#define FROST_BOLT_SPEED_MS 14.0f
#define SHOCK_BOLT_SPEED_MS 60.0f
#define SPELL_MIN_CHARGE_S 0.25f // (a fire-and-forget spell: held at least this long)
#define ILLUSION_REACH_M 40.0f   // Fear, Calm, Paralyze: as far as they fly
#define LIGHT_REACH_M 30.0f      // Magelight: where it strikes, this far at most
#define CLOAK_RADIUS_M 2.5f      // a cloak burns those within (Skyrim's: melee range)
#define STORM_MAX_M 31.0f        // a blast's reach at most (Fire Storm's: 100 feet)
#define HAZARD_TICK_S 0.5f       // a storm laid (Blizzard's FrostBlizzardSpellHazard03): it touches those in it this often,
#define HAZARD_HOLD_S 5.0f       // its cold holding on this long (HazardFrostBlizzardSpell03: 5 s), slowing them as long
#define SPELL_MAX (SPELL_COUNT + 120) // (Skyrim's and its add-ons' tomes: 103)
#define DUAL_EFFECT 2.2f         // dual cast: its power raised this much (Skyrim's fMagicDualCastingEffectivenessBase)...
#define DUAL_COST 2.8f           // ...for this much the magicka (fMagicDualCastingCostMult)
#define DUAL_WINDOW_S 0.15f      // a hand's press waits this long for the other's to dual cast
#define PERK_IMPACT 0x000153d2u  // (Skyrim's Impact: a dual-cast Destruction bolt staggers)

// How the engine casts the tome spells beyond its own seven (spells.txt X lines' kind)
enum { KIND_NONE, KIND_BOLT_FIRE, KIND_BOLT_FROST, KIND_BOLT_SHOCK, KIND_STREAM_FROST, KIND_RUNE_FROST, KIND_RUNE_SHOCK,
       KIND_HEAL, KIND_ARMOUR, KIND_INVISIBILITY, KIND_WATERBREATHING, KIND_LIGHT, KIND_CLOAK_FIRE, KIND_CLOAK_FROST,
       KIND_CLOAK_SHOCK, KIND_STORM_FIRE, KIND_STORM_FROST, KIND_STORM_SHOCK, KIND_FEAR, KIND_CALM, KIND_PARALYZE,
       KIND_TURN, KIND_FRENZY, KIND_CLAIRVOYANCE, KIND_SUMMON, KIND_REANIMATE, KIND_BOUND, KIND_WARD, KIND_DETECT,
       KIND_TELEKINESIS, KIND_RALLY, KIND_HEAL_OTHER, KIND_TRANSMUTE, KIND_SOULTRAP, KIND_EQUILIBRIUM, KINDS };
#define PERSON_STRUCK_M 0.8f // (a bolt's touch on whoever's there: a townsperson, if no creature)

// A stream's or a bolt's ray: the creature it reaches first, else a townsperson (people.c: Flames
// on Whiterun's folk is assault, as your blade is)
static float struck_ray(const float eye[3], const float dir[3], float reach, float radius, float damage) {
	const float c = creatures_ray(eye, dir, reach, radius, damage);
	return c >= 0.0f ? c : people_ray(eye, dir, reach, radius, damage);
}

static const char *const KIND_NAMES[KINDS] = {"none", "bolt_fire", "bolt_frost", "bolt_shock", "stream_frost",
                                              "rune_frost", "rune_shock", "heal", "armour", "invisibility",
                                              "waterbreathing", "light", "cloak_fire", "cloak_frost", "cloak_shock",
                                              "storm_fire", "storm_frost", "storm_shock", "fear", "calm", "paralyze",
                                              "turn", "frenzy", "clairvoyance", "summon", "reanimate", "bound", "ward",
                                              "detect", "telekinesis", "rally", "heal_other", "transmute", "soultrap",
                                              "equilibrium"};

// Skyrim's spells (pack/spells.txt, converter/perks.py): cost (per second when held),
// magnitude (health a second, damage, a light's radius...), duration, school, the casting
// perk that halves its cost (Novice Destruction...), its effect's keywords (perks ask: fire
// damage...), its burst. The defaults are Skyrim's, for a pack without the file.
typedef struct {
	char key[48];
	char name[32];
	int skill;
	float cost, magnitude, duration, area;
	uint32_t casting_perk;
	uint32_t keywords[4];
	int keyword_count;
	int held; // concentration: held, paid by the second
	int kind; // (the tome spells': KIND_*)
	char extra[32]; // (... a summons' creature kind, a bound weapon's name; "undead" for Detect Dead)
	float charge; // (... held this long before it's let go)
	float dual_scale; // dual casting: its effect's scale (MGEF's), and what its power raises (1 magnitude, 2 duration)
	int dual_what;
} Spell;

static Spell spells[SPELL_MAX] = {
	{"Candlelight", "Candlelight", SK_ALTERATION, 21, 5, 60, 0, 0, {0}, 0, 0},
	{"Healing", "Healing", SK_RESTORATION, 12, 10, 1, 0, 0, {0}, 0, 1},
	{"Flames", "", SK_DESTRUCTION, 14, 8, 1, 0, 0, {0}, 0, 1}, // (its name, and the others' left empty here: magic_init, from the game's text)
	{"Sparks", "Sparks", SK_DESTRUCTION, 19, 8, 1, 0, 0, {0}, 0, 1},
	{"FireRune", "", SK_DESTRUCTION, 234, 50, 30, RUNE_BURST_M, 0, {0}, 0, 0},
	{"Muffle", "Muffle", SK_ILLUSION, 144, 1, 180, 0, 0, {0}, 0, 0},
	{"BoundSword", "", SK_CONJURATION, 92, 0, 120, 0, 0, {0}, 0, 0},
};

static int spell_count = SPELL_COUNT; // (the engine's own, then the tome spells it can cast)
static int hand_spell[2] = {-1, SPELL_FLAMES}, readied;
static float spell_held[2];  // a fire-and-forget tome spell charging in the hand
static float cloak_left, cloak_dps, cloak_fx; // a cloak (Flame Cloak...): burning those about you
static int cloak_element;
static int candle_fixed;     // (Magelight: stays where it struck)
static int rune_element[MAX_RUNES];
static int dry[2]; // a held spell ran out of magicka: no casting till the button is let go
static float cast_s[2];
static float candle_left, candle_at[3];
static int candle_placed;
static float runes[MAX_RUNES][4]; // pos, age (age < 0: none)
static float muffle_left, bound_left;
static char bound_name[32]; // (the bound weapon in hand: its item's name)
static float ward_now, ward_left;    // a ward held up: its strength, till it's let fall
static float detect_range, detect_left; // Detect Life / Dead held: how far, till it's let go
static float guide_left; // Clairvoyance held: the way shown, till it's let go
static int detect_undead, telekinesis_now;
static int bound_cast, bound_end;
static float scroll_held[2];     // a scroll in the hand: how long it's been held charging
static float armour_left, armour_bonus; // Oakflesh-like scrolls: armour added for a while
static float rune_damage[MAX_RUNES];
static float hazard_left, hazard_tick, hazard_fx, hazard_dps, hazard_area, hazard_at[3]; // a storm laid (Blizzard)
static int hazard_element;
static unsigned known = (1u << SPELL_COUNT) - 1; // spells learned (bits; a new character: Skyrim's two)
static char other[MAGIC_OTHER_MAX][MAGIC_KEY_LEN]; // learned from tomes (the engine's own seven aside): their names
static int other_count;
// Dual casting (the same spell in both hands, its school's Dual Casting perk, both cast together):
// one cast, stronger (magnitude and / or duration) for more magicka - set while it's cast
static float dual_mag = 1.0f, dual_dur = 1.0f, dual_cost = 1.0f;
static int dual_impact;
static int dual_state;      // 0 none; 1 both hands casting it; 2 cast, till both are let go
static float dual_wait[2];  // a hand's press held back, waiting for the other's

#define SUMMON_REACH_M 5.0f     // a summons comes this far before you (Summoner: two, three times)
#define DARK_SOULS_HEALTH 100.0f
static PerkContext rune_ctx; // (the rune being laid: its spell's, for Rune Master)
#define PERK_RESPITE 0x000581f9u
#define PERK_ELEMENTAL_POTENCY 0x000cb41au
#define ELEMENTAL_POTENCY_MULT 1.5f // (Skyrim's: conjured atronachs 50% more powerful)
static const uint32_t PERK_MAGE_ARMOR[3] = {0x000d7999u, 0x000d799au, 0x000d799bu};
static const float MAGE_ARMOR_MULT[3] = {2.0f, 2.5f, 3.0f};

// Wearing no armour at all (clothes are none): Mage Armor's
static int no_armour_worn(void) {
	uint32_t forms[32];
	const int n = equip_save(forms, 32);
	for (int i = 0; i < n; i++) {
		const int item = items_by_form(forms[i]);
		if (items_type(item) == IT_ARMO && items_stat(item) > 0)
			return 0;
	}
	return 1;
}

static PerkContext context_of(const Spell *sp) {
	PerkContext ctx = {0};
	ctx.keywords = sp->keywords, ctx.keyword_count = sp->keyword_count;
	ctx.school = sp->skill + 6, ctx.casting_perk = sp->casting_perk;
	return ctx;
}

static PerkContext spell_context(int spell) {
	PerkContext ctx = {0};
	ctx.keywords = spells[spell].keywords, ctx.keyword_count = spells[spell].keyword_count;
	ctx.school = spells[spell].skill + 6, ctx.casting_perk = spells[spell].casting_perk; // (skill -> actor value)
	return ctx;
}

// With the player's perks (Novice Destruction halves Flames' cost, Augmented Flames...)
float magic_cost(int spell) {
	if (spell < 0 || spell >= spell_count) // (a scroll: nothing)
		return 0.0f;
	const PerkContext ctx = spell_context(spell);
	const float fortify = fminf(0.85f, fmaxf(-1.0f, effects_skill(spells[spell].skill) / 100.0f)); // (Fortify Destruction...)
	return perks_apply(EP_MOD_SPELL_COST, spells[spell].cost, &ctx) * (1.0f - fortify) * dual_cost;
}

static float magnitude(int spell) {
	const PerkContext ctx = spell_context(spell);
	return perks_apply(EP_MOD_SPELL_MAGNITUDE, spells[spell].magnitude, &ctx) * dual_mag;
}

static float duration(int spell) {
	const PerkContext ctx = spell_context(spell);
	return perks_apply(EP_MOD_SPELL_DURATION, spells[spell].duration, &ctx) * dual_dur;
}

// Whether the spell can be dual cast (its school's Dual Casting perk: Skyrim's Can Dual Cast entry)
static int can_dual(int spell) {
	if (spell < 0 || spell >= spell_count)
		return 0;
	const PerkContext ctx = spell_context(spell);
	return perks_apply(EP_CAN_DUAL_CAST, 0.0f, &ctx) >= 1.0f;
}

int magic_dual_casting(void) {
	return dual_state == 1;
}

int magic_flesh(float *left) { // (Oakflesh .. Ebonyflesh on you: 1..4 by its armour, 0 none; seconds left)
	if (left)
		*left = armour_left;
	return armour_left <= 0.0f ? 0 : armour_bonus >= 100.0f ? 4 : armour_bonus >= 80.0f ? 3 : armour_bonus >= 60.0f ? 2 : 1;
}

int magic_muffled(void) {
	return muffle_left > 0.0f;
}

int magic_take_bound_cast(void) { // (bits: 1 the left hand, 2 the right)
	int b = bound_cast;
	bound_cast = 0;
	return b;
}

const char *magic_bound_name(void) {
	return bound_name;
}

float magic_ward(void) { // (a ward up: its strength - armour, and half off a breath or a bolt)
	return ward_left > 0.0f ? ward_now : 0.0f;
}

int magic_guiding(void) { // (Clairvoyance held: main sends its wisps along the way)
	return guide_left > 0.0f;
}

float magic_detect(int *undead) { // (Detect Life / Dead held: its range; 0 not)
	if (undead)
		*undead = detect_undead;
	return detect_left > 0.0f ? detect_range : 0.0f;
}

int magic_take_telekinesis(void) { // (Telekinesis held this frame: main draws what's aimed at to hand)
	const int t = telekinesis_now;
	telekinesis_now = 0;
	return t;
}

int magic_take_bound_end(void) {
	int b = bound_end;
	bound_end = 0;
	return b;
}

// A scroll in a hand (its item), -1 if the hand holds a spell or nothing
static int hand_scroll(int h) {
	return hand_spell[h] >= MAGIC_SCROLL_BASE ? hand_spell[h] - MAGIC_SCROLL_BASE : -1;
}

float magic_armour_bonus(void) {
	return (armour_left > 0.0f ? armour_bonus : 0.0f) + (ward_left > 0.0f ? ward_now : 0.0f); // (a ward up: its armour too)
}

// A rune laid where you look (on the ground below, aimed at the air), the oldest making way
// when there are too many; 0 (and a notice) if there's no ground there
static int lay_rune(const float eye[3], const float dir[3], float damage, int element) {
	const float reach = perks_apply(EP_MOD_SPELL_RANGE, RUNE_REACH_M, &rune_ctx); // (Rune Master: five times as far)
	const float to[3] = {eye[0] + dir[0] * reach, eye[1] + dir[1] * reach, eye[2] + dir[2] * reach};
	float t = collision_ray_ground(eye, to), at[3];
	for (int i = 0; i < 3; i++)
		at[i] = eye[i] + (to[i] - eye[i]) * fminf(t, 0.98f);
	const float below[3] = {at[0], at[1] - 30.0f, at[2]};
	const float down = collision_ray_ground(at, below);
	if (down >= 1.0f) {
		ui_notice("You can't place a rune there", 2.0f);
		return 0;
	}
	int slot = -1, oldest = 0;
	for (int i = 0; i < MAX_RUNES && slot < 0; i++)
		if (runes[i][3] < 0.0f)
			slot = i;
	for (int i = 1; i < MAX_RUNES; i++)
		if (runes[i][3] > runes[oldest][3])
			oldest = i;
	if (slot < 0)
		slot = oldest;
	runes[slot][0] = at[0], runes[slot][1] = at[1] - 30.0f * down + 0.03f, runes[slot][2] = at[2];
	runes[slot][3] = 0.0f;
	rune_damage[slot] = damage, rune_element[slot] = element;
	return 1;
}

// A bolt from the hand toward what's under the crosshair (Firebolt, Ice Spike, Lightning Bolt)
static void fire_bolt(int element, float damage, float area, int h, const float eye[3], const float palms[2][3],
                      const float dir[3]) {
	const float far_pt[3] = {eye[0] + dir[0] * 80.0f, eye[1] + dir[1] * 80.0f, eye[2] + dir[2] * 80.0f};
	const float t = collision_ray_ground(eye, far_pt);
	float d[3] = {eye[0] + (far_pt[0] - eye[0]) * t - palms[h][0], eye[1] + (far_pt[1] - eye[1]) * t - palms[h][1],
	              eye[2] + (far_pt[2] - eye[2]) * t - palms[h][2]};
	const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	for (int k = 0; k < 3; k++)
		d[k] = l > 0.5f ? d[k] / l : dir[k];
	const float speed = element == ELEMENT_FIRE ? FIRE_BOLT_SPEED_MS : element == ELEMENT_FROST ? FROST_BOLT_SPEED_MS
	                                                                                           : SHOCK_BOLT_SPEED_MS;
	const int shot = projectiles_fire_bolt(palms[h], d, speed, element, damage, fmaxf(area, 0.0f));
	if (dual_impact)
		projectiles_set_stagger(shot);
	sfx_play(element == ELEMENT_FIRE ? "mag_firebolt_cast" : element == ELEMENT_FROST ? "mag_frost_cast" : "mag_shock_hit",
	         0.8f, 0.0f);
}

// A blast all about you (Fire Storm's: and its nearer rings - "75@19.81,50@7.62" - hit too, the
// closer the harder); lasting (seconds): a storm laid where you stand, its cold or fire on those
// in it by the second (Blizzard's hazard)
static void storm(int element, float damage, float area, float seconds, const char *rings, const float eye[3]) {
	creatures_set_spell_element(element);
	const float at[3] = {eye[0], eye[1] - 0.8f, eye[2]};
	area = fminf(fmaxf(area, 2.0f), STORM_MAX_M);
	if (seconds > 0.0f) {
		memcpy(hazard_at, at, sizeof(hazard_at));
		hazard_left = seconds, hazard_tick = 0.0f, hazard_dps = damage, hazard_area = area, hazard_element = element;
		game_log("magic: a storm laid, %.0f m, %.0f a second for %.0f s", area, damage, seconds);
		spellfx_burst(at, element, fminf(area * 0.5f, 6.0f));
		creatures_set_spell_element(-1);
		return;
	}
	creatures_area(at, area, damage);
	people_area(at, area, damage);
	for (const char *r = rings; r && *r;) {
		float mag = 0.0f, reach = 0.0f;
		if (sscanf(r, "%f@%f", &mag, &reach) == 2 && reach > 0.0f)
			creatures_area(at, fminf(reach, STORM_MAX_M), mag), people_area(at, fminf(reach, STORM_MAX_M), mag);
		r = strchr(r, ',');
		r = r ? r + 1 : NULL;
	}
	spellfx_burst(at, element, fminf(area * 0.5f, 6.0f));
	if (element == ELEMENT_FIRE)
		traps_fire_at(at, area);
}

// A once-cast spell's magicka; 0 (and a notice) if there isn't enough
static int pay(Vitals *v, float cost, int skill) {
	if (v->magicka < cost) {
		ui_notice("Not enough magicka", 2.0f);
		return 0;
	}
	v->magicka -= cost;
	v->shown_for = 0.0f;
	skills_use(skill, cost);
	return 1;
}

void magic_init(const char *pack_dir) {
	snprintf(spells[2].name, sizeof(spells[2].name), "%s", T_ENCH_0004DEDA_FULL0); // (Flames, Fire Rune, Bound Sword: the game's own names)
	snprintf(spells[4].name, sizeof(spells[4].name), "%s", T_ENCH_000B62DD_FULL0);
	snprintf(spells[6].name, sizeof(spells[6].name), "%s", T_MGEF_0001CE9F_FULL0);
	if (!bound_name[0])
		snprintf(bound_name, sizeof(bound_name), "%s", T_MGEF_0001CE9F_FULL0);
	hand_spell[HAND_LEFT] = -1, hand_spell[HAND_RIGHT] = SPELL_FLAMES, readied = 0;
	candle_left = 0.0f, candle_placed = 0;
	for (int i = 0; i < MAX_RUNES; i++)
		runes[i][3] = -1.0f;
	muffle_left = bound_left = armour_left = cloak_left = hazard_left = 0.0f;
	scroll_held[0] = scroll_held[1] = spell_held[0] = spell_held[1] = 0.0f;
	candle_fixed = 0;
	if (!pack_dir)
		return;
	char path[256];
	snprintf(path, sizeof(path), "%sspells.txt", pack_dir);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	spell_count = SPELL_COUNT;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		char *f[15]; // (an X line's 13th: a summons' creature, a bound weapon's name; 14th: dual casting)
		int n = 0;
		for (char *p = line; n < 15; n++) { // (fields split on '|', empty ones kept)
			f[n] = p;
			char *bar = strchr(p, '|');
			if (!bar) {
				n++;
				break;
			}
			*bar = 0, p = bar + 1;
		}
		Spell *sp = NULL;
		char **g = f;
		if (!strcmp(f[0], "X") && n >= 12) { // (a tome spell: castable if the engine knows its kind)
			int kind = KIND_NONE;
			for (int k = 1; k < KINDS; k++)
				if (!strcmp(f[10], KIND_NAMES[k]))
					kind = k;
			if (kind == KIND_NONE || spell_count >= SPELL_MAX)
				continue;
			sp = &spells[spell_count++];
			memset(sp, 0, sizeof(*sp));
			snprintf(sp->key, sizeof(sp->key), "%s", f[1]);
			sp->kind = kind, sp->charge = (float)atof(f[11]);
			sp->held = kind == KIND_STREAM_FROST;
			if (n >= 13)
				snprintf(sp->extra, sizeof(sp->extra), "%s", f[12]);
			g = f + 1;
		} else if (n >= 9) {
			for (int s = 0; s < SPELL_COUNT; s++)
				if (!strcmp(spells[s].key, f[0]))
					sp = &spells[s];
		}
		if (!sp)
			continue;
		snprintf(sp->name, sizeof(sp->name), "%s", g[1]);
		sp->skill = atoi(g[2]) - 6;
		sp->cost = (float)atof(g[3]), sp->magnitude = (float)atof(g[4]);
		if (atof(g[5]) > 1.0 || sp->kind)
			sp->duration = (float)atof(g[5]);
		sp->casting_perk = (uint32_t)strtoul(g[6], NULL, 16);
		sp->keyword_count = 0;
		for (char *kw = g[7]; *kw && sp->keyword_count < 4;) {
			char *end;
			sp->keywords[sp->keyword_count++] = (uint32_t)strtoul(kw, &end, 16);
			if (end == kw)
				break;
			kw = end;
		}
		if (atof(g[8]) > 0.1 || sp->kind)
			sp->area = (float)atof(g[8]);
		const char *dual = g == f ? (n >= 10 ? f[9] : "") : (n >= 14 ? f[13] : "");
		sp->dual_scale = 1.0f, sp->dual_what = 1;
		sscanf(dual, "%f:%d", &sp->dual_scale, &sp->dual_what);
	}
	free(buf);
}

int magic_count(void) {
	return spell_count;
}

const char *magic_name(int spell) {
	if (spell >= MAGIC_SCROLL_BASE)
		return items_name(spell - MAGIC_SCROLL_BASE);
	return spell >= 0 && spell < spell_count ? spells[spell].name : "";
}

const char *magic_detail(int spell) {
	static char text[64];
	if (spell < 0 || spell >= spell_count)
		return "";
	static const char *const schools[5] = {"Alteration", "Conjuration", "Destruction", "Illusion", "Restoration"};
	const int school = spells[spell].skill - SK_ALTERATION;
	snprintf(text, sizeof(text), "%s, %d magicka%s", school >= 0 && school < 5 ? schools[school] : "",
	         (int)(magic_cost(spell) + 0.5f), spells[spell].held ? " a second (hold)" : "");
	return text;
}

int magic_hand(int hand) {
	return hand == HAND_LEFT || hand == HAND_RIGHT ? hand_spell[hand] : -1;
}

void magic_set_hand(int hand, int spell) {
	if (hand == HAND_LEFT || hand == HAND_RIGHT) {
		const int scroll = spell >= MAGIC_SCROLL_BASE && items_scroll(spell - MAGIC_SCROLL_BASE) != NULL;
		hand_spell[hand] = (spell >= 0 && spell < spell_count) || scroll ? spell : -1;
		spell_held[hand] = 0.0f;
		scroll_held[hand] = 0.0f;
	}
}

void magic_set_scroll(int hand, int item) {
	magic_set_hand(hand, item >= 0 ? MAGIC_SCROLL_BASE + item : -1);
}

int magic_readied(void) {
	return readied;
}

void magic_set_readied(int on) {
	readied = on != 0;
}

float magic_candle_left(void) {
	return candle_left;
}

void magic_set_candle(float seconds) {
	candle_left = seconds > 0.0f ? seconds : 0.0f;
	candle_placed = candle_fixed = 0;
}

// Spends a concentration spell's magicka for this frame; 0 if there isn't enough
static float spend(Vitals *v, float per_second, float dt) {
	float cost = per_second * dt;
	if (v->magicka < cost)
		return 0.0f;
	v->magicka -= cost;
	v->shown_for = 0.0f;
	return cost;
}

static int circle_inside(const float pos[3], void *ctx) { // (a cloak's reach)
	const float *c = ctx;
	return fabsf(pos[1] - c[1]) < 2.0f && hypotf(pos[0] - c[0], pos[2] - c[2]) < c[3];
}

// A spell let go (charged), at that strength: its effect; 0 if it couldn't be (a rune with no
// ground). about: all about you (a scroll's self-cast Harmony, Mass Paralysis), not where aimed
static int cast_spell(const Spell *sp, float mag, float secs, int about, int h, Vitals *v, const float eye[3],
                      const float palms[2][3], const float dir[3], MagicFx *fx) {
	switch (sp->kind) {
	case KIND_BOLT_FIRE: fire_bolt(ELEMENT_FIRE, mag, sp->area, h, eye, palms, dir); break;
	case KIND_BOLT_FROST: fire_bolt(ELEMENT_FROST, mag, sp->area, h, eye, palms, dir); break;
	case KIND_BOLT_SHOCK: fire_bolt(ELEMENT_SHOCK, mag, sp->area, h, eye, palms, dir); break;
	case KIND_STORM_FIRE: storm(ELEMENT_FIRE, mag, sp->area, secs, sp->extra, eye); break;
	case KIND_STORM_FROST: storm(ELEMENT_FROST, mag, sp->area, secs, sp->extra, eye); break;
	case KIND_STORM_SHOCK: storm(ELEMENT_SHOCK, mag, sp->area, secs, sp->extra, eye); break;
	case KIND_RUNE_FROST:
	case KIND_RUNE_SHOCK:
		rune_ctx = context_of(sp);
		if (!lay_rune(eye, dir, mag, sp->kind == KIND_RUNE_FROST ? ELEMENT_FROST : ELEMENT_SHOCK))
			return 0;
		fx->rune_cast = 1;
		break;
	case KIND_HEAL: // (Fast Healing, Close Wounds: at once - Respite: your stamina too)
		v->health = fminf(v->max_health, v->health + mag);
		if (perks_has_form(PERK_RESPITE))
			v->stamina = fminf(v->max_stamina, v->stamina + mag);
		v->shown_for = 0.0f;
		fx->hand[h].healing = 1, fx->heal_burst = 1;
		break;
	case KIND_ARMOUR: // (Oakflesh .. Ebonyflesh; Mage Armor: twice, two and a half, three times as strong with no armour on)
		armour_left = secs, armour_bonus = mag;
		for (int r = 2; r >= 0; r--)
			if (perks_has_form(PERK_MAGE_ARMOR[r]) && no_armour_worn()) {
				armour_bonus = mag * MAGE_ARMOR_MULT[r];
				break;
			}
		ui_notice("Your skin hardens", 2.5f);
		break;
	case KIND_INVISIBILITY:
	case KIND_WATERBREATHING: { // (the player's active effects, as a potion's)
		static MagicEffect invisible = {"Invisibility", "Invisibility", NULL, 0, 0, ARCH_INVISIBILITY, 54, 0};
		static MagicEffect gills = {"Waterbreathing", "", NULL, 0, 0, ARCH_PEAK_VALUE, 57, 0};
		snprintf(gills.name, sizeof(gills.name), "%s", T_ENCH_00092A76_FULL0);
		effects_apply_effect(sp->kind == KIND_INVISIBILITY ? &invisible : &gills, 1.0f, secs, v);
		ui_notice(sp->kind == KIND_INVISIBILITY ? "You are invisible" : "You can breathe under water", 2.5f);
		break;
	}
	case KIND_LIGHT: { // Magelight: a light flies where you aim and stays where it strikes
		const float to[3] = {eye[0] + dir[0] * LIGHT_REACH_M, eye[1] + dir[1] * LIGHT_REACH_M, eye[2] + dir[2] * LIGHT_REACH_M};
		const float t = fminf(collision_ray_ground(eye, to), 1.0f);
		for (int k = 0; k < 3; k++)
			candle_at[k] = eye[k] + (to[k] - eye[k]) * t - dir[k] * 0.3f;
		candle_left = secs > 1.0f ? secs : 60.0f, candle_placed = 1, candle_fixed = 1;
		sfx_play("mag_candlelight", 0.7f, 0.0f);
		break;
	}
	case KIND_CLOAK_FIRE:
	case KIND_CLOAK_FROST:
	case KIND_CLOAK_SHOCK:
		cloak_left = secs, cloak_dps = mag;
		cloak_element = sp->kind == KIND_CLOAK_FIRE ? ELEMENT_FIRE : sp->kind == KIND_CLOAK_FROST ? ELEMENT_FROST : ELEMENT_SHOCK;
		break;
	case KIND_SUMMON: { // (Conjuration: at the spot you aim at, a few steps off - Summoner: further)
		const PerkContext sctx = context_of(sp);
		const float reach = perks_apply(EP_MOD_SPELL_RANGE, SUMMON_REACH_M, &sctx);
		creatures_set_summon_limit(1 + (int)perks_apply(EP_MOD_COMMANDED_LIMIT, 0.0f, &sctx)); // (Twin Souls: two at once)
		const float to[3] = {eye[0] + dir[0] * reach, eye[1] + dir[1] * reach, eye[2] + dir[2] * reach};
		const float t = fminf(collision_ray_ground(eye, to), 1.0f) * 0.9f;
		const float at[3] = {eye[0] + dir[0] * reach * t, eye[1] - 1.5f, eye[2] + dir[2] * reach * t};
		const int spirit = strstr(sp->name, "Familiar") && !strstr(sp->name, "Flaming"); // (Skyrim's familiar: a spectral wolf)
		if (!(spirit ? creatures_summon_spirit : creatures_summon_ally)(sp->extra, at, secs > 0.0f ? secs : 60.0f)) {
			ui_notice("Nothing answers your summons here", 3.0f);
			return 0;
		}
		if (perks_has_form(PERK_ELEMENTAL_POTENCY) && strstr(sp->extra, "atronach"))
			creatures_strengthen_summons(ELEMENTAL_POTENCY_MULT, 0.0f); // (Elemental Potency)
		spellfx_burst(at, ELEMENT_FORCE, 1.0f);
		sfx_play("mag_ready_alteration", 0.9f, 0.0f);
		break;
	}
	case KIND_REANIMATE: {
		const PerkContext rctx = context_of(sp);
		creatures_set_summon_limit(1 + (int)perks_apply(EP_MOD_COMMANDED_LIMIT, 0.0f, &rctx));
		if (!creatures_reanimate(eye, dir, 14.0f, secs > 0.0f ? secs : 60.0f)) {
			ui_notice("There is no body there to raise", 3.0f);
			return 0;
		}
		if (perks_apply(EP_APPLY_REANIMATE_SPELL, 0.0f, &rctx) > 0.0f) // (Dark Souls: the raised have 100 more health)
			creatures_strengthen_summons(1.0f, DARK_SOULS_HEALTH);
		sfx_play("mag_ready_alteration", 0.9f, 0.0f);
		break;
	}
	case KIND_BOUND: // (a weapon of light in hand while it lasts: main equips it - magic_bound_name)
		snprintf(bound_name, sizeof(bound_name), "%s", sp->extra[0] ? sp->extra : sp->name);
		bound_left = secs > 0.0f ? secs : 120.0f;
		bound_cast |= 2; // (the right hand: an axe's and a bow's two)
		break;
	case KIND_RALLY: { // (Courage, Rally, Call to Arms: those with you strike harder)
		const int nr = creatures_rally_allies(secs > 0.0f ? secs : 60.0f);
		ui_notice(nr ? "Those with you take heart" : "There is no one with you to hearten", 3.0f);
		sfx_play("mag_ready_alteration", 0.7f, 0.0f);
		if (!nr)
			return 0;
		break;
	}
	case KIND_HEAL_OTHER: { // (Heal Other, Healing Hands: those with you)
		const int nh = creatures_heal_allies(mag * (secs > 1.0f ? secs : 1.0f) * 3.0f);
		ui_notice(nh ? "Those with you are healed" : "There is no one with you in need of healing", 3.0f);
		sfx_play("mag_heal_lp", 0.7f, 0.0f);
		if (!nh)
			return 0;
		break;
	}
	case KIND_TRANSMUTE: { // (Skyrim's: iron ore to silver; with none, silver to gold)
		const int iron = items_find_name(T_MISC_00071CF3_FULL0), silver = items_find_name(T_MISC_0005ACDF_FULL0), gold = items_find_name(T_MISC_0005ACDE_FULL0);
		if (iron >= 0 && silver >= 0 && items_have(iron) > 0) {
			items_add(iron, -1), items_add(silver, 1);
			ui_notice("Iron Ore becomes Silver Ore", 3.0f);
		} else if (silver >= 0 && gold >= 0 && items_have(silver) > 0) {
			items_add(silver, -1), items_add(gold, 1);
			ui_notice("Silver Ore becomes Gold Ore", 3.0f);
		} else {
			ui_notice("You have no iron or silver ore", 3.0f);
			return 0;
		}
		sfx_play("mag_ready_alteration", 0.8f, 0.0f);
		break;
	}
	case KIND_SOULTRAP: { // (dying within its time, its soul fills a gem you carry: main)
		float hit[3];
		const float to[3] = {eye[0] + dir[0] * ILLUSION_REACH_M, eye[1] + dir[1] * ILLUSION_REACH_M,
		                     eye[2] + dir[2] * ILLUSION_REACH_M};
		const float reach = ILLUSION_REACH_M * fminf(collision_ray_ground(eye, to), 1.0f);
		if (creatures_spell_ray(eye, dir, reach, CSPELL_SOULTRAP, 0.0f, secs > 0.0f ? secs : 60.0f, 0.0f, hit))
			spellfx_burst(hit, ELEMENT_FORCE, 0.6f);
		sfx_play("mag_ready_alteration", 0.7f, 0.0f);
		break;
	}
	case KIND_FEAR: // (it ran on into the summons: Fear, Hysteria found nothing to call)
	case KIND_FRENZY:
	case KIND_CALM:
	case KIND_PARALYZE:
	case KIND_TURN: { // (at what's under the crosshair: no higher than its magnitude's level)
		float hit[3];
		const int effect = sp->kind == KIND_FRENZY ? CSPELL_FRENZY : sp->kind == KIND_FEAR ? CSPELL_FEAR : sp->kind == KIND_CALM ? CSPELL_CALM
		                 : sp->kind == KIND_PARALYZE ? CSPELL_PARALYZE : CSPELL_TURN;
		const float to[3] = {eye[0] + dir[0] * ILLUSION_REACH_M, eye[1] + dir[1] * ILLUSION_REACH_M,
		                     eye[2] + dir[2] * ILLUSION_REACH_M};
		const float reach = ILLUSION_REACH_M * fminf(collision_ray_ground(eye, to), 1.0f);
		const float level = sp->kind == KIND_PARALYZE ? 0.0f : mag;
		if (about) {
			const float feet[3] = {eye[0], eye[1] - 1.6f, eye[2]};
			creatures_spell_about(feet, fmaxf(sp->area, 2.0f), effect, level, secs);
			spellfx_burst(feet, ELEMENT_FORCE, fminf(fmaxf(sp->area, 2.0f) * 0.5f, 6.0f));
		} else if (creatures_spell_ray(eye, dir, reach, effect, level, secs, sp->area, hit))
			spellfx_burst(hit, ELEMENT_FORCE, 0.6f);
		sfx_play("mag_ready_alteration", 0.7f, 0.0f);
		break;
	}
	case KIND_DETECT: // (a scroll's: Detect Life for its seconds, not held)
		detect_range = sp->area > 1.0f ? sp->area : 60.0f, detect_undead = !strcmp(sp->extra, "undead");
		detect_left = secs > 0.0f ? secs : 15.0f;
		break;
	default:
		return 0;
	}
	return 1;
}

// A tome spell let go (charged): its effect, at its strength (perks')
static int cast_tome(int spell, int h, Vitals *v, const float eye[3], const float palms[2][3], const float dir[3],
                     MagicFx *fx) {
	game_log("magic: %s let go, magnitude %.0f, %.0f s, %.0f magicka%s", spells[spell].name, magnitude(spell), duration(spell),
	         magic_cost(spell), dual_cost > 1.0f ? " (dual)" : "");
	return cast_spell(&spells[spell], magnitude(spell), duration(spell), 0, h, v, eye, palms, dir, fx);
}

static int kind_named(const char *name) {
	for (int k = 0; k < KINDS; k++)
		if (!strcmp(name, KIND_NAMES[k]))
			return k;
	return KIND_NONE;
}

// A scroll read: its spell, at once, at the scroll's strength, for nothing (Skyrim's scrolls cost
// no magicka, nor train). The engine's own spells' kinds (Candlelight, Muffle, Fire Rune) here;
// the rest as the tome spell of that kind is cast
static void read_scroll(const ItemScroll *sc, const char *title, int h, Vitals *v, const float eye[3],
                        const float palms[2][3], const float dir[3], MagicFx *fx) {
	if (!strcmp(sc->kind, "muffle")) {
		muffle_left = sc->seconds;
		fx->muffle_cast = 1;
		ui_notice("Muffle: your steps are silent", 2.5f);
		return;
	}
	if (!strcmp(sc->kind, "candlelight")) {
		candle_left = sc->seconds > 1.0f ? sc->seconds : 60.0f, candle_placed = candle_fixed = 0;
		sfx_play("mag_candlelight", 0.7f, 0.0f);
		return;
	}
	if (!strcmp(sc->kind, "rune_fire")) {
		rune_ctx = (PerkContext){0};
		rune_ctx.school = -1;
		if (lay_rune(eye, dir, sc->magnitude, ELEMENT_FIRE))
			fx->rune_cast = 1;
		return;
	}
	Spell sp = {0};
	snprintf(sp.name, sizeof(sp.name), "%s", strncmp(title, "Scroll of ", 10) ? title : title + 10);
	sp.kind = kind_named(sc->kind), sp.area = sc->area;
	snprintf(sp.extra, sizeof(sp.extra), "%s", sc->extra);
	for (int i = SPELL_COUNT; i < spell_count && !sp.extra[0]; i++) // (a summons' creature: the spell's own)
		if (spells[i].kind == sp.kind && !strcmp(spells[i].name, sp.name))
			snprintf(sp.extra, sizeof(sp.extra), "%s", spells[i].extra);
	cast_spell(&sp, sc->magnitude, sc->seconds, sc->delivery == 0 && sc->area > 0.0f, h, v, eye, palms, dir, fx);
}

// A staff's blow (swung, as the player likes it - not cast from it as Skyrim's): its spell at
// the staff's strength, for a charge. The spells at someone (a bolt's harm and its burst, Fear,
// Calm, Frenzy, Paralysis, Turn Undead, Soul Trap) on what the blow struck - nothing if it missed;
// the rest (a summons, the raised dead, Courage, Healing Hands, Magelight...) as the swing goes
int magic_staff_blow(int staff, int struck, Vitals *v, const float eye[3], const float dir[3]) {
	const ItemScroll *sc = items_staff(staff);
	if (!sc)
		return 0;
	const int kind = kind_named(sc->kind);
	const int at_one = (kind >= KIND_BOLT_FIRE && kind <= KIND_STREAM_FROST) || kind == KIND_FEAR || kind == KIND_CALM ||
	                   kind == KIND_FRENZY || kind == KIND_PARALYZE || kind == KIND_TURN || kind == KIND_SOULTRAP;
	if (at_one && !struck)
		return 0;
	if (!items_charge_use(staff)) {
		static int told = -1;
		if (told != staff) {
			char text[96];
			snprintf(text, sizeof(text), "%s has no charge left", items_name(staff));
			ui_notice(text, 3.0f);
			told = staff;
		}
		return 0;
	}
	float at[3];
	if (at_one && !creatures_magic_struck(0.0f, at))
		return 0;
	switch (kind) {
	case KIND_BOLT_FIRE:
	case KIND_BOLT_FROST:
	case KIND_BOLT_SHOCK:
	case KIND_STREAM_FROST: { // (its harm on the one struck - a Fireball's on all about it)
		const int element = kind == KIND_BOLT_FIRE ? ELEMENT_FIRE : kind == KIND_BOLT_SHOCK ? ELEMENT_SHOCK : ELEMENT_FROST;
		if (sc->area > 0.0f)
			creatures_area(at, sc->area, sc->magnitude), people_area(at, sc->area, sc->magnitude);
		else if (!creatures_magic_struck(sc->magnitude, at))
			people_area(at, PERSON_STRUCK_M, sc->magnitude);
		spellfx_burst(at, element, sc->area > 0.0f ? fminf(sc->area * 0.5f, 4.0f) : 0.5f);
		if (element == ELEMENT_FIRE)
			traps_fire_at(at, fmaxf(sc->area, 1.0f));
		sfx_play_at(element == ELEMENT_FIRE ? "mag_fire_hit" : element == ELEMENT_FROST ? "mag_frost_hit" : "mag_shock_hit", at,
		            25.0f);
		break;
	}
	case KIND_PARALYZE:
		creatures_paralyze_struck(sc->seconds > 0.0f ? sc->seconds : 10.0f);
		spellfx_burst(at, ELEMENT_FORCE, 0.6f);
		break;
	case KIND_FEAR:
	case KIND_CALM:
	case KIND_FRENZY:
	case KIND_TURN:
	case KIND_SOULTRAP: {
		const int effect = kind == KIND_FEAR ? CSPELL_FEAR : kind == KIND_CALM ? CSPELL_CALM : kind == KIND_FRENZY ? CSPELL_FRENZY
		                   : kind == KIND_TURN ? CSPELL_TURN : CSPELL_SOULTRAP;
		const float secs = sc->seconds > 0.0f ? sc->seconds : 30.0f;
		const float level = kind == KIND_SOULTRAP ? 0.0f : sc->magnitude;
		if (sc->area > 0.0f) // (Vanquishment, Frenzy's: all about it)
			creatures_spell_about(at, sc->area, effect, level, secs);
		else
			creatures_bewitch_struck(effect, level, secs);
		spellfx_burst(at, ELEMENT_FORCE, 0.6f);
		break;
	}
	default: { // (not at someone: cast as the swing goes, where you look)
		Spell sp = {0};
		snprintf(sp.name, sizeof(sp.name), "%s", items_name(staff));
		sp.kind = kind, sp.area = sc->area;
		snprintf(sp.extra, sizeof(sp.extra), "%s", sc->extra);
		const float palms[2][3] = {{eye[0], eye[1] - 0.3f, eye[2]}, {eye[0], eye[1] - 0.3f, eye[2]}};
		MagicFx scratch;
		memset(&scratch, 0, sizeof(scratch));
		cast_spell(&sp, sc->magnitude, sc->seconds, 0, HAND_RIGHT, v, eye, palms, dir, &scratch);
		break;
	}
	}
	game_log("magic: %s's blow - %s %.0f%s", items_name(staff), sc->kind, sc->magnitude, struck ? " on what it struck" : "");
	return 1;
}

// A tome spell's look in the hand (the engine's own spells': fire, lightning, a golden glow,
// a cool light)
static int look_of(int kind) {
	switch (kind) {
	case KIND_BOLT_FIRE: case KIND_CLOAK_FIRE: case KIND_STORM_FIRE: return SPELL_FLAMES;
	case KIND_BOLT_SHOCK: case KIND_RUNE_SHOCK: case KIND_CLOAK_SHOCK: case KIND_STORM_SHOCK: return SPELL_SPARKS;
	case KIND_HEAL: case KIND_ARMOUR: case KIND_TURN: return SPELL_HEALING;
	default: return SPELL_CANDLELIGHT;
	}
}

void magic_update(float dt, const int pressed_in[2], const int held_in[2], Vitals *v, const float eye[3],
                  const float palms[2][3], const float dir[3], float yaw_deg, MagicFx *fx) {
	memset(fx, 0, sizeof(*fx));
	for (int h = 0; h < 2; h++)
		if (pressed_in[h] && hand_spell[h] >= 0)
			readied = 1;
	// Dual casting (Skyrim's): the same spell in both hands, its school's Dual Casting perk taken,
	// both buttons together - one cast from the right hand, overcharged; a lone press waits a
	// moment for the other's before it casts as one hand's
	int pressed[2] = {pressed_in[0], pressed_in[1]}, held[2] = {held_in[0], held_in[1]};
	const int same = readied && hand_spell[1] >= 0 && hand_spell[0] == hand_spell[1] && hand_scroll(1) < 0 &&
	                 can_dual(hand_spell[1]);
	int dual_now = 0;
	if (!same) {
		dual_state = 0, dual_wait[0] = dual_wait[1] = 0.0f;
	} else {
		if (dual_state == 0) {
			for (int h = 0; h < 2; h++)
				if (pressed[h])
					dual_wait[h] = DUAL_WINDOW_S;
			if (held[0] && held[1] && dual_wait[0] > 0.0f && dual_wait[1] > 0.0f) { // (pressed within a moment of each other)
				dual_state = 1, dual_wait[0] = dual_wait[1] = 0.0f;
				pressed[1] = 1; // (cast as the both of them went down)
				game_log("magic: dual cast %s", spells[hand_spell[1]].name);
			} else {
				for (int h = 0; h < 2; h++) {
					if (dual_wait[h] <= 0.0f)
						continue;
					if ((dual_wait[h] -= dt) <= 0.0f || !held[h])
						pressed[h] = 1, dual_wait[h] = 0.0f; // (the other never came: this hand's own cast, late)
					else
						pressed[h] = held[h] = 0; // (held back a moment)
				}
			}
		}
		if (dual_state == 1) {
			dual_now = 1;
			held[1] = held[0] && held[1];
			if (!held[1])
				dual_state = 2; // (one let go: the cast goes - a charged one's let go now)
			pressed[0] = held[0] = 0;
		} else if (dual_state == 2) {
			pressed[0] = pressed[1] = held[0] = held[1] = 0;
			if (!held_in[0] && !held_in[1])
				dual_state = 0;
		}
	}
	int aimed = 0;
	for (int h = 0; h < 2; h++)
		if (!held[h])
			dry[h] = 0;
	for (int h = 0; h < 2; h++) {
		MagicHand *m = &fx->hand[h];
		m->spell = readied ? hand_spell[h] : -1;
		int casting = 0;
		if (dual_now && h == HAND_RIGHT) { // (the dual cast's power: Skyrim's 2.2 times, by the effect's scale)
			const Spell *ds = &spells[hand_spell[h]];
			const float up = 1.0f + (DUAL_EFFECT - 1.0f) * ds->dual_scale;
			dual_mag = ds->dual_what & 1 ? up : 1.0f, dual_dur = ds->dual_what & 2 ? up : 1.0f, dual_cost = DUAL_COST;
			dual_impact = ds->skill == SK_DESTRUCTION && perks_has_form(PERK_IMPACT);
		}
		const int scroll = hand_scroll(h);
		if (scroll >= 0 && items_have(scroll) <= 0) { // (all read: the hand's empty)
			hand_spell[h] = -1, m->spell = -1;
		} else if (scroll >= 0 && readied) {
			// A scroll: held, it charges (its spell gathering in the hand); let go once charged,
			// it's read - and gone
			const ItemScroll *sc = items_scroll(scroll);
			m->spell = !strcmp(sc->kind, "rune_fire") ? SPELL_FLAMES : look_of(kind_named(sc->kind)); // (its look)
			const float need = fmaxf(sc->charge, SCROLL_MIN_CHARGE_S);
			if (held[h]) {
				scroll_held[h] += dt;
				casting = 1;
				m->pose = sc->delivery == 0 ? MAGIC_SELF : MAGIC_AIM;
			} else {
				if (scroll_held[h] >= need) {
					read_scroll(sc, items_name(scroll), h, v, eye, palms, dir, fx);
					items_add(scroll, -1);
					m->pose = sc->delivery == 0 ? MAGIC_SELF : MAGIC_AIM;
				}
				scroll_held[h] = 0.0f;
			}
			m->charged = scroll_held[h] >= need;
		} else if (m->spell >= SPELL_COUNT && m->spell < spell_count) {
			const int spell = m->spell;
			const Spell *sp = &spells[spell];
			m->spell = look_of(sp->kind);
			if (sp->kind == KIND_STREAM_FROST) { // Frostbite: held, a stream of cold
				if (held[h] && !dry[h]) {
					float spent = spend(v, magic_cost(spell), dt);
					if (spent <= 0.0f) {
						dry[h] = 1;
						ui_notice("Not enough magicka", 2.0f);
					} else {
						casting = m->frost = 1;
						m->pose = MAGIC_AIM;
						const float reach = FLAME_REACH_M + 1.5f;
						const float to[3] = {eye[0] + dir[0] * reach, eye[1] + dir[1] * reach, eye[2] + dir[2] * reach};
						const float t = collision_ray_ground(eye, to);
						activators_strike(eye, dir, reach * t + 0.5f); // (a spider's web: burnt, frozen away)
						creatures_set_spell_element(ELEMENT_FROST);
						const float struck = struck_ray(eye, dir, reach * t, SPELL_RADIUS_M, magnitude(spell) * dt);
						creatures_set_spell_element(-1);
						if (!aimed) {
							const float stop = struck >= 0.0f ? struck / reach : t;
							fx->flame_hit = stop < 1.0f;
							for (int i = 0; i < 3; i++)
								fx->flame_to[i] = eye[i] + (to[i] - eye[i]) * stop;
							aimed = 1;
						}
						if (struck >= 0.0f) {
							skills_use(SK_DESTRUCTION, spent);
							static float chill;
							if ((chill -= dt) <= 0.0f)
								sfx_play_at("mag_frost_hit", fx->flame_to, 25.0f), chill = 0.5f;
						}
					}
				}
			} else if (sp->kind == KIND_WARD || sp->kind == KIND_DETECT || sp->kind == KIND_TELEKINESIS ||
			           sp->kind == KIND_EQUILIBRIUM || sp->kind == KIND_CLAIRVOYANCE) { // Held, by the second: a ward, the
			                                                                            // detects, Telekinesis, Equilibrium, Clairvoyance
				if (held[h] && !dry[h]) {
					const float spent = sp->kind == KIND_EQUILIBRIUM ? dt : spend(v, magic_cost(spell), dt);
					if (spent <= 0.0f) {
						dry[h] = 1;
						ui_notice("Not enough magicka", 2.0f);
					} else {
						casting = 1;
						m->pose = sp->kind == KIND_WARD || sp->kind == KIND_TELEKINESIS ? MAGIC_AIM : MAGIC_SELF;
						if (sp->kind == KIND_WARD) {
							ward_now = magnitude(spell), ward_left = 0.25f;
						} else if (sp->kind == KIND_DETECT) {
							detect_range = sp->area > 1.0f ? sp->area : 60.0f, detect_left = 0.4f;
							detect_undead = !strcmp(sp->extra, "undead");
						} else if (sp->kind == KIND_TELEKINESIS) {
							telekinesis_now = 1;
						} else if (sp->kind == KIND_CLAIRVOYANCE) { // (Skyrim's: concentration, the way shown while held)
							guide_left = 0.3f;
						} else { // (Equilibrium: health into magicka, 25 a second - not your last of it)
							const float take = fminf(25.0f * dt, fmaxf(0.0f, v->health - 1.0f));
							v->health -= take, v->magicka = fminf(v->max_magicka, v->magicka + take);
							if (take <= 0.0f)
								dry[h] = 1;
						}
						skills_use(sp->skill, spent);
					}
				}
			} else { // the rest: held to charge (Skyrim's charge time), let go to cast
				const float need = fmaxf(sp->charge, SPELL_MIN_CHARGE_S);
				const int self = sp->kind == KIND_HEAL || sp->kind == KIND_ARMOUR || sp->kind == KIND_INVISIBILITY ||
				                 sp->kind == KIND_WATERBREATHING || sp->kind == KIND_CLAIRVOYANCE || (sp->kind >= KIND_CLOAK_FIRE && sp->kind <= KIND_STORM_SHOCK);
				if (held[h]) {
					spell_held[h] += dt;
					casting = 1;
					m->pose = self ? MAGIC_SELF : MAGIC_AIM;
				} else {
					if (spell_held[h] >= need) {
						if (v->magicka < magic_cost(spell))
							ui_notice("Not enough magicka", 2.0f);
						else if (cast_tome(spell, h, v, eye, palms, dir, fx))
							pay(v, magic_cost(spell), sp->skill);
						m->pose = self ? MAGIC_SELF : MAGIC_AIM;
					}
					spell_held[h] = 0.0f;
				}
				m->charged = spell_held[h] >= need;
			}
		} else if (m->spell == SPELL_CANDLELIGHT && pressed[h]) {
			// Candlelight: cast once, then it rises from the hand to float above your
			// shoulder until it burns out
			if (pay(v, magic_cost(SPELL_CANDLELIGHT), SK_ALTERATION)) {
				candle_left = duration(SPELL_CANDLELIGHT), candle_fixed = 0;
				memcpy(candle_at, palms[h], sizeof(candle_at));
				candle_placed = 1;
			}
		} else if (m->spell == SPELL_HEALING && held[h] && !dry[h]) {
			float spent = spend(v, magic_cost(SPELL_HEALING), dt);
			if (spent <= 0.0f) {
				dry[h] = 1;
				ui_notice("Not enough magicka", 2.0f);
			} else {
				casting = m->healing = 1;
				m->pose = MAGIC_SELF;
				if (v->health < v->max_health) { // (it trains only when it heals)
					v->health = fminf(v->max_health, v->health + magnitude(SPELL_HEALING) * dt);
					if (perks_has_form(PERK_RESPITE)) // (Respite: stamina too)
						v->stamina = fminf(v->max_stamina, v->stamina + magnitude(SPELL_HEALING) * dt);
					skills_use(SK_RESTORATION, spent);
				}
			}
		} else if (m->spell == SPELL_FLAMES && held[h] && !dry[h]) {
			float spent = spend(v, magic_cost(SPELL_FLAMES), dt);
			if (spent <= 0.0f) {
				dry[h] = 1;
				ui_notice("Not enough magicka", 2.0f);
			} else {
				casting = m->flames = 1;
				m->pose = MAGIC_AIM;
				// Aimed where you look: from the eye, as far as the hand reaches; it burns the
				// first creature in the way (Skyrim's damage a second, each hand's)
				const float reach = FLAME_REACH_M + 0.5f;
				const float to[3] = {eye[0] + dir[0] * reach, eye[1] + dir[1] * reach, eye[2] + dir[2] * reach};
				const float t = collision_ray_ground(eye, to);
				activators_strike(eye, dir, reach * t + 0.5f); // (a spider's web: burnt away)
				creatures_set_spell_element(ELEMENT_FIRE);
				const float struck = struck_ray(eye, dir, reach * t, SPELL_RADIUS_M, magnitude(SPELL_FLAMES) * dt);
				creatures_set_spell_element(-1);
				if (!aimed) {
					const float stop = struck >= 0.0f ? struck / reach : t;
					fx->flame_hit = stop < 1.0f;
					for (int i = 0; i < 3; i++)
						fx->flame_to[i] = eye[i] + (to[i] - eye[i]) * stop;
					aimed = 1;
				}
				if (struck >= 0.0f) { // (Skyrim trains Destruction on hits; its fire crackles on them)
					skills_use(SK_DESTRUCTION, spent);
					static float crackle;
					if ((crackle -= dt) <= 0.0f)
						sfx_play_at("mag_fire_hit", fx->flame_to, 25.0f), crackle = 0.4f;
				}
				if (fx->flame_hit) // (oil catches)
					traps_fire_at(fx->flame_to, 0.6f);
			}
		} else if (m->spell == SPELL_SPARKS && held[h] && !dry[h]) {
			float spent = spend(v, magic_cost(SPELL_SPARKS), dt);
			if (spent <= 0.0f) {
				dry[h] = 1;
				ui_notice("Not enough magicka", 2.0f);
			} else {
				casting = m->sparks = 1;
				m->pose = MAGIC_AIM;
				const float to[3] = {eye[0] + dir[0] * SPARK_REACH_M, eye[1] + dir[1] * SPARK_REACH_M,
				                     eye[2] + dir[2] * SPARK_REACH_M};
				const float t = collision_ray_ground(eye, to);
				activators_strike(eye, dir, SPARK_REACH_M * t + 0.5f); // (a spider's web: burnt away)
				creatures_set_spell_element(ELEMENT_SHOCK);
				const float struck = struck_ray(eye, dir, SPARK_REACH_M * t, SPELL_RADIUS_M,
				                                   magnitude(SPELL_SPARKS) * dt);
				creatures_set_spell_element(-1);
				const float stop = struck >= 0.0f ? struck / SPARK_REACH_M : t;
				fx->spark_hit = stop < 1.0f;
				for (int i = 0; i < 3; i++)
					fx->spark_to[i] = eye[i] + (to[i] - eye[i]) * stop;
				if (struck >= 0.0f) {
					skills_use(SK_DESTRUCTION, spent);
					static float zap;
					if ((zap -= dt) <= 0.0f)
						sfx_play_at("mag_shock_hit", fx->spark_to, 25.0f), zap = 0.35f;
				}
			}
		} else if (m->spell == SPELL_FIRERUNE && pressed[h]) {
			if (v->magicka < magic_cost(SPELL_FIRERUNE))
				ui_notice("Not enough magicka", 2.0f);
			else if ((rune_ctx = spell_context(SPELL_FIRERUNE), lay_rune(eye, dir, magnitude(SPELL_FIRERUNE), ELEMENT_FIRE)) &&
			         pay(v, magic_cost(SPELL_FIRERUNE), SK_DESTRUCTION)) {
				fx->rune_cast = 1;
				m->pose = MAGIC_AIM;
			}
		} else if (m->spell == SPELL_MUFFLE && pressed[h]) {
			if (pay(v, magic_cost(SPELL_MUFFLE), SK_ILLUSION)) {
				muffle_left = duration(SPELL_MUFFLE);
				fx->muffle_cast = 1;
				ui_notice("Muffle: your steps are silent", 2.5f);
			}
		} else if (m->spell == SPELL_BOUNDSWORD && pressed[h]) {
			if (pay(v, magic_cost(SPELL_BOUNDSWORD), SK_CONJURATION)) {
				bound_left = duration(SPELL_BOUNDSWORD);
				bound_cast |= 1 << h; // (into the hand that cast it: one in each, cast from both)
			}
		}
		cast_s[h] = casting ? cast_s[h] + dt : 0.0f;
		m->cast_s = cast_s[h];
		dual_mag = dual_dur = dual_cost = 1.0f, dual_impact = 0;
	}
	if (dual_now) { // (both hands cast it: the left's look the right's)
		fx->hand[HAND_LEFT] = fx->hand[HAND_RIGHT];
		fx->dual = 1;
	}
	// Fire Runes: lying in wait; someone stepping on one sets it off; spent after a while
	for (int i = 0; i < MAX_RUNES; i++) {
		if (runes[i][3] < 0.0f)
			continue;
		runes[i][3] += dt;
		int burst = 0;
		for (int a = 0; a < actors_count() && !burst; a++) {
			float pos[3];
			if (actors_where(a, pos) && fabsf(pos[1] - runes[i][1]) < 1.5f &&
			    hypotf(pos[0] - runes[i][0], pos[2] - runes[i][2]) < RUNE_TRIGGER_M)
				burst = 1;
		}
		burst |= creatures_near(runes[i], RUNE_TRIGGER_M);
		if (burst) { // (its blast burns everything about it)
			if (fx->bursts < MAX_RUNES) {
				fx->burst_element[fx->bursts] = rune_element[i];
				memcpy(fx->burst_pos[fx->bursts++], runes[i], 3 * sizeof(float));
			}
			if (rune_element[i] == ELEMENT_FIRE)
				traps_fire_at(runes[i], spells[SPELL_FIRERUNE].area);
			creatures_set_spell_element(rune_element[i]);
			if (creatures_area(runes[i], spells[SPELL_FIRERUNE].area, rune_damage[i]) +
			    people_area(runes[i], spells[SPELL_FIRERUNE].area, rune_damage[i]))
				skills_use(SK_DESTRUCTION, magic_cost(SPELL_FIRERUNE) * 0.5f);
			creatures_set_spell_element(-1);
		}
		if (burst || runes[i][3] > duration(SPELL_FIRERUNE)) {
			runes[i][3] = -1.0f;
			continue;
		}
		memcpy(fx->rune_pos[fx->runes], runes[i], 3 * sizeof(float));
		fx->rune_element[fx->runes] = rune_element[i];
		fx->rune_age[fx->runes++] = runes[i][3];
	}
	if (armour_left > 0.0f && (armour_left -= dt) <= 0.0f)
		ui_notice("Your skin softens", 2.0f);
	if (muffle_left > 0.0f && (muffle_left -= dt) <= 0.0f)
		ui_notice("Muffle has worn off", 2.0f);
	if (ward_left > 0.0f)
		ward_left -= dt;
	if (detect_left > 0.0f)
		detect_left -= dt;
	if (hazard_left > 0.0f) { // a storm laid (Blizzard): those in it, every half second; its swirl
		hazard_left -= dt;
		if ((hazard_tick -= dt) <= 0.0f) {
			hazard_tick = HAZARD_TICK_S;
			creatures_area_dot(hazard_at, hazard_area, hazard_dps, HAZARD_HOLD_S, HAZARD_HOLD_S);
		}
		if ((hazard_fx -= dt) <= 0.0f) {
			const float a = (float)rand() / (float)RAND_MAX * 6.2831853f, r = (float)rand() / (float)RAND_MAX * hazard_area;
			const float at[3] = {hazard_at[0] + cosf(a) * r, hazard_at[1] + 0.5f + (float)rand() / (float)RAND_MAX * 2.0f,
			                     hazard_at[2] + sinf(a) * r};
			spellfx_burst(at, hazard_element, 0.6f);
			hazard_fx = 0.05f;
		}
	}
	if (guide_left > 0.0f)
		guide_left -= dt;
	if (bound_left > 0.0f && (bound_left -= dt) <= 0.0f)
		bound_end = 1;
	if (cloak_left > 0.0f) { // a cloak: those about you burn (or freeze, or crackle)
		cloak_left -= dt;
		const float ring[4] = {eye[0], eye[1] - 1.6f, eye[2], CLOAK_RADIUS_M};
		if (creatures_hurt_where(circle_inside, (void *)ring, cloak_dps * dt))
			skills_use(SK_DESTRUCTION, cloak_dps * dt);
		if ((cloak_fx -= dt) <= 0.0f) { // (its look: bursts of it about you)
			const float a = (float)rand() / (float)RAND_MAX * 6.2831853f;
			const float at[3] = {eye[0] + cosf(a) * 0.7f, eye[1] - 1.0f + (float)rand() / (float)RAND_MAX, eye[2] + sinf(a) * 0.7f};
			spellfx_burst(at, cloak_element, 0.3f);
			cloak_fx = 0.15f;
		}
		if (cloak_left <= 0.0f)
			ui_notice("The cloak fades", 2.0f);
	}
	if (candle_left > 0.0f) {
		candle_left -= dt;
		const float yr = yaw_deg * (float)M_PI / 180.0f;
		const float want[3] = {eye[0] + cosf(yr) * 0.5f, eye[1] + 0.5f, eye[2] + sinf(yr) * 0.5f};
		float k = candle_fixed ? 0.0f : candle_placed ? fminf(1.0f, dt * CANDLE_FOLLOW) : 1.0f;
		for (int i = 0; i < 3; i++)
			candle_at[i] += (want[i] - candle_at[i]) * k;
		candle_placed = 1;
		fx->candle = 1;
		memcpy(fx->candle_pos, candle_at, sizeof(candle_at));
	}
}

static int learned(const char *name) {
	for (int i = 0; i < other_count; i++)
		if (!strcmp(other[i], name))
			return 1;
	return 0;
}

int magic_known(int spell) {
	if (spell >= SPELL_COUNT && spell < spell_count) // (a tome spell: learned by name)
		return learned(spells[spell].name);
	return spell >= 0 && spell < SPELL_COUNT && (known >> spell & 1);
}

int magic_castable_name(const char *name) {
	for (int s = SPELL_COUNT; s < spell_count; s++)
		if (!strcmp(spells[s].name, name))
			return 1;
	return 0;
}

unsigned magic_known_mask(void) {
	return known;
}

void magic_set_known_mask(unsigned mask) {
	known = mask & ((1u << SPELL_COUNT) - 1);
	for (int h = 0; h < 2; h++) // (a hand's spell not known: empty)
		if (hand_spell[h] >= 0 && hand_spell[h] < spell_count && !magic_known(hand_spell[h]))
			hand_spell[h] = -1;
}

void magic_new_character(void) {
	// Skyrim's start: Flames and Healing (the rest from spell tomes)
	magic_set_known_mask(1u << SPELL_FLAMES | 1u << SPELL_HEALING);
	other_count = 0;
}

int magic_learn(const char *key, const char *name) {
	for (int s = 0; s < SPELL_COUNT; s++)
		if (!strcmp(spells[s].key, key)) {
			if (magic_known(s))
				return 0;
			known |= 1u << s;
			return 1;
		}
	if (learned(name))
		return 0;
	if (other_count < MAGIC_OTHER_MAX)
		snprintf(other[other_count++], MAGIC_KEY_LEN, "%s", name);
	return magic_castable_name(name) ? 1 : 2;
}

int magic_other_count(void) {
	return other_count;
}

const char *magic_other(int i) {
	return i >= 0 && i < other_count ? other[i] : "";
}

void magic_add_other(const char *name) {
	if (name[0] && !learned(name) && other_count < MAGIC_OTHER_MAX)
		snprintf(other[other_count++], MAGIC_KEY_LEN, "%s", name);
}

void magic_set_other(const char names[][MAGIC_KEY_LEN], int n) {
	other_count = 0;
	for (int i = 0; i < n && i < MAGIC_OTHER_MAX; i++)
		if (names[i][0])
			snprintf(other[other_count++], MAGIC_KEY_LEN, "%s", names[i]);
}
