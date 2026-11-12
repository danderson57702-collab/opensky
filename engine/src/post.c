#include <math.h>
#include "post.h"
#include "gamelog.h"

#include <string.h>

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include "shader.h"

#define SCREEN_W 960
#define SCREEN_H 544

// --- Look (tweak here) ------------------------------------------------------------------
#define BLOOM_THRESHOLD 0.88f // where bloom starts: only near-white highlights (sun, fire, sunlit snow)
#define BLOOM_KNEE 0.12f      // soft ramp above the threshold
#define SHOULDER 0.80f        // highlights above this roll off softly instead of clipping
#define BLOOM_STRENGTH 0.55f
#define SATURATION 0.78f      // 1 = unchanged
#define CONTRAST 1.06f        // around mid-grey
#define LIFT 0.035f           // raises blacks: hazy, never pure black
#define SHADOW_TINT_R 0.97f   // shadows lean blue-grey...
#define SHADOW_TINT_G 0.99f
#define SHADOW_TINT_B 1.04f
#define HIGHLIGHT_TINT_R 1.04f // ...highlights lean warm
#define HIGHLIGHT_TINT_G 1.01f
#define HIGHLIGHT_TINT_B 0.95f
#define DITHER_LEVELS 63.0f   // output quantized to 6 bits per channel, ordered-dithered

// --- Shaders ------------------------------------------------------------------------------

static const char *QUAD_VS =
	"void main(float2 position, float2 texcoord,\n"
	"	float4 out vPosition : POSITION, float2 out vTexcoord : TEXCOORD0) {\n"
	"	vPosition = float4(position, 0.0f, 1.0f);\n"
	"	vTexcoord = texcoord;\n"
	"}\n";

// Scene -> quarter size: 4 bilinear taps (a 4x4 box) and a soft threshold
static const char *BRIGHT_FS =
	"uniform sampler2D u_tex;\n"
	"uniform float2 u_texel;  // scene texel size\n"
	"uniform float2 u_bloom;  // threshold, knee\n"
	"float4 main(float2 uv : TEXCOORD0) {\n"
	"	float3 c = tex2D(u_tex, uv + u_texel * float2(-1.0f, -1.0f)).rgb;\n"
	"	c += tex2D(u_tex, uv + u_texel * float2(1.0f, -1.0f)).rgb;\n"
	"	c += tex2D(u_tex, uv + u_texel * float2(-1.0f, 1.0f)).rgb;\n"
	"	c += tex2D(u_tex, uv + u_texel * float2(1.0f, 1.0f)).rgb;\n"
	"	c *= 0.25f;\n"
	"	float l = max(c.r, max(c.g, c.b));\n"
	"	float w = saturate((l - u_bloom.x) / u_bloom.y);\n"
	"	return float4(c * (w * w * (3.0f - 2.0f * w)), 1.0f);\n"
	"}\n";

// 9-tap Gaussian along u_dir using 5 bilinear taps
static const char *BLUR_FS =
	"uniform sampler2D u_tex;\n"
	"uniform float2 u_dir; // texel size along the blur axis\n"
	"float4 main(float2 uv : TEXCOORD0) {\n"
	"	float3 c = tex2D(u_tex, uv).rgb * 0.2270270f;\n"
	"	c += tex2D(u_tex, uv + u_dir * 1.3846154f).rgb * 0.3162162f;\n"
	"	c += tex2D(u_tex, uv - u_dir * 1.3846154f).rgb * 0.3162162f;\n"
	"	c += tex2D(u_tex, uv + u_dir * 3.2307692f).rgb * 0.0702703f;\n"
	"	c += tex2D(u_tex, uv - u_dir * 3.2307692f).rgb * 0.0702703f;\n"
	"	return float4(c, 1.0f);\n"
	"}\n";

