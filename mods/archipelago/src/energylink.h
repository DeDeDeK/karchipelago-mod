#ifndef ENERGYLINK_H
#define ENERGYLINK_H

void EnergyLink_On3DLoadEnd();
void EnergyLink_OnTopRideLoad();

// Emit a fractional withdrawal into the cumulative send counter (the client
// reads-and-diffs it and forwards the delta to the server) AND immediately
// decrement the local balance by whole MJ so affordability gates self-limit.
// Used by Auto-Charge for its per-frame fractional spends. See the definition
// for the rationale. (Integer menu purchases bypass this and subtract from the
// counter and balance directly — see energylink_spend.c Buy.)
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
