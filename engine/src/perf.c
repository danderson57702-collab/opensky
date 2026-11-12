#include "perf.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "stream.h"
#include "gamelog.h"

#ifdef __vita__
#include <vitaGL.h>
#endif

#define AVERAGE_FRAMES 30 // (half a second at 60 fps)
#define WORST_FRAMES 60

extern int _newlib_heap_size_user; // (main.c: the heap's size)

static unsigned long long started[PERF_COUNT], frame_start;
static unsigned long long sum[PERF_COUNT];
static float shown[PERF_COUNT]; // ms, averaged
static unsigned worst_us, worst_shown_us;
static int frames, worst_frames;
static int res_w, res_h; // (the dynamic resolution's size this frame)
static int hitch_live;   // (in play: a frame this slow is logged with where its time went)
#define HITCH_US 150000u
#define LAPS_MAX 80
static struct { const char *name; unsigned long long us; } laps[LAPS_MAX];
static unsigned long long lap_frame_us[LAPS_MAX]; // (this frame's, for a hitch)
static unsigned long long section_at_frame[PERF_COUNT];
static int lap_count;

void perf_set_resolution(int w, int h) {
	res_w = w, res_h = h;
}

void perf_set_live(int live) {
	hitch_live = live;
}

// A frame far slower than the rest, in play: logged with its own heaviest laps and sections (the 963 ms frame on the road,
// Nov 8, showed only as a 10 s average)
static void log_hitch(unsigned us) {
	char line[400];
	int n = snprintf(line, sizeof(line), "hitch: a %u ms frame -", us / 1000);
	static const char *const names[PERF_COUNT] = {"frame", "game", "ai", "stream", "upload", "world", "actors", "fx",
	                                              "post", "swap", "input", "move", "look", "pose", "camera"};
	for (int i = 1; i < PERF_COUNT && n < (int)sizeof(line); i++) {
		const unsigned long long d = sum[i] >= section_at_frame[i] ? sum[i] - section_at_frame[i] : 0;
		if (d >= 20000)
			n += snprintf(line + n, sizeof(line) - n, " %s %u", names[i], (unsigned)(d / 1000));
	}
	n += snprintf(line + n, sizeof(line) - n, " | laps");
	char used[LAPS_MAX] = {0};
	for (int k = 0; k < 6 && n < (int)sizeof(line); k++) {
		int best = -1;
		for (int i = 0; i < lap_count; i++)
			if (!used[i] && (best < 0 || lap_frame_us[i] > lap_frame_us[best]))
				best = i;
		if (best < 0 || lap_frame_us[best] < 10000)
			break;
		used[best] = 1;
		n += snprintf(line + n, sizeof(line) - n, " %s %u", laps[best].name, (unsigned)(lap_frame_us[best] / 1000));
	}
	game_log("%s", line);
}

static volatile unsigned frame_count; // (frames begun: the watchdog's - gamelog.c)
static volatile const char *section_now = "";

unsigned perf_frame_count(void) {
	return frame_count;
}

void perf_frame(void) {
	frame_count++;
	const unsigned long long now = stream_now_us();
	if (frame_start) {
		const unsigned us = (unsigned)(now - frame_start);
		if (us > HITCH_US && hitch_live)
			log_hitch(us);
		sum[PERF_FRAME] += us;
		if (us > worst_us)
			worst_us = us;
		if (++worst_frames >= WORST_FRAMES)
			worst_shown_us = worst_us, worst_us = 0, worst_frames = 0;
		if (++frames >= AVERAGE_FRAMES) {
			for (int i = 0; i < PERF_COUNT; i++)
				shown[i] = sum[i] / (float)frames / 1000.0f, sum[i] = 0;
			frames = 0;
		}
	}
	frame_start = now;
	memcpy(section_at_frame, sum, sizeof(section_at_frame));
	memset(lap_frame_us, 0, sizeof(lap_frame_us));
}

void perf_begin(int section) {
	static const char *const names[PERF_COUNT] = {"frame", "game", "ai", "stream", "upload", "world", "actors", "fx",
	                                              "post", "swap", "input", "move", "look", "pose", "camera"};
	section_now = names[section];
	started[section] = stream_now_us();
}

void perf_end(int section) {
	sum[section] += stream_now_us() - started[section];
}

// Laps: named marks through a long stretch of the frame (main's game update), each charging the
// time since the one before to its name; summed and the heaviest logged (perf_laps_summary)
static unsigned long long lap_at;
static const char *lap_name;

void perf_lap(const char *name) {
	const unsigned long long now = stream_now_us();
	if (lap_name) {
		int i = 0;
		while (i < lap_count && laps[i].name != lap_name)
			i++;
		if (i == lap_count && lap_count < LAPS_MAX)
			laps[lap_count++].name = lap_name;
		if (i < lap_count)
			laps[i].us += now - lap_at, lap_frame_us[i] += now - lap_at;
	}
	lap_name = name, lap_at = now;
}

// Where the frame is now - its section and lap (read by the watchdog from another thread: for the log only)
void perf_where(const char **section, const char **lap) {
	*section = (const char *)section_now, *lap = lap_name ? lap_name : "";
}

