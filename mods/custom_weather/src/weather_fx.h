#ifndef WEATHER_FX_H
#define WEATHER_FX_H

#include "structs.h"

// Player/machine slots iterated by the wind, hail, and puddle layers.
#define WEATHER_PLAYER_SLOTS 5

// Symmetric random in [-1, 1).
float Weather_Randf2(void);

// Resolve a {Preset, Off, On} menu index (0/1/2) to a boolean. Preset (0)
// yields `preset_default` (the module's built-in behavior); Off (1) = 0;
// On (2) = 1. Lets every toggle default to "Preset" yet still force Off/On.
static inline int WeatherToggle(int idx, int preset_default)
{
    return (idx == 0) ? preset_default : (idx == 2);
}

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
