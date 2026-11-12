#ifndef ACTORS_H
#define ACTORS_H

#include <stdint.h>

// Skyrim's unique NPCs (pack/actors/*.sknpc, see converter/npcs.py), living by their
// schedules: outdoors at the spots their packages name (wandering around them), or
// inside a building (they walk to its door and disappear, and come out of it again).
// Near the player they walk Skyrim's navmeshes with a procedural walk cycle; elsewhere
// they are simply where their schedule says. Skinned on the CPU, drawn like objects.

void actors_init(const char *pack_dir);
// The player's character (actors/player/<stem>.sknpc and its first-person arms; races.h),
// replacing the one before (gear comes off: put it back on after); tone: the skin tone of
// the bare skin in gear. 0 if it can't be loaded
int actors_player_load(const char *stem, const unsigned char tone[3]);
// Which way the player's body faces (degrees, as the view's yaw)
float actors_player_heading_deg(void);
void actors_player_face_view(int on); // (a scene turning your view to what's happening: the body turns to it too)
void actors_player_face_camera(int on, float turn_deg); // (the race menu: the body turned to the camera, then turned by turn_deg)
// Sitting down (Skyrim's clips by name: getting on from beside the seat, sitting, getting up)
// on a seat at pos facing heading (radians, as engine headings); 0 if it can't
int actors_player_seat(const float pos[3], float heading_rad, const char *in, const char *idle, const char *out);
void actors_player_stand(void); // (getting up)
void actors_player_mount(const char *clip, float t); // (in the saddle: a rider clip at t into it; NULL: down)
void actors_script_corpse(int i, int on); // (a scene's dead: searched, not talked to)
int actors_corpse(int i);
void actors_script_strip(int i);  // (their gear taken: bare under it)
void actors_beast_strip(int i);
void actors_beast_headonly(int i, int on); // (a head alone: the rest drawn into the neck)
void actors_player_scene(const char *clip, float t); // (a killmove's: its whole-body clip at t; NULL: done)
// Riding: sat at pos (moved each frame by a script - the opening's cart) in a clip, facing
// heading; NULL: off it
void actors_player_ride(const float pos[3], float heading_rad, const char *idle);
// Where the body's eyes are (world; as last posed), 0 if it hasn't been posed lately
int actors_player_eye(float out[3]);
// While sitting (getting on, on it, getting up): where the body is and faces (degrees, as the
// view's yaw); 0: not sitting
int actors_player_seated(float pos[3], float *heading_deg);
// dt_s: real seconds since the last frame; hours: game clock (0..24)
void actors_update(float dt_s, float hours, float player_x, float player_z);
// Call after objects_draw.
// Hands this frame's posing to a second core (call before drawing the world; actors_draw waits for it)
void actors_prepare(float cam_x, float cam_z);
void actors_draw(float cam_x, float cam_z);

// Loading screens: people's models read faster behind one; those in view still unread; in view
void actors_set_loading(int on);
int actors_meshes_waiting(void);
void actors_script_far(int i, int on); // (a scene's actor seen from afar: drawn past the setting's reach for people)
int actors_preload(float x, float z, float radius, int max); // (models read ahead behind black: how many)
int actors_in_view(void);
int actors_count(void);
void actors_watch(int i); // (testing: logged every 2 s)
// Editor id of NPC i (lowercase: the name of its pack files)
const char *actors_id(int i);
// Where NPC i is; returns 0 if it's out of sight (inside a building, or far away and not
// simulated)
int actors_where(int i, float pos[3]);
int actors_door(int i, float pos[3]);
// You among people (your feet, your radius): pushed out of anyone you'd walk into - they're
// shouldered aside too, unless they hold a scene's mark. 1 if anyone was in the way
int actors_block_player(float *px, float *pz, float py, float radius); // (indoors, out of sight: the door outside they went in by; 0 if not)
// Where someone is and whether they're in the world to be seen (talking: those standing at their
// schedule's spot, not walked this frame, count too)
int actors_shown(int i, float pos[3]);
// The player went inside building `interior` (index in interiors.txt; its door to
// Tamriel is at `exit`, inside), or back out (-1, exit NULL). Everyone is placed for the
// new world: inside, only the NPCs whose schedule has them in there are present.
void actors_set_interior(int interior, const float exit[3]);
// NPC i is asleep (in bed: no greetings unless woken)
int actors_asleep(int i);
// Wakes NPC i (talked to by someone at x, z): up beside the bed, back to sleep later
void actors_wake(int i, float x, float z);
// How open NPC i's mouth is this frame (0..1; speech loudness); it closes by itself
void actors_set_mouth(int i, float open);
// NPC i stops what it's doing and turns to (x, z) for `seconds` (talking to the player)
void actors_attend(int i, float x, float z, float seconds);

