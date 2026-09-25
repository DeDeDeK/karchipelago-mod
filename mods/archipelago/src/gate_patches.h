#ifndef GATE_PATCHES_H
#define GATE_PATCHES_H

#include "item.h"

// 1 if it_kind belongs to a stat patch that is still locked.
int GatePatches_IsItemLocked(u8 it_kind);
int GatePatches_UnlockPatch(PatchKind kind);

#endif
