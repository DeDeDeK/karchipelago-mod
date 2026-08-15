#ifndef GATE_MACHINES_H
#define GATE_MACHINES_H

#include "machine.h"

void GateMachines_OnBoot();
// Availability filter for custom_machines' select-screen packing; registered once
// the registry resolves.
int GateMachines_FilterSelectCharacter(int ckind, int default_available);
// Gates the City Trial select screen directly, for a build without custom_machines.
void GateMachines_OnCustomMachinesAbsent(void);
int GateMachines_UnlockMachine(MachineKind kind, int announce);
int GateMachines_GiveLegendaryMachine(int machine_index);
void GateMachines_On3DLoadEnd(void);

#endif
