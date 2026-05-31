#ifndef GATE_MACHINES_H
#define GATE_MACHINES_H

#include "machine.h"

void GateMachines_OnBoot();
int GateMachines_UnlockMachine(MachineKind kind, int announce);
int GateMachines_GiveLegendaryMachine(int machine_index);

#endif
