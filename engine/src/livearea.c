// livearea.c: the LiveArea's background (the page shown when the bubble is tapped) taken from the game's own view.
//
// The VPK carries only OpenSKY's own plain background (sce_sys/livearea/contents/bg.png): no game picture may
// be in it. Options' "Use this view for the LiveArea" turns the scene on screen into an 840x500 picture with
// 256 colours (what the LiveArea takes) and writes it three places:
//   ux0:data/opensky_livearea.png             - kept: put back after every reinstall (livearea_restore)
//   ux0:app/<title>/sce_sys/livearea/...      - the app's own copy (VitaShell's "Refresh LiveArea" reads it)
//   ur0:appmeta/<title>/livearea/...          - the copy the home screen shows
#include "livearea.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vitaGL.h>

#include "gamelog.h"

#ifndef TITLE_ID
#define TITLE_ID "OSKY00001"
#endif
#define KEEP_PATH "ux0:data/opensky_livearea.png"
#define APP_DIR "ux0:app/" TITLE_ID "/sce_sys/livearea/contents"
#define META_DIR "ur0:appmeta/" TITLE_ID "/livearea/contents"
#define BG_W 840
#define BG_H 500
#define COLOURS 256

// --- 256 colours: median cut over a 5-5-5 count of the picture's colours ---
typedef struct {
	int lo[3], hi[3]; // (inclusive, 0..31 each)
	unsigned count;
} Box;

static unsigned *hist; // [32768]

static void box_shrink(Box *b) {
	int lo[3] = {31, 31, 31}, hi[3] = {0, 0, 0};
	unsigned count = 0;
	for (int r = b->lo[0]; r <= b->hi[0]; r++)
		for (int g = b->lo[1]; g <= b->hi[1]; g++)
			for (int bl = b->lo[2]; bl <= b->hi[2]; bl++) {
				const unsigned n = hist[r << 10 | g << 5 | bl];
				if (!n)
					continue;
				count += n;
				const int c[3] = {r, g, bl};
				for (int k = 0; k < 3; k++) {
					if (c[k] < lo[k])
						lo[k] = c[k];
					if (c[k] > hi[k])
						hi[k] = c[k];
				}
			}
	b->count = count;
	if (count)
		memcpy(b->lo, lo, sizeof(lo)), memcpy(b->hi, hi, sizeof(hi));
}

static int median_cut(uint8_t palette[COLOURS][3]) {
	static Box boxes[COLOURS];
	int n = 1;
	boxes[0] = (Box){{0, 0, 0}, {31, 31, 31}, 0};
	box_shrink(&boxes[0]);
	while (n < COLOURS) {
		int best = -1;
		unsigned best_score = 0;
		for (int i = 0; i < n; i++) { // (the box to split: many pixels over a wide range)
			int span = 0;
			for (int k = 0; k < 3; k++)
				if (boxes[i].hi[k] - boxes[i].lo[k] > span)
					span = boxes[i].hi[k] - boxes[i].lo[k];
			const unsigned score = span ? boxes[i].count * (unsigned)span : 0;
			if (span && boxes[i].count && score >= best_score)
				best = i, best_score = score;
		}
		if (best < 0)
			break;
		Box *b = &boxes[best];
		int axis = 0;
		for (int k = 1; k < 3; k++)
			if (b->hi[k] - b->lo[k] > b->hi[axis] - b->lo[axis])
				axis = k;
		unsigned half = 0; // (at the median along the longest side)
		int cut = b->lo[axis];
		for (int v = b->lo[axis]; v < b->hi[axis]; v++) {
			Box slab = *b;
			slab.lo[axis] = slab.hi[axis] = v;
			unsigned in = 0;
			for (int r = slab.lo[0]; r <= slab.hi[0]; r++)
				for (int g = slab.lo[1]; g <= slab.hi[1]; g++)
					for (int bl = slab.lo[2]; bl <= slab.hi[2]; bl++)
						in += hist[r << 10 | g << 5 | bl];
			half += in;
			cut = v;
			if (half * 2 >= b->count)
				break;
		}
		Box other = *b;
		b->hi[axis] = cut, other.lo[axis] = cut + 1;
		box_shrink(b), box_shrink(&other);
		boxes[n++] = other;
	}
	for (int i = 0; i < n; i++) { // (each colour: the boxed pixels' average)
		double sum[3] = {0, 0, 0}, w = 0;
		for (int r = boxes[i].lo[0]; r <= boxes[i].hi[0]; r++)
			for (int g = boxes[i].lo[1]; g <= boxes[i].hi[1]; g++)
				for (int bl = boxes[i].lo[2]; bl <= boxes[i].hi[2]; bl++) {
					const unsigned c = hist[r << 10 | g << 5 | bl];
					sum[0] += c * (r * 8 + 4), sum[1] += c * (g * 8 + 4), sum[2] += c * (bl * 8 + 4), w += c;
				}
		for (int k = 0; k < 3; k++)
			palette[i][k] = (uint8_t)(w > 0 ? sum[k] / w : 0);
	}
	for (int i = n; i < COLOURS; i++)
		palette[i][0] = palette[i][1] = palette[i][2] = 0;
	return n;
}

