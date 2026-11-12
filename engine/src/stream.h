#ifndef STREAM_H
#define STREAM_H

// Background streaming. Loading work is split in two:
//   work   runs on a worker thread (own CPU core): file reads, mesh/terrain building,
//          lighting, collision. Must not call GL.
//   finish runs on the render thread from stream_pump(), under a per-frame time budget:
//          GPU uploads and installing results. Returns 1 when done, 0 to be called
//          again next frame (for results too big to upload in one go).
// The worker takes the waiting job with the lowest priority() (evaluated when it picks,
// so it follows the player); ties go in submission order. Jobs finish in work order.

// Priority of a job that has `slack` rings to go before its result can be seen (0: it
// is about to be, negative: it already should be), `ring` cells from the player.
// Nearer jobs win between equal slack.
#define STREAM_PRIORITY(slack, ring) ((slack) * 16 + (ring))
#define STREAM_PRIORITY_URGENT (-1000) // gameplay (collision), and what other jobs wait on

typedef struct StreamJob {
	int (*priority)(struct StreamJob *job); // NULL: 0. Called on the worker thread.
	void (*work)(struct StreamJob *job);
	int (*finish)(struct StreamJob *job, unsigned long long deadline_us);
	void (*discard)(struct StreamJob *job); // frees the job (also called after finish)
	const char *what; // (diagnostics) what it loads
	struct StreamJob *next;
} StreamJob;

void stream_init(void);
void stream_submit(StreamJob *job);
// Finishes completed jobs until budget_us has been spent this frame.
void stream_pump(unsigned budget_us);
// Waits for and finishes everything queued (loading screen / startup).
void stream_flush(void);
// Jobs submitted but not yet finished.
int stream_pending(void);
// (diagnostics) jobs waiting for the worker, worked and waiting to finish, how long the worker's
// been on its current one (ms; -1 idle), jobs worked in all
void stream_stats(int *waiting, int *worked, int *busy_ms, int *done_total);
// (diagnostics) what the worker's on ("": idle); stream_progress: bumped as jobs work along
const char *stream_working_on(void);
extern volatile int stream_progress;
unsigned long long stream_now_us(void);

#endif
