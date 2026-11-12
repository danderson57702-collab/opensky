#include "atmosphere.h"

#include <math.h>
#include <string.h>
#include <psp2/kernel/clib.h>

#include "lighting.h"
#include "shader.h"

// --- Look (tweak here) ------------------------------------------------------------------
#define FOG_DENSITY 0.0032f   // per metre at the base height
#define FOG_SCALE_HEIGHT 70.0f // density falls by e every this many metres up
#define FOG_BASE_Y -80.0f     // valley floors around Whiterun sit at -120..-70
#define EDGE_FADE_M 90.0f     // fade to haze over the last metres before the streamed edge
// Colours come from the time of day (lighting.c)

static const char *SKY_VS =
	"void main(float2 position, float3 dir,\n"
	"	float4 out vPosition : POSITION, float3 out vDir : TEXCOORD0) {\n"
	"	vPosition = float4(position, 1.0f, 1.0f);\n"
	"	vDir = dir;\n"
	"}\n";

static const char *SKY_FS =
	ATMO_CG
	"uniform float3 u_zenith;\n"
	"uniform float4 u_sun;   // sun direction, disc visibility\n"
	"uniform float4 u_moon;  // moon direction, disc visibility\n"
	"uniform float u_stars;  // star visibility\n"
	"float4 main(float3 vDir : TEXCOORD0) {\n"
	"	float3 dir = normalize(vDir);\n"
	"	float3 c = atmo_fog_color(dir); // at and below the horizon: exactly the fog\n"
	"	float t = sqrt(saturate(dir.y / 0.65f));\n"
	"	c = lerp(c, u_zenith, t * t * (3.0f - 2.0f * t));\n"
	"	float s = dot(dir, u_sun.xyz);\n"
	"	// Halo blends toward the glow colour (never past it) so the sky around a low sun\n"
	"	// can't clip to a flat blob in the 8-bit scene buffer; only the disc is allowed to\n"
	"	c = lerp(c, u_sunGlow.rgb * 0.95f, saturate(pow(saturate(s), 48.0f) * 0.45f * u_sun.w));\n"
	"	c = min(c, float3(0.95f, 0.95f, 0.95f));\n"
	"	c += u_sunGlow.rgb * u_sun.w * smoothstep(0.9990f, 0.9997f, s) * 1.6f;\n"
	"	float m = dot(dir, u_moon.xyz);\n"
	"	c += float3(0.80f, 0.84f, 0.92f) * u_moon.w * smoothstep(0.9992f, 0.9997f, m);\n"
	"	// Stars: sparse cells of a direction grid, fading toward the hazy horizon\n"
	"	float3 cell = floor(dir * 180.0f);\n"
	"	float h = frac(sin(dot(cell, float3(12.9898f, 78.233f, 37.719f))) * 43758.5453f);\n"
	"	c += saturate((h - 0.996f) * 250.0f) * u_stars * saturate(dir.y * 4.0f) * 0.8f;\n"
	"	return float4(c, 1.0f);\n"
	"}\n";

static float cam[3], edge_end;
static float fog_mult = 1.0f; // the weather's (atmosphere_set_weather_fog)
static GLuint sky_prog;
static AtmoUniforms sky_u;
static GLint sky_zenith, sky_sun, sky_moon, sky_stars;

const char *atmosphere_cg(int v) {
	(void)v;
	return ATMO_CG;
}

int atmosphere_variant(void) {
	return 0;
}

void atmosphere_set_edge(float edge_end_m) {
	edge_end = edge_end_m;
}

void atmosphere_init(float edge_end_m) {
	edge_end = edge_end_m;
	static const char *const attribs[] = {"position", "dir"};
	sky_prog = shader_program("sky", SKY_VS, SKY_FS, attribs, 2);
	if (sky_prog) {
		atmosphere_locate(sky_prog, &sky_u);
		sky_zenith = glGetUniformLocation(sky_prog, "u_zenith");
		sky_sun = glGetUniformLocation(sky_prog, "u_sun");
		sky_moon = glGetUniformLocation(sky_prog, "u_moon");
		sky_stars = glGetUniformLocation(sky_prog, "u_stars");
	}
}

void atmosphere_set_camera(float x, float y, float z) {
	cam[0] = x, cam[1] = y, cam[2] = z;
}

