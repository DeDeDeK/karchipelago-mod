#ifndef AP_STAR_SHOT_H
#define AP_STAR_SHOT_H

#include "structs.h"

// Registers the shot's projectile kind and hooks the charge release.
void ApStarShot_OnBoot(void);

void ApStarShot_OnSceneChange(void);

// Load this scene's shot model.
void ApStarShot_On3DLoadEnd(void);

#endif // AP_STAR_SHOT_H
