#ifndef GATE_ITEMS_H
#define GATE_ITEMS_H

#include "archipelago_api.h"

void GateItems_OnBoot();
// 1 if it_kind is individually gated and still locked.
int GateItems_IsItemLocked(u8 it_kind);
void GateItems_EnsureAllUpInSpawnPools();
int GateItems_UnlockItem(ItemUnlockKind kind);

#endif
