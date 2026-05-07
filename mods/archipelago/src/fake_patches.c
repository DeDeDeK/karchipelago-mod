#include "event.h"
#include "item.h"
#include "stage.h"
#include "code_patch/code_patch.h"

#include "fake_patches.h"

// Replacement for CityItem_ProcessFakeItem (0x802542dc).
//
// Vanilla returns 0 unless the in-game Fake Powerups event is active
// (stc_city_item_mgr->fake_event_data != NULL), in which case the surrounding
// caller (Machine_OnTouchItem at 0x801db8c0) skips the bl Machine_ApplyHurt
// and the fake patch silently does nothing. AP traps spawn ITKIND_*FAKE
// items outside the event, so we look up the fake-data table from the loaded
// event archive directly.
//
// We read it from GrData.event_config rather than *stc_eventcheck_gobj because
// the event GOBJ is never created when CT events are disabled in the menu —
// CityEvent_Init bails when Gm_CheckEnemyEnabled returns 0 — but the archive
// is loaded unconditionally by fn_grSetupCityEventData on every City Trial
// load, so the bgm_sky table is always available.
static int ProcessFakeItem(GOBJ *item_gobj, void *hurt_params)
{
    GrObj *gr = stc_grobj ? *stc_grobj : 0;
    if (!gr || !gr->gr_data)
        return 0;
    EventConfigData *cfg = gr->gr_data->event_config;
    if (!cfg || !cfg->bgm_sky)
        return 0;

    void *fake_data = cfg->bgm_sky[EVKIND_FAKEPOWERUPS].event_data;
    if (!fake_data)
        return 0;

    Event_FakeItems_FillHurtParams(fake_data, hurt_params);
    return 1;
}

void FakePatches_OnBoot()
{
    CODEPATCH_REPLACEFUNC(CityItem_ProcessFakeItem, ProcessFakeItem);
}
