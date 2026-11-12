#include "lighting.h"

#include <math.h>
#include <string.h>

#define REAL_SECONDS_PER_GAME_HOUR 75.0f // a full day in 30 minutes
#define SUN_MAX_ELEVATION_DEG 55.0f      // at noon, due south

// Palette keyed by the sun's height (sin of its elevation). The day key reproduces the
// look the world had with fixed lighting.
typedef struct {
	float sun_y;
	float light[3]; // sunlight (x strength); 0 once the sun is down
	float sky_ambient[3], ground_ambient[3];
	float haze[3], zenith[3], glow[3];
} Key;

static const Key KEYS[] = {
	// sun_y  sunlight                  sky ambient           ground ambient        haze                  zenith                glow
	{0.35f, {1.25f, 1.125f, 0.90f}, {0.62f, 0.68f, 0.78f}, {0.42f, 0.38f, 0.32f}, {0.70f, 0.71f, 0.70f}, {0.44f, 0.52f, 0.62f}, {1.00f, 0.86f, 0.62f}}, // day
	{0.10f, {1.10f, 0.78f, 0.50f}, {0.56f, 0.55f, 0.62f}, {0.40f, 0.33f, 0.27f}, {0.80f, 0.68f, 0.54f}, {0.40f, 0.46f, 0.60f}, {1.00f, 0.62f, 0.34f}}, // golden hour
	{-0.06f, {0.00f, 0.00f, 0.00f}, {0.44f, 0.41f, 0.52f}, {0.24f, 0.20f, 0.22f}, {0.36f, 0.31f, 0.38f}, {0.15f, 0.17f, 0.30f}, {0.80f, 0.42f, 0.30f}}, // twilight
	{-0.30f, {0.00f, 0.00f, 0.00f}, {0.20f, 0.23f, 0.33f}, {0.08f, 0.08f, 0.10f}, {0.11f, 0.13f, 0.18f}, {0.03f, 0.04f, 0.09f}, {0.40f, 0.46f, 0.60f}}, // night
};
#define KEY_COUNT (int)(sizeof(KEYS) / sizeof(KEYS[0]))
static const float MOONLIGHT[3] = {0.30f, 0.36f, 0.52f};

static SkyState state;

#define AMBIENT_WEIGHT 0.5f // ground/sky ambient are averaged...
#define SUN_WEIGHT 0.55f    // ...and the sun counts about this much for a typical surface

// Overall light level of the scene (what the baked vertex light is scaled against)
static void light_level(const float sky[3], const float ground[3], const float light[3], float out[3]) {
	for (int i = 0; i < 3; i++)
		out[i] = (sky[i] + ground[i]) * AMBIENT_WEIGHT + light[i] * SUN_WEIGHT;
}

static uint8_t to_u8(float f) {
	f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
	return (uint8_t)(f * 255.0f + 0.5f);
}

