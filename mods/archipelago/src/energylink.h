#ifndef ENERGYLINK_H
#define ENERGYLINK_H

#include "main.h"

void EnergyLink_On3DLoadEnd();
void EnergyLink_OnTopRideLoadEnd();

// Credit the local energy balance without touching the send accumulator. Debug
// path for simulating received energy.
void EnergyLink_Deposit(float amount);

// The local balance in whole MJ, and a debug override of it that leaves the
// deposit and withdraw totals alone.
s64 EnergyLink_GetBalance(void);
void EnergyLink_DebugSetBalance(s64 mj);

// Re-snap a player's stats baseline so a current-stat increase is invisible to
// the next frame's send delta, keeping received patches from refunding energy.
void EnergyLink_RebaseStats(int ply);

#endif // ENERGYLINK_H
