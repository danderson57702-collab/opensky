#include "music.h"

#include "adpcm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#define MAX_TRACKS 32
#define SKMU_VERSION 1
#define OUT_FRAMES 1024           // samples per channel per sceAudioOutOutput
#define MAX_BLOCK_FRAMES 4096
#define VOLUME 0.75f              // music sits under the (future) world sounds
#define FIRST_DELAY_S 3.0f
#define GAP_MIN_S 25.0f           // silence between tracks, as in Skyrim
#define GAP_MAX_S 70.0f
#define FADE_S 1.5f               // fade used when toggling off or quitting

static char pack[256];
static char tracks[MAX_TRACKS][64];
static int track_period[MAX_TRACKS]; // PERIOD_* from the track name
static int track_count;
static volatile float game_hours = 12.0f;

enum { PERIOD_MORNING, PERIOD_DAY, PERIOD_DUSK, PERIOD_NIGHT };
static uint32_t next_random(void);

static int period_of_name(const char *name) {
	if (strstr(name, "_night_"))
		return PERIOD_NIGHT;
	if (strstr(name, "_dusk_"))
		return PERIOD_DUSK;
	if (strstr(name, "_morning_"))
		return PERIOD_MORNING;
	return PERIOD_DAY;
}

static int period_of_hours(float h) {
	if (h >= 5.0f && h < 8.5f)
		return PERIOD_MORNING;
	if (h >= 8.5f && h < 17.0f)
		return PERIOD_DAY;
	if (h >= 17.0f && h < 20.5f)
		return PERIOD_DUSK;
	return PERIOD_NIGHT;
}

// A random track for the current time of day (day tracks if that period has none)
static int pick_track(int last) {
	int want = period_of_hours(game_hours), pool[MAX_TRACKS], n = 0;
	for (int pass = 0; pass < 2 && n == 0; pass++)
		for (int i = 0; i < track_count; i++)
			if (track_period[i] == (pass ? PERIOD_DAY : want) && (i != last || track_count == 1))
				pool[n++] = i;
	if (n == 0)
		return last >= 0 ? last : 0;
	return pool[next_random() % (uint32_t)n];
}
static SceUID thread_id = -1;
static volatile int quit;
static volatile int enabled = 1;
static volatile float duck_target = 1.0f; // music_duck
static volatile float user_volume = 0.8f;  // music_set_volume
#define DUCK_S 0.35f             // time to dip under speech / come back
static uint32_t rng;
static char special[64];          // music_special: the set piece asked for...
static volatile int special_asked; // ...and that it's waiting
static int playing_special;

// --- Playback --------------------------------------------------------------------------

static uint32_t next_random(void) {
	rng = rng * 1664525u + 1013904223u;
	return rng >> 8;
}

// Sleeps up to `seconds`, returning early (0) when asked to quit
static int wait_s(float seconds) {
	for (float t = 0.0f; t < seconds; t += 0.1f) {
		if (quit)
			return 0;
		sceKernelDelayThread(100 * 1000);
	}
	return !quit;
}

// Reads exactly n bytes (sceIo: thread-safe, unlike newlib's FILE table)
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

// The track read ahead by a thread of its own: some 20 seconds of it in memory, so the card
// busy with the world's reads (Whiterun's people read as they come into view) doesn't leave
// the music waiting mid-block - it skipped, walking through the city
#define RING_BYTES (1u << 20)
#define FEED_CHUNK (64u * 1024u)
#define PREFILL_BYTES (192u * 1024u)
static uint8_t ring[RING_BYTES];
static volatile uint32_t ring_w, ring_r;     // (bytes written, read: ever-growing)
static volatile SceUID feed_fd = -1;
static volatile int feed_eof, feed_busy;
static SceUID feeder_id = -1;

static int feeder_thread(SceSize args, void *argp) {
	(void)args, (void)argp;
	while (!quit) {
		const SceUID fd = feed_fd;
		feed_busy = fd >= 0;
		const uint32_t used = ring_w - ring_r;
		if (fd < 0 || feed_eof || RING_BYTES - used < FEED_CHUNK) {
			feed_busy = 0;
			sceKernelDelayThread(4 * 1000);
			continue;
		}
		const uint32_t at = ring_w % RING_BYTES, room = RING_BYTES - at;
		const int n = sceIoRead(fd, ring + at, room < FEED_CHUNK ? room : FEED_CHUNK);
		__sync_synchronize();
		if (n <= 0)
			feed_eof = 1;
		else
			ring_w += (uint32_t)n;
	}
	feed_busy = 0;
	return 0;
}

