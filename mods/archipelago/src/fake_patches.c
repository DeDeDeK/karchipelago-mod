#include "event.h"
#include "item.h"
#include "stage.h"
#include "code_patch/code_patch.h"

#include "fake_patches.h"

// Replacement for ItemGObj_ProcessFakeItem (0x802542dc). Vanilla no-ops the fake
// patch while CityItemMgr.fake_event_data is NULL, which holds until the scene's
// first Fake Powerups event starts (the event's end leaves it set). AP traps spawn
// ITKIND_*FAKE items without that event, so read the fake-data table from the
// loaded event archive directly. It comes from GrData.event_config rather than
// *stc_eventcheck_gobj because the event GOBJ is absent when CT events are
// disabled while the archive, loaded every CT load, still has the table.
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
    CODEPATCH_REPLACEFUNC(ItemGObj_ProcessFakeItem, ProcessFakeItem);
}
