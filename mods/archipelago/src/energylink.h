#ifndef ENERGYLINK_H
#define ENERGYLINK_H

#include "structs.h"

void EnergyLink_PerFrame(GOBJ *rg);
void EnergyLink_On3DLoadEnd();

// Queue a withdrawal through the send accumulator.
// The next flush will include this amount as a negative delta.
void EnergyLink_Withdraw(float amount);

#endif // ENERGYLINK_H