// The player's character: position (feet), camera yaw, velocity (m/s, world; vy up), on
// the ground or not, swimming, sneaking (crouched); shown: drawn this frame (third person).
void actors_player_update(float x, float y, float z, float yaw_deg, float vx, float vz, float vy, int grounded,
                          int swimming, int sneaking, float dt, int shown);
// The player's readied spells, per arm (left, right): -1 none, 0 held ready, 1 aimed (a
// stream), 2 on yourself. The arms play Skyrim's casting clips.
// Drawing a bow: how long it's been drawn (Skyrim's draw clip, held at full draw), < 0 not
void actors_player_bow(float draw_s);
float actors_player_bow_draw_length(void); // the draw clip's length (s)
void actors_player_magic(int left_pose, int right_pose);
// Where spells leave the player's left and right hands (world) as last drawn: 1, or 0 if
// the hands weren't drawn last frame
int actors_player_palms(float out[2][3]);
// The first-person arms (shown in first person while holding the torch or a spell): placed at the
// eye, turned and tilted with the view; actors_draw_first draws them (on top: clear depth first).
void actors_first_update(const float eye[3], float yaw_deg, float pitch_deg, float speed, float dt, int shown);
void actors_draw_first(void);
// The player's torch: 0 not held; 1 held but the body isn't drawn (first person); 2 held,
// out = its flame (world)
int actors_player_torch(float out[3]);
// The player's wardrobe (pause menu): count, name and whether it's worn, put on / take off
int actors_player_items(void);
int actors_player_female(void);       // the player's character is a woman
int actors_player_base_items(void); // (the player's own clothes: the first ones)
// Puts item i on or takes it off (loading a game)
void actors_player_set_worn(int i, int on);
const char *actors_player_item(int i, int *equipped);
void actors_player_toggle(int i);
// Puts a gear file (converter/gear.py: armour, a weapon) on the player as a new wardrobe item
// (not worn yet); its index, or -1
int actors_player_add_gear(const char *path, const char *name, uint32_t slots, int weapon);
// Takes gear (an index from actors_player_add_gear, not worn) off the player for good: its
// slot is reused (the player holds MAX_ITEMS at once)
void actors_player_drop_gear(int item);
// Gear made one of the character's own parts (chargen.c: hair, eyes, a beard): worn for good,
// hidden while something worn covers any of hidden_by (a helmet over the hair), not in the wardrobe
void actors_player_set_part(int item, uint32_t hidden_by);
// Gear (its index) tinted green: a poisoned weapon
void actors_player_tint(int item, int on);
// The character creator's look (chargen.c) on the player's batches the converter marked
// (.skmorph beside the player's models and gear): morph weights by name (the face's sliders,
// Skyrim's NoseType<n>..., "weight" - the thin build's share), applied over the vertices as loaded
void actors_player_look_morph(const char *name, float w);
void actors_player_look_morphs_clear(void);
void actors_player_look_apply(void);
// The bare skin's and hair's colour multipliers (Skyrim's shaders: x2 on white); NULL skin: none (a
// ready face, an older save's)
void actors_player_look_colors(const float skin[3], const float hair[3]);
int actors_player_look_face(unsigned tex); // (the face's texture: the batches given it)
int actors_player_look_roles(void);        // (its batches with a part in the look: 0 - none, an old pack)
// Gear (its index) glowing in this colour (0xRRGGBB; 0 none): an enchanted weapon, drawn
void actors_player_glow(int item, unsigned rgb);
// Weapons drawn (in hand) or sheathed, and the right hand's weapon kind (Skyrim's animation
// type, -1: none): the arms take Skyrim's ready pose for it
void actors_player_weapon_drawn(int drawn);
// The weapon in the left hand (dual wielding; Skyrim's animation type, -1 none)
void actors_player_left_weapon(int kind);
void actors_player_weapon(int kind);

