#include "os.h"
#include "game.h"
#include "stage.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

static int event_sky_changes = 1;

// Reimplements Sky_TransitionGlobal (0x800d5444), the event-start wrapper.
void EventSky_TransitionGlobal(int preset_index)
{
    if (!event_sky_changes)
        return;
    Sky_BeginTransition(*stc_grobj, preset_index);
}

// Reimplements Sky_RestoreGlobal (0x800d546c), the event-end wrapper that transitions
// back to the round's stored preset.
void EventSky_RestoreGlobal(void)
{
    if (!event_sky_changes)
        return;
    Sky_ApplyStoredIndex(*stc_grobj);
}

void EventSky_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(Sky_TransitionGlobal, EventSky_TransitionGlobal);
    CODEPATCH_REPLACEFUNC(Sky_RestoreGlobal, EventSky_RestoreGlobal);
    OSReport("[EventSky] Event sky-transition override installed\n");
}

static void OnEventSkyChange(int val)
{
    OSReport("[EventSky] Event sky changes %s\n", weather_onoff_names[val]);
}

OptionDesc event_sky_option = {
    .name = "Event Sky Changes",
    .description = "Let City Trial events swap the sky while they run (Off keeps the current weather through events)",
    .kind = OPTKIND_VALUE,
    .val = &event_sky_changes,
    .value_num = 2,
    .value_names = weather_onoff_names,
    .on_change = OnEventSkyChange,
};
