#include "game.h"

#include "main.h"
#include "city_trial_event.h"

// Like CityEvent_ForceStart (0x800ee778) but without the reserve queue - the AP item
// handler retries by returning 0 instead.
static int Event_Do(EventKind kind)
{
    if (!stc_eventcheck_gobj || !*stc_eventcheck_gobj)
        return 0;

    GOBJ *g = *stc_eventcheck_gobj;
    EventCheckData *ev_chk = g->userdata;

    if (ev_chk->state != 0)
        return 0;

    if ((*stc_event_function)[kind].check &&
        !(*stc_event_function)[kind].check(ev_chk))
        return 0;

    ev_chk->state = 1;
    ev_chk->cur_kind = kind;
    ev_chk->timer = 0;

    if (ev_chk->data->event->param->arr[kind].is_siren)
    {
        Gm_FadeOutMusic(ev_chk->data->event->music_fadeout_frames);
        SFX_PlayFullVolume(EVENT_SIREN_SFX);

        int sky_preset = ev_chk->data->bgm_sky[kind].sky_preset;
        if (sky_preset != -1)
            Sky_TransitionGlobal(sky_preset);
    }

    return 1;
}

int Event_GiveItem(EventKind kind)
{
    if (stGetCurrentStageKind() == STAGEKIND_CITY1)
    {
        if (Event_Do(kind))
        {
            OSReport("[CTEvent] Event kind %d triggered\n", kind);
            return 1;
        }
    }
    return 0;
}
