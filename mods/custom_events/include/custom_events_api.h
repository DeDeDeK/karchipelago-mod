#ifndef CUSTOM_EVENTS_API_H
#define CUSTOM_EVENTS_API_H

#include "event.h"

// Pass to Hoshi_ImportMod with the version macros below to resolve this API.
#define CUSTOM_EVENTS_MOD_NAME "custom_events"

// Bump major on breaking changes, minor on additions.
#define CUSTOM_EVENTS_API_MAJOR 2
#define CUSTOM_EVENTS_API_MINOR 10

// Custom kinds continue after vanilla EVKIND_NUM.
typedef enum CustomEventKind
{
    CUSTOM_EVKIND_WADDLE_DEE_SWARM = EVKIND_NUM,
    CUSTOM_EVKIND_GRAVITY_CHANGE,
    CUSTOM_EVKIND_SCALE_CHANGE,
    CUSTOM_EVKIND_GOURMET_RACE,
    CUSTOM_EVKIND_PLANET,
    CUSTOM_EVKIND_GIANT_MODE,
    CUSTOM_EVKIND_TINY_MODE,
    CUSTOM_EVKIND_TRUCK_STORM,
    CUSTOM_EVKIND_CANNON,
    CUSTOM_EVKIND_MINEFIELD,
    CUSTOM_EVKIND_DANGER_RAIN,
    CUSTOM_EVKIND_JUMPING_ITEMS,
    CUSTOM_EVKIND_STAT_SHUFFLE,
    CUSTOM_EVKIND_MINI_UFOS,
    CUSTOM_EVKIND_TREASURE_RAIN,
    CUSTOM_EVKIND_BOMB_TACS,
    CUSTOM_EVKIND_DRIVE_BACKWARDS,
    CUSTOM_EVKIND_REVERSE_CONTROLS,
    CUSTOM_EVKIND_SKY_RINGS,
    CUSTOM_EVKIND_NUM
} CustomEventKind;

// What Danger Rain drops; each start picks one at random.
typedef enum DangerRainKind
{
    DANGER_RAIN_METEOR,
    DANGER_RAIN_GORDO,
    DANGER_RAIN_SENSOR_BOMB,
    DANGER_RAIN_BOMB,
    DANGER_RAIN_DRAGON,
    DANGER_RAIN_NUM
} DangerRainKind;

typedef struct CustomEventsAPI
{
    // Starts a custom event now. Returns 0 if the kind is out of range, the scene is
    // not a City Trial round with events on, an event is already running, or the
    // event cannot run this round.
    int (*Do)(int kind);

    // Starts Danger Rain now, raining the given DangerRainKind. Returns 0 like Do, or
    // for a kind out of range.
    int (*DoDangerRain)(int rain);
} CustomEventsAPI;

#endif // CUSTOM_EVENTS_API_H