void atmosphere_locate(GLuint prog, AtmoUniforms *u) {
	u->cam = glGetUniformLocation(prog, "u_cam");
	u->fog = glGetUniformLocation(prog, "u_fog");
	u->edge = glGetUniformLocation(prog, "u_edge");
	u->haze = glGetUniformLocation(prog, "u_haze");
	u->sun_dir = glGetUniformLocation(prog, "u_sunDir");
	u->sun_glow = glGetUniformLocation(prog, "u_sunGlow");
	u->light_tint = glGetUniformLocation(prog, "u_lightTint");
	u->torch = glGetUniformLocation(prog, "u_torch");
	u->torch_color = glGetUniformLocation(prog, "u_torchColor");
}

static float torch_pos[3], torch_inv_reach, torch_rgb[3];

void atmosphere_set_torch(const float pos[3], float reach_m, const float rgb[3]) {
	if (!pos || reach_m <= 0.0f) {
		torch_inv_reach = 0.0f;
		torch_rgb[0] = torch_rgb[1] = torch_rgb[2] = 0.0f;
		return;
	}
	memcpy(torch_pos, pos, sizeof(torch_pos));
	torch_inv_reach = 1.0f / reach_m;
	memcpy(torch_rgb, rgb, sizeof(torch_rgb));
}

static void apply_torch(const AtmoUniforms *u) {
	glUniform4f(u->torch, torch_pos[0], torch_pos[1], torch_pos[2], torch_inv_reach);
	glUniform3fv(u->torch_color, 1, torch_rgb);
}

static int indoors;
static float in_fog[3], in_near, in_far;

void atmosphere_set_interior(const float *fog_rgb, float near_m, float far_m) {
	indoors = fog_rgb != NULL;
	if (indoors) {
		memcpy(in_fog, fog_rgb, sizeof(in_fog));
		in_near = near_m, in_far = far_m > near_m + 1.0f ? far_m : near_m + 1.0f;
	}
}

static int underwater;

// Murky green-blue, dimming with the daylight; light filtered toward it
static const float WATER_FOG[3] = {0.09f, 0.20f, 0.22f}, WATER_FILTER[3] = {0.55f, 0.78f, 0.82f};
#define WATER_FOG_FAR_M 16.0f

void atmosphere_set_underwater(int on) {
	underwater = on;
}

int atmosphere_underwater_fog(float out[3]) {
	if (!underwater)
		return 0;
	const SkyState *sky = lighting_sky();
	float day = (sky->tint[0] + sky->tint[1] + sky->tint[2]) / 3.0f;
	for (int k = 0; k < 3; k++)
		out[k] = WATER_FOG[k] * (0.25f + 0.75f * fminf(day, 1.0f));
	return 1;
}

static int clear_air; // atmosphere_set_clear

void atmosphere_set_clear(int on) {
	clear_air = on;
}

void atmosphere_apply(const AtmoUniforms *u) {
	const SkyState *sky = lighting_sky();
	apply_torch(u);
	if (clear_air) { // (no fog at all: menus' 3D scenes)
		glUniform3f(u->cam, cam[0], cam[1], cam[2]);
		glUniform4f(u->fog, 0.0f, 0.0f, 0.0f, 0.0f);
		glUniform2f(u->edge, 1e6f, 2e6f);
		glUniform3f(u->haze, 0.0f, 0.0f, 0.0f);
		glUniform3f(u->sun_dir, 0.0f, 1.0f, 0.0f);
		glUniform4f(u->sun_glow, 0.0f, 0.0f, 0.0f, 1.0f);
		glUniform3f(u->light_tint, 1.0f, 1.0f, 1.0f);
		return;
	}
	float water_fog[3];
	if (atmosphere_underwater_fog(water_fog)) {
		glUniform3f(u->cam, cam[0], cam[1], cam[2]);
		glUniform4f(u->fog, 1.6f / WATER_FOG_FAR_M, 0.0f, 0.0f, 0.0f);
		glUniform2f(u->edge, WATER_FOG_FAR_M * 0.7f, WATER_FOG_FAR_M);
		glUniform3fv(u->haze, 1, water_fog);
		glUniform3f(u->sun_dir, 0.0f, 1.0f, 0.0f);
		glUniform4f(u->sun_glow, 0.0f, 0.0f, 0.0f, 1.0f);
		glUniform3f(u->light_tint, sky->tint[0] * WATER_FILTER[0], sky->tint[1] * WATER_FILTER[1],
		            sky->tint[2] * WATER_FILTER[2]);
		return;
	}
	if (indoors) {
		// Uniform density (no height falloff) that is ~60% at the far distance, then the
		// edge fade finishes it
		glUniform3f(u->cam, cam[0], cam[1], cam[2]);
		glUniform4f(u->fog, 0.9f / in_far, 0.0f, 0.0f, 0.0f);
		glUniform2f(u->edge, in_near + (in_far - in_near) * 0.6f, in_far);
		glUniform3fv(u->haze, 1, in_fog);
		glUniform3f(u->sun_dir, 0.0f, 1.0f, 0.0f);
		glUniform4f(u->sun_glow, 0.0f, 0.0f, 0.0f, 1.0f);
		glUniform3f(u->light_tint, 1.0f, 1.0f, 1.0f);
		return;
	}
	glUniform3f(u->cam, cam[0], cam[1], cam[2]);
	glUniform4f(u->fog, FOG_DENSITY * fog_mult, 1.0f / FOG_SCALE_HEIGHT, FOG_BASE_Y, 0.0f);
	// (the fade's last stretch only: with a short view (Ultra Low, 120 m) a 90 m fade began 30 m
	// out and left everything half sky-coloured - mammoths and rocks seemed see-through)
	const float fade = fminf(EDGE_FADE_M, edge_end * 0.25f);
	glUniform2f(u->edge, edge_end - fade, edge_end);
	glUniform3fv(u->haze, 1, sky->haze);
	glUniform3fv(u->sun_dir, 1, sky->glow_dir);
	glUniform4fv(u->sun_glow, 1, sky->glow);
	glUniform3fv(u->light_tint, 1, sky->tint);
}