int perf_laps_summary(char *out, int size, int frames) {
	int n = 0, shown_n = 0;
	char used[LAPS_MAX] = {0};
	out[0] = 0;
	while (shown_n < 12 && n < size) { // (the heaviest few, heaviest first)
		int best = -1;
		for (int i = 0; i < lap_count; i++)
			if (!used[i] && (best < 0 || laps[i].us > laps[best].us))
				best = i;
		if (best < 0 || laps[best].us / (frames > 0 ? frames : 1) < 100)
			break;
		used[best] = 1, shown_n++;
		n += snprintf(out + n, size - n, " %s %.1f", laps[best].name, laps[best].us / 1000.0f / (frames > 0 ? frames : 1));
	}
	for (int i = 0; i < lap_count; i++)
		laps[i].us = 0;
	return n;
}

int perf_summary(char *out, int size) {
	static const char *const names[PERF_COUNT] = {"frame", "game", "ai", "stream", "upload", "world", "actors", "fx",
	                                              "post", "swap", "input", "move", "look", "pose", "camera"};
	int n = snprintf(out, size, "%.1f ms worst %.1f res %dx%d |", shown[PERF_FRAME], worst_shown_us / 1000.0f, res_w, res_h);
	for (int i = 1; i < PERF_COUNT && n < size; i++)
		n += snprintf(out + n, size - n, " %s %.1f", names[i], shown[i]);
	return n;
}

unsigned perf_heap_free_kb(void) {
	static struct mallinfo mi; // (mallinfo walks the heap: every few seconds)
	static unsigned long long walked;
	if (!walked || stream_now_us() - walked > 10000000ull) { // (every 10 s: each walk a stall - timed into the log once)
		const unsigned long long t0 = stream_now_us();
		mi = mallinfo(), walked = stream_now_us();
		static int told;
		if (told++ < 3)
			game_log("perf: the heap walked in %u ms", (unsigned)((walked - t0) / 1000));
	}
	const long used = (long)mi.uordblks;
	const long total = _newlib_heap_size_user;
	return used < total ? (unsigned)((total - used) / 1024) : 0;
}

// vitaGL's free memory: it walks its whole allocator to say (on the Vita about 15 ms), so it's
// asked every few seconds and remembered
static size_t vram_free, vram_total;
static void refresh_vram(void) {
#ifdef __vita__
	static unsigned long long asked;
	if (asked && stream_now_us() - asked < 10000000ull)
		return;
	asked = stream_now_us();
	vram_free = vglMemFree(VGL_MEM_VRAM), vram_total = vglMemTotal(VGL_MEM_VRAM);
	static int told;
	if (told++ < 3)
		game_log("perf: video memory walked in %u ms", (unsigned)((stream_now_us() - asked) / 1000));
#endif
}

unsigned perf_gpu_free_kb(void) {
#ifdef __vita__
	refresh_vram();
	if (vram_free > vram_total) // (an emulator's nonsense: unknown)
		return 0;
	return (unsigned)(vram_free / 1024);
#else
	return 0;
#endif
}

int perf_lines(char lines[][64], int max) {
	static const char *const names[PERF_COUNT] = {"frame", "game", "AI", "stream", "upload", "world",
	                                              "actors", "fx", "post+UI", "swap", "input", "move",
	                                              "look", "pose", "camera"};
	int n = 0;
	if (n < max) {
		const float ms = shown[PERF_FRAME];
		snprintf(lines[n++], 64, "%.1f ms (%.0f fps)  worst %.1f  %dx%d", ms, ms > 0.1f ? 1000.0f / ms : 0.0f,
		         worst_shown_us / 1000.0f, res_w, res_h);
	}
	for (int i = 1; i < PERF_COUNT && n < max - 3; i += 2) // (two to a line)
		snprintf(lines[n++], 64, "%-7s %5.1f   %-7s %5.1f", names[i], shown[i], i + 1 < PERF_COUNT ? names[i + 1] : "",
		         i + 1 < PERF_COUNT ? shown[i + 1] : 0.0f);
	if (n < max) { // (the heap: walked twice a second, not every frame - it costs)
		const unsigned used_mb = (unsigned)(_newlib_heap_size_user >> 20) - perf_heap_free_kb() / 1024;
		snprintf(lines[n++], 64, "heap %u / %u MB used", used_mb, (unsigned)(_newlib_heap_size_user >> 20));
	}
#ifdef __vita__
	if (n < max) { // (Vita3K reports nonsense here - more free than there is: said so, not shown)
		refresh_vram();
		const size_t free_v = vram_free, total_v = vram_total;
		if (free_v <= total_v)
			snprintf(lines[n++], 64, "vram %u of %u MB used", (unsigned)((total_v - free_v) >> 20),
			         (unsigned)(total_v >> 20));
		else
			snprintf(lines[n++], 64, "vram: not reported (emulator)");
	}
#endif
	if (n < max)
		snprintf(lines[n++], 64, "loads queued %d", stream_pending());
	return n;
}
