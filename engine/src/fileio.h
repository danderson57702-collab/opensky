#ifndef FILEIO_H
#define FILEIO_H

// Whole-file read, safe from any thread. (VitaSDK's newlib doesn't lock its FILE table,
// so concurrent fopen calls from the render, streaming and music threads could share a
// FILE; the Vita's sceIo calls are thread-safe.) Returns a malloc'd buffer, NULL on
// failure.
void *file_read_all(const char *path, long *size);

// The pack's archives (converter/pack_archives.py: meshes, objects, terrain...) opened: file_read_all
// reads those folders' files from them (the memory card finds a file in a folder of thousands
// slowly); how many were found (0: the loose files)
int fileio_init(const char *pack_dir);

#endif
