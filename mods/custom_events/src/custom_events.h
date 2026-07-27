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

// Indexed by kind - EVKIND_NUM.
extern CustomEventParam custom_params[CUSTOM_EVENT_COUNT];

// Installs the state handler wrappers and exports the API. Call once at boot.
void CustomEvents_OnBoot(void);

// Pre-composes the custom SIS text and extends the SIS pointer array.
// Call on City Trial load.
void CustomEvents_InitSis(void);

// Returns 1 on success, 0 if the event system is not ready or another event
// is already active.
int CustomEvent_Do(int kind);

#endif // CUSTOM_EVENTS_H
