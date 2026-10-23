#include "event.h"
#include "os.h"
#include "item.h"
#include "stage.h"
#include "code_patch/code_patch.h"

#include "fake_patches.h"

// Replaces ItemGObj_ProcessFakeItem (0x802542dc), which no-ops until the scene's first
// Fake Powerups event, so AP trap fakes work without one. The table comes from
// GrData.event_config, loaded even with City Trial events off.
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
    OSReport("[FakePatches] Hooks installed\n");
}
