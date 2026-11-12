#ifndef GAMELOG_H
#define GAMELOG_H

// The log on the card (ux0:data/opensky_log.txt): start-up's steps, the frame's numbers every few
// seconds while playing, and what goes wrong (a voice line that won't play...). Each line is
// written at once (opened, appended, closed: a crash leaves the line before it), with the
// time since the game started. Also printed to the debug console.
void game_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// From now on queued and written by a thread of its own, twice a second (once it's playing: no
// stall in a frame for the card)
void game_log_background(void);

#endif
