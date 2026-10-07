#ifndef GATE_EVENTS_H
#define GATE_EVENTS_H

#include "event.h"

void GateEvents_OnBoot();
int GateEvents_UnlockEvent(int kind);
int GateEvents_IsUnlocked(int kind);

#endif
