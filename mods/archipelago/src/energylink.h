#ifndef ENERGYLINK_H
#define ENERGYLINK_H

#include "main.h"

void EnergyLink_On3DLoadEnd();
void EnergyLink_OnTopRideLoadEnd();

// The local balance in whole MJ, and a debug override of the balance alone: the deposit
// and withdraw totals are rising counters the client diffs, so they are never lowered.
s64 EnergyLink_GetBalance(void);
void EnergyLink_DebugSetBalance(s64 mj);

// Re-snaps a player's stats baseline so a stat increase is invisible to the next frame's
// send delta, keeping received patches from refunding energy.
void EnergyLink_RebaseStats(int ply);

#endif // ENERGYLINK_H
