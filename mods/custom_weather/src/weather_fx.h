#ifndef WEATHER_FX_H
#define WEATHER_FX_H

#include "structs.h"

// Player/machine slots iterated by the weather layers.
#define WEATHER_PLAYER_SLOTS 5

// Symmetric random in [-1, 1).
float Weather_Randf2(void);

// Normalized City Trial round progress, 0 (start) .. 1 (end), from the match timer.
// -1 when there is no live round (menus, match intro) - the layers that schedule
// events across the round hold still until it goes non-negative.
float Weather_RoundProgress(void);

// Resolve a {Preset, Off, On} menu index to a boolean: Preset (0) yields
// `preset_default`, Off (1) = 0, On (2) = 1.
static inline int WeatherToggle(int idx, int preset_default)
{
    return (idx == 0) ? preset_default : (idx == 2);
}

// Shared GX setup for a weather layer's translucent world pass: flat per-vertex
// color, alpha blend (additive when `additive`), depth-tested but not
// depth-writing so opaque geometry occludes the layer, no cull, camera view
// matrix loaded.
void WeatherGX_BeginXlu(COBJ *cam, int additive, int line_width);

// Create a world-camera GX layer GObj bound to callback `cb`; NULL on failure.
GOBJ *WeatherGX_EnsureLayer(int entity_class, int p_link, void *cb,
                            int gx_link, int gx_pri, const char *log);

#endif // WEATHER_FX_H