// Fighting: a swing (one of Skyrim's attack clips; both views) - its length, 0 if one is
// already going; how far into it (s, -1: none); blocking with a clip (NULL: not)
float actors_player_attack(const char *clip);
void actors_player_attack_speed(float mult); // (Elemental Fury: 1 as Skyrim's clips)
void actors_player_flesh(int kind, float left); // (Oakflesh .. Ebonyflesh: its shell drawn over you)
void actors_player_ghost(int on);           // (Become Ethereal: drawn pale)
float actors_player_attack_time(void);
void actors_player_block(const char *clip);
// Creatures (creatures.c runs them): a kind's clips and body (creatures/<name>), -1 if it isn't
// in the pack; one of it in the world (-1: no room); gone again
// A kind (creatures/<name>.sknpc) on a clip set (creatures/<set>.skanim, or @male / @female:
// the characters'); -1 if it can't be loaded
int actors_beast_kind(const char *name, const char *set);
// A creature falling dead (no clip of its own): tipped over backwards / onto its side (radians)
// Draws creature i this much bigger than its skeleton (its race's height)
void actors_beast_scale(int i, float scale);
void actors_beast_fall(int beast, float pitch, float roll); // (a flyer: banking, diving)
// A dragon: seen from far off without the distance dissolve; fade: that much of it gone (burning)
void actors_beast_look(int beast, int far, float fade);
int actors_beast_spawn(int kind);
void actors_beast_ghost(int i, int on); // (a spirit: half see-through, blue-white - a conjured familiar)
void actors_beast_remove(int beast);
// A kind's clip: its length (s) and travel speed (m/s; 0: none)
float actors_beast_clip(int kind, const char *clip, float *speed);
// Where a creature is and how it stands: clip at t, blended toward another by w (0..1)
void actors_beast_pose(int beast, float x, float y, float z, float heading, const char *clip, float t,
                       const char *blend, float blend_t, float w, float dt);

// Directed by a script (the opening, intro.c): someone's schedule set aside (on), back to it (off).
// Placed (feet, heading as actors' radians); standing in a clip of the characters' ("": idle;
// once: held at its end); walking (running) to a spot over the navmesh; following the player.
void actors_script(int i, int on);
void actors_script_place(int i, const float pos[3], float heading);
void actors_script_clip(int i, const char *clip, int once);
void actors_script_clip_from(int i, const char *clip, int once, float t0); // (a clip begun t0 s in)
void actors_script_clip_next(int i, const char *clip); // (after the once clip: this, looping)
void actors_script_walk(int i, const float to[3], int run);
void actors_script_headless(int i, int on);
void actors_script_prop(int i, int on); // (a scene's thing in their hands shown: Hadvar's list)
void actors_player_bound(int on); // (the arms bound before you: Skyrim's opening) // (beheaded at the block)
int actors_script_walking(int i);
int actors_scripted(int i);
int actors_script_has_clip(int i); // (a script has them in a clip of their own: sitting, kneeling...) // (directed by a script now: the opening, a quest's scene)
void actors_script_follow(int i, int on);
void actors_script_release(int i); // (out of a script, out of sight: back where their day has them)
void actors_script_face(int i, float heading);
void actors_script_look_at(int i, const float *at); // (the head turned toward a point; NULL: ahead)

// The performance setting: how far off people walk their schedules (30..110 m; beyond, they're
// placed where their schedule has them), how often those beyond 30 m think (every Nth frame,
// taking turns), how many are posed a frame at most beyond the nearest few
// In a walled city: only the nearest two posed every frame, those within 12 m every other,
// the rest every fourth (a crowd's posing was a third of Whiterun's frame)
void actors_set_city(int city);
void actors_set_detail(float sim_m, int far_think_every, int poses_per_frame);
// How far off people (and creatures) are drawn: the near detail's dissolve end + a little
void actors_set_draw_radius(float m);

#endif
