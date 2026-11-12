#ifndef ITEMS_H
#define ITEMS_H

#include <stdint.h>

#include "vitals.h"

// Everything the player can carry (pack/items/items.txt, see converter/items.py): Skyrim's
// items with their names, weights and values; the player's inventory and gold; what's in
// each container (rolled from Skyrim's leveled lists at the player's level the first time
// it's opened, then kept); which loose things have been taken. Saved with the game
// (ux0:data/opensky_items.dat).

enum { IT_MISC, IT_WEAP, IT_ARMO, IT_ALCH, IT_INGR, IT_BOOK, IT_KEYM, IT_AMMO, IT_SLGM, IT_SCRL, IT_LIGH, IT_TYPES };

// Skyrim's inventory categories
enum { CAT_ALL, CAT_WEAPONS, CAT_APPAREL, CAT_POTIONS, CAT_SCROLLS, CAT_FOOD, CAT_INGREDIENTS, CAT_BOOKS, CAT_KEYS,
       CAT_MISC, CAT_COUNT };

// A scroll's spell (Skyrim's SCRL): the spell of its main effect, cast at the scroll's strength
typedef struct {
	char kind[16];  // how the engine casts it: a tome spell's kind (magic.c), "muffle", "candlelight", "rune_fire"
	int delivery;   // Skyrim's: 0 self, 2 aimed, 4 a place
	float charge;   // seconds held before it can be let go (Skyrim's charge time)
	float magnitude, area, seconds; // damage / health / level...; its reach (m); how long
	char extra[32]; // a summons' creature; a blast's nearer rings ("75@19.81,50@7.62")
} ItemScroll;

// Skyrim's magic effects (MGEF) on potions, food and ingredients (items.txt M lines)
#define MGEF_HOSTILE 0x1u              // (alchemy: a poison's)
#define MGEF_RECOVER 0x2u              // its actor value comes back when it ends (a fortify)
#define MGEF_DETRIMENTAL 0x4u
#define MGEF_POWER_MAGNITUDE 0x200000u // (alchemy: the potion's strength scales its magnitude)
#define MGEF_POWER_DURATION 0x400000u  // (... its duration)
enum { MGEF_KW_RESTORE = 1, MGEF_KW_BENEFICIAL = 2, MGEF_KW_HARMFUL = 4 }; // (the alchemy perks ask)
enum { ARCH_VALUE = 0, ARCH_INVISIBILITY = 11, ARCH_PARALYSIS = 21, ARCH_PEAK_VALUE = 34 }; // (Skyrim's archetypes)
typedef struct MagicEffect {
	char eid[48], name[40];
	const char *desc; // Skyrim's words, with <mag> and <dur> (its words)
	uint32_t flags;
	float cost;       // its base cost (a potion's value)
	int archetype, av, keywords;
} MagicEffect;
typedef struct {
	int mgef;
	float magnitude, seconds;
} ItemEffect;
#define ITEM_EFFECTS_MAX 8
int items_magic_effect_count(void);
const MagicEffect *items_magic_effect(int mgef); // NULL: none
int items_magic_effect_find(const char *eid);    // by editor id, -1
// An item's effects (a potion's, food's, an ingredient's four, in order); how many
int items_effects(int item, ItemEffect *out, int max);
// A weapon's or armour's enchantment (Skyrim's: on each blow, or while worn): its effects and
// what resists each (41 fire, 42 shock, 43 frost, 44 magic, -1; resist may be NULL); 0 if none
int items_enchantment(int item, ItemEffect *out, int *resist, int max);
// One effect in Skyrim's words (its words)
void items_effect_text(int mgef, float magnitude, float seconds, char *out, int size);
// The ingredient effects the player knows: bits 1, 2, 4, 8 for its first .. fourth (saved)
int items_known(int item);
void items_learn(int item, int bits);
// A potion or poison the player made (alchemy.c): its item (the same one again if it was made
// before; saved with the inventory), -1 if there's no room for more kinds
#define ITEMS_CREATED_MAX 96
int items_create_potion(const char *name, int poison, int value, const ItemEffect *e, int n);

