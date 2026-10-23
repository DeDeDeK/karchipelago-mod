#ifndef GATE_MACHINES_H
#define GATE_MACHINES_H

#include "machine.h"

void GateMachines_OnBoot();
void GateMachines_On3DLoadEnd(void);

// custom_machines' select-screen availability and City Trial spawn-weight filters.
int GateMachines_FilterSelectCharacter(int ckind, int default_available);
float GateMachines_SpawnWeight(int kind, float default_weight);

// The Top Ride control types (Free / Steer Star).
#define TR_MACHINE_BITS ((1u << VCKIND_FREE) | (1u << VCKIND_STEER))

// `bit` is a machine unlock mask bit: a vanilla MachineKind or AP_MACHINE_BIT_AP_STAR.
int GateMachines_UnlockMachine(int bit, int announce);

// Runs a human through a legendary's assembly; returns an APItemResult.
int GateMachines_GiveLegendaryMachine(int machine_index);

#endif
