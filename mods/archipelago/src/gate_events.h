#ifndef GATE_EVENTS_H
#define GATE_EVENTS_H

#include "event.h"
#include "custom_events_api.h"

void GateEvents_OnBoot();
void GateEvents_LogEnabledEvents(void);
int GateEvents_UnlockEvent(int kind);
CustomEventWeightFilter GateEvents_GetWeightFilter(void);

#endif
