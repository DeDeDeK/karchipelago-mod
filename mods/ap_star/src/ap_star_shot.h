#ifndef AP_STAR_SHOT_H
#define AP_STAR_SHOT_H

#include "structs.h"

// A full-charge release fires the pod nearest the machine's heading, wearing that
// pod's color. The sixth empties the ring and starts a 60-frame regrow, during which
// a release is an ordinary boost. A shot fired from the ground follows it; one fired
// in the air flies straight.

void ApStarShot_OnBoot(void);
void ApStarShot_OnFrameStart(void);

// Load this scene's shot model and claim the machine's per-kind handler slots.
void ApStarShot_On3DLoadEnd(void);

// 1 if this projectile GObj is a sphere shot.
int ApStarShot_IsShot(GOBJ *proj);

#endif // AP_STAR_SHOT_H
