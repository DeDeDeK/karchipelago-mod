#ifndef CUSTOM_EVENTS_API_H
#define CUSTOM_EVENTS_API_H

#include "event.h"

// API version: bump major on breaking changes, minor on additions.
#define CUSTOM_EVENTS_API_MAJOR 1
#define CUSTOM_EVENTS_API_MINOR 0

// Custom event kinds start after vanilla EVKIND_NUM (16).
// These values are stored in ev_chk->cur_kind but never index into
// vanilla fixed-size arrays (EventFunction, occurrence_count, etc.).
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

// Weight filter callback for gating custom events during the extended roll.
// Receives: event index (0-based into custom event array), default weight.
// Returns: filtered weight (0 = disabled, >0 = enabled with that weight).
// If no filter is installed, default weights are used (standalone mode).
typedef int (*CustomEventWeightFilter)(int event_index, int default_weight);

typedef struct CustomEventsAPI
{
    // Trigger a custom event by kind. Returns 1 on success, 0 on failure.
    int (*Do)(int kind);

    // Read-only access to event parameters.
    const CustomEventParam *params;  // array of CUSTOM_EVENT_COUNT
    int event_count;

    // Install a weight filter for gating. Only one filter at a time.
    // Pass NULL to remove.
    void (*SetWeightFilter)(CustomEventWeightFilter filter);
} CustomEventsAPI;

#endif // CUSTOM_EVENTS_API_H
