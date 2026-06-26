#include "os.h"
#include "game.h"
#include "stage.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

// On (default) = vanilla: events transition the sky and restore it. Off = keep
// the round's weather through every event.
static char *event_sky_toggle_names[] = {"Off", "On"};
static int event_sky_changes = 1; // default On: events change the sky (vanilla)

// Replaces Sky_TransitionGlobal (0x800d5444). Vanilla: begin a smooth transition
// to the event's themed sky preset. Suppressed: leave the current weather alone.
void EventSky_TransitionGlobal(int preset_index)
{
    if (!event_sky_changes)
        return;
    Sky_BeginTransition(*stc_grobj, preset_index);
}

// Replaces Sky_RestoreGlobal (0x800d546c). Vanilla: transition back to the
// round's stored weather preset (sky state +0x1C). Suppressed: nothing to
// restore, since the transition out was skipped too.
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

OptionDesc event_sky_option = {
    .name = "Event Sky Changes",
    .description = "Let City Trial events (Meteor, Fog, etc.) swap the sky for their duration (Off keeps the current weather through events)",
    .kind = OPTKIND_VALUE,
    .val = &event_sky_changes,
    .value_num = 2,
    .value_names = event_sky_toggle_names,
};
