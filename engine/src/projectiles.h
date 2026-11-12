#ifndef PROJECTILES_H
#define PROJECTILES_H

#include "objects.h"

// Arrows in flight (and traps' darts): Skyrim's flight model, speed and gravity (the arrow's
// projectile record), flying until they strike a creature (its damage, Skyrim's sounds) or
// the world (where they stick a while). The player's are drawn with the loose things.

// An arrow from pos along dir (unit) at speed (m/s), falling at gravity x g; model: its
// flight model (-1: none, -2: a fire bolt); hurts the first creature it meets for damage (from_player:
// trains Archery, shows its health); hits the player instead if hits_player; item: the arrow it
// is (stuck in the world, it can be taken), -1
int projectiles_fire(const float pos[3], const float dir[3], float speed, float gravity, int model, float damage,
                      int from_player, int hits_player, int item);
// Each frame: flight, what they strike. Returns the damage struck into the player (at feet,
// standing height), 0 if none
float projectiles_update(float dt, const float player_feet[3]);
// Of the damage projectiles_update returned since last asked: the mages' bolts' and the arrows'
// (shields and perks take their share of each: Deflect Arrows, Elemental Protection)
void projectiles_take_split(float *magic, float *arrows);
// Fire bolts (model -2) as sprites, to draw with the spells'; returns how many
// A spell's bolt from the player (a Fireball scroll's): ELEMENT_* (spellfx.h), its damage, bursting
// over area (m) where it strikes (0: only what it hits)
int projectiles_fire_bolt(const float pos[3], const float dir[3], float speed, int element, float damage, float area);
// ... a mage's at you (its element and damage: its own spell's)
int projectiles_fire_hostile_bolt(const float pos[3], const float dir[3], float speed, int element, float damage, float area);
void projectiles_set_stagger(int shot); // (a dual-cast bolt with Impact: what it strikes staggers)
// A frostbite spider's spit at the player: a glob arcing as it flies; it poisons
// (damage a second, for seconds) where it lands on them
void projectiles_fire_spit(const float pos[3], const float dir[3], float speed, float gravity, float dps, float seconds);
int projectiles_sprites(ObjectsSprite *out, int max);
// Before loot_draw: the arrows' models
void projectiles_draw(void);
void projectiles_clear(void); // (changing place)
// An arrow stuck in the world under the crosshair within reach (-1: none): its distance, prompt
int projectiles_aim(const float eye[3], const float dir[3], float *along, const char **label);
void projectiles_take(int arrow); // (into the inventory)
// An arrow on the string this frame: its nock at the drawing hand, pointing past the bow hand
void projectiles_draw_nocked(int model, const float nock[3], const float grip[3]);

// A shot just fired (projectiles_fire's) carrying a poison: its harm where it strikes
void projectiles_set_poison(int shot, int poison); // (its item, -1 none)
void projectiles_set_bow(int shot, int bow);       // (the player's bow: its enchantment on what it strikes)

#endif
