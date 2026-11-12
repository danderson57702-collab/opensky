#ifndef CREATURES_H
#define CREATURES_H

#include <stdint.h>

// The region's creatures and hostile people (pack/creatures, see converter/creatures.py):
// Skyrim's wolves, bears, skeevers, draugr, giants... and bandits, at their spawn points
// outdoors and in the interiors, there while you're near. Predators, the undead and bandits
// come for you when they see you (sneaking: from nearer), along the navmesh, spreading round
// you; giants and mammoths only once you've hurt them; prey runs. Archers keep their
// distance and shoot, mages throw fire. They strike when they reach you, flinch when hit and
// die (their clip, or falling over), and their bodies can be searched.

void creatures_init(const char *pack_dir);
// Each frame: the player's feet, whether they're sneaking, where they are (interior.h's
// interior_current: -1 Tamriel, an interior's index)
void creatures_update(float dt, const float player[3], int sneaking, int place);
// How plainly they see the player (1; Fortify Sneak less, Invisibility 0: only heard)
void creatures_set_player_visibility(float m);
// How many creatures can be about at once (the performance setting; the rest wait at their spawn
// points till there's room)
void creatures_set_max_awake(int n);
// No spawn points wake (and no random dragon) while on: Skyrim's opening in Helgen
void creatures_hold_spawns(int on);
// Skyrim's opening in Helgen Keep: whose side you're on (1 Ralof's: its Imperials come for you; 2
// Hadvar's: its Stormcloaks), 0 none - the spawn points marked for a side (or for after it)
void creatures_set_opening_side(int side);
int creatures_opening_bodies(int place);
// Conjuration's allies. A summons: a creature of this kind (creatures.txt K; a wolf if the pack
// hasn't it) at a spot by you, yours for these seconds - it goes for whoever's hostile near
// you, keeps by you otherwise, comes with you through doors; one at a time (the last goes).
int creatures_summon_spirit(const char *kind_name, const float at[3], float seconds); // (as an ally, a spirit: the familiar)
int creatures_summon_ally(const char *kind, const float at[3], float seconds);
void creatures_set_summon_limit(int n); // (Twin Souls: two at once; else one)
void creatures_strengthen_summons(float mult, float health); // (the last one called: Elemental Potency, Dark Souls)
// The dead raised: the body along the ray (within reach) up again as yours for these seconds,
// then down for good; 0 if there's none
int creatures_reanimate(const float eye[3], const float dir[3], float reach, float seconds);
int creatures_rally_allies(float seconds);  // (Courage, Rally, Call to Arms: they strike harder; how many)
int creatures_heal_allies(float health);    // (Heal Other, Healing Hands: how many were healed)
// Detect Life / Dead: where the living (or with undead: the undead) within range are; how many
int creatures_positions(const float from[3], float range, int undead, float out[][2], int max);
int creatures_take_trapped(void);           // (Soul Trap: souls taken since last asked) // (the placed dead there - Gunjar's body in Helgen Keep: how many)
// The player's swing landing now: from eye along dir within reach, over this cone (cosine of
// its half angle); damage to the first creature in it. Returns 1 if it hit one
int creatures_hit(const float eye[3], const float dir[3], float reach, float cone_cos, float damage);
// A spell along a ray (Flames, Sparks: damage this frame) from eye along dir (unit) within
// reach: the first creature it passes through (within radius of its body) takes the damage.
// Returns how far along the ray it was struck, or -1 if none was
float creatures_ray(const float eye[3], const float dir[3], float reach, float radius, float damage);
// Damage to every creature within radius of pos (a rune's burst, an explosion)
int creatures_area(const float pos[3], float radius, float damage);
// A storm's hold on those within radius of pos (Skyrim's hazard - Blizzard's - touching them):
// damage a second for seconds, slowed for slow_seconds; how many
int creatures_area_dot(const float pos[3], float radius, float dps, float seconds, float slow_seconds);
// Whether a live creature stands within radius of pos (setting off runes and traps)
int creatures_near(const float pos[3], float radius);
// Aim assist: the creature nearest the look within a cone (cosine of its half angle) and range,
// in sight; 1 and the middle of its body
int creatures_aim_assist(const float eye[3], const float dir[3], float cone_cos, float range, float speed, float out[3]);
// An arrow (item) lodged in the creature creatures_ray last struck
void creatures_take_arrow(int item);
// A spawn point's key (its kind, place and spot - steady as the map grows), and back: saves keep these
uint32_t creatures_spawn_key(int spawn);
int creatures_spawn_by_key(uint32_t key); // (-1: none here)
// Damage struck into the player since last asked (block taken off: blocked x share)
float creatures_take_damage(int blocking, float block_share);
// The creature you're fighting (the last hit or attacking you): its name and health left
// (0..1); 0 if none
int creatures_target(const char **name, float *health);
// A body under the crosshair within reach (-1: none): its distance and prompt
int creatures_aim(const float eye[3], const float dir[3], float *along, const char **label);
// Searching it: its container's handle (items.h) and name, -1 if none
int creatures_search(int body, const char **name);
void creatures_strip(int body); // (a person's body, its gear taken: bare)
int creatures_cut_down(int i);
void creatures_release_near(const float pos[3], float radius); // (a web struck down: whoever it held, down too)
int creatures_find_titled(const char *title, float out[3]); // (testing: one awake by its name - its feet)
int creatures_guard_talk(int i); // (creatures_aim's "Talk to": a guard's greeting; 0 if not one at peace)
void creatures_set_player_identity(int female, const char *race_eid); // (the guards' greetings: "...Argonian") // (creatures_aim's "Cut Down": Arvel out of the web; 0 if not one held)
// Whether anything hostile is after you within radius (no sleeping, no waiting)
int creatures_hostile_near(const float pos[3], float radius);
int creatures_spawn_where(int s, float out[3]); // (a placed foe by its spawn, up: its feet; 0 if down or gone)
int creatures_spawn_body(int s, float out[3]);  // (... alive or dead: where it is, or its body lies; 0 if gone)
int creatures_nearest_foe(const float from[3], float range, float out[3]); // (the nearest one against you still up: its feet)
void creatures_clear(void); // (a game loaded)
// How loud the player is now (0 silent .. 1 walking .. 2 running, fighting): how far off
// they're heard
void creatures_set_noise(float noise);
// Saving: which spawn points' creatures were killed
int creatures_save(uint32_t *dead, float *dead_at, int max); // (and when: game hours)
void creatures_load(const uint32_t *dead, const float *dead_at, int n); // (dead_at NULL: just now)
// Skyrim's cell reset: those dead 10 game days (a cleared place's: 30) come back, their bodies'
// things with them - outdoors, only out of sight of the player (place: where they are now);
// how many
int creatures_respawn(int place, const float player[3]);

