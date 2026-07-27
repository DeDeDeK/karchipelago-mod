#ifndef GATE_TOPRIDE_ITEMS_H
#define GATE_TOPRIDE_ITEMS_H

#include "item.h"
#include "rider.h"

void GateTopRideItems_OnBoot();
void GateTopRideItems_ApplyMask();
int GateTopRideItems_UnlockItem(TopRideItemKind kind, int announce);

// Applies the item to every human Kirby directly. Only valid in a Top Ride scene with
// the item manager initialized; returns 1 if applied at least once.
int GateTopRideItems_GiveItem(TopRideItemKind kind);

// Maps a copy ability to its Top Ride item analog, or -1 if it has none.
int GateTopRideItems_AbilityToItem(CopyKind ability);

#endif
