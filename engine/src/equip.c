#include "equip.h"

#include "gamelog.h"
#include "enchant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>

#include "actors.h"
#include "fileio.h"
#include "items.h"
#include "magic.h"
#include "perks.h"
#include "sfx.h"
#include "ui.h"

#define MAX_GEAR 8192 // (gear_<family>.txt: 4137 lines in Skyrim's - the player's own clothes last, and were cut off)
#define SAVE_WARDROBE 32
#define MAX_PLAYER_GEAR 64 // (actors.c's MAX_ITEMS: what the player holds at once)
#define SHIELD_SLOT (1u << 9)
#define LEFT_HAND_MARK 0x80000000u // (a saved worn form held in the left hand)
#define TWO_HANDED(k) ((k) >= 5) // greatswords, battleaxes, warhammers, bows, staves, crossbows

enum { KIND_ARMOUR, KIND_SHIELD, KIND_WEAPON, KIND_TORCH, KIND_QUIVER };

typedef struct {
	int item;
	int kind, wardrobe; // wardrobe: the player's own clothes (player.sknpc), -1: a gear file
	char gear[16];
	uint32_t slots;
	int weapon_kind;
	int actor; // its wardrobe index on the player once loaded, -1
	int left_actor; // a one-handed weapon's left-hand twin on the player (dual wielding), -1
} Gear;

static char pack[128], family[16] = "human"; // (the body family's gear: races.h)
static Gear gear[MAX_GEAR];
static int gear_count;
static int right_weapon = -1; // gear index of the weapon in hand
static int left_weapon = -1;  // ... in the left hand (dual wielding)
static int ammo = -1;         // the arrows equipped (item)

static int left_worn(const Gear *g);

static Gear *gear_of(int item) {
	for (int i = 0; i < gear_count; i++)
		if (gear[i].item == item)
			return &gear[i];
	// One the player made (improved at a grindstone or workbench, enchanted): worn as the
	// one it was made from looks (it had no entry: it couldn't be equipped at all)
	const int base = items_made_from(item);
	if (base < 0 || gear_count >= MAX_GEAR)
		return NULL;
	for (int i = 0; i < gear_count; i++)
		if (gear[i].item == base) {
			Gear *g = &gear[gear_count++];
			*g = gear[i];
			g->item = item, g->actor = g->wardrobe, g->left_actor = -1;
			return g;
		}
	return NULL;
}

// The gear map of the current body family (items/gear_<family>.txt)
static void load_map(void) {
	gear_count = 0;
	right_weapon = left_weapon = -1;
	char path[256];
	snprintf(path, sizeof(path), "%sitems/gear_%s.txt", pack, family);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line && gear_count < MAX_GEAR; line = strtok(NULL, "\n")) {
		unsigned form, slots;
		char what[12], name[16], kind[12];
		int wkind;
		if (sscanf(line, "%x %11s %15s %11s %u %d", &form, what, name, kind, &slots, &wkind) != 6)
			continue;
		Gear *g = &gear[gear_count];
		memset(g, 0, sizeof(*g));
		g->item = items_by_form(form);
		if (g->item < 0)
			continue;
		g->kind = !strcmp(kind, "shield") ? KIND_SHIELD : !strcmp(kind, "weapon") ? KIND_WEAPON
		        : !strcmp(kind, "torch") ? KIND_TORCH : !strcmp(kind, "quiver") ? KIND_QUIVER : KIND_ARMOUR;
		g->wardrobe = !strcmp(what, "wardrobe") ? atoi(name) : -1;
		g->actor = g->wardrobe;
		g->left_actor = -1;
		snprintf(g->gear, sizeof(g->gear), "%s", g->wardrobe < 0 ? name : "");
		g->slots = slots, g->weapon_kind = wkind;
		gear_count++;
	}
	free(buf);
}

void equip_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	load_map();
}