// Poison on the player (a spider's bite or spit): damage a second, for seconds
void creatures_poison_player(float dps, float seconds);
void creatures_cure_player_poison(void); // (Cure Poison)
// The poison's damage this frame (armour doesn't stop it); whether the player is poisoned
float creatures_take_poison(float dt);
int creatures_poisoned(void);

// Shouts: in the cone before `from` (dir: unit; cone_cos: its half-angle's cosine) within range
// and in sight - Unrelenting Force's push (Skyrim's stagger magnitude: flung, knocked down
// when knock_down) and damage; a breath's damage (seconds > 1: over that long, with a slow
// for slow_seconds; stagger: Skyrim's). Each returns how many it reached
// Shouts' other effects on those in the cone before you (CSHOUT_*): disarmed, frozen (its
// magnitude the cold's harm), marked for death or drained (magnitude: harm a second), animals
// calmed or on your side - for seconds; how many it took
enum { CSHOUT_DISARM, CSHOUT_ICE_FORM, CSHOUT_MARK, CSHOUT_DRAIN, CSHOUT_CALM_ANIMALS, CSHOUT_TAME_ANIMALS, CSHOUT_FEAR };
// Dragonrend: the dragons before you brought out of the sky, held to the ground for seconds; how many
int creatures_dragonrend(const float from[3], const float dir[3], float range, float seconds);
// Storm Call: lightning on one of your foes within radius of pos - its harm, where (at); 0 none
int creatures_storm_strike(const float pos[3], float radius, float damage, float at[3]);
int creatures_shout_effect(const float from[3], const float dir[3], float range, float cone_cos, int what, float magnitude,
                           float seconds);
int creatures_shout_push(const float from[3], const float dir[3], float range, float cone_cos, float push, float damage,
                         int knock_down);
int creatures_shout_breath(const float from[3], const float dir[3], float range, float cone_cos, float damage, float seconds,
                           float slow_seconds, float stagger);
// Battle Cry: those within radius flee for seconds
int creatures_frighten(const float pos[3], float radius, float seconds);

// The last blow that landed on the player since asked: its weapon kind (0 fists, -1 claws or
// teeth), -2 none (main: the sound of it, or of the block)
int creatures_take_struck(void);

