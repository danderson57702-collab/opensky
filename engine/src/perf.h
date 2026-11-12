#ifndef PERF_H
#define PERF_H

// Where each frame's time goes (for tuning on the Vita): the frame's systems timed with
// perf_begin / perf_end, averaged over half a second, with the worst frame of the last
// second and the memory in use (the heap; vitaGL's video, main and physically contiguous
// memory). Shown over the game when the Performance Readout option is on.

enum {
	PERF_FRAME,     // the whole frame (begin to begin)
	PERF_GAME,      // the player, vitals, magic, combat (main's update)
	PERF_AI,        // NPCs and creatures thinking and walking
	PERF_STREAM,    // what to load (terrain, objects, navmesh updates)
	PERF_UPLOAD,    // finishing loads (GPU uploads: the stream pump)
	PERF_WORLD,     // drawing terrain, objects, water
	PERF_ACTORS,    // posing and drawing characters and creatures
	PERF_FX,        // particles, sprites, weather, the arms
	PERF_POST,      // post-processing and the UI
	PERF_SWAP,      // waiting for the GPU (the buffer swap)
	// (within "game")
	PERF_INPUT,     // the pad, the menus, the readout
	PERF_MOVE,      // your movement against the collision
	PERF_LOOK,      // what's under the crosshair (people, doors, loot...)
	PERF_POSE,      // your character's pose (and the first-person arms)
	PERF_CAMERA,    // the third-person camera kept out of walls
	PERF_COUNT
};

void perf_frame(void); // at the top of each frame
void perf_set_resolution(int w, int h); // (the dynamic resolution's: shown, logged)
void perf_set_live(int live);           // (in play: a frame over 150 ms is logged with where it went)
void perf_begin(int section);
void perf_end(int section);
// The readout's lines (into lines, each up to 64 chars); how many
int perf_lines(char lines[][64], int max);
// The readout's numbers on one line (for the log: where, each section's ms)
int perf_summary(char *out, int size);
// Memory left: the heap (malloc) and vitaGL's pools, in KB (also used for the budgets)
unsigned perf_heap_free_kb(void);
unsigned perf_gpu_free_kb(void); // video memory (CDRAM)

// Laps through the game update: perf_lap("name") ends the last lap (charged to its name) and
// starts this one (NULL: stops); the heaviest per frame over the frames since asked
void perf_lap(const char *name);
unsigned perf_frame_count(void); // (frames begun)
void perf_where(const char **section, const char **lap); // (where the frame is now: for the watchdog's log line)
int perf_laps_summary(char *out, int size, int frames);

#endif
