#include "game.h"
#include "inline.h"

#include "main.h"
#include "city_trial_event.h"
#include "textbox.h"
#include "item_queue.h"

// TODO: this triggers the event successfully, but the event does not
// happen, even after the in-game notification appears
int Event_Do(EventKind kind)
{
    // check if event system is initialized
    if (!stc_eventcheck_gobj || !*stc_eventcheck_gobj)
        return 0;

    GOBJ *g = *stc_eventcheck_gobj;
    EventCheckData *ev_chk = g->userdata;

    // ensure no event is currently active
    if (ev_chk->state != 0)
        return 0;

    // run event check function
    if ((*stc_event_function)[kind].check &&
        !(*stc_event_function)[kind].check(ev_chk))
        return 0;

    ev_chk->state = 1;                                              // event starting
    ev_chk->cur_kind = kind;                                        // set the next event
    ev_chk->timer = 0;                                              // reset timer
    ev_chk->prev_kind[ev_chk->prev_kind_num] = ev_chk->cur_kind;    //
    ev_chk->prev_kind_num++;

    if (ev_chk->data->event->param->arr[kind].is_siren)
    {    
        Gm_FadeOutMusic(ev_chk->data->event->music_fadeout_frames);  // fade out music
        SFX_Play(0x130002); // event siren
    }

    return 1;
}

int Event_GiveItem(APItemKind kind) {
    if (Gm_GetCurrentGrKind() == GRKIND_CITY1) {
        int i = HSD_Randi(15); // exclude EVKIND_NUM
        OSReport("Attempting to trigger a random event: %d\n", i);
        int ev_done = Event_Do(i);
        if (ev_done) {
            OSReport("Event triggered successfully!\n");
            TextBox_AddMessage("Event triggered successfully!");
            return 1;
        } else {
            OSReport("Failed to trigger event\n");
            TextBox_AddMessage("Failed to trigger event");
        }
    }
    return 0;
}
