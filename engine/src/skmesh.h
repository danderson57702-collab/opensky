#ifndef SKMESH_H
#define SKMESH_H

#include <stdint.h>

#define SKMESH_ALPHA_TEST 1

typedef struct {
	float pos[3];
	float uv[2];
	int8_t n[3];
	uint8_t pad;
	uint8_t col[4];
} SkmVertex; // 28 bytes, matches converter/meshes.py VERTEX_DTYPE

typedef struct {
	char texture[128];
	uint8_t flags;
	uint32_t vertex_count, index_count;
	SkmVertex *vertices;
	uint16_t *indices;
} SkmSubmesh;

typedef struct {
	float bmin[3], bmax[3];
	int submesh_count;
	SkmSubmesh *submeshes;
} SkmMesh;

// Loads a converter-produced .skmesh. Returns 0 on success.
int skmesh_load(const char *path, SkmMesh *out);
void skmesh_free(SkmMesh *m);

#endif
