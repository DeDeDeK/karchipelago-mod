#ifndef AP_STAR_SHOT_FX_H
#define AP_STAR_SHOT_FX_H

#include "structs.h"
#include "gx.h"
#include "weapon.h"

void ApStarShotFx_OnBoot(void);

// Forgets every trail, which belonged to the scene just torn down.
void ApStarShotFx_OnSceneChange(void);

// Starts drawing a shot. `radius` is its size at scale 1. Returns the handle
// ApStarShotFx_Detach takes, or 0 with every slot in use or no GObj to draw from.
int ApStarShotFx_Attach(WeaponData *proj, GXColor color, float radius);

// The shot is being destroyed; called once, from its dtor. Its trail drains from where
// it ended.
void ApStarShotFx_Detach(int handle);

#endif // AP_STAR_SHOT_FX_H
