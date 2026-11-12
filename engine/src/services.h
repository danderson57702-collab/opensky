#ifndef SERVICES_H
#define SERVICES_H

#include <stdint.h>

// Skyrim's trainers and speech checks.
//
// Training: a trainer teaches their skill up to their tier's most (journeyman 50, expert 75,
// master 90), a level at a time for gold, two hours each, five times a character level
// (iTrainingNumAllowedPerLevel).
//
// Speech checks: Persuade against Skyrim's difficulties (SpeechVeryEasy 10 .. SpeechVeryHard
// 100, a third easier with the Persuasion perk); Intimidate (your Speech, or your level over
// theirs); Bribe (gold scaled by their level, less with Speech: fBribeScale, fBribeCostCurve,
// fBribeSpeechcraftMult).
//
// The Whiterun gate: closed INFO 000D197C until you've told the guard why you've
// come - Riverwood's call for aid, news of Helgen (Persuade), a threat (Intimidate), a bribe.

enum { SPEECH_VERY_EASY = 10, SPEECH_EASY = 25, SPEECH_AVERAGE = 50, SPEECH_HARD = 75, SPEECH_VERY_HARD = 100 };
int speech_persuade(int difficulty);           // 1: persuaded (Speech trains either way)
int speech_intimidate(int difficulty, int their_level);
int speech_bribe_cost(int their_level);

// Training: what to say to them (appended to opts; how many); one of those said - the
// training menu opens; its choices (menu.h's dialogue) come back through services_choice
int services_topics(int actor, const char **opts, int max);
void services_say(int actor, int k);
// The Whiterun gate guard's stop: 1 if the door (doors.h / interior.h index) is held shut - the
// guard's line and choices are up; then services_choice. Its door to go through once let in
int services_gate_stop(int door, const char *door_label, int place);
int services_gate_open(const char *door_label, int place); // (let in once, or past the Jarl: the gate's open to you)
// A choice in a menu of ours (training, the gate): 1 if it was ours. *go_door: a door to go
// through now (-1 none)
int services_choice(int choice, int *go_door);
int services_open(void); // (a menu of ours is up, or about to be)
void services_update(void); // each frame: our menus, once the speaker's done

// Saving: trainings this level, the gate (services_save fills one int)
int32_t services_save(void);
void services_load(int32_t v);

#endif