void items_init(const char *pack_dir);
int items_category(int item); // CAT_* (never CAT_ALL)
const char *items_category_name(int category);
int items_count(void);
const char *items_name(int item);
int items_type(int item);
float items_weight(int item);
int items_value(int item);
int items_usable(int item); // food, potions and ingredients (and poisons: put on a weapon, not drunk)
int items_is_poison(int item);
float items_poison_damage(int item); // a poison's harm to health where it strikes
int items_model(int item);   // its model (objects model index) for the inventory's 3D view, -1
int items_stat(int item);    // a weapon's / an arrow's damage, armour's rating (Skyrim's base)
int items_keywords(int item, const uint32_t **out); // a weapon's / armour's keywords (form ids)
// An arrow's flight: its model in flight (-1: none), speed (m/s) and gravity (x g)
int items_flight(int item, float *speed, float *gravity);
// A scroll's spell, NULL if it isn't one (or not one the engine can cast)
const ItemScroll *items_scroll(int item);
// A staff's spell (items.txt T: its enchantment, as a scroll's), NULL if it isn't one
const ItemScroll *items_staff(int item);
// Books: the skill one teaches (a skill book: -1 none), the spell a tome teaches (its editor id
// into key, its name; 0 none), its text (lines; pages split by '\f'; 0 if there's none)
int items_book_skill(int item);
int items_book_spell(int item, char *key, int key_size, const char **name);
int items_book_text(int item, char *out, int size);
// A skill book read: 1 the first time (its skill rises), 0 after; the ones read (forms, saved)
#define ITEMS_BOOKS_READ_MAX 96
int items_book_first_read(int item);
int items_books_read(uint32_t *out, int max);
void items_set_books_read(const uint32_t *forms, int n);
void items_describe(int item, char *out, int size); // its effects, as Skyrim words them
// Eats / drinks one from the inventory: its effects on the player (effects.h); 1 if it was used.
// An ingredient eaten: all its effects, its first learned (alchemy.c: more with Experimenter)
int items_use(int item, Vitals *v);
const char *items_container_name(int container);
int items_npc_pockets(const char *editor_id); // an NPC's inventory as a container index, -1
// Pickpocketing a stack (percent): Skyrim's settings for the thief's and target's Pickpocket
// skill, the weight and value taken
int items_pickpocket_chance(int item, int count, int skill, int target_skill);

// The player's inventory (gold is an item too)
int items_have(int item);
void items_add(int item, int count);
int items_gold(void);
float items_carried(void);              // total weight
float items_carry_max(const Vitals *v); // Skyrim: 300, +5 per 10 stamina gained
int items_inventory(int *out, int max); // what's carried (item indices), sorted by kind and name

// Stolen goods (Skyrim's mark: only fences buy them, guards take them): how many of what's
// carried are; marked (count < 0: unmarked); the container being looked through when it's
// someone's (kind 1: taking is theft, reported if seen - items_take_theft, its worth since last
// asked; 2: a pocket picked, stolen but unnoticed; 0: no one's)
int items_stolen(int item);
void items_mark_stolen(int item, int count);
void items_set_theft(int handle, int kind);
int items_take_theft(void);

// Containers, by placed reference: their contents, rolled on first opening
int items_open_container(uint32_t ref, int container); // a handle for the calls below
int items_in_container(int handle, int *items, int *counts, int max);
uint32_t items_container_ref(int handle); // (its placed reference: Skyrim's form id)
void items_take(int handle, int item, int count); // into the inventory
void items_put(int handle, int item, int count);  // from the inventory (stored: kept for good)
void items_container_add(int handle, int item, int count); // (things found in it: arrows in a body)
// Skyrim's cell reset: containers rolled `hours` ago or more are forgotten (they refill when next
// opened) - not ones you stored things in, nor bodies (items_forget_container: with their
// creature); how many. Handles change: only while none is open. The clock: game hours
void items_set_clock(float game_hours);
int items_reset_containers(float hours);
void items_forget_container(uint32_t ref);

// Things dropped from the inventory: lying where they fell (place: interior.h's index, -1
// Tamriel, -2 - n a walled city), saved with the game, picked up again by their reference
int items_drop(int item, int count, const float pos[3], float yaw, int place); // how many dropped
int items_dropped_count(void);
// The index'th dropped thing if it lies in place: 1 and what, how many, where, its reference
int items_dropped(int index, int place, int *item, int *count, float pos[3], float *yaw, uint32_t *ref);
int items_pick_up(uint32_t ref); // into the inventory: how many

// Loose things taken (by placed reference)
int items_taken(uint32_t ref);
// A plant harvested (game hours): not to be harvested again till it's grown back (ten days, Skyrim's)
int items_harvested(uint32_t ref, float now_hours);
void items_set_harvested(uint32_t ref, float now_hours);
void items_set_taken(uint32_t ref);

