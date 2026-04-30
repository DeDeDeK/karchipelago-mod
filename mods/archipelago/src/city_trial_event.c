#include "game.h"

#include "main.h"
#include "city_trial_event.h"

// Trigger a specific event. Similar to the game's CityEvent_ForceStart (0x800ee778),
// but adds the event to prev_kind[] history (prevents natural re-occurrence stacking)
// and does not use the reserve queue (AP item handler retries via returning 0 instead).
int Event_Do(EventKind kind)
{
    // Check if event system is initialized
    if (!stc_eventcheck_gobj || !*stc_eventcheck_gobj)
        return 0;

    GOBJ *g = *stc_eventcheck_gobj;
    EventCheckData *ev_chk = g->userdata;

    // Ensure no event is currently active
    if (ev_chk->state != 0)
        return 0;

    // Run event check function
    if ((*stc_event_function)[kind].check &&
        !(*stc_event_function)[kind].check(ev_chk))
        return 0;

    ev_chk->state = 1;                                              // event starting
    ev_chk->cur_kind = kind;                                        // set the next event
    ev_chk->timer = 0;                                              // reset timer
    ev_chk->prev_kind[ev_chk->prev_kind_num] = ev_chk->cur_kind;
    ev_chk->prev_kind_num++;

    if (ev_chk->data->event->param->arr[kind].is_siren)
    {
        Gm_FadeOutMusic(ev_chk->data->event->music_fadeout_frames);    // fade out music
        SFX_Play(0x130002);                                             // event siren

        int sky_preset = ev_chk->data->bgm_sky[kind].sky_preset;
        if (sky_preset != -1)
            Sky_TransitionGlobal(sky_preset);
    }

    return 1;
}

int Event_GiveItem(EventKind kind)
{
    if (Gm_GetCurrentGrKind() == GRKIND_CITY1)
    {
        if (Event_Do(kind))
        {
            OSReport("[CTEvent] Event kind %d triggered\n", kind);
            return 1;
        }
    }
    return 0;
}
