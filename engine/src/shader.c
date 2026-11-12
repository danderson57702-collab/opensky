#include "shader.h"

#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>

static GLuint compile(const char *name, GLenum type, const char *src) {
	GLuint sh = glCreateShader(type);
	GLint len = (GLint)strlen(src);
	glShaderSource(sh, 1, &src, &len);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = "";
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		sceClibPrintf("shader %s: %s shader failed to compile:\n%s\n", name,
		              type == GL_CG_VERTEX_SHADER_EXT ? "vertex" : "fragment", log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

GLuint shader_program(const char *name, const char *vs_src, const char *fs_src, const char *const *attribs, int attrib_count) {
	GLuint vs = compile(name, GL_CG_VERTEX_SHADER_EXT, vs_src);
	GLuint fs = compile(name, GL_CG_FRAGMENT_SHADER_EXT, fs_src);
	if (!vs || !fs)
		return 0;
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	for (int i = 0; i < attrib_count; i++)
		glBindAttribLocation(prog, i, attribs[i]);
	glLinkProgram(prog);
	sceClibPrintf("shader %s: ready (prog %u)\n", name, prog);
	return prog;
}

void shader_wvp(float out[16]) {
	GLfloat proj[16], view[16];
	glGetFloatv(GL_PROJECTION_MATRIX, proj);
	glGetFloatv(GL_MODELVIEW_MATRIX, view);
	for (int col = 0; col < 4; col++)
		for (int row = 0; row < 4; row++) {
			float v = 0.0f;
			for (int k = 0; k < 4; k++)
				v += proj[k * 4 + row] * view[col * 4 + k];
			out[col * 4 + row] = v;
		}
}

GLuint shader_program_prefixed(const char *name, const char *prefix, const char *vs, const char *fs,
                               const char *const *attribs, int attrib_count) {
	size_t a = strlen(prefix), b = strlen(vs);
	char *src = malloc(a + b + 1);
	if (!src)
		return 0;
	memcpy(src, prefix, a);
	memcpy(src + a, vs, b + 1);
	GLuint prog = shader_program(name, src, fs, attribs, attrib_count);
	free(src);
	return prog;
}
