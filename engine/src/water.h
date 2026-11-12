#ifndef WATER_H
#define WATER_H

// Translucent water surfaces (moving ripples: sky reflection and glints, plus the shared
// atmosphere fog), used by the
// terrain's cell water planes and Skyrim's placed water objects.
// Between water_begin and water_end, callers bind their vertex data as attribute 0
// (xyz) and draw. Positions are world-space floats unless water_set_transform says
// otherwise: world = position * scale + origin.

void water_init(void);
// Returns 0 if the water shader is unavailable (nothing to draw with).
int water_begin(void);
void water_set_transform(float ox, float oy, float oz, float scale);
// Animation time (seconds), once per frame
void water_set_time(float seconds);
void water_end(void);

#endif
