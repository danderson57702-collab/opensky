// Milestone 1: rotating, per-vertex-lit cube with linear distance fog.
// The fog color doubles as the clear color so geometry fades into the "sky".
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

#define SCREEN_W 960
#define SCREEN_H 544

// Hazy, desaturated SotC-ish horizon tone.
static const GLfloat FOG_COLOR[4] = { 0.62f, 0.66f, 0.70f, 1.0f };

// 6 faces * 4 verts, with per-face normals (flat shading via per-vertex lighting).
static const GLfloat cube_pos[] = {
	-1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1, // +Z
	 1,-1,-1, -1,-1,-1, -1, 1,-1,  1, 1,-1, // -Z
	 1,-1, 1,  1,-1,-1,  1, 1,-1,  1, 1, 1, // +X
	-1,-1,-1, -1,-1, 1, -1, 1, 1, -1, 1,-1, // -X
	-1, 1, 1,  1, 1, 1,  1, 1,-1, -1, 1,-1, // +Y
	-1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1, // -Y
};
static const GLfloat cube_nrm[] = {
	0,0,1, 0,0,1, 0,0,1, 0,0,1,
	0,0,-1, 0,0,-1, 0,0,-1, 0,0,-1,
	1,0,0, 1,0,0, 1,0,0, 1,0,0,
	-1,0,0, -1,0,0, -1,0,0, -1,0,0,
	0,1,0, 0,1,0, 0,1,0, 0,1,0,
	0,-1,0, 0,-1,0, 0,-1,0, 0,-1,0,
};
static const GLushort cube_idx[] = {
	 0, 1, 2,  0, 2, 3,   4, 5, 6,  4, 6, 7,
	 8, 9,10,  8,10,11,  12,13,14, 12,14,15,
	16,17,18, 16,18,19,  20,21,22, 20,22,23,
};

static void setup_state(void) {
	glClearColor(FOG_COLOR[0], FOG_COLOR[1], FOG_COLOR[2], FOG_COLOR[3]);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(60.0, (double)SCREEN_W / SCREEN_H, 0.5, 100.0);
	glMatrixMode(GL_MODELVIEW);

	// Per-vertex lighting: one warm directional "sun" plus cool ambient.
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	const GLfloat ambient[]  = { 0.30f, 0.33f, 0.38f, 1.0f };
	const GLfloat diffuse[]  = { 0.95f, 0.88f, 0.75f, 1.0f };
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
	glEnable(GL_COLOR_MATERIAL);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

	// Linear fog matched to the clear color.
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogfv(GL_FOG_COLOR, FOG_COLOR);
	glFogf(GL_FOG_START, 4.0f);
	glFogf(GL_FOG_END, 22.0f);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, cube_pos);
	glNormalPointer(GL_FLOAT, 0, cube_nrm);
}

static void draw_cube(float x, float z, float angle) {
	glPushMatrix();
	glTranslatef(x, 0.0f, z);
	glRotatef(angle, 0.3f, 1.0f, 0.15f);
	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, cube_idx);
	glPopMatrix();
}

int main(void) {
	vglInitExtended(0, SCREEN_W, SCREEN_H, 8 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
	setup_state();

	SceCtrlData pad;
	float angle = 0.0f;
	for (;;) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_START)
			break;

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glLoadIdentity();

		// Light direction is set in eye space after loading the view.
		const GLfloat sun_dir[] = { -0.4f, 0.8f, 0.5f, 0.0f };
		glLightfv(GL_LIGHT0, GL_POSITION, sun_dir);

		// A receding row of cubes makes the fog falloff easy to judge.
		glColor4f(0.78f, 0.72f, 0.62f, 1.0f);
		for (int i = 0; i < 6; i++)
			draw_cube((i % 2 ? 2.5f : -2.5f), -5.0f - i * 3.5f, angle + i * 25.0f);

		vglSwapBuffers(GL_FALSE);
		angle += 1.5f;
	}

	sceKernelExitProcess(0);
	return 0;
}
