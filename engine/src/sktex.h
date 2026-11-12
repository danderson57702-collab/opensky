#ifndef SKTEX_H
#define SKTEX_H

#include <vitaGL.h>

// Loads a converter-produced .sktex (DXT1/DXT5 + mip chain) into a GL texture.
// Returns 0 on failure.
GLuint sktex_load(const char *path);
// Same, but skips leading mip levels larger than max_dim pixels (0 = no limit), e.g. to
// give distant terrain cells a 32 px copy of their color map.
GLuint sktex_load_lod(const char *path, int max_dim);

// Split loading for background streaming: sktex_read (any thread) returns the file's
// bytes (malloc'd, NULL on failure); sktex_upload (render thread) makes the texture.
void *sktex_read(const char *path, long *size);
GLuint sktex_upload(const void *data, long size, int max_dim, const char *path);

// 1x1 white texture, used when a pack file is missing.
GLuint sktex_white(void);

// Debug integrity check: every texture loaded through sktex_load() has a checksum
// of its GPU memory recorded after upload; this re-checks them all and logs any
// whose memory changed since. Returns the number of corrupted textures.
int sktex_verify_all(const char *when);
// Stop tracking a texture before deleting it.
void sktex_forget(GLuint tex);

#endif