int equip_change_body(const char *body_family, const char *stem, const unsigned char tone[3]) {
	// What's worn now (gear by form, the wardrobe by index), to put back on the new body
	uint32_t forms[64];
	const int n = equip_save(forms, 64);
	int wardrobe[SAVE_WARDROBE], wn = actors_player_base_items();
	if (wn > SAVE_WARDROBE)
		wn = SAVE_WARDROBE;
	for (int i = 0; i < wn; i++)
		actors_player_item(i, &wardrobe[i]);
	const unsigned long long t0 = sceKernelGetProcessTimeWide();
	if (!actors_player_load(stem, tone))
		return 0;
	const unsigned long long t1 = sceKernelGetProcessTimeWide();
	snprintf(family, sizeof(family), "%s", body_family);
	load_map();
	const unsigned long long t2 = sceKernelGetProcessTimeWide();
	for (int i = 0; i < wn && i < actors_player_base_items(); i++)
		actors_player_set_worn(i, wardrobe[i]);
	equip_restore(forms, n);
	game_log("equip: a new body (%s) - model %u ms, gear map %u ms, worn again %u ms", stem, (unsigned)((t1 - t0) / 1000),
	         (unsigned)((t2 - t1) / 1000), (unsigned)((sceKernelGetProcessTimeWide() - t2) / 1000));
	return 1;
}

void equip_new_game(void) {
	char path[256];
	snprintf(path, sizeof(path), "%sitems/start.txt", pack);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		unsigned form;
		int count, worn;
		if (sscanf(line, "%x %d %d", &form, &count, &worn) == 3)
			items_add(items_by_form(form), count);
	}
	free(buf);
}

void equip_strip(void) {
	for (int i = 0; i < actors_player_items(); i++) // (the character's own clothes too)
		actors_player_set_worn(i, 0);
	right_weapon = left_weapon = -1, ammo = -1;
	actors_player_weapon(-1);
	actors_player_left_weapon(-1);
	actors_player_weapon_drawn(0);
	int carried[512];
	const int n = items_inventory(carried, 512);
	for (int i = 0; i < n; i++)
		items_add(carried[i], -items_have(carried[i]));
}

void equip_grant_start_shields(void) {
	char path[256];
	snprintf(path, sizeof(path), "%sitems/start.txt", pack);
	long size = 0;
	char *text = file_read_all(path, &size), *buf = text ? realloc(text, size + 1) : NULL;
	if (!buf) {
		free(text);
		return;
	}
	buf[size] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
		unsigned form;
		int count, worn;
		if (sscanf(line, "%x %d %d", &form, &count, &worn) != 3)
			continue;
		const int item = items_by_form(form);
		const Gear *g = gear_of(item);
		if (g && g->kind == KIND_SHIELD && items_have(item) <= 0)
			items_add(item, count);
	}
	free(buf);
}

int equip_wearable(int item) {
	return gear_of(item) != NULL || items_type(item) == IT_AMMO;
}

static void sync_quiver(void);

int equip_ammo(void) {
	if (ammo >= 0 && items_have(ammo) <= 0) {
		ammo = -1;
		sync_quiver();
	}
	return ammo;
}

static int actor_worn(const Gear *g) {
	int worn = 0;
	if (g->actor >= 0)
		actors_player_item(g->actor, &worn);
	return worn;
}

int equip_worn(int item) {
	if (item >= 0 && item == equip_ammo())
		return 1;
	if (left_weapon >= 0 && gear[left_weapon].item == item && left_worn(&gear[left_weapon]))
		return 1;
	const Gear *g = gear_of(item);
	return g && actor_worn(g);
}

// Room on the player for more gear: one piece loaded but not worn (nor held left) taken off
// for good (loaded again when next put on); 0 if there's none
static int make_room(const Gear *keep) {
	for (int i = 0; i < gear_count; i++) {
		Gear *g = &gear[i];
		if (g == keep || g->wardrobe >= 0)
			continue;
		if (g->actor >= 0 && !actor_worn(g)) {
			actors_player_drop_gear(g->actor);
			g->actor = -1;
			return 1;
		}
		if (g->left_actor >= 0 && i != left_weapon && !left_worn(g)) {
			actors_player_drop_gear(g->left_actor);
			g->left_actor = -1;
			return 1;
		}
	}
	return 0;
}

// Its wardrobe index on the player, loading the gear file the first time
static int ensure_actor(Gear *g) {
	if (g->actor >= 0)
		return g->actor;
	char path[256];
	snprintf(path, sizeof(path), "%sactors/gear/%s/%s.skgear", pack, family, g->gear);
	g->actor = actors_player_add_gear(path, items_name(g->item), g->slots, g->kind == KIND_WEAPON);
	if (g->actor < 0 && actors_player_items() >= MAX_PLAYER_GEAR && make_room(g)) // (full: again, with room)
		g->actor = actors_player_add_gear(path, items_name(g->item), g->slots, g->kind == KIND_WEAPON);
	if (g->actor >= 0 && g->kind == KIND_WEAPON) // (enchanted: it glows its element's colour)
		actors_player_glow(g->actor, enchant_color(g->item));
	return g->actor;
}

