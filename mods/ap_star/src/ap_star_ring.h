#ifndef AP_STAR_RING_H
#define AP_STAR_RING_H

#include "structs.h"

// Claim the star's Init and Think handler slots.
void ApStarRing_Bind(int kind);

// Every ring's joints belong to the scene heap that was just reset.
void ApStarRing_OnSceneChange(void);

// The pod nearest the star's heading and its world position, or -1 for a machine that
// is not the star or whose ring is regrowing or not built yet.
int ApStarRing_AimPod(MachineData *md, Vec3 *muzzle);

// Collapse a pod ApStarRing_AimPod has just handed out for this machine.
void ApStarRing_Spend(MachineData *md, int pod);

#endif // AP_STAR_RING_H
