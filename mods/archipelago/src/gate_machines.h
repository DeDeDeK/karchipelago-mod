#ifndef GATE_MACHINES_H
#define GATE_MACHINES_H

#include "machine.h"

void GateMachines_OnBoot();
void GateMachines_FilterSelectList();
int GateMachines_UnlockMachine(MachineKind kind);

#endif
