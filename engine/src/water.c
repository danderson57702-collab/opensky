#include "water.h"

#include <vitaGL.h>

#include "atmosphere.h"
#include "shader.h"

#include <math.h>

static const float WATER_RGBA[4] = {0.36f, 0.46f, 0.50f, 0.65f};

static const char *WATER_VS = // atmosphere_cg() is prepended
	"uniform float4x4 u_wvp;\n"
	"uniform float4 u_color;\n"
	"uniform float4 u_pos; // world = position * w + xyz (quantized object vertices)\n"
	"void main(float3 position, float4 out vPosition : POSITION, float4 out vFog : TEXCOORD0,\n"
	"	float3 out vLit : TEXCOORD1, float2 out vXZ : TEXCOORD2, float3 out vView : TEXCOORD3,\n"
	"	float3 out vSky : TEXCOORD4, float3 out vSunDir : TEXCOORD5, float3 out vGlow : TEXCOORD6) {\n"
	"	float3 world = position * u_pos.w + u_pos.xyz;\n"
	"	vPosition = mul(float4(world, 1.0f), u_wvp);\n"
	"	vFog = atmo_fog(world);\n"
	"	vLit = atmo_light(u_color.rgb); // dims with the time of day\n"
	"	vXZ = world.xz;\n"
	"	vView = u_cam - world; // (exact when interpolated)\n"
	"	vSky = u_haze * u_lightTint * 1.15f;\n"
	"	vSunDir = u_sunDir;\n"
	"	vGlow = u_sunGlow.rgb;\n"
	"}\n";

// Ripples: a few travelling waves give the surface a moving slope, which shades it
// between the deep colour and the sky (more sky at grazing angles, as real water) and
// throws a sun (or moon) glint. A handful of cosines per water pixel.
static const char *WATER_FS =
	"uniform float4 u_color;\n"
	"uniform float u_time;\n"
	"float4 main(float4 vFog : TEXCOORD0, float3 vLit : TEXCOORD1, float2 vXZ : TEXCOORD2, float3 vView : TEXCOORD3,\n"
	"	float3 vSky : TEXCOORD4, float3 vSunDir : TEXCOORD5, float3 vGlow : TEXCOORD6) {\n"
	"	float2 p = vXZ;\n"
	"	float t = u_time;\n"
	"	float2 g = float2(0.8f, 0.6f) * (cos(dot(p, float2(0.8f, 0.6f)) * 1.7f + t * 1.9f) * 0.10f);\n"
	"	g += float2(-0.5f, 0.86f) * (cos(dot(p, float2(-0.5f, 0.86f)) * 2.9f + t * 2.7f) * 0.07f);\n"
	"	g += float2(0.95f, -0.31f) * (cos(dot(p, float2(0.95f, -0.31f)) * 5.3f + t * 3.6f) * 0.05f);\n"
	"	g += float2(-0.2f, -0.98f) * (cos(dot(p, float2(-0.2f, -0.98f)) * 9.1f + t * 4.4f) * 0.035f);\n"
	"	float3 n = normalize(float3(-g.x, 1.0f, -g.y));\n"
	"	float3 v = normalize(vView);\n"
	"	float k = 1.0f - saturate(dot(n, v));\n"
	"	float fres = 0.06f + 0.94f * k * k * k * k * k;\n"
	"	float3 c = lerp(vLit, vSky, fres);\n"
	"	float3 r = reflect(-v, n);\n"
	"	c += vGlow * (pow(saturate(dot(r, vSunDir)), 90.0f) * 1.4f);\n"
	"	float a = lerp(u_color.a, 0.94f, fres);\n"
	"	return float4(lerp(c, vFog.rgb, vFog.a), a);\n"
	"}\n";

// The plain look, if the rippling shader doesn't compile
static const char *WATER_VS_FLAT = // atmosphere_cg() is prepended
	"uniform float4x4 u_wvp;\n"
	"uniform float4 u_color;\n"
	"uniform float4 u_pos;\n"
	"void main(float3 position, float4 out vPosition : POSITION, float4 out vFog : TEXCOORD0,\n"
	"	float3 out vLit : TEXCOORD1) {\n"
	"	float3 world = position * u_pos.w + u_pos.xyz;\n"
	"	vPosition = mul(float4(world, 1.0f), u_wvp);\n"
	"	vFog = atmo_fog(world);\n"
	"	vLit = atmo_light(u_color.rgb);\n"
	"}\n";
static const char *WATER_FS_FLAT =
	"uniform float4 u_color;\n"
	"float4 main(float4 vFog : TEXCOORD0, float3 vLit : TEXCOORD1) {\n"
	"	return float4(lerp(vLit, vFog.rgb, vFog.a), u_color.a);\n"
	"}\n";

static GLuint progs[ATMO_VARIANTS];
static GLint u_wvps[ATMO_VARIANTS], u_colors[ATMO_VARIANTS], u_poss[ATMO_VARIANTS], u_times[ATMO_VARIANTS];
static float water_time;
static AtmoUniforms atmos[ATMO_VARIANTS];

void water_init(void) {
	static const char *const attribs[] = {"position"};
	for (int i = 0; i < ATMO_VARIANTS; i++) {
		progs[i] = shader_program_prefixed("water", atmosphere_cg(i), WATER_VS, WATER_FS, attribs, 1);
		if (!progs[i])
			progs[i] = shader_program_prefixed("water (flat)", atmosphere_cg(i), WATER_VS_FLAT, WATER_FS_FLAT, attribs, 1);
		if (!progs[i])
			continue;
		u_wvps[i] = glGetUniformLocation(progs[i], "u_wvp");
		u_colors[i] = glGetUniformLocation(progs[i], "u_color");
		u_poss[i] = glGetUniformLocation(progs[i], "u_pos");
		u_times[i] = glGetUniformLocation(progs[i], "u_time");
		atmosphere_locate(progs[i], &atmos[i]);
	}
}

int water_begin(void) {
	int v = atmosphere_variant();
	GLuint prog = progs[v];
	if (!prog)
		return 0;
	float wvp[16];
	shader_wvp(wvp);
	glUseProgram(prog);
	glUniformMatrix4fv(u_wvps[v], 1, GL_FALSE, wvp);
	glUniform4fv(u_colors[v], 1, WATER_RGBA);
	glUniform1f(u_times[v], water_time);
	water_set_transform(0.0f, 0.0f, 0.0f, 1.0f);
	atmosphere_apply(&atmos[v]);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glEnableVertexAttribArray(0);
	return 1;
}

void water_set_time(float seconds) {
	water_time = fmodf(seconds, 600.0f); // (wrapped: float precision in the shader)
}

void water_set_transform(float ox, float oy, float oz, float scale) {
	glUniform4f(u_poss[atmosphere_variant()], ox, oy, oz, scale);
}

void water_end(void) {
	glDisableVertexAttribArray(0);
	glUseProgram(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_CULL_FACE);
}
