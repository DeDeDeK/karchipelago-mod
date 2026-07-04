#ifndef WEATHER_FX_H
#define WEATHER_FX_H

#include "structs.h"

// Player/machine slots iterated by the wind, hail, and puddle layers.
#define WEATHER_PLAYER_SLOTS 5

// Symmetric random in [-1, 1).
float Weather_Randf2(void);

// Shared GX setup for a weather layer's translucent world pass: flat per-vertex
// color, alpha blend (additive when `additive`), depth-tested but not
// depth-writing so opaque geometry occludes the layer, no cull, camera view
// matrix loaded. line_width > 0 also sets the GX line width for line layers.
void WeatherGX_BeginXlu(COBJ *cam, int additive, int line_width);

// Create a world-camera GX layer GObj bound to callback `cb`, logging `log`.
// Returns the GObj to cache, or NULL on allocation failure.
GOBJ *WeatherGX_EnsureLayer(int entity_class, int p_link, void *cb,
                            int gx_link, int gx_pri, const char *log);

#endif // WEATHER_FX_H