static int read_exact(SceUID fd, void *dst, int n);
static void feed_start(SceUID fd) {
	ring_w = ring_r = 0, feed_eof = 0;
	__sync_synchronize();
	feed_fd = fd;
	if (feeder_id < 0)
		return;
	while (!quit && !feed_eof && ring_w - ring_r < PREFILL_BYTES) // (a start in hand before it plays)
		sceKernelDelayThread(2 * 1000);
}

static void feed_stop(void) {
	feed_fd = -1;
	__sync_synchronize();
	while (feed_busy) // (its read done before the file's closed)
		sceKernelDelayThread(1000);
}

// Reads exactly n bytes of the track from what's read ahead (waiting on the reader if need be)
static int ring_read(void *dst, uint32_t n) {
	if (feeder_id < 0) // (no reader thread: straight from the card, as before)
		return read_exact(feed_fd, dst, (int)n);
	while (ring_w - ring_r < n) {
		if (feed_eof || quit)
			return 0;
		sceKernelDelayThread(1000);
	}
	__sync_synchronize();
	const uint32_t at = ring_r % RING_BYTES, first = RING_BYTES - at < n ? RING_BYTES - at : n;
	memcpy(dst, ring + at, first);
	memcpy((uint8_t *)dst + first, ring, n - first);
	ring_r += n;
	return 1;
}

// Plays one track to the end (or until quit/disabled, with a fade). Returns 0 on error.
static int play_track(const char *name) {
	char path[400];
	snprintf(path, sizeof(path), "%smusic/%s.skmus", pack, name);
	SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (f < 0) {
		sceClibPrintf("music: can't open %s\n", path);
		return 0;
	}
	struct __attribute__((packed)) {
		char magic[4];
		uint16_t version, channels;
		uint32_t rate, frames, block_frames;
	} h;
	int ok = read_exact(f, &h, sizeof(h)) && !memcmp(h.magic, "SKMU", 4) && h.version == SKMU_VERSION &&
	         h.channels == 2 && h.block_frames % OUT_FRAMES == 0 && h.block_frames <= MAX_BLOCK_FRAMES;
	if (!ok) {
		sceClibPrintf("music: bad file %s\n", path);
		sceIoClose(f);
		return 0;
	}
	const uint32_t rate = h.rate, frames = h.frames, block_frames = h.block_frames;
	int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, OUT_FRAMES, (int)rate, SCE_AUDIO_OUT_MODE_STEREO);
	if (port < 0) {
		sceClibPrintf("music: sceAudioOutOpenPort failed 0x%08X\n", port);
		sceIoClose(f);
		return 0;
	}
	int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
	sceClibPrintf("music: playing %s (%u s)\n", name, (unsigned)(frames / rate));

	const int ch_block = 4 + (int)block_frames / 2;
	static uint8_t block[2 * (4 + MAX_BLOCK_FRAMES / 2)];
	static int16_t pcm[MAX_BLOCK_FRAMES * 2];
	static int16_t out[2][OUT_FRAMES * 2]; // double-buffered: the port reads while we fill
	int which = 0;
	float gain = VOLUME, fade_step = VOLUME / (FADE_S * rate);
	static float duck = 1.0f; // kept across tracks
	const float duck_step = 1.0f / (DUCK_S * rate);
	uint32_t played = 0;
	feed_start(f);
	while (played < frames) {
		if (!ring_read(block, (uint32_t)ch_block * 2))
			break;
		adpcm_decode_block(block, (int)block_frames, pcm, 2);
		adpcm_decode_block(block + ch_block, (int)block_frames, pcm + 1, 2);
		for (uint32_t o = 0; o < block_frames && played < frames; o += OUT_FRAMES) {
			int16_t *dst = out[which];
			int n = 0;
			for (; n < OUT_FRAMES && played < frames; n++, played++) {
				if (quit || !enabled || (special_asked && !playing_special))
					gain -= fade_step * 2.0f;
				if (gain < 0.0f)
					gain = 0.0f;
				if (duck < duck_target)
					duck = duck + duck_step > duck_target ? duck_target : duck + duck_step;
				else if (duck > duck_target)
					duck = duck - duck_step < duck_target ? duck_target : duck - duck_step;
				float g = gain * duck * user_volume;
				dst[n * 2] = (int16_t)(pcm[(o + n) * 2] * g);
				dst[n * 2 + 1] = (int16_t)(pcm[(o + n) * 2 + 1] * g);
			}
			for (; n < OUT_FRAMES; n++)
				dst[n * 2] = dst[n * 2 + 1] = 0;
			sceAudioOutOutput(port, dst); // blocks until the previous buffer has played
			which ^= 1;
			if (gain <= 0.0f)
				played = frames; // faded out: stop this track
		}
	}
	sceAudioOutOutput(port, NULL); // wait for the last buffer
	sceAudioOutReleasePort(port);
	feed_stop();
	sceIoClose(f);
	return 1;
}