static const char *COMPOSE_FS =
	"uniform sampler2D u_scene;\n"
	"uniform sampler2D u_bloom;\n"
	"uniform float4 u_grade;     // bloom strength, saturation, contrast, lift\n"
	"uniform float3 u_shadow;    // shadow tint\n"
	"uniform float3 u_highlight; // highlight tint\n"
	"uniform float3 u_out;       // output width, height, dither levels\n"
	"uniform float u_shoulder;\n"
	"float bayer2(float2 q) { return fmod(2.0f * q.x + 3.0f * q.y, 4.0f); }\n"
	"float4 main(float2 uv : TEXCOORD0) {\n"
	"	float3 c = tex2D(u_scene, uv).rgb + tex2D(u_bloom, uv).rgb * u_grade.x;\n"
	"	// Highlights roll off by the brightest channel, keeping the hue (per-channel\n"
	"	// clipping turned bright canvas into fire colours)\n"
	"	float m = max(c.r, max(c.g, c.b)), k = u_shoulder;\n"
	"	float t = k + (1.0f - k) * (1.0f - exp(-(max(m, k) - k) / (1.0f - k)));\n"
	"	c *= lerp(1.0f, t / max(m, 0.0001f), step(k, m));\n"
	"	float l = dot(c, float3(0.299f, 0.587f, 0.114f));\n"
	"	c = lerp(float3(l, l, l), c, u_grade.y);\n"
	"	c = (c - 0.5f) * u_grade.z + 0.5f;\n"
	"	c *= lerp(u_shadow, u_highlight, saturate(l));\n"
	"	c = u_grade.w + c * (1.0f - u_grade.w);\n"
	"	float2 p = fmod(floor(uv * u_out.xy), 4.0f);\n"
	"	float d = (4.0f * bayer2(fmod(p, 2.0f)) + bayer2(floor(p * 0.5f)) + 0.5f) / 16.0f;\n"
	"	c = floor(saturate(c) * u_out.z + d) / u_out.z;\n"
	"	return float4(c, 1.0f);\n"
	"}\n";

// The scene at a lower resolution, scaled up to the screen as it is (bloom off)
static const char *COPY_FS =
	"uniform sampler2D u_tex;\n"
	"float4 main(float2 uv : TEXCOORD0) {\n"
	"	return float4(tex2D(u_tex, uv).rgb, 1.0f);\n"
	"}\n";

// Camera motion blur (the option): the view's turn this frame smeared along its direction - 30 a second looks less
// stepped as you look about; 7 taps along u_vec (the turn, in texture units), weighted to the middle
#define MOTION_MIN 0.0015f // (a turn of less than this much of the screen a frame: no blur)
static const char *MOTION_FS =
	"uniform sampler2D u_tex;\n"
	"uniform float2 u_vec;\n"
	"float4 main(float2 uv : TEXCOORD0) {\n"
	"	float3 c = tex2D(u_tex, uv).rgb * 0.22f;\n"
	"	c += (tex2D(u_tex, uv + u_vec * 0.167f).rgb + tex2D(u_tex, uv - u_vec * 0.167f).rgb) * 0.17f;\n"
	"	c += (tex2D(u_tex, uv + u_vec * 0.333f).rgb + tex2D(u_tex, uv - u_vec * 0.333f).rgb) * 0.13f;\n"
	"	c += (tex2D(u_tex, uv + u_vec * 0.5f).rgb + tex2D(u_tex, uv - u_vec * 0.5f).rgb) * 0.09f;\n"
	"	return float4(c, 1.0f);\n"
	"}\n";

// --- Targets ------------------------------------------------------------------------------

typedef struct {
	GLuint fbo, tex;
	int w, h;
} Target;

// The targets are made at the resolution setting's size (the most the world is drawn at); the
// dynamic resolution draws into the lower-left part of them (cur_w x cur_h) and every pass reads
// just that part - nothing made again as the size moves
static Target scene, bloom_a, bloom_b, moved; // (moved: the scene after the motion blur)
static GLuint bright_prog, blur_prog, compose_prog, copy_prog;
static GLint motion_vec;
static GLuint motion_prog;
static int upscaler = POST_UPSCALE_BILINEAR;
static int motion_on;
static float motion_u, motion_v;
static int want_w = POST_W, want_h = POST_H; // (the resolution setting: the most the world is drawn at)
static int cur_w = POST_W, cur_h = POST_H;   // (this frame's: the dynamic resolution's, within it)
static GLint bright_texel, bright_bloom, blur_dir;
static GLint compose_grade, compose_shadow, compose_highlight, compose_out, compose_shoulder;
static int ready, active, enabled = 0;
static GLint cur_filter = -1; // (the filter last set on the texture scaled up to the screen)

static void free_target(Target *t) {
	if (t->fbo)
		glDeleteFramebuffers(1, &t->fbo);
	if (t->tex)
		glDeleteTextures(1, &t->tex);
	memset(t, 0, sizeof(*t));
}