static void set_worn(Gear *g, int on) {
	if (ensure_actor(g) >= 0)
		actors_player_set_worn(g->actor, on);
}

// The left-hand twin on the player, loading <gear>l.skgear the first time
static int ensure_left(Gear *g) {
	if (g->left_actor >= 0)
		return g->left_actor;
	char path[256];
	snprintf(path, sizeof(path), "%sactors/gear/%s/%sl.skgear", pack, family, g->gear);
	g->left_actor = actors_player_add_gear(path, items_name(g->item), 0, 1);
	if (g->left_actor < 0 && actors_player_items() >= MAX_PLAYER_GEAR && make_room(g))
		g->left_actor = actors_player_add_gear(path, items_name(g->item), 0, 1);
	if (g->left_actor >= 0)
		actors_player_glow(g->left_actor, enchant_color(g->item));
	return g->left_actor;
}

static int left_worn(const Gear *g) {
	int worn = 0;
	if (g->left_actor >= 0)
		actors_player_item(g->left_actor, &worn);
	return worn;
}

// The equipped arrows' quiver on the back (Skyrim's: worn while they're equipped), the others off
static void sync_quiver(void) {
	for (int i = 0; i < gear_count; i++)
		if (gear[i].kind == KIND_QUIVER && (gear[i].item == ammo) != (actor_worn(&gear[i]) != 0))
			set_worn(&gear[i], gear[i].item == ammo);
}

static int locked; // (hands bound - Skyrim's opening: nothing put on, taken off or drawn)

void equip_set_locked(int on) {
	locked = on;
}

int equip_left_toggle(int item) {
	if (locked)
		return 0;
	Gear *g = gear_of(item);
	if (!g || g->kind != KIND_WEAPON || g->weapon_kind < 1 || g->weapon_kind > 4 || g->wardrobe >= 0) {
		ui_notice("Only one-handed weapons go in the left hand", 2.0f);
		return 0;
	}
	const int idx = (int)(g - gear);
	if (left_weapon == idx) { // (off again)
		actors_player_set_worn(g->left_actor, 0);
		left_weapon = -1;
		return 1;
	}
	if (right_weapon == idx && items_have(item) < 2) { // (one of it: it moves to the left hand)
		set_worn(g, 0);
		right_weapon = -1;
		ui_notice("You carry only one: it moves to your left hand", 2.5f);
	}
	if (ensure_left(g) < 0) {
		ui_notice("Can't be held", 2.0f);
		return 0;
	}
	magic_set_readied(0);
	if (left_weapon >= 0)
		actors_player_set_worn(gear[left_weapon].left_actor, 0);
	magic_set_hand(HAND_LEFT, -1); // (no spell, shield or torch there; nor a two-handed weapon)
	if (right_weapon >= 0 && TWO_HANDED(gear[right_weapon].weapon_kind)) {
		set_worn(&gear[right_weapon], 0);
		right_weapon = -1;
	}
	for (int i = 0; i < gear_count; i++)
		if ((gear[i].kind == KIND_SHIELD || gear[i].kind == KIND_TORCH) && actor_worn(&gear[i]))
			set_worn(&gear[i], 0);
	actors_player_set_worn(g->left_actor, 1);
	left_weapon = idx;
	return 1;
}

void equip_drop_uncarried(void) {
	for (int i = 0; i < gear_count; i++) {
		Gear *g = &gear[i];
		if (g->wardrobe >= 0 || g->kind == KIND_QUIVER || items_have(g->item) > 0)
			continue;
		if (actor_worn(g)) { // (worn, and none of it carried: off - it stayed in the hand for good, unseen in the pack)
			game_log("equip: %s worn but not carried: taken off", items_name(g->item));
			set_worn(g, 0);
		}
		if (right_weapon == i)
			right_weapon = -1;
		if (left_weapon == i) {
			if (g->left_actor >= 0)
				actors_player_set_worn(g->left_actor, 0);
			left_weapon = -1;
		}
	}
}

