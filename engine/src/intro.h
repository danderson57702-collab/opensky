#ifndef INTRO_H
#define INTRO_H

// Skyrim's opening, Unbound (Papyrus scripts in Skyrim; played here from its own scenes - their
// voiced lines, who goes where in each phase, Alduin's perches and strafing runs, and the
// markers they name - and the cart's road, from pack/quests.txt: converter/quests.py): the
// cart ride into Helgen, the lineup (the race menu and your name at INFO 0003F88B), Lokir's
// run, the execution, Alduin, Ralof's tower and the leap into the inn, Hadvar through the
// burning town, the choice at the keep, the escape through Helgen Keep, and on to Riverwood
// (Before the Storm).

void intro_init(const char *pack_dir);
// A new game: the cart. 0 if the opening's data isn't in the pack (start as before)
int intro_start(void);
int intro_active(void);
int intro_ready(void); // (the pack has it)
int intro_on_cart(void); // (the opening's ride in, on the cart)
int intro_first_person(void); // (first person only: the cart, the lineup - till INFO 0003F88B)

// What the opening wants of the frame (main.c acts on it)
typedef struct {
	int lock_move;     // the stick and the action buttons do nothing (riding, kneeling, a scene)
	int hold;          // the player is put at pos (riding the cart, kneeling at the block)
	float pos[3];
	int face;          // the view turned toward look (yaw, pitch: degrees) at once
	float yaw_turn;    // the view turned this much more (degrees): riding, as the cart turns under you
	float roll;        // the view rolled (degrees, + clockwise): your head on its side at the block
	float look_yaw, look_pitch;
	int look_on;       // the view turned toward look_at (world) over a moment: what's happening
	float look_at[3];
	float fade;        // black over the screen (0..1)
	float title;       // the opening's card shown this much (0..1: ui_opening_card)
	int card;          // ... which: 1 the studio, 2 "Presents", 3 the line over the game's name, 4 the name
	int race_menu;     // open the race menu now, then your name (intro_race_chosen when done)
	int choice;        // ask who to follow into the keep now (intro_choose with the answer)
	int teleport;      // put the player at pos (loading what's there, behind black)
	int enter;         // go into this interior (index) at pos, facing look_yaw; -1 none
	int eye_set;       // the view from eye (riding, kneeling: where the clip puts the head)
	float eye[3];
	int use_door;      // go through this door of the interior now (the caves' mouth: walked into); -1 none
	int prompt_door;   // the door the prompt's for (the caves' mouth from further off: through it if pressed); -1 none
	int shake;         // the view shaken (Alduin's fire, his shout)
	int bound;         // hands bound: no swinging, no casting (till "let me get those bindings off")
	const char *prompt; // a hint at the bottom (the jump: INFO 000DF7A7)
} IntroFrame;

// Each frame (playing, unpaused): where the player is (feet), the current interior (-1
// outdoors), the buttons pressed this frame (CROSS: the jump)
void intro_update(float dt, const float feet[3], int interior, unsigned pressed, IntroFrame *out);
// While a loading / fade-in transition keeps intro_update from running: the cart's eye, at once
void intro_prime_view(IntroFrame *out);
void intro_race_chosen(void);
void intro_choose(int ralof); // (the keep: 1 Ralof, 0 Hadvar)
// The compass's marks while it's on (the ones to follow; at the keep, both): how many
int intro_markers(float out[][2], int max);
// The stage (saving): 0 not in it / done; and back from a save (anything but done: at the
// keep with the one chosen, or on the way to Riverwood)
int intro_stage(void);
int intro_side(void);
int intro_face_point(float out[3]); // (the race menu in the opening: where Hadvar stands - you face him) // (in the keep: 1 with Ralof, 2 with Hadvar; else 0 - whose enemies come)
void intro_load(int stage);
void intro_jump_to_escape(void);
void intro_jump_to_climb(void); // (testing: the tower's stairs)
void intro_jump_to_town(void);
void intro_jump_to_wall(void); // (testing: the town, put beside Hadvar as the wall scene begins) // (testing: Hadvar's way through the burning town, from the inn)
void intro_jump_to_riverwood(int ralof); // (... to the road to Riverwood with them)
int intro_died(void); // (dead in the keep, where there's no saving: 1 and back to its doors with the one you chose) // (testing: straight to Alduin's attack, after intro_start)
void intro_jump_to_lineup(void);
void intro_jump_to_keep(int ralof); // (... to the keep with them) // (... to the cart's arrival)

#endif
