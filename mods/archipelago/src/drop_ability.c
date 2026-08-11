#include "game.h"
#include "rider.h"
#include "topride.h"
#include "hsd.h"
#include "os.h"
#include "inline.h"

#include "drop_ability.h"
#include "settings_menu.h"

// Pressing Z discards the held copy ability, reproducing the engine's expiry
// sequence. The remove must come first: the state enter alone only animates and
// never clears copy_kind.

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

        // input.down is the rising edge, so each press drops once. The teardown
        // clears copy_kind, so the guard above also blocks a re-fire.
        if (rd->input.down & PAD_TRIGGER_Z)
        {
            OSReport("[DropAbility] Player %d dropped %s\n", i + 1, CopyKind_Names[rd->copy_kind]);
            Rider_AbilityRemoveModel(rd);     // clear copy_kind + poof VFX/SFX + remove model
            Rider_LoseAbilityState_Enter(rd); // spit-out animation
        }
    }
}

void DropAbility_On3DLoadEnd(void)
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DropAbility_PerFrame, 0, 0, 0, 0);
}

// Top Ride's copy-ability analogs are the four timed ability-power items. While
// one is active the kirby's state_handler carries that item's vtable, which is
// how it is detected; dropping reverts to TopRide_KirbyNormal, the same revert
// the engine runs when the power expires or is replaced.

static const char *DropAbility_TopRidePowerName(void *state_vt)
{
    if (state_vt == TR_ITEMPOWER_VT_FIRE)       return "Fire";
    if (state_vt == TR_ITEMPOWER_VT_FREEZE_FAN) return "Freeze Fan";
    if (state_vt == TR_ITEMPOWER_VT_BOMB)       return "Bomb";
    if (state_vt == TR_ITEMPOWER_VT_WALKY)      return "Walky";
    return 0;
}

static void DropAbility_TopRidePerFrame(GOBJ *g)
{
    if (!ap_menu_settings.drop_ability_enabled)
        return;

    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    // kirbys[] are only populated, and state_handler only wired, once the race
    // is active (round_state == 2).
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

        // Rising edge, so each press drops once. The revert flips state_handler
        // off the power vtable, so the check above also blocks a re-fire.
        if (stc_engine_pads[port].down & PAD_TRIGGER_Z)
        {
            OSReport("[DropAbility] Top Ride player %d dropped %s\n", i + 1, power);
            TopRide_KirbyNormal(k);      // exit power state + install KirbyNormal
            k->active_item_kind = 0xFF;  // clear held-item marker
        }
    }
}

void DropAbility_OnTopRideLoadEnd(void)
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DropAbility_TopRidePerFrame, 0, 0, 0, 0);
}
