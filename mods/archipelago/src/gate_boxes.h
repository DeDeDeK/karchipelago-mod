#ifndef GATE_BOXES_H
#define GATE_BOXES_H

#include "item.h"

void GateBoxes_OnBoot();
void GateBoxes_UpdateItemAvailability();
int GateBoxes_UnlockBox(BoxKind kind);

#endif
