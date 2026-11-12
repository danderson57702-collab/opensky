#include "voice.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "adpcm.h"
#include "gamelog.h"
#include "sfx.h"

#define SKVOC_VERSION 1
#define VOICE_RATE 22050          // every line is converted to this (converter/dialogue.py)
#define OUT_FRAMES 512            // per sceAudioOutOutput
#define MAX_BLOCK_FRAMES 2048
#define VOLUME 1.0f
#define MIX_SMOOTH 0.05f          // per output buffer: gain/pan follow the speaker smoothly

static char pack[256];
static SceUID thread_id = -1, request_sema = -1;
static SceKernelLwMutexWork lock;
static char requested[160];       // next line to play ("" = none)
static volatile int request_pending, stop_requested, playing;
static volatile float mix_gain = 1.0f, mix_pan = 0.0f;
static volatile float level; // speech loudness of the buffer being played
static volatile float user_volume = 1.0f;
#define LEVEL_FULL_RMS 5000.0f // this loud (of 32767) opens the mouth fully

static int read_exact(SceUID fd, void *dst, int n) {
	int got = 0;
	while (got < n) {
		int r = sceIoRead(fd, (char *)dst + got, n - got);
		if (r <= 0)
			return 0;
		got += r;
	}
	return 1;
}

// A line is read whole before it plays (the biggest is about 210 KB): read a block at a time while it
// played, a block's read waited behind the loader's (a model, a texture - "loads queued 20" on the cart
// ride) and the mixer's third of a second ran dry: Lokir's voice cut out for a line or two
#define FILE_MAX (320 * 1024)
static uint8_t file_buf[FILE_MAX];

static void play_file(const char *name) {
	char path[448];
	snprintf(path, sizeof(path), "%svoice/%s", pack, name);
	const SceUInt64 t0 = sceKernelGetProcessTimeWide();
	SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (f < 0) {
		game_log("voice: can't open %s (0x%08X)", path, (unsigned)f);
		return;
	}
	const int size = (int)sceIoLseek(f, 0, SCE_SEEK_END);
	sceIoLseek(f, 0, SCE_SEEK_SET);
	const int ok = size > 0 && size <= FILE_MAX && read_exact(f, file_buf, size);
	sceIoClose(f);
	const unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000);
	if (ms > 150) // (the card busy: the line starts late, but plays whole)
		game_log("voice: %s took %u ms to read (%d KB)", name, ms, size / 1024);
	struct __attribute__((packed)) Head {
		char magic[4];
		uint16_t version, channels;
		uint32_t rate, frames, block_frames;
	} h;
	if (!ok || size < (int)sizeof(h)) {
		game_log("voice: can't read %s (%d bytes)", path, size);
		return;
	}
	memcpy(&h, file_buf, sizeof(h));
	if (memcmp(h.magic, "SKMU", 4) || h.version != SKVOC_VERSION || h.channels != 1 || h.rate != VOICE_RATE ||
	    h.block_frames > MAX_BLOCK_FRAMES || h.block_frames % OUT_FRAMES) {
		game_log("voice: bad file %s", path);
		return;
	}
	const uint8_t *at = file_buf + sizeof(h), *end = file_buf + size;
	const int block_bytes = 4 + (int)h.block_frames / 2;
	static int16_t pcm[MAX_BLOCK_FRAMES];
	static int16_t out[2][OUT_FRAMES * 2];
	int which = 0;
	float gl = 0.0f, gr = 0.0f;
	uint32_t played = 0;
	while (played < h.frames && !stop_requested && !request_pending) {
		if (at + block_bytes > end)
			break;
		adpcm_decode_block(at, (int)h.block_frames, pcm, 1);
		at += block_bytes;
		for (uint32_t o = 0; o < h.block_frames && played < h.frames; o += OUT_FRAMES) {
			// Equal-power pan, eased toward the current mix
			float pan = mix_pan, g = mix_gain * VOLUME * user_volume;
			float tl = g * (pan > 0.0f ? 1.0f - pan * 0.7f : 1.0f), tr = g * (pan < 0.0f ? 1.0f + pan * 0.7f : 1.0f);
			if (o == 0 && played == 0)
				gl = tl, gr = tr;
			int16_t *dst = out[which];
			int n = 0;
			for (; n < OUT_FRAMES && played < h.frames; n++, played++) {
				gl += (tl - gl) * MIX_SMOOTH * 0.02f;
				gr += (tr - gr) * MIX_SMOOTH * 0.02f;
				dst[n * 2] = (int16_t)(pcm[o + n] * gl);
				dst[n * 2 + 1] = (int16_t)(pcm[o + n] * gr);
			}
			float sum = 0.0f;
			for (int k = 0; k < n; k++)
				sum += (float)pcm[o + k] * pcm[o + k];
			float rms = n ? sqrtf(sum / n) : 0.0f;
			level = rms > LEVEL_FULL_RMS ? 1.0f : rms / LEVEL_FULL_RMS;
			for (; n < OUT_FRAMES; n++)
				dst[n * 2] = dst[n * 2 + 1] = 0;
			sfx_stream_write(dst, OUT_FRAMES); // (mixed in with the effects: sfx.c)
			which ^= 1;
		}
	}
	level = 0.0f;
}

static int voice_thread(SceSize args, void *argp) {
	(void)args, (void)argp;
	for (;;) {
		sceKernelWaitSema(request_sema, 1, NULL);
		char name[160];
		sceKernelLockLwMutex(&lock, 1, NULL);
		snprintf(name, sizeof(name), "%s", requested);
		requested[0] = 0;
		request_pending = 0;
		stop_requested = 0;
		sceKernelUnlockLwMutex(&lock, 1);
		if (!name[0])
			continue;
		playing = 1;
		static int said;
		if (said++ < 40) // (the first few, to see they play)
			game_log("voice: playing %s", name);
		play_file(name);
		if (stop_requested)
			sfx_stream_clear(); // (cut off: what's queued goes too)
		else
			sfx_stream_drain(); // let the last of it play
		playing = request_pending; // another line may already be queued
	}
	return 0;
}

void voice_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	sceKernelCreateLwMutex(&lock, "voice", 0, 0, NULL);
	request_sema = sceKernelCreateSema("voice_req", 0, 0, 16, NULL);
	thread_id = sceKernelCreateThread("voice", voice_thread, 0x10000100, 0x10000, 0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
	if (thread_id < 0) {
		game_log("voice: no thread (0x%08X)", (unsigned)thread_id);
		return;
	}
	sceKernelStartThread(thread_id, 0, NULL);
}

int voice_play(const char *path) {
	if (thread_id < 0)
		return 0;
	sceKernelLockLwMutex(&lock, 1, NULL);
	snprintf(requested, sizeof(requested), "%s", path);
	request_pending = 1;
	playing = 1;
	sceKernelUnlockLwMutex(&lock, 1);
	sceKernelSignalSema(request_sema, 1);
	return 1;
}

void voice_stop(void) {
	stop_requested = 1;
}

int voice_playing(void) {
	return playing;
}

void voice_set_volume(float volume) {
	user_volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

float voice_level(void) {
	return playing ? level : 0.0f;
}

void voice_set_mix(float gain, float pan) {
	mix_gain = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
	mix_pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
}
