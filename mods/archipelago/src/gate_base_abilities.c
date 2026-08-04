#include "game.h"
#include "rider.h"
#include "machine.h"
#include "topride.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_base_abilities.h"
#include "textbox_api.h"

static const char *const BaseAbility_Names[BASEABILITY_NUM] = {
    "Inhale",
    "Quick Spin",
    "Charge",
};

// Caller-side bounds checks ensure kind is in [0, BASEABILITY_NUM).
static int IsBaseAbilityLocked(BaseAbilityKind kind)
{
    return (ap_save->base_ability_unlocked_mask & (1 << kind)) == 0;
}

static int RiderIsHuman(RiderData *rd)
{
    return rd && Ply_GetPKind(rd->ply) == PKIND_HMN;
}

static int MachineRiderIsHuman(MachineData *md)
{
    GOBJ *rg = md ? md->rider_gobj : 0;
    return rg && RiderIsHuman((RiderData *)rg->userdata);
}

static int TRKirbyIsHuman(TopRideKirby *k)
{
    return k && TopRide_GetPlayerKind(k->player_slot) == TR_PKIND_HMN;
}

// Replaces the bl Rider_StartInhale at 0x8019c610 (Rider_TryStartInhale).
void GateBaseAbilities_StartInhale(RiderData *rd)
{
    if (IsBaseAbilityLocked(BASEABILITY_INHALE) && RiderIsHuman(rd))
        return;
    Rider_StartInhale(rd);
}

// Replaces both bl Rider_QuickSpin_Enter sites (0x801b7ec0, 0x801b7e58). Kirby only -
// Dedede and Meta Knight have their own enters below.
void GateBaseAbilities_QuickSpinEnter(float f, RiderData *rd, int dir, int flag)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    Rider_QuickSpin_Enter(f, rd, dir, flag);
}

// Dedede and Meta Knight have their own quick-spin enters, one call site each.
void GateBaseAbilities_DededeSpinEnter(RiderData *rd, int dir)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    Rider_Dedede_QuickSpin_Enter(rd, dir);
}

void GateBaseAbilities_MetaKnightSpinEnter(RiderData *rd, int dir)
{
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && RiderIsHuman(rd))
        return;
    Rider_MetaKnight_QuickSpin_Enter(rd, dir);
}

// Replaces every bl Machine_IncrementCharge - generic grounded (0x801ef424,
// 0x801ef350) and the Wheel/wheelie callbacks (0x801fa1d4, 0x801fa29c).
void GateBaseAbilities_IncrementCharge(MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_IncrementCharge(md);
}

// Explicit-rate charge accumulators. rate stays a named param so the compiler
// preserves f1 across the human check before forwarding it.
void GateBaseAbilities_AddCharge(double rate, MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_AddCharge(rate, md);
}

void GateBaseAbilities_AddChargeEx(double rate, MachineData *md)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && MachineRiderIsHuman(md))
        return;
    Machine_AddChargeEx(rate, md);
}

// Conditional-hook body for the inline charge store at 0x802e01b4 (stfs f0,52(r3)) in
// TopRide_ChargeUpdate: r3 = charge component, f1 = post-add value. Always returns 1 so
// the hook's alt exit (0x802e01b8) is taken and the original store never re-runs.
int GateBaseAbilities_TopRideChargeStore(TopRideChargeComponent *comp, float new_value)
{
    if (IsBaseAbilityLocked(BASEABILITY_CHARGE) && TRKirbyIsHuman((TopRideKirby *)comp->kirby_ptr))
        return 1;
    comp->charge_value = new_value;
    return 1;
}

CODEPATCH_HOOKCONDITIONALCREATE(0x802e01b4,
    "fmr 1,0\n\t",
    GateBaseAbilities_TopRideChargeStore,
    "",
    0,
    0x802e01b8)

// Replaces the bl TopRide_KirbyHistoryQuery at 0x802d5f90 in TopRide_KirbyPhysUpdate,
// the stick-flick spin's oscillation query. Returning 0 ("no flick") skips the whole
// spin block. The query receives &kirby->history (kirby+0x64) in r3.
int GateBaseAbilities_TopRideQuickSpinQuery(int *history)
{
    TopRideKirby *k = (TopRideKirby *)((char *)history - 0x64);
    if (IsBaseAbilityLocked(BASEABILITY_QUICKSPIN) && TRKirbyIsHuman(k))
        return 0;
    return TopRide_KirbyHistoryQuery(history);
}

void GateBaseAbilities_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x8019c610, GateBaseAbilities_StartInhale);

    CODEPATCH_REPLACECALL(0x801b7ec0, GateBaseAbilities_QuickSpinEnter);
    CODEPATCH_REPLACECALL(0x801b7e58, GateBaseAbilities_QuickSpinEnter);
    CODEPATCH_REPLACECALL(0x801c05d4, GateBaseAbilities_DededeSpinEnter);
    CODEPATCH_REPLACECALL(0x801c3f6c, GateBaseAbilities_MetaKnightSpinEnter);
    CODEPATCH_REPLACECALL(0x802d5f90, GateBaseAbilities_TopRideQuickSpinQuery);

    CODEPATCH_REPLACECALL(0x801ef424, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801ef350, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801fa1d4, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801fa29c, GateBaseAbilities_IncrementCharge);
    CODEPATCH_REPLACECALL(0x801efa6c, GateBaseAbilities_AddCharge);
    CODEPATCH_REPLACECALL(0x801eb968, GateBaseAbilities_AddChargeEx);
    CODEPATCH_REPLACECALL(0x801f5f30, GateBaseAbilities_AddChargeEx);

    CODEPATCH_HOOKAPPLY(0x802e01b4);

    OSReport("[GateBaseAbilities] Base ability gating hooks installed\n");
}

int GateBaseAbilities_UnlockAbility(BaseAbilityKind kind)
{
    if (kind >= BASEABILITY_NUM)
        return 0;

    ap_save->base_ability_unlocked_mask |= (1 << kind);
    OSReport("[GateBaseAbilities] Base ability %d (%s) unlocked (mask = %s)\n",
             kind, BaseAbility_Names[kind],
             MaskBits(ap_save->base_ability_unlocked_mask, BASEABILITY_NUM));
    tb_api->EnqueueColoredNoun("Unlock: ", BaseAbility_Names[kind], tb_api->DefaultColor, NULL);
    return 1;
}
