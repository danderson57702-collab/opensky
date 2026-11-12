#ifndef ATMOSPHERE_H
#define ATMOSPHERE_H

#include <vitaGL.h>

// One model of the air, shared by every world shader so terrain, objects, water and
// the sky agree: exponential height fog (thick in valleys, thinning with altitude),
// a warm glow in the fog toward the sun, and a fade to the haze colour at the edge of
// the streamed world. The sky is drawn from the same functions, so fully fogged
// geometry melts exactly into the horizon.

// CG source for vertex/fragment shaders: declares the atmosphere and light uniforms and
//   float3 atmo_fog_color(float3 dir)  haze colour seen along a view direction
//   float4 atmo_fog(float3 world_pos)  rgb = fog colour, a = fog amount (0..1)
//   float3 atmo_light(float3 baked)    baked vertex light (half strength, doubled by the
//                                      fragment shaders, PS2-style) scaled for the time of day
//   float3 atmo_torch(float3 world)    offset from the held torch in units of its reach, and
//   u_torchColor                       its light: a shader passes both to its fragments, which
//                                      add colour * (1 - |offset|^2)^2 (none when no torch)
#define ATMO_CG_UNIFORMS                                                                               \
	"uniform float3 u_cam;\n"                                                                          \
	"uniform float4 u_fog;     // density at the base height, 1 / scale height, base height\n"       \
	"uniform float2 u_edge;    // edge fade start, end (horizontal metres)\n"                         \
	"uniform float3 u_haze;\n"                                                                         \
	"uniform float3 u_sunDir;  // where the fog glows: the sun, or the moon at night\n"               \
	"uniform float4 u_sunGlow; // rgb, falloff exponent\n"                                             \
	"uniform float3 u_lightTint; // time-of-day scale of the baked vertex light\n"                    \
	"uniform float4 u_torch;   // the held torch: position, 1 / reach (0: none)\n"                   \
	"uniform float3 u_torchColor;\n"                                                                  \
	"float3 atmo_torch(float3 p) { return (p - u_torch.xyz) * u_torch.w; }\n"                          \
	"float3 atmo_fog_color(float3 dir) {\n"                                                            \
	"	float s = saturate(dot(dir, u_sunDir));\n"                                                     \
	"	return lerp(u_haze, u_sunGlow.rgb, pow(s, u_sunGlow.w) * 0.55f);\n"                            \
	"}\n"

#define ATMO_CG_FOG                                                                                    \
	"float4 atmo_fog(float3 p) {\n"                                                                    \
	"	float3 d = p - u_cam;\n"                                                                       \
	"	float dist = length(d);\n"                                                                     \
	"	float k = d.y * u_fog.y;\n"                                                                    \
	"	float f = abs(k) > 0.01f ? (1.0f - exp(-k)) / k : 1.0f - 0.5f * k;\n"                          \
	"	float depth = u_fog.x * dist * exp((u_fog.z - u_cam.y) * u_fog.y) * f;\n"                      \
	"	float a = 1.0f - exp(-depth);\n"                                                               \
	"	a = max(a, saturate((length(d.xz) - u_edge.x) / (u_edge.y - u_edge.x)));\n"                    \
	"	return float4(atmo_fog_color(d / max(dist, 0.001f)), a);\n"                                   \
	"}\n"
#define ATMO_CG_NO_FOG "float4 atmo_fog(float3 p) { return float4(0.0f, 0.0f, 0.0f, 0.0f); }\n"

#define ATMO_CG_LIGHT "float3 atmo_light(float3 baked) { return saturate(baked * u_lightTint); }\n"
#define ATMO_CG_NO_LIGHT "float3 atmo_light(float3 baked) { return baked; }\n"

#define ATMO_CG ATMO_CG_UNIFORMS ATMO_CG_FOG ATMO_CG_LIGHT

// Shader programs are built per atmosphere variant (atmosphere_cg); only the full one is
// used now (the stubbed debug variants that isolated the stage-3 rendering bug are gone).
#define ATMO_VARIANTS 1
const char *atmosphere_cg(int variant);
int atmosphere_variant(void);

// Fragment-shader statements: rolls off overbright colour (float3 `c`, or `c.rgb`) by its
// brightest channel, keeping the hue. The scene is stored in 8 bits, so without this a
// bright texture under strong light clips channel by channel into false yellow/red.
#define TONEMAP_CG                                                                                     \
	"	{\n"                                                                                           \
	"		float3 rgb = c.rgb;\n"                                                                       \
	"		float m = max(rgb.r, max(rgb.g, rgb.b));\n"                                                  \
	"		float k = 0.75f;\n"                                                                          \
	"		float t = k + (1.0f - k) * (1.0f - exp(-(max(m, k) - k) / (1.0f - k)));\n"                  \
	"		c.rgb = rgb * lerp(1.0f, t / max(m, 0.0001f), step(k, m));\n"                                \
	"	}\n"

typedef struct {
	GLint cam, fog, edge, haze, sun_dir, sun_glow, light_tint, torch, torch_color;
} AtmoUniforms;

void atmosphere_init(float edge_end_m);
// The streamed world's edge moved (the performance setting's view distance)
void atmosphere_set_edge(float edge_end_m);
void atmosphere_set_camera(float x, float y, float z);
// Finds the atmosphere uniforms of a program using ATMO_CG.
void atmosphere_locate(GLuint prog, AtmoUniforms *u);
// Uploads the current atmosphere to the bound program.
void atmosphere_apply(const AtmoUniforms *u);
// Full-screen sky for the camera (draw first; leaves depth testing/writing as it was).
void atmosphere_set_view_roll(float deg); // (the view rolled, degrees: the sky drawn to match)
void atmosphere_draw_sky(float yaw_deg, float pitch_deg, float fov_y_deg, float aspect);
// Indoors: plain distance fog of the cell's colour reaching full at far_m, no sun glow,
// and baked light at full strength (no time of day). NULL fog: back outdoors.
void atmosphere_set_interior(const float *fog_rgb, float near_m, float far_m);
// Under water (outdoors): short green-blue fog, filtered light. The fog colour, if on.
void atmosphere_set_underwater(int on);
// No fog or haze at all while on (a menu's 3D scene drawn over the game)
void atmosphere_set_clear(int on);
int atmosphere_underwater_fog(float out[3]);
// The held torch's light this frame (reach 0: none)
void atmosphere_set_torch(const float pos[3], float reach_m, const float rgb[3]);
// Fragment-shader statement: adds the torch to colour c (float3) lit from texture t (rgb),
// given the interpolated atmo_torch offset vTorch and colour vTorchColor
#define TORCH_CG                                                                                       \
	"	{\n"                                                                                           \
	"		float fall = saturate(1.0f - dot(vTorch, vTorch));\n"                                       \
	"		c.rgb += t.rgb * vTorchColor * (fall * fall);\n"                                             \
	"	}\n"
// Current horizontal haze colour (for the fixed-function fallbacks).
void atmosphere_haze(float out[3]);
// The weather's haze outdoors: its density x this (fog, rain, snow: thicker)
void atmosphere_set_weather_fog(float density_mult);

#endif
