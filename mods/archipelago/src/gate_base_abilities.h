#ifndef GATE_BASE_ABILITIES_H
#define GATE_BASE_ABILITIES_H

#include "archipelago_api.h"

// Install the base-ability gating hooks (inhale, quick spin, charge) across the
// 3D modes and Top Ride. Call once from OnBoot.
void GateBaseAbilities_OnBoot(void);

// Mark a base ability unlocked and announce it. Returns 1.
int GateBaseAbilities_UnlockAbility(BaseAbilityKind kind);

#endif
