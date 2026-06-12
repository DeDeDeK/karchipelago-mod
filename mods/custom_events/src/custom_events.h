#ifndef CUSTOM_EVENTS_H
#define CUSTOM_EVENTS_H

#include "custom_events_api.h"

typedef struct CustomEventFunc
{
    void (*start)(EventCheckData *ev_chk);
    void (*active)(EventCheckData *ev_chk);
    void (*end)(EventCheckData *ev_chk);
    void (*end2)(EventCheckData *ev_chk);
    int (*check)(EventCheckData *ev_chk);
} CustomEventFunc;

// Custom event parameters (indexed by kind - EVKIND_NUM).
extern CustomEventParam custom_params[CUSTOM_EVENT_COUNT];

// Called from OnBoot to install state handler wrappers and export the API.
void CustomEvents_OnBoot(void);

// Called from On3DLoadEnd when in City Trial to pre-compose SIS text
// entries and extend the SIS pointer array.
void CustomEvents_InitSis(void);

// Trigger a custom event. Returns 1 on success, 0 if event system
// is not ready or another event is already active.
int CustomEvent_Do(int kind);

#endif // CUSTOM_EVENTS_H
