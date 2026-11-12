#ifndef SHADER_H
#define SHADER_H

#include <vitaGL.h>

// Compiles and links a CG vertex + fragment program (vitaGL's CG shader types; the
// GLSL types would run the source through vitaGL's GLSL translator and mangle it).
// attribs[i] is bound to attribute location i. Returns 0 (and logs) on failure.
GLuint shader_program(const char *name, const char *vs, const char *fs, const char *const *attribs, int attrib_count);

// Same, with `prefix` prepended to the vertex shader source (e.g. atmosphere_cg()).
GLuint shader_program_prefixed(const char *name, const char *prefix, const char *vs, const char *fs,
                               const char *const *attribs, int attrib_count);

// World -> clip matrix (column-major, for glUniformMatrix4fv) from the current
// fixed-function projection and modelview matrices.
void shader_wvp(float out[16]);

#endif