int equip_left_weapon(int *kind) {
	if (left_weapon < 0 || !left_worn(&gear[left_weapon]) || items_have(gear[left_weapon].item) <= 0 ||
	    (left_weapon == right_weapon && items_have(gear[left_weapon].item) < 2)) {
		if (left_weapon >= 0 && gear[left_weapon].left_actor >= 0)
			actors_player_set_worn(gear[left_weapon].left_actor, 0);
		left_weapon = -1;
		*kind = WEAP_NONE;
		return -1;
	}
	*kind = gear[left_weapon].weapon_kind;
	return gear[left_weapon].item;
}

void equip_free_left_weapon(void) {
	if (left_weapon >= 0 && gear[left_weapon].left_actor >= 0)
		actors_player_set_worn(gear[left_weapon].left_actor, 0);
	left_weapon = -1;
}

int equip_toggle(int item) {
	if (locked)
		return 0;
	if (items_type(item) == IT_AMMO && items_have(item) > 0) { // (arrows: the ones nocked)
		ammo = ammo == item ? -1 : item;
		sync_quiver();
		sfx_play("itm_up", 0.5f, 0.0f);
		return 1;
	}
	Gear *g = gear_of(item);
	if (!g || items_have(item) <= 0) {
		game_log("equip: %s: %s (family %s)", items_name(item), g ? "not carried" : "no gear for it", family);
		return 0;
	}
	const int on = !actor_worn(g);
	if (ensure_actor(g) < 0) {
		game_log("equip: %s: its gear (%s) didn't load", items_name(item), g->gear);
		ui_notice("Can't be worn", 2.0f);
		return 0;
	}
	if (g->kind == KIND_WEAPON) {
		if (on) { // into the right hand (and the left, for two-handed ones); sheathed till drawn
			magic_set_readied(0);
			if (right_weapon >= 0 && &gear[right_weapon] != g)
				set_worn(&gear[right_weapon], 0);
			right_weapon = (int)(g - gear);
			magic_set_hand(HAND_RIGHT, -1);
			if (TWO_HANDED(g->weapon_kind)) {
				magic_set_hand(HAND_LEFT, -1);
				equip_free_left_weapon();
				for (int i = 0; i < gear_count; i++) // (no shield or torch with it)
					if ((gear[i].kind == KIND_SHIELD || gear[i].kind == KIND_TORCH) && actor_worn(&gear[i]))
						set_worn(&gear[i], 0);
			}
		} else {
			right_weapon = -1;
		}
	} else if ((g->kind == KIND_SHIELD || g->kind == KIND_TORCH) && on) {
		// The left hand: no spell there, nor a two-handed weapon, nor a weapon
		magic_set_hand(HAND_LEFT, -1);
		equip_free_left_weapon();
		if (right_weapon >= 0 && TWO_HANDED(gear[right_weapon].weapon_kind)) {
			set_worn(&gear[right_weapon], 0);
			right_weapon = -1;
		}
		for (int i = 0; i < gear_count; i++)
			if (&gear[i] != g && (gear[i].kind == KIND_SHIELD || gear[i].kind == KIND_TORCH) && actor_worn(&gear[i]))
				set_worn(&gear[i], 0);
	}
	set_worn(g, on);
	if (g->kind == KIND_ARMOUR || g->kind == KIND_SHIELD) // (weapons sound as they're drawn)
		sfx_play(g->slots & 0x4 /* body */ && g->kind == KIND_ARMOUR ? "itm_armor" : "itm_cloth", 0.6f, 0.0f);
	return 1;
}

void equip_free_left_hand(void) {
	if (right_weapon >= 0 && TWO_HANDED(gear[right_weapon].weapon_kind) && actor_worn(&gear[right_weapon])) {
		set_worn(&gear[right_weapon], 0);
		right_weapon = -1;
	}
	for (int i = 0; i < gear_count; i++)
		if (gear[i].kind == KIND_SHIELD && actor_worn(&gear[i]))
			set_worn(&gear[i], 0);
}

int equip_weapon(int *kind) {
	if (right_weapon < 0 || !actor_worn(&gear[right_weapon]) || items_have(gear[right_weapon].item) <= 0) {
		right_weapon = -1;
		*kind = WEAP_NONE;
		return -1;
	}
	*kind = gear[right_weapon].weapon_kind;
	return gear[right_weapon].item;
}

void equip_set_drawn(int drawn) {
	int kind, left;
	equip_weapon(&kind);
	equip_left_weapon(&left);
	actors_player_weapon(kind);
	actors_player_left_weapon(left);
	actors_player_weapon_drawn(drawn);
}

