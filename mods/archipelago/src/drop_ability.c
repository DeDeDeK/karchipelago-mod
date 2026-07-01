#include "game.h"
#include "rider.h"
#include "topride.h"
#include "hsd.h"
#include "os.h"
#include "inline.h"

#include "drop_ability.h"
#include "settings_menu.h"

// Optional control (off by default): pressing Z while holding a copy ability
// discards it. We reproduce the engine's natural ability-loss sequence directly:
//   1. Rider_AbilityRemoveModel - the engine's universal ability teardown (the one
//      it runs to strip the old ability when a new inhale replaces it). It resets
//      copy_kind to COPYKIND_NONE, spawns the "ability lost" poof VFX/SFX, and
//      removes the ability model/hat, invoking whichever per-ability teardown
//      callback that copy_kind installed - so it covers every ability, and
//   2. Rider_LoseAbilityState_Enter - the AS_LoseCopyAbility action-state, for the
//      spit-out animation.
// This mirrors what each ability's timeout handler does (revert, then spit).
// Entering AS_LoseCopyAbility on its own only plays the spit animation - the
// teardown that actually clears copy_kind never runs - which is why the ability
// must be removed via Rider_AbilityRemoveModel first.
//
// Top Ride is a separate object system (TopRideKirby, no RiderData / copy_kind):
// its copy-ability analogs are the four timed ability-power items, dropped by
// reverting the kirby to its neutral state. That path is registered separately
// (DropAbility_OnTopRideLoadEnd) and lives at the bottom of this file.

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
        // drops once rather than re-triggering every frame Z is held. The teardown
        // clears copy_kind to COPYKIND_NONE, so the copy_kind guard above also
        // prevents any re-fire.
        if (rd->input.down & PAD_TRIGGER_Z)
        {
            OSReport("[DropAbility] Player %d dropped %s\n", i + 1, CopyKind_Names[rd->copy_kind]);
            Rider_AbilityRemoveModel(rd);     // reset copy_kind + poof VFX/SFX + remove model
            Rider_LoseAbilityState_Enter(rd); // spit-out animation
        }
    }
}

void DropAbility_On3DLoadEnd(void)
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DropAbility_PerFrame, 0, 0, 0, 0);
}

// --- Top Ride ---
// The TR copy-ability analogs are the four ability-power items (Fire, Freeze
// Fan, Bomb, Walky). Each is a timed Kirby state; while active the kirby's
// state_handler carries that item's vtable, which is how we detect a held power.
// Dropping it means reverting to KirbyNormal (TopRide_KirbyNormal), the same
// revert the engine runs when the power expires or is replaced by another.

static const char *DropAbility_TopRidePowerName(void *state_vt)
{
    if (state_vt == TR_ITEMPOWER_VT_FIRE)       return "Fire";
    if (state_vt == TR_ITEMPOWER_VT_FREEZE_FAN) return "Freeze Fan";
    if (state_vt == TR_ITEMPOWER_VT_BOMB)       return "Bomb";
    if (state_vt == TR_ITEMPOWER_VT_WALKY)      return "Walky";
    return 0; // not a droppable ability power
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

        const char *power = DropAbility_TopRidePowerName(*(void **)k->state_handler);
        if (!power)
            continue;

        u8 port = Gm_GetGameData()->topride_config.slots[k->player_slot].controller_port;
        if (port >= 4)
            continue;

        // Rising edge, so each press drops once. The revert flips state_handler
        // off the power vtable, so the power check above also blocks a re-fire.
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