static int make_target(Target *t, int w, int h) {
	t->w = w, t->h = h;
	glGenTextures(1, &t->tex);
	glBindTexture(GL_TEXTURE_2D, t->tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &t->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return t->fbo && t->tex;
}

static const char *const ATTRIBS[] = {"position", "texcoord"};

// The motion blur's shader, compiled only when its option is first turned on. (FXAA and FSR 1.0 are gone, Nov 10: on the
// Vita FXAA blurred the small picture and cost frames, and FSR's 12 taps over every pixel of the screen took 70 ms a frame)
static void need_motion(void) {
	static int tried;
	if (tried || !ready)
		return;
	tried = 1;
	const unsigned long long t0 = sceKernelGetProcessTimeWide();
	motion_prog = shader_program("post motion", QUAD_VS, MOTION_FS, ATTRIBS, 2);
	if (motion_prog) {
		glUseProgram(motion_prog);
		glUniform1i(glGetUniformLocation(motion_prog, "u_tex"), 0);
		motion_vec = glGetUniformLocation(motion_prog, "u_vec");
		glUseProgram(0);
	}
	game_log("post: motion blur %s in %u ms", motion_prog ? "ready" : "FAILED", (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000));
}

int post_init(void) {
	bright_prog = shader_program("post bright", QUAD_VS, BRIGHT_FS, ATTRIBS, 2);
	blur_prog = shader_program("post blur", QUAD_VS, BLUR_FS, ATTRIBS, 2);
	compose_prog = shader_program("post compose", QUAD_VS, COMPOSE_FS, ATTRIBS, 2);
	copy_prog = shader_program("post copy", QUAD_VS, COPY_FS, ATTRIBS, 2);
	if (!bright_prog || !blur_prog || !compose_prog || !copy_prog) {
		sceClibPrintf("post: shaders failed, drawing straight to the screen\n");
		return 0;
	}
	bright_texel = glGetUniformLocation(bright_prog, "u_texel");
	bright_bloom = glGetUniformLocation(bright_prog, "u_bloom");
	blur_dir = glGetUniformLocation(blur_prog, "u_dir");
	compose_grade = glGetUniformLocation(compose_prog, "u_grade");
	compose_shadow = glGetUniformLocation(compose_prog, "u_shadow");
	compose_highlight = glGetUniformLocation(compose_prog, "u_highlight");
	compose_out = glGetUniformLocation(compose_prog, "u_out");
	compose_shoulder = glGetUniformLocation(compose_prog, "u_shoulder");
	glUseProgram(bright_prog);
	glUniform1i(glGetUniformLocation(bright_prog, "u_tex"), 0);
	glUseProgram(blur_prog);
	glUniform1i(glGetUniformLocation(blur_prog, "u_tex"), 0);
	glUseProgram(compose_prog);
	glUniform1i(glGetUniformLocation(compose_prog, "u_scene"), 0);
	glUniform1i(glGetUniformLocation(compose_prog, "u_bloom"), 1);
	glUseProgram(copy_prog);
	glUniform1i(glGetUniformLocation(copy_prog, "u_tex"), 0);
	glUseProgram(0);
	ready = 1;
	if (motion_on) // (the option, as saved: compiled now, while it loads)
		need_motion();
	return 1;
}

// The targets at the resolution setting's size (made again when it changes); FXAA's and the
// motion blur's only while their option is on (each a screen's worth of video memory)
static int size_targets(void) {
	if (!scene.fbo || scene.w != want_w || scene.h != want_h) {
		free_target(&scene), free_target(&bloom_a), free_target(&bloom_b), free_target(&moved);
		if (!make_target(&scene, want_w, want_h) || !make_target(&bloom_a, want_w / 4, want_h / 4) ||
		    !make_target(&bloom_b, want_w / 4, want_h / 4)) {
			sceClibPrintf("post: render targets failed, drawing straight to the screen\n");
			free_target(&scene), free_target(&bloom_a), free_target(&bloom_b);
			ready = 0;
			return 0;
		}
		cur_filter = -1;
		sceClibPrintf("post: %dx%d scene, %dx%d bloom\n", want_w, want_h, want_w / 4, want_h / 4);
	}
	if (motion_on && motion_prog && !moved.fbo && !make_target(&moved, want_w, want_h))
		free_target(&moved);
	else if (!motion_on && moved.fbo)
		free_target(&moved);
	return 1;
}

void post_set_resolution(int w, int h) {
	want_w = w > 64 && w <= SCREEN_W ? w : SCREEN_W;
	want_h = h > 64 && h <= SCREEN_H ? h : SCREEN_H;
	if (cur_w > want_w || cur_h > want_h || cur_w <= 0)
		cur_w = want_w, cur_h = want_h;
}

void post_set_dynamic_size(int w, int h) {
	cur_w = w < 64 ? 64 : w > want_w ? want_w : w;
	cur_h = h < 64 ? 64 : h > want_h ? want_h : h;
}

int post_max_w(void) {
	return want_w;
}

int post_max_h(void) {
	return want_h;
}

int post_render_w(void) {
	return active ? cur_w : SCREEN_W;
}

int post_render_h(void) {
	return active ? cur_h : SCREEN_H;
}

void post_set_enabled(int on) {
	enabled = on;
}

void post_set_upscaler(int kind) {
	upscaler = kind <= POST_UPSCALE_OFF ? POST_UPSCALE_OFF : POST_UPSCALE_BILINEAR;
}

void post_set_motion_blur(int on) {
	motion_on = on;
	if (on)
		need_motion();
}

void post_camera_motion(float u, float v) {
	motion_u = u, motion_v = v;
}

void post_begin(void) {
	// (offscreen for bloom, or to draw the world smaller than the screen)
	const int scaled = want_w != SCREEN_W || want_h != SCREEN_H || cur_w != SCREEN_W || cur_h != SCREEN_H;
	active = ready && (enabled || scaled || (motion_on && motion_prog)) && size_targets();
	if (!active)
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, scene.fbo);
	glViewport(0, 0, cur_w, cur_h);
}