// --- PNG: indexed, its pixels in stored (uncompressed) deflate blocks: no zlib on the Vita's SDK ---
static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t v = i;
			for (int k = 0; k < 8; k++)
				v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
			crc_table[i] = v;
		}
	c = ~c;
	while (n--)
		c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return ~c;
}

static void put32(uint8_t *p, uint32_t v) {
	p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v;
}

static int chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
	uint8_t head[8];
	put32(head, n);
	memcpy(head + 4, type, 4);
	uint32_t c = crc(0, head + 4, 4);
	c = crc(c, data, n);
	uint8_t tail[4];
	put32(tail, c);
	return fwrite(head, 1, 8, f) == 8 && (!n || fwrite(data, 1, n, f) == n) && fwrite(tail, 1, 4, f) == 4;
}

static int write_png(const char *path, const uint8_t *index, const uint8_t palette[COLOURS][3]) {
	const size_t raw_n = (size_t)(BG_W + 1) * BG_H; // (each row: filter 0, then its pixels)
	const size_t blocks = (raw_n + 65534) / 65535;
	const size_t z_n = 2 + raw_n + blocks * 5 + 4;
	uint8_t *z = malloc(z_n);
	if (!z)
		return 0;
	size_t o = 0;
	z[o++] = 0x78, z[o++] = 0x01;
	uint32_t a = 1, b = 0; // (Adler-32 of the raw rows)
	size_t done = 0;
	int x = -1, y = 0; // (x -1: the row's filter byte)
	while (done < raw_n) {
		const size_t n = raw_n - done < 65535 ? raw_n - done : 65535;
		z[o++] = done + n == raw_n;
		z[o++] = n & 0xFF, z[o++] = n >> 8, z[o++] = ~n & 0xFF, z[o++] = (~n >> 8) & 0xFF;
		for (size_t i = 0; i < n; i++) {
			const uint8_t v = x < 0 ? 0 : index[y * BG_W + x];
			z[o++] = v;
			a = (a + v) % 65521, b = (b + a) % 65521;
			if (++x == BG_W)
				x = -1, y++;
		}
		done += n;
	}
	put32(z + o, b << 16 | a), o += 4;

	FILE *f = fopen(path, "wb");
	if (!f) {
		free(z);
		return 0;
	}
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	uint8_t ihdr[13];
	put32(ihdr, BG_W), put32(ihdr + 4, BG_H);
	ihdr[8] = 8, ihdr[9] = 3, ihdr[10] = ihdr[11] = ihdr[12] = 0; // (8-bit, a palette)
	int ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) &&
	         chunk(f, "PLTE", &palette[0][0], COLOURS * 3) && chunk(f, "IDAT", z, (uint32_t)o) && chunk(f, "IEND", NULL, 0);
	ok = fclose(f) == 0 && ok;
	free(z);
	return ok;
}

static int copy_file(const char *from, const char *to) {
	FILE *in = fopen(from, "rb");
	if (!in)
		return 0;
	FILE *out = fopen(to, "wb");
	if (!out) {
		fclose(in);
		return 0;
	}
	char buf[16384];
	size_t n;
	int ok = 1;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		ok = ok && fwrite(buf, 1, n, out) == n;
	fclose(in);
	return fclose(out) == 0 && ok;
}

