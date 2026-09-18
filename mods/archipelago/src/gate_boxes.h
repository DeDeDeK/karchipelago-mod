#ifndef GATE_BOXES_H
#define GATE_BOXES_H

#include "item.h"

void GateBoxes_OnBoot();
int GateBoxes_UnlockBox(BoxKind kind);

// 1 when a box color's unlock bit is set. Read by the legendary-piece gates, whose
// carrier box never reaches the color picker.
int GateBoxes_IsUnlocked(BoxKind kind);

#endif
