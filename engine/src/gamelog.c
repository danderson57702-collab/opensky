#include "gamelog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "stream.h"
#include "perf.h"

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#define GAME_LOG "ux0:data/opensky_log.txt"

// Lines go to the card as they come while it starts (a crash leaves the step it died in); once it's
// playing (game_log_background) they're queued and a thread of their own writes them twice a second.
// The game never waits on that thread: a line is queued only if the queue is free this instant
// (a try-lock; else the line is dropped and counted), and the writer - above the loader's priority -
// holds the queue only to swap it for the empty one. (Nov 10: the first
// writer, below the loader's priority, was starved by it while holding the queue - the game's next
// line waited 0.2-0.9 s, logged that as a hitch, and waited again: runs of 4 fps on the user's Vita)
#define QUEUE_BYTES (48 * 1024)
static char queues[2][QUEUE_BYTES];
static volatile int active, queued[2], dropped;
static int background;
static SceKernelLwMutexWork lock;

static void write_out(const char *text, int n, int truncate) {
	SceUID fd = sceIoOpen(GAME_LOG, SCE_O_WRONLY | SCE_O_CREAT | (truncate ? SCE_O_TRUNC : SCE_O_APPEND), 0777);
	if (fd >= 0) {
		sceIoWrite(fd, text, n);
		sceIoClose(fd);
	}
}

static int writer(SceSize args, void *argp) {
	(void)args, (void)argp;
	unsigned last_frame = 0, stuck = 0;
	for (;;) {
		sceKernelDelayThread(500 * 1000);
		{ // (the watchdog: no new frame in 4 s - where the game's thread is, said once a freeze; a hang leaves that in the log)
			const unsigned f = perf_frame_count();
			stuck = f == last_frame ? stuck + 1 : 0, last_frame = f;
			if (stuck == 8) {
				const char *section, *lap;
				perf_where(&section, &lap);
				char line[160];
				const int n = snprintf(line, sizeof(line), "   watchdog: no frame for 4 s - the game's thread is in %s / %s (frame %u)\n", section, lap, f);
				write_out(line, n, 0);
				sceClibPrintf("%s", line);
			}
		}
		if (!queued[active])
			continue;
		sceKernelLockLwMutex(&lock, 1, NULL); // (held for these three lines only)
		const int full = active;
		active ^= 1, queued[active] = 0;
		sceKernelUnlockLwMutex(&lock, 1);
		write_out(queues[full], queued[full], 0);
		queued[full] = 0;
	}
	return 0;
}

void game_log_background(void) {
	if (background)
		return;
	sceKernelCreateLwMutex(&lock, "gamelog", 0, 0, NULL);
	SceUID t = sceKernelCreateThread("gamelog", writer, 0x10000100 - 4, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
	if (t >= 0 && sceKernelStartThread(t, 0, NULL) >= 0)
		background = 1;
}
#else
void game_log_background(void) {
}
#endif

void game_log(const char *fmt, ...) {
	static unsigned long long t0;
	static int started;
	if (!t0)
		t0 = stream_now_us();
	char line[720];
	int n = snprintf(line, sizeof(line), "%7.2f s  ", (stream_now_us() - t0) / 1e6);
	va_list ap;
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof(line) - 2)
		n = (int)sizeof(line) - 2;
	line[n++] = '\n', line[n] = 0;
#ifdef __vita__
	if (!started) { // (the last session's log kept beside it)
		sceIoRemove("ux0:data/opensky_log_prev.txt");
		sceIoRename(GAME_LOG, "ux0:data/opensky_log_prev.txt");
	}
	if (background) { // (queued if the queue's free this instant: never waited for)
		if (sceKernelTryLockLwMutex(&lock, 1) >= 0) {
			const int q = active;
			if (queued[q] + n <= QUEUE_BYTES)
				memcpy(queues[q] + queued[q], line, n), queued[q] += n;
			else
				dropped++;
			sceKernelUnlockLwMutex(&lock, 1);
		} else {
			dropped++;
		}
	} else {
		write_out(line, n, !started);
	}
	started = 1;
	sceClibPrintf("%s", line);
#else
	started = 1;
	fputs(line, stdout);
#endif
}