static long file_size(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

// The kept picture into the app's two copies (the folders exist once the VPK with a LiveArea is installed)
static int install(void) {
	const int app = copy_file(KEEP_PATH, APP_DIR "/bg.png");
	const int meta = copy_file(KEEP_PATH, META_DIR "/bg.png");
	game_log("livearea: picture into the app %s, the home screen %s", app ? "done" : "FAILED", meta ? "done" : "FAILED");
	return app || meta;
}

int livearea_save(void) {
	const int sw = 960, sh = 544;
	uint8_t *px = malloc(sw * sh * 4);
	float *rgb = malloc(sizeof(float) * 3 * BG_W * BG_H);
	uint8_t *index = malloc(BG_W * BG_H);
	hist = calloc(32768, sizeof(unsigned));
	int ok = 0;
	if (px && rgb && index && hist) {
		glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, px); // (bottom row first)
		// 544 rows to 500, and the middle 914 of 960 columns to 840 (the LiveArea is a little narrower), bilinear
		const float s = (float)sh / BG_H, x0 = (sw - BG_W * s) * 0.5f;
		for (int y = 0; y < BG_H; y++)
			for (int x = 0; x < BG_W; x++) {
				float fx = x0 + (x + 0.5f) * s - 0.5f, fy = (y + 0.5f) * s - 0.5f;
				if (fx < 0)
					fx = 0;
				if (fy < 0)
					fy = 0;
				int ix = (int)fx, iy = (int)fy;
				if (ix > sw - 2)
					ix = sw - 2;
				if (iy > sh - 2)
					iy = sh - 2;
				const float tx = fx - ix, ty = fy - iy;
				for (int k = 0; k < 3; k++) {
					const uint8_t *r0 = &px[((sh - 1 - iy) * sw + ix) * 4 + k], *r1 = &px[((sh - 2 - iy) * sw + ix) * 4 + k];
					const float top = r0[0] + (r0[4] - r0[0]) * tx, bottom = r1[0] + (r1[4] - r1[0]) * tx;
					rgb[(y * BG_W + x) * 3 + k] = top + (bottom - top) * ty;
				}
				const float *c = &rgb[(y * BG_W + x) * 3];
				hist[((int)c[0] >> 3) << 10 | ((int)c[1] >> 3) << 5 | ((int)c[2] >> 3)]++;
			}
		uint8_t palette[COLOURS][3];
		const int colours = median_cut(palette);
		// Each pixel to its nearest colour, the difference passed on (Floyd-Steinberg): no bands in the sky
		static int16_t nearest[32768];
		memset(nearest, 0xFF, sizeof(nearest));
		for (int y = 0; y < BG_H; y++)
			for (int x = 0; x < BG_W; x++) {
				float *c = &rgb[(y * BG_W + x) * 3];
				int q[3];
				for (int k = 0; k < 3; k++)
					q[k] = c[k] < 0 ? 0 : c[k] > 255 ? 255 : (int)c[k];
				const int key = (q[0] >> 3) << 10 | (q[1] >> 3) << 5 | (q[2] >> 3);
				if (nearest[key] < 0) {
					int best = 0, best_d = 1 << 30;
					for (int i = 0; i < colours; i++) {
						const int dr = palette[i][0] - ((q[0] & ~7) + 4), dg = palette[i][1] - ((q[1] & ~7) + 4),
						          db = palette[i][2] - ((q[2] & ~7) + 4);
						const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
						if (d < best_d)
							best = i, best_d = d;
					}
					nearest[key] = (int16_t)best;
				}
				const int i = nearest[key];
				index[y * BG_W + x] = (uint8_t)i;
				for (int k = 0; k < 3; k++) {
					const float e = c[k] - palette[i][k];
					if (x + 1 < BG_W)
						c[3 + k] += e * 7 / 16;
					if (y + 1 < BG_H) {
						float *below = &rgb[((y + 1) * BG_W + x) * 3 + k];
						if (x > 0)
							below[-3] += e * 3 / 16;
						below[0] += e * 5 / 16;
						if (x + 1 < BG_W)
							below[3] += e * 1 / 16;
					}
				}
			}
		ok = write_png(KEEP_PATH, index, palette);
		game_log("livearea: this view kept as %s (%d colours) %s", KEEP_PATH, colours, ok ? "" : "- FAILED to write");
		if (ok)
			install();
	}
	free(px), free(rgb), free(index), free(hist);
	hist = NULL;
	return ok;
}

void livearea_restore(void) {
	const long kept = file_size(KEEP_PATH);
	if (kept <= 0)
		return;
	if (file_size(APP_DIR "/bg.png") == kept && file_size(META_DIR "/bg.png") == kept)
		return;
	game_log("livearea: the kept picture put back (the app was installed again)");
	install();
}
