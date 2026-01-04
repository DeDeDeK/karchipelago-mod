#ifndef PATCH_ITEM_H
#define PATCH_ITEM_H

#include "item.h"
#include "obj.h"

GOBJ *Patch_SpawnItem(ItemKind item_kind, Vec3 *pos);

void Patch_DebugSpawnItem(SpawnItem *s);

int Patch_GiveItem(PatchKind kind, int num);
int Patch_AllUp_GiveItem(int num);

#endif