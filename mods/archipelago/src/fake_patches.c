#include "event.h"
#include "item.h"
#include "code_patch/code_patch.h"

#include "fake_patches.h"

// Replacement for CityItem_ProcessFakeItem (0x802542dc).
//
// Vanilla returns 0 unless the in-game Fake Powerups event is active
// (stc_city_item_mgr->fake_event_data != NULL), in which case the surrounding
// caller (Machine_OnTouchItem at 0x801db8c0) skips the bl Machine_ApplyHurt
// and the fake patch silently does nothing. AP traps spawn ITKIND_*FAKE
// items outside the event, so we look up the same fake-data table directly
// from the event registry, which is loaded for the whole match.
//
// Note: stc_city_item_mgr->fake_event_data and the event-table lookup return
// the same pointer (CityItem_InitFakeEvent copies it from the event into
// the manager). Reading the event table works in both cases.
static int ProcessFakeItem(GOBJ *item_gobj, void *hurt_params)
{
    GOBJ *ev_gobj = *stc_eventcheck_gobj;
    if (!ev_gobj)
        return 0;
    EventCheckData *ev = ev_gobj->userdata;
    if (!ev || !ev->data || !ev->data->bgm_sky)
        return 0;

    void *fake_data = ev->data->bgm_sky[EVKIND_FAKEPOWERUPS].event_data;
    if (!fake_data)
        return 0;

    Event_FakeItems_FillHurtParams(fake_data, hurt_params);
    return 1;
}

void FakePatches_OnBoot()
{
    CODEPATCH_REPLACEFUNC(CityItem_ProcessFakeItem, ProcessFakeItem);
}
