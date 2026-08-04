#ifndef ENERGYLINK_H
#define ENERGYLINK_H

void EnergyLink_On3DLoadEnd();
void EnergyLink_OnTopRideLoadEnd();

// Credit the local energy balance without touching the send accumulator. Debug
// path for simulating received energy.
void EnergyLink_Deposit(float amount);

// Re-snap a player's stats baseline so a current-stat increase is invisible to
// the next frame's send delta, keeping received patches from refunding energy.
void EnergyLink_RebaseStats(int ply);

#endif // ENERGYLINK_H