// A quad over the destination's drawn part (the screen whole: dst NULL), reading the source's
// part u1 x v1 (texture units: the drawn share of the targets); attribute 0 = clip xy, 1 = uv
static void draw_quad(const Target *dst, int w, int h, float u1, float v1) {
	const float verts[] = {-1, -1, 0, 0, 1, -1, u1, 0, 1, 1, u1, v1, -1, 1, 0, v1};
	glBindFramebuffer(GL_FRAMEBUFFER, dst ? dst->fbo : 0);
	glViewport(0, 0, w, h);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts + 2);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

void post_end(void) {
	if (!active)
		return;
	active = 0;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDepthMask(GL_FALSE);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glActiveTexture(GL_TEXTURE0);
	const float ru = (float)cur_w / scene.w, rv = (float)cur_h / scene.h; // (the drawn share of the targets)

	GLuint src = scene.tex;
	// The motion blur (the option): only while the view turns enough to see
	if (motion_on && motion_prog && moved.fbo && (fabsf(motion_u) > MOTION_MIN || fabsf(motion_v) > MOTION_MIN)) {
		glUseProgram(motion_prog);
		glUniform2f(motion_vec, motion_u * ru, motion_v * rv);
		glBindTexture(GL_TEXTURE_2D, src);
		draw_quad(&moved, cur_w, cur_h, ru, rv);
		src = moved.tex;
	}

	if (!enabled) { // (only drawn smaller: scaled up to the screen - plain (Off: its pixels as blocks) or bilinear)
		glUseProgram(copy_prog);
		glBindTexture(GL_TEXTURE_2D, src);
		const GLint filter = upscaler == POST_UPSCALE_OFF ? GL_NEAREST : GL_LINEAR;
		if (filter != cur_filter || src != scene.tex) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
			if (src == scene.tex)
				cur_filter = filter;
		}
		draw_quad(NULL, SCREEN_W, SCREEN_H, ru, rv);
		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glUseProgram(0);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		return;
	}
	if (cur_filter != GL_LINEAR) { // (bloom's passes read the scene smoothly)
		glBindTexture(GL_TEXTURE_2D, scene.tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		cur_filter = GL_LINEAR;
	}

	// Bright pass: scene -> bloom_a (quarter size)
	const int bw = cur_w / 4 > 1 ? cur_w / 4 : 1, bh = cur_h / 4 > 1 ? cur_h / 4 : 1;
	glUseProgram(bright_prog);
	glUniform2f(bright_texel, 1.0f / scene.w, 1.0f / scene.h);
	glUniform2f(bright_bloom, BLOOM_THRESHOLD, BLOOM_KNEE);
	glBindTexture(GL_TEXTURE_2D, src);
	draw_quad(&bloom_a, bw, bh, ru, rv);

	// Separable blur: a -> b horizontally, b -> a vertically
	const float bu = (float)bw / bloom_a.w, bv = (float)bh / bloom_a.h;
	glUseProgram(blur_prog);
	glUniform2f(blur_dir, 1.0f / bloom_a.w, 0.0f);
	glBindTexture(GL_TEXTURE_2D, bloom_a.tex);
	draw_quad(&bloom_b, bw, bh, bu, bv);
	glUniform2f(blur_dir, 0.0f, 1.0f / bloom_a.h);
	glBindTexture(GL_TEXTURE_2D, bloom_b.tex);
	draw_quad(&bloom_a, bw, bh, bu, bv);

	// Compose onto the screen
	glUseProgram(compose_prog);
	glUniform4f(compose_grade, BLOOM_STRENGTH, SATURATION, CONTRAST, LIFT);
	glUniform3f(compose_shadow, SHADOW_TINT_R, SHADOW_TINT_G, SHADOW_TINT_B);
	glUniform3f(compose_highlight, HIGHLIGHT_TINT_R, HIGHLIGHT_TINT_G, HIGHLIGHT_TINT_B);
	glUniform3f(compose_out, SCREEN_W / ru, SCREEN_H / rv, DITHER_LEVELS); // (the dither by the screen's pixels: uv runs to ru, rv)
	glUniform1f(compose_shoulder, SHOULDER);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, bloom_a.tex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, src);
	draw_quad(NULL, SCREEN_W, SCREEN_H, ru, rv);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glUseProgram(0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}
