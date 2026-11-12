#ifndef PERKS_H
#define PERKS_H

#include <stdint.h>

// Skyrim's perk trees (pack/perks.txt, see converter/perks.py): per skill a constellation of
// perks (some with ranks), each needing a skill level and the perks that lead to it. Every
// player level gives a perk point (skills.h) to spend on one. A perk's effects are Skyrim's
// entry points - a value of the game's (attack damage, spell cost, armour rating...) set,
// added to or multiplied while the entry's conditions hold (the weapon's kind, the spell's
// school...) - and abilities that raise an actor value (carry weight, magicka regeneration).

#define PERKS_MAX 256
#define PERK_NODES_MAX 24
#define PERK_RANKS_MAX 5

// Skyrim's entry points (the ones the engine uses)
enum {
	EP_MOD_POWER_ATTACK_STAMINA = 27, EP_MOD_POWER_ATTACK_DAMAGE = 28, EP_MOD_SPELL_MAGNITUDE = 29,
	EP_MOD_SPELL_DURATION = 30, EP_MOD_ARMOR_WEIGHT = 32, EP_MOD_ATTACK_DAMAGE = 35, EP_MOD_INCOMING_DAMAGE = 36,
	EP_MOD_SPELL_COST = 38, EP_MOD_PERCENT_BLOCKED = 39, EP_MOD_SNEAK_ATTACK_MULT = 18,
	EP_MOD_PICKPOCKET_CHANCE = 56, EP_MOD_DETECTION_SNEAK = 57, EP_MOD_LOCKPICK_SWEET_SPOT = 59,
	EP_MOD_ARMOR_RATING = 85,
	EP_CRIT_CHANCE = 1, EP_CRIT_DAMAGE = 2, EP_MOD_BUY_PRICES = 8, EP_MOD_BOW_ZOOM = 20, EP_MOD_RECOVER_ARROW = 21,
	EP_MOD_TARGET_DAMAGE_RESIST = 37, EP_MOD_DEFLECT_ARROW = 40, EP_MOD_INCOMING_SPELL_MAGNITUDE = 41,
	EP_MOD_FALL_DAMAGE = 58, EP_MOD_SELL_PRICES = 60, EP_MOD_ENCHANT_POWER = 77,
	EP_MOD_BASH_DAMAGE = 26, EP_APPLY_BASH_HIT_SPELL = 52, EP_CAN_DUAL_CAST = 75
};
// Actor values abilities raise
enum { AV_CARRY_WEIGHT = 32, AV_MAGICKA_RATE = 156, AV_STAMINA_RATE = 157,
       AV_UNARMED_DAMAGE = 35, AV_ABSORB_CHANCE = 83, AV_WEAPON_SPEED = 85, AV_MOVEMENT_NOISE = 92, AV_FENCE = 93,
       AV_MERCHANT = 94, AV_REFLECT_DAMAGE = 163 };
// Skyrim's entry points the engine reads besides (perks_apply)
enum { EP_MOD_INCOMING_STAGGER = 33, EP_SET_SWEEP_ATTACK = 50, EP_MOD_PLAYER_INTIMIDATION = 43, EP_MOD_SOUL_GEM_RECHARGE = 49,
       EP_APPLY_REANIMATE_SPELL = 53, EP_MOD_CASTING_SOUND = 55, EP_CAN_PICKPOCKET_EQUIPPED = 61,
       EP_SET_PROGRESSION_PICKING = 63, EP_LOCKPICKS_UNBREAKABLE = 65, EP_MOD_COMMANDED_LIMIT = 68,
       EP_APPLY_SNEAKING_SPELL = 69, EP_MOD_WARD_ABSORB = 71, EP_MOD_TEMPERING = 76, EP_MOD_SOUL_TO_WEAPON = 78,
       EP_MOD_ENCHANTMENTS_ALLOWED = 80, EP_MOD_POISON_DOSES = 84, EP_MOD_LOCKPICK_CRIME = 86, EP_MOD_SPELL_RANGE = 88,
       EP_MOD_KEY_REWARD = 90 };

// Skyrim's power attack kinds (keywords)
#define POWER_STANDING 0x000914e5u
#define POWER_FORWARD 0x000914e6u

// What an entry's conditions are asked about: the weapon / spell / item in question
typedef struct {
	const uint32_t *keywords; // its keywords (form ids)
	int keyword_count;
	int school;               // a spell's skill (actor value 18..22), -1
	uint32_t casting_perk;    // a spell's casting perk (Novice Destruction...), 0
	uint32_t power_attack;    // a power attack's kind (Skyrim's keyword: POWER_STANDING...), 0
	int sneaking;
	int lock_level;           // a lock's (Skyrim's 1 novice .. 100 master), with lock_known
	int lock_known;
	int blocking;             // the player's blocking (Deflect Arrows, Elemental Protection)
	uint32_t item_form;       // the item asked about (pickpocketing: Cutpurse's gold), 0
	int target_asleep;        // the one it's done to is asleep (Night Thief)
} PerkContext;

void perks_init(const char *pack_dir);
void perks_reset(void); // (a new game: none taken)

// The tree of a skill: its nodes (0 is the root, no perk)
int perks_node_count(int skill);
// A node: its position (Skyrim's grid units), the nodes it leads to, its ranks (perk indices)
int perks_node(int skill, int node, float *x, float *y, const int **links, int *link_count, const int **ranks,
               int *rank_count);
const char *perks_name(int perk);
const char *perks_description(int perk);
int perks_level(int perk); // the skill level it needs
// Ranks of a node taken (0..rank count)
int perks_node_taken(int skill, int node);
// Whether its next rank can be taken now; if not, why ("Requires One-handed 30")
int perks_can_take(int skill, int node, char *why, int why_len);
// Takes the node's next rank (spending a perk point); 1 if it was taken
int perks_take(int skill, int node);
int perks_has(int perk);
int perks_grant_named(const char *name); // (a test's: taken by its name, no points; its index, -1)
int perks_has_form(uint32_t form); // (a perk by its Skyrim form id)
int perks_taken_count(void);

// The value with every taken perk's entries for the entry point applied (ctx: NULL when the
// entry's conditions only ask about the player)
float perks_apply(int entry_point, float value, const PerkContext *ctx);
// What taken perks' abilities add to an actor value
float perks_av(int av);

// Saving: the taken perks as bits (PERKS_MAX / 8 bytes)
void perks_save(uint8_t bits[PERKS_MAX / 8]);
void perks_load(const uint8_t bits[PERKS_MAX / 8]);

#endif
