#ifndef CUSTOM_EVENTS_H
#define CUSTOM_EVENTS_H

#include "event.h"

// Custom event kinds start after vanilla EVKIND_NUM (16).
// These values are stored in ev_chk->cur_kind but never index into
// vanilla fixed-size arrays (EventFunction, occurrence_count, etc.).
typedef enum CustomEventKind
{
    CUSTOM_EVKIND_TEST = EVKIND_NUM, // 16
    CUSTOM_EVKIND_GRAVITY_CHANGE,    // 17
    CUSTOM_EVKIND_SCALE_CHANGE,      // 18
    CUSTOM_EVKIND_NUM
} CustomEventKind;

#define CUSTOM_EVENT_COUNT (CUSTOM_EVKIND_NUM - EVKIND_NUM)

typedef struct CustomEventParam
{
    int duration;     // frames in state 2
    int is_siren;     // play siren + fade music + change sky
    int sky_preset;   // sky transition (-1 = no change)
    int bgm_file;     // secondary BGM file index (0 = no music)
    const char *name; // HUD display name
} CustomEventParam;

typedef struct CustomEventFunc
{
    void (*start)(EventCheckData *ev_chk);
    void (*active)(EventCheckData *ev_chk);
    void (*end)(EventCheckData *ev_chk);
    void (*end2)(EventCheckData *ev_chk);
    int (*check)(EventCheckData *ev_chk);
} CustomEventFunc;

// Called from OnBoot to install state handler wrappers.
void CustomEvents_OnBoot(void);

// Called from On3DLoadEnd when in City Trial to pre-compose SIS text
// entries and extend the SIS pointer array.
void CustomEvents_InitSis(void);

// Trigger a custom event. Returns 1 on success, 0 if event system
// is not ready or another event is already active.
int CustomEvent_Do(int kind);

#endif // CUSTOM_EVENTS_H