// Hurts (no stagger: burning, not a blow) each creature whose feet inside() says are in it
int creatures_hurt_where(int (*inside)(const float pos[3], void *ctx), void *ctx, float damage);
// A spell along a ray (from the eye): the first creature in the way, and with area those
// about it too - each no higher than max_level (0: any) - feared (fleeing), calmed (not
// fighting), paralysed (down, stiff) or, undead, turned (fleeing) for seconds; how many it
// took, where it struck
enum { CSPELL_FEAR, CSPELL_CALM, CSPELL_PARALYZE, CSPELL_TURN, CSPELL_FRENZY, CSPELL_SOULTRAP };
int creatures_spell_ray(const float eye[3], const float dir[3], float reach, int effect, float max_level, float seconds,
                        float area, float hit[3]);
// The same (CSPELL_*) on all within radius of pos: a spell all about you (Harmony, Mass Paralysis)
int creatures_spell_about(const float pos[3], float radius, int effect, float max_level, float seconds);

// More harm to the creature last struck (a blow's, an arrow's): a poison on the weapon
int creatures_hurt_struck(float damage);
// A poison (its item) on the creature last struck: its damage, Paralysis (down for its
// seconds), Slow
int creatures_poison_struck(int poison);

// A noise at pos (an arrow striking near them, Skyrim's): the hostile within radius that
// haven't found you come looking, toward the source
int creatures_alert_at(const float pos[3], float radius, const float source[3]);

// The player's perks: the share of armour their blows ignore (Bone Breaker, Skullcrusher), the
// chance of an arrow in a body coming back (Hunter's Discipline); on the creature last struck,
// paralysis (Bullseye, Paralyzing Strike), bleeding (Hack and Slash), a stagger (Power Shot)
void creatures_set_armor_pierce(float share);
// Sneak attacks: a blow's and an arrow's multiple on one that hasn't found you (Skyrim's x3, x2 and the perks')
// How far the player is put back out of those it walked into this frame (x, z)
void creatures_take_block(float out[2]);
void creatures_set_sneak_attack(float melee, float ranged);
void creatures_set_ray_sneaks(int arrow); // (the next rays' damage is an arrow's: it may sneak attack)
void creatures_set_arrow_recovery(float chance);
int creatures_paralyze_struck(float seconds);
int creatures_bleed_struck(float dps, float seconds);
int creatures_stagger_struck(void);
// People on your side (a follower, a quest's allies; slot 0..7): enemies go for them as for you;
// the harm done them since asked (their health is the caller's)
void creatures_set_friend(int slot, const float pos[3], int on);
float creatures_take_friend_damage(int slot);
void creatures_set_blow_power(int power);
// A person your last blow killed (its live index; -1 none) - unseen: struck as a sneak attack - and
// its body put through a killmove: at pos, facing heading, Skyrim's victim clip (1 if it has it)
int creatures_take_blow_kill(int *unseen);
float creatures_take_blow_soul(void); // (what your last blow killed: its soul's charge, 0 none)
int creatures_killmove(int i, const char *clip, const float pos[3], float heading); // (the next creatures_hit is a power attack: no guard holds it)
int creatures_killmove_pick(const float eye[3], const float dir[3], float reach, float cone_cos, float damage,
                            const float feet[3], float last_m, int *unseen); // (the foe a lethal blow would bring down, the last: its index)
int creatures_killmove_clip_ok(int i, const char *clip);
int creatures_killmove_strike(int i); // (its blow: it falls where it stands)
int creatures_killmove_done(int i); // 1 if victim died, transitioned to ragdoll, or killmove finished
int creatures_take_power_struck(void);   // (the last blow on you was a power attack: it staggers you)
void creatures_recoil_striker(void);
int creatures_reflect_striker(float damage);
void creatures_set_spell_element(int element);
int creatures_banish_struck(void); // (Oblivion Binding: the conjured one struck is sent back) // (a Destruction spell striking now: its element; -1 none)
// Shadow Warrior: those fighting you further off than within lose you for a moment and search; how many
int creatures_lose_track(const float player[3], float within); // (Reflect Blows: its blow back on the one who struck you)
// Sweep: everything before you in the cone hurt (a two-handed power attack sideways), how many
int creatures_hit_all(const float eye[3], const float dir[3], float reach, float cone_cos, float damage);     // (your guard took its blow: the one who struck recoils)
int creatures_bash_struck(int disarm); // (a bash: staggered even mid-blow; disarm: its weapon out of its hand - 2 if it was)
int creatures_power_attack_on_you(const float player[3]); // (one winding up a power attack at you, in reach)
// An enchantment on the creature last struck: Fear, Calm, Turn Undead (CSPELL_*, up to a level);
// magic damage (Fire, Frost, Shock: its health), and where on it (for the flash); 0 if none
int creatures_bewitch_struck(int effect, float max_level, float seconds);
int creatures_magic_struck(float damage, float pos[3]);

// The hostile ones placed in an interior (its index), and how many of them are alive
int creatures_place_hostiles(int place, int *alive);

