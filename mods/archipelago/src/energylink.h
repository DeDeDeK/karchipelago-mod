#ifndef ENERGYLINK_H
#define ENERGYLINK_H

void EnergyLink_On3DLoadEnd();
void EnergyLink_OnTopRideLoad();

// Queue a withdrawal through the send accumulator.
// The next flush will include this amount as a negative delta.
void EnergyLink_Withdraw(float amount);

// Credit `amount` to the local energy balance without touching the
// send accumulator. The polling logic treats this as an AP-side credit
// on the next tick. Used by debug paths to simulate received energy.
void EnergyLink_Deposit(float amount);

// Re-snap a player's stats baseline so any current-stat increase is invisible
// to the next frame's send-delta calculation. Used by patch delivery to
// prevent received stat patches from refunding energy back into the pool.
void EnergyLink_RebaseStats(int ply);

#endif // ENERGYLINK_H
