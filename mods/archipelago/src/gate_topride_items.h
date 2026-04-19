#ifndef GATE_TOPRIDE_ITEMS_H
#define GATE_TOPRIDE_ITEMS_H

#include "item.h"

void GateTopRideItems_OnBoot();
void GateTopRideItems_ApplyMask();
int GateTopRideItems_UnlockItem(TopRideItemKind kind);

// Spawn a Top Ride item at each human Kirby's position so it is collected on
// the next collision tick. Only valid in a Top Ride scene with the item
// manager initialized. Returns 1 if spawned at least once, 0 otherwise.
int GateTopRideItems_GiveItem(TopRideItemKind kind);

#endif
