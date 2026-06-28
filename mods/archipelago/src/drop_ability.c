#include "game.h"
#include "rider.h"
#include "hsd.h"
#include "os.h"
#include "inline.h"

#include "drop_ability.h"
#include "settings_menu.h"

// Optional control (off by default): pressing Z while holding a copy ability
// discards it. Routes through Rider_LoseAbilityState_Enter (action-state 0x68) -
// the same drop the engine runs when an ability's timer expires (abilityTimer_*
// call it on timeout) - so the spit-out animation, VFX, and copy_kind reset all
// happen exactly as in vanilla. Top Ride has no copy abilities, so the applier is
// only registered for 3D scenes (City Trial / Air Ride) via On3DLoadEnd.

static void DropAbility_PerFrame(GOBJ *g)
{
    if (!ap_menu_settings.drop_ability_enabled)
        return;

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;

        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;

        RiderData *rd = rg->userdata;
        if (rd->copy_kind == COPYKIND_NONE)
            continue;

        // input.down = buttons newly pressed this frame (edge), so each press
        // drops once rather than re-triggering every frame Z is held.
        if (rd->input.down & PAD_TRIGGER_Z)
        {
            OSReport("[DropAbility] Player %d dropped %s\n", i + 1, CopyKind_Names[rd->copy_kind]);
            Rider_LoseAbilityState_Enter(rd);
        }
    }
}

void DropAbility_On3DLoadEnd(void)
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DropAbility_PerFrame, 0, 0, 0, 0);
}
