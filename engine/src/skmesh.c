#include "skmesh.h"
#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>

#define SKMS_VERSION 1

typedef struct {
	const uint8_t *p, *end;
	int ok;
} Cursor;

// Copies out of the file buffer instead of pointing into it: fields there are
// unaligned, and VFP loads of unaligned floats fault on the Cortex-A9.
static void take(Cursor *c, void *dst, size_t n) {
	if (!c->ok || c->p + n > c->end) {
		c->ok = 0;
		return;
	}
	memcpy(dst, c->p, n);
	c->p += n;
}

int skmesh_load(const char *path, SkmMesh *out) {
	memset(out, 0, sizeof(*out));
	long size = 0;
	uint8_t *buf = file_read_all(path, &size);
	if (!buf) {
		sceClibPrintf("skmesh: cannot open %s\n", path);
		return -1;
	}

	Cursor c = {buf, buf + size, 1};
	char magic[4];
	uint16_t version, count;
	take(&c, magic, 4);
	take(&c, &version, 2);
	take(&c, &count, 2);
	take(&c, out->bmin, 12);
	take(&c, out->bmax, 12);
	if (!c.ok || memcmp(magic, "SKMS", 4) || version != SKMS_VERSION) {
		sceClibPrintf("skmesh: bad header in %s\n", path);
		free(buf);
		return -1;
	}

	out->submeshes = calloc(count, sizeof(SkmSubmesh));
	out->submesh_count = count;
	for (int i = 0; i < count && c.ok; i++) {
		SkmSubmesh *s = &out->submeshes[i];
		uint8_t name_len;
		uint16_t reserved;
		take(&c, &name_len, 1);
		if (name_len >= sizeof(s->texture))
			c.ok = 0;
		take(&c, s->texture, name_len);
		take(&c, &s->flags, 1);
		take(&c, &reserved, 2);
		take(&c, &s->vertex_count, 4);
		take(&c, &s->index_count, 4);
		if (!c.ok)
			break;
		s->vertices = malloc(s->vertex_count * sizeof(SkmVertex));
		s->indices = malloc(s->index_count * sizeof(uint16_t));
		take(&c, s->vertices, s->vertex_count * sizeof(SkmVertex));
		take(&c, s->indices, s->index_count * sizeof(uint16_t));
		c.p += (4 - (s->index_count * 2) % 4) % 4;
		for (uint32_t k = 0; k < s->index_count && c.ok; k++)
			if (s->indices[k] >= s->vertex_count)
				c.ok = 0;
	}
	free(buf);

	if (!c.ok) {
		sceClibPrintf("skmesh: truncated or corrupt %s\n", path);
		skmesh_free(out);
		return -1;
	}
	sceClibPrintf("skmesh: %s %d submeshes\n", path, count);
	return 0;
}

void skmesh_free(SkmMesh *m) {
	for (int i = 0; i < m->submesh_count; i++) {
		free(m->submeshes[i].vertices);
		free(m->submeshes[i].indices);
	}
	free(m->submeshes);
	memset(m, 0, sizeof(*m));
}
