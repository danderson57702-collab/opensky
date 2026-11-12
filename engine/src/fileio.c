#include "fileio.h"

#include <stdlib.h>

#ifdef __vita__
#include <stdio.h>
#include <string.h>
#include <psp2/io/fcntl.h>

// The pack's many-file folders as archives (converter/pack_archives.py): each opened once,
// its index kept, a file read from it by offset. The memory card's file system finds a file
// by scanning its folder, which in a folder of thousands takes much of a second each time.
typedef struct {
	char prefix[128]; // "ux0:data/opensky/meshes/"
	int prefix_len;
	SceUID fd;
	int count;
	char *names;      // the index's names, each zero-ended
	const char **name;
	unsigned *offset, *size;
} Archive;
#define MAX_ARCHIVES 12
static Archive archives[MAX_ARCHIVES];
static int archive_count;

static void open_archive(const char *pack_dir, const char *folder) {
	char path[160];
	snprintf(path, sizeof(path), "%s%s.skpak", pack_dir, folder);
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return;
	unsigned head[4];
	if (sceIoRead(fd, head, sizeof(head)) != sizeof(head) || memcmp(head, "SKPK", 4) || head[1] != 1) {
		sceIoClose(fd);
		return;
	}
	const int count = (int)head[2];
	const unsigned index_bytes = head[3];
	unsigned char *index = malloc(index_bytes ? index_bytes : 1);
	Archive *a = &archives[archive_count];
	memset(a, 0, sizeof(*a));
	a->names = malloc(index_bytes + count + 1);
	a->name = malloc(count * sizeof(char *) + 1);
	a->offset = malloc(count * sizeof(unsigned) + 1), a->size = malloc(count * sizeof(unsigned) + 1);
	if (!index || !a->names || !a->name || !a->offset || !a->size || sceIoRead(fd, index, index_bytes) != (int)index_bytes) {
		free(index), free(a->names), free(a->name), free(a->offset), free(a->size);
		sceIoClose(fd);
		return;
	}
	unsigned at = 0, out = 0;
	for (int i = 0; i < count && at + 10 <= index_bytes; i++) {
		unsigned short len;
		memcpy(&a->offset[i], index + at, 4), memcpy(&a->size[i], index + at + 4, 4), memcpy(&len, index + at + 8, 2);
		at += 10;
		memcpy(a->names + out, index + at, len);
		a->names[out + len] = 0;
		a->name[i] = a->names + out;
		out += len + 1, at += len;
	}
	free(index);
	snprintf(a->prefix, sizeof(a->prefix), "%s%s/", pack_dir, folder);
	a->prefix_len = (int)strlen(a->prefix);
	a->fd = fd, a->count = count;
	archive_count++;
}

int fileio_init(const char *pack_dir) {
	static const char *const folders[] = {"meshes", "objects", "terrain", "textures", "navmesh", "actors", "interiors",
	                                      "creatures"};
	for (unsigned i = 0; i < sizeof(folders) / sizeof(folders[0]) && archive_count < MAX_ARCHIVES; i++)
		open_archive(pack_dir, folders[i]);
	return archive_count;
}

// A file from an archive (NULL: not in one). *archived: its folder has an archive - the
// archive is the whole folder then: a file not in it doesn't exist (asking the card for the
// loose file would scan the folder's thousands of names only to find nothing)
static void *archive_read(const char *path, long *size, int *archived) {
	*archived = 0;
	for (int k = 0; k < archive_count; k++) {
		const Archive *a = &archives[k];
		if (strncmp(path, a->prefix, a->prefix_len))
			continue;
		*archived = 1;
		const char *name = path + a->prefix_len;
		int lo = 0, hi = a->count - 1;
		while (lo <= hi) {
			const int mid = (lo + hi) / 2, c = strcmp(name, a->name[mid]);
			if (c == 0) {
				void *buf = malloc(a->size[mid] ? a->size[mid] : 1);
				if (!buf)
					return NULL;
				unsigned got = 0;
				while (got < a->size[mid]) { // (by position: any thread, the one handle)
					const int n = sceIoPread(a->fd, (char *)buf + got, a->size[mid] - got, (SceOff)a->offset[mid] + got);
					if (n <= 0) {
						free(buf);
						return NULL;
					}
					got += (unsigned)n;
				}
				*size = (long)a->size[mid];
				return buf;
			}
			if (c < 0)
				hi = mid - 1;
			else
				lo = mid + 1;
		}
		return NULL; // (not in it: no such file)
	}
	return NULL;
}

void *file_read_all(const char *path, long *size) {
	int archived;
	void *packed = archive_read(path, size, &archived);
	if (packed || archived)
		return packed;
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0)
		return NULL;
	SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
	sceIoLseek(fd, 0, SCE_SEEK_SET);
	void *buf = end >= 0 ? malloc(end > 0 ? (size_t)end : 1) : NULL;
	long got = 0;
	while (buf && got < (long)end) {
		int n = sceIoRead(fd, (char *)buf + got, (SceSize)(end - got));
		if (n <= 0)
			break;
		got += n;
	}
	sceIoClose(fd);
	if (buf && got != (long)end) {
		free(buf);
		return NULL;
	}
	*size = (long)end;
	return buf;
}
#else
#include <stdio.h>

int fileio_init(const char *pack_dir) { // (the host reads the loose files)
	(void)pack_dir;
	return 0;
}

void *file_read_all(const char *path, long *size) {
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *buf = malloc(*size > 0 ? *size : 1);
	if (buf && fread(buf, 1, *size, f) != (size_t)*size) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}
#endif
