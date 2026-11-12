#ifndef SFX_H
#define SFX_H

// Skyrim's sound effects (pack/sound/sfx.bank, see converter/extract_sounds.py): footsteps,
// swimming, weapons, spells, lockpicking, items, doors, traps, the menus. Up to SFX_VOICES at
// once, mixed on their own audio port; each play picks one of the event's variants at
// random. Volume 0..1; pan -1 (left) .. 1 (right).

#define SFX_VOICES 8

void sfx_init(const char *pack_dir);
void sfx_set_volume(float volume); // (the options' effects volume)
// Plays an event once; returns a voice handle (-1: unknown event, or none free)
int sfx_play(const char *event, float volume, float pan);
// Plays an event over and over (a spell's loop) until sfx_stop
int sfx_loop(const char *event, float volume);
void sfx_stop(int voice);
// Plays an event heard from a place: quieter with distance (gone beyond max_m), panned by
// where it is relative to the listener (x, z, facing yaw degrees)
int sfx_play_at(const char *event, const float pos[3], float max_m);
void sfx_listener(float x, float z, float yaw_deg);

// Spoken lines' stereo frames (at the sound bank's rate, 22050 Hz) mixed in with the effects:
// written as they play (waits while the buffer's full); drained (waits till played); cleared
#include <stdint.h>
int sfx_stream_write(const int16_t *stereo, int frames);
void sfx_stream_drain(void);
void sfx_stream_clear(void);

#endif
