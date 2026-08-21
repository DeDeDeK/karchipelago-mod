#ifndef GATE_MACHINES_H
#define GATE_MACHINES_H

#include "machine.h"

void GateMachines_OnBoot();
// The filters custom_machines gates through, registered once the registry resolves:
// who gets a select-screen icon, and what a kind weighs in the City Trial field
// spawn roll.
int GateMachines_FilterSelectCharacter(int ckind, int default_available);
float GateMachines_SpawnWeight(int kind, float default_weight);
int GateMachines_UnlockMachine(MachineKind kind, int announce);
int GateMachines_GiveLegendaryMachine(int machine_index);

#endif
