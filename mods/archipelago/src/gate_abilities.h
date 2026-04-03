#ifndef GATE_ABILITIES_H
#define GATE_ABILITIES_H

#include "rider.h"

void GateAbilities_OnBoot();
void GateAbilities_On3DLoadEnd();
void GateAbilities_FilterSpawnTables();
void GateAbilities_FilterEventDropTables();
int GateAbilities_UnlockAbility(CopyKind kind);

#endif
