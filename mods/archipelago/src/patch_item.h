#ifndef PATCH_ITEM_H
#define PATCH_ITEM_H

#include "item.h"
#include "obj.h"

// The nine "+1" patches and their PatchKind; PATCHKIND_NUM for any other ItemKind.
ItemKind PatchItem_PatchKindToItKind(PatchKind kind);
PatchKind PatchItem_ItKindToPatchKind(ItemKind it_kind);

// Each returns how many human riders it reached; 0 while every one is on foot.
int PatchItem_Give(PatchKind kind);
int PatchItem_GiveAllUp(int num);

// Ejects each human rider's stats as patches. Open City Trial only: elsewhere
// Rider_DropPatches crashes spawning the patch items.
int PatchItem_DropTrap(void);

#endif