void lighting_bake(const float n[3], float tint_r, float tint_g, float tint_b, uint8_t out_rgb[3]) {
	// The midday key and the sun direction the world was lit with before day/night
	static const float d[3] = {-0.5f, 0.65f, 0.6f};
	const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	const Key *day = &KEYS[0];
	float hemi = n[1] * 0.5f + 0.5f;
	float ndl = (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / len;
	if (ndl < 0.0f)
		ndl = 0.0f;
	const float tint[3] = {tint_r, tint_g, tint_b};
	for (int i = 0; i < 3; i++) {
		float light = day->ground_ambient[i] + (day->sky_ambient[i] - day->ground_ambient[i]) * hemi + day->light[i] * ndl;
		out_rgb[i] = to_u8(light * tint[i] * 0.5f);
	}
}

void lighting_bake_params(float out[16]) {
	static const float d[3] = {-0.5f, 0.65f, 0.6f}; // (lighting_bake's: the midday key, its sun)
	const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	const Key *day = &KEYS[0];
	for (int i = 0; i < 3; i++) {
		out[i] = day->ground_ambient[i], out[4 + i] = day->sky_ambient[i], out[8 + i] = day->light[i];
		out[12 + i] = d[i] / len;
	}
	out[3] = out[7] = out[11] = out[15] = 0.0f;
}

static float smooth(float t) {
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return t * t * (3.0f - 2.0f * t);
}

static void mix3(float out[3], const float a[3], const float b[3], float t) {
	for (int i = 0; i < 3; i++)
		out[i] = a[i] + (b[i] - a[i]) * t;
}

static float overcast; // the weather (lighting_set_weather)

static void recompute(void) {
	SkyState *s = &state;
	// The sun rises in the east (+x) at 6:00, culminates due south (+z) at noon and sets
	// in the west at 18:00; below the horizon it keeps turning, and the moon is opposite.
	const float d2r = (float)M_PI / 180.0f;
	float a = (s->hours - 6.0f) / 12.0f * (float)M_PI;
	float tilt = SUN_MAX_ELEVATION_DEG * d2r;
	s->sun_dir[0] = cosf(a);
	s->sun_dir[1] = sinf(a) * sinf(tilt);
	s->sun_dir[2] = sinf(a) * cosf(tilt);
	for (int i = 0; i < 3; i++)
		s->moon_dir[i] = -s->sun_dir[i];
	float y = s->sun_dir[1];

	// Palette between the two keys around the sun's height
	const Key *hi = &KEYS[0], *lo = &KEYS[0];
	float t = 0.0f;
	if (y <= KEYS[KEY_COUNT - 1].sun_y) {
		hi = lo = &KEYS[KEY_COUNT - 1];
	} else if (y < KEYS[0].sun_y) {
		for (int k = 0; k < KEY_COUNT - 1; k++)
			if (y <= KEYS[k].sun_y && y >= KEYS[k + 1].sun_y) {
				hi = &KEYS[k], lo = &KEYS[k + 1];
				t = smooth((KEYS[k].sun_y - y) / (KEYS[k].sun_y - KEYS[k + 1].sun_y));
				break;
			}
	}
	float sunlight[3];
	mix3(sunlight, hi->light, lo->light, t);
	mix3(s->sky_ambient, hi->sky_ambient, lo->sky_ambient, t);
	mix3(s->ground_ambient, hi->ground_ambient, lo->ground_ambient, t);
	mix3(s->haze, hi->haze, lo->haze, t);
	mix3(s->zenith, hi->zenith, lo->zenith, t);
	mix3(s->glow, hi->glow, lo->glow, t);

	// One directional light: the sun while it is up, then the moon
	s->night = smooth((0.02f - y) / 0.30f);
	// The sun fades out as it touches the horizon while the moonlight fades in, so dusk
	// never gets darker than night
	float sun_w = smooth(y / 0.06f);
	float moon_w = smooth((0.04f - y) / 0.14f) * smooth(s->moon_dir[1] / 0.15f + 1.0f);
	if (sun_w >= moon_w) {
		memcpy(s->light_dir, s->sun_dir, sizeof(s->light_dir));
		for (int i = 0; i < 3; i++)
			s->light_color[i] = sunlight[i] * sun_w + MOONLIGHT[i] * moon_w;
	} else {
		memcpy(s->light_dir, s->moon_dir, sizeof(s->light_dir));
		for (int i = 0; i < 3; i++)
			s->light_color[i] = MOONLIGHT[i] * moon_w;
	}
	// The weather: an overcast sky dims the sun and greys the sky and haze (rain, snow, fog)
	if (overcast > 0.0f) {
		const float ov = overcast;
		const float grey = 0.299f * s->haze[0] + 0.587f * s->haze[1] + 0.114f * s->haze[2];
		const float zgrey = 0.299f * s->zenith[0] + 0.587f * s->zenith[1] + 0.114f * s->zenith[2];
		for (int i = 0; i < 3; i++) {
			s->haze[i] += (grey * 0.9f - s->haze[i]) * ov * 0.8f;
			s->zenith[i] += ((zgrey + grey) * 0.45f - s->zenith[i]) * ov * 0.85f;
			s->light_color[i] *= 1.0f - 0.65f * ov;
			s->sky_ambient[i] *= 1.0f - 0.15f * ov;
			s->glow[i] *= 1.0f - 0.8f * ov;
		}
	}
	// Scene light relative to midday (scales the baked vertex light in the shaders)
	float now[3], noon[3];
	light_level(s->sky_ambient, s->ground_ambient, s->light_color, now);
	light_level(KEYS[0].sky_ambient, KEYS[0].ground_ambient, KEYS[0].light, noon);
	for (int i = 0; i < 3; i++)
		s->tint[i] = now[i] / noon[i];
	memcpy(s->glow_dir, y > -0.04f ? s->sun_dir : s->moon_dir, sizeof(s->glow_dir));
	s->glow[3] = y > -0.04f ? 8.0f : 24.0f; // the moon's glow is tighter
	s->sun_disc = smooth((y + 0.03f) / 0.05f) * (1.0f - overcast);
	s->moon_disc = smooth(s->moon_dir[1] / 0.08f) * s->night * (1.0f - overcast);
	s->stars = smooth((-0.08f - y) / 0.15f) * (1.0f - overcast);
}

void lighting_set_weather(float overcast_0_1) {
	overcast = overcast_0_1 < 0.0f ? 0.0f : overcast_0_1 > 1.0f ? 1.0f : overcast_0_1;
}

static double game_hours; // (since the game began: cells reset by it)

float lighting_game_hours(void) {
	return (float)game_hours;
}

void lighting_set_game_hours(float hours) {
	game_hours = hours > 0.0f ? hours : 0.0;
}

void lighting_init(float start_hours) {
	state.hours = start_hours;
	game_hours = start_hours;
	recompute();
}

void lighting_update(float dt_s, float speed) {
	state.hours += dt_s * speed / REAL_SECONDS_PER_GAME_HOUR;
	game_hours += dt_s * speed / REAL_SECONDS_PER_GAME_HOUR;
	while (state.hours >= 24.0f)
		state.hours -= 24.0f;
	recompute();
}

void lighting_set_hours(float hours) {
	state.hours = fmodf(fmaxf(hours, 0.0f), 24.0f);
	recompute();
}

void lighting_advance_hours(float hours) {
	state.hours = fmodf(state.hours + hours, 24.0f);
	game_hours += hours > 0.0f ? hours : 0.0f;
	recompute();
}

const SkyState *lighting_sky(void) {
	return &state;
}
