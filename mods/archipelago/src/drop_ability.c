#include "game.h"
#include "rider.h"
#include "topride.h"
#include "hsd.h"
#include "os.h"
#include "inline.h"

#include "drop_ability.h"
#include "gate_topride_items.h"

static void DropAbility_PerFrame(GOBJ *g)
{
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;

        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;

        RiderData *rd = rg->userdata;
        if (rd->copy_kind == COPYKIND_NONE)
            continue;

        // input.down is the rising edge.
        if (rd->input.down & PAD_TRIGGER_Z)
        {
            OSReport("[DropAbility] Player %d dropped %s\n", i + 1, CopyKind_Names[rd->copy_kind]);
            // The engine's expiry sequence. The remove clears copy_kind; the state enter
            // only animates.
            Rider_AbilityRemoveModel(rd);
            RiderState_LoseAbilityEnter(rd);
        }
    }
}

void DropAbility_On3DLoadEnd(void)
{
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, DropAbility_PerFrame, 0, 0, 0, 0);
}

// An active Top Ride ability power shows as its item vtable on state_handler; dropping
// runs the engine's expiry revert, TopRide_KirbyNormal.

static const char *DropAbility_TopRidePowerName(void *state_vt)
{
    for (int i = 0; i < TR_ABILITY_ITEM_NUM; i++)
    {
        if (state_vt == topride_ability_items[i].power_vt)
            return TopRideItemKind_Names[topride_ability_items[i].item];
    }
    return 0;
}

static void DropAbility_TopRidePerFrame(GOBJ *g)
{
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    // kirbys[] and state_handler are wired only once the race is active.
    if (!mgr || mgr->round_state != 2)
        return;

    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *k = mgr->kirbys[i];
        if (!k || !k->state_handler)
            continue;

        if (TopRide_GetPlayerKind(k->player_slot) != TR_PKIND_HMN)
            continue;

        const char *power = DropAbility_TopRidePowerName(TopRide_KirbyStateVtable(k));
        if (!power)
            continue;

        u8 port = Gm_GetGameData()->topride_config.slots[k->player_slot].controller_port;
        if (port >= 4)
            continue;

        if (stc_engine_pads[port].down & PAD_TRIGGER_Z)
        {
            OSReport("[DropAbility] Top Ride player %d dropped %s\n", i + 1, power);
            TopRide_KirbyNormal(k);
            k->active_item_kind = 0xFF;
        }
    }
}

void DropAbility_OnTopRideLoadEnd(void)
{
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, DropAbility_TopRidePerFrame, 0, 0, 0, 0);
}
