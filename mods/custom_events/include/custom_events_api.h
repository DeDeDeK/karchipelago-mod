#ifndef CUSTOM_EVENTS_API_H
#define CUSTOM_EVENTS_API_H

#include "event.h"

// Pass to Hoshi_ImportMod with the version macros below to resolve this API.
#define CUSTOM_EVENTS_MOD_NAME "custom_events"

// Bump major on breaking changes, minor on additions.
#define CUSTOM_EVENTS_API_MAJOR 2
#define CUSTOM_EVENTS_API_MINOR 0

// Custom kinds continue after vanilla EVKIND_NUM. They are stored in
// ev_chk->cur_kind but must never index a vanilla 16-entry per-kind array.
typedef enum CustomEventKind
{
    CUSTOM_EVKIND_WADDLE_DEE_SWARM = EVKIND_NUM,
    CUSTOM_EVKIND_GRAVITY_CHANGE,
    CUSTOM_EVKIND_SCALE_CHANGE,
    CUSTOM_EVKIND_GOURMET_RACE,
    CUSTOM_EVKIND_NUM
} CustomEventKind;

typedef struct CustomEventsAPI
{
    // Starts a custom event now. Returns 0 if the kind is out of range, the round
    // has no event system, or an event is already running.
    int (*Do)(int kind);
} CustomEventsAPI;

#endif // CUSTOM_EVENTS_API_H
