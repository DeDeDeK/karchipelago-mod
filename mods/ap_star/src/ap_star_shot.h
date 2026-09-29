#ifndef AP_STAR_SHOT_H
#define AP_STAR_SHOT_H

#include "structs.h"

// A full-charge release fires the pod nearest the machine's heading as a projectile
// of its own kind, wearing that pod's color. The sixth empties the ring and starts a
// 60-frame regrow, during which a release is an ordinary boost. A shot fired from the
// ground follows it; one fired in the air flies straight. Either bends gently toward
// a player ahead of it.

// Registers the shot's projectile kind and hooks the charge release.
void ApStarShot_OnBoot(void);

// Claim the star's Init and Think handler slots.
void ApStarShot_Bind(int kind);

void ApStarShot_OnSceneChange(void);

// Load this scene's shot model.
void ApStarShot_On3DLoadEnd(void);

void ApStarShot_OnFrameStart(void);

#endif // AP_STAR_SHOT_H
