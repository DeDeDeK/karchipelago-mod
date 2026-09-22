#ifndef GATE_MACHINES_H
#define GATE_MACHINES_H

#include "machine.h"

void GateMachines_OnBoot();

// The filters custom_machines gates through, registered once the registry resolves:
// who gets a select-screen icon, and what a kind weighs in the City Trial field
// spawn roll.
int GateMachines_FilterSelectCharacter(int ckind, int default_available);
float GateMachines_SpawnWeight(int kind, float default_weight);

// The two Top Ride control types. One unlock mask covers all three modes, but the AP
// world only ships these two when Top Ride is in the seed.
#define TR_MACHINE_BITS ((1u << VCKIND_FREE) | (1u << VCKIND_STEER))

// `bit` is a machine unlock mask bit: a vanilla MachineKind or AP_MACHINE_BIT_AP_STAR.
int GateMachines_UnlockMachine(int bit, int announce);
int GateMachines_GiveLegendaryMachine(int machine_index);

#endif
