#ifndef WEATHER_FX_H
#define WEATHER_FX_H

#include "structs.h"
#include "stage.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"

// Every layer draws on the world camera's gx_link 0, XLU sub-pass.
#define WEATHER_GX_LINK 0
#define WEATHER_GX_PRI  0

// The City Trial backdrop is a depth-writing sphere at the world origin; a celestial
// layer anchored beyond it would be occluded, so moon and stars clamp inside it.
// Apparent size is referenced to WEATHER_DOME_REF_DIST so it survives the clamp.
#define WEATHER_DOME_R        2500.0f  // backdrop dome radius (geometry ~2856 * stage scale)
#define WEATHER_DOME_REF_DIST 1800.0f

// Shared menu value-name tables.
extern char *weather_toggle_names[];  // {"Preset", "Off", "On"}
extern char *weather_onoff_names[];   // {"Off", "On"}
extern char *weather_enable_names[];  // {"Disabled", "Enabled"}

// Symmetric random in [-1, 1).
float Weather_Randf2(void);
float Weather_RandRange(float lo, float hi);

// Normalized City Trial round progress, 0 (start) .. 1 (end), from the match timer.
// -1 when there is no live round (menus, match intro).
float Weather_RoundProgress(void);

// Resolve a {Preset, Off, On} menu index to a boolean: Preset (0) yields
// `preset_default`, Off (1) = 0, On (2) = 1.
static inline int WeatherToggle(int idx, int preset_default)
{
    return (idx == 0) ? preset_default : (idx == 2);
}

// NULL until the 3D scene is built; the layers that need stage extents retry.
StageNode *Weather_StageNode(void);
HSD_Fog *Weather_LiveFog(void);

// Ground height under (x, z), or `fallback` where the cast finds nothing.
float Weather_GroundYAt(StageNode *sn, float x, float z, float fallback);

// Center and XZ half-extents of the out-of-bounds box.
void Weather_PlayBox(StageNode *sn, float *cx, float *cz, float *hx, float *hz);

// Any live rider, for the engine calls that assert on a null owner. NULL if none.
GOBJ *Weather_FindDonorRider(void);

// Spread `n` events over the round, one per equal slice with jitter inside it, and
// return the first entry still ahead of progress `p` - so re-planning mid-round
// does not replay one.
int Weather_SeedSchedule(float *out, int n, float p);

// Advance `d` by `v`, wrapping into [0, box). Requires |v| < box.
float Weather_WrapStep(float d, float v, float box);

void Weather_SetAllEnabled(int *mask, int n, int val);

// Create a world-camera GX layer GObj; NULL on failure. `tag` names the layer in
// the one pool-exhausted warning.
GOBJ *WeatherGX_EnsureLayer(int entity_class, int p_link, void *cb, const char *tag);

// Anchor a unit sky direction at P = eye + dir * dist, with dist the smallest of
// `want`, `dome_frac` of the eye-to-dome distance, and `maxd`. Returns that dist,
// which the caller scales apparent size by.
float WeatherGX_PlaceOnDome(const Vec3 *dir, const Vec3 *eye, float e2,
                            float want, float dome_frac, float maxd, Vec3 *P);

#endif // WEATHER_FX_H
