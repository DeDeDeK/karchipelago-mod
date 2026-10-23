#ifndef GATE_BOXES_H
#define GATE_BOXES_H

#include "item.h"

void GateBoxes_OnBoot();
int GateBoxes_UnlockBox(BoxKind kind);
int GateBoxes_IsUnlocked(BoxKind kind);

// A box size rolled off the stage's chance table for one color, or for every color
// together when color < 0. Small when the table is missing or empty.
int GateBoxes_RollSize(int color);

#endif
