#ifndef VOICE_H
#define VOICE_H

// NPC speech: one voice line at a time (pack/voice/*.skvoc, mono IMA ADPCM), streamed
// on its own thread through the Vita's voice audio port, mixed over the music.

void voice_init(const char *pack_dir);
// Starts a line (path under voice/), replacing the current one. Returns 0 if it can't.
int voice_play(const char *path);
void voice_stop(void);
// Whether a line is still playing (or about to start).
int voice_playing(void);
// How loud the line is right now (0..1, before distance), for the speaker's jaw
float voice_level(void);
// Loudness (0..1) and position (-1 left .. 1 right), applied smoothly while playing.
void voice_set_mix(float gain, float pan);
// The player's voice volume (0..1)
void voice_set_volume(float volume);

#endif