// The difficulty's multiplier on the player's damage (blows, arrows, spells, shouts)
void creatures_set_player_damage_mult(float m);

// Dragons (Skyrim's: creatures with K flags 64): one flying in now (the debug controls; they
// also come at random outdoors once you're a few levels along), 1 if one came
int creatures_take_dragon_fell(void); // (a dragon killed since last asked: its last words)
int creatures_summon_dragon(const float player[3]);
// A quest's dragon at a place (a kind by name - "alduin" -; NULL: one for your level), perched -
// calm: looking on till creatures_dragon_wake; gone again (flown off) with creatures_dragon_away.
// Its index (creatures' live slot), -1
int creatures_place_dragon(const char *kind, const float pos[3], float heading, int calm);
void creatures_dragon_wake(int i);
int creatures_place_foe(const char *kind_name, const float pos[3], float heading, int ghost, int place); // (a quest's: the Silver Hand's ambush; place interior.h's)
int creatures_kind_title_live(int i, const char *title);
int creatures_live_look_as(int i, const char *model, int female); // (a quest's foe drawn as that actor: pack actors/<model>.sknpc)
int creatures_live_spawn(int i);      // (its spawn point)
int creatures_title_killed(const char *title); // (a placed one of that name killed)
int creatures_spawn_of_ref(uint32_t ref);
int creatures_spawn_alive(int spawn); // (a quest's foe: 1 alive, 0 killed, -1 gone)
void creatures_forget_spawn(int spawn);
void creatures_set_name(int i, const char *name); // (a quest's own name for one: "Sahloknir"; a literal)
void creatures_dragon_away(int i);
// A quest's hold on one: never brought below share of its health (0: none) - Alduin flees, Odahviing
// is trapped - and how much of its health it has (0 dead, -1 not about)
void creatures_set_floor(int i, float share);
float creatures_health_share(int i);
// Their breath on the player since asked (damage; element: spellfx.h ELEMENT_FIRE / FROST)
float creatures_take_breath(int *element);
// A draugr's shout since asked: its damage (and element: ELEMENT_FORCE a push's), the shove (m/s,
// world x z), whether it disarmed you
float creatures_take_shout(float push[2], int *element, int *disarm);
// Dragon souls absorbed since asked (each unlocks a word of a shout)
int creatures_take_dragon_souls(void);

// Guards (crime.c's): each hold's mood toward you (0 at peace, 1 to arrest you, 2 to attack); the
// guard who has come up to stop you this frame (its live index, and hold; -1 none) and done with
// (resisted: it fights); the hold of a guard who can see a place (-1 none); a guard struck at
// peace, or killed, since last asked (its hold, -1); a live one's name and voice type
void creatures_set_hold_mood(int hold, int mood);
int creatures_take_arrest(int *hold);
void creatures_arrest_done(int guard, int resisted);
int creatures_guard_sees(const float at[3], float range);
int creatures_take_guard_struck(void);
int creatures_take_guard_killed(float at[3]);
const char *creatures_title(int i);
const char *creatures_voice(int i);
int creatures_guard_count(void); // (awake: a test's)
int creatures_position(int i, float pos[3]); // (a live one's feet; 0 if it's gone)
// Allies who come with you for a while, several at once (Irileth's men at the Western
// Watchtower); a hold's guards' kind (its name, NULL)
int creatures_summon_escort(const char *kind, const float at[3], float seconds);
int creatures_dismiss_allies(const char *kind);
void creatures_test_health(const float player[3], float radius); // (testing: the health of those about you, to the log)
int creatures_test_dragon_dies(const float player[3]); // (testing: a dragon summoned and killed beside you) // (an escort's job done: those of that kind on your side, gone)
const char *creatures_guard_kind(int hold);
const char *creatures_guard_archer_kind(int hold); // (one with a bow, if the hold has one)
void creatures_blame_player(int on); // (0 around blows that aren't yours - a follower's, Irileth's: no crime of yours)

// Diplomatic Immunity's Thalmor (spawn sides 4-6): the one placed as Skyrim's reference (the
// Solar's guard), awake (live index) or -1; held at peace talking to you (on) or let be; fooled,
// walking off toward to (x, z) and gone after seconds; turned on you; whether it's after you; a
// body's (its container's reference) spawn side, -1 not a body
int creatures_by_ref(uint32_t ref);
void creatures_parley(int i, int on);
void creatures_parley_go(int i, const float to[2], float seconds);
void creatures_turn_on(int i);
int creatures_hostile(int i);
int creatures_body_side(uint32_t container_ref);
// (a quest's Thalmor who challenges you rather than joining the alarm: the Solar's guard)
void creatures_set_challenger(uint32_t ref);

#endif