static int music_thread(SceSize args, void *argp) {
	(void)args, (void)argp;
	int last = -1;
	if (!wait_s(FIRST_DELAY_S))
		return 0;
	while (!quit) {
		if (!enabled) {
			wait_s(0.2f);
			continue;
		}
		if (special_asked) {
			char name[64];
			snprintf(name, sizeof(name), "%s", special);
			special_asked = 0, playing_special = 1;
			play_track(name);
			playing_special = 0;
		} else {
			int t = pick_track(last);
			last = t;
			if (!play_track(tracks[t]) && !wait_s(5.0f))
				break;
		}
		float gap = GAP_MIN_S + (GAP_MAX_S - GAP_MIN_S) * (float)(next_random() % 1000) / 1000.0f;
		for (float w = 0.0f; w < gap && !quit && enabled && !special_asked; w += 0.1f)
			sceKernelDelayThread(100 * 1000);
	}
	return 0;
}

void music_init(const char *pack_dir) {
	snprintf(pack, sizeof(pack), "%s", pack_dir);
	char path[400];
	snprintf(path, sizeof(path), "%smusic/music.txt", pack);
	FILE *f = fopen(path, "r");
	if (!f) {
		sceClibPrintf("music: no %s, playing nothing\n", path);
		return;
	}
	char line[64];
	while (track_count < MAX_TRACKS && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0]) {
			track_period[track_count] = period_of_name(line);
			snprintf(tracks[track_count++], sizeof(tracks[0]), "%s", line);
		}
	}
	fclose(f);
	if (!track_count)
		return;
	rng = (uint32_t)sceKernelGetProcessTimeWide() | 1u;
	thread_id = sceKernelCreateThread("music", music_thread, 0x10000100, 0x10000, 0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
	if (thread_id < 0) {
		sceClibPrintf("music: thread creation failed 0x%08X\n", thread_id);
		return;
	}
	sceKernelStartThread(thread_id, 0, NULL);
	feeder_id = sceKernelCreateThread("music_feed", feeder_thread, 0x10000100, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_2, NULL);
	if (feeder_id >= 0)
		sceKernelStartThread(feeder_id, 0, NULL);
	sceClibPrintf("music: %d tracks\n", track_count);
}

void music_set_hours(float hours) {
	game_hours = hours;
}

void music_toggle(void) {
	enabled = !enabled;
	sceClibPrintf("music: %s\n", enabled ? "on" : "off");
}

void music_shutdown(void) {
	if (thread_id < 0)
		return;
	quit = 1;
	SceUInt timeout = 3 * 1000 * 1000;
	sceKernelWaitThreadEnd(thread_id, NULL, &timeout);
	sceKernelDeleteThread(thread_id);
	thread_id = -1;
	if (feeder_id >= 0) {
		sceKernelWaitThreadEnd(feeder_id, NULL, &timeout);
		sceKernelDeleteThread(feeder_id);
		feeder_id = -1;
	}
}

void music_set_volume(float volume) {
	user_volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
}

void music_duck(float level) {
	duck_target = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
}

void music_special(const char *name) {
	if (!enabled)
		return;
	snprintf(special, sizeof(special), "%s", name);
	special_asked = 1;
}
