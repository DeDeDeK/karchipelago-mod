#ifndef GATE_EVENTS_H
#define GATE_EVENTS_H

#include "event.h"

void GateEvents_OnBoot();
void GateEvents_LogEnabledEvents(void);
int GateEvents_UnlockEvent(int kind);

#endif
