#include "event.h"
#include "item.h"
#include "stage.h"
#include "code_patch/code_patch.h"

#include "fake_patches.h"

// Replacement for CityItem_ProcessFakeItem (0x802542dc). Vanilla no-ops the fake
// patch unless the Fake Powerups event is active, but AP traps spawn ITKIND_*FAKE
// items outside the event, so read the fake-data table from the loaded event
// archive directly. It comes from GrData.event_config rather than
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
    CODEPATCH_REPLACEFUNC(CityItem_ProcessFakeItem, ProcessFakeItem);
}
