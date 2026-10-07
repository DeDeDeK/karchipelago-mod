#ifndef ARCHIPELAGO_PERMANENT_PATCH_H
#define ARCHIPELAGO_PERMANENT_PATCH_H

#include "item.h"

// Record a permanent patch in the save; the stats land at the next round start.
int PermanentPatch_GiveItem(PatchKind kind);
int PermanentPatch_GiveAllUp(void);

// Applies the accumulated permanent patches to every human rider once the intro ends.
void PermanentPatch_On3DLoadEnd(void);

#endif // ARCHIPELAGO_PERMANENT_PATCH_H