// Items by Skyrim form id (-1: not in the pack) and back
int items_by_form(uint32_t form);
int items_form(int item);
int items_find_name(const char *name); // (an item by its name: -1 none)
int items_find_gold(void);
// What's worn (forms), kept with the saved inventory (equip.c fills it before items_save and
// reads it after items_load)
void items_set_worn(const uint32_t *forms, int n);
int items_worn(uint32_t *forms, int max);

// Locks picked open (by placed reference)
int items_unlocked(uint32_t ref);
void items_set_unlocked(uint32_t ref);
void items_set_locked(uint32_t ref); // (a gate closed again)

// With the saved game: write after save_write, read after save_read; a new game starts with
// items_new_game (an empty inventory but for 50 lockpicks, for trying lockpicking)
// Where items_save / items_load keep the inventory (a save slot's file, save.h)
void items_set_save_path(const char *path);
void items_save(void);
int items_load(void);
void items_load_corpses_by(int (*fn)(int old)); // (NULL: as saved) // the inventory file's version (0: none: a fresh one)
void items_reset(void);
void items_new_game(void);

// Merchants (Skyrim's vendor factions): the one an NPC is (-1), whether they trade at this
// hour, their stock (a container handle, restocked every two days; their gold is in it),
// whether they'd buy an item (by its keywords), and Skyrim's prices with Speech (fBarterMin,
// fBarterMax) and the perks' multiplier (Haggling, Allure)
int items_vendor(const char *npc_id);
int items_vendor_open_now(int vendor, float hour);
int items_vendor_stock(int vendor);
int items_vendor_buys(int vendor, int item);
int items_vendor_fence(int vendor); // (buys stolen goods only)
// Investor: put 500 gold into the store (1 done); whether you have; the stores invested in, saved and loaded
int items_vendor_invest(int vendor);
int items_vendor_invested(int vendor);
void items_vendors_invested(uint32_t *bits, int words, int save);
int items_buy_price(int item, int speech, float perk_mult);
int items_sell_price(int item, int speech, float perk_mult);
// An item ('I') or a leveled list ('L', rolled at your level) into your pack (a quest's
// reward); how many kinds of thing it gave, and the first (its name for the notice)
int items_give_rolled(int kind, int index, int count, int *first);

// Crafting (Skyrim's recipes): those of a station (1 forge, 2 smelter, 3 tanning rack, 4
// cooking pot: their numbers), one's parts and the perks it needs (forms), making it
int items_recipes(int station, int *out, int max);
int items_recipe(int recipe, int *made, int *count, const int **parts, const int **need, int *nparts,
                 const uint32_t **perks, int *nperks);
int items_craft(int recipe); // (1: made; the parts used)

// Enchanting: the enchantments learned (by disenchanting), learning one (the item's destroyed),
// a soul gem's soul (0 empty .. 5 grand), a weapon or armour made enchanted (saved with the
// item it was)
int items_learned_count(void);
int items_learned(int i, int *mgef, float *magnitude, float *seconds);
int items_disenchant(int item);
int items_soul(int item);
// An enchanted weapon's charge (Skyrim's: each strike spends its enchantment's cost): 1 if it has one
// (left: the share remaining); spending a strike's (0: not enough left - its enchantment does nothing);
// recharged from the fullest soul gem carried (used up; 0 none, or it's full)
int items_charge(int item, float *left);
int items_charge_use(int item);
int items_recharge(int item);
int items_create_enchanted(int base, const char *name, int value, const ItemEffect *e, int n);
void items_add_charge(int item, float amount); // (an enchanted weapon's charge, that much back: Soul Siphon)
// Improving at a grindstone or workbench: an item's level (1 Fine .. 6 Legendary, 0), the
// item made at a new level (-1: not better than it is)
int items_temper_level(int item);
int items_made_from(int item); // (one made here: the item it was made from, -1)
int items_temper(int item, int level);
// An ore vein's or chopping block's (by its reference): what it gives, how many a use, how
// many in all (0: never runs out), the tools that work it (items); 0 if it's neither
int items_resource(uint32_t ref, int *item, int *per_use, int *total, const int **tools, int *ntools);
int items_find_named(const char *name); // (Skyrim's item by its name, -1)
// Debug: ingots, leather, ore, hides, tools, food, ingredients, soul gems and gold to craft with
int items_give_crafting_kit(void);

#endif
