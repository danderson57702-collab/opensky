#include "sktex.h"
#include "fileio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>

#define SKTX_VERSION 1
#define SKTX_DXT1 1
#define SKTX_DXT5 5
#define MAX_MIPS 16

typedef struct {
	char magic[4];
	uint16_t version, format;
	uint16_t width, height, mip_count, reserved;
} __attribute__((packed)) SktexHeader;

// --- Integrity tracking (debug) ------------------------------------------------
// Checksums every uploaded texture's GPU memory: slow (reads uncached GPU memory byte
// by byte), so only built when hunting texture corruption.
#ifndef SKTEX_DEBUG_INTEGRITY
#define SKTEX_DEBUG_INTEGRITY 0
#endif

#define MAX_TRACKED 2048

typedef struct {
	GLuint tex;
	uint32_t size, sum;
	char name[48];
} Tracked;

static Tracked tracked[MAX_TRACKED];
static int tracked_count;

static uint32_t checksum(const uint8_t *p, uint32_t n) {
	uint32_t h = 2166136261u;
	for (uint32_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static void track(GLuint tex, uint32_t size, const char *path) {
	if (!SKTEX_DEBUG_INTEGRITY || tracked_count == MAX_TRACKED)
		return;
	const uint8_t *data = vglGetTexDataPointer(GL_TEXTURE_2D); // bound texture
	if (!data)
		return;
	Tracked *t = &tracked[tracked_count++];
	t->tex = tex;
	t->size = size;
	t->sum = checksum(data, size);
	const char *slash = strrchr(path, '/');
	snprintf(t->name, sizeof(t->name), "%s", slash ? slash + 1 : path);
}

void sktex_forget(GLuint tex) {
	if (!SKTEX_DEBUG_INTEGRITY)
		return;
	for (int i = 0; i < tracked_count; i++)
		if (tracked[i].tex == tex) {
			tracked[i] = tracked[--tracked_count];
			return;
		}
}

int sktex_verify_all(const char *when) {
	GLint prev;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
	int bad = 0;
	for (int i = 0; i < tracked_count; i++) {
		glBindTexture(GL_TEXTURE_2D, tracked[i].tex);
		const uint8_t *data = vglGetTexDataPointer(GL_TEXTURE_2D);
		if (!data || checksum(data, tracked[i].size) != tracked[i].sum) {
			if (bad < 8)
				sceClibPrintf("sktex verify [%s]: CORRUPT %s (tex %u, data %p)\n", when, tracked[i].name,
				              tracked[i].tex, data);
			bad++;
		}
	}
	glBindTexture(GL_TEXTURE_2D, prev);
	sceClibPrintf("sktex verify [%s]: %d textures checked, %d corrupt\n", when, tracked_count, bad);
	return bad;
}

void *sktex_read(const char *path, long *size) {
	return file_read_all(path, size);
}

GLuint sktex_load(const char *path) {
	return sktex_load_lod(path, 0);
}

GLuint sktex_load_lod(const char *path, int max_dim) {
	long size;
	void *buf = sktex_read(path, &size);
	if (!buf) {
		sceClibPrintf("sktex: cannot open %s\n", path);
		return 0;
	}
	GLuint tex = sktex_upload(buf, size, max_dim, path);
	free(buf);
	return tex;
}

GLuint sktex_upload(const void *data, long size, int max_dim, const char *path) {
	const uint8_t *buf = data;
	const SktexHeader *h = (const SktexHeader *)buf;
	if (size < (long)sizeof(*h) || memcmp(h->magic, "SKTX", 4) || h->version != SKTX_VERSION ||
	    (h->format != SKTX_DXT1 && h->format != SKTX_DXT5)) {
		sceClibPrintf("sktex: bad header in %s\n", path);
		return 0;
	}
	GLenum gl_format = h->format == SKTX_DXT1 ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT
	                                          : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);

	// Validate and index every mip level first
	struct { const uint8_t *data; uint32_t size; int w, h; } mips[MAX_MIPS];
	const uint8_t *p = buf + sizeof(*h);
	const uint8_t *end = buf + size;
	int w = h->width, ht = h->height;
	int level;
	for (level = 0; level < h->mip_count && level < MAX_MIPS; level++) {
		uint32_t mip_size;
		if (p + 4 > end)
			break;
		memcpy(&mip_size, p, 4);
		p += 4;
		if (p + mip_size > end)
			break;
		mips[level].data = p;
		mips[level].size = mip_size;
		mips[level].w = w;
		mips[level].h = ht;
		p += mip_size;
		w = w > 1 ? w / 2 : 1;
		ht = ht > 1 ? ht / 2 : 1;
	}

	// Our patched vitaGL allocates the full mip chain with level 0 and swizzles on the
	// CPU (patches/vitagl.patch), so levels are written in place, in order.
	// Waiting on transfers keeps this safe even if an upload ever takes the GPU path.
	int first = 0;
	while (max_dim > 0 && first < level - 1 && (mips[first].w > max_dim || mips[first].h > max_dim))
		first++;
	for (int l = first; l < level; l++) {
		glCompressedTexImage2D(GL_TEXTURE_2D, l - first, gl_format, mips[l].w, mips[l].h, 0, mips[l].size,
		                       mips[l].data);
		sceGxmTransferFinish();
	}
	uint32_t total = 0;
	for (int k = first; k < level; k++)
		total += mips[k].size;
	level -= first;
	track(tex, total, path);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, level > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	return tex;
}

GLuint sktex_white(void) {
	static const uint8_t white[4] = {255, 255, 255, 255};
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	return tex;
}
