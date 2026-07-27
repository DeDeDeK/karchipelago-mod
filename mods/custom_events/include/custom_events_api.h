#ifndef CUSTOM_EVENTS_API_H
#define CUSTOM_EVENTS_API_H

#include "event.h"

// Pass to Hoshi_ImportMod with the version macros below to resolve this API.
#define CUSTOM_EVENTS_MOD_NAME "custom_events"

// Bump major on breaking changes, minor on additions.
#define CUSTOM_EVENTS_API_MAJOR 1
#define CUSTOM_EVENTS_API_MINOR 0

// Custom kinds start after vanilla EVKIND_NUM (16). They are stored in
// ev_chk->cur_kind but must never index a vanilla 16-entry per-kind array
// (EventFunction, occurrence_count, ...).
typedef enum CustomEventKind
{
    CUSTOM_EVKIND_WADDLE_DEE_SWARM = EVKIND_NUM, // 16
    CUSTOM_EVKIND_GRAVITY_CHANGE,    // 17
    CUSTOM_EVKIND_SCALE_CHANGE,      // 18
    CUSTOM_EVKIND_GOURMET_RACE,      // 19
    CUSTOM_EVKIND_NUM
} CustomEventKind;

#define CUSTOM_EVENT_COUNT (CUSTOM_EVKIND_NUM - EVKIND_NUM)

typedef struct CustomEventParam
{
    int duration;       // frames in state 2
    int is_siren;       // play siren + fade music + change sky
    int sky_preset;     // sky transition (-1 = no change)
    int bgm_file;       // secondary BGM file index (0 = no music)
    int weight;         // selection weight for natural occurrence (0 = never naturally occurs)
    const char *label;    // short name for menus/notifications ("Waddle Dee Swarm")
    const char *hud_text; // HUD popup text ("Waddle Dee swarm incoming!")
} CustomEventParam;

// Gates custom events during the extended roll: given a 0-based event index and
// its default weight, returns the weight to use (0 = disabled).
typedef int (*CustomEventWeightFilter)(int event_index, int default_weight);

typedef struct CustomEventsAPI
{
    // Returns 1 on success, 0 on failure.
    int (*Do)(int kind);

    const CustomEventParam *params;  // read-only, CUSTOM_EVENT_COUNT entries
    int event_count;

    // One filter at a time; NULL removes it and restores default weights.
    void (*SetWeightFilter)(CustomEventWeightFilter filter);
} CustomEventsAPI;

#endif // CUSTOM_EVENTS_API_H
