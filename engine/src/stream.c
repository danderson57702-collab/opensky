#include "stream.h"

#include <stddef.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

static SceKernelLwMutexWork lock;
static SceUID work_sema = -1;
static StreamJob *todo_head, *todo_tail; // waiting for the worker
static StreamJob *done_head, *done_tail; // worked, waiting for finish (render thread)
static volatile int pending;              // submitted, not yet finished
static volatile int worker_busy;
static volatile unsigned long long worker_since; // (when its current job began)
static volatile int jobs_done;                   // worked so far
static const char *volatile working_on = "";
volatile int stream_progress;

const char *stream_working_on(void) {
	return working_on;
}

static void push(StreamJob **head, StreamJob **tail, StreamJob *job) {
	job->next = NULL;
	if (*tail)
		(*tail)->next = job;
	else
		*head = job;
	*tail = job;
}

// The waiting job with the lowest priority (first submitted among equals), unlinked
static StreamJob *pop_best(StreamJob **head, StreamJob **tail) {
	StreamJob *best = NULL, *best_prev = NULL, *prev = NULL;
	int best_p = 0;
	for (StreamJob *j = *head; j; prev = j, j = j->next) {
		int p = j->priority ? j->priority(j) : 0;
		if (!best || p < best_p)
			best = j, best_prev = prev, best_p = p;
	}
	if (!best)
		return NULL;
	if (best_prev)
		best_prev->next = best->next;
	else
		*head = best->next;
	if (*tail == best)
		*tail = best_prev;
	return best;
}

static StreamJob *pop(StreamJob **head, StreamJob **tail) {
	StreamJob *job = *head;
	if (job) {
		*head = job->next;
		if (!*head)
			*tail = NULL;
	}
	return job;
}

unsigned long long stream_now_us(void) {
	return sceKernelGetProcessTimeWide();
}

static int worker(SceSize args, void *argp) {
	(void)args, (void)argp;
	for (;;) {
		sceKernelWaitSema(work_sema, 1, NULL);
		sceKernelLockLwMutex(&lock, 1, NULL);
		StreamJob *job = pop_best(&todo_head, &todo_tail);
		worker_busy = job != NULL;
		worker_since = sceKernelGetProcessTimeWide();
		sceKernelUnlockLwMutex(&lock, 1);
		if (!job)
			continue;
		working_on = job->what ? job->what : "a job";
		job->work(job);
		working_on = "";
		sceKernelLockLwMutex(&lock, 1, NULL);
		push(&done_head, &done_tail, job);
		worker_busy = 0;
		jobs_done++;
		sceKernelUnlockLwMutex(&lock, 1);
	}
	return 0;
}

void stream_init(void) {
	sceKernelCreateLwMutex(&lock, "stream", 0, 0, NULL);
	work_sema = sceKernelCreateSema("stream_work", 0, 0, 0x7fffffff, NULL);
	// Its own core, a bit below the render thread: loading must never steal frame time
	SceUID th = sceKernelCreateThread("stream", worker, 0x10000100 + 8, 256 * 1024, 0,
	                                  SCE_KERNEL_CPU_MASK_USER_2, NULL); // (core 2: the posing has core 1, the game core 0)
	if (th < 0) {
		sceClibPrintf("stream: thread creation failed 0x%08X\n", th);
		return;
	}
	sceKernelStartThread(th, 0, NULL);
}

void stream_submit(StreamJob *job) {
	pending++;
	sceKernelLockLwMutex(&lock, 1, NULL);
	push(&todo_head, &todo_tail, job);
	sceKernelUnlockLwMutex(&lock, 1);
	sceKernelSignalSema(work_sema, 1);
}

void stream_stats(int *waiting, int *worked, int *busy_ms, int *done_total) {
	int w = 0, d = 0;
	sceKernelLockLwMutex(&lock, 1, NULL);
	for (StreamJob *j = todo_head; j; j = j->next)
		w++;
	for (StreamJob *j = done_head; j; j = j->next)
		d++;
	sceKernelUnlockLwMutex(&lock, 1);
	*waiting = w, *worked = d, *done_total = jobs_done;
	*busy_ms = worker_busy ? (int)((sceKernelGetProcessTimeWide() - worker_since) / 1000) : -1;
}

int stream_pending(void) {
	return pending;
}

// Finishes done jobs (in order) until the deadline; returns 1 if the done queue emptied
static int finish_until(unsigned long long deadline) {
	for (;;) {
		sceKernelLockLwMutex(&lock, 1, NULL);
		StreamJob *job = done_head;
		sceKernelUnlockLwMutex(&lock, 1);
		if (!job)
			return 1;
		if (!job->finish(job, deadline))
			return 0; // resumes next frame
		sceKernelLockLwMutex(&lock, 1, NULL);
		pop(&done_head, &done_tail);
		sceKernelUnlockLwMutex(&lock, 1);
		pending--;
		job->discard(job);
		if (stream_now_us() >= deadline)
			return 0;
	}
}

void stream_pump(unsigned budget_us) {
	finish_until(stream_now_us() + budget_us);
}

void stream_flush(void) {
	while (pending > 0) {
		if (finish_until(stream_now_us() + 1000000ull) && pending > 0)
			sceKernelDelayThread(1000);
	}
}
