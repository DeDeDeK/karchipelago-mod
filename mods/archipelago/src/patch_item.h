#ifndef PATCH_ITEM_H
#define PATCH_ITEM_H

#include "item.h"
#include "obj.h"

int Patch_GiveItem(PatchKind kind, int num);
int Patch_AllUp_GiveItem(int num);

int PermanentPatch_GiveItem(PatchKind kind);
int PermanentPatch_GiveAllUp();
void PermanentPatch_On3DLoadEnd();

int Patch_DropTrap();

#endif
