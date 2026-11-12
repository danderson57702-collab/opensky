#include "sfx.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include "adpcm.h"
#include "fileio.h"

#define SKSF_VERSION 2
#define OUT_FRAMES 256 // per sceAudioOutOutput (about 12 ms at 22050 Hz)
#define MAX_EVENTS 512 // (the bank: about 270 - more than this were silently dropped)
#define MAX_VARIANTS 8
#define MAX_BLOCK 1024
#define SFX_GAIN 0.7f // (Skyrim's effects sit under its music and voices)

typedef struct {
	const uint8_t *blocks; // in the bank
	int frames;
} Variant;

typedef struct {
	char name[25];
	int loop, variant_count, last;
	Variant variants[MAX_VARIANTS];
} Event;

typedef struct {
	const Variant *v;  // NULL: free
	int pos, loop;
	float gain_l, gain_r;
	int16_t block[MAX_BLOCK]; // the decoded block being played
	int block_index;          // which block is in `block` (-1: none)
} Voice;

static uint8_t *bank;
static Event events[MAX_EVENTS];
static int event_count, rate = 22050, block_frames = 256;
static Voice voices[SFX_VOICES];
static SceKernelLwMutexWork lock;
static volatile float master = 1.0f;
static float listener[3]; // x, z, yaw (radians)
static unsigned rng = 12345;

// Spoken lines (voice.c) mixed in here: the Vita has too few voice-type audio ports for a
// port of their own (SCE_AUDIO_OUT_ERROR_PORT_FULL). A ring of stereo frames at this rate,
// written by the voice thread, read by this one (one each: the indices are enough)
#define STREAM_FRAMES 8192
static int16_t stream[STREAM_FRAMES * 2];
static volatile unsigned stream_write, stream_read; // (frames, ever increasing)

int sfx_stream_write(const int16_t *stereo, int frames) {
	int done = 0;
	while (done < frames) {
		const unsigned room = STREAM_FRAMES - (stream_write - stream_read);
		if (!room) {
			sceKernelDelayThread(2000); // (the mixer takes them as it plays)
			continue;
		}
		const unsigned n = (unsigned)(frames - done) < room ? (unsigned)(frames - done) : room;
		for (unsigned k = 0; k < n; k++) {
			const unsigned at = (stream_write + k) % STREAM_FRAMES;
			stream[at * 2] = stereo[(done + k) * 2], stream[at * 2 + 1] = stereo[(done + k) * 2 + 1];
		}
		__sync_synchronize();
		stream_write += n;
		done += (int)n;
	}
	return done;
}

void sfx_stream_drain(void) {
	while (stream_write != stream_read)
		sceKernelDelayThread(2000);
}

void sfx_stream_clear(void) {
	stream_read = stream_write;
}

static int sfx_thread(SceSize args, void *argp) {
	(void)args, (void)argp;
	int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, OUT_FRAMES, rate, SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) {
		sceClibPrintf("sfx: sceAudioOutOpenPort failed 0x%08X\n", port);
		return 0;
	}
	int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
	static int16_t out[2][OUT_FRAMES * 2];
	int which = 0;
	for (;;) {
		static int32_t mix[OUT_FRAMES * 2];
		memset(mix, 0, sizeof(mix));
		sceKernelLockLwMutex(&lock, 1, NULL);
		const float m = master;
		for (int i = 0; i < SFX_VOICES; i++) {
			Voice *v = &voices[i];
			if (!v->v)
				continue;
			const int gl = (int)(v->gain_l * m * SFX_GAIN * 256.0f), gr = (int)(v->gain_r * m * SFX_GAIN * 256.0f);
			const int block_bytes = 4 + block_frames / 2;
			for (int f = 0; f < OUT_FRAMES; f++) {
				if (v->pos >= v->v->frames) {
					if (!v->loop) {
						v->v = NULL;
						break;
					}
					v->pos = 0;
				}
				const int b = v->pos / block_frames;
				if (b != v->block_index) {
					adpcm_decode_block(v->v->blocks + (size_t)b * block_bytes, block_frames, v->block, 1);
					v->block_index = b;
				}
				const int s = v->block[v->pos % block_frames];
				mix[f * 2] += s * gl >> 8, mix[f * 2 + 1] += s * gr >> 8;
				v->pos++;
			}
		}
		sceKernelUnlockLwMutex(&lock, 1);
		{ // (a spoken line's frames, if any are waiting)
			unsigned avail = stream_write - stream_read;
			__sync_synchronize();
			if (avail > OUT_FRAMES)
				avail = OUT_FRAMES;
			for (unsigned f = 0; f < avail; f++) {
				const unsigned at = (stream_read + f) % STREAM_FRAMES;
				mix[f * 2] += stream[at * 2], mix[f * 2 + 1] += stream[at * 2 + 1];
			}
			stream_read += avail;
		}
		int16_t *dst = out[which];
		for (int k = 0; k < OUT_FRAMES * 2; k++)
			dst[k] = (int16_t)(mix[k] > 32767 ? 32767 : mix[k] < -32768 ? -32768 : mix[k]);
		sceAudioOutOutput(port, dst);
		which ^= 1;
	}
	return 0;
}

