#ifndef AP_STAR_SHOT_FX_H
#define AP_STAR_SHOT_FX_H

#include "structs.h"
#include "gx.h"
#include "weapon.h"

// The sphere shot's glow and trail, drawn in immediate-mode GX on the world camera's
// translucent pass: an additive halo around each shot and a ribbon in its color. The
// ribbon outlives the shot and drains into where it ended, and an impact flares the
// halo out.

void ApStarShotFx_OnBoot(void);

// Forgets every trail, which belonged to the scene just torn down.
void ApStarShotFx_OnSceneChange(void);

// Starts drawing a shot. `radius` is its size at scale 1. Returns the handle
// ApStarShotFx_Detach takes, or 0 with every slot in use or no GObj to draw from.
int ApStarShotFx_Attach(WeaponData *proj, GXColor color, float radius);

// The shot is being destroyed. Its trail drains from where it ended.
void ApStarShotFx_Detach(int handle);

#endif // AP_STAR_SHOT_FX_H
