#ifndef MUSIC_H
#define MUSIC_H

// Background music: Skyrim's exploration tracks (pack/music/*.skmus, IMA ADPCM) streamed
// from disk on their own thread. Like Skyrim, tracks play in random order with quiet
// gaps between them.

// Starts the music thread; does nothing if the pack has no music.
void music_init(const char *pack_dir);
// Fades out and stops the thread.
void music_shutdown(void);
// Time of day (0..24): picks from the morning / day / dusk / night tracks.
void music_set_hours(float hours);
// Toggles music on/off (fades).
void music_toggle(void);
// Volume relative to normal (0..1), eased: the music dips under NPC speech.
void music_duck(float level);
// The player's music volume (0..1)
void music_set_volume(float volume);
// One of Skyrim's set pieces (pack/music/<name>.skmus, not in the playlist: the Word of Power's
// at a word wall): what's playing fades, it plays, then the exploration tracks go on
void music_special(const char *name);

#endif
