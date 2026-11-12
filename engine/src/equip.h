#ifndef EQUIP_H
#define EQUIP_H

#include <stdint.h>

// Wearing and wielding what's in the inventory (pack/items/gear_<family>.txt, converter/gear.py):
// armour and clothes onto the player (loaded the first time they're put on), shields and
// the torch in the left hand, weapons in the right hand (two-handed ones and bows take both).
// The starting outfit is pack/items/start.txt.

enum { WEAP_NONE = -1 }; // else Skyrim's animation type: 1 sword, 2 dagger, 3 axe, 4 mace, 5 greatsword,
                         // 6 battleaxe / warhammer, 7 bow, 8 staff, 9 crossbow

void equip_init(const char *pack_dir);
// The player's character changed (the race menu): loads it (actors_player_load) with its
// body family's gear ("human", "khajiit", "argonian"), and puts back on what was worn
int equip_change_body(const char *family, const char *stem, const unsigned char tone[3]);
// A new game: the starting outfit into the inventory, worn as it says
void equip_new_game(void);
// Nothing worn or carried (the opening's prisoner, before the rags)
void equip_strip(void);
// A game saved before the starting shields: given them now
void equip_grant_start_shields(void);
int equip_wearable(int item);  // armour, clothes, shields, weapons, the torch
int equip_worn(int item);      // worn / held
// Puts it on / takes it off (CROSS in the inventory); 0 if it can't be
int equip_toggle(int item);
void equip_set_locked(int on); // (hands bound: equipping does nothing)
// The torch went into the left hand another way (the D-pad): a shield or a two-handed weapon
// comes off
void equip_free_left_hand(void);
int equip_shield_worn(void); // (blocking with it)
int equip_ammo(void);        // the arrows equipped (item), -1
// Dual wielding: a one-handed weapon into / out of the left hand (SQUARE in the inventory)
int equip_left_toggle(int item);
int equip_left_weapon(int *kind); // the weapon in the left hand (item, -1) and its kind
void equip_free_left_weapon(void);
// The weapon in the right hand (item, -1) and its kind
int equip_weapon(int *kind);
// Anything worn or held that isn't carried any more (none of it in the pack): taken off - each frame
void equip_drop_uncarried(void);
// Each frame: weapons drawn (spells readied) or sheathed
void equip_set_drawn(int drawn);
// Saving: what's worn (forms) with the inventory (items.c's file); after loading, put back on
int equip_save(uint32_t *forms, int max);
void equip_restore(const uint32_t *forms, int n);

// Poisons (Skyrim's: put on a weapon, spent on its next strike): the right hand's weapon, the
// left's, or the next arrow loosed. The weapon it'd go on (its name; NULL: nothing there to
// take it), putting one on (from the inventory: 1 if it took), its harm taken by a strike
enum { EQUIP_POISON_RIGHT, EQUIP_POISON_LEFT, EQUIP_POISON_ARROW, EQUIP_POISON_SLOTS };
const char *equip_poison_target(int slot);
int equip_apply_poison(int slot, int poison);
// A blow landed: the poison on it (item, -1 none), spent (Concentrated Poison: after two hits)
int equip_take_poison(int slot);
int equip_poisoned(int slot);

#endif