void sfx_init(const char *pack_dir) {
	char path[256];
	snprintf(path, sizeof(path), "%ssound/sfx.bank", pack_dir);
	long size = 0;
	bank = file_read_all(path, &size);
	if (!bank || size < 16 || memcmp(bank, "SKSF", 4) || *(uint16_t *)(bank + 4) != SKSF_VERSION) {
		sceClibPrintf("sfx: no sound bank %s\n", path);
		free(bank);
		bank = NULL;
		return;
	}
	const int count = *(uint16_t *)(bank + 6);
	memcpy(&rate, bank + 8, 4);
	memcpy(&block_frames, bank + 12, 4);
	if (block_frames <= 0 || block_frames > MAX_BLOCK || block_frames % 2) {
		free(bank);
		bank = NULL;
		return;
	}
	const int block_bytes = 4 + block_frames / 2;
	const uint8_t *p = bank + 16, *end = bank + size;
	for (int e = 0; e < count && event_count < MAX_EVENTS; e++) {
		if (p + 28 > end)
			break;
		Event *ev = &events[event_count];
		memset(ev, 0, sizeof(*ev));
		memcpy(ev->name, p, 24);
		const int nv = p[24];
		ev->loop = p[25] & 1;
		p += 28;
		for (int k = 0; k < nv; k++) {
			uint32_t frames;
			if (p + 4 > end)
				break;
			memcpy(&frames, p, 4);
			p += 4;
			const long bytes = (long)((frames + block_frames - 1) / block_frames) * block_bytes;
			if (p + bytes > end)
				break;
			if (ev->variant_count < MAX_VARIANTS)
				ev->variants[ev->variant_count++] = (Variant){p, (int)frames};
			p += bytes;
		}
		if (ev->variant_count)
			event_count++;
	}
	sceKernelCreateLwMutex(&lock, "sfx", 0, 0, NULL);
	SceUID t = sceKernelCreateThread("sfx", sfx_thread, 0x10000100, 0x8000, 0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
	if (t >= 0)
		sceKernelStartThread(t, 0, NULL);
	sceClibPrintf("sfx: %d sound events\n", event_count);
}

void sfx_set_volume(float volume) {
	master = volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume;
}

static Event *find(const char *name) {
	for (int i = 0; i < event_count; i++)
		if (!strcmp(events[i].name, name))
			return &events[i];
	return NULL;
}

static int start(const char *name, float volume, float pan, int loop) {
	Event *ev = bank ? find(name) : NULL;
	if (!ev || volume <= 0.0f)
		return -1;
	// A variant at random, not the one just played
	rng = rng * 1664525u + 1013904223u;
	int k = (int)((rng >> 16) % (unsigned)ev->variant_count);
	if (ev->variant_count > 1 && k == ev->last)
		k = (k + 1) % ev->variant_count;
	ev->last = k;
	sceKernelLockLwMutex(&lock, 1, NULL);
	int slot = -1;
	for (int i = 0; i < SFX_VOICES && slot < 0; i++)
		if (!voices[i].v)
			slot = i;
	if (slot < 0) { // none free: the one nearest its end makes way (never a loop)
		int best = 0x7fffffff;
		for (int i = 0; i < SFX_VOICES; i++)
			if (!voices[i].loop && voices[i].v->frames - voices[i].pos < best)
				best = voices[i].v->frames - voices[i].pos, slot = i;
	}
	if (slot >= 0) {
		Voice *v = &voices[slot];
		const float p = pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan;
		v->v = &ev->variants[k];
		v->pos = 0, v->loop = loop, v->block_index = -1; // (a looped sound played once ends: only sfx_loop's go round, and are stopped by their caller)
		v->gain_l = volume * (p > 0.0f ? 1.0f - p : 1.0f);
		v->gain_r = volume * (p < 0.0f ? 1.0f + p : 1.0f);
	}
	sceKernelUnlockLwMutex(&lock, 1);
	return slot;
}

int sfx_play(const char *event, float volume, float pan) {
	return start(event, volume, pan, 0);
}

int sfx_loop(const char *event, float volume) {
	return start(event, volume, 0.0f, 1);
}

void sfx_stop(int voice) {
	if (voice < 0 || voice >= SFX_VOICES)
		return;
	sceKernelLockLwMutex(&lock, 1, NULL);
	voices[voice].v = NULL;
	sceKernelUnlockLwMutex(&lock, 1);
}

void sfx_listener(float x, float z, float yaw_deg) {
	listener[0] = x, listener[1] = z, listener[2] = yaw_deg * (float)M_PI / 180.0f;
}

int sfx_play_at(const char *event, const float pos[3], float max_m) {
	const float dx = pos[0] - listener[0], dz = pos[2] - listener[1], d = hypotf(dx, dz);
	if (d >= max_m)
		return -1;
	const float near = 1.5f, volume = d < near ? 1.0f : 1.0f - (d - near) / (max_m - near);
	// Right of the view: (cos yaw, sin yaw) (the view looks along (sin yaw, -cos yaw))
	const float right = d > 0.01f ? (dx * cosf(listener[2]) + dz * sinf(listener[2])) / d : 0.0f;
	return sfx_play(event, volume * volume, right * 0.7f);
}