int equip_save(uint32_t *forms, int max) {
	int n = 0;
	for (int i = 0; i < gear_count && n < max; i++)
		if (gear[i].wardrobe < 0 && gear[i].kind != KIND_QUIVER && actor_worn(&gear[i]))
			forms[n++] = (uint32_t)items_form(gear[i].item);
	if (equip_ammo() >= 0 && n < max)
		forms[n++] = (uint32_t)items_form(ammo);
	int lk;
	if (equip_left_weapon(&lk) >= 0 && n < max) // (the left hand's: marked)
		forms[n++] = (uint32_t)items_form(gear[left_weapon].item) | LEFT_HAND_MARK;
	return n;
}

void equip_restore(const uint32_t *forms, int n) {
	right_weapon = -1;
	for (int i = 0; i < gear_count; i++)
		if (gear[i].wardrobe < 0 && gear[i].actor >= 0)
			actors_player_set_worn(gear[i].actor, 0);
	ammo = -1;
	equip_free_left_weapon();
	for (int k = 0; k < n; k++) {
		if (forms[k] & LEFT_HAND_MARK) {
			const int lit = items_by_form(forms[k] & ~LEFT_HAND_MARK);
			if (lit >= 0 && items_have(lit) > 0)
				equip_left_toggle(lit);
			continue;
		}
		const int it = items_by_form(forms[k]);
		if (items_type(it) == IT_AMMO) {
			if (items_have(it) > 0)
				ammo = it;
			continue;
		}
		Gear *g = gear_of(it);
		if (!g || items_have(g->item) <= 0)
			continue;
		set_worn(g, 1);
		if (g->kind == KIND_WEAPON)
			right_weapon = (int)(g - gear);
	}
	sync_quiver();
}

int equip_shield_worn(void) {
	for (int i = 0; i < gear_count; i++)
		if (gear[i].kind == KIND_SHIELD && actor_worn(&gear[i]))
			return 1;
	return 0;
}

// Poisons on weapons (Skyrim's: one strike's worth): the right hand's, the left's, the next arrow's
static int poison_on[EQUIP_POISON_SLOTS] = {-1, -1, -1};
static int poison_hits[EQUIP_POISON_SLOTS]; // (hits left)
#define CONCENTRATED_POISON 0x00105f2fu // (Skyrim's perk: twice as many hits)

// The weapon a slot means now (its gear index; -1 none)
static int poison_weapon(int slot) {
	int k;
	if (slot == EQUIP_POISON_RIGHT)
		return equip_weapon(&k) >= 0 && k != 7 && k != 9 ? right_weapon : -1;
	if (slot == EQUIP_POISON_LEFT)
		return equip_left_weapon(&k) >= 0 ? left_weapon : -1;
	return equip_weapon(&k) >= 0 && (k == 7 || k == 9) && equip_ammo() >= 0 ? right_weapon : -1;
}

static void poison_tint(int slot, int on) {
	const int w = slot == EQUIP_POISON_ARROW ? -1 : poison_weapon(slot);
	if (w < 0)
		return;
	const int actor = slot == EQUIP_POISON_LEFT ? gear[w].left_actor : gear[w].actor;
	if (actor >= 0)
		actors_player_tint(actor, on);
}

const char *equip_poison_target(int slot) {
	const int w = poison_weapon(slot);
	if (w < 0)
		return NULL;
	return slot == EQUIP_POISON_ARROW ? items_name(equip_ammo()) : items_name(gear[w].item);
}

int equip_apply_poison(int slot, int poison) {
	if (slot < 0 || slot >= EQUIP_POISON_SLOTS || poison_weapon(slot) < 0 || items_have(poison) <= 0)
		return 0;
	items_add(poison, -1);
	poison_on[slot] = poison;
	poison_hits[slot] = perks_has_form(CONCENTRATED_POISON) ? 2 : 1;
	poison_tint(slot, 1);
	return 1;
}

int equip_take_poison(int slot) {
	if (slot < 0 || slot >= EQUIP_POISON_SLOTS || poison_on[slot] < 0)
		return -1;
	const int poison = poison_on[slot];
	if (--poison_hits[slot] <= 0) {
		poison_tint(slot, 0);
		poison_on[slot] = -1;
	}
	return poison;
}

int equip_poisoned(int slot) {
	return slot >= 0 && slot < EQUIP_POISON_SLOTS && poison_on[slot] >= 0;
}