void atmosphere_haze(float out[3]) {
	if (indoors) {
		memcpy(out, in_fog, 3 * sizeof(float));
		return;
	}
	const SkyState *sky = lighting_sky();
	out[0] = sky->haze[0], out[1] = sky->haze[1], out[2] = sky->haze[2];
}

// Camera-space direction -> world, inverting the view rotation the frame uses
// (glRotatef(-pitch, x) then glRotatef(yaw, y)).
static void eye_to_world(const float e[3], float yaw, float pitch, float out[3]) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	// Rx(pitch)
	float x = e[0], y = e[1] * cp - e[2] * sp, z = e[1] * sp + e[2] * cp;
	// Ry(-yaw)
	out[0] = x * cy - z * sy;
	out[1] = y;
	out[2] = x * sy + z * cy;
}

static float view_roll_deg; // (the view rolled about its axis: the opening's head on the block)

void atmosphere_set_view_roll(float deg) {
	view_roll_deg = deg;
}

void atmosphere_draw_sky(float yaw_deg, float pitch_deg, float fov_y_deg, float aspect) {
	if (!sky_prog)
		return;
	const float d2r = (float)M_PI / 180.0f;
	float ty = tanf(fov_y_deg * 0.5f * d2r), tx = ty * aspect;
	static const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
	float verts[4][5];
	const float cr = cosf(-view_roll_deg * d2r), sr = sinf(-view_roll_deg * d2r);
	for (int i = 0; i < 4; i++) {
		const float ex = corners[i][0] * tx, ey = corners[i][1] * ty;
		float e[3] = {ex * cr - ey * sr, ex * sr + ey * cr, -1.0f}; // (the screen's corner, unrolled: the sky turns with the world)
		verts[i][0] = corners[i][0], verts[i][1] = corners[i][1];
		eye_to_world(e, yaw_deg * d2r, pitch_deg * d2r, &verts[i][2]);
	}
	GLboolean depth_test = glIsEnabled(GL_DEPTH_TEST);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(sky_prog);
	atmosphere_apply(&sky_u);
	const SkyState *sky = lighting_sky();
	glUniform3fv(sky_zenith, 1, sky->zenith);
	glUniform4f(sky_sun, sky->sun_dir[0], sky->sun_dir[1], sky->sun_dir[2], sky->sun_disc);
	glUniform4f(sky_moon, sky->moon_dir[0], sky->moon_dir[1], sky->moon_dir[2], sky->moon_disc);
	glUniform1f(sky_stars, sky->stars);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), &verts[0][0]);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), &verts[0][2]);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glUseProgram(0);
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	if (depth_test)
		glEnable(GL_DEPTH_TEST);
}

void atmosphere_set_weather_fog(float density_mult) {
	fog_mult = density_mult < 0.2f ? 0.2f : density_mult;
}
