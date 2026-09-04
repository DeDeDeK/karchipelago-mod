#ifndef PATCH_ITEM_H
#define PATCH_ITEM_H

#include "item.h"
#include "obj.h"

PatchKind Patch_ItKindToPatchKind(ItemKind it_kind);

// Both return the number of human riders they reached, 0 while every one of them
// is on foot and has no machine to give to.
int Patch_GiveItem(PatchKind kind, int num);
int Patch_AllUp_GiveItem(int num);

int PermanentPatch_GiveItem(PatchKind kind);
int PermanentPatch_GiveAllUp();
void PermanentPatch_On3DLoadEnd();

int Patch_DropTrap();

#endif
