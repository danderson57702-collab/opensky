#ifndef LIGHTING_H
#define LIGHTING_H

#include <stdint.h>

// Time of day: a game clock driving the sun and moon, and the palette of light, sky and
// haze colours that every world shader uses (see atmosphere.h). Surfaces are lit per
// vertex in the shaders: hemisphere ambient (sky above, ground below) plus one
// directional light, the sun by day and the moon by night.

typedef struct {
	float hours;          // 0..24
	float light_dir[3];   // toward the light currently shading the world
	float light_color[3]; // its colour x strength
	float sky_ambient[3];
	float ground_ambient[3];
	float haze[3];        // fog / horizon colour
	float zenith[3];      // sky colour overhead
	float glow_dir[3];    // where the fog glows (sun, or moon at night)
	float glow[4];        // glow colour, falloff exponent
	float sun_dir[3];
	float moon_dir[3];
	float sun_disc;       // visibility of the discs (0..1)
	float moon_disc;
	float stars;          // 0..1
	float night;          // 0 (day) .. 1 (full night)
	float tint[3];        // world light now relative to midday: scales the baked vertex light
} SkyState;

void lighting_init(float start_hours);
// Advances the clock by dt real seconds, speed x faster than normal.
void lighting_update(float dt_s, float speed);
// Jumps the time of day forward (waiting)
void lighting_advance_hours(float hours);
void lighting_set_hours(float hours); // (loading a game)
// Game hours since the game began (waiting and sleeping count; saved: cells reset by it)
float lighting_game_hours(void);
void lighting_set_game_hours(float hours);
const SkyState *lighting_sky(void);
// The weather: how overcast the sky is (0 clear .. 1 a storm): duller light, grey sky and
// haze, no sun, moon or stars
void lighting_set_weather(float overcast);

// Baked vertex light (midday sun from the south-west + sky/ground ambient) at half strength
// (the texture stage doubles it, PS2-style). Shaders scale it by SkyState.tint, so the
// world warms, dims and cools through the day while the vertex data stays static.
void lighting_bake(const float n[3], float tint_r, float tint_g, float tint_b, uint8_t out_rgb[3]);
// lighting_bake's constants for a shader doing it per vertex: ground, sky, sun light (rgb),
// the sun's direction (unit), each a float4
void lighting_bake_params(float out[16]);

#endif
