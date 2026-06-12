#ifndef GATE_ITEMS_H
#define GATE_ITEMS_H

#include "archipelago_api.h"  // ItemUnlockKind / ITUNLOCK_*

void GateItems_OnBoot();
void GateItems_FilterSpawnTables();
void GateItems_FilterEventDropTables();
void GateItems_EnsureAllUpInSpawnPools();
int GateItems_UnlockItem(ItemUnlockKind kind);

#endif
