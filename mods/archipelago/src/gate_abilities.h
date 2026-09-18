#ifndef GATE_ABILITIES_H
#define GATE_ABILITIES_H

#include "rider.h"

void GateAbilities_OnBoot();
void GateAbilities_On3DLoadEnd();
// 1 if it_kind is a copy item whose ability is still locked.
int GateAbilities_IsItemLocked(u8 it_kind);
int GateAbilities_UnlockAbility(CopyKind kind);

#endif
