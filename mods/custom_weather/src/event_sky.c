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
// back to the round's stored preset. With event skies off an event never moved the
// sky, but a legendary machine finished mid-event still stores its preset and leaves
// the transition to this restore (Sky_SetDragoonPreset, 0x800d5490).
void EventSky_RestoreGlobal(void)
{
    GrObj *gr = *stc_grobj;
    if (!event_sky_changes)
    {
        SkyState *ss = (SkyState *)gr->sky_gobj->userdata;
        SkyPresetEntry *presets = gr->gr_data->sky_block->preset_header->preset_array;
        if (ss->target_preset == &presets[ss->current_preset_index])
            return;
    }
    Sky_ApplyStoredIndex(gr);
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
    .value_num = WEATHER_ONOFF_NUM,
    .value_names = weather_onoff_names,
    .on_change = OnEventSkyChange,
};
