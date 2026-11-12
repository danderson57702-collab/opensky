#ifndef WEATHER_H
#define WEATHER_H

// Skyrim's weather (pack/weather.txt, see converter/weather.py): the region you're in has its
// weathers and their chances (the tundra round Whiterun mostly clear or cloudy, the
// mountains snow, the marshes fog); every few game hours one is picked and the sky changes
// to it over half a minute. It greys the light and thickens the haze (lighting.h,
// atmosphere.h); rain and snow fall around you outdoors.

void weather_init(const char *pack_dir);
// Each frame: where you are (outdoors or not), real seconds and game hours gone by
void weather_update(float x, float z, int outdoors, float dt_s, float hours_passed);
// Rain or snow falling around the camera (outdoors; after the world, before the HUD)
void weather_draw(const float cam[3], float yaw_deg, float pitch_deg);
const char *weather_name(void); // ("Clear", "Rain"...)
// (debug) the next weather at once: its name
const char *weather_cycle(void);

// Clear Skies shouted: the clouds, rain and fog blown away for this long (real seconds)
void weather_clear_for(float seconds);

// How much of the rain / snow is drawn (the performance setting: 0.1 .. 1)
void weather_set_density(float share);

#endif
